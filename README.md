# Battlefield: Bad Company — PC Recompilation

A Windows x64 static recompilation of **Battlefield: Bad Company (2008)**, based on [crabinacrabic/BadCompanyRecomp](https://github.com/crabinacrabic/BadCompanyRecomp) and the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). This fork contains the local campaign, graphics, input and release-profile fixes developed on top of upstream revision `f78b557b413ca8fdcb67696c18239db4a34d43a2`.

**Status: 2026.10.22 RC1.** A complete campaign playthrough was reported on the preceding development build. RC1 itself has passed bounded startup/render/audio checks and targeted regressions; a complete RC1 playthrough has not yet been verified. Occasional discrete frame stalls remain a known issue.

This repository contains source code and configuration only. Supply your own extracted Xbox 360 game files. Game data, generated recompilation output, SDK binaries, caches and personal saves are not included.

## Supported version and requirements

| Item | Supported configuration |
|---|---|
| Game | Battlefield: Bad Company, Xbox 360, USA |
| Title ID | `454107F9` |
| `default.xex` SHA256 | `2072aebe08f5a6e3aa30a09171278277151d3d6557617ee762983bbdae9a196a` |
| Runtime | ReXGlue SDK `0.10.0.8-dev.g1406e1b`, Windows AMD64 |
| Host | Windows 10/11 x64 with a D3D12 ROV-capable GPU |
| Frame rate | 30 FPS |
| Internal resolution | 1280×720; initial window 1920×1080 |

The tested host uses an NVIDIA RTX 3080. Other hardware is not covered by that validation. The [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) is required when the runtime is not already installed.

## Run a prepared package

1. Extract the complete Windows release archive into a writable folder.
2. Put the extracted game files in `assets/`, with `default.xex` directly inside that directory. Alternatively, create `game-path.txt` beside the launcher and put the game directory path on one line.
3. Run **`Start_BC1.cmd`**.

The launcher verifies the supported XEX hash and selects the FULL release profile. Paths with spaces and a trailing backslash are supported. You can also launch from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Start_BC1.ps1 -GameDataRoot 'D:\Games\Bad Company'
```

Saves and profile data are stored in `runtime/user`, shader caches in `runtime/cache`, and logs in `logs`. Copy `runtime/user` separately when moving your progress to another installation. A portable package has no personal saves or cache; its first startup and first encounters with new effects can take longer.

## Release profile

- **FULL readback** and D3D12 ROV are retained for the confirmed grass and lighting behavior.
- The shadow texture-array layer correction covers 4,119 verified shader variants.
- Mouse input is sampled once per guest frame, with an inverse correction for the game's right-stick dead zone and a 3× vertical sensitivity multiplier.
- Monitor-oriented gamma is selected and the engine film-grain pass is disabled.
- The original 30 FPS limit is retained for vehicle and gameplay timing.
- Diagnostic self-tests, periodic timing reports and experimental graphics hotkeys are disabled in the ordinary release profile.

The 1920×1080 window displays the native 1280×720 image. RC1 does not enable an increased internal render scale. Experimental depth/HDR readback paths are disabled. See [settings/hardware.toml](settings/hardware.toml) and [settings/mapping.toml](settings/mapping.toml).

## Controls

| Action | Default binding |
|---|---|
| Move / look | WASD / mouse |
| Fire / aim | Left / right mouse button |
| Jump / crouch / sprint | Space / C / Shift |
| Interact / reload | E / R, depending on game context |
| Switch weapon | 2 |
| Grenade / gadget / melee | Q / G / F |
| Pause | Escape |
| Capture or release mouse | F8 |
| Runtime settings / achievements | F4 / F7 |

SDL-compatible gamepads are supported. Experimental F6 and F9–F12 bindings are not registered by the release profile. The game's own text, audio and button prompts come from your game files; this fork translates the project documentation and tooling, not the retail game resources.

## Build from source

The validated toolchain is **Visual Studio 2022 C++ x64 tools and Windows SDK, LLVM 18.1.8 (`clang-cl`), CMake 3.25+ and Ninja**. Only Windows x64 is currently validated, even though upstream includes additional platform presets.

1. Obtain the exact ReXGlue SDK archive `rexglue-sdk-0.10.0.8-dev.g1406e1b-win-amd64.zip` from the [SDK releases](https://github.com/rexglue/rexglue-sdk/releases). Extract it so `rexglue/win-amd64/bin/rexglue.exe` exists.
2. Put the supported extracted game in `assets/`. Source generation needs the game files there even if the runtime launcher uses an external game path.
3. Ensure CMake and Ninja are available, then run the build wrapper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Config local-win-release -LLVMRoot 'C:\Program Files\LLVM' -Jobs 2
```

The wrapper discovers Visual Studio, initializes its x64 environment, verifies the pinned SDK DLL hashes, runs ReXGlue code generation and builds the selected preset. `-VisualStudioPath` can select an explicit installation. `-ValidateOnly` checks prerequisites without generating or compiling sources.

The output is `out/build/local-win-release/badcompanyrecomp.exe`. Run `Start_BC1.cmd` from the repository root, or use `Start_BC1.ps1 -Config local-win-relwithdebinfo` for a development build.

**Use `tools/build.ps1` for CLI builds. Do not edit `generated/`.** Guest hooks belong in `src/game_fixes.h`; code generation replaces its output. LLVM 18 requires an existing per-source `/Od` workaround for generated module 82; the rest of the release uses `/O2 /Ob2 /DNDEBUG`.

The game executable uses the **Release** configuration. SDK runtime/GPU DLLs deliberately remain in the tested **RelWithDebInfo** configuration with the normal MSVC runtime: the fixes bind to that exact ABI and GPU layout. Substituting SDK Release DLLs or upgrading the SDK requires revalidating the bindings. See [game_version.json](game_version.json).

## Verify and package

Python 3.9+ is used for repository verification and packaging:

```powershell
python tools/verify_source.py
python tools/package_release.py
```

Packaging reads the completed release build and creates an English portable ZIP under `dist/`. It verifies pinned SDK hashes, excludes game files and personal data, and writes file/archive SHA256 checksums. Debug symbols can be packaged separately with `--symbols`.

See [RC1 validation](docs/RC1_VALIDATION.md), [release notes](docs/RELEASE_NOTES.md), [contribution guidance](CONTRIBUTING.md), and [component provenance](NOTICES.md). A successful startup check does not establish campaign stability or eliminate the known frame stalls.
