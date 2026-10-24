// Main-camera FOV scaling for the supported Xbox 360 image.
// badco_camera_fov_scale: 1.4 by default; 1.0 restores the original angle.
// This scales the internal camera angle, including zoom and vehicle cameras
// that use this call site. It is not a horizontal-FOV-in-degrees setting.
#pragma once
#include <algorithm>
#include <cmath>

namespace badco_camera_fov {
inline constexpr double kCap = 120.0 * 0.017453292519943295;

inline bool BindingVerified(uint8_t* base) {
  return REX_LOAD_U32(0x827F2A20) == 0xC003000C &&
      REX_LOAD_U32(0x827F2A38) == 0xD023000C &&
      REX_LOAD_U32(0x827F2A40) == 0xD0030010 &&
      REX_LOAD_U32(0x8226DD48) == 0xC3EB1C28 &&
      REX_LOAD_U32(0x8226DD4C) == 0xEC2007F2 &&
      REX_LOAD_U32(0x8226DD50) == 0x48584CD1 &&
      REX_LOAD_U32(0x8237D4A8) == 0x3BDF0010 &&
      REX_LOAD_U32(0x8237D4B0) == 0x4BFFE731 &&
      REX_LOAD_U32(0x8237BC38) == 0xC01F000C &&
      REX_LOAD_U32(0x8237BC3C) == 0xD01E000C;
}

inline void Call(PPCContext& ctx, uint8_t* base) {
  const DWORD incoming_error = GetLastError();
  const uint64_t original_bits = ctx.f1.u64;
  const double original = ctx.f1.f64;
  const double scale = REXCVAR_GET(badco_camera_fov_scale);
  // Only the main projection setter call; alternate views and renderer
  // ForceFov overrides retain their original behavior.
  if (ctx.lr == 0x8226DD54 && std::isfinite(scale) && scale > 1.0 &&
      std::isfinite(original) && original > 0.0 && original < kCap &&
      BindingVerified(base)) {
    ctx.f1.f64 = std::min(original * std::clamp(scale, 1.0, 1.4), kCap);
  }
  SetLastError(incoming_error);
  __imp__sub_827F2A20(ctx, base);
  // Keep the original dirty flags/aspect product and preserve the setter ABI.
  ctx.f1.u64 = original_bits;
}
}  // namespace badco_camera_fov
