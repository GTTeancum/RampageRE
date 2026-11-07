"""Safe, local-only asset preparation for the original Xbox build.

No retail files, credentials, network requests, or game launch code are included.
"""
from __future__ import annotations
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
import hashlib
import json
import os
import queue
import re
import shutil
import stat
import struct
import subprocess
import sys
import threading
import uuid

CHUNK = 4 * 1024 * 1024
VOL_COUNT = 352
VOL_END = 1_342_827_563
TARGET_NAME = 'Rampage Xbox'
WII_FOLDERS = ('Game', 'Shell', 'Audio')
WII_MONSTERS = ('Boris', 'Ruby', 'Plucky', 'Fifi', 'Cyril', 'Squirmy', 'Flea', 'Sarah', 'Brian', 'Vern')
WII_REQUIRED = ('Dallas.bdg', 'Dallas_Props.bdg', 'ShellData.bdg', 'Shell.bdg',
                'Rampage.uber', 'Rampage.samp', 'RShell.uber', 'RShell.samp') + tuple(
                    name + suffix for name in WII_MONSTERS for suffix in ('.cmg', '_Anims.cmg'))
RESERVED = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}


class PrepError(Exception):
    pass


class Cancelled(PrepError):
    pass


def check_cancel(cancel):
    if cancel and cancel.is_set():
        raise Cancelled('Preparation canceled. Existing assets were not changed.')


def safe_path(name, *, fatx=True):
    """Reject rather than sanitize names; never silently merge Xbox/Windows paths."""
    if not isinstance(name, str) or not name or '\\' in name:
        raise PrepError(f'Unsafe asset path: {name!r}')
    parts = name.split('/')
    for part in parts:
        if (part in ('', '.', '..') or part[-1:] in (' ', '.') or
                any(ord(c) < 32 or ord(c) > 126 or c in '<>:"|?*' for c in part) or
                part.split('.')[0].upper() in RESERVED or (fatx and len(part) > 42)):
            raise PrepError(f'Unsafe or unsupported Xbox asset path: {name!r}')
    if len(name) > 220:
        raise PrepError(f'Asset path is too long for the Xbox: {name!r}')
    return Path(*parts)


def plain_path(path):
    """Disallow symlinks/junctions at every existing ancestor, including input roots."""
    path = Path(os.path.abspath(path))
    for p in (path, *path.parents):
        try:
            st = p.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(st.st_mode) or getattr(st, 'st_file_attributes', 0) & 0x400:
            raise PrepError(f'Links and junctions are not accepted: {p}')
    return path


def exact(f, offset, count, limit=None):
    if offset < 0 or count < 0 or (limit is not None and offset + count > limit):
        raise PrepError('A disc entry points beyond the end of its file.')
    f.seek(offset)
    data = f.read(count)
    if len(data) != count:
        raise PrepError('The source is truncated or became unreadable (short read).')
    return data


@dataclass(frozen=True)
class Entry:
    name: str
    source: Path
    offset: int
    size: int

    def read(self, n=None):
        with plain_path(self.source).open('rb') as f:
            return exact(f, self.offset, self.size if n is None else min(n, self.size))


def unique(entries):
    names = {}
    for e in entries:
        safe_path(e.name)
        key = e.name.casefold()
        if key in names:
            raise PrepError(f'Case-insensitive duplicate asset path: {e.name}')
        for other in names:
            if key.startswith(other + '/') or other.startswith(key + '/'):
                raise PrepError(f'File/directory asset collision: {e.name}')
        names[key] = e
    return names


