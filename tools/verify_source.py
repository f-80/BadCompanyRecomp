"""Check publication boundaries and the documented release configuration."""
from pathlib import Path
import json
import re
import subprocess

root = Path(__file__).resolve().parent.parent
excluded = {'.git', 'assets', 'generated', 'rexglue', 'out', 'build', 'bin', 'obj', 'dist', 'runtime', 'logs', 'cache', '__pycache__'}
allowed_assets = root / 'assets/README.md'
files = []
for path in root.rglob('*'):
    if not path.is_file():
        continue
    relative = path.relative_to(root)
    if path == allowed_assets or not set(relative.parts[:-1]) & excluded:
        if path.name not in {'CMakeUserPresets.json'} and not path.name.startswith('game-path'):
            files.append(path)
errors = []
for path in files:
    relative = path.relative_to(root).as_posix()
    if path.suffix.lower() in {'.exe', '.dll', '.lib', '.pdb', '.map', '.xex', '.iso', '.dmp', '.xsh', '.xpso', '.zip'}:
        errors.append(f'Unexpected binary/private artifact: {relative}')
        continue
    text = path.read_text(encoding='utf-8-sig')
    if re.search(r'[\u0400-\u04ff]', text):
        errors.append(f'Non-English Cyrillic text: {relative}')
    if re.search(r'[A-Za-z]:[\\/](?:Users|Recompiles)[\\/]', text):
        errors.append(f'Personal or fixed workspace path: {relative}')
    if re.search(r'\b(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[A-Z0-9]{16})\b', text):
        errors.append(f'Possible credential: {relative}')
hardware = (root / 'settings/hardware.toml').read_text(encoding='utf-8-sig')
for key, expected in {
    'readback_resolve': '"full"', 'badco_fps_60': 'false', 'render_target_path_d3d12': '"rov"',
    'badco_shadow_array_fetch_fix': 'true', 'badco_shadow_depth_readback_fast': 'false',
    'badco_hdr_readback_fast': 'false', 'badco_diagnostic_hotkeys': 'false',
    'badco_startup_self_tests': 'false', 'badco_timing_diagnostics': 'false',
}.items():
    if not re.search(r'^' + key + r'\s*=\s*' + re.escape(expected) + r'\s*$', hardware, re.M):
        errors.append(f'Unexpected release setting: {key}')
presets = json.loads((root / 'CMakePresets.json').read_text(encoding='utf-8'))
release = next(p for p in presets['configurePresets'] if p['name'] == 'local-win-release')
assert release['cacheVariables']['BADCO_RELEASE_PACKAGE'] == 'ON'
assert release['cacheVariables']['CMAKE_MAP_IMPORTED_CONFIG_RELEASE'] == 'RelWithDebInfo'
if (root / '.git').exists():
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
    for name in filter(None, tracked):
        p = Path(name)
        if set(p.parts[:-1]) & excluded and name != 'assets/README.md':
            errors.append(f'Private/generated path tracked by Git: {name}')
        if p.name.startswith('game-path') or p.name == 'CMakeUserPresets.json' or p.name.startswith('.env'):
            errors.append(f'Local configuration or credentials tracked by Git: {name}')
if errors:
    raise SystemExit('\n'.join(errors))
print(f'Publication checks passed for {len(files)} English text files; FULL/30 FPS/ROV profile verified.')
