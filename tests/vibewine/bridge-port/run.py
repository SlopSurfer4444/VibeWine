#!/usr/bin/env python3
"""Compile the real diagnostics and verbatim thunk against a mock native layer."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
OUT = REPO / 'build/native-acquire-tests'
OUT.mkdir(parents=True, exist_ok=True)
SOURCE = REPO / 'src/winemetal/unix'
spec = importlib.util.spec_from_file_location('decode_acquire', ROOT / 'decode.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
thunk = re.search(r'static NTSTATUS\n_MetalLayer_nextDrawable\(void \*obj\) \{.*?\n\}',
                  (SOURCE / 'winemetal_unix.c').read_text(), re.S).group()
fixture = (ROOT / 'fixture.m').read_text().replace('/* PRODUCTION_THUNK */', thunk)
fixture = fixture.replace('"../../../src/winemetal/unix/wave7_acquire_diag.inc"',
                          json.dumps(str(SOURCE / 'wave7_acquire_diag.inc')))
(OUT / 'fixture.m').write_text(fixture)


def run(args, **kwargs):
    result = subprocess.run([str(x) for x in args], text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(str(args) + '\n' + result.stdout + result.stderr)
    return result


def rejected(path, **kwargs):
    try:
        decoder.decode(path, **kwargs)
    except (OSError, ValueError):
        return
    raise AssertionError('decoder accepted invalid snapshot')


receipts = []
for architecture in ['arm64', 'x86_64']:
    binary = OUT / f'realclock-{architecture}'
    command = ['/usr/bin/clang', '-arch', architecture, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               ROOT / 'realclock.c', '-o', binary]
    run(command)
    with tempfile.TemporaryDirectory(prefix='w7ad-clock-', dir='/private/tmp') as scratch:
        environment = os.environ.copy()
        environment['DXMT_ACQUIRE_DIAGNOSTICS_DIR'] = scratch
        run([binary], env=environment)
        result = decoder.decode(next(Path(scratch).iterdir()))
        assert result['header']['started'] == result['recorded'] == 1
    receipts.append(dict(variant=f'realclock-{architecture}', command=list(map(str, command)),
                         binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest(), passed=True))
for variant, flags in [('regular', []), ('regular-x86_64', ['-arch', 'x86_64']),
                       ('sanitized', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    binary = OUT / variant
    command = ['/usr/bin/clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               *flags, OUT / 'fixture.m', '-framework', 'Foundation', '-o', binary]
    run(command)
    environment = os.environ.copy()
    environment.pop('DXMT_ACQUIRE_DIAGNOSTICS_DIR', None)
    # LeakSanitizer is not supported on Darwin; ASan and UBSan remain active.
    environment['ASAN_OPTIONS'] = 'detect_leaks=0'
    with tempfile.TemporaryDirectory(prefix='w7ad-', dir='/private/tmp') as scratch:
        base = Path(scratch)
        run([binary, 'disabled', 'unused'], env=environment)
        invalid = base / 'unsafe'; invalid.mkdir(mode=0o755)
        run([binary, 'path', invalid], env=environment)
        assert not list(invalid.iterdir())
        private = base / 'private'; private.mkdir(mode=0o700)
        link = base / 'link'; link.symlink_to(private, target_is_directory=True)
        for path in [link, base / 'link/child', str(private) + '/..', 'relative']:
            run([binary, 'path', path], env=environment)
        assert not list(private.iterdir())
        run([binary, 'enabled', private], env=environment)
        files = list(private.iterdir()); assert len(files) == 1
        path = files[0]
        result = decoder.decode(path)
        assert result['recorded'] == 1024 and result['header']['dropped'] == 583
        assert result['header']['started'] == 1610 and result['unrecorded_finished'] == 3
        assert result['header']['threshold_ns'] == 100000000
        assert result['records'][0]['native_wall_ns'] == 100000000
        assert result['records'][0]['thread_cpu_ns'] == 100
        assert result['records'][1]['native_wall_ns'] == 717460500
        assert result['records'][2]['nil']
        assert result['records'][3]['native_wall_ns'] is None
        assert result['records'][4]['thread_cpu_ns'] is None
        assert result['records'][5]['native_wall_ns'] is None
        assert result['records'][6]['thread_cpu_ns'] is None
        assert result['header']['clock_errors'] == 4
        decoder.decode(path, result['identity'], result['header']['pid'], result['birth'])
        rejected(path, expected_identity='0:0')
        rejected(path, expected_pid=result['header']['pid'] + 1)
        rejected(path, expected_birth='1:1')
        symlink = base / 'snapshot-link'; symlink.symlink_to(path); rejected(symlink)
        original = path.read_bytes()
        assert result['header']['version'] == 2 and result['header']['skipped_occluded'] == 0
        legacy = bytearray(original); legacy[:8] = b'W7ACQ01\0'; legacy[8:16] = (1).to_bytes(8,'little')
        path.write_bytes(legacy); assert decoder.decode(path)['header']['skipped_occluded'] == 0
        counted = bytearray(original); counted[136:144] = (7).to_bytes(8,'little')
        path.write_bytes(counted); assert decoder.decode(path)['header']['skipped_occluded'] == 7
        path.write_bytes(original)
        # Counter regression, unknown flags, malformed clocks and half-published slots.
        for offset, value in [(0, 0), (8, 3), (40, 10000000), (256 + 72, 32), (256, 2), (256 + 48, 0), (112, 0)]:
            broken = bytearray(original)
            broken[offset:offset + 8] = value.to_bytes(8, 'little')
            path.write_bytes(broken); rejected(path)
        duplicate = bytearray(original)
        duplicate[256 + 80 + 8:256 + 80 + 16] = original[256 + 8:256 + 16]
        path.write_bytes(duplicate); rejected(path)
        path.write_bytes(original[:-1]); rejected(path)
        path.write_bytes(original)
        hole = bytearray(original); hole[256:264] = bytes(8); path.write_bytes(hole)
        assert decoder.decode(path)['uncommitted_slots'] == [0]
        path.write_bytes(original)
        (OUT / f'{variant}-decoded.json').write_text(json.dumps(result, indent=2))
    receipts.append(dict(variant=variant, command=list(map(str, command)),
                         binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest(), passed=True))
receipt = dict(schema=1, scope='mock native layer; actual diagnostic sink and verbatim nextDrawable thunk; no Metal/GPU/Wine/game',
               sources={str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in [SOURCE / 'wave7_acquire_diag.inc', SOURCE / 'winemetal_unix.c', ROOT / 'fixture.m', ROOT / 'realclock.c', ROOT / 'decode.py', Path(__file__)]},
               variants=receipts)
(OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(dict(passed=True, variants=len(receipts), receipt=str(OUT / 'receipt.json'))))
