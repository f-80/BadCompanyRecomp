// game_fixes.h - Guest-function overrides and fixes for Battlefield: Bad Company
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include "game_cvars.h"

#include "generated/default/badcompanyrecomp_pch.h"
#include "generated/default/badcompanyrecomp_funcs.h"

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include "trial_physical_trace.h"
#include "trial_file_trace.h"
#include "trial_cs_semaphore.h"
#include <rex/chrono/clock.h>
#include "trial_input_frame.h"
#include "trial_mouse_precision.h"
#include "camera_fov.h"

REX_HOOK_RAW(sub_827F2A20) {
  badco_camera_fov::Call(ctx, base);
}

namespace trial_shadow {
inline std::atomic<int> mode{-1};
struct Observed { uint8_t original; int applied = -2; };
inline std::mutex settings_mutex;
inline std::unordered_map<uint32_t, Observed> observed;

inline bool BindingVerified(uint8_t* base) {
  // WorldRenderSettings is held at renderer+152 in both cascade paths.
  // The original field descriptor identifies a bool at +216.
  static const bool verified =
      REX_LOAD_U32(0x82E88538) == 0x82063178 &&
      REX_LOAD_U32(0x82E88548) == 216 &&
      REX_LOAD_U32(0x827EA248) == 0x8BCB00D8 &&
      REX_LOAD_U32(0x827D6010) == 0x896B00D8;
  return verified;
}

inline void Apply(PPCContext& ctx, uint8_t* base) {
  if (!BindingVerified(base)) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true))
      REXLOG_ERROR("[trial-shadow] Field binding mismatch; original shadow settings retained");
    return;
  }
  const uint32_t settings = REX_LOAD_U32(ctx.r3.u32 + 152);
  if (!settings) return;
  std::lock_guard lock(settings_mutex);
  auto [it, inserted] = observed.try_emplace(settings, Observed{REX_LOAD_U8(settings + 216)});
  auto& state = it->second;
  const int selected = mode.load(std::memory_order_relaxed);
  if (state.applied == selected) return;
  const uint8_t effective = selected < 0 ? state.original : static_cast<uint8_t>(selected);
  // Default mode is observational: do not write until the user selects a test.
  if (selected >= 0 || state.applied >= 0)
    REX_STORE_U8(settings + 216, effective);
  state.applied = selected;
  REXLOG_INFO("[trial-shadow] binding_verified=true WorldRenderSettings={:08X} original_fixed_movement={} effective={} mode={}",
              settings, state.original, effective, selected);
}

inline void Verify() {
  auto* rt = rex::Runtime::instance();
  auto* memory = rt ? rt->memory() : nullptr;
  uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  if (!memory || !base || !BindingVerified(base)) {
    REXLOG_WARN("[trial-shadow-test] binding unavailable; transition test skipped");
    return;
  }
  const uint32_t allocation = memory->SystemHeapAlloc(1024);
  if (!allocation) std::abort();
  const int saved_mode = mode.load();
  bool correct = true;
  for (unsigned original = 0; original != 2; ++original) {
    const uint32_t renderer = allocation + original * 512;
    const uint32_t settings = renderer + 256;
    std::memset(base + renderer, 0xA5, 512);
    REX_STORE_U32(renderer + 152, settings);
    REX_STORE_U8(settings + 216, original);
    PPCContext scratch{};
    scratch.r3.u32 = renderer;
    for (int selected : {-1, 1, 0, -1}) {
      mode.store(selected);
      Apply(scratch, base);
      correct &= REX_LOAD_U8(settings + 216) == (selected < 0 ? original : selected);
      correct &= REX_LOAD_U8(settings + 215) == 0xA5 && REX_LOAD_U8(settings + 217) == 0xA5;
    }
    std::lock_guard lock(settings_mutex);
    observed.erase(settings);
  }
  mode.store(saved_mode);
  memory->SystemHeapFree(allocation);
  REXLOG_INFO("[trial-shadow-test] both_original_values_restore={} adjacent_flags_untouched={}", correct, correct);
  if (!correct) std::abort();
}
}  // namespace trial_shadow

#include "trial_shadow_trace.h"
#include "trial_shadow_upload.h"
#include "trial_shadow_semantics.h"
#include "trial_shadow_gpu.h"

