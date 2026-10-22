"""Create an English portable package from a completed Release build."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import tempfile
import zipfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'out/build/local-win-release')
parser.add_argument('--output-dir', type=Path, default=root / 'dist')
parser.add_argument('--symbols', action='store_true')
args = parser.parse_args()
version = '2026.10.22-rc1'
name = f'BadCompanyRecomp-{version}-win64-en'
game = json.loads((root / 'game_version.json').read_text(encoding='utf-8'))
reference = json.loads((root / 'docs/RC1_VALIDATION.json').read_text(encoding='utf-8'))

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

exe = args.build_dir / 'badcompanyrecomp.exe'
if not exe.is_file():
    raise SystemExit('Build local-win-release with tools/build.ps1 first.')
for dll, expected in game['sdk_binary_hashes'].items():
    if sha(args.build_dir / dll) != expected:
        raise SystemExit(f'Pinned SDK mismatch: {dll}')
args.output_dir.mkdir(parents=True, exist_ok=True)
archive = args.output_dir / (name + '.zip')
symbols_archive = args.output_dir / (name + '-symbols.zip')
if archive.exists() or (args.symbols and symbols_archive.exists()):
    raise SystemExit('Refusing to overwrite an existing archive. Select a new --output-dir.')
if args.symbols:
    for suffix in ['pdb', 'map']:
        if not (args.build_dir / ('badcompanyrecomp.' + suffix)).is_file():
            raise SystemExit(f'Missing debug artifact: badcompanyrecomp.{suffix}')
archive_results = []

def zip_checked(path, folder, prefix):
    files = sorted(p for p in folder.rglob('*') if p.is_file())
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in files:
            z.write(p, prefix + '/' + p.relative_to(folder).as_posix())
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        assert len(z.namelist()) == len(files)
        for p in files:
            member = prefix + '/' + p.relative_to(folder).as_posix()
            assert hashlib.sha256(z.read(member)).hexdigest() == sha(p)
    archive_results.append({'file': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path), 'entries': len(files)})

with tempfile.TemporaryDirectory(prefix='bc1-package-', dir=args.output_dir) as temporary:
    stage = Path(temporary) / name
    stage.mkdir()
    def copy(src, relative):
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    for filename in ['badcompanyrecomp.exe', *game['sdk_binary_hashes'], 'gamecontrollerdb.txt']:
        copy(args.build_dir / filename, filename)
    for filename in ['README.md', 'CONTRIBUTING.md', 'NOTICES.md', 'game_version.json', 'Start_BC1.ps1', 'Start_BC1.cmd', 'assets/README.md', 'settings/hardware.toml', 'settings/mapping.toml']:
        copy(root / filename, filename)
    for p in (root / 'docs').glob('*'):
        if p.is_file():
            copy(p, p.relative_to(root))
    for p in (root / 'licenses').rglob('*'):
        if p.is_file():
            copy(p, p.relative_to(root))
    sdk_licenses = root / 'rexglue/win-amd64/licenses'
    if sdk_licenses.is_dir():
        for p in sdk_licenses.rglob('*'):
            if p.is_file():
                copy(p, 'licenses/SDK/' + p.relative_to(sdk_licenses).as_posix())
    source_files = list((root / 'src').glob('*.h')) + list((root / 'src').glob('*.cpp'))
    source_files += [root / p for p in ['CMakeLists.txt', 'CMakePresets.json', 'badcompanyrecomp_manifest.toml', 'default_functions.toml', 'tools/build.ps1']]
    for p in source_files:
        copy(p, 'source_snapshot/' + p.relative_to(root).as_posix())
    info = dict(reference)
    info['executable_sha256'] = sha(exe)
    info['executable_bytes'] = exe.stat().st_size
    info['reference_executable_sha256'] = reference['executable_sha256']
    info['matches_validated_reference_binary'] = info['executable_sha256'] == reference['executable_sha256']
    if not info['matches_validated_reference_binary']:
        for key in ['release_boot_checked', 'release_self_tests_checked', 'portable_path_boot_checked']:
            info[key] = False
        info['validation'] = {'reference_only': True, 'note': 'Reference evidence is in source docs; this rebuilt executable has not been run by packaging.'}
        info['validation_scope'] = 'Packaging and pinned DLL hashes only; rebuilt executable requires fresh runtime validation.'
    info['source_hashes'] = {p.relative_to(root).as_posix(): sha(p) for p in source_files}
    (stage / 'BUILD_INFO.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    files = sorted(p for p in stage.rglob('*') if p.is_file())
    (stage / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(stage).as_posix()}\n' for p in files), encoding='utf-8')
    assert not any(p.suffix.lower() in {'.xex', '.xsh', '.xpso', '.dmp'} for p in files)
    assert not any(set(p.relative_to(stage).parts) & {'runtime', 'logs'} for p in files)
    zip_checked(archive, stage, name)
    if args.symbols:
        symbols = Path(temporary) / 'symbols'
        symbols.mkdir()
        for suffix in ['pdb', 'map']:
            shutil.copy2(args.build_dir / ('badcompanyrecomp.' + suffix), symbols)
        shutil.copy2(stage / 'BUILD_INFO.json', symbols)
        (symbols / 'README.md').write_text('# RC1 debug symbols\n\nPDB and map files are optional. They belong to the executable identified in BUILD_INFO.json.\n', encoding='utf-8')
        zip_checked(symbols_archive, symbols, name + '-symbols')
checksum_file = args.output_dir / (name + '.sha256')
checksum_file.write_text(''.join(f"{a['sha256']}  {a['file']}\n" for a in archive_results), encoding='utf-8')
for item in archive_results:
    print(json.dumps(item))
