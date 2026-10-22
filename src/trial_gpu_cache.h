// One-shot diagnostic using the frozen Xenos plugin's public cache API.
#pragma once
#include <rex/graphics/graphics_system.h>

namespace trial_gpu_cache {
inline bool RequestReset(const char* source) {
  auto* rt = rex::Runtime::instance();
  auto* graphics = rt ? rt->graphics_system() : nullptr;
  if (!graphics) return false;
#if defined(_WIN32)
  // This downcast is specific to the shipped Xenos plugin and matching headers.
  // Refuse other injected graphics systems rather than assuming their layout.
  const auto plugin = GetModuleHandleW(L"rexgpu-xenosrd.dll");
  MEMORY_BASIC_INFORMATION info{};
  const auto vtable = *reinterpret_cast<void***>(graphics);
  if (!plugin || !VirtualQuery(vtable, &info, sizeof(info)) || info.AllocationBase != plugin) {
    REXLOG_WARN("[trial-gpu-cache] source={} unsupported graphics system; reset skipped", source);
    return false;
  }
#else
  return false;
#endif
  auto* xenos = static_cast<rex::graphics::GraphicsSystem*>(graphics);
  if (xenos->memory() != rt->memory() || !xenos->command_processor()) {
    REXLOG_WARN("[trial-gpu-cache] source={} graphics binding mismatch; reset skipped", source);
    return false;
  }
  // The plugin schedules this on its command processor; it releases runtime
  // resource caches after pending GPU work. Persistent game/save files are not
  // involved. Do not call this every frame or treat it as a shadow fix.
  xenos->ClearCaches();
  REXLOG_INFO("[trial-gpu-cache] source={} one-shot runtime cache reset requested via Xenos API", source);
  rex::FlushLogging();
  return true;
}
}  // namespace trial_gpu_cache
