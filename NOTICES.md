# Component provenance

This project is derived from [crabinacrabic/BadCompanyRecomp](https://github.com/crabinacrabic/BadCompanyRecomp), starting at revision `f78b557b413ca8fdcb67696c18239db4a34d43a2`. Upstream authorship is retained; the fork adds the local fixes and English project documentation described in the release notes. Original historical commits retain their original content and language.

The runtime is [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), version `0.10.0.8-dev.g1406e1b`, which includes code derived from [Xenia](https://github.com/xenia-project/xenia). The SDK's BSD-3-Clause notice is included in `licenses/ReXGlue-LICENSE.txt`. SDK dependencies include SDL3, FFmpeg, Tracy, fmt, spdlog, xxHash, toml++ and Dear ImGui. The provided SDK notices are copied into release packages; consult the corresponding SDK source revision for its complete dependency inventory.

Release packages use the unchanged, optimized SDK RelWithDebInfo binaries `rexruntimerd.dll`, `rexgpu-xenosrd.dll` and `TracyClientrd.dll`. The game executable is built separately in Release. The mixed configuration preserves validated helper interfaces and GPU bindings and uses the normal MSVC runtime.

The controller mapping database is from [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB), retained from the upstream project's `settings/gamecontrollerdb.txt` with its existing notices.

No new license is asserted over upstream project code. Component notices apply to their respective components. Battlefield: Bad Company, its assets and trademarks belong to their respective owners. This project does not include retail game files or claim affiliation with EA or DICE.