def iso_entries(path):
    """Strict cooked 2048-byte ISO9660 reader; no raw BIN or multi-extent support."""
    path = plain_path(path)
    length = path.stat().st_size
    with path.open('rb') as f:
        root = None
        for sector in range(16, 32):
            pvd = exact(f, sector * 2048, 2048, length)
            if pvd[1:6] != b'CD001' or pvd[6] != 1:
                raise PrepError('Select a 2048-byte PS2 ISO, not a Wii image, ZIP, BIN, or compressed image.')
            if pvd[0] == 255:
                break
            if pvd[0] == 1:
                if pvd[128:132] != b'\x00\x08\x08\x00':
                    raise PrepError('Unsupported ISO logical block size.')
                root = pvd[156:190]
                break
        if root is None or root[0] < 34:
            raise PrepError('The PS2 ISO has no valid primary directory.')
        entries, visited, allnames = [], set(), set()

        def walk(lba, size, prefix='', depth=0):
            if depth > 16 or (lba, size) in visited or size > 16 * 1024 * 1024:
                raise PrepError('Invalid, cyclic, or oversized ISO directory.')
            visited.add((lba, size))
            data = exact(f, lba * 2048, size, length)
            pos = 0
            while pos < len(data):
                n = data[pos]
                if not n:
                    pos = (pos // 2048 + 1) * 2048
                    continue
                if n < 34 or pos + n > len(data) or pos % 2048 + n > 2048:
                    raise PrepError('Malformed ISO directory record.')
                r = data[pos:pos+n]
                pos += n
                if 33 + r[32] > n or r[1] or r[26] or r[27] or r[25] & 0x80:
                    raise PrepError('Unsupported or malformed ISO file record.')
                at, count = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0]
                if at != struct.unpack_from('>I', r, 6)[0] or count != struct.unpack_from('>I', r, 14)[0]:
                    raise PrepError('Inconsistent ISO directory lengths.')
                raw = r[33:33+r[32]]
                if raw in (b'\x00', b'\x01'):
                    continue
                name = raw.decode('ascii', 'strict').split(';')[0]
                if name.endswith('.'):
                    name = name[:-1]
                full = f'{prefix}/{name}' if prefix else name
                safe_path(full)
                if full.casefold() in allnames or len(allnames) >= 100000:
                    raise PrepError('Duplicate or excessive ISO directory entries.')
                allnames.add(full.casefold())
                if at * 2048 + count > length:
                    raise PrepError(f'Truncated ISO asset: {full}')
                if r[25] & 2:
                    walk(at, count, full, depth+1)
                else:
                    entries.append(Entry(full, path, at*2048, count))
        walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])
    return unique(entries)


def folder_entries(path, recursive=False):
    root = plain_path(path)
    if not root.is_dir():
        raise PrepError(f'Not a folder: {root}')
    entries = []
    seen = set()
    paths = root.rglob('*') if recursive else root.iterdir()
    for path in paths:
        plain_path(path)
        name = path.relative_to(root).as_posix()
        safe_path(name)
        key = name.casefold()
        if key in seen:
            raise PrepError(f'Case-insensitive duplicate input name: {name}')
        seen.add(key)
        if path.is_file():
            entries.append(Entry(name, path, 0, path.stat().st_size))
        elif not path.is_dir():
            raise PrepError(f'Not a regular source file: {path}')
    return unique(entries)


def validate_vol(entry):
    head = entry.read(16)
    if len(head) != 16:
        raise PrepError('RAMPAGE.VOL is truncated.')
    magic, version, count, names_size = struct.unpack('<4sIII', head)
    if magic != b'PVOL' or version != 0x1001 or count != VOL_COUNT:
        raise PrepError('Unsupported RAMPAGE.VOL. Use the PS2 Europe v1.00 or USA v1.01 release.')
    table_end = 16 + 12*count + names_size
    if names_size < 4+4*count or names_size > 1024*1024 or table_end > entry.size:
        raise PrepError('Malformed RAMPAGE.VOL name table.')
    with entry.source.open('rb') as f:
        raw = exact(f, entry.offset+16, table_end-16)
    index, block = raw[:12*count], raw[12*count:]
    if struct.unpack_from('<I', block)[0] != count:
        raise PrepError('Malformed RAMPAGE.VOL name count.')
    end, seen = 0, set()
    used_names_end = 4+4*count
    ranges = []
    for i in range(count):
        offset, size, _ = struct.unpack_from('<III', index, i*12)
        at = struct.unpack_from('<I', block, 4+4*i)[0]
        if at < 4+4*count or at >= len(block) or b'\0' not in block[at:]:
            raise PrepError('Malformed RAMPAGE.VOL name offset.')
        terminator = block.index(b'\0', at)
        used_names_end = max(used_names_end, terminator+1)
        name = block[at:terminator].decode('ascii', 'strict')
        safe_path(name, fatx=False)
        if name.casefold() in seen:
            raise PrepError('Duplicate name in RAMPAGE.VOL.')
        seen.add(name.casefold())
        ranges.append((offset, offset+size))
        if offset < 16+12*count or offset+size > entry.size:
            raise PrepError('Truncated or invalid RAMPAGE.VOL file range.')
        end = max(end, offset+size)
    true_index_end = 16+12*count+used_names_end
    if any(start < true_index_end for start, _ in ranges):
        raise PrepError('RAMPAGE.VOL data overlaps its actual name table.')
    if end != VOL_END:
        raise PrepError('RAMPAGE.VOL does not match the supported PS2 releases.')


