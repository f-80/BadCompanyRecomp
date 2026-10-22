// Observe game physical-memory imports while forwarding their original ABI.
#pragma once
#if defined(_WIN32)
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

namespace trial_physical {
inline std::atomic<uint64_t> events{0}, failures{0};

inline PPCFunc* Original(const char *name)
{
  auto module = GetModuleHandleW(L"rexruntimerd.dll");
  auto function = module ? reinterpret_cast<PPCFunc*>(GetProcAddress(module, name)) : nullptr;
  if (!function)
  {
    REXLOG_CRITICAL("[trial] Original SDK physical-memory export unavailable: {}", name);
    rex::FlushLogging();
    std::abort();
  }
  return function;
}

inline void Call(PPCContext &ctx, uint8_t *base, bool allocate)
{
  DWORD incoming_error = GetLastError();
  static PPCFunc *alloc = Original("__imp__MmAllocatePhysicalMemoryEx");
  static PPCFunc *free = Original("__imp__MmFreePhysicalMemory");
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  const uint32_t args[] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                           ctx.r6.u32, ctx.r7.u32, ctx.r8.u32};
  SetLastError(incoming_error);
  (allocate ? alloc : free)(ctx, base);
  DWORD returned_error = GetLastError();
  const uint64_t event = events.fetch_add(1, std::memory_order_relaxed);
  uint64_t failure = 0;
  if (allocate && ctx.r3.u32 == 0)
    failure = failures.fetch_add(1, std::memory_order_relaxed) + 1;
  // Bound output even if the game retries a failed allocation in a tight loop.
  const bool power_of_two = failure && !(failure & (failure - 1));
  const bool periodic = event && !(event & (event - 1));
  if (event < 256 || periodic || (failure && (failure <= 16 || power_of_two)))
  {
    uint32_t total = 0, available = 0, page = 0;
    auto *runtime = rex::Runtime::instance();
    auto *heap = runtime && runtime->memory() ? runtime->memory()->GetPhysicalHeap() : nullptr;
    if (heap)
    {
      total = heap->GetTotalPageCount();
      available = heap->GetUnreservedPageCount();
      page = heap->page_size();
    }
    char line[768]{};
    int length = std::snprintf(line, sizeof(line),
        "event=%llu operation=%s pid=%lu tid=%lu lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X result_r3=%08X failed_allocations=%llu physical_total_pages=%u physical_free_pages=%u page_size=%u\r\n",
        static_cast<unsigned long long>(event), allocate ? "alloc" : "free",
        GetCurrentProcessId(), GetCurrentThreadId(), lr,
        args[0], args[1], args[2], args[3], args[4], args[5], ctx.r3.u32,
        static_cast<unsigned long long>(failures.load(std::memory_order_relaxed)),
        total, available, page);
    HANDLE file = CreateFileW(L"logs/trial_physical_memory.txt", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
      DWORD written = 0;
      if (length > 0 && length < static_cast<int>(sizeof(line)))
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
      CloseHandle(file);
    }
  }
  SetLastError(returned_error);
}
} // namespace trial_physical
#endif
