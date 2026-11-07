"""Bundle the actual locked Rust dependency license notices for redistribution."""
import hashlib
import json
from pathlib import Path
import subprocess


NOD_144_NOTICES = {
    'nod-LICENSE-MIT.txt': '544ffe9befaefa6b59ab1fdde58789069ba7c1b65432bdea273a16b2117909b7',
    'nod-LICENSE-APACHE.txt': 'fd363f1eb5135402a2e66f4a6173e6eff06daabe79d150acd7c669d33b985b4e',
}
CRATES_IO_SOURCES = {
    'registry+https://github.com/rust-lang/crates.io-index',
    'sparse+https://index.crates.io/',
}


def pinned_nod_notices(package, bundled_dir=None):
    """Only nod 1.4.4 from crates.io may use its reviewed upstream notices."""
    if (package.get('name'), package.get('version')) != ('nod', '1.4.4'):
        return None
    if (package.get('source') not in CRATES_IO_SOURCES or
            package.get('license') != 'MIT OR Apache-2.0'):
        return None
    folder = (Path(bundled_dir) if bundled_dir is not None else
              Path(__file__).resolve().parent/'licenses').resolve()
    notices = []
    for name, expected in sorted(NOD_144_NOTICES.items()):
        path = folder/name
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(folder)
        except (OSError, ValueError) as exc:
            raise RuntimeError(f'Missing or out-of-package pinned nod 1.4.4 notice: {path}') from exc
        if not resolved.is_file():
            raise RuntimeError(f'Invalid pinned nod 1.4.4 notice: {path}')
        raw = resolved.read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(raw).hexdigest() != expected:
            raise RuntimeError(f'Pinned nod 1.4.4 notice hash mismatch: {path}')
        notices.append(resolved)
    return folder, notices


def package_notices(package, bundled_dir=None):
    """Honor Cargo license_file and only read notices inside this crate package."""
    folder = Path(package['manifest_path']).resolve().parent
    candidates = {p for pattern in ('LICENSE*', 'license*', 'COPYING*', 'NOTICE*', 'Copyright*', 'copyright*')
                  for p in folder.rglob(pattern) if p.is_file()}
    explicit = package.get('license_file')
    if explicit:
        path = Path(explicit)
        candidates.add(path if path.is_absolute() else folder/path)
    notices = set()
    for path in candidates:
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(folder)
        except (OSError, ValueError) as exc:
            raise RuntimeError(f"Missing or out-of-package license path for {package['name']}: {path}") from exc
        if not resolved.is_file() or not resolved.read_bytes().strip():
            raise RuntimeError(f"Empty or invalid license notice for {package['name']}: {path}")
        notices.add(resolved)
    if not notices:
        fallback = pinned_nod_notices(package, bundled_dir)
        if fallback is not None:
            return fallback
        raise RuntimeError(f"No redistribution notice found for {package['name']} {package['version']}")
    return folder, sorted(notices)


def collect(root=None):
    root = Path(root) if root else Path(__file__).resolve().parent
    metadata = json.loads(subprocess.check_output(
        ['cargo', 'metadata', '--locked', '--format-version', '1', '--manifest-path', str(root/'disc_reader/Cargo.toml')],
        text=True, encoding='utf-8'))
    chunks = ['Rust dependency notices from the resolved Cargo.lock.\n']
    count = 0
    for package in sorted(metadata['packages'], key=lambda p: (p['name'], p['version'])):
        if package['source'] is None:
            continue
        folder, notices = package_notices(package, bundled_dir=root/'licenses')
        count += 1
        chunks.append('\n'+'='*72+'\n'+package['name']+' '+package['version']+'\nSPDX: '+str(package['license'])+
                      '\nSource: '+str(package.get('repository') or package['source'])+'\n')
        for notice in notices:
            chunks.append('\n--- '+notice.relative_to(folder).as_posix()+' ---\n'+
                          notice.read_text(encoding='utf-8', errors='replace')+'\n')
    output = root/'licenses/Rust-dependency-notices.txt'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(''.join(chunks), encoding='utf-8')
    print(f'Collected notices for {count} dependencies into {output}')
    return output


if __name__ == '__main__':
    collect()
