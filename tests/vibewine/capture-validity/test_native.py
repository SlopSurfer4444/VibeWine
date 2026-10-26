#!/usr/bin/env python3
"""CPU-only exact-production capture bridge and allocation getter fixture."""
from pathlib import Path
import os
import platform
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]


def function(source: str, name: str) -> str:
    """Extract a complete top-level function, including its production signature."""
    pattern = rf'^(?:static NTSTATUS|WINEMETAL_API (?:bool|void|uint64_t))\n{re.escape(name)}\([^\n]*\) \{{.*?^\}}'
    matches = re.findall(pattern, source, flags=re.MULTILINE | re.DOTALL)
    assert len(matches) == 1, f'expected one complete production function: {name}'
    return matches[0]


def main() -> None:
    native = (root / 'src/winemetal/unix/winemetal_unix.c').read_text()
    thunks = (root / 'src/winemetal/winemetal_thunks.c').read_text()
    native_names = ['_MTLCaptureManager_startCapture', '_MTLCaptureManager_stopCapture',
                    '_MTLAllocation_allocatedSize']
    thunk_names = ['MTLCaptureManager_startCapture', 'MTLCaptureManager_stopCapture',
                   'MTLAllocation_allocatedSize']
    for table in ['__wine_unix_call_funcs', '__wine_unix_call_wow64_funcs']:
        body = native.split(table + '[] = {', 1)[1].split('};', 1)[0]
        entries = [entry.strip() for entry in body.split(',') if entry.strip()]
        assert entries[54:56] == ['&_MTLCaptureManager_startCapture', '&_MTLCaptureManager_stopCapture']
        assert entries[149] == '&_MTLAllocation_allocatedSize'
        assert entries[150] == '&_WMTMachPortDeallocate'
        assert len(entries) == 151, f'{table}: Mach cleanup must be appended at 150'

    template = (here / 'native_fixture.m.in').read_text()
    source_text = template.replace('// EXACT_NATIVE_FUNCTIONS', '\n\n'.join(
        function(native, name) for name in native_names)).replace('// EXACT_PE_THUNKS', '\n\n'.join(
        function(thunks, name) for name in thunk_names))
    assert '// EXACT_' not in source_text
    with tempfile.TemporaryDirectory(prefix='dxmt-capture-native-') as directory:
        work = Path(directory)
        source = work / 'fixture.m'
        source.write_text(source_text)
        common = [os.environ.get('CC', 'clang'), '-std=c2x', '-fno-objc-arc',
                  '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                  '-I', str(root / 'src/winemetal')]
        # Verify both supported macOS ABI targets, without requiring Rosetta.
        for arch in ['arm64', 'x86_64']:
            subprocess.run(common + ['-arch', arch, '-c', str(source),
                                     '-o', str(work / (arch + '.o'))], check=True, timeout=30)
        sanitizer_flags = ['-arch', platform.machine(), '-fsanitize=address,undefined',
                           '-framework', 'Foundation']
        environment = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1',
                       'ASAN_OPTIONS': 'halt_on_error=1'}

        def compile_run(name: str, text: str, mutant: bool = False) -> None:
            candidate = work / (name + '.m')
            binary = work / name
            candidate.write_text(text)
            subprocess.run(common + sanitizer_flags + [str(candidate), '-o', str(binary)],
                           check=True, timeout=30)
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    timeout=15, env=environment)
            if mutant:
                assert result.returncode == 1, (name, result.returncode, result.stderr)
                assert 'CHECK failed at ' in result.stderr, (name, result.stderr)
                for diagnostic in ['Sanitizer', 'runtime error:']:
                    assert diagnostic not in result.stderr, (name, result.stderr)
            else:
                assert result.returncode == 0, (result.stdout, result.stderr)
                print(result.stdout.strip())

        compile_run('fixture', source_text)

        def changed(old: str, new: str) -> str:
            assert source_text.count(old) == 1, old
            return source_text.replace(old, new)

        # A known true return slot models stale/poisoned stack memory without
        # introducing undefined reads. The fake dispatcher checks initialization.
        mutations = {
            'capture-ret-poison': changed(
                'struct unixcall_mtlcapturemanager_startcapture params = {0};',
                'struct unixcall_mtlcapturemanager_startcapture params = {.ret = true};'),
            'capture-ignore-dispatch-failure': changed(
                'if (WINE_UNIX_CALL(54, &params))\n    return false;',
                '(void)WINE_UNIX_CALL(54, &params);'),
            'native-result-always-success': changed(
                'params->ret = [manager startCaptureWithDescriptor:desc error:&error];',
                '(void)[manager startCaptureWithDescriptor:desc error:&error];\n  params->ret = true;'),
            'native-bypass-existing-capture': changed('manager.isCapturing || ', ''),
        }
        for name, text in mutations.items():
            compile_run(name, text, mutant=True)
    print('PASS native capture ABI: arm64+x86_64 compile; host ASan/UBSan; additive149 in both native tables')
    print(f'PASS {len(mutations)} compiled mutation controls: CHECK exit1, no sanitizer diagnostics')


if __name__ == '__main__':
    main()
