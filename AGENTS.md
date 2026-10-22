# Instructions for contributors and coding agents

This fork recompiles Battlefield: Bad Company (Xbox 360, Title ID `454107F9`) for Windows x64 using ReXGlue SDK `0.10.0.8-dev.g1406e1b`.

## Required workflow

- Write project documentation, source comments, tooling output and new commit messages in English.
- Use `tools/build.ps1` for CLI builds. It initializes Visual Studio and the validated LLVM toolchain before code generation, CMake configuration and compilation.
- Treat `generated/` as read-only output. Add guest overrides to `src/game_fixes.h` and function discovery metadata to the TOML manifests. Never patch generated C++ directly.
- Preserve the tested release profile: **30 FPS, FULL readback, D3D12 ROV, native 1280×720 rendering into a 1920×1080 window**, the shadow-array fix and mouse corrections.
- Keep experimental readback, capture probes, diagnostic hotkeys, scratch self-tests and periodic timing reports disabled in ordinary release launches.
- Do not substitute SDK DLLs. `game_version.json` records the required image and SDK hashes. Guest helper bindings and GPU vtable offsets depend on those exact binaries.
- Keep the guest input-frame epoch increment active even when timing diagnostics are disabled. Mouse sampling depends on it.
- Do not commit game data, generated code, SDK/build binaries, shader caches, logs, dumps, personal saves, local paths or credentials.

## Guest overrides

ReXGlue emits a strong `__imp__sub_XXXXXXXX` implementation and a weak `sub_XXXXXXXX` alias. A `REX_HOOK_RAW(sub_XXXXXXXX)` definition in `src/game_fixes.h` overrides the weak alias. Call `__imp__sub_XXXXXXXX(ctx, base)` when the original implementation is required. Validate image bounds, object layout and error-state preservation when changing these hooks.

## Validation

Run `python tools/verify_source.py` before publishing source changes. Use the build wrapper for compile validation and `tools/test_run.ps1` for a bounded launch. The latter runs with an isolated writable profile and stops only its own child process. It does not automate campaign input.

Maintain the distinction between reported playthrough evidence and automated checks. A preceding development build completed the campaign according to the tester; RC1 passed startup/render/audio and targeted regressions. RC1 has not yet completed a new full playthrough. Residual frame stalls remain unresolved.

## Project layout

| Path | Purpose |
|---|---|
| `src/game_fixes.h` | Guest function overrides |
| `src/badcompanyrecomp_app.h` | Runtime setup, memory handling, graphics and input integration |
| `src/game_cvars.h` | Game-specific configuration flags |
| `src/trial_*.h` | Existing validated fixes and optional diagnostic helpers |
| `settings/` | Rendering and input defaults |
| `tools/build.ps1` | Supported CLI build entry point |
| `Start_BC1.ps1` | Portable launcher and supported XEX check |
| `tools/package_release.py` | Allowlisted portable package creation |
| `docs/` | Validation scope, known issues and release notes |

The `trial_` source names are retained to avoid changing the tested implementation solely for renaming. Follow the user's explicit instructions when they change a project preference.
