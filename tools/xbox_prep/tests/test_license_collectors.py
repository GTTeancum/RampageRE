"""Synthetic notice fixtures only; never contacts a package registry."""
from contextlib import redirect_stdout
from importlib import metadata
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import collect_licenses as pylicenses
import collect_rust_licenses as rustlicenses


class Distribution:
    def __init__(self, root, files):
        self.root=root
        self.files=[Path(x) for x in files]
    def locate_file(self, file):
        return self.root/file


class LicenseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1])
        self.root=Path(self.temp.name)
        self.prefix=self.root/'Python312'
        self.tcl=self.prefix/'tcl/tcl8.6'
        self.tk=self.prefix/'tcl/tk8.6'
        self.tcl.mkdir(parents=True);self.tk.mkdir()
        (self.prefix/'LICENSE.txt').write_text('Python copyright and permission notice')
        self.dists={}
        for name in pylicenses.REQUIRED_PACKAGES:
            folder=self.root/name;folder.mkdir()
            (folder/'COPYING.txt').write_text(name+' copyright and permission notice')
            self.dists[name]=Distribution(folder,['COPYING.txt'])
    def tearDown(self):self.temp.cleanup()
    def distribution(self,name):
        if name not in self.dists:raise metadata.PackageNotFoundError(name)
        return self.dists[name]
    def collect(self, *, tcl_patchlevel=None, notice_dir=None):
        with redirect_stdout(io.StringIO()):
            return pylicenses.collect(self.root/'out',prefix=self.prefix,tcl_library=self.tcl,
                                      distribution=self.distribution,tcl_patchlevel=tcl_patchlevel,
                                      notice_dir=notice_dir)
    def libraries(self):
        (self.tcl/'license.terms').write_text('Tcl copyright and permission notice')
        (self.tk/'license.terms').write_text('Tk copyright and permission notice')
    def test_windows_per_library_layout(self):
        self.libraries();copied=self.collect()
        self.assertIn('tcl-tcl8.6-license.terms.txt',copied)
        self.assertIn('tk-tk8.6-license.terms.txt',copied)
        self.assertTrue(any(n.startswith('pyinstaller-') for n in copied))
    def test_windows_combined_root_license(self):
        (self.prefix/'LICENSE.txt').write_text('Python permission notice\n\nTcl/Tk\n------\nCopyright and permission terms for both libraries.\n')
        copied=self.collect();self.assertIn('Tcl-Tk-LICENSE.txt.txt',copied)
    def test_windows_common_directory_notices(self):
        (self.prefix/'tcl/license.terms.tcl').write_text('Tcl permission notice')
        (self.prefix/'tcl/license.terms.tk').write_text('Tk permission notice')
        copied=self.collect()
        self.assertIn('tcl-license.terms.tcl.txt',copied)
        self.assertIn('tk-license.terms.tk.txt',copied)
    def test_missing_tk_fails(self):
        (self.tcl/'license.terms').write_text('Tcl notice')
        with self.assertRaisesRegex(RuntimeError,'Missing required tk'):self.collect()
    def test_empty_tcl_fails(self):
        self.libraries();(self.tcl/'license.terms').write_text('  ')
        with self.assertRaisesRegex(RuntimeError,'Missing required tcl'):self.collect()
    def test_missing_pyinstaller_fails(self):
        self.libraries();del self.dists['pyinstaller']
        with self.assertRaisesRegex(RuntimeError,'Required build package is missing: pyinstaller'):self.collect()
    def test_pyinstaller_without_license_fails(self):
        self.libraries();self.dists['pyinstaller'].files=[]
        with self.assertRaisesRegex(RuntimeError,'pyinstaller has no nonempty'):self.collect()
    def test_required_hooks_notice_fails_closed(self):
        self.libraries();self.dists['pyinstaller-hooks-contrib'].files=[]
        with self.assertRaisesRegex(RuntimeError,'hooks-contrib has no nonempty'):self.collect()
    def test_official_tcl_8615_fallback(self):
        (self.tk/'license.terms').write_text('Tk permission notice')
        copied=self.collect(tcl_patchlevel='8.6.15')
        self.assertIn('Tcl-8.6.15-LICENSE.txt',copied)
        self.assertIn('Tcl-8.6.15-PROVENANCE.txt',copied)
        expected=(Path(pylicenses.__file__).parent/'notices/Tcl-8.6.15-LICENSE.txt').read_bytes()
        self.assertEqual((self.root/'out/Tcl-8.6.15-LICENSE.txt').read_bytes(),expected)
    def test_pinned_tcl_notice_not_used_for_other_version(self):
        (self.tk/'license.terms').write_text('Tk permission notice')
        with self.assertRaisesRegex(RuntimeError,'Missing required tcl'):
            self.collect(tcl_patchlevel='8.6.14')
    def test_pinned_tcl_notice_tampering_fails(self):
        (self.tk/'license.terms').write_text('Tk permission notice')
        notices=self.root/'notices';notices.mkdir()
        (notices/'Tcl-8.6.15-LICENSE.txt').write_text('Altered terms')
        with self.assertRaisesRegex(RuntimeError,'hash mismatch'):
            self.collect(tcl_patchlevel='8.6.15',notice_dir=notices)
    def test_missing_pinned_tcl_notice_fails(self):
        (self.tk/'license.terms').write_text('Tk permission notice')
        notices=self.root/'notices';notices.mkdir()
        with self.assertRaisesRegex(RuntimeError,'Missing pinned official Tcl'):
            self.collect(tcl_patchlevel='8.6.15',notice_dir=notices)
    def test_tcl_fallback_does_not_waive_tk(self):
        with self.assertRaisesRegex(RuntimeError,'Missing required tk'):
            self.collect(tcl_patchlevel='8.6.15')

    def crate(self, explicit=None):
        folder=self.root/'crate';folder.mkdir()
        (folder/'Cargo.toml').write_text('[package]\nname="synthetic"\n')
        return folder,{'name':'synthetic','version':'1.0','manifest_path':str(folder/'Cargo.toml'),'license_file':explicit}
    def test_cargo_explicit_relative_notice(self):
        folder,package=self.crate('legal/terms.txt')
        (folder/'legal').mkdir();(folder/'legal/terms.txt').write_text('Synthetic notice')
        _,notices=rustlicenses.package_notices(package)
        self.assertEqual(notices,[(folder/'legal/terms.txt').resolve()])
    def test_cargo_explicit_absolute_inside_package(self):
        folder,package=self.crate()
        path=folder/'terms.txt';path.write_text('Synthetic notice');package['license_file']=str(path)
        self.assertEqual(rustlicenses.package_notices(package)[1],[path.resolve()])
    def test_cargo_explicit_parent_escape_rejected(self):
        folder,package=self.crate('../outside.txt');(self.root/'outside.txt').write_text('Not a crate notice')
        with self.assertRaisesRegex(RuntimeError,'out-of-package'):rustlicenses.package_notices(package)
    def test_cargo_missing_explicit_notice_rejected(self):
        _,package=self.crate('missing.txt')
        with self.assertRaisesRegex(RuntimeError,'Missing'):rustlicenses.package_notices(package)
    def test_cargo_empty_notice_rejected(self):
        folder,package=self.crate('terms.txt');(folder/'terms.txt').write_text('')
        with self.assertRaisesRegex(RuntimeError,'Empty'):rustlicenses.package_notices(package)
    def test_cargo_notice_symlink_escape_rejected(self):
        folder,package=self.crate('terms.txt');outside=self.root/'outside';outside.write_text('Outside')
        try:(folder/'terms.txt').symlink_to(outside)
        except (OSError,NotImplementedError):self.skipTest('symlinks unavailable')
        with self.assertRaisesRegex(RuntimeError,'out-of-package'):rustlicenses.package_notices(package)


if __name__=='__main__':unittest.main(verbosity=2)
