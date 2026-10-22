# Contributing

Use English for issues, documentation, comments and new commit messages. Keep the original upstream attribution and report behavior with the exact game image, SDK and build configuration.

For a bug report, include the mission and approximate location, reproduction steps, expected/actual behavior, GPU/driver information and whether the problem repeats after restarting. Share only relevant log excerpts; remove personal paths. Do not attach game files or personal saves to public issues.

Run `python tools/verify_source.py`, build through `tools/build.ps1`, and describe which startup, regression or gameplay checks were actually performed. Keep experimental graphics changes separate from the FULL release profile until their visual correctness and stability have been tested.

The current implementation is Windows-specific in several areas. Other platform presets inherited from upstream are not evidence of working Linux/macOS support. Performance work should preserve guest timing, input-frame sampling, save/load behavior and the exact scope of the shadow-array correction.