REX_HOOK_RAW(sub_827D5B68) {
  trial_shadow::Apply(ctx, base);
  __imp__sub_827D5B68(ctx, base);
}
REX_HOOK_RAW(sub_827E9AA0) {
  trial_shadow::Apply(ctx, base);
  __imp__sub_827E9AA0(ctx, base);
}

// GlobalPostProcessSettings.FilmGrainEnable is registered at byte offset 256
// by sub_82B77078. The renderer at 82824334 reads it via *(renderer + 12).
// Scope the override to the original renderer call, preserving serialized data
// and the other post-processing options (tonemapping, bloom, DOF, vignette).
REX_HOOK_RAW(sub_82823650) {
  if (!REXCVAR_GET(badco_disable_film_grain)) {
    __imp__sub_82823650(ctx, base);
    return;
  }
  // Check the live field registration and the original flag-load instruction
  // before using the offset tied to this XEX revision.
  static const bool binding_verified =
      REX_LOAD_U32(0x82E8B358) == 0x8206729C &&
      REX_LOAD_U32(0x82E8B368) == 256 &&
      REX_LOAD_U32(0x82824334) == 0x89670100;
  if (!binding_verified) {
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true))
      REXLOG_ERROR("[trial-grain] Field binding mismatch; original post-processing retained");
    __imp__sub_82823650(ctx, base);
    return;
  }
  const uint32_t settings = REX_LOAD_U32(ctx.r3.u32 + 12);
  const uint32_t flag_address = settings + 256;
  const uint8_t original_flag = REX_LOAD_U8(flag_address);
  struct RestoreFlag {
    uint8_t* address;
    uint8_t value;
    ~RestoreFlag() { *address = value; }
  } restore{base + flag_address, original_flag};
  REX_STORE_U8(flag_address, 0);
  static std::atomic<bool> reported{false};
  if (!reported.exchange(true)) {
    REXLOG_INFO("[trial-grain] binding_verified=true GlobalPostProcessSettings={:08X} FilmGrainEnable={} -> 0; scoped renderer override",
                settings, original_flag);
  }
  __imp__sub_82823650(ctx, base);
}

#if defined(_WIN32)
REX_HOOK_RAW(__imp__RtlLeaveCriticalSection) {
  const DWORD incoming_error = GetLastError();
  static PPCFunc* original = trial_physical::Original("__imp__RtlLeaveCriticalSection");
  trial_cs_semaphore::last.active = true;
  const bool handled = trial_cs_semaphore::TryLeave(ctx, base);
  SetLastError(incoming_error);
  if (!handled) original(ctx, base);
  trial_cs_semaphore::last.active = false;
}

REX_HOOK_RAW(__imp__NtReadFile) { trial_file::Call(ctx, base, false); }
REX_HOOK_RAW(__imp__NtReadFileScatter) { trial_file::Call(ctx, base, true); }
REX_HOOK_RAW(__imp__XamShowDirtyDiscErrorUI) { trial_file::DirtyDisc(ctx, base); }

REX_HOOK_RAW(__imp__VdGetCurrentDisplayGamma) {
  const DWORD incoming_error = GetLastError();
  const uint32_t type_address = ctx.r3.u32;
  static PPCFunc* original = trial_physical::Original("__imp__VdGetCurrentDisplayGamma");
  SetLastError(incoming_error);
  original(ctx, base);
  const DWORD returned_error = GetLastError();
  const uint32_t sdk_type = REX_LOAD_U32(type_address);
  const uint32_t selected_type = REXCVAR_GET(badco_display_gamma_type);
  REX_STORE_U32(type_address, selected_type);
  static std::atomic<bool> reported{false};
  if (!reported.exchange(true))
    REXLOG_INFO("[trial-display] gamma SDK={} selected={} (1=sRGB monitor, 2=BT.709 TV)",
                sdk_type, selected_type);
  SetLastError(returned_error);
}

REX_HOOK_RAW(__imp__MmAllocatePhysicalMemoryEx)
{
  trial_physical::Call(ctx, base, true);
}
REX_HOOK_RAW(__imp__MmFreePhysicalMemory)
{
  trial_physical::Call(ctx, base, false);
}

