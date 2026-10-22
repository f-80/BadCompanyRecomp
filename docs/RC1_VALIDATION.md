# RC1 validation

Version: `2026.10.22-rc1`, Windows x64. The reference executable SHA256 is `4b17c8c581c9472e2b210404ba706fea16ebeff5e1bc47bffa2327d456bb4003`.

## Completed checks

- Release configuration with `/O2 /Ob2 /DNDEBUG /Zi`; generated module 82 retains its documented `/Od` compiler workaround.
- Three bounded startup checks on the reference executable: targeted regressions, ordinary quiet release profile, and a portable copy in a path containing spaces. These processes were stopped by the harness after approximately 25 seconds; those stops were not crashes.
- SDK output capture confirmed 1280×720 RGBA output into a 1920×1080 window; audio and input initialization were present in the runtime logs.
- Exact SDK GPU bindings and unmodified DLL hashes were verified.
- The original SDK shader loader verified 4,119 corrected shadow shader variants, 21,792 dimension fields, original payload preservation and scope/toggle/format/layer guards.
- Mouse correction was checked across all 65,536 guest axis values against the original guest filter, with maximum error 1. Frame-sampling and critical-section/semaphore checks passed, including an arriving-waiter race case.
- The ordinary release profile confirmed FULL, shadow correction enabled, depth/HDR experiments disabled, 60 FPS disabled, diagnostic hotkeys/self-tests/timing disabled and frame-based input sampling retained.
- Launcher checks covered missing game files, spaces and a trailing backslash using the actual Windows argument parser. Original and copied save profiles were unchanged by the isolated checks.
- Portable archive entries passed CRC and individual SHA256 verification; game files, caches, local paths and personal saves were excluded.

Machine-readable reference details are in [RC1_VALIDATION.json](RC1_VALIDATION.json). This fork preserves all 27 C++ files from that tested source state. Packaging scripts, public build discovery and English documentation are separate tooling changes. Rebuilding on another machine can produce a different executable hash.

## Scope and known issues

A complete campaign playthrough was reported on the preceding development build. There has been no full RC1 playthrough. Startup checks do not prove mission loading, restart behavior or long-session stability on every machine.

Occasional discrete frame stalls remain unresolved. Small residual shadow movements were previously reported as non-critical. The tester reported correct grass and shadows with the enabled fix; these observations are distinct from the synthetic shader-loader checks. Increased internal resolution, alternative SDK versions and other GPU families are outside this validation.
