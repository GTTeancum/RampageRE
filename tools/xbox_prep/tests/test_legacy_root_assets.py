"""Regression checks for legacy root VOL taking precedence over data/."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import asset_engine as engine


class LegacyRootTests(unittest.TestCase):
    def make_package(self, root):
        package = root/'release'
        target = package/engine.TARGET_NAME
        target.mkdir(parents=True)
        (target/'default.xbe').write_bytes(b'XBEHsynthetic')
        (target/'profile.dat').write_bytes(b'preserve save')
        return package, target

    def snapshot(self, target):
        return {p.relative_to(target).as_posix(): p.read_bytes() if p.is_file() else None
                for p in target.rglob('*')}

    def test_root_vol_refused_before_planning_or_copy_without_changes(self):
        for name in ('RAMPAGE.VOL', 'rampage.vol', 'RaMpAgE.VoL'):
            for replace in (False, True):
                for prepared in (False, True):
                    with self.subTest(name=name, replace=replace, prepared=prepared):
                        with tempfile.TemporaryDirectory() as temp:
                            root = Path(temp)
                            package, target = self.make_package(root)
                            (target/name).write_bytes(b'old root volume')
                            if prepared:
                                (target/'data').mkdir()
                                (target/'data/RAMPAGE.VOL').write_bytes(b'old data volume')
                            before = self.snapshot(target)
                            with patch.object(engine, 'ps2_plan') as plan:
                                with patch.object(engine, 'copy_entry') as copy:
                                    with patch.object(engine.os, 'replace') as commit:
                                        with self.assertRaisesRegex(engine.PrepError, 'Legacy root RAMPAGE.VOL') as raised:
                                            engine.prepare(package, root/'unused.iso', allow_replace=replace)
                                        plan.assert_not_called()
                                        copy.assert_not_called()
                                        commit.assert_not_called()
                            self.assertIn('Extract the whole ZIP into a new folder', str(raised.exception))
                            self.assertIn('move/back up the old root assets', str(raised.exception))
                            self.assertEqual(self.snapshot(target), before)
                            self.assertEqual(engine.pending_recovery(package), [])

    def prepare_synthetic(self, root, package, **kwargs):
        source = root/'source.bin'
        source.write_bytes(b'synthetic disc bytes')
        entries = [engine.Entry('data/'+name, source, 0, source.stat().st_size)
                   for name in ('RAMPAGE.VOL', 'RAMPAGE.ELF')]
        with patch.object(engine, 'ps2_plan', return_value=entries):
            return engine.prepare(package, source, **kwargs)

    def test_fresh_folder_preparation_is_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            package, target = self.make_package(root)
            self.prepare_synthetic(root, package)
            self.assertEqual((target/'data/RAMPAGE.VOL').read_bytes(), b'synthetic disc bytes')
            self.assertEqual((target/'data/RAMPAGE.ELF').read_bytes(), b'synthetic disc bytes')
            self.assertEqual((target/'default.xbe').read_bytes(), b'XBEHsynthetic')
            self.assertEqual((target/'profile.dat').read_bytes(), b'preserve save')
            self.assertFalse((target/'RAMPAGE.VOL').exists())
            self.assertEqual(engine.pending_recovery(package), [])

    def test_data_replacement_and_non_vol_root_files_remain_allowed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            package, target = self.make_package(root)
            (target/'data').mkdir()
            (target/'data/RAMPAGE.VOL').write_bytes(b'old prepared volume')
            for name in ('RAMPAGE.ELF', 'SYSTEM.CNF'):
                (target/name).write_bytes(b'preserve unrelated root file')
            self.prepare_synthetic(root, package, allow_replace=True)
            self.assertEqual((target/'data/RAMPAGE.VOL').read_bytes(), b'synthetic disc bytes')
            for name in ('RAMPAGE.ELF', 'SYSTEM.CNF'):
                self.assertEqual((target/name).read_bytes(), b'preserve unrelated root file')
            self.assertEqual(engine.pending_recovery(package), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
