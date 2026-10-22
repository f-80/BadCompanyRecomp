// badcompanyrecomp_app.h - Battlefield: Bad Company Recompiled Project
// Based on ReXGlue SDK v0.10.0+

#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/function_dispatcher.h>
#include <rex/ui/window.h>
#include <rex/ui/keybinds.h>
#include <rex/input/mnk/mnk_input_driver.h>

#include "generated/default/badcompanyrecomp_pch.h"
#include "generated/default/badcompanyrecomp_funcs.h"

#include "debug_tools.h"
#include "game_cvars.h"
#include "game_patches.h"
#include "utils.h"
#include "trial_fault_trace.h"
#include "trial_mouse_axes.h"
#include "trial_mnk_sampling.h"
#include "trial_gpu_cache.h"

extern const rex::PPCImageInfo PPCImageConfig;

#if defined(_WIN32)
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ep)
{
  // 1. On-demand guest memory page fault & write-protection handler
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
  {
    uintptr_t fault_addr = ep->ExceptionRecord->ExceptionInformation[1];
    uint8_t *base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
    if (base && fault_addr >= (uintptr_t)base && fault_addr < (uintptr_t)base + 0x100000000ull)
    {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery((void *)fault_addr, &mbi, sizeof(mbi)))
      {
        if (mbi.State == MEM_COMMIT)
        {
          DWORD old_protect = 0;
          DWORD new_protect = (mbi.AllocationProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                                  ? PAGE_EXECUTE_READWRITE
                                  : PAGE_READWRITE;
          if (VirtualProtect(mbi.BaseAddress, mbi.RegionSize, new_protect, &old_protect))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
          if (new_protect != PAGE_READWRITE && VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_READWRITE, &old_protect))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
          void *page_4k = (void *)(fault_addr & ~0xFFFull);
          if (VirtualProtect(page_4k, 0x1000, PAGE_READWRITE, &old_protect))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
        }
        else if (mbi.State == MEM_RESERVE)
        {
          void *page_base = (void *)(fault_addr & ~0xFFFFull);
          if (VirtualAlloc(page_base, 0x10000, MEM_COMMIT, PAGE_READWRITE))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
          void *page_4k = (void *)(fault_addr & ~0xFFFull);
          if (VirtualAlloc(page_4k, 0x1000, MEM_COMMIT, PAGE_READWRITE))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
        }
        else if (mbi.State == MEM_FREE)
        {
          void *page_base = (void *)(fault_addr & ~0xFFFFull);
          if (VirtualAlloc(page_base, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
          {
            return EXCEPTION_CONTINUE_EXECUTION;
          }
        }
      }
    }
  }

  // 2. Fatal crash diagnostics and minidump creation
  if (ep->ExceptionRecord->ExceptionCode == 0x80000003 ||
      ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
      ep->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION)
  {
    rex::FlushLogging();

    FILE *f = std::fopen("logs/crash_stack.txt", "w");
    if (f)
    {
      std::fprintf(f, "=== CRASH CAUGHT BY HANDLER ===\n");
      std::fprintf(f, "Exception Code:    0x%08lX\n", ep->ExceptionRecord->ExceptionCode);
      std::fprintf(f, "Exception Address: %p\n", ep->ExceptionRecord->ExceptionAddress);
      std::fprintf(f, "Thread ID:         %lu\n", GetCurrentThreadId());
      std::fprintf(f, "RIP: %p, RSP: %p\n", (void *)ep->ContextRecord->Rip, (void *)ep->ContextRecord->Rsp);
      const auto &cs = trial_cs_semaphore::last;
      std::fprintf(f, "RTL leave snapshot: active=%u cs=%08X lr=%08X recursion=%u count=%d handle=%08X type=%u\n",
                   cs.active, cs.address, cs.lr, cs.recursion, cs.count, cs.handle, cs.type);

      uintptr_t *rsp = (uintptr_t *)ep->ContextRecord->Rsp;
      for (int i = 0; i < 128; ++i)
      {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(&rsp[i], &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
        {
          uintptr_t val = rsp[i];
          HMODULE mod = nullptr;
          if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)val, &mod))
          {
            char modname[MAX_PATH];
            GetModuleFileNameA(mod, modname, sizeof(modname));
            const char *base = std::strrchr(modname, '\\');
            std::fprintf(f, "  [stack %02d] %p (%s + 0x%llX)\n", i, (void *)val, base ? base + 1 : modname, (unsigned long long)(val - (uintptr_t)mod));
          }
        }
      }
      std::fclose(f);
    }

    HANDLE hFile = CreateFileA("logs/crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION mei;
      mei.ThreadId = GetCurrentThreadId();
      mei.ExceptionPointers = ep;
      mei.ClientPointers = FALSE;
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
      CloseHandle(hFile);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

class BadcompanyrecompApp : public rex::ReXApp
{
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext &ctx)
  {
#if defined(_WIN32)
    AddVectoredExceptionHandler(1, CrashHandler);
#endif
    return std::unique_ptr<BadcompanyrecompApp>(new BadcompanyrecompApp(ctx, "badcompanyrecomp",
                                                                       PPCImageConfig));
  }

  static void SafeCommitRange(uint8_t *start, size_t size)
  {
#if defined(_WIN32)
    const size_t kChunkSize = 64 * 1024;
    for (size_t offset = 0; offset < size; offset += kChunkSize)
    {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery(start + offset, &mbi, sizeof(mbi)))
      {
        if (mbi.State == MEM_RESERVE)
        {
          VirtualAlloc(start + offset, kChunkSize, MEM_COMMIT, PAGE_READWRITE);
        }
      }
    }
#endif
  }

  void PrecommitMemoryAliases()
  {
#if defined(_WIN32)
    uint8_t *base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
    if (!base)
    {
      return;
    }
    // Commit physical, cached and uncached write-combined Xbox 360 memory ranges (512MB each alias)
    VirtualAlloc(base + 0x00000000ull, 0x40000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0x40000000ull, 0x40000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0x80000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xA0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xC0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xE0000000ull, 0x1FD00000ull, MEM_COMMIT, PAGE_READWRITE);
#endif
  }

  void OnPreSetup(rex::RuntimeConfig &config) override
  {
    trial_mnk_sampling::Configure(config);
    std::string backend = REXCVAR_GET(graphics_backend);
    if (backend != "any" && !config.gpu_plugin.empty())
    {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (!config.graphics)
      {
        REXLOG_WARN("graphics_backend '{}' unavailable, falling back to automatic selection",
                    backend);
      }
    }
  }

  static void UnlockGuestMemoryProtection()
  {
#if defined(_WIN32)
    uint8_t *base = rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
    if (!base)
    {
      return;
    }
    // Scan full 4GB guest space and unprotect all committed read-only pages
    uint8_t *curr = base;
    uint8_t *end = base + 0x100000000ull;
    while (curr < end)
    {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery(curr, &mbi, sizeof(mbi)))
      {
        if (mbi.State == MEM_COMMIT && mbi.BaseAddress != nullptr &&
            (mbi.Protect == PAGE_READONLY || mbi.Protect == PAGE_NOACCESS))
        {
          DWORD old_protect = 0;
          DWORD new_protect = (mbi.AllocationProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                                  ? PAGE_EXECUTE_READWRITE
                                  : PAGE_READWRITE;
          if (!VirtualProtect(mbi.BaseAddress, mbi.RegionSize, new_protect, &old_protect))
          {
            VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_READWRITE, &old_protect);
          }
        }
        size_t step = mbi.RegionSize ? mbi.RegionSize : 0x1000;
        curr = (uint8_t *)mbi.BaseAddress + step;
      }
      else
      {
        curr += 0x10000;
      }
    }
#endif
  }

  void OnPostLoadXexImage() override
  {
    PrecommitMemoryAliases();
    UnlockGuestMemoryProtection();
    game_patches::FpsUnlock();
  }

  void OnPostSetup() override
  {
#if defined(_WIN32)
    // Observe faults before runtime handlers; preserve their disposition.
    AddVectoredExceptionHandler(1, TrialGuestFaultTrace);
#endif
    // Trial diagnostics: report only this application's window and desktop.
#if defined(_WIN32)
    if (auto *w = window())
    {
      HWND hwnd = static_cast<HWND>(w->GetNativeWindowHandle());
      RECT rect{};
      GetWindowRect(hwnd, &rect);
      char station[128]{};
      char desktop[128]{};
      DWORD needed = 0, session = 0;
      GetUserObjectInformationA(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &needed);
      GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &needed);
      ProcessIdToSessionId(GetCurrentProcessId(), &session);
      REXLOG_INFO("[trial-window] handle={:X} visible={} minimized={} rect=({},{},{},{}) logical={}x{} station={} desktop={} session={} console_session={}",
                  reinterpret_cast<uintptr_t>(hwnd), IsWindowVisible(hwnd) != FALSE, IsIconic(hwnd) != FALSE,
                  rect.left, rect.top, rect.right, rect.bottom,
                  w->GetActualLogicalWidth(), w->GetActualLogicalHeight(), station, desktop,
                  session, WTSGetActiveConsoleSessionId());
      rex::FlushLogging();
    }
