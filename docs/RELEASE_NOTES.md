# Release notes

## 2026.10.22 RC1

The release candidate packages the accumulated local fixes on top of upstream `f78b557b413ca8fdcb67696c18239db4a34d43a2`.

- Preserves memory/page handling, guest synchronization, file I/O and campaign/save-load fixes developed after the upstream title-screen milestone.
- Uses FULL readback and the validated shadow texture-array correction, covering 4,119 shader variants. Grass placement was confirmed correct with this profile.
- Retains mouse frame sampling, right-stick dead-zone compensation and increased vertical sensitivity.
- Selects monitor gamma and disables the engine film-grain pass.
- Builds an optimized Release executable with quiet startup defaults while retaining exact SDK RelWithDebInfo DLLs for ABI compatibility.
- Keeps 30 FPS and native 1280×720 rendering in a 1920×1080 window.
- Disables diagnostic hotkeys, startup scratch tests, periodic timing output and experimental readback in ordinary release launches. F8 mouse capture and SDK F4/F7 controls remain available.
- Adds English documentation, portable launch/build/package tooling, game/SDK hash checks and source-publication exclusions.

Known limitations: occasional frame stalls remain; a full campaign playthrough of this RC1 binary is pending. This is a release candidate, not a claim that every remaining visual or performance issue has been resolved.
