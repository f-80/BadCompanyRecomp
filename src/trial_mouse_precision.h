// Invert the verified guest axis deadzone for the MnK look axes only.
#pragma once
#include <cmath>
#include <bit>
#include <atomic>
#include <algorithm>
#include <rex/input/input.h>
#include <rex/input/mnk/mnk_input_driver.h>

REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(bool, mnk_mouse);

namespace trial_mouse_precision {
inline std::atomic<bool> guest_path_seen{false};
inline bool BindingVerified(uint8_t* base) {
  return REX_LOAD_U32(0x821817E8) == 0x3D608200 &&
         REX_LOAD_U32(0x82181878) == 0x7D8802A6 &&
         REX_LOAD_U32(0x82181890) == 0xC0273A80 &&
         REX_LOAD_U32(0x82001F5C) == 0x46FFFE00;
}
inline int Threshold(float fraction) {
  if (!std::isfinite(fraction) || fraction < 0.0f || fraction >= 1.0f) return -1;
  // The guest uses fmuls followed by fctiwz.
  return int(float(fraction * 32767.0f));
}
inline int16_t Encode(int16_t desired, int threshold) {
  if (!desired || threshold <= 0) return desired;
  const int magnitude = desired < 0 ? -int(desired) : int(desired);
  const int range = 32767 - threshold;
  const int encoded = threshold + int((int64_t(magnitude) * range + 32766) / 32767);
  return desired < 0 ? int16_t(-std::min(encoded, 32768)) : int16_t(std::min(encoded, 32767));
}
inline void Adjust(rex::input::X_INPUT_STATE& state) {
  if (!REXCVAR_GET(badco_mouse_deadzone_compensation) || !REXCVAR_GET(mnk_mode) ||
      !REXCVAR_GET(mnk_mouse) || !rex::input::mnk::IsMouseLookActive() ||
      !guest_path_seen.load(std::memory_order_relaxed)) return;
  auto* rt = rex::Runtime::instance();
  auto* base = rt ? rt->virtual_membase() : nullptr;
  if (!base || !BindingVerified(base)) return;
  const float fraction = std::bit_cast<float>(REX_LOAD_U32(0x82BE3A80));
  const int threshold = Threshold(fraction);
  if (threshold < 0 || threshold >= 32767) return;
  state.gamepad.thumb_rx = Encode(int16_t(state.gamepad.thumb_rx), threshold);
  state.gamepad.thumb_ry = Encode(int16_t(state.gamepad.thumb_ry), threshold);
  static std::atomic<bool> reported{false};
  if (!reported.exchange(true))
    REXLOG_INFO("[trial-mouse-precision] binding_verified=true fraction={:.9g} threshold={} right_axes_only=true",
                fraction, threshold);
}

inline void Verify() {
  auto* rt = rex::Runtime::instance();
  auto* memory = rt ? rt->memory() : nullptr;
  auto* base = rt ? rt->virtual_membase() : nullptr;
  if (!memory || !base || !BindingVerified(base)) std::abort();
  const float fraction = std::bit_cast<float>(REX_LOAD_U32(0x82BE3A80));
  const int threshold = Threshold(fraction);
  if (threshold < 0 || threshold >= 32767) std::abort();
  const uint32_t allocation = memory->SystemHeapAlloc(4096);
  if (!allocation) std::abort();
  std::memset(base + allocation, 0xA5, 4096);
  const uint32_t value = allocation + 64;
  PPCContext scratch{};
  scratch.r1.u32 = allocation + 512;
  unsigned max_error = 0;
  bool correct = true;
  int previous = -32769;
  for (int desired = -32768; desired <= 32767; ++desired) {
    const int16_t encoded = Encode(int16_t(desired), threshold);
    REX_STORE_U16(value, uint16_t(encoded));
    scratch.r4.u32 = value;
    scratch.f1.f64 = fraction;
    __imp__sub_821817E8(scratch, base);
    const int actual = int16_t(REX_LOAD_U16(value));
    const unsigned error = unsigned(std::abs(actual - desired));
    max_error = std::max(max_error, error);
    correct &= error <= 1 && actual >= previous;
    if (!desired) correct &= encoded == 0 && actual == 0;
    if (desired < 0) correct &= actual < 0;
    if (desired > 0) correct &= actual > 0;
    previous = actual;
  }
  correct &= REX_LOAD_U8(value - 1) == 0xA5 && REX_LOAD_U8(value + 2) == 0xA5;
  correct &= Encode(32767, threshold) == 32767 && Encode(-32768, threshold) == -32768;
  memory->SystemHeapFree(allocation);
  REXLOG_INFO("[trial-mouse-precision-test] guest_axis_values=65536 threshold={} max_error={} monotonic_sign_zero_guards={}",
              threshold, max_error, correct);
  if (!correct) std::abort();
}
}  // namespace trial_mouse_precision

REX_HOOK_RAW(sub_82181878) {
  __imp__sub_82181878(ctx, base);
#if defined(_WIN32)
  const DWORD outgoing_error = GetLastError();
#endif
  if (!trial_mouse_precision::guest_path_seen.load(std::memory_order_relaxed) &&
      trial_mouse_precision::BindingVerified(base)) {
    trial_mouse_precision::guest_path_seen.store(true, std::memory_order_relaxed);
    REXLOG_INFO("[trial-mouse-precision] guest_deadzone_path_seen=true fraction={:.9g}",
        std::bit_cast<float>(REX_LOAD_U32(0x82BE3A80)));
  }
#if defined(_WIN32)
  SetLastError(outgoing_error);
#endif
}
