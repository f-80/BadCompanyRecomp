// Sample the original MnK driver once per guest frame; retain SDL gamepads.
#pragma once
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include "trial_input_frame.h"
#include "trial_mouse_precision.h"

REXCVAR_DECLARE(std::string, input_backend);

namespace trial_mnk_sampling {
using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;
inline int64_t Now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
class Snapshot {
 public:
  void Reset() { valid_ = false; }
  template<class Reader>
  X_RESULT Read(DeviceId id, uint64_t frame, int64_t now, X_INPUT_STATE* output, Reader&& reader) {
    // Refresh if swap submission stops (menus/loading), without holding stale input.
    if (!valid_ || id != id_ || frame != frame_ || now - sampled_ms_ >= 40) {
      state_ = {};
      result_ = reader(&state_);
      id_ = id;
      frame_ = frame;
      sampled_ms_ = now;
      valid_ = true;
    }
    if (result_ == 0 && output) *output = state_;
    return result_;
  }
 private:
  bool valid_ = false;
  DeviceId id_ = DeviceId::kInvalid;
  uint64_t frame_ = 0;
  int64_t sampled_ms_ = 0;
  X_RESULT result_ = 0;
  X_INPUT_STATE state_{};
};

class Driver final : public InputDriver {
 public:
  Driver() : InputDriver(nullptr, 0), original_(std::make_unique<mnk::MnkInputDriver>(nullptr, 0)) {
    original_->set_is_active_callback([this] { return is_active(); });
  }
  X_STATUS Setup() override { return original_->Setup(); }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override { original_->EnumerateDevices(out); }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* output) override {
    std::lock_guard lock(mutex_);
    ++requests_;
    trial_input_frame::mnk_requests.fetch_add(1, std::memory_order_relaxed);
    return snapshot_.Read(id, trial_input_frame::epoch.load(std::memory_order_relaxed), Now(), output,
        [&](X_INPUT_STATE* state) {
          ++reads_;
          trial_input_frame::mnk_reads.fetch_add(1, std::memory_order_relaxed);
          const auto result = original_->GetDeviceState(id, state);
#if defined(_WIN32)
          const DWORD original_error = GetLastError();
#endif
          if (result == 0) trial_mouse_precision::Adjust(*state);
          if (REXCVAR_GET(badco_shadow_array_diagnostics))
            trial_input_frame::observed_right_trigger.store(
                result == 0 ? state->gamepad.right_trigger : 0, std::memory_order_relaxed);
#if defined(_WIN32)
          SetLastError(original_error);
#endif
          return result;
        });
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out) override {
    return original_->GetDeviceCapabilities(id, flags, out);
  }
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    return original_->SetDeviceVibration(id, vibration);
  }
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out) override {
    return original_->GetDeviceKeystroke(id, flags, out);
  }
  void OnWindowAvailable(rex::ui::Window* window) override {
    original_->OnWindowAvailable(window);
    std::lock_guard lock(mutex_);
    snapshot_.Reset();
  }
  ~Driver() override {
    REXLOG_INFO("[trial-mnk-sampling] state_requests={} original_reads={}", requests_, reads_);
  }
 private:
  std::unique_ptr<mnk::MnkInputDriver> original_;
  std::mutex mutex_;
  Snapshot snapshot_;
  uint64_t requests_ = 0, reads_ = 0;
};

inline void Configure(rex::RuntimeConfig& config) {
  if (!REXCVAR_GET(badco_mnk_frame_sampling)) return;
  const auto fallback = config.input_factory;
  config.input_factory = [fallback](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
    const auto original = [&]() -> std::unique_ptr<rex::system::IInputSystem> {
      return fallback ? fallback(tool_mode) : CreateDefaultInputSystem(tool_mode);
    };
    if (tool_mode || REXCVAR_GET(input_backend) != "sdl") return original();
    // Match the frozen SDK factory: setup SDL and MnK, then add the NOP fallback
    // and the slot assignment. Only MnK GetDeviceState receives a snapshot cache.
    auto system = std::make_unique<InputSystem>(nullptr);
    auto sdl_driver = std::make_unique<sdl::SDLInputDriver>(nullptr, 0);
    if (sdl_driver->Setup() != 0) return original();
    system->AddDriver(std::move(sdl_driver));
    auto mnk_driver = std::make_unique<Driver>();
    if (mnk_driver->Setup() != 0) return original();
    system->AddDriver(std::move(mnk_driver));
    system->AddDriver(std::make_unique<nop::NopInputDriver>(nullptr, 1));
    system->SetDeviceAssignment(std::make_unique<SlotAssignment>());
    REXLOG_INFO("[trial-mnk-sampling] enabled=true; one original MnK sample per guest frame, 40ms fallback; SDL driver retained");
    return system;
  };
}

inline void Verify() {
  Snapshot cache;
  const auto id = static_cast<DeviceId>(0x4D4E4B00);
  X_INPUT_STATE state{}, output{};
  state.gamepad.thumb_rx = 12000;
  state.gamepad.buttons = X_INPUT_GAMEPAD_A;
  unsigned calls = 0;
  X_RESULT status = 0;
  const auto reader = [&](X_INPUT_STATE* out) {
    ++calls;
    *out = state;
    state = {};  // Model the SDK's destructive delta read.
    return status;
  };
  bool correct = cache.Read(id, 7, 0, &output, reader) == 0;
  correct &= int16_t(output.gamepad.thumb_rx) == 12000;
  cache.Read(id, 7, 8, &output, reader);
  correct &= calls == 1 && int16_t(output.gamepad.thumb_rx) == 12000;
  correct &= uint16_t(output.gamepad.buttons) == X_INPUT_GAMEPAD_A;
  cache.Read(id, 8, 9, &output, reader);
  correct &= calls == 2 && int16_t(output.gamepad.thumb_rx) == 0 && uint16_t(output.gamepad.buttons) == 0;
  cache.Read(id, 8, 48, nullptr, reader);
  correct &= calls == 2;
  cache.Read(id, 8, 49, &output, reader);
  correct &= calls == 3;
  cache.Reset();
  status = 0x48F;
  output.gamepad.thumb_rx = 4321;
  correct &= cache.Read(id, 8, 50, &output, reader) == status;
  correct &= int16_t(output.gamepad.thumb_rx) == 4321;
  cache.Reset();
  status = 0;
  state.gamepad.thumb_rx = -9000;
  cache.Read(id, 8, 51, nullptr, reader);
  cache.Read(id, 8, 52, &output, reader);
  correct &= calls == 5 && int16_t(output.gamepad.thumb_rx) == -9000;
  REXLOG_INFO("[trial-mnk-sampling-test] repeated_polls_frame_advance_expiry_reset_error_null_output={}", correct);
  if (!correct) std::abort();
}
}  // namespace trial_mnk_sampling
