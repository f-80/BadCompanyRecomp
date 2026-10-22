// Retain RTL lock accounting, but wake a semaphore through its correct API.
#pragma once
#if defined(_WIN32)
#include <rex/system/xsemaphore.h>
#include <rex/thread/atomic.h>

namespace trial_cs_semaphore {
struct Snapshot {
  bool active = false;
  uint32_t address = 0, lr = 0, recursion = 0, handle = 0, type = 0xFFFFFFFF;
  int32_t count = 0;
};
inline thread_local Snapshot last;
inline rex::system::object_ref<rex::system::XObject> Select(PPCContext& ctx, uint8_t* base) {
  const uint32_t address = ctx.r3.u32;
  auto* count = reinterpret_cast<volatile int32_t*>(REX_RAW_ADDR(address + 16));
  last.address = address;
  last.lr = static_cast<uint32_t>(ctx.lr);
  last.recursion = REX_LOAD_U32(address + 20);
  last.count = *count;
  last.type = 0xFFFFFFFF;
  last.handle = 0;
  if (last.recursion != 1) return {};
  // Do not select by lock_count: a waiter can arrive before the atomic unlock.
  // A pristine event header can use the SDK without eager native allocation.
  if (REX_LOAD_U32(address + 8) != 0x52455800 && REX_LOAD_U8(address) == 1) return {};
  auto* rt = rex::Runtime::instance();
  auto* kernel = rt ? rt->kernel_state() : nullptr;
  if (!kernel) return {};
  // Resolve the same dispatch header as the SDK's contended leave path.
  auto object = rex::system::XObject::GetNativeObject(kernel, REX_RAW_ADDR(address));
  if (object) {
    last.type = static_cast<uint32_t>(object->type());
    last.handle = object->handle();
  }
  if (!object || object->type() != rex::system::XObject::Type::Semaphore) return {};
  return object;
}
inline void FinishLeave(PPCContext& ctx, uint8_t* base, rex::system::XSemaphore* semaphore) {
  const uint32_t address = ctx.r3.u32;
  auto* count = reinterpret_cast<volatile int32_t*>(REX_RAW_ADDR(address + 16));
  const uint32_t header_type = REX_LOAD_U8(address);
  const uint32_t handle = semaphore->handle();
  REX_STORE_U32(address + 20, 0);
  REX_STORE_U32(address + 24, 0);
  const int32_t remaining = rex::thread::atomic_dec(count);
  int32_t previous = -1;
  if (remaining != -1 && !semaphore->ReleaseSemaphore(1, &previous)) {
    REXLOG_CRITICAL("[trial-cs-sem] semaphore wake failed: cs={:08X} handle={:08X}", address, handle);
    rex::FlushLogging();
    std::abort();
  }
  static std::atomic<unsigned> reports{0};
  if (reports.fetch_add(1, std::memory_order_relaxed) < 64) {
    REXLOG_INFO("[trial-cs-sem] cs={:08X} header_type={} handle={:08X} remaining={} previous={} parent_LR={:08X}; semaphore unlock",
                address, header_type, handle, remaining, previous, static_cast<uint32_t>(ctx.lr));
  }
}
inline bool TryLeave(PPCContext& ctx, uint8_t* base) {
  auto object = Select(ctx, base);
  if (!object) return false;
  FinishLeave(ctx, base, static_cast<rex::system::XSemaphore*>(object.get()));
  return true;
}
inline void Verify() {
  auto* rt = rex::Runtime::instance();
  auto* mem = rt->memory();
  const uint32_t address = mem->SystemHeapAlloc(32);
  if (!address) std::abort();
  uint8_t* base = rt->virtual_membase();
  auto semaphore = rex::system::object_ref<rex::system::XSemaphore>(new rex::system::XSemaphore(rt->kernel_state()));
  if (!semaphore->Initialize(0, 1000)) std::abort();
  for (unsigned off = 0; off < 32; off += 4) REX_STORE_U32(address + off, 0);
  REX_STORE_U8(address, 2);
  REX_STORE_U32(address + 8, 0x52455800);
  REX_STORE_U32(address + 12, semaphore->handle());
  auto* count = reinterpret_cast<volatile int32_t*>(REX_RAW_ADDR(address + 16));
  *count = 1; // Owner plus one waiter, matching the SDK's native-endian field.
  REX_STORE_U32(address + 20, 1);
  REX_STORE_U32(address + 24, 0x1234);
  // Only the count is initialized here; do not inspect the trailing padding.
  uint32_t event_slot = 0;
  std::memcpy(&event_slot, reinterpret_cast<const uint8_t*>(semaphore.get()) + 0x68, sizeof(event_slot));
  PPCContext ctx{};
  ctx.r3.u64 = address;
  *count = 2;
  REX_STORE_U32(address + 20, 2);
  const bool nested_untouched = !TryLeave(ctx, base) && *count == 2 &&
      REX_LOAD_U32(address + 20) == 2 && REX_LOAD_U32(address + 24) == 0x1234;
  *count = 1;
  REX_STORE_U32(address + 20, 1);
  const bool handled = TryLeave(ctx, base);
  int32_t previous = -1;
  const bool released = semaphore->ReleaseSemaphore(1, &previous);
  const bool correct = nested_untouched && handled && released && previous == 1 && *count == 0 &&
      REX_LOAD_U32(address + 20) == 0 && REX_LOAD_U32(address + 24) == 0 && ctx.r3.u32 == address;
  // Deterministic interleaving: select with no waiter, then one arrives.
  auto racing = rex::system::object_ref<rex::system::XSemaphore>(new rex::system::XSemaphore(rt->kernel_state()));
  if (!racing->Initialize(0, 1000)) std::abort();
  REX_STORE_U8(address, 1); // Real crash had an event header caching a semaphore.
  REX_STORE_U32(address + 12, racing->handle());
  *count = 0;
  REX_STORE_U32(address + 20, 1);
  REX_STORE_U32(address + 24, 0x1234);
  auto selected = Select(ctx, base);
  const bool selected_without_waiter = bool(selected);
  rex::thread::atomic_inc(count);
  if (selected) FinishLeave(ctx, base, static_cast<rex::system::XSemaphore*>(selected.get()));
  int32_t race_previous = -1;
  const bool race_correct = selected && racing->ReleaseSemaphore(1, &race_previous) && race_previous == 1 &&
      *count == 0 && REX_LOAD_U32(address + 20) == 0 && REX_LOAD_U32(address + 24) == 0;
  selected.reset();
  racing->ReleaseHandle();
  semaphore->ReleaseHandle();
  auto event = rex::system::object_ref<rex::system::XEvent>(new rex::system::XEvent(rt->kernel_state()));
  event->Initialize(false, false);
  REX_STORE_U8(address, 1);
  REX_STORE_U32(address + 8, 0x52455800);
  REX_STORE_U32(address + 12, event->handle());
  *count = 1;
  REX_STORE_U32(address + 20, 1);
  REX_STORE_U32(address + 24, 0x1234);
  const bool event_untouched = !TryLeave(ctx, base) && *count == 1 &&
      REX_LOAD_U32(address + 20) == 1 && REX_LOAD_U32(address + 24) == 0x1234;
  event->ReleaseHandle();
  mem->SystemHeapFree(address);
  REXLOG_INFO("[trial-cs-sem-test] event_slot_low32={:X} handled={} previous_after_wake={} accounting_correct={} nested_untouched={} event_untouched={}",
              event_slot, handled, previous, correct, nested_untouched, event_untouched);
  REXLOG_INFO("[trial-cs-race-test] selected_without_waiter={} previous_after_arriving_waiter={} accounting_correct={}",
              selected_without_waiter, race_previous, race_correct);
  rex::FlushLogging();
  if (!correct || !event_untouched || !race_correct) std::abort();
}
} // namespace trial_cs_semaphore
#endif