#endif
    PrecommitMemoryAliases();
    UnlockGuestMemoryProtection();
    utils::LoadSettingsFiles();

    // Enable the SDK mouse-look gate and provide a way to release/reacquire it.
#if defined(_WIN32)
    if (REXCVAR_GET(badco_startup_self_tests)) {
    trial_cs::VerifyInitialization();
    trial_cs_semaphore::Verify();
    trial_shadow::Verify();
    trial_mnk_sampling::Verify();
    trial_mouse_precision::Verify();
    }
    if (REXCVAR_GET(badco_shadow_packing_probe)) {
      trial_shadow_upload::VerifyPacking();
      trial_shadow_semantics::Verify();
    }
#endif
    rex::input::mnk::SetMouseLookActive(true);
    trial_mouse::axes.Attach(window());
    if (REXCVAR_GET(badco_diagnostic_hotkeys)) {
    rex::ui::RegisterBind("bind_trial_gpu_cache_reset", "F11", "Reset runtime GPU caches once", [] {
      trial_gpu_cache::RequestReset("F11");
    });
    REXLOG_INFO("[trial-gpu-cache] F11 requests one runtime cache reset; default rendering unchanged");
    }
    if (REXCVAR_GET(badco_gpu_cache_reset_probe))
      trial_gpu_cache::RequestReset("boot_probe");
    if (REXCVAR_GET(badco_diagnostic_hotkeys)) {
    REXLOG_INFO("[trial-shadow-trace] binding_verified={} semantic_binding_verified={}; F10 captures cascades, named uploads and engine semantic 33 for 10 seconds",
                trial_shadow_trace::BindingVerified(rex::Runtime::instance()->virtual_membase()),
                trial_shadow_semantics::BindingVerified(rex::Runtime::instance()->virtual_membase()));
    rex::ui::RegisterBind("bind_trial_shadow_capture", "F10", "Capture shadow matrices for 10 seconds", [] {
      trial_shadow_trace::Arm();
        trial_shadow_gpu::Request();
      rex::FlushLogging();
    });
    }
      if (REXCVAR_GET(badco_shadow_gpu_capture) || REXCVAR_GET(badco_shadow_array_fetch_fix)) {
        const bool installed = trial_shadow_gpu::Install();
        if (installed && REXCVAR_GET(badco_shadow_gpu_capture_probe)) trial_shadow_gpu::Request();
        if (installed && REXCVAR_GET(badco_diagnostic_hotkeys) && REXCVAR_GET(badco_shadow_depth_readback_fast)) {
          rex::ui::RegisterBind("bind_trial_depth_readback", "F6", "Toggle selective GPU readback", [this] {
            const bool next = !trial_readback32::enabled.load(std::memory_order_relaxed);
            trial_readback32::enabled.store(next, std::memory_order_relaxed);
            if (auto* w = window()) w->SetTitle(next ? "Selective readback: FAST (F6)" : "Selective readback: FULL (F6)");
            REXLOG_INFO("[trial-depth-readback] F6 enabled={}", next);
            rex::FlushLogging();
          });
          REXLOG_INFO("[trial-depth-readback] experiment armed; F6 toggles selective readback; hdr_experiment={} rgba8_resolves_retain_configured_mode=true", REXCVAR_GET(badco_hdr_readback_fast));
          if (REXCVAR_GET(badco_depth_readback_probe)) {
            unsigned matches = 0;
            std::string owners;
            for (const auto& entry : rex::cvar::GetRegistry()) {
              if (entry.name.starts_with("bind_") && entry.getter) {
                const auto key = entry.getter();
                REXLOG_INFO("[trial-keybind-registry] name={} key={}", entry.name, key);
                if (key == "F6") { ++matches; owners += entry.name + ";"; }
              }
            }
            const bool saved = trial_readback32::enabled.load(std::memory_order_relaxed);
            bool routed = false;
            if (matches == 1 && owners == "bind_trial_depth_readback;") {
              rex::ui::KeyEvent first(nullptr, rex::ui::ParseVirtualKey("F6"), 1, false, false, false, false, false);
              rex::ui::KeyEvent second(nullptr, rex::ui::ParseVirtualKey("F6"), 1, false, false, false, false, false);
              routed = rex::ui::ProcessKeyEvent(first) && first.is_handled() &&
                  trial_readback32::enabled.load(std::memory_order_relaxed) != saved;
              routed &= rex::ui::ProcessKeyEvent(second) && second.is_handled() &&
                  trial_readback32::enabled.load(std::memory_order_relaxed) == saved;
            }
            REXLOG_INFO("[trial-depth-key-test] key=F6 owners={} unique={} real_sdk_dispatch_two_toggles_restore={} os_input=false", owners, matches == 1, routed);
            if (!routed) std::abort();
          }
        }
        if (installed && REXCVAR_GET(badco_diagnostic_hotkeys) && REXCVAR_GET(badco_shadow_array_fetch_fix)) {
          rex::ui::RegisterBind("bind_trial_shadow_array", "F12", "Toggle experimental shadow layer selection", [this] {
            const bool next = !trial_shadow_array::enabled.load(std::memory_order_relaxed);
            trial_shadow_array::enabled.store(next, std::memory_order_relaxed);
            if (auto* w = window()) w->SetTitle(next ? "Shadows: array fix ON" : "Shadows: array fix OFF");
            REXLOG_INFO("[trial-shadow-array] F12 enabled={} corrected_draws={}", next,
                        trial_shadow_array::corrected_draws.load(std::memory_order_relaxed));
            rex::FlushLogging();
          });
          REXLOG_INFO("[trial-shadow-array] experiment enabled; F12 toggles corrected layer selection / original");
        }
      }
    trial_shadow::mode.store(REXCVAR_GET(badco_shadow_fixed_movement), std::memory_order_relaxed);
    if (REXCVAR_GET(badco_diagnostic_hotkeys)) {
    rex::ui::RegisterBind("bind_trial_shadow_stabilization", "F9", "Cycle shadow stabilization test", [this] {
      const int current = trial_shadow::mode.load(std::memory_order_relaxed);
      const int next = current < 0 ? 1 : current == 1 ? 0 : -1;
      trial_shadow::mode.store(next, std::memory_order_relaxed);
      const char* label = next < 0 ? "Shadows: original" : next ? "Shadows: stabilization ON" : "Shadows: stabilization OFF";
      if (auto* w = window()) w->SetTitle(label);
      REXLOG_INFO("[trial-shadow] F9: {} (mode={})", label, next);
      rex::FlushLogging();
    });
    REXLOG_INFO("[trial-shadow] mode={}; F9 cycles original / stabilization ON / stabilization OFF",
                trial_shadow::mode.load(std::memory_order_relaxed));
    }
    REXLOG_INFO("[trial-input] relative mouse vertical multiplier={:.2f}; gamepads unchanged",
                REXCVAR_GET(badco_mouse_y_scale));
    rex::ui::RegisterBind("bind_trial_mouse_look", "F8", "Toggle mouse look capture", [] {
      const bool active = !rex::input::mnk::IsMouseLookActive();
      rex::input::mnk::SetMouseLookActive(active);
      REXLOG_INFO("[trial-input] mouse look active={}; F8 toggles capture", active);
      rex::FlushLogging();
    });
    REXLOG_INFO("[trial-input] mouse look active={}; F8 toggles capture; LMB/RMB map to triggers",
                rex::input::mnk::IsMouseLookActive());

    // Start real-time background log flusher (every 500ms) to ensure logs are never lost
    static std::atomic<bool> s_flush_running{false};
    if (!s_flush_running.exchange(true))
    {
      std::thread([]() {
        while (true)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          rex::FlushLogging();
        }
      }).detach();
    }

    auto *rt = rex::Runtime::instance();
    auto *fd = rt ? rt->function_dispatcher() : nullptr;
    if (fd)
    {
      // Intercept NULL indirect function pointer calls
      fd->SetFunction(0x00000000, [](PPCContext &ctx, uint8_t *base) {
        REXLOG_WARN("[fix] Ignored call to NULL guest function pointer (Caller LR=0x{:08X})", (uint32_t)ctx.lr);
        ctx.r3.s64 = 0;
      });

      // Intercept KeBugCheck to diagnose guest kernel panics
      fd->SetFunction(0x82B2FD90, [](PPCContext &ctx, uint8_t *base) {
        REXLOG_ERROR("=== GUEST KERNEL PANIC: KeBugCheck ===");
        REXLOG_ERROR("  BugCheckCode: 0x{:08X}", ctx.r3.u32);
        REXLOG_ERROR("  Caller LR:    0x{:08X}", (uint32_t)ctx.lr);
        rex::FlushLogging();

        FILE *f = std::fopen("logs/bugcheck.txt", "w");
        if (f)
        {
          std::fprintf(f, "KeBugCheck: Code=0x%08X (LR=0x%08llX)\n",
                       ctx.r3.u32, (unsigned long long)ctx.lr);
          std::fclose(f);
        }
        __imp__KeBugCheck(ctx, base);
      });

      // Intercept KeBugCheckEx to diagnose guest kernel panics
      fd->SetFunction(0x82B30170, [](PPCContext &ctx, uint8_t *base) {
        REXLOG_ERROR("=== GUEST KERNEL PANIC: KeBugCheckEx ===");
        REXLOG_ERROR("  BugCheckCode: 0x{:08X}", ctx.r3.u32);
        REXLOG_ERROR("  Param 1:      0x{:08X}", ctx.r4.u32);
        REXLOG_ERROR("  Param 2:      0x{:08X}", ctx.r5.u32);
        REXLOG_ERROR("  Param 3:      0x{:08X}", ctx.r6.u32);
        REXLOG_ERROR("  Param 4:      0x{:08X}", ctx.r7.u32);
        REXLOG_ERROR("  Caller LR:    0x{:08X}", (uint32_t)ctx.lr);
        rex::FlushLogging();

        FILE *f = std::fopen("logs/bugcheck.txt", "w");
        if (f)
        {
          std::fprintf(f, "KeBugCheckEx: Code=0x%08X (r4=0x%08X, r5=0x%08X, r6=0x%08X, r7=0x%08X, LR=0x%08llX)\n",
                       ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, (unsigned long long)ctx.lr);
          std::fclose(f);
        }
        __imp__KeBugCheckEx(ctx, base);
      });
    }

    if (REXCVAR_GET(dev_debug_runtime))
    {
      debug_tools::PerformMissingFunctionScan();
      debug_tools::PerformStubSweep();
    }