// Count guest swap submissions without pacing or changing the SDK call.
REX_HOOK_RAW(__imp__VdSwap)
{
  const DWORD incoming_error = GetLastError();
  static PPCFunc *original = [] {
    auto module = GetModuleHandleW(L"rexruntimerd.dll");
    auto fn = module ? reinterpret_cast<PPCFunc*>(GetProcAddress(module, "__imp__VdSwap")) : nullptr;
    if (!fn) { REXLOG_CRITICAL("[trial-timing] Original VdSwap unavailable"); std::abort(); }
    return fn;
  }();
  SetLastError(incoming_error);
  const bool timed = REXCVAR_GET(badco_timing_diagnostics);
  const auto submitted = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  original(ctx, base);
  const DWORD returned_error = GetLastError();
  const auto finished = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  trial_input_frame::epoch.fetch_add(1, std::memory_order_relaxed);
  if (!timed) { SetLastError(returned_error); return; }
  thread_local auto previous = submitted;
  thread_local double longest_interval = 0.0, longest_swap = 0.0;
  thread_local unsigned slow_intervals = 0;
  const double interval = std::chrono::duration<double, std::milli>(submitted - previous).count();
  previous = submitted;
  longest_interval = std::max(longest_interval, interval);
  longest_swap = std::max(longest_swap,
      std::chrono::duration<double, std::milli>(finished - submitted).count());
  if (interval > 50.0) ++slow_intervals;
  thread_local ULONGLONG start = GetTickCount64();
  thread_local unsigned count = 0;
  ++count;
  const ULONGLONG now = GetTickCount64();
  if (now - start >= 2000)
  {
      REXLOG_INFO("[trial-timing] guest_swaps={} elapsed_ms={} swaps_per_second={:.2f} guest_time_scalar={:.3f} max_interval_ms={:.2f} max_swap_ms={:.2f} intervals_over_50ms={} mnk_requests={} mnk_reads={}",
                  count, now - start, double(count) * 1000.0 / double(now - start),
                  rex::chrono::Clock::guest_time_scalar(), longest_interval, longest_swap, slow_intervals,
                  trial_input_frame::mnk_requests.exchange(0, std::memory_order_relaxed),
                  trial_input_frame::mnk_reads.exchange(0, std::memory_order_relaxed));
    longest_interval = longest_swap = 0.0;
    slow_intervals = 0;
    start = now;
    count = 0;
  }
  SetLastError(returned_error);
}
#endif

#if defined(_WIN32)
namespace trial_cs {
inline PPCFunc* Original(bool spin) {
  static PPCFunc* plain = trial_physical::Original("__imp__RtlInitializeCriticalSection");
  static PPCFunc* counted = trial_physical::Original("__imp__RtlInitializeCriticalSectionAndSpinCount");
  return spin ? counted : plain;
}
inline void Initialize(PPCContext& ctx, uint8_t* base, bool spin) {
  const uint32_t address = ctx.r3.u32;
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  Original(spin)(ctx, base);
  const DWORD saved_error = GetLastError();
  // A newly initialized critical section must not retain the previous object's
  // lazy native-object cache in the dispatch header's wait-list fields.
  const uint32_t signature = REX_LOAD_U32(address + 8);
  const uint32_t handle = REX_LOAD_U32(address + 12);
  REX_STORE_U32(address + 8, 0);
  REX_STORE_U32(address + 12, 0);
  if (signature == 0x52455800) {
    static std::atomic<unsigned> reports{0};
    if (reports.fetch_add(1, std::memory_order_relaxed) < 64) {
      REXLOG_INFO("[trial-cs] initialized={:08X} stale_handle={:08X} parent_LR={:08X} spin={}; cleared old dispatch cache",
                  address, handle, lr, spin);
    }
  }
  SetLastError(saved_error);
}
inline void VerifyInitialization() {
  auto* rt = rex::Runtime::instance();
  auto* mem = rt ? rt->memory() : nullptr;
  if (!mem) { std::abort(); }
  const uint32_t address = mem->SystemHeapAlloc(32);
  if (!address) { std::abort(); }
  uint8_t* base = rt->virtual_membase();
  PPCContext ctx{};
  bool original_preserved[2]{}, corrected[2]{};
  for (unsigned mode = 0; mode != 2; ++mode) {
    for (unsigned off = 0; off < 32; off += 4) REX_STORE_U32(address + off, 0);
    REX_STORE_U32(address + 8, 0x52455800);
    REX_STORE_U32(address + 12, 0xF8FFFFFC);
    ctx.r3.u64 = address;
    ctx.r4.u64 = 256;
    Original(mode != 0)(ctx, base);
    original_preserved[mode] = REX_LOAD_U32(address + 8) == 0x52455800 &&
                               REX_LOAD_U32(address + 12) == 0xF8FFFFFC;
    ctx.r3.u64 = address;
    ctx.r4.u64 = 256;
    Initialize(ctx, base, mode != 0);
    corrected[mode] = REX_LOAD_U32(address + 8) == 0 && REX_LOAD_U32(address + 12) == 0 &&
                       REX_LOAD_U32(address + 16) == 0xFFFFFFFF &&
                       REX_LOAD_U32(address + 20) == 0 && REX_LOAD_U32(address + 24) == 0 &&
                       REX_LOAD_U8(address) == 1;
  }
  mem->SystemHeapFree(address);
  REXLOG_INFO("[trial-cs-test] original_preserved_stale_cache_plain={} spin={}; corrected_plain={} spin={}",
              original_preserved[0], original_preserved[1], corrected[0], corrected[1]);
  rex::FlushLogging();
  if (!corrected[0] || !corrected[1]) std::abort();
}
} // namespace trial_cs
REX_HOOK_RAW(__imp__RtlInitializeCriticalSection) { trial_cs::Initialize(ctx, base, false); }
REX_HOOK_RAW(__imp__RtlInitializeCriticalSectionAndSpinCount) { trial_cs::Initialize(ctx, base, true); }
#endif

