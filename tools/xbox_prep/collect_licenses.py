"""Collect and require notices from the exact Windows Python/build environment."""
from importlib import metadata
from pathlib import Path
import argparse
import re
import shutil
import sys
import tkinter

PACKAGES = ('pyinstaller', 'pyinstaller-hooks-contrib', 'altgraph', 'pefile', 'pywin32-ctypes', 'packaging')
REQUIRED_PACKAGES = {'pyinstaller', 'pyinstaller-hooks-contrib'}


def nonempty(path):
    return path.is_file() and bool(path.read_bytes().strip())


def combined_tcl_tk(text):
    """Official Windows Python can incorporate these notices in root LICENSE.txt."""
    joint = re.search(r'(?im)^\s*tcl\s*(?:/|and|&)\s*tk\b', text)
    separate = (re.search(r'(?im)^\s*tcl(?:\s+license|\s+\d|\s*$)', text)
                and re.search(r'(?im)^\s*tk(?:\s+license|\s+\d|\s*$)', text))
    # A component heading alone is not a notice.
    return bool((joint or separate) and re.search(r'(?i)copyright|permission|redistribution', text))


def collect(output, *, prefix=None, tcl_library=None, distribution=None):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    prefix = Path(prefix) if prefix else Path(sys.base_prefix)
    distribution = distribution or metadata.distribution
    copied = set()
    def save(source, name):
        if not nonempty(source):
            raise RuntimeError(f'Empty or missing redistribution notice: {source}')
        if name not in copied:
            shutil.copyfile(source, out/name)
            copied.add(name)
    python_license = next((p for p in (prefix/'LICENSE.txt', prefix/'LICENSE', prefix/'share/doc/python3/LICENSE')
                           if nonempty(p)), None)
    if python_license is None:
        raise RuntimeError(f'Cannot locate the Python redistribution license under {prefix}; include the exact official build notice before publishing.')
    save(python_license, 'Python-LICENSE.txt')
    tcl = Path(tcl_library) if tcl_library else Path(tkinter.Tcl().eval('info library'))
    found = {'tcl': False, 'tk': False}
    # Official Windows layouts use <Python>/tcl/tcl8.x and tk8.x. Tcl's
    # reported library path also covers non-default install locations.
    roots = [(tcl, 'tcl')]
    roots += [(p, 'tk') for p in tcl.parent.glob('tk*') if p.is_dir()]
    roots += [(p, 'tcl') for p in (prefix/'tcl').glob('tcl*') if p.is_dir()]
    roots += [(p, 'tk') for p in (prefix/'tcl').glob('tk*') if p.is_dir()]
    for folder, kind in roots:
        for path in folder.iterdir() if folder.is_dir() else ():
            if path.name.lower().startswith(('license', 'copying', 'notice')) and nonempty(path):
                save(path, kind+'-'+folder.name+'-'+path.name+'.txt')
                found[kind] = True
    # Some Windows bundles retain distinct notices in the common Tcl directory
    # or beside Python instead of inside each script-library folder.
    for folder in (prefix, prefix/'tcl', tcl.parent):
        if not folder.is_dir():
            continue
        for path in folder.iterdir():
            name = path.name.lower()
            if not nonempty(path) or not ('license' in name or name.startswith(('copying','notice'))):
                continue
            if 'tcl' in name and 'tk' not in name:
                save(path, 'tcl-'+path.name+'.txt'); found['tcl'] = True
            elif 'tk' in name and 'tcl' not in name:
                save(path, 'tk-'+path.name+'.txt'); found['tk'] = True
            elif ('tcl' in name and 'tk' in name) or combined_tcl_tk(path.read_text(encoding='utf-8',errors='replace')):
                save(path, 'Tcl-Tk-'+path.name+'.txt'); found = {'tcl': True, 'tk': True}
    missing = [name for name, present in found.items() if not present]
    if missing:
        raise RuntimeError('Missing required '+', '.join(missing)+' redistribution notices. Checked the active Tcl library, '
                           f'{prefix}/tcl and root Python LICENSE.txt. Add the notices from this exact Windows Python/Tcl/Tk build; do not publish without them.')
    for name in PACKAGES:
        try:
            dist = distribution(name)
        except metadata.PackageNotFoundError:
            if name in REQUIRED_PACKAGES:
                raise RuntimeError(f'Required build package is missing: {name}') from None
            continue
        notices = []
        for file in dist.files or ():
            if file.name.lower().startswith(('license', 'copying', 'notice')):
                candidate = Path(dist.locate_file(file))
                if nonempty(candidate):
                    notices.append((file, candidate))
        if not notices:
            raise RuntimeError(f'Installed package {name} has no nonempty redistribution notice in its distribution metadata.')
        # Keep distinct files even if a wheel contains several LICENSE filenames.
        for index, (file, candidate) in enumerate(sorted(notices, key=lambda x:str(x[0]))):
            save(candidate, f'{name}-{index+1}-{file.name}.txt')
    (out/'BUILD-NOTICES.txt').write_text('Required notices verified: Python, Tcl, Tk, PyInstaller, PyInstaller hooks.\n'
                                      'Licenses copied from the exact build environment:\n'+'\n'.join(sorted(copied))+'\n', encoding='utf-8')
    print('\n'.join(sorted(copied)))
    return copied


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    collect(args.output)


if __name__ == '__main__':
    main()
