#!/usr/bin/env python3
"""Exact producer-header regression using the existing portable mapping seam."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

COMPONENT = Path(__file__).resolve().parents[2]
HUB = Path(os.environ.get('DXMT_TRACE_TEST_HUB', COMPONENT.parents[1]))
spec = importlib.util.spec_from_file_location('existing_detail_fixture', HUB / 'tools/telemetry/test_detail_producer.py')
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)


class TraceProfileProducerTests(unittest.TestCase):
    def test_actual_producer_filter_identity_budget_boundary_and_defaults(self):
        with tempfile.TemporaryDirectory(prefix='dxmt-trace-profile-') as directory:
            work = Path(directory)
            (work / 'windows.h').write_text(existing.WINDOWS)
            (work / 'util_env.hpp').write_text(existing.ENV)
            binary = work / 'fixture'
            subprocess.run(['xcrun', 'clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-pthread', '-fsanitize=address,undefined', '-I' + directory,
                            '-I' + str(COMPONENT / 'src/d3d11'),
                            str(Path(__file__).with_name('trace_profile_fixture.cpp')), '-o', str(binary)],
                           check=True, timeout=60, capture_output=True, text=True)
            for profile in ['', 'compile-waits', 'presentation-waits', 'invalid', 'full', 'Compile-Waits', ' compile-waits', 'compile-waits ']:
                with self.subTest(profile=profile):
                    env = dict(os.environ, DXMT_CS2_TRACE_DIR='fixture-only',
                               DXMT_CS2_TRACE_DETAIL_LIMIT='3', DXMT_CS2_TRACE_PROFILE=profile,
                               UBSAN_OPTIONS='halt_on_error=1')
                    mode = 'full' if not profile else 'compile-waits' if profile == 'compile-waits' else 'presentation-waits' if profile == 'presentation-waits' else 'invalid'
                    result = subprocess.run([str(binary), mode], env=env, capture_output=True, text=True, timeout=15)
                    self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''), result.stderr)


if __name__ == '__main__':
    unittest.main()
