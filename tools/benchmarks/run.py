#!/usr/bin/env python3
"""Finite D3D11 suite. Explicit private runtime only; no game/helper dispatch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import stat
import subprocess
import sys
import time
import uuid

CAPACITY = 131072
RAW_BYTES = 8392704
PROBE_SENTINEL = 1685613940
COMMAND_CAP = 24 * 1024 * 1024
IMAGE_CAP = 3840 * 2160 * 4
IMAGE_FRAME = 120


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def regular(path, maximum=None):
    path = Path(path)
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'regular single-link file required: ' + str(path))
    require(maximum is None or info.st_size <= maximum, 'file exceeds cap: ' + str(path))
    return info


def digest(path):
    regular(path)
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    raw = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
    require(len(raw) <= 1024 * 1024, 'receipt exceeds cap')
    path = Path(path)
    temporary = path.with_name('.' + path.name + '-' + uuid.uuid4().hex)
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path)  # atomic visibility; refuses an existing target
    finally:
        temporary.unlink()


def read_json(path, cap=65536):
    regular(path, cap)
    with open(path, encoding='utf8') as stream:
        return json.load(stream)


def terminate_owned(child):
    if child.poll() is not None:
        return
    # start_new_session gives this launch a group unrelated to the invoking shell.
    require(os.getpgid(child.pid) == child.pid, 'owned process group changed; cleanup unknown')
    os.killpg(child.pid, signal.SIGTERM)
    try:
        child.wait(timeout=2)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.wait(timeout=2)


def command(argv, timeout=10, cap=COMMAND_CAP):
    child = subprocess.Popen(list(map(str, argv)), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             start_new_session=True)
    chunks = [bytearray(), bytearray()]
    sel = selectors.DefaultSelector()
    for i, stream in enumerate((child.stdout, child.stderr)):
        sel.register(stream, selectors.EVENT_READ, i)
    end = time.monotonic() + timeout
    try:
        while sel.get_map():
            require(time.monotonic() < end, 'command timeout: ' + str(argv[0]))
            for key, _ in sel.select(min(.1, max(.001, end - time.monotonic()))):
                part = os.read(key.fileobj.fileno(), 65536)
                if not part:
                    sel.unregister(key.fileobj)
                else:
                    chunks[key.data].extend(part)
                    require(sum(map(len, chunks)) <= cap, 'command output cap')
        code = child.wait(timeout=max(.001, end - time.monotonic()))
        require(code == 0, 'command exit ' + str(code) + ': ' + bytes(chunks[1])[:2048].decode(errors='replace'))
        return bytes(chunks[0])
    finally:
        sel.close()
        terminate_owned(child)
        child.stdout.close()
        child.stderr.close()


def wait_for(path, child, deadline, logs=(), tick=None):
    while time.monotonic() < deadline:
        for log in logs:
            require(log.stat().st_size <= 65536, 'guest output cap')
        if tick is not None:
            tick()
        if path.exists():
            return read_json(path)
        require(child.poll() is None, 'guest exited before ' + path.name + ': ' + str(child.returncode))
        time.sleep(.025)
    raise ValueError('guest deadline waiting for ' + path.name)


def bound_message(value, run_id, pid=None):
    require(value.get('schema') == 1 and value.get('runId') == run_id, 'guest protocol/run binding')
    require(type(value.get('pid')) is int and value['pid'] > 0, 'guest Windows PID')
    require(pid is None or value['pid'] == pid, 'guest PID changed')
    return value


def validate_probe_completion(value):
    require(value.get('gpuEventComplete') is True, 'probe GPU EVENT completion proof missing or false')
    require(value.get('cpuReadbackComplete') is True, 'probe CPU readback completion proof missing or false')
    require(type(value.get('readbackValue')) is int and value['readbackValue'] == PROBE_SENTINEL,
            'probe CPU readback sentinel mismatch/type')


def native_binding(trace_file, child, modules):
    # lsof observes the mapped producer file rather than guessing Windows/native PID equality.
    holders = command(['/usr/sbin/lsof', '-t', str(trace_file)], cap=65536).decode().split()
    candidates = []
    for text in set(holders):
        if text.isdigit():
            pid = int(text)
            try:
                if os.getpgid(pid) == child.pid:
                    candidates.append(pid)
            except ProcessLookupError:
                pass
    require(len(candidates) == 1, 'exactly one owned native trace holder required')
    pid = candidates[0]
    birth = command(['/bin/ps', '-p', str(pid), '-o', 'lstart='], cap=65536).decode().strip()
    require(bool(birth), 'native birth unavailable')
    mapped = command(['/usr/sbin/lsof', '-a', '-p', str(pid), '-Fn'], cap=1024*1024).decode(errors='replace')
    names = {line[1:] for line in mapped.splitlines() if line.startswith('n')}
    proof = {}
    for module in modules:
        module = Path(module).resolve(strict=True)
        require(str(module) in names, 'required module not mapped: ' + str(module))
        proof[str(module)] = digest(module)
    require(bool(proof), 'at least one independently mapped runtime module required')
    return dict(nativePid=pid, birth=birth, processGroup=child.pid, mappedModules=proof)


def controller(cli, expected_hash, *args):
    require(digest(cli) == expected_hash, 'collector bytes changed')
    return json.loads(command([cli, *args]))


def validate_guest_paths(output, session):
    # Guest options and the producer's CreateFileA both have finite path budgets.
    # Reserve ten decimal PID digits and the terminating NUL for the trace leaf.
    windows_output = 'Z:' + str(output).replace('/', '\\')
    trace_leaf = 'Z:' + str(session).replace('/', '\\') + '\\events-4294967295.bin'
    require(len(windows_output.encode('utf-8')) <= 220,
            'guest output path exceeds 220-byte budget; use a shorter result root')
    require(len(trace_leaf.encode('utf-8')) < 260,
            'Win32 trace path exceeds MAX_PATH budget; use a shorter result root')


def validate_window_request(width, height):
    require((width == 0 and height == 0) or (64 <= width <= 3840 and 64 <= height <= 2160),
            'explicit window dimensions must be a bounded pair')


def validate_geometry_proof(value, args):
    if not args.window_width and not args.window_height:
        return  # Old guests remain valid only for the existing coupled mode.
    expected = dict(renderWidth=args.width, renderHeight=args.height,
                    windowWidth=args.window_width, windowHeight=args.window_height)
    actual = value.get('geometry')
    require(type(actual) is dict and actual == expected and all(type(x) is int for x in actual.values()),
            'verified render/window geometry mismatch or missing proof')


def validate_image_proof(value, args, scene, directory, expected=None):
    """Hash the retained actual pixels; correctness evidence outside measurement."""
    if not args.image_proof:
        return None  # Preserve default/old guest admission and argv.
    proof = value.get('imageProof')
    fields = dict(schema=1, artifact='shader-image.rgba', scene=scene, frameId=IMAGE_FRAME,
                  width=args.width, height=args.height, format='RGBA8_UNORM',
                  rows=args.height, rowBytes=args.width * 4, byteCount=args.width * args.height * 4,
                  gpuEventComplete=True, cpuReadbackComplete=True)
    require(type(proof) is dict and set(proof) == set(fields) | {'fnv1a64'},
            'image proof missing or fields mismatch')
    for key, wanted in fields.items():
        require(type(proof[key]) is type(wanted) and proof[key] == wanted,
                'image proof field mismatch/type: ' + key)
    require(type(proof['fnv1a64']) is str and re.fullmatch('[0-9a-f]{16}', proof['fnv1a64']),
            'image proof hash format')
    path = Path(directory) / fields['artifact']
    info = regular(path, IMAGE_CAP)
    require(info.st_size == fields['byteCount'], 'image byte count mismatch')
    sha, fnv, count = hashlib.sha256(), 14695981039346656037, 0
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        opened = os.fstat(stream.fileno())
        require((opened.st_dev, opened.st_ino) == (info.st_dev, info.st_ino), 'image identity changed')
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            count += len(block)
            require(count <= fields['byteCount'], 'image grew during hashing')
            sha.update(block)
            for byte in block:
                fnv = ((fnv ^ byte) * 1099511628211) & 0xffffffffffffffff
        after = os.fstat(stream.fileno())
        require((after.st_size, after.st_mtime_ns, after.st_ctime_ns, after.st_nlink) ==
                (info.st_size, info.st_mtime_ns, info.st_ctime_ns, 1), 'image changed during hashing')
    require(count == fields['byteCount'], 'image truncated during hashing')
    require(format(fnv, '016x') == proof['fnv1a64'], 'image guest hash mismatch')
    result = dict(guest=proof, sha256=sha.hexdigest(), byteCount=count)
    require(expected is None or result == expected, 'image readiness/result bytes or proof mismatch')
    return result


def execute_scene(args, scene, run_root):
    run_id = str(uuid.uuid4())
    out = run_root / (scene + '-' + run_id)
    out.mkdir(mode=0o700)
    cli = args.trace_controller
    collector_hash = digest(cli)
    prelaunch_modules = {str(path.resolve(strict=True)): digest(path.resolve(strict=True)) for path in args.module}
    session = controller(cli, collector_hash, 'new-session', str(out))['session']
    validate_guest_paths(out, session)  # Refuse before creating any Wine process.
    clock = controller(cli, collector_hash, 'clock-sample')
    ready_at = time.monotonic()
    # This receipt proves the native observer can operate before guest creation.
    # Producer attachment and first real data are independently required below.
    write_json(out / 'observer-ready.json', dict(schema=1, runId=run_id,
               phase='prelaunch-native-controller-ready', requestedProfile=args.profile,
               requestedPresentEvery=args.present_every, collectorSha256=collector_hash,
               traceSession=session, clock=clock, producerAttached=False, requiredModuleSha256=prelaunch_modules))
    env = os.environ.copy()
    env.update(args.environment)
    env.update(WINEPREFIX=str(args.prefix), DXMT_CS2_TRACE_DIR='Z:' + session.replace('/', '\\'),
               DXMT_CS2_TRACE_PROFILE=args.profile, DXMT_CS2_TRACE_DETAIL_LIMIT='12000')
    argv = [str(args.wine), str(args.guest), '--scene', scene, '--duration', str(args.duration),
            '--warmup', str(args.warmup), '--frames', str(args.frames), '--width', str(args.width), '--height', str(args.height),
            '--vsync', str(args.vsync), '--output', 'Z:' + str(out).replace('/', '\\'), '--run-id', run_id, '--barrier']
    # Older installed guests implement the default every-iteration mode only.
    if args.present_every != 1:
        argv += ['--present-every', str(args.present_every)]
    if args.window_width or args.window_height:
        argv += ['--window-width', str(args.window_width), '--window-height', str(args.window_height)]
    if args.image_proof:
        argv += ['--image-proof']
    logs = [out / 'stdout.log', out / 'stderr.log']
    child = None
    trace_file = trace_identity = None
    armed = stopped = False
    capture_generation = '1'
    receipt = dict(schema=1, runId=run_id, scene=scene, status='failed', profile=args.profile, requestedProfile=args.profile,
                   requestedPresentEvery=args.present_every,
                   requestedGeometry=dict(renderWidth=args.width, renderHeight=args.height,
                                          windowWidth=args.window_width or args.width,
                                          windowHeight=args.window_height or args.height),
                   guestSha256=digest(args.guest), collectorSha256=collector_hash,
                   cpuMetricsScope='guest CPU loop/Present call, not displayed FPS',
                   observerReadyBeforeLaunch=True)
    if args.image_proof:
        receipt['requestedImageProof'] = True
    end = time.monotonic() + args.timeout
    try:
        with open(logs[0], 'xb', buffering=0) as stdout, open(logs[1], 'xb', buffering=0) as stderr:
            child = subprocess.Popen(argv, env=env, stdout=stdout, stderr=stderr, start_new_session=True,
                                     cwd=out)
        receipt['launchMonotonic'] = time.monotonic()
        require(receipt['launchMonotonic'] >= ready_at, 'observer launch ordering')
        ready = bound_message(wait_for(out / 'ready.json', child, end, logs), run_id)
        receipt['startupToReadySeconds'] = time.monotonic() - receipt['launchMonotonic']
        receipt['startupScope'] = 'process launch through initialization and requested warmup; not cold-driver-cache cost'
        require(ready.get('scene') == scene, 'ready scene mismatch')
        validate_geometry_proof(ready, args)
        if args.image_proof:
            receipt['imageProof'] = validate_image_proof(ready, args, scene, out)
            receipt['startupScope'] += '; includes optional fixed-frame image proof'
            require(time.monotonic() < end, 'guest deadline after readiness validation')
        require(type(ready.get('presentEvery', 1)) is int and
                ready.get('presentEvery', 1) == args.present_every, 'ready presentation mode mismatch')
        pid = ready['pid']
        trace_file = Path(session) / ('events-' + str(pid) + '.bin')
        while not trace_file.exists() and time.monotonic() < end:
            require(child.poll() is None, 'guest ended before trace attachment')
            time.sleep(.025)
        require(regular(trace_file).st_size == RAW_BYTES, 'trace geometry')
        receipt['nativeBinding'] = native_binding(trace_file, child, args.module)
        require(receipt['nativeBinding']['mappedModules'] == prelaunch_modules, 'module bytes changed since prelaunch pin')
        initial = controller(cli, collector_hash, 'inspect', str(trace_file))
        require(initial['capture'] == 0 and initial['next_reserved'] == 0 and initial['dropped'] == 0,
                'producer not fresh')
        trace_identity = initial['identity']
        receipt['probeClockBegin'] = controller(cli, collector_hash, 'clock-sample')
        armed = True  # uncertain ARM must still receive one STOP attempt
        arm = controller(cli, collector_hash, 'arm', str(trace_file), '1', trace_identity, '110000', '21072')
        require(arm['capture'] == 1 and arm['generation'] == 1, 'ARM rejected')
        write_json(out / 'probe.json', dict(schema=1, runId=run_id, pid=pid))
        probe_completion = bound_message(wait_for(out / 'probe-done.json', child, end, logs), run_id, pid)
        receipt['probeCompletion'] = probe_completion
        validate_probe_completion(probe_completion)
        progress = controller(cli, collector_hash, 'inspect', str(trace_file))
        require(progress['identity'] == trace_identity and progress['capture'] == 1 and
                4 <= progress['next_reserved'] < 110000 and progress['dropped'] == 0,
                'no useful fresh probe records or trace exhausted')
        receipt['preMeasureRecords'] = progress['next_reserved']
        probe_stop = controller(cli, collector_hash, 'stop', str(trace_file), '1', trace_identity)
        require(probe_stop['capture'] == 0 and probe_stop['producer_quiescence_verified'] is True and
                probe_stop['generation'] == 1, 'probe STOP not quiescent')
        armed = False
        receipt['probeStop'] = probe_stop
        receipt['probeClockEnd'] = controller(cli, collector_hash, 'clock-sample')
        probe_raw = command([cli, 'decode', trace_file, capture_generation, trace_identity], cap=1024*1024)
        with open(out / 'probe.ndjson', 'xb') as stream:
            stream.write(probe_raw)
        from analyze import native_trace
        probe_analysis = native_trace(out / 'probe.ndjson', dict(width=args.width, height=args.height), expected_profile=args.profile)
        # Preserve the computed refusal before any gate can raise; the stopped
        # original and decoded probe remain useful diagnosis after a failed arm.
        receipt['probeAnalysis'] = probe_analysis
        probe_rows = [json.loads(line) for line in probe_raw.splitlines()]
        require(probe_rows[-1]['generation'] == 1 and probe_rows[-1]['identity'] == trace_identity,
                'probe decoder generation/identity mismatch')
        require(probe_analysis['observationUsable'],
                'probe observation rejected: ' + ','.join(probe_analysis.get('failures', [])) +
                '; rejections=' + ','.join(key for key, count in probe_analysis.get('rejections', {}).items() if count))
        require(any(row.get('type') == 40 for row in probe_rows[:-1]),
                'no parsed useful Present records before measurement')
        # Generation 2 is fresh without resetting the lifetime ring or prior evidence.
        remaining = controller(cli, collector_hash, 'inspect', str(trace_file))
        require(remaining['identity'] == trace_identity and remaining['capture'] == 0 and
                remaining['remaining_capacity'] >= 120000, 'insufficient measured capture capacity')
        capture_generation = '2'
        armed = True
        measure_arm = controller(cli, collector_hash, 'arm', str(trace_file), '2', trace_identity, '100000', '20000')
        require(measure_arm['capture'] == 2 and measure_arm['generation'] == 2, 'measured ARM rejected')
        receipt['measureClockBegin'] = controller(cli, collector_hash, 'clock-sample')
        receipt['guestReady'] = ready
        receipt['readyToMeasureSeconds'] = time.monotonic() - receipt['launchMonotonic'] - receipt['startupToReadySeconds']
        write_json(out / 'go.json', dict(schema=1, runId=run_id, pid=pid))
        last_poll = [0.0]
        def capture_tick():
            if time.monotonic() - last_poll[0] >= .2:
                progress = controller(cli, collector_hash, 'inspect', str(trace_file))
                require(progress['identity'] == trace_identity and progress['capture'] == 2 and
                        progress['next_reserved'] < 110000 and progress['dropped'] == 0,
                        'capture identity/budget reached')
                last_poll[0] = time.monotonic()
        results = wait_for(out / 'results.json', child, end, logs, tick=capture_tick)
        while child.poll() is None and time.monotonic() < end:
            for log in logs:
                require(log.stat().st_size <= 65536, 'guest output cap')
            progress = controller(cli, collector_hash, 'inspect', str(trace_file))
            require(progress['next_reserved'] < 110000 and progress['dropped'] == 0, 'capture budget reached')
            time.sleep(.025)
        require(child.poll() == 0, 'guest nonzero exit or timeout')
        bound_message(results, run_id, pid)
        require(results.get('status') == 'complete' and results.get('scene') == scene,
                'guest failed or wrong scene')
        validate_presentation_result(results, args.present_every)
        validate_geometry_proof(results, args)
        if args.image_proof:
            receipt['imageResultProof'] = validate_image_proof(results, args, scene, out, receipt['imageProof'])
            require(time.monotonic() < end, 'guest deadline after result validation')
        require(regular(out / 'perframe.csv', 4*1024*1024).st_size > 0, 'missing CPU frames')
        receipt['guestResult'] = results
        receipt['status'] = 'complete'
    except Exception as error:
        receipt['error'] = str(error)
    finally:
        if armed:
            try:
                stop = controller(cli, collector_hash, 'stop', str(trace_file), capture_generation, trace_identity)
                require(stop['capture'] == 0 and stop['producer_quiescence_verified'] is True and
                        stop['generation'] == int(capture_generation), 'STOP not quiescent')
                stopped = True
                receipt['traceStop'] = stop
                receipt['measureClockEnd'] = controller(cli, collector_hash, 'clock-sample')
            except Exception as error:
                receipt['status'] = 'failed'
                receipt['traceStopError'] = str(error)
        if child is not None:
            try:
                terminate_owned(child)
                receipt['guestExit'] = child.returncode
            except Exception as error:
                receipt['cleanupUnknown'] = str(error)
                receipt['status'] = 'failed'
        if stopped:
            try:
                require(digest(cli) == collector_hash, 'collector bytes changed before decode')
                raw = command([cli, 'decode', trace_file, capture_generation, trace_identity])
                with open(out / 'trace.ndjson', 'xb') as stream:
                    stream.write(raw)
                from analyze import analyze_run
                receipt['analysis'] = analyze_run(out, receipt.get('guestResult'), expected_profile=args.profile)
                trace_result = receipt['analysis']['trace']
                require(trace_result['observationUsable'], 'decoded trace not accepted: ' +
                        ','.join(trace_result['failures']) + '; profile=' +
                        ','.join(trace_result.get('producerProfile', {}).get('failures', [])))
            except Exception as error:
                receipt['status'] = 'failed'
                receipt['analysisError'] = str(error)
        write_json(out / 'run.json', receipt)
    return receipt, out


def validate_presentation_result(result, expected):
    observed = result.get('presentEvery', 1)
    require(type(observed) is int and observed == expected, 'result presentation mode mismatch')
    if expected != 1:
        frames, calls = result.get('frames'), result.get('measuredPresentCalls')
        require(type(frames) is int and frames > 0 and type(calls) is int and
                calls == frames // expected, 'diagnostic measured Present count mismatch')


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('wine', 'prefix', 'guest', 'trace-controller', 'output'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--module', action='append', required=True, type=Path,
                   help='actual required mapped runtime module path; repeat for D3D11/DXGI/native bridge')
    p.add_argument('--env', action='append', default=[], help='explicit runtime environment NAME=VALUE')
    p.add_argument('--profile', choices=('presentation-waits', 'combined-bounded'), default='presentation-waits')
    p.add_argument('--scenes', default='cube,drawstress,instanced,dynamicupload,pixelheavy,textured')
    p.add_argument('--duration', type=float, default=2)
    p.add_argument('--warmup', type=float, default=.5)
    p.add_argument('--frames', type=int, default=10000, help='stop at this measured frame cap or duration, whichever comes first')
    p.add_argument('--present-every', type=int, default=1, help='diagnostic render iterations per Present; values >1 are not displayed FPS')
    p.add_argument('--image-proof', action='store_true', help='retain and verify actual fixed-frame RGBA8 pixels before warmup; outside timing')
    p.add_argument('--vsync', type=int, choices=(0, 1), default=0)
    p.add_argument('--width', type=int, default=1280)
    p.add_argument('--height', type=int, default=720)
    p.add_argument('--window-width', type=int, default=0, help='optional client width, paired with --window-height; render size stays --width/--height')
    p.add_argument('--window-height', type=int, default=0)
    p.add_argument('--timeout', type=float, default=45)
    return p


def main():
    args = parser().parse_args()
    try:
        require(0.1 <= args.duration <= 10 and 0 <= args.warmup <= 10, 'bounded scene duration/warmup')
        require(1 <= args.frames <= 10000, 'bounded measured frame cap: 1..10000')
        require(1 <= args.present_every <= 64, 'bounded diagnostic presentation divisor: 1..64')
        require(64 <= args.width <= 3840 and 64 <= args.height <= 2160, 'bounded geometry')
        validate_window_request(args.window_width, args.window_height)
        require(10 <= args.timeout <= 120, 'bounded process timeout')
        args.environment = {}
        for value in args.env:
            key, sep, val = value.partition('=')
            require(sep and re.fullmatch('[A-Z][A-Z0-9_]*', key), 'environment NAME=VALUE required')
            require(key not in ('WINEPREFIX', 'DXMT_CS2_TRACE_DIR', 'DXMT_CS2_TRACE_PROFILE',
                                'DXMT_CS2_TRACE_DETAIL_LIMIT'), 'runner-owned environment')
            args.environment[key] = val
        scenes = args.scenes.split(',')
        require(1 <= len(scenes) <= 6 and len(set(scenes)) == len(scenes) and
                all(re.fullmatch('[a-z][a-z0-9-]{0,30}', s) for s in scenes), 'bounded unique scene names')
        require(4 <= len(args.module) <= 6 and len(set(args.module)) == len(args.module),
                'four to six explicit required module paths required')
        for path in args.module:
            require(path.is_absolute(), 'absolute module path required')
            regular(path.resolve(strict=True))
        for path in (args.wine, args.guest, args.trace_controller):
            require(path.is_absolute(), 'absolute executable path required')
            regular(path)
        require(args.prefix.is_absolute() and args.prefix.is_dir(), 'existing explicit private prefix required')
        require(args.output.is_absolute() and not args.output.exists(), 'new absolute output directory required')
        args.output.mkdir(mode=0o700)
        results = []
        for scene in scenes:
            result, directory = execute_scene(args, scene, args.output)
            results.append(dict(scene=scene, directory=str(directory), status=result['status']))
            if result['status'] != 'complete':
                break
        complete = len(results) == len(scenes) and all(x['status'] == 'complete' for x in results)
        write_json(args.output / 'suite.json', dict(schema=1, status='complete' if complete else 'failed',
                   scenes=results, plannedScenes=scenes, profile=args.profile, requestedProfile=args.profile,
                   settings=dict(duration=args.duration, warmup=args.warmup, frames=args.frames, presentEvery=args.present_every, vsync=args.vsync, width=args.width, height=args.height,
                                 windowWidth=args.window_width or args.width, windowHeight=args.window_height or args.height),
                   limits=['Synthetic workload only; no DS3/game FPS claim.',
                           'Native feedback scope includes calibration and shutdown; per-scene CPU timing is separate.',
                           'Collector overhead and A/A then A/B repeats require separate controlled runs.']))
        print(json.dumps(dict(status='complete' if complete else 'failed', output=str(args.output))))
        return 0 if complete else 1
    except Exception as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
