#!/usr/bin/env python3
"""Exercise bounded builder failure controls; never compiles or reads LLVM."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('native_worker_builder', ROOT / 'tools/build-native-arm-worker.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
checks = []
build_id = 'a' * 64
expected = {'schema': 'dxmt-native-arm-worker-info-v1', 'build_id': build_id,
            'architecture': 'arm64', 'protocol': 2, 'max_requests': 256}
with patch.object(builder, 'tool_output', return_value='arm64'), \
     patch.object(builder.subprocess, 'check_output', return_value=json.dumps(expected).encode()):
    assert builder.worker_info(Path('/owned/worker'), build_id) == expected
    try:
        builder.worker_info(Path('/owned/worker'), 'b' * 64)
    except ValueError:
        pass
    else:
        raise AssertionError('stale embedded build identity accepted')
with patch.object(builder, 'tool_output', return_value='x86_64 arm64'):
    try:
        builder.worker_info(Path('/owned/worker'), build_id)
    except ValueError:
        pass
    else:
        raise AssertionError('universal/translated worker accepted as native-only')
checks.append('embedded-identity-and-native-architecture-admission')
with tempfile.TemporaryDirectory(prefix='dxmt-native-builder-controls-') as scratch:
    directory = Path(scratch)
    declaration = directory / 'meson.build'
    declaration.write_text("sources = files(['one.cpp', 'two.cpp'])\n")
    assert builder.meson_list(declaration, 'sources', True) == ['one.cpp', 'two.cpp']
    declaration.write_text("sources = files(['one.cpp', dynamic_expression])\n")
    try:
        builder.meson_list(declaration, 'sources', True)
    except ValueError:
        pass
    else:
        raise AssertionError('non-static declaration accepted')
    checks.append('static-meson-list-validation')

    # Leader exits; its child keeps stdout open. Deadline must still kill the group.
    child_file = directory / 'child.pid'
    fixture = directory / 'leader.py'
    fixture.write_text('import os,time\npid=os.fork()\nif pid:\n open(' +
                       repr(str(child_file)) + ',"w").write(str(pid))\n os._exit(0)\ntime.sleep(60)\n')
    with (directory / 'build.log').open('wb') as log:
        try:
            builder.bounded([sys.executable, str(fixture)], directory, time.monotonic() + 3, .3, log)
        except RuntimeError as error:
            assert 'deadline' in str(error)
        else:
            raise AssertionError('missing compiler deadline')
    child = int(child_file.read_text())
    until = time.monotonic() + 2
    while True:
        state = subprocess.run(['ps', '-o', 'stat=', '-p', str(child)], capture_output=True, text=True).stdout.strip()
        if not state or time.monotonic() >= until:
            break
        time.sleep(.02)
    assert not state, ('task-owned descendant survived', child, state)
    checks.append('exited-leader-descendant-killed')

    fixture.write_text('import pathlib,time\npathlib.Path(' + repr(str(directory / 'oversize')) +
                       ').write_bytes(b"x"*8192)\ntime.sleep(60)\n')
    previous = builder.BUILD_CAP
    builder.BUILD_CAP = 4096
    try:
        with (directory / 'build.log').open('wb') as log:
            try:
                builder.bounded([sys.executable, str(fixture)], directory, time.monotonic() + 3, 2, log)
            except RuntimeError as error:
                assert 'cap' in str(error)
            else:
                raise AssertionError('missing active output cap')
    finally:
        builder.BUILD_CAP = previous
    checks.append('active-output-cap-kills-writer')
print(json.dumps({'passed': checks}))