namespace game_fixes
{
  // ---------------------------------------------------------------------------
  // 1. Serialize XInput calls across multiple guest threads
  // ---------------------------------------------------------------------------
  inline std::mutex g_input_mutex;
  inline std::atomic<uint32_t> g_input_contention{0};

  class InputLock
  {
  public:
    InputLock()
    {
      if (!g_input_mutex.try_lock())
      {
        if (g_input_contention.fetch_add(1, std::memory_order_relaxed) == 0)
        {
          REXLOG_WARN("[fix] XamInput called from two guest threads simultaneously; serialized");
        }
        g_input_mutex.lock();
      }
    }
    ~InputLock() { g_input_mutex.unlock(); }
    InputLock(const InputLock &) = delete;
    InputLock &operator=(const InputLock &) = delete;
  };
} // namespace game_fixes

// Observe the restored size getter used by sub_821D24F8. Keep its real result.
REX_HOOK_RAW(sub_826F3658)
{
  const uint32_t object = ctx.r3.u32;
  const uint32_t parent_lr = static_cast<uint32_t>(ctx.lr);
  __imp__sub_826F3658(ctx, base);
#if defined(_WIN32)
  const DWORD saved_error = GetLastError();
#endif
  static std::atomic<unsigned> reported{0};
  if (reported.fetch_add(1, std::memory_order_relaxed) < 16)
  {
    REXLOG_INFO("[trial-size] sub_826F3658: object={:08X} parent_LR={:08X} result={} expected=1024",
                object, parent_lr, ctx.r3.u32);
    rex::FlushLogging();
  }
#if defined(_WIN32)
  SetLastError(saved_error);
#endif
}

// Log the guest caller before the original CRT pure-virtual failure path.
REX_HOOK_RAW(sub_82904B50)
{
#if defined(_WIN32)
  const DWORD saved_error = GetLastError();
#endif
  REXLOG_ERROR("[trial-purecall] parent_LR={:08X} ctr={:08X} sp={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X}; executing original CRT failure",
               static_cast<uint32_t>(ctx.lr), ctx.ctr.u32,
               ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
  rex::FlushLogging();
#if defined(_WIN32)
  SetLastError(saved_error);
#endif
  __imp__sub_82904B50(ctx, base);
}

// Identify a failed resource lookup before descriptor construction; keep result.
REX_HOOK_RAW(sub_82383968)
{
  const uint32_t parent_lr = static_cast<uint32_t>(ctx.lr);
  const uint32_t manager = ctx.r3.u32;
  const uint32_t resource_id = ctx.r4.u32;
  __imp__sub_82383968(ctx, base);
  if (ctx.r3.u32 == 0 && parent_lr == 0x821ECF9C)
  {
#if defined(_WIN32)
    const DWORD saved_error = GetLastError();
#endif
    static std::atomic<unsigned> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 32)
    {
      REXLOG_ERROR("[trial-resource] lookup returned zero: manager={:08X} resource_id={:08X} parent_LR={:08X}; original result preserved",
                   manager, resource_id, parent_lr);
      rex::FlushLogging();
    }
#if defined(_WIN32)
    SetLastError(saved_error);
#endif
  }
}

