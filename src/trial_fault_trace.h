// Bounded observation of guest reads and suspicious writes; never handles faults.
#pragma once
#include <atomic>
#include <cstdio>
#include <rex/runtime.h>
#include <rex/system/thread_state.h>

#if defined(_WIN32)
inline LONG WINAPI TrialGuestFaultTrace(EXCEPTION_POINTERS *ep)
{
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      ep->ExceptionRecord->NumberParameters < 2 ||
      ep->ExceptionRecord->ExceptionInformation[0] > 1)
    return EXCEPTION_CONTINUE_SEARCH;
  auto *runtime = rex::Runtime::instance();
  auto *base = runtime ? runtime->virtual_membase() : nullptr;
  const uintptr_t address = ep->ExceptionRecord->ExceptionInformation[1];
  if (!base || address < reinterpret_cast<uintptr_t>(base))
    return EXCEPTION_CONTINUE_SEARCH;
  const uintptr_t guest = address - reinterpret_cast<uintptr_t>(base);
  const bool read = ep->ExceptionRecord->ExceptionInformation[0] == 0;
  if (guest >= 0x100000000ull ||
      (!read && (guest < 0x40000000ull || guest >= 0x70000000ull)))
    return EXCEPTION_CONTINUE_SEARCH;
  static std::atomic<unsigned> reads{0}, writes{0};
  const unsigned index = (read ? reads : writes).fetch_add(1, std::memory_order_relaxed);
  if (index >= 64)
    return EXCEPTION_CONTINUE_SEARCH;

  const DWORD saved_error = GetLastError();
  auto *thread = rex::runtime::ThreadState::Get();
  auto *ctx = thread ? thread->context() : nullptr;
  const uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  char line[1024]{};
  int length = std::snprintf(line, sizeof(line),
      "event=%u operation=%s pid=%lu guest=%08llX rip=%016llX exe_base=%016llX exe_rva=%016llX native_tid=%lu guest_tid=%08X lr=%08X ctr=%08X sp=%08X r3=%08X r4=%08X r5=%08X r6=%08X\r\n",
      index, read ? "read" : "write", GetCurrentProcessId(), static_cast<unsigned long long>(guest),
      static_cast<unsigned long long>(ep->ContextRecord->Rip),
      static_cast<unsigned long long>(module),
      static_cast<unsigned long long>(ep->ContextRecord->Rip - module),
      GetCurrentThreadId(), thread ? thread->thread_id() : 0,
      ctx ? static_cast<uint32_t>(ctx->lr) : 0, ctx ? ctx->ctr.u32 : 0,
      ctx ? ctx->r1.u32 : 0, ctx ? ctx->r3.u32 : 0, ctx ? ctx->r4.u32 : 0,
      ctx ? ctx->r5.u32 : 0, ctx ? ctx->r6.u32 : 0);
  HANDLE file = CreateFileW(L"logs/trial_guest_faults.txt", FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE)
  {
    DWORD written = 0;
    if (length > 0 && length < static_cast<int>(sizeof(line)))
      WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
  }
  SetLastError(saved_error);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
