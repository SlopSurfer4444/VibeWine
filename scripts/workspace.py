#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fetch or verify pinned source only; never execute component code."""
import argparse
import configparser
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ('wine', 'dxmt')
URL = 'https://github.com/SlopSurfer4444/VibeWine.git'


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def metadata(root):
    config = configparser.ConfigParser()
    config.read(root / '.gitmodules')
    expected = {f'submodule "components/{name}"' for name in COMPONENTS}
    if set(config.sections()) != expected:
        raise ValueError('Unexpected or missing submodule configuration')
    pins = {}
    for name in COMPONENTS:
        path = f'components/{name}'
        entry = config[f'submodule "{path}"']
        if dict(entry) != {'path': path, 'url': URL, 'branch': f'component/{name}'}:
            raise ValueError(f'Unexpected configuration for {path}')
        # Check the committed tree, not an unreviewed staging-area replacement.
        row = git(root, 'ls-tree', 'HEAD', '--', path).split()
        if len(row) != 4 or row[0:2] != ['160000', 'commit'] or row[3] != path:
            raise ValueError(f'{path} has no committed gitlink')
        staged = git(root, 'ls-files', '--stage', '--', path).split()
        if staged != ['160000', row[2], '0', path]:
            raise ValueError(f'{path} index pin differs from HEAD; commit or unstage the pin first')
        pins[path] = row[2]
    return pins


def check(root, metadata_only=False):
    pins = metadata(root)
    if not metadata_only:
        for path, pin in pins.items():
            component = root / path
            if not (component / '.git').exists():
                raise ValueError(f'{path} not initialized; run workspace.py init')
            if git(component, 'rev-parse', 'HEAD') != pin:
                raise ValueError(f'{path} differs from pinned {pin}')
            if git(component, 'status', '--porcelain', '--untracked-files=normal'):
                raise ValueError(f'{path} contains local changes; preserve or commit them first')
    return pins


def initialize(root):
    pins = metadata(root)
    # Inspect all existing checkouts before updating any of them.
    for path in pins:
        component = root / path
        if (component / '.git').exists() and git(
                component, 'status', '--porcelain', '--untracked-files=normal'):
            raise ValueError(f'{path} contains local changes; init will not modify any component')
    subprocess.run(['git', '-C', str(root), 'submodule', 'update', '--init',
                    '--', *pins], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('init', 'check'))
    parser.add_argument('--metadata-only', action='store_true')
    args = parser.parse_args()
    try:
        if args.command == 'init':
            if args.metadata_only:
                parser.error('--metadata-only applies only to check')
            initialize(ROOT)
        pins = check(ROOT, args.metadata_only)
        for path, pin in pins.items():
            print(f'{path}: {pin}')
        print('Source metadata verified.' if args.metadata_only else 'Pinned source checkouts verified.')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Workspace check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