// Trace null descriptor construction without changing the constructor.
REX_HOOK_RAW(sub_821BEBB8)
{
#if defined(_WIN32)
  if (ctx.r4.u32 == 0)
  {
    const DWORD saved_error = GetLastError();
    static std::atomic<unsigned> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 64)
    {
      REXLOG_WARN("[trial-ctor] sub_821BEBB8: destination={:08X} source=0 parent_LR={:08X} sp={:08X}; executing original",
                  ctx.r3.u32, static_cast<uint32_t>(ctx.lr), ctx.r1.u32);
      rex::FlushLogging();
    }
    SetLastError(saved_error);
  }
#endif
  __imp__sub_821BEBB8(ctx, base);
}

// Trace the caller supplying an empty descriptor to the original lookup chain.
REX_HOOK_RAW(sub_821EAEB8)
{
#if defined(_WIN32)
  const DWORD saved_error = GetLastError();
  const uint32_t item = ctx.r3.u32;
  uint32_t object_be = 0, state_be = 0;
  SIZE_T copied = 0;
  const bool object_ok = ReadProcessMemory(GetCurrentProcess(),
      REX_RAW_ADDR(item + 44), &object_be, sizeof(object_be), &copied) && copied == sizeof(object_be);
  const bool state_ok = ReadProcessMemory(GetCurrentProcess(),
      REX_RAW_ADDR(item + 60), &state_be, sizeof(state_be), &copied) && copied == sizeof(state_be);
  if (object_ok && state_ok && object_be == 0 && state_be == 0)
  {
    static std::atomic<unsigned> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 32)
    {
      REXLOG_ERROR("[trial-lookup] sub_821EAEB8: item={:08X} field44=0 state60=0 parent_LR={:08X} sp={:08X}; executing original",
                   item, static_cast<uint32_t>(ctx.lr), ctx.r1.u32);
      rex::FlushLogging();
    }
  }
  SetLastError(saved_error);
#endif
  __imp__sub_821EAEB8(ctx, base);
}

// Observe the invalid object before its original virtual call; do not bypass it.
REX_HOOK_RAW(sub_821DEBE0)
{
#if defined(_WIN32)
  const DWORD saved_error = GetLastError();
  const uint32_t item = ctx.r4.u32;
  uint32_t object_be = 0, state_be = 0;
  SIZE_T copied = 0;
  const bool object_ok = ReadProcessMemory(GetCurrentProcess(),
      REX_RAW_ADDR(item + 44), &object_be, sizeof(object_be), &copied) && copied == sizeof(object_be);
  const bool state_ok = ReadProcessMemory(GetCurrentProcess(),
      REX_RAW_ADDR(item + 60), &state_be, sizeof(state_be), &copied) && copied == sizeof(state_be);
  if (object_ok && state_ok && object_be == 0 && state_be == 0)
  {
    static std::atomic<unsigned> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 16)
    {
      REXLOG_ERROR("[trial-object] sub_821DEBE0: manager={:08X} item={:08X} field44=0 state60=0 parent_LR={:08X}; executing original",
                  ctx.r3.u32, item, static_cast<uint32_t>(ctx.lr));
      rex::FlushLogging();
    }
  }
  SetLastError(saved_error);
#endif
  __imp__sub_821DEBE0(ctx, base);
}

// ---------------------------------------------------------------------------
// 2. sub_82907DB0: CRT Exit / Abort Diagnosis Handler
// ---------------------------------------------------------------------------
// Called by Frostbite when an unrecoverable engine condition occurs.
// Logs full register and caller context before terminating via KeBugCheck.
// ---------------------------------------------------------------------------
REX_HOOK_RAW(sub_82907DB0)
{
  REXLOG_ERROR("=== FROSTBITE CRT SHUTDOWN TRIGGERED (sub_82907DB0) ===");
  REXLOG_ERROR("  r3 (exit code): 0x{:08X}", ctx.r3.u32);
  REXLOG_ERROR("  r4:             0x{:08X}", ctx.r4.u32);
  REXLOG_ERROR("  r5:             0x{:08X}", ctx.r5.u32);
  REXLOG_ERROR("  Caller LR:      0x{:08X}", (uint32_t)ctx.lr);
  rex::FlushLogging();

  // Call the generated original CRT termination function
  __imp__sub_82907DB0(ctx, base);
}