#if defined(BADCO_RELEASE_PACKAGE)
    if (auto* w = window()) w->SetTitle("Battlefield: Bad Company | RC1 (FULL)");
    REXLOG_INFO("[release-profile] readback={} shadow_layer_fix={} depth_experiment={} hdr_experiment={} fps_60={} diagnostic_hotkeys={} startup_tests={} timing_diagnostics={} input_frame_sampling={}",
        rex::cvar::GetFlagByName("readback_resolve"), REXCVAR_GET(badco_shadow_array_fetch_fix),
        REXCVAR_GET(badco_shadow_depth_readback_fast), REXCVAR_GET(badco_hdr_readback_fast),
        REXCVAR_GET(badco_fps_60), REXCVAR_GET(badco_diagnostic_hotkeys),
        REXCVAR_GET(badco_startup_self_tests), REXCVAR_GET(badco_timing_diagnostics),
        REXCVAR_GET(badco_mnk_frame_sampling));
#endif
  }

  void OnConfigurePaths(rex::PathConfig &paths) override
  {
    if (paths.game_data_root.empty())
    {
      paths.game_data_root = utils::RepoRoot() / "assets";
    }

    paths.config_path = utils::SettingsDir() / "hardware.toml";
    utils::LoadSettingsFiles();
  }
};