def ps2_plan(source):
    source = plain_path(source)
    if source.is_dir():
        entries = folder_entries(source)
    elif source.name.lower() == 'rampage.vol':
        entries = folder_entries(source.parent)
    else:
        entries = iso_entries(source)
    vol = entries.get('rampage.vol')
    if not vol:
        raise PrepError('The selected PS2 source has no RAMPAGE.VOL at its root.')
    validate_vol(vol)
    cnf = entries.get('system.cnf')
    boot = None
    if cnf:
        if cnf.size > 65536:
            raise PrepError('SYSTEM.CNF is unexpectedly large.')
        text = cnf.read().decode('ascii', 'strict')
        match = re.search(r'BOOT2\s*=\s*cdrom0:[\\/]*([^;\r\n ]+)', text, re.I)
        version = re.search(r'\bVER\s*=\s*([^\s]+)', text, re.I)
        if not match:
            raise PrepError('SYSTEM.CNF has no supported PS2 BOOT2 entry.')
        boot = match[1].upper()
        if boot not in ('SLES_538.00', 'SLUS_213.23'):
            raise PrepError(f'Wrong PS2 disc: {boot}. Expected SLES_538.00 or SLUS_213.23.')
        expected = '1.00' if boot.startswith('SLES') else '1.01'
        if not version or version[1] != expected:
            raise PrepError(f'Unsupported PS2 disc version. Expected {boot} v{expected}.')
    elf = entries.get(boot.lower()) if boot else None
    elf = elf or entries.get('rampage.elf') or entries.get('sles_538.00') or entries.get('slus_213.23')
    if not elf or elf.size < 52 or elf.size > 64*1024*1024:
        raise PrepError('The PS2 executable is missing. Keep RAMPAGE.ELF, SLES_538.00, or SLUS_213.23 beside RAMPAGE.VOL.')
    header = elf.read(52)
    if header[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', header, 18)[0] != 8:
        raise PrepError('The supplied executable is not the required PS2 32-bit MIPS ELF.')
    result = [Entry('data/RAMPAGE.VOL', vol.source, vol.offset, vol.size),
              Entry('data/RAMPAGE.ELF', elf.source, elf.offset, elf.size)]
    if cnf:
        result.append(Entry('data/SYSTEM.CNF', cnf.source, cnf.offset, cnf.size))
    return result


def wii_folder_plan(source):
    source = plain_path(source)
    root = None
    for suffix in ('', 'DATA/files', 'files'):
        candidate = source / suffix
        if candidate.is_dir():
            children = {p.name.casefold(): p for p in candidate.iterdir()}
            if 'game' in children:
                root = candidate
                break
    if root is None:
        raise PrepError('Choose the Wii extracted disc root, DATA/files, or files folder containing Game, Shell, and Audio.')
    found = folder_entries(root, recursive=True)
    chosen = []
    for name, e in found.items():
        parts = e.name.split('/')
        if len(parts) == 2 and parts[0].casefold() in ('game', 'shell', 'audio'):
            canonical = next(x for x in WII_FOLDERS if x.casefold() == parts[0].casefold())
            chosen.append(Entry(f'wii/{canonical}/{parts[1]}', e.source, e.offset, e.size))
    validate_wii_entries(chosen)
    return chosen


def validate_wii_entries(entries):
    table = unique(entries)
    basenames = {Path(e.name).name.casefold() for e in entries}
    missing = [name for name in WII_REQUIRED if name.casefold() not in basenames]
    if missing:
        raise PrepError('Incomplete or wrong Wii source. Missing: ' + ', '.join(missing) + '. Use the full disc, not a Game-only folder.')
    for folder in WII_FOLDERS:
        if not any(k.startswith('wii/'+folder.lower()+'/') for k in table):
            raise PrepError(f'The Wii source is missing its {folder} files.')
    # The runtime treats names across all three folders as one namespace.
    flat = set()
    for e in entries:
        key = Path(e.name).name.casefold()
        if key in flat:
            raise PrepError(f'Ambiguous Wii filename across Game/Shell/Audio: {key}')
        flat.add(key)
        if e.size <= 0 or e.size > 0xffffffff:
            raise PrepError(f'Empty or oversized Wii file: {e.name}')
        if key.endswith(('.bdg', '.cmg')):
            header = e.read(0x38)
            if (len(header) < 0x38 or not header.startswith(b'Pipeworks Bundle File version 1.4') or
                    struct.unpack_from('>I', header, 0x30)[0] != 1400):
                raise PrepError(f'Invalid Wii bundle: {e.name}')


def bundled_reader():
    root = Path(getattr(sys, '_MEIPASS', Path(__file__).parent))
    return root / ('rampage-disc-reader.exe' if os.name == 'nt' else 'rampage-disc-reader')


def reader_run(args, cancel=None, progress=None, helper=None):
    helper = plain_path(helper or bundled_reader())
    if not helper.is_file():
        raise PrepError('The bundled Wii disc reader is missing. Use the complete release ZIP, or select a Dolphin-extracted full Wii disc folder.')
    events = queue.Queue()
    flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    process = subprocess.Popen([str(helper), *map(str, args)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace', creationflags=flags)
    def pump():
        try:
            for line in process.stdout:
                events.put(line)
        finally:
            events.put(None)
    threading.Thread(target=pump, daemon=True).start()
    result, tail = None, []
    try:
        while True:
            check_cancel(cancel)
            try:
                line = events.get(timeout=.1)
            except queue.Empty:
                continue
            if line is None:
                break
            if len(line) > 16*1024*1024:
                raise PrepError('Wii reader returned excessive metadata.')
            tail.append(line.rstrip())
            tail = tail[-12:]
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if 'files' in value:
                result = value
            if progress and value.get('event') == 'progress':
                progress(value.get('done', 0), value.get('total', 0), 'Wii: '+value.get('path', ''))
        code = process.wait()
        if code:
            raise PrepError('Wii disc reading failed:\n' + '\n'.join(tail)[-3000:])
        return result
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdout.close()


def copy_entry(entry, root, cancel=None, report=None):
    check_cancel(cancel)
    src = plain_path(entry.source)
    dst = root / safe_path(entry.name)
    plain_path(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    h = hashlib.sha256()
    with src.open('rb') as f, dst.open('xb') as out:
        f.seek(entry.offset)
        left = entry.size
        while left:
            check_cancel(cancel)
            chunk = f.read(min(CHUNK, left))
            if not chunk:
                raise PrepError(f'Short read while copying {entry.name}. Existing assets were not changed.')
            out.write(chunk)
            h.update(chunk)
            left -= len(chunk)
            if report:
                report(len(chunk), entry.name)
        out.flush()
        os.fsync(out.fileno())
    # Verify the actual staged file, not merely the bytes supplied to write().
    verify = hashlib.sha256()
    with dst.open('rb') as f:
        while True:
            check_cancel(cancel)
            chunk = f.read(CHUNK)
            if not chunk:
                break
            verify.update(chunk)
    if verify.digest() != h.digest() or dst.stat().st_size != entry.size:
        raise PrepError(f'Staged-file verification failed: {entry.name}')
    return {'path': entry.name, 'size': entry.size, 'sha256': h.hexdigest()}


def existing_asset_paths(package):
    target = plain_path(package) / TARGET_NAME
    if not target.is_dir():
        return []
    return [p.name for p in target.iterdir() if p.name.casefold() in ('data', 'wii')]


def pending_recovery(package):
    root = plain_path(package)
    return sorted(root.glob('._rampage-prep-*'))


def recover_transaction(work):
    """Serialize recovery with preparation, including its final rename window."""
    work = plain_path(work)
    with package_lock(work.parent):
        return _recover_transaction(work)


def _recover_transaction(work):
    """Restore a missing target after an interrupted rename, never delete any backup."""
    work = plain_path(work)
    if not work.is_dir() or not work.name.startswith('._rampage-prep-'):
        raise PrepError('Invalid recovery folder.')
    journal = json.loads((work/'transaction.json').read_text(encoding='utf-8'))
    if journal != {'schema': 1, 'target': TARGET_NAME}:
        raise PrepError('Unrecognized recovery journal. Keep this folder for manual recovery.')
    target, backup = work.parent/TARGET_NAME, work/'backup'
    plain_path(target)
    plain_path(backup)
    if target.exists():
        raise PrepError('The game folder already exists. Keep the recovery folder until you have checked both copies; move it outside this ZIP folder before retrying.')
    if not backup.is_dir() or not (backup/'default.xbe').is_file():
        raise PrepError('No complete prior game folder is available for automatic recovery.')
    os.replace(backup, target)
    return target


@contextmanager
def package_lock(package):
    path = plain_path(Path(package)/'._rampage-prep.lock')
    with path.open('a+b') as handle:
        handle.seek(0, os.SEEK_END)
        if not handle.tell():
            handle.write(b'0')
            handle.flush()
        handle.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise PrepError('Another preparation is already running for this package.') from exc
        try:
            yield
        finally:
            handle.seek(0)
            if os.name == 'nt':
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def prepare(package, ps2, wii=None, *, allow_replace=False, cancel=None, progress=None, helper=None):
    package = plain_path(package)
    with package_lock(package):
        return _prepare(package, ps2, wii, allow_replace=allow_replace, cancel=cancel, progress=progress, helper=helper)


def _prepare(package, ps2, wii=None, *, allow_replace=False, cancel=None, progress=None, helper=None):
    """Stage a whole game directory, verify, then commit with rollback-capable renames.

    Cancellation is accepted until commit starts. Original game files are retained
    in a recovery folder if rollback itself fails or the process is interrupted.
    """
    package = plain_path(package)
    target = package/TARGET_NAME
    plain_path(target)
    xbe = plain_path(target/'default.xbe')
    if not xbe.is_file() or Entry('default.xbe', xbe, 0, 4).read() != b'XBEH':
        raise PrepError('Extract the complete release ZIP first. Rampage Xbox/default.xbe is missing or invalid beside this tool.')
    if pending_recovery(package):
        raise PrepError('A previous preparation/recovery folder exists beside the tool. Use Recover, or keep it outside this folder after checking it, then retry.')
    current = folder_entries(target, recursive=True)
    roots = {p.name.casefold(): p.name for p in target.iterdir()}
    if 'rampage.vol' in roots:
        raise PrepError('Legacy root RAMPAGE.VOL would override the newly prepared data folder. '
                        'Extract the whole ZIP into a new folder, or move/back up the old root assets '
                        'outside Rampage Xbox before retrying. No existing files were changed.')
    if ('data' in roots or (wii and 'wii' in roots)) and not allow_replace:
        raise PrepError('Existing assets need explicit replacement confirmation.')
    check_cancel(cancel)
    if progress:
        progress(0, 0, 'Checking the PS2 disc and supported release…')
    planned = ps2_plan(ps2)
    image_wii = bool(wii and not Path(wii).is_dir())
    probe = None
    if wii:
        if progress:
            progress(0, 0, 'Checking the full Wii source…')
        if image_wii:
            plain_path(wii)
            probe = reader_run(['probe', wii], cancel, progress, helper)
            if not probe or not re.fullmatch(r'RPG[A-Z]5D', probe.get('disc_id', '')):
                raise PrepError('The Wii image is not a recognized Rampage: Total Destruction release.')
            for e in probe['files']:
                safe_path(e['path'])
                if not isinstance(e['size'], int) or e['size'] < 0:
                    raise PrepError('Invalid Wii image file size.')
        else:
            planned.extend(wii_folder_plan(wii))
    keep = [e for e in current.values() if e.name.split('/')[0].casefold() != 'data'
            and not (wii and e.name.split('/')[0].casefold() == 'wii')
            and e.name.casefold() != 'asset-manifest.json']
    total = sum(e.size for e in planned+keep) + (sum(e['size'] for e in probe['files']) if probe else 0)
    # Wii images are extracted into temporary storage before staged verification.
    required = total + (sum(e['size'] for e in probe['files']) if probe else 0) + 64*1024*1024
    if shutil.disk_usage(package).free < required:
        raise PrepError(f'Not enough free space. Need at least {required/(1024**3):.2f} GiB free beside this tool.')
    work = package/('._rampage-prep-'+uuid.uuid4().hex)
    work.mkdir()
    (work/'transaction.json').write_text(json.dumps({'schema': 1, 'target': TARGET_NAME}), encoding='utf-8')
    stage, backup = work/'content', work/'backup'
    stage.mkdir()
    committed = False
    moved = False
    keep_work = False
    done = 0
    def report(amount, name):
        nonlocal done
        done += amount
        if progress:
            progress(done, total, 'Copying and verifying '+name)
    try:
        if image_wii:
            extracted = work/'wii-extracted'
            reader_run(['extract', wii, extracted], cancel, progress, helper)
            wii_entries = wii_folder_plan(extracted)
            actual = {e.name.removeprefix('wii/').casefold(): e.size for e in wii_entries}
            expected = {e['path'].casefold(): e['size'] for e in probe['files']}
            if actual != expected:
                raise PrepError('Wii extraction does not match the inspected disc file list.')
            planned.extend(wii_entries)
        unique(planned+keep)
        manifest = []
        for entry in keep+planned:
            manifest.append(copy_entry(entry, stage, cancel, report))
        (stage/'asset-manifest.json').write_text(json.dumps({'schema': 1, 'tool': 'Rampage Xbox Asset Prep',
             'files': manifest, 'wii_requested': bool(wii), 'wii_disc_id': probe.get('disc_id') if probe else None}, indent=2)+'\n', encoding='utf-8')
        check_cancel(cancel)
        if progress:
            progress(total, total, 'Finishing safely. Please do not close this window…')
        # Commit is deliberately not cancelable. A failed second rename restores the original.
        os.replace(target, backup)
        moved = True
        try:
            os.replace(stage, target)
            committed = True
        except OSError as exc:
            try:
                os.replace(backup, target)
                moved = False
            except OSError as rollback:
                keep_work = True
                raise PrepError(f'Installation could not finish or restore automatically. Your original game folder is safe at {backup}. Use Recover before continuing. {rollback}') from exc
            raise PrepError('Installation could not finish. The original game folder was restored.') from exc
        # A successful commit is never reported as a failure merely due to backup cleanup.
        try:
            shutil.rmtree(work)
        except OSError:
            keep_work = True
        return {'target': str(target), 'files': len(planned), 'bytes': sum(e.size for e in planned),
                'recovery_folder': str(work) if keep_work else None, 'manifest': manifest}
    finally:
        if not committed and not keep_work:
            if moved and backup.exists() and not target.exists():
                try:
                    os.replace(backup, target)
                except OSError:
                    keep_work = True
            if not keep_work:
                shutil.rmtree(work, ignore_errors=True)
