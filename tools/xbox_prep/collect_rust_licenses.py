"""Bundle the actual locked Rust dependency license notices for redistribution."""
import json
from pathlib import Path
import subprocess


def package_notices(package):
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
        folder, notices = package_notices(package)
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
