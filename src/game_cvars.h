// game_cvars.h - Game-specific CVAR definitions for Battlefield: Bad Company
#pragma once

#include <rex/cvar.h>
#if defined(BADCO_RELEASE_PACKAGE)
inline constexpr bool kBadcoDevelopmentDefaults = false;
#else
inline constexpr bool kBadcoDevelopmentDefaults = true;
#endif

REXCVAR_DEFINE_STRING(graphics_backend, "any", "GPU",
                      "Graphics API backend: any, d3d12, vulkan")
    .allowed({"any", "d3d12", "vulkan"});

REXCVAR_DEFINE_BOOL(badco_fps_60, false, "Gameplay",
                    "Unlock 60 FPS (Default: false, authentic 30 FPS for physics stability)");

REXCVAR_DEFINE_BOOL(dev_debug_runtime, kBadcoDevelopmentDefaults, "Debug",
                    "Enable runtime debug tools (stub sweep, missing function scan)");
REXCVAR_DEFINE_BOOL(badco_diagnostic_hotkeys, kBadcoDevelopmentDefaults, "Debug",
                    "Enable experimental graphics keys F6 and F9-F12; mouse capture F8 is retained");
REXCVAR_DEFINE_BOOL(badco_startup_self_tests, kBadcoDevelopmentDefaults, "Debug",
                    "Run isolated scratch-memory regression checks at startup");
REXCVAR_DEFINE_BOOL(badco_timing_diagnostics, kBadcoDevelopmentDefaults, "Debug",
                    "Log periodic guest swap submission timing; input frame sampling is always retained");

REXCVAR_DEFINE_DOUBLE(badco_mouse_y_scale, 3.0, "Input",
                      "Vertical relative mouse sensitivity multiplier (1 = SDK default)")
    .range(1.0, 10.0);
REXCVAR_DEFINE_INT32(badco_display_gamma_type, 1, "Display",
                     "Display gamma: 1 = sRGB monitor, 2 = original BT.709 TV")
    .range(1, 2)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(badco_disable_film_grain, true, "Display",
                    "Disable only the game's film grain during post-processing");

REXCVAR_DEFINE_INT32(badco_shadow_fixed_movement, -1, "Display",
                     "Shadow stabilization diagnostic: -1 original, 1 on, 0 off (F9 cycles)")
    .range(-1, 1);

REXCVAR_DEFINE_BOOL(badco_gpu_cache_reset_probe, false, "Debug",
                    "Request one runtime GPU cache reset during boot for an isolated probe");
REXCVAR_DEFINE_BOOL(badco_shadow_packing_probe, false, "Debug",
                    "Check original D3DX matrix packing in isolated scratch memory during boot");
REXCVAR_DEFINE_BOOL(badco_shadow_gpu_capture, false, "Debug",
                    "Observe two GPU command frames on F10; frozen Xenos plugin only")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(badco_shadow_gpu_capture_probe, false, "Debug",
                    "Request the opt-in GPU command capture during isolated boot verification");
REXCVAR_DEFINE_BOOL(badco_shadow_array_fetch_fix, false, "Display",
                    "Experimental stacked-depth layer selection for captured shadow shaders")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(badco_shadow_array_probe_root, "", "Debug",
                      "Isolated probe: folder of captured shader microcode for original-loader checks");
REXCVAR_DEFINE_BOOL(badco_shadow_array_diagnostics, false, "Debug",
                    "Report shadow program coverage and sampled fire-trigger state");
REXCVAR_DEFINE_BOOL(badco_shadow_variant_cache, true, "Display",
                    "Reuse loaded shadow variants; invalidate on GPU cache clear and shutdown");
REXCVAR_DEFINE_BOOL(badco_performance_diagnostics, false, "Debug",
                    "Time slow GPU draw, resolve and present calls without changing rendering");
REXCVAR_DEFINE_BOOL(badco_shadow_depth_readback_fast, false, "Display",
                    "Experimental fast CPU readback only for captured cascade/view depth copies; F6 toggles");
REXCVAR_DEFINE_BOOL(badco_hdr_readback_fast, false, "Display",
                    "Experimental fast CPU readback for captured 640x360/1280x720 RGBA16F copies; F6 toggles with depth experiment");
REXCVAR_DEFINE_BOOL(badco_depth_readback_probe, false, "Debug",
                    "Verify scoped depth-readback selection and restoration through the SDK flag registry");
REXCVAR_DEFINE_BOOL(badco_output_size_probe, false, "Debug",
                    "Read one presenter image size during an isolated resolution probe");
REXCVAR_DEFINE_STRING(badco_prepare_shadow_pipelines, "", "Debug",
                      "Isolated probe: stage corrected descriptors beside source.xpso in this folder");
REXCVAR_DEFINE_BOOL(badco_mnk_frame_sampling, true, "Input",
                    "Sample keyboard/mouse once per guest frame; retain physical gamepad polling")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(badco_mouse_deadzone_compensation, true, "Input",
                    "Compensate the verified guest deadzone on keyboard/mouse look axes");
