"""Pinned nod notices are checked locally; no dependency downloads."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect_rust_licenses as licenses


class NodLicenseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.crate = self.root/'crate'
        self.crate.mkdir()
        (self.crate/'Cargo.toml').write_text('[package]\nname="nod"\n')
        self.bundled = self.root/'bundled'
        self.bundled.mkdir()
        originals = Path(licenses.__file__).resolve().parent/'licenses'
        for name in licenses.NOD_144_NOTICES:
            (self.bundled/name).write_bytes((originals/name).read_bytes())
        self.package = {
            'name': 'nod', 'version': '1.4.4',
            'source': 'registry+https://github.com/rust-lang/crates.io-index',
            'license': 'MIT OR Apache-2.0',
            'manifest_path': str(self.crate/'Cargo.toml'),
            'license_file': None,
        }

    def tearDown(self):
        self.temp.cleanup()

    def find(self):
        return licenses.package_notices(self.package, bundled_dir=self.bundled)

    def test_verified_nod_notices_used(self):
        folder, notices = self.find()
        self.assertEqual(folder, self.bundled.resolve())
        self.assertEqual({p.name for p in notices}, set(licenses.NOD_144_NOTICES))

    def test_tampered_mit_rejected(self):
        (self.bundled/'nod-LICENSE-MIT.txt').write_text('Altered notice')
        with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
            self.find()

    def test_tampered_apache_rejected(self):
        (self.bundled/'nod-LICENSE-APACHE.txt').write_text('Altered notice')
        with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
            self.find()

    def test_missing_pinned_notice_rejected(self):
        (self.bundled/'nod-LICENSE-MIT.txt').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing'):
            self.find()

    def test_other_packages_versions_sources_and_licenses_stay_strict(self):
        changes = [('name', 'other'), ('version', '1.4.5'),
                   ('source', 'registry+https://example.invalid/index'),
                   ('license', 'Unknown')]
        for key, value in changes:
            original = self.package[key]
            with self.subTest(key=key):
                self.package[key] = value
                try:
                    with self.assertRaisesRegex(RuntimeError, 'No redistribution notice'):
                        self.find()
                finally:
                    self.package[key] = original

    def test_explicit_missing_license_is_not_masked(self):
        self.package['license_file'] = 'missing.txt'
        with self.assertRaisesRegex(RuntimeError, 'Missing or out-of-package license'):
            self.find()

    def test_existing_crate_notice_takes_precedence(self):
        notice = self.crate/'LICENSE'
        notice.write_text('Existing crate notice')
        folder, notices = self.find()
        self.assertEqual(folder, self.crate.resolve())
        self.assertEqual(notices, [notice.resolve()])

    def test_pinned_symlink_escape_rejected(self):
        path = self.bundled/'nod-LICENSE-MIT.txt'
        outside = self.root/'outside.txt'
        outside.write_bytes(path.read_bytes())
        path.unlink()
        try:
            path.symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest('symlinks unavailable')
        with self.assertRaisesRegex(RuntimeError, 'out-of-package'):
            self.find()


if __name__ == '__main__':
    unittest.main(verbosity=2)
