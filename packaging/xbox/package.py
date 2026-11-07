"""Create the asset-free console test ZIP; never collect arbitrary work folders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def verify_runtime(root=ROOT):
    packaging = root / 'packaging/xbox'
    runtime = json.loads((packaging / 'runtime.json').read_text())
    xbe = (packaging / 'default.xbe').read_bytes()
    if not xbe.startswith(b'XBEH') or len(xbe) != runtime['bytes'] or sha256(xbe) != runtime['sha256']:
        raise ValueError('The frozen Xbox executable does not match the approved runtime')
    snapshot = (packaging / runtime['source_snapshot']).read_bytes()
    if sha256(snapshot) != runtime['source_snapshot_sha256']:
        raise ValueError('Source snapshot manifest changed')
    for name, expected in json.loads(snapshot).items():
        if not name.startswith(('src/', 'tests/')) or '..' in Path(name).parts:
            raise ValueError('Unsafe source snapshot path')
        if sha256((root / name).read_bytes()) != expected:
            raise ValueError(f'Source differs from the Xbox snapshot: {name}')
    return runtime


def build(exe, output, root=ROOT):
    runtime = verify_runtime(root)
    exe = Path(exe).resolve()
    gui = exe.read_bytes()
    if not gui.startswith(b'MZ'):
        raise ValueError('Expected the Windows asset preparation EXE')
    commit = os.environ.get('GITHUB_SHA') or subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    repo = os.environ.get('GITHUB_REPOSITORY', 'GTTeancum/RampageRE')
    run = os.environ.get('GITHUB_RUN_ID')
    info = dict(schema=1, source_commit=commit, source_url=f'https://github.com/{repo}/tree/{commit}',
                workflow_run=f'https://github.com/{repo}/actions/runs/{run}' if run else None,
                runtime=runtime, asset_tool_sha256=sha256(gui), original_game_assets_included=False)
    payload = {
        'Prepare Rampage Xbox.exe': gui,
        'Rampage Xbox/default.xbe': (root / 'packaging/xbox/default.xbe').read_bytes(),
        'README.txt': (root / 'packaging/xbox/README.txt').read_bytes(),
        'BUILD-INFO.json': (json.dumps(info, indent=2) + '\n').encode(),
        'LICENSES/THIRD-PARTY-NOTICES.txt': (root / 'packaging/xbox/THIRD-PARTY-NOTICES.txt').read_bytes(),
    }
    # These directories contain only reviewed license text, never runtime/game data.
    for parent in (root / 'packaging/xbox/licenses', root / 'tools/xbox_prep/licenses'):
        for path in sorted(parent.glob('*.txt')):
            name = 'LICENSES/' + path.name
            if name in payload:
                raise ValueError(f'Duplicate notice: {name}')
            payload[name] = path.read_bytes()
    if not any(name.startswith('LICENSES/') and 'nod' in name.lower() for name in payload):
        raise ValueError('Wii reader license notice is missing')
    for name in payload:
        if Path(name).suffix.lower() in ('.vol', '.elf', '.iso', '.bin', '.cue', '.bdg', '.cmg', '.samp', '.uber'):
            raise ValueError(f'Game data is never packaged: {name}')
    payload['SHA256SUMS.txt'] = ''.join(f'{sha256(data)}  {name}\n' for name, data in sorted(payload.items())).encode()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    target = output / f'Rampage-Xbox-hardware-test-{commit[:12]}.zip'
    with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, (2026, 11, 7, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            archive.writestr(entry, data)
    with zipfile.ZipFile(target) as archive:
        if set(archive.namelist()) != set(payload) or archive.testzip():
            raise ValueError('Package verification failed')
        for name, data in payload.items():
            if sha256(archive.read(name)) != sha256(data):
                raise ValueError(f'Package payload mismatch: {name}')
    (output / 'SHA256SUMS.txt').write_text(f'{sha256(target.read_bytes())}  {target.name}\n', encoding='ascii')
    print(f'Verified asset-free package: {target} ({target.stat().st_size:,} bytes)')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--out', type=Path, default=Path('dist/console-package'))
    parser.add_argument('--verify-runtime', action='store_true')
    args = parser.parse_args()
    if args.verify_runtime:
        verify_runtime()
        print('Xbox binary and 282 source/test files match their frozen hashes')
    else:
        if not args.exe:
            parser.error('--exe is required')
        build(args.exe, args.out)
