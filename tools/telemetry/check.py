#!/usr/bin/env python3
"""Build and exercise the collector using offline synthetic records on macOS."""
from pathlib import Path
import platform
import subprocess
import sys
import unittest


def main():
    if platform.system() != 'Darwin':
        raise SystemExit('Native collector checks require macOS; no game is launched.')
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    protocol = root / 'components/dxmt/src/d3d11/cs2_trace_protocol.hpp'
    if not protocol.is_file():
        raise SystemExit('Initialize the pinned components/dxmt submodule first.')
    build = root / 'build/telemetry'
    build.mkdir(parents=True, exist_ok=True)
    binary = build / 'cs2trace'
    subprocess.run(['xcrun', 'clang++', '-std=c++20', '-O2', '-Wall', '-Wextra',
                    '-Werror', str(here / 'trace_controller.cpp'), '-o', str(binary)], check=True)
    import tests_cli
    tests_cli.CONTROLLER = binary
    suite = unittest.defaultTestLoader.discover(str(here), pattern='test*.py')
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
