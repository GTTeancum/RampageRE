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


def rust_host():
    """Use the compiler that builds the helper, not Python's platform name."""
    version = subprocess.check_output(['rustc', '-vV'], text=True, encoding='utf-8')
    hosts = [line.partition(':')[2].strip() for line in version.splitlines()
             if line.startswith('host:')]
    allowed = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.')
    if len(hosts) != 1 or '-' not in hosts[0] or not set(hosts[0]) <= allowed:
        raise RuntimeError('Cannot determine a valid rustc host target from rustc -vV')
    return hosts[0]


def reachable_packages(metadata):
    """metadata.packages is unfiltered; walk only the filtered resolve graph."""
    resolve = metadata.get('resolve')
    if not isinstance(resolve, dict) or not isinstance(resolve.get('root'), str) or not resolve['root']:
        raise RuntimeError('Cargo metadata has no resolve.root')

    def index(records, label):
        if not isinstance(records, list):
            raise RuntimeError(f'Cargo metadata has no {label} list')
        result = {}
        for record in records:
            identifier = record.get('id') if isinstance(record, dict) else None
            if not isinstance(identifier, str) or not identifier or identifier in result:
                raise RuntimeError(f'Cargo metadata has invalid or duplicate {label} IDs')
            result[identifier] = record
        return result

    packages = index(metadata.get('packages'), 'packages')
    nodes = index(resolve.get('nodes'), 'resolve.nodes')
    pending = [resolve['root']]
    seen = set()
    while pending:
        identifier = pending.pop()
        if identifier in seen:
            continue
        if identifier not in packages or identifier not in nodes:
            raise RuntimeError(f'Incomplete Cargo resolve graph: missing package or node for {identifier}')
        seen.add(identifier)
        deps = nodes[identifier].get('deps')
        if not isinstance(deps, list):
            raise RuntimeError(f'Incomplete Cargo resolve graph: missing deps for {identifier}')
        for dep in deps:
            target = dep.get('pkg') if isinstance(dep, dict) else None
            if not isinstance(target, str) or not target:
                raise RuntimeError(f'Incomplete Cargo resolve graph: invalid dependency for {identifier}')
            pending.append(target)
    return sorted((packages[key] for key in seen),
                  key=lambda package: (package['name'], package['version'], package['id']))


def collect(root=None):
    root = Path(root) if root else Path(__file__).resolve().parent
    target = rust_host()
    metadata = json.loads(subprocess.check_output(
        ['cargo', 'metadata', '--locked', '--format-version', '1', '--filter-platform', target,
         '--manifest-path', str(root/'disc_reader/Cargo.toml')],
        text=True, encoding='utf-8'))
    packages = reachable_packages(metadata)
    chunks = [f'Rust dependency notices from the resolved Cargo.lock for {target}.\n']
    count = 0
    errors = []
    for package in packages:
        if package['id'] == metadata['resolve']['root']:
            continue
        try:
            folder, notices = package_notices(package, bundled_dir=root/'licenses')
            section = ['\n'+'='*72+'\n'+package['name']+' '+package['version']+'\nSPDX: '+str(package['license'])+
                       '\nSource: '+str(package.get('repository') or package.get('source') or 'local path')+'\n']
            for notice in notices:
                section.append('\n--- '+notice.relative_to(folder).as_posix()+' ---\n'+
                               notice.read_text(encoding='utf-8', errors='replace')+'\n')
        except (RuntimeError, OSError, ValueError) as exc:
            errors.append(f"{package['name']} {package['version']}: {exc}")
            continue
        count += 1
        chunks.extend(section)
    if errors:
        raise RuntimeError('Rust dependency notice collection failed:\n- '+'\n- '.join(errors))
    output = root/'licenses/Rust-dependency-notices.txt'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(''.join(chunks), encoding='utf-8')
    print(f'Collected notices for {count} dependencies into {output}')
    return output


if __name__ == '__main__':
    collect()
