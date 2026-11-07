"""Synthetic Cargo graphs and notices only; never runs Cargo or downloads."""
from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect_rust_licenses as licenses


HOST = 'x86_64-pc-windows-msvc'


def graph():
    names = ('helper', 'active', 'shared', 'r-efi')
    return {
        'packages': [{'id': name, 'name': name, 'version': '1.0',
                      'source': None if name == 'helper' else 'registry+synthetic'}
                     for name in names],
        'resolve': {'root': 'helper', 'nodes': [
            {'id': 'helper', 'deps': [{'pkg': 'active'}, {'pkg': 'shared'}]},
            {'id': 'active', 'deps': [{'pkg': 'shared'}]},
            {'id': 'shared', 'deps': []},
            {'id': 'r-efi', 'deps': []},
        ]},
    }


class RustGraphTests(unittest.TestCase):
    def test_unfiltered_packages_and_disconnected_nodes_are_excluded(self):
        selected = licenses.reachable_packages(graph())
        self.assertEqual([p['id'] for p in selected], ['active', 'helper', 'shared'])

    def test_shared_and_cyclic_dependencies_are_visited_once(self):
        metadata = graph()
        metadata['resolve']['nodes'][2]['deps'] = [{'pkg': 'active'}]
        selected = licenses.reachable_packages(metadata)
        self.assertEqual([p['id'] for p in selected], ['active', 'helper', 'shared'])

    def test_local_path_dependency_is_not_silently_excluded(self):
        metadata = graph()
        metadata['packages'][1]['source'] = None
        self.assertIn('active', [p['id'] for p in licenses.reachable_packages(metadata)])

    def test_missing_root_or_graph_lists_fail(self):
        for field in ('resolve', 'root', 'nodes', 'packages'):
            with self.subTest(field=field):
                metadata = graph()
                if field in ('resolve', 'packages'):
                    metadata.pop(field)
                else:
                    metadata['resolve'].pop(field)
                with self.assertRaises(RuntimeError):
                    licenses.reachable_packages(metadata)

    def test_missing_reachable_package_or_node_fails(self):
        for identifier in ('helper', 'active', 'shared'):
            for field in ('packages', 'nodes'):
                with self.subTest(identifier=identifier, field=field):
                    metadata = graph()
                    records = metadata['packages'] if field == 'packages' else metadata['resolve']['nodes']
                    records[:] = [p for p in records if p['id'] != identifier]
                    with self.assertRaisesRegex(RuntimeError, 'Incomplete Cargo resolve graph'):
                        licenses.reachable_packages(metadata)

    def test_invalid_reachable_dependencies_fail(self):
        for deps in (None, {}, [{}], [{'pkg': ''}], [{'pkg': 'missing'}], ['active']):
            with self.subTest(deps=deps):
                metadata = graph()
                metadata['resolve']['nodes'][0]['deps'] = deps
                with self.assertRaisesRegex(RuntimeError, 'Incomplete Cargo resolve graph'):
                    licenses.reachable_packages(metadata)

    def test_duplicate_package_or_node_ids_fail(self):
        for field in ('packages', 'nodes'):
            with self.subTest(field=field):
                metadata = graph()
                records = metadata['packages'] if field == 'packages' else metadata['resolve']['nodes']
                records.append(copy.deepcopy(records[0]))
                with self.assertRaisesRegex(RuntimeError, 'duplicate'):
                    licenses.reachable_packages(metadata)

    def test_rust_host_is_read_from_compiler(self):
        with patch.object(licenses.subprocess, 'check_output', return_value=f'rustc 1.90.0\nhost: {HOST}\n') as run:
            self.assertEqual(licenses.rust_host(), HOST)
        self.assertEqual(run.call_args.args[0], ['rustc', '-vV'])

    def test_missing_ambiguous_or_invalid_rust_host_fails(self):
        for text in ('rustc 1.90.0\n', 'host: \n', 'host: host\n',
                     f'host: {HOST}\nhost: {HOST}\n', 'host: ../x-y\n'):
            with self.subTest(text=text):
                with patch.object(licenses.subprocess, 'check_output', return_value=text):
                    with self.assertRaisesRegex(RuntimeError, 'rustc host'):
                        licenses.rust_host()


class RustCollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.metadata = graph()
        for package in self.metadata['packages']:
            folder = self.root/package['name']
            folder.mkdir()
            package.update(manifest_path=str(folder/'Cargo.toml'),
                           license_file=None, license='MIT', repository=None)
            (folder/'Cargo.toml').write_text('[package]\n')
            if package['name'] != 'r-efi':
                (folder/'LICENSE').write_text('Synthetic notice for '+package['name'])
        self.commands = []

    def tearDown(self):
        self.temp.cleanup()

    def command(self, args, **kwargs):
        self.commands.append(args)
        if args == ['rustc', '-vV']:
            return f'rustc 1.90.0\nhost: {HOST}\n'
        self.assertEqual(args[:2], ['cargo', 'metadata'])
        return json.dumps(self.metadata)

    def collect(self):
        with patch.object(licenses.subprocess, 'check_output', side_effect=self.command):
            with redirect_stdout(io.StringIO()):
                return licenses.collect(self.root)

    def test_collect_filters_for_actual_host_and_ignores_off_target_notice(self):
        output = self.collect().read_text(encoding='utf-8')
        cargo = self.commands[1]
        self.assertEqual(cargo[cargo.index('--filter-platform')+1], HOST)
        self.assertIn('--locked', cargo)
        self.assertIn('active 1.0', output)
        self.assertIn('shared 1.0', output)
        self.assertNotIn('r-efi', output)
        self.assertNotIn('helper 1.0', output)
        self.assertIn(HOST, output)

    def test_all_reachable_notice_failures_are_aggregated_without_overwrite(self):
        (self.root/'active/LICENSE').unlink()
        (self.root/'shared/LICENSE').write_text('')
        output = self.root/'licenses/Rust-dependency-notices.txt'
        output.parent.mkdir()
        output.write_text('previous complete notices')
        with self.assertRaises(RuntimeError) as raised:
            self.collect()
        message = str(raised.exception)
        self.assertIn('active 1.0: No redistribution notice', message)
        self.assertIn('shared 1.0: Empty or invalid', message)
        self.assertNotIn('r-efi', message)
        self.assertEqual(output.read_text(), 'previous complete notices')

    def test_reachable_local_path_dependency_requires_notice(self):
        self.metadata['packages'][1]['source'] = None
        (self.root/'active/LICENSE').unlink()
        with self.assertRaisesRegex(RuntimeError, 'active 1.0: No redistribution notice'):
            self.collect()

    def test_incomplete_graph_is_rejected_before_reading_notices(self):
        self.metadata['resolve']['nodes'].pop(1)
        with patch.object(licenses, 'package_notices') as find:
            with self.assertRaisesRegex(RuntimeError, 'Incomplete Cargo resolve graph'):
                self.collect()
            find.assert_not_called()


if __name__ == '__main__':
    unittest.main(verbosity=2)
