// Observe null dispatch from generated code, then forward to the original SDK.
// Do not register a fake null function or turn a fatal call into success.
#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/thread_state.h>
#include <rex/logging.h>
#include <rex/logging/api.h>

namespace rex::runtime {
__declspec(noinline) PPCFunc* ResolveIndirectFunction(uint32_t guest_address)
{
  using Resolver = PPCFunc* (*)(uint32_t);
  static Resolver original = []() -> Resolver {
    HMODULE module = GetModuleHandleW(L"rexruntimerd.dll");
    // Export spelling verified against this SDK's PE export table.
    return module ? reinterpret_cast<Resolver>(GetProcAddress(module,
        "?ResolveIndirectFunction@runtime@rex@@YAP6AXAEAUPPCContext@@PEAE@ZI@Z")) : nullptr;
  }();
  if (!original)
  {
    REXLOG_CRITICAL("[trial] Original SDK indirect resolver unavailable");
    rex::FlushLogging();
    std::abort();
  }
  if (guest_address == 0)
  {
    auto *thread = ThreadState::Get();
    auto *ctx = thread ? thread->context() : nullptr;
    uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    char line[1024]{};
    int length = std::snprintf(line, sizeof(line),
        "pid=%lu guest_target=%08X caller=%016llX exe_base=%016llX caller_rva=%016llX native_tid=%lu guest_tid=%08X lr=%08X ctr=%08X sp=%08X r3=%08X r4=%08X r5=%08X r6=%08X\r\n",
        GetCurrentProcessId(), guest_address, static_cast<unsigned long long>(caller),
        static_cast<unsigned long long>(module), static_cast<unsigned long long>(caller - module),
        GetCurrentThreadId(), thread ? thread->thread_id() : 0,
        ctx ? static_cast<uint32_t>(ctx->lr) : 0, ctx ? ctx->ctr.u32 : 0,
        ctx ? ctx->r1.u32 : 0, ctx ? ctx->r3.u32 : 0, ctx ? ctx->r4.u32 : 0,
        ctx ? ctx->r5.u32 : 0, ctx ? ctx->r6.u32 : 0);
    HANDLE file = CreateFileW(L"logs/trial_invalid_dispatch.txt", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
      DWORD written = 0;
      if (length > 0 && length < static_cast<int>(sizeof(line)))
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
      CloseHandle(file);
    }
    REXLOG_ERROR("[trial] Null guest dispatch: LR={:08X}, caller RVA={:X}; forwarding to SDK",
                ctx ? static_cast<uint32_t>(ctx->lr) : 0, caller - module);
    rex::FlushLogging();
  }
  return original(guest_address);
}
} // namespace rex::runtime
#endif
