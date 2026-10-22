# Development handover

Read [AGENTS.md](AGENTS.md) for the current workflow and [README.md](README.md) for setup. This document supersedes the upstream title-screen-era handover.

The campaign advanced beyond the original HDD cache calibration blocker and was completed on a preceding development build. The current release candidate is `2026.10.22-rc1`. It preserves the accumulated memory, file I/O, synchronization, save/load, graphics and mouse corrections. See [RC1 validation](docs/RC1_VALIDATION.md) for the exact evidence and limitations.

Current defaults are FULL readback, 30 FPS, D3D12 ROV, shadow texture-array correction enabled, monitor gamma, film grain disabled and corrected mouse input. Depth/HDR readback experiments and release diagnostics remain disabled. Occasional frame stalls are still unresolved; a new full campaign playthrough on RC1 is pending.

Do not reapply the historical cache bypass or old memory changes blindly. They are already represented in the existing hooks. Do not replace the frozen SDK binaries without revalidating runtime helper and GPU bindings. There is no required absolute workspace path.
