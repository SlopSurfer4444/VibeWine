#!/usr/bin/env python3
"""Opt-in actual GUI-PE logger endpoint test; explicit local tools, private prefix."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', required=True, type=Path)
parser.add_argument('--wine', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path, help='Fresh owned directory; never an existing prefix')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
prefix = out / 'prefix'
prefix.mkdir()
identity = (prefix.stat().st_dev, prefix.stat().st_ino)
server = Path(f'/private/tmp/.wine-{os.getuid()}/server-{identity[0]:x}-{identity[1]:x}')
exe = out / 'idle-delivery.exe'
sources = [HERE / 'fixture.cpp', ROOT / 'src/util/log/log.cpp',
           ROOT / 'src/util/util_env.cpp', ROOT / 'src/util/util_string.cpp']
command = [str(args.cxx.absolute()), '-std=c++20', '-O1', '-Wall', '-Wextra',
           '-Werror', '-Wno-unused-parameter', '-static', '-mwindows',
           '-I'+str(ROOT / 'src/util'), '-I'+str(ROOT / 'src/dxmt'),
           *map(str, sources), '-o', str(exe)]
subprocess.run(command, check=True, timeout=60)
pe = exe.read_bytes()
pe_offset = struct.unpack_from('<I', pe, 0x3c)[0]
assert pe[pe_offset:pe_offset+4] == b'PE\0\0'
assert struct.unpack_from('<H', pe, pe_offset+24+68)[0] == 2, 'Fixture must use the GUI subsystem'
# No writes outside this fresh project fixture or this prefix's exact Wine socket
# directory; no IP networking or Launch Services calls. The fixture creates no UI,
# and winemac is disabled even during private prefix initialization.
profile = out / 'isolation.sb'
profile.write_text(f'''(version 1)
(allow default)
(deny file-write* (require-all
  (require-not (subpath {json.dumps(str(out))}))
  (require-not (subpath {json.dumps(str(server))}))
  (require-not (literal "/private/tmp/.wine-{os.getuid()}"))
  (require-not (literal "/dev/null"))))
(deny network-outbound (remote ip "*:*"))
(deny network-inbound (local ip "*:*"))
(deny mach-lookup (global-name "com.apple.coreservices.launchservicesd"))
''')
env = {key: value for key, value in os.environ.items()
       if not key.startswith(('WINE', 'DXMT', 'DYLD'))}
env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEARCH='win64',
           WINEDLLOVERRIDES='winemenubuilder.exe=d;winemac.drv=d',
           DXMT_LOG_PATH='none', DXMT_LOG_LEVEL='none',
           WINESERVER=str(args.wine.resolve().with_name('wineserver')),
           WINELOADER=str(args.wine.resolve()),
           WINEDLLPATH=str(args.wine.resolve().parents[1]/'lib/wine'),
           DYLD_FALLBACK_LIBRARY_PATH=str(args.wine.resolve().parents[2]/'Frameworks')+':/usr/lib',
           WINEMSYNC='0', WINEESYNC='0',
           HOME=str(out), TMPDIR=str(out))
runner = ['/usr/bin/sandbox-exec', '-f', str(profile)]
rows = []

def limit():
    resource.setrlimit(resource.RLIMIT_FSIZE, (128*1024*1024, 128*1024*1024))

def run(name, argv, runtime_env=env, timeout=45):
    log = out / (name+'.log')
    with log.open('wb') as capture:
        result = subprocess.run(runner+argv, env=runtime_env, cwd=out,
                                stdout=capture, stderr=subprocess.STDOUT,
                                timeout=timeout, preexec_fn=limit)
    text = log.read_text(errors='replace')
    assert len(log.read_bytes()) < 1024*1024, name+' capture cap reached'
    assert result.returncode == 0, (name, result.returncode, text[-4000:])
    return text

try:
    # Prefix setup may start short-lived Wine services only in this owned prefix.
    run('prefix-init', [str(args.wine.resolve()), 'wineboot', '-i'], timeout=60)
    run('prefix-settle', [str(args.wine.resolve().with_name('wineserver')), '-w'], timeout=60)
    assert (prefix/'system.reg').is_file(), 'Wine prefix init did not complete'
    for name, enabled, mode, level, expected in [
        ('absent', None, '', 'none', 0),
        ('zero', '0', '', 'none', 0),
        ('old-invalid', '1', 'old', 'none', 0),
        ('new-invalid', '1', '', 'none', 64),
        ('new-usable', '1', 'usable', 'none', 64),
        ('old-usable', '1', 'old usable', 'none', 0),
        ('new-info', '1', '', 'info', 64),
    ]:
        case_env = env.copy()
        if enabled is not None:
            case_env['DXMT_IDLE_ATTRIBUTION'] = enabled
        case_env['DXMT_LOG_LEVEL'] = level
        guest_receipt = out/(name+'-guest.txt')
        case_env['DXMT_FIXTURE_RECEIPT'] = 'Z:'+str(guest_receipt).replace('/', '\\')
        text = run(name, [str(args.wine.resolve()), str(exe), mode], case_env)
        guest = dict(field.split('=') for field in guest_receipt.read_text().split())
        assert guest['code'] == '0' and int(guest['records']) == (64 if enabled=='1' else 0), (name, guest)
        assert guest['enabled'] == ('1' if enabled=='1' else '0')
        assert guest['stderr_invalid'] == ('0' if 'usable' in mode else '1')
        if name == 'old-invalid': assert guest['old_failed'] == '64'
        lines = [line for line in text.splitlines() if re.search(r'VIBE_IDLE(?:_START)? ', line)]
        assert len(lines) == expected, (name, len(lines), text[-1000:])
        if expected:
            assert re.fullmatch(r'info:  VIBE_IDLE_START pid=\d+ queue=2 enabled=1 time_ns=1', lines[0]), lines[0]
            assert all(line.startswith('info:  VIBE_IDLE ') for line in lines[1:])
            assert all(len(line.encode())+1 <= 1019 for line in lines)
            assert 'sampled=3 registered=19' in lines[1]
            assert 'idle=8/99/1 unclassified=7/0/7 released_window=10/1024' in lines[1]
            assert 'snapshot=64 ' in lines[-1]
        if level == 'none':
            assert 'NORMAL_INFO_SENTINEL' not in text and 'NORMAL_ERROR_SENTINEL' not in text
        else:
            assert 'NORMAL_INFO_SENTINEL' in text and 'NORMAL_ERROR_SENTINEL' in text
        if name == 'old-usable':
            windows_lines = (out/'windows-stderr.txt').read_text().splitlines()
            assert len(windows_lines) == 64
        rows.append(dict(name=name, captured_records=len(lines), bytes=len(text.encode()), guest=guest))
finally:
    # Scope every stop/wait to the exact private prefix, never a shared server.
    subprocess.run(runner+[str(args.wine.resolve().with_name('wineserver')), '-k'], env=env,
                   cwd=out, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    subprocess.run(runner+[str(args.wine.resolve().with_name('wineserver')), '-w'], env=env,
                   cwd=out, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, check=True)
    assert identity == (prefix.stat().st_dev, prefix.stat().st_ino)
    shutil.rmtree(prefix) # Exact newly-created owned prefix, after terminal server wait.

bound_sources = sources + [ROOT/'src/util/log/log.hpp', ROOT/'src/dxmt/dxmt_idle_diagnostics.hpp',
                           ROOT/'src/dxmt/dxmt_idle_retirement.hpp']
engine = args.wine.resolve().parents[1]
engine_files = [args.wine.resolve(), engine/'bin/wineserver', engine/'lib/wine/x86_64-unix/ntdll.so',
                engine/'lib/wine/x86_64-windows/ntdll.dll']
receipt = dict(result='PASS', cases=rows, gui_subsystem=True, source_sha256={
    str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in bound_sources},
    exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
    wine=str(args.wine.resolve()), wine_sha256=hashlib.sha256(args.wine.read_bytes()).hexdigest(),
    engine_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in engine_files},
    prefix_removed=True, server_wait_complete=True)
(out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(json.dumps(receipt, indent=2))
