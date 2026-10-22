# Settings

BadCompanyRecomp loads `hardware.toml` and `mapping.toml` from this directory. The files contain flat `key = value` entries; do not add TOML section headers. Settings are loaded during path configuration and again after runtime setup so that later-registered GPU flags can be applied.

| File | Purpose |
|---|---|
| [hardware.toml](hardware.toml) | Rendering, window, release profile and game-specific fixes |
| [mapping.toml](mapping.toml) | SDL input, keyboard/controller bindings and mouse sensitivity |
| [gamecontrollerdb.txt](gamecontrollerdb.txt) | Additional mappings from SDL_GameControllerDB |

Command-line flags and `REX_*` environment variables take precedence over configuration files. The launcher explicitly pins FULL readback, the shadow-array correction, 30 FPS and disabled diagnostic/experimental modes.

## RC1 defaults

| Setting | Value |
|---|---|
| `readback_resolve` | `"full"` |
| `render_target_path_d3d12` | `"rov"` |
| `resolution_scale` | `1` |
| `window_width` / `window_height` | `1920` / `1080` |
| `fullscreen` | `false` |
| `badco_fps_60` | `false` |
| `badco_shadow_array_fetch_fix` | `true` |
| `badco_disable_film_grain` | `true` |
| `badco_display_gamma_type` | `1` (monitor sRGB) |
| `badco_mouse_y_scale` | `3.0` |
| `mnk_mode` / `mnk_mouse` | `true` / `true` |

Internal rendering remains 1280×720. A 1920×1080 window does not increase the internal resolution. The experimental depth/HDR readback flags, diagnostic hotkeys, startup self-tests and periodic timing reports are disabled in the release profile.

F8 controls mouse capture. F4 opens SDK settings and F7 opens achievements. Experimental F6 and F9–F12 bindings are disabled. The runtime settings overlay can save changes to the configured hardware file, so review the resulting changes before committing them.

Use `game-path.txt` or the launcher's `-GameDataRoot` parameter to select external game files. Putting `game_data_root` in these files is insufficient because the SDK needs the game path before ordinary settings are loaded. Source generation still requires `assets/default.xex`.
