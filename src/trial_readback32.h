// GPU-thread-only experiment for captured cascade and view depth resolves.
#pragma once
#include <atomic>
#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>

namespace trial_readback32 {
using Processor = rex::graphics::CommandProcessor;
inline std::atomic<bool> enabled{true};
inline uint64_t switched_copies = 0;
inline bool probe_done = false;
inline bool CapturedDepth(uint32_t control, uint32_t pitch, uint32_t info) {
  // Exact register tuples observed in manual103; view-depth stalls in 130/131.
  // Color-mask copies, other sizes, clears and unknown formats retain full.
  return control == 4 &&
      ((pitch == 0x04000400 && info == 0x00000302) ||
       ((pitch == 0x02D00500 || pitch == 0x01680280) && info == 0x00004302));
}
inline bool CapturedHDR(uint32_t control, uint32_t pitch, uint32_t info) {
  // Captured in manual103; slow calls in 135/136. Format 32 is RGBA16F.
  // Never select RGBA8, smaller masks, unknown formats or clear commands.
  return control == 0x00100000 && info == 0x0000F001 &&
      (pitch == 0x02D00500 || pitch == 0x01680280);
}
struct Scope {
  bool changed = false;
  Scope(uint32_t control, uint32_t pitch, uint32_t info, bool requested, bool hdr_requested = false) {
    if (!((requested && CapturedDepth(control, pitch, info)) ||
          (hdr_requested && CapturedHDR(control, pitch, info))) ||
        rex::cvar::GetFlagByName("readback_resolve") != "full") return;
    changed = rex::cvar::SetFlagByName("readback_resolve", "fast");
    if (changed && !switched_copies++)
      REXLOG_INFO("[trial-depth-readback] active=true control={:08X} pitch={:08X} info={:08X} hdr_experiment={} rgba8_resolves_unchanged=true", control, pitch, info, hdr_requested);
  }
  ~Scope() {
    if (changed && !rex::cvar::SetFlagByName("readback_resolve", "full")) std::abort();
  }
};
inline void Probe(Processor* processor) {
  if (probe_done || !REXCVAR_GET(badco_depth_readback_probe)) return;
  probe_done = true;
  const auto original = rex::cvar::GetFlagByName("readback_resolve");
  const auto count = switched_copies;
  auto mode = [] { return rex::cvar::GetFlagByName("readback_resolve"); };
  bool correct = rex::cvar::SetFlagByName("readback_resolve", "full") && mode() == "full";
  {
    Scope selected(4, 0x04000400, 0x302, true);
    correct &= selected.changed && mode() == "fast";
  }
  correct &= mode() == "full";
  for (const auto pitch : {0x02D00500u, 0x01680280u}) {
    { Scope selected(4, pitch, 0x4302, true); correct &= selected.changed && mode() == "fast"; }
    correct &= mode() == "full";
  }
  for (unsigned guard = 0; guard != 5; ++guard) {
    Scope unchanged(guard == 0 ? 0 : guard == 1 ? 0x304 : 4,
                    guard == 2 ? 0x00400040 : 0x04000400,
                    guard == 3 ? 0x4302 : 0x302, guard != 4);
    correct &= !unchanged.changed && mode() == "full";
  }
  if (REXCVAR_GET(badco_hdr_readback_fast)) {
    for (const auto pitch : {0x02D00500u, 0x01680280u}) {
      { Scope selected(0x100000, pitch, 0xF001, false, true); correct &= selected.changed && mode() == "fast"; }
      correct &= mode() == "full";
    }
    for (unsigned guard = 0; guard != 5; ++guard) {
      Scope unchanged(guard == 0 ? 0x100100 : 0x100000,
                      guard == 1 ? 0x00400040 : 0x01680280,
                      guard == 2 ? 0xF302 : guard == 3 ? 0xF081 : 0xF001, false, guard != 4);
      correct &= !unchanged.changed && mode() == "full";
    }
  }
  correct &= rex::cvar::SetFlagByName("readback_resolve", "fast");
  { Scope unchanged(4, 0x04000400, 0x302, true); correct &= !unchanged.changed && mode() == "fast"; }
  correct &= rex::cvar::SetFlagByName("readback_resolve", original);
  switched_copies = count;
  REXLOG_INFO("[trial-depth-readback-test] sdk_registry_fast_inside_full_after=true depth_configurations=3 color_size_format_clear_disabled_guards=true original_value_restored={} result={} synthetic_draw=false",
              rex::cvar::GetFlagByName("readback_resolve") == original, correct);
  if (REXCVAR_GET(badco_hdr_readback_fast))
    REXLOG_INFO("[trial-hdr-readback-test] hdr_configurations=2 rgba8_small_mask_unknown_format_clear_disabled_guards=true original_value_restored={} result={} synthetic_draw=false",
                rex::cvar::GetFlagByName("readback_resolve") == original, correct);
  if (!correct) std::abort();
}
}  // namespace trial_readback32
