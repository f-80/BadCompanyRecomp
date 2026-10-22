// Opt-in, bounded command-processor snapshots for the frozen Xenos plugin.
#pragma once
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <fstream>
#include <vector>
#include "trial_shadow_array.h"
#include "trial_readback32.h"

namespace trial_shadow_gpu {
using Processor = rex::graphics::CommandProcessor;
using Registers = rex::graphics::RegisterFile;
using Draw = bool (*)(Processor*, uint32_t, uint32_t, void*, bool);
using Copy = bool (*)(Processor*);
using Swap = void (*)(Processor*, uint32_t, uint32_t, uint32_t);
using Control = void (*)(Processor*);
inline Draw original_draw = nullptr;
inline Copy original_copy = nullptr;
inline Swap original_swap = nullptr;
inline Control original_clear = nullptr, original_shutdown = nullptr;
inline Processor* processor = nullptr;
inline Registers* registers = nullptr;
// Preserve RTTI at [-1] and all 22 original virtual slots, including destruction.
inline std::array<void*, 23> table{};
inline std::atomic<unsigned> request{0};
inline unsigned seen_request = 0, frame = 0, start_frame = 0, captured_frames = 0;
inline uint64_t draws_forwarded = 0, copies_forwarded = 0;
inline bool installed = false, collecting = false, overflow = false;
constexpr size_t kMaxRecords = 4096;

struct Record {
  uint32_t kind, frame, ordinal, primitive, count, accepted;
  uint64_t tick_ms, guest_epoch, vertex_hash, pixel_hash;
  uint32_t state[0x400];       // Registers 0x2000..0x23FF.
  uint32_t constants[0x800];   // 512 float4 ALU registers, VS then PS.
  uint32_t fetch[192];        // All 32 six-word fetch descriptors.
};
static_assert(sizeof(Record) == 13112);
struct Header {
  uint32_t magic = 0x38324753, version = 1, record_bytes = sizeof(Record), records = 0;
  uint32_t capture = 0, frames = 0, truncated = 0, reserved = 0;
};
static_assert(sizeof(Header) == 32);
inline std::vector<Record> records;
using Clock = std::chrono::steady_clock;
inline unsigned slow_call_records = 0;
inline void SlowCall(const char* kind, Clock::time_point start, Clock::time_point finish,
                     uint64_t source_hash = 0, double preparation_ms = 0) {
  const double elapsed = std::chrono::duration<double, std::milli>(finish - start).count();
  if (elapsed < 50 || slow_call_records >= 96) return;
  ++slow_call_records;
  REXLOG_INFO("[trial-gpu-slow] kind={} frame={} elapsed_ms={:.3f} preparation_ms={:.3f} source_ps={:016X} readback={}",
              kind, frame, elapsed, preparation_ms, source_hash, rex::cvar::GetFlagByName("readback_resolve"));
  if (registers && std::strcmp(kind, "resolve") == 0)
    REXLOG_INFO("[trial-resolve-state] frame={} control={:08X} destination={:08X} pitch={:08X} info={:08X} surface={:08X} depth={:08X} depth_fast_enabled={} switched_copies={} hdr_fast_enabled={}",
                frame, registers->values[0x2318], registers->values[0x2319],
                registers->values[0x231A], registers->values[0x231B],
                registers->values[0x2000], registers->values[0x2002],
                REXCVAR_GET(badco_shadow_depth_readback_fast) && trial_readback32::enabled.load(std::memory_order_relaxed), trial_readback32::switched_copies,
                REXCVAR_GET(badco_hdr_readback_fast) && trial_readback32::enabled.load(std::memory_order_relaxed));
}
inline void ClearHook(Processor* self) {
  const DWORD incoming = GetLastError();
  trial_shadow_array::ClearReady("gpu_cache_clear");
  SetLastError(incoming);
  original_clear(self);
}
inline void ShutdownHook(Processor* self) {
  original_shutdown(self);  // Stop the command worker before touching its cache.
  const DWORD outgoing = GetLastError();
  trial_shadow_array::ClearReady("gpu_shutdown");
  SetLastError(outgoing);
}

inline void Finish() {
  collecting = false;
  const Header header{0x38324753, 1, sizeof(Record), uint32_t(records.size()),
                      seen_request, captured_frames, uint32_t(overflow), 0};
  const auto path = std::filesystem::path(REXCVAR_GET(cache_root)) /
      ("shadow_gpu28_" + std::to_string(trial_shadow_trace::Now()) + ".bin");
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));
  file.write(reinterpret_cast<const char*>(records.data()), records.size() * sizeof(Record));
  file.close();
  REXLOG_INFO("[trial-shadow-gpu] capture={} records={} complete_frames={} truncated={} written={} path={} forwarded_draws={} forwarded_copies={}",
      seen_request, records.size(), captured_frames, overflow, !file.fail(), path.string(),
      draws_forwarded, copies_forwarded);
  rex::FlushLogging();
}
inline Record* Before(uint32_t kind, uint32_t primitive, uint32_t count) {
  if (!collecting || (frame != start_frame && frame != start_frame + 60)) return nullptr;
  if (records.size() == kMaxRecords) { overflow = true; return nullptr; }
  auto& record = records.emplace_back();
  record.kind = kind;
  record.frame = frame;
  record.ordinal = uint32_t(records.size() - 1);
  record.primitive = primitive;
  record.count = count;
  record.tick_ms = trial_shadow_trace::Now();
  record.guest_epoch = trial_input_frame::epoch.load(std::memory_order_relaxed);
  const auto* vs = processor->active_vertex_shader();
  const auto* ps = processor->active_pixel_shader();
  record.vertex_hash = vs ? vs->ucode_data_hash() : 0;
  record.pixel_hash = ps ? ps->ucode_data_hash() : 0;
  std::memcpy(record.state, &registers->values[0x2000], sizeof(record.state));
  std::memcpy(record.constants, &registers->values[0x4000], sizeof(record.constants));
  std::memcpy(record.fetch, &registers->values[0x4800], sizeof(record.fetch));
  return &record;
}
inline bool DrawHook(Processor* self, uint32_t primitive, uint32_t count,
                     void* indices, bool explicit_mode) {
  const DWORD incoming = GetLastError();
  const bool timed = REXCVAR_GET(badco_performance_diagnostics);
  const auto start = timed ? Clock::now() : Clock::time_point{};
  const uint64_t source_hash = timed && self->active_pixel_shader() ? self->active_pixel_shader()->ucode_data_hash() : 0;
  trial_shadow_array::Scope shadow_array(self, registers);
  auto* record = Before(0, primitive, count);
  const auto prepared = timed ? Clock::now() : Clock::time_point{};
  SetLastError(incoming);
  const bool accepted = original_draw(self, primitive, count, indices, explicit_mode);
  const DWORD outgoing = GetLastError();
  if (timed) SlowCall("draw", start, Clock::now(), source_hash,
                      std::chrono::duration<double, std::milli>(prepared - start).count());
  if (record) record->accepted = accepted;
  ++draws_forwarded;
  SetLastError(outgoing);
  return accepted;
}
inline bool CopyHook(Processor* self) {
  const DWORD incoming = GetLastError();
  const bool timed = REXCVAR_GET(badco_performance_diagnostics);
  const auto start = timed ? Clock::now() : Clock::time_point{};
  auto* record = Before(1, 0, 0);
  bool accepted;
  DWORD outgoing;
  {
    trial_readback32::Scope readback(registers->values[0x2318], registers->values[0x231A], registers->values[0x231B],
        REXCVAR_GET(badco_shadow_depth_readback_fast) && trial_readback32::enabled.load(std::memory_order_relaxed),
        REXCVAR_GET(badco_hdr_readback_fast) && trial_readback32::enabled.load(std::memory_order_relaxed));
    SetLastError(incoming);
    accepted = original_copy(self);
    outgoing = GetLastError();
    if (timed) SlowCall("resolve", start, Clock::now());
  }
  if (record) record->accepted = accepted;
  ++copies_forwarded;
  SetLastError(outgoing);
  return accepted;
}
inline void SwapHook(Processor* self, uint32_t address, uint32_t width, uint32_t height) {
  const bool timed = REXCVAR_GET(badco_performance_diagnostics);
  const auto start = timed ? Clock::now() : Clock::time_point{};
  original_swap(self, address, width, height);
  const DWORD outgoing = GetLastError();
  if (timed) SlowCall("present", start, Clock::now());
  static bool output_probe_done = false;
  if (REXCVAR_GET(badco_output_size_probe) && !output_probe_done && frame >= 30) {
    output_probe_done = true;
    auto* graphics = rex::Runtime::instance()->graphics_system();
    rex::ui::RawImage image;
    const bool captured = graphics && graphics->presenter() && graphics->presenter()->CaptureGuestOutput(image);
    REXLOG_INFO("[trial-output-size] captured={} source={}x{} bytes={} requested_scale={}x{} display_window={}x{}",
                captured, image.width, image.height, image.data.size(),
                rex::cvar::GetFlagByName("draw_resolution_scale_x"), rex::cvar::GetFlagByName("draw_resolution_scale_y"),
                rex::cvar::GetFlagByName("window_width"), rex::cvar::GetFlagByName("window_height"));
  }
  trial_shadow_array::Probe(self);
  trial_readback32::Probe(self);
  trial_shadow_array::Report();
  if (collecting && (frame == start_frame || frame == start_frame + 60)) {
    ++captured_frames;
    if (captured_frames == 2) Finish();
  }
  ++frame;
  const auto requested = request.load(std::memory_order_acquire);
  if (!collecting && requested != seen_request) {
    records.clear();
    seen_request = requested;
    start_frame = frame;
    captured_frames = 0;
    overflow = false;
    collecting = true;
    REXLOG_INFO("[trial-shadow-gpu] capture={} begins at next complete GPU command frame={}; second frame after 60 swaps",
                seen_request, frame);
  }
  SetLastError(outgoing);
}
inline bool Install() {
  if (!REXCVAR_GET(badco_shadow_gpu_capture) && !REXCVAR_GET(badco_shadow_array_fetch_fix)) return false;
  auto* runtime = rex::Runtime::instance();
  auto* graphics = runtime ? runtime->graphics_system() : nullptr;
  const auto plugin = GetModuleHandleW(L"rexgpu-xenosrd.dll");
  if (!graphics || !plugin) return false;
  MEMORY_BASIC_INFORMATION info{};
  const auto graphics_table = *reinterpret_cast<void***>(graphics);
  if (!VirtualQuery(graphics_table, &info, sizeof(info)) || info.AllocationBase != plugin) return false;
  auto* xenos = static_cast<rex::graphics::GraphicsSystem*>(graphics);
  processor = xenos->command_processor();
  registers = xenos->register_file();
  if (!processor || xenos->memory() != runtime->memory()) return false;
  auto** original = *reinterpret_cast<void***>(processor);
  const uintptr_t module = reinterpret_cast<uintptr_t>(plugin);
  constexpr std::array<uint32_t, 22> expected{
      0xAF910, 0x17C60, 0x18030, 0x9C280, 0x9C2E0, 0xA3390,
      0x9C380, 0x9E560, 0xA1BC0, 0xA2DE0, 0xA2EE0, 0x19840,
      0xA3370, 0xA3380, 0x199D0, 0x19DB0, 0x19DC0, 0xA4C60,
      0x9C3C0, 0xA4CE0, 0xA4D00, 0xAAE00};
  bool correct = reinterpret_cast<uintptr_t>(original) == module + 0x1BF318;
  if (correct)
    for (size_t i = 0; i != expected.size(); ++i)
      correct &= reinterpret_cast<uintptr_t>(original[i]) == module + expected[i];
  // Independently check the protected base prefix before using inline accessors.
  void* prefix[5]{};
  std::memcpy(prefix, static_cast<const void*>(processor), sizeof(prefix));
  correct &= prefix[1] == runtime->memory() && prefix[3] == xenos && prefix[4] == registers;
  if (!correct) {
    REXLOG_ERROR("[trial-shadow-gpu] frozen plugin binding mismatch; observer not installed");
    return false;
  }
  if (REXCVAR_GET(badco_shadow_gpu_capture)) records.reserve(kMaxRecords);
  std::copy_n(original - 1, table.size(), table.begin());
  original_swap = reinterpret_cast<Swap>(original[5]);
  original_shutdown = reinterpret_cast<Control>(original[2]);
  original_clear = reinterpret_cast<Control>(original[3]);
  original_draw = reinterpret_cast<Draw>(original[20]);
  original_copy = reinterpret_cast<Copy>(original[21]);
  trial_shadow_array::loader = reinterpret_cast<trial_shadow_array::Loader>(original[19]);
  table[6] = reinterpret_cast<void*>(&SwapHook);
  table[3] = reinterpret_cast<void*>(&ShutdownHook);
  table[4] = reinterpret_cast<void*>(&ClearHook);
  table[21] = reinterpret_cast<void*>(&DrawHook);
  table[22] = reinterpret_cast<void*>(&CopyHook);
  auto* previous = InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(processor), table.data() + 1, original);
  installed = previous == original;
  REXLOG_INFO("[trial-shadow-gpu] binding_verified=true installed={} capture_enabled={} array_fetch_experiment={} DLL_bytes_unchanged=true max_records={}",
              installed, REXCVAR_GET(badco_shadow_gpu_capture), REXCVAR_GET(badco_shadow_array_fetch_fix), kMaxRecords);
  return installed;
}
inline void Request() {
  if (installed && REXCVAR_GET(badco_shadow_gpu_capture)) {
    request.fetch_add(1, std::memory_order_release);
    REXLOG_INFO("[trial-shadow-gpu] two complete command frames requested; constants, shaders and fetch descriptors");
  }
}
}  // namespace trial_shadow_gpu
