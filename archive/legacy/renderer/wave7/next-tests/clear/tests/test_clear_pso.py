"""Extract actual cache class/methods; compile offline regular/sanitized fixtures.

No production binary, Wine, Objective-C/Metal device or GPU is loaded. The
baseline and semantic mutations must compile and fail behavioral assertions.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
GENERATION = HERE.parent
CANDIDATE = HERE.parents[2] / 'source/src/dxmt'
BASELINE = HERE.parent / 'baseline'
BASE_HASHES = {'dxmt_command.cpp': 'ac57a91b378d2aff5e256fef68429afc95f4ad0a85c307a1399e250882ba8585',
               'dxmt_command.hpp': 'a72450116910ba4cf00238ef8f2d97092a549f7a6a3d73d9727847abd539531e'}
REPORT = {'scope': 'offline extracted production class/methods with mock WMT/device/pass/commands; no GPU/readback',
          'builds': {}, 'runs': {}}


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def extract(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        if source[cursor] == '{':depth += 1
        elif source[cursor] == '}':depth -= 1
        cursor += 1
    return source[start:cursor]


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise AssertionError('mutation anchor drift: ' + old)
    return text.replace(old, new, 1)


def fixture(root, mutation=None):
    cpp = (root / 'dxmt_command.cpp').read_text()
    hpp = (root / 'dxmt_command.hpp').read_text()
    if mutation == 'format_only':
        cpp = replace_once(cpp, 'const PipelineKey key{format, texture->sampleCount()};',
                           'const PipelineKey key{format, 1};')
        # Keep the descriptor honest so the mutation represents a cache-key
        # omission, rather than an independently invalid creation descriptor.
        cpp = replace_once(cpp, 'pipeline_info.raster_sample_count = key.sample_count;',
                           'pipeline_info.raster_sample_count = texture->sampleCount();')
    if mutation == 'null_insert':
        cpp = replace_once(cpp, 'err.description().getUTF8String());\n      return;\n    }\n    pso_cache_.emplace',
                           'err.description().getUTF8String());\n    }\n    pso_cache_.emplace')
    declaration = extract(hpp, 'class ClearRenderTargetContext {') + ';'
    functions = [extract(cpp, 'ClearRenderTargetContext::ClearRenderTargetContext(')]
    for name in ('begin', 'clear', 'end'):
        functions.append('void\n' + extract(cpp, 'ClearRenderTargetContext::' + name + '('))
    return ('#include "' + str(HERE/'mock_runtime.hpp') + '"\nnamespace dxmt {\n' + declaration + '\n' +
            '\n'.join(functions) + '\n}\n' + (HERE/'behavior.cpp').read_text())


class ClearPSOTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for name, want in BASE_HASHES.items():
            if digest((BASELINE/name).read_bytes()) != want:
                raise AssertionError('exact reviewed public baseline changed: ' + name)
        cls.temp = tempfile.TemporaryDirectory(prefix='wave7-clear-pso-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.executables = {}
        compiler = '/usr/bin/clang++'
        REPORT['compiler'] = subprocess.run([compiler, '--version'], text=True, capture_output=True, timeout=10, check=True).stdout
        for name, root, mutation, sanitized in [
            ('candidate', CANDIDATE, None, False),
            ('sanitized', CANDIDATE, None, True),
            ('baseline', BASELINE, None, False),
            ('format_only', CANDIDATE, 'format_only', False),
            ('null_insert', CANDIDATE, 'null_insert', False),
        ]:
            source = Path(cls.temp.name)/(name+'.cpp')
            text = fixture(root, mutation)
            source.write_text(text)
            binary = source.with_suffix('')
            args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-g', '-O1' if sanitized else '-O2', str(source), '-o', str(binary)]
            if sanitized:args += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            result = subprocess.run(args, text=True, capture_output=True, timeout=45)
            REPORT['builds'][name] = {'fixtureSha256': digest(text.encode()), 'arguments': args,
                                     'exitCode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
            if result.returncode:
                raise AssertionError(name + ' did not compile; compilation failure is not a killed mutation:\n' + result.stderr)
            REPORT['builds'][name]['binarySha256'] = digest(binary.read_bytes())
            cls.executables[name] = binary

    def run_case(self, variant, case, expected=0, reason=None):
        environment = dict(os.environ)
        environment.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.run([str(self.executables[variant]), case], text=True, capture_output=True,
                                timeout=10, env=environment)
        REPORT['runs'][variant + ':' + case] = {'exitCode': result.returncode,
                                               'stdout': result.stdout, 'stderr': result.stderr}
        self.assertEqual(result.returncode, expected, result.stderr)
        if expected == 0:
            self.assertEqual(result.stdout.strip(), 'PASS ' + case)
            self.assertEqual(result.stderr, '', 'successful fixtures must be sanitizer/warning clean at runtime')
        elif reason:
            self.assertIn(reason, result.stderr)

    def test_candidate_actual_class_and_methods(self):
        for case in ('matrix', 'retry', 'isolation', 'depth'):
            with self.subTest(case=case):self.run_case('candidate', case)

    def test_candidate_address_and_undefined_sanitizers(self):
        for case in ('matrix', 'retry', 'isolation', 'depth'):
            with self.subTest(case=case):self.run_case('sanitized', case)

    def test_exact_original_rejects_mixed_sample_count(self):
        self.run_case('baseline', 'matrix', 2, 'selected PSO sample count mismatches attachment')

    def test_exact_original_rejects_null_retry(self):
        self.run_case('baseline', 'retry', 2, 'successful begin must create a pass')

    def test_format_only_mutation_rejected(self):
        self.run_case('format_only', 'matrix', 2, 'selected PSO sample count mismatches attachment')

    def test_failed_null_insertion_mutation_rejected(self):
        self.run_case('null_insert', 'retry', 2, 'successful begin must create a pass')


if __name__ == '__main__':
    result = unittest.main(verbosity=2, exit=False).result
    REPORT['passed'] = result.wasSuccessful()
    REPORT['pythonTests'] = result.testsRun
    REPORT['sourceHashes'] = {name: digest((CANDIDATE/name).read_bytes()) for name in BASE_HASHES}
    REPORT['testHashes'] = {name: digest((HERE/name).read_bytes()) for name in
                            ('test_clear_pso.py', 'mock_runtime.hpp', 'behavior.cpp')}
    (HERE/'results.json').write_text(json.dumps(REPORT, indent=2)+'\n')
    sys.exit(not result.wasSuccessful())