// ---------------------------------------------------------------------------
// 3. sub_8292BDA0: Frostbite Storage & Cache Partition Check
// ---------------------------------------------------------------------------
// Attempts to open and dismount raw HDD cache partitions (\Device\Harddisk0\Cache0).
// In ReXGlue VFS, these partitions resolve to NullDevice.
// Returning STATUS_SUCCESS (0) emulates an Xbox 360 Core/Arcade SKU (No-HDD)
// so the engine proceeds with direct streaming from GAME:\ without panicking.
// ---------------------------------------------------------------------------
REX_HOOK_RAW(sub_8292BDA0)
{
  REXLOG_INFO("[fix] sub_8292BDA0 called (HDD Cache container open/dismount); returning SUCCESS to emulate No-HDD SKU");
  ctx.r3.s64 = 0; // STATUS_SUCCESS (0)
}

// ---------------------------------------------------------------------------
// 4. sub_82591430: Frostbite State / Worker Dispatcher
// ---------------------------------------------------------------------------
REX_HOOK_RAW(sub_82591430)
{
  uint32_t obj = ctx.r3.u32;
  uint32_t param = ctx.r4.u32;
  uint32_t r10 = 0;
  uint32_t func_ptr = 0;

  if (obj != 0)
  {
    r10 = REX_LOAD_U32(obj + 16);
    if (r10 != 0)
    {
      func_ptr = REX_LOAD_U32(r10 + 88);
    }
  }

  REXLOG_INFO("[fix] sub_82591430 entry: obj=0x{:08X}, param={}, r10=0x{:08X}, func_ptr(r10+88)=0x{:08X}",
              obj, param, r10, func_ptr);
  rex::FlushLogging();

  if (func_ptr == 0)
  {
    REXLOG_WARN("[fix] sub_82591430: func_ptr is NULL! Returning gracefully instead of aborting.");
    return;
  }

  __imp__sub_82591430(ctx, base);

  REXLOG_INFO("[fix] sub_82591430 exit (completed without crash)");
  rex::FlushLogging();
}

// ---------------------------------------------------------------------------
// 5. sub_8291F508: XamShowDeviceSelectorUI wrapper & Storage Selection
// ---------------------------------------------------------------------------
// Frostbite calls this to display the device selector UI when starting a campaign.
// With an overlapped pointer (ctx.r8 != 0), the engine strictly expects
// ERROR_IO_PENDING (997) as the return value to enter its completion wait loop.
// ReXGlue headless UI returns 0 (ERROR_SUCCESS), which causes Frostbite to enter
// an infinite retry loop at loc_82ABC430, freezing on "PLEASE WAIT - STARTING GAME".
// We also ensure *pDeviceId (ctx.r7) is set to 1 (valid storage device).
// ---------------------------------------------------------------------------
REX_HOOK_RAW(sub_8291F508)
{
  uint32_t device_id_ptr = ctx.r7.u32;
  uint32_t overlapped_ptr = ctx.r8.u32;

  REXLOG_INFO("[fix] sub_8291F508 (XamShowDeviceSelectorUI wrapper) called: user={}, type={}, flags={}, size={}, pDeviceId=0x{:08X}, pOverlapped=0x{:08X}",
              ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, device_id_ptr, overlapped_ptr);
  rex::FlushLogging();

  __imp__sub_8291F508(ctx, base);

  if (device_id_ptr != 0)
  {
    uint32_t current_id = REX_LOAD_U32(device_id_ptr);
    if (current_id == 0)
    {
      REX_STORE_U32(device_id_ptr, 1); // 1 = Valid default HDD / MU device ID
      REXLOG_INFO("[fix] sub_8291F508: Set *pDeviceId = 1 (default storage)");
    }
  }

  if (overlapped_ptr != 0 && ctx.r3.u32 == 0)
  {
    ctx.r3.s64 = 997; // ERROR_IO_PENDING
    REXLOG_INFO("[fix] sub_8291F508: Overrode return value to ERROR_IO_PENDING (997) for Frostbite async loop");
  }
  rex::FlushLogging();
}

