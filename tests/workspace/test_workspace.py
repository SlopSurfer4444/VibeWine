# SPDX-License-Identifier: LGPL-2.1-or-later
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('workspace', ROOT / 'scripts/workspace.py')
workspace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workspace)


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        git(self.root, 'init', '-q')
        git(self.root, 'config', 'user.name', 'Fixture')
        git(self.root, 'config', 'user.email', 'fixture@example.invalid')
        (self.root / '.gitmodules').write_text((ROOT / '.gitmodules').read_text())
        git(self.root, 'add', '.gitmodules')
        git(self.root, 'commit', '-qm', 'initial fixture')
        self.pin = git(self.root, 'rev-parse', 'HEAD')
        for name in workspace.COMPONENTS:
            git(self.root, 'update-index', '--add', '--cacheinfo',
                f'160000,{self.pin},components/{name}')
        git(self.root, 'commit', '-qm', 'pin fixture')

    def populate(self):
        for name in workspace.COMPONENTS:
            path = self.root / 'components' / name
            path.mkdir(parents=True)
            git(path, 'init', '-q')
            git(path, 'fetch', '-q', str(self.root), self.pin)
            git(path, 'checkout', '-q', '--detach', self.pin)

    def test_clean_exact_checkouts(self):
        self.populate()
        self.assertEqual(len(workspace.check(self.root)), 2)

    def test_dirty_checkout_rejected(self):
        self.populate()
        (self.root / 'components/wine/local.txt').write_text('local changes')
        with self.assertRaisesRegex(ValueError, 'local changes'):
            workspace.check(self.root)

    def test_dirty_init_never_runs_update(self):
        self.populate()
        (self.root / 'components/dxmt/local.txt').write_text('local changes')
        with patch.object(workspace.subprocess, 'run', wraps=subprocess.run) as run:
            with self.assertRaisesRegex(ValueError, 'local changes'):
                workspace.initialize(self.root)
            self.assertFalse(any('submodule' in call.args[0] for call in run.call_args_list))

    def test_wrong_commit_rejected(self):
        self.populate()
        path = self.root / 'components/dxmt'
        git(path, 'fetch', '-q', str(self.root), 'HEAD')
        git(path, 'checkout', '-q', '--detach', 'FETCH_HEAD')
        with self.assertRaisesRegex(ValueError, 'differs from pinned'):
            workspace.check(self.root)

    def nested_fixture(self, populate=True):
        self.populate()
        parent = self.root / 'components/dxmt'
        git(parent, 'config', 'user.name', 'Fixture')
        git(parent, 'config', 'user.email', 'fixture@example.invalid')
        (parent / '.gitmodules').write_text(
            '[submodule "dependency"]\n path = dependency\n url = https://example.invalid/source\n')
        git(parent, 'add', '.gitmodules')
        git(parent, 'update-index', '--add', '--cacheinfo', f'160000,{self.pin},dependency')
        git(parent, 'commit', '-qm', 'nested fixture')
        nested_pin = git(parent, 'rev-parse', 'HEAD')
        git(self.root, 'update-index', '--cacheinfo', f'160000,{nested_pin},components/dxmt')
        git(self.root, 'commit', '-qm', 'pin nested fixture')
        child = parent / 'dependency'
        child.mkdir()
        if populate:
            git(child, 'init', '-q')
            git(child, 'fetch', '-q', str(self.root), self.pin)
            git(child, 'checkout', '-q', '--detach', self.pin)
        return parent, child

    def test_nested_missing_rejected(self):
        self.nested_fixture(False)
        with self.assertRaisesRegex(ValueError, 'not initialized'):
            workspace.check(self.root)

    def test_nested_exact_passes(self):
        self.nested_fixture()
        workspace.check(self.root)

    def test_nested_dirty_blocks_init_even_when_ignored(self):
        parent, child = self.nested_fixture()
        git(parent, 'config', 'submodule.dependency.ignore', 'all')
        (child / 'local.txt').write_text('preserve me')
        with patch.object(workspace.subprocess, 'run', wraps=subprocess.run) as run:
            with self.assertRaisesRegex(ValueError, 'local changes'):
                workspace.initialize(self.root)
            self.assertFalse(any('update' in call.args[0] for call in run.call_args_list))
        self.assertEqual((child / 'local.txt').read_text(), 'preserve me')

    def test_nested_wrong_pin_rejected_even_when_ignored(self):
        parent, child = self.nested_fixture()
        git(parent, 'config', 'submodule.dependency.ignore', 'all')
        git(child, 'fetch', '-q', str(parent), 'HEAD')
        git(child, 'checkout', '-q', '--detach', 'FETCH_HEAD')
        with self.assertRaisesRegex(ValueError, 'local changes|differs from pinned'):
            workspace.check(self.root)

    def test_metadata_without_network(self):
        self.assertEqual(len(workspace.check(self.root, True)), 2)

    def test_missing_checkout_rejected(self):
        with self.assertRaisesRegex(ValueError, 'not initialized'):
            workspace.check(self.root)

    def test_redirected_url_rejected(self):
        p = self.root / '.gitmodules'
        p.write_text(p.read_text().replace(workspace.URL, 'https://example.invalid/source'))
        with self.assertRaisesRegex(ValueError, 'configuration'):
            workspace.metadata(self.root)

    def test_staged_pin_mismatch_rejected_without_checkout_mutation(self):
        self.populate()
        changed = git(self.root, 'rev-parse', 'HEAD')
        git(self.root, 'update-index', '--cacheinfo', f'160000,{changed},components/wine')
        before = git(self.root / 'components/wine', 'rev-parse', 'HEAD')
        with patch.object(workspace.subprocess, 'run', wraps=subprocess.run) as run:
            with self.assertRaisesRegex(ValueError, 'index pin differs'):
                workspace.initialize(self.root)
            self.assertFalse(any('submodule' in call.args[0] for call in run.call_args_list))
        self.assertEqual(git(self.root / 'components/wine', 'rev-parse', 'HEAD'), before)

    def test_missing_pin_rejected(self):
        git(self.root, 'update-index', '--force-remove', 'components/wine')
        git(self.root, 'commit', '-qm', 'remove fixture')
        with self.assertRaisesRegex(ValueError, 'gitlink'):
            workspace.metadata(self.root)


if __name__ == '__main__':
    unittest.main()
