#!/usr/bin/env python3
"""One-shot scene-triggered use of the existing cs2trace controller. No game input."""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import signal
import stat
import struct
import subprocess
import time

import session_phases as phases

CAPACITY = 131072
BYTES = 4096 + CAPACITY * 64
MAGIC = 0x4353325452414332
MIB = 1024 * 1024
MARKER = b'CS2TRACE_SESSION_V2\n'


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def integer(value, low, high, name):
    require(type(value) is int and low <= value <= high, 'invalid ' + name)
    return value


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def owned_read(path, cap):
    """Revalidate the path entry too; mmap header updates may change mtime."""
    path = Path(path)
    require(path.is_absolute() and path.resolve() == path, 'canonical file required')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_uid == os.getuid()
                and before.st_nlink == 1 and not before.st_mode & 0o022
                and 0 <= before.st_size <= cap, 'unsafe file or byte cap')
        data = bytearray()
        while len(data) <= cap:
            block = os.read(fd, min(65536, cap + 1 - len(data)))
            if not block:
                break
            data.extend(block)
        after, entry = os.fstat(fd), path.lstat()
        key = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_uid, s.st_nlink, s.st_mode)
        require(key(before) == key(after) == key(entry) and len(data) == before.st_size,
                'file identity/size changed')
        return bytes(data), f'{before.st_dev}:{before.st_ino}'
    finally:
        os.close(fd)


def header(raw):
    require(len(raw) == BYTES, 'wrong trace length')
    magic, version, capacity, reserved, control, dropped, rejected = struct.unpack_from('<7Q', raw)
    require((magic, version, capacity) == (MAGIC, 2, CAPACITY), 'trace protocol mismatch')
    generation, state, writers = control >> 32, (control >> 30) & 3, control & ((1 << 30) - 1)
    require(state == 0 and writers == 0, 'trace must be CLOSED with zero admitted writers')
    require(reserved <= CAPACITY and dropped == 0, 'trace capacity/loss invalid')
    return dict(generation=generation, reserved=reserved, boundaryRejected=rejected)


def validate_slots(raw, count, generation):
    for i in range(CAPACITY):
        at = 4096 + i * 64
        event_generation = struct.unpack_from('<I', raw, at + 36)[0]
        committed = struct.unpack_from('<Q', raw, at + 56)[0]
        if i < count:
            require(committed == i + 1 and event_generation == generation,
                    'prefix commit/generation mismatch')
        else:
            require(committed == 0, 'committed event beyond reserved prefix')


def fresh_source(path, expected_identity, reader=owned_read):
    raw, actual = reader(path, BYTES)
    require(actual == expected_identity, 'trace inode mismatch')
    h = header(raw)
    require(h == dict(generation=0, reserved=0, boundaryRejected=0), 'producer is not fresh')
    validate_slots(raw, 0, 0)
    return dict(identity=actual, generation=0, reserved=0, sha256=digest(raw))


def stopped_prefix(path, expected_identity, generation, reader=owned_read):
    """Finite two-read immutable proof, excluding only the late-rejection counter.

    The existing producer never resets/reuses slots. Exclusive controller
    ownership and the pre-arm fresh-source receipt are separate prerequisites.
    No original bytes are rewritten, normalized on disk or copied to a new file.
    """
    integer(generation, 1, 0xffffffff, 'generation')
    first, identity = reader(path, BYTES)
    second, identity2 = reader(path, BYTES)
    require(identity == identity2 == expected_identity, 'trace inode mismatch')
    a, b = header(first), header(second)
    require(a['generation'] == b['generation'] == generation, 'stopped generation mismatch')
    immutable = lambda raw: raw[:48] + raw[56:]
    require(immutable(first) == immutable(second), 'stopped immutable source changed')
    require(b['boundaryRejected'] >= a['boundaryRejected'], 'boundary counter decreased')
    validate_slots(first, a['reserved'], generation)
    validate_slots(second, b['reserved'], generation)
    end = 4096 + b['reserved'] * 64
    return dict(schema=1, snapshotMode='original-stopped-immutable-reference',
                source=str(path), sourceIdentity=identity, generation=generation,
                bytes=BYTES, secondCopyBytes=0, publishedRecords=b['reserved'],
                immutableSha256=digest(immutable(second)),
                publishedPrefixSha256=digest(second[4096:end]),
                immutableHashScope='file bytes [0,48) + [56,8392704); only [48,56) excluded',
                boundaryRejectedBefore=a['boundaryRejected'], boundaryRejectedAfter=b['boundaryRejected'],
                wholeFileSha256AtRead=digest(second),
                limits=['Revalidate immutable digest, identity, generation and slots before analysis.',
                        'Late boundary counter growth is not payload corruption; its cause/time is unknown.',
                        'No asynchronous-operation completion, scene coverage or FPS acceptance is implied.'])


def verify_reference(receipt, reader=owned_read):
    """Required immediately before consuming a held original, without a copy."""
    require(receipt.get('schema') == 1 and receipt.get('snapshotMode') == 'original-stopped-immutable-reference',
            'reference receipt schema')
    current = stopped_prefix(receipt['source'], receipt['sourceIdentity'], receipt['generation'], reader)
    for key in ('immutableSha256', 'publishedPrefixSha256', 'publishedRecords', 'bytes'):
        require(current[key] == receipt[key], 'reference invalidated: ' + key)
    require(current['boundaryRejectedBefore'] >= receipt['boundaryRejectedAfter'], 'reference boundary counter regressed')
    return current


def policy(value):
    require(type(value) is dict, 'capture policy required')
    require(value.get('profile') in ('combined-bounded', 'stall-waits'), 'supported explicit profile required')
    for key, low, high in (('waitSeconds', 1, 900), ('windowSeconds', 1, 600),
                          ('pollMilliseconds', 50, 1000), ('markerMaxAgeSeconds', 1, 30),
                          ('plannedRecords', 1, CAPACITY), ('tailReserve', 1, CAPACITY),
                          ('stopReserved', 1, CAPACITY), ('detailLimit', 0, 32768)):
        integer(value.get(key), low, high, key)
    require(value['plannedRecords'] + value['tailReserve'] <= CAPACITY, 'admission budget exceeds capacity')
    require(value['stopReserved'] <= value['plannedRecords'], 'stop threshold exceeds planned budget')
    return dict(value)


class Capture:
    """Injected seams are exercised offline; Native supplies existing real APIs."""
    def __init__(self, backend, config):
        self.io, self.config = backend, config
        self.policy = policy(config['policy'])
        self.role = phases.identity(config['game'])
        self.arm_attempted = self.stop_attempted = self.stopped = False

    def alive(self):
        require(self.io.process_identity(self.role['pid']) == self.role, 'game birth ended/changed')

    def run(self):
        p, io = self.policy, self.io
        result = dict(schema=1, phase='failed', armed=False)
        try:
            self.alive()
            io.check_launch(p)
            io.emit('capture-ready.json', dict(phase='waiting_scene', armed=False, game=self.role, policy=p,
                    controllerSha256=self.config['controller']['sha256'], sourceBinding=self.config['sourceBinding']))
            deadline = io.monotonic() + p['waitSeconds']
            selected = None
            while io.monotonic() < deadline:
                self.alive()
                markers = io.markers()
                phases.validate_markers(markers, self.role)
                if markers:
                    marker = markers[-1]
                    age = io.wall() - marker['wall']
                    if (marker['phase'] == 'gameplay' and marker['wall'] >= self.config['notBeforeWall']
                            and 0 <= age <= p['markerMaxAgeSeconds']):
                        selected = marker
                        break
                io.sleep(p['pollMilliseconds'] / 1000)
            require(selected is not None, 'fresh observed gameplay marker timed out; never armed')
            self.alive()
            io.verify_trace_held(self.role)
            fresh = io.fresh()
            io.emit('capture-trigger.json', dict(phase='observed_gameplay_trigger', marker=selected, freshSource=fresh))
            # Mapping inspection may be slow. Recheck the visual boundary and
            # its age after that work, immediately before controller dispatch.
            self.alive()
            latest = io.markers()
            phases.validate_markers(latest, self.role)
            require(latest and latest[-1] == selected and
                    0 <= io.wall() - selected['wall'] <= p['markerMaxAgeSeconds'],
                    'scene changed or marker expired before arm; never armed')
            # This engine owns exactly one arm attempt and never retries it.
            self.arm_attempted = True
            armed = io.command('arm', 1, p['plannedRecords'], p['tailReserve'])
            require(armed.get('identity') == self.config['traceIdentity'] and armed.get('generation') == 1
                    and armed.get('capture') == 1, 'arm receipt mismatch')
            io.emit('capture-armed.json', dict(armed, trigger=selected, armedMonotonic=io.monotonic()))
            started = io.monotonic()
            reason, progress, phase_end = 'duration_limit', None, None
            while io.monotonic() - started < p['windowSeconds']:
                if io.process_identity(self.role['pid']) != self.role:
                    reason = 'game_birth_ended'
                    break
                markers = io.markers()
                phases.validate_markers(markers, self.role)
                if not markers or markers[-1] != selected:
                    reason, phase_end = 'scene_boundary_or_marker_change', markers[-1] if markers else None
                    break
                progress = io.command('inspect')
                require(progress.get('identity') == self.config['traceIdentity'] and progress.get('capture') == 1,
                        'active trace binding changed')
                count = integer(progress.get('next_reserved'), 0, 2**64 - 1, 'reserved progress')
                require(count <= CAPACITY and progress.get('dropped') == 0, 'active trace capacity/loss invalid')
                if count >= p['stopReserved']:
                    reason = 'sampled_reserved_limit'
                    break
                io.sleep(p['pollMilliseconds'] / 1000)
            elapsed = io.monotonic() - started
            self.stop()
            proof = io.proof()
            require(proof['publishedRecords'] > 0, 'no actual published records')
            io.emit('capture-proof.json', proof)
            result = dict(schema=1, phase='stopped_reference_saved', armed=False, stopReason=reason,
                          game=self.role, trigger=selected, phaseEnd=phase_end, elapsedSeconds=elapsed,
                          lastProgress=progress, policy=p, publishedRecords=proof['publishedRecords'],
                          coverage='only stopped observed records; scene transition poll tail remains unclassified',
                          overhead='unmeasured; requires disabled/enabled-unarmed/light/detail matched reference',
                          acceptance='No decode, whole-session coverage or FPS claim; original held for analysis.')
        except BaseException as error:
            result.update(error=str(error)[:2048], armed='unknown' if self.arm_attempted and not self.stopped else False)
            if self.arm_attempted and not self.stop_attempted:
                try:
                    self.stop()
                    result['armed'] = False
                except Exception as stop_error:
                    result['stopError'] = str(stop_error)[:2048]
        result.update(armAttempted=self.arm_attempted, stopAttempted=self.stop_attempted)
        io.emit('capture-final.json', result)
        return result

    def stop(self):
        require(not self.stop_attempted, 'stop already attempted')
        self.stop_attempted = True
        stopped = self.io.command('stop', 1)
        self.io.emit('capture-stop.json', stopped)
        require(stopped.get('identity') == self.config['traceIdentity'] and stopped.get('generation') == 1
                and stopped.get('capture') == 0 and stopped.get('producer_quiescence_verified') is True
                and stopped.get('structurally_complete') is True and stopped.get('admitted_writers') == 0,
                'stop not proven quiescent')
        self.stopped = True


def bounded_command(args, cap=32768):
    child = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    data = {child.stdout: bytearray(), child.stderr: bytearray()}
    deadline = time.monotonic() + 15
    try:
        with selectors.DefaultSelector() as selector:
            for stream in data:
                selector.register(stream, selectors.EVENT_READ)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                require(remaining > 0, 'owned command timeout')
                ready = selector.select(remaining)
                require(ready, 'owned command timeout')
                for key, _ in ready:
                    block = os.read(key.fileobj.fileno(), 4096)
                    if not block:
                        selector.unregister(key.fileobj)
                        continue
                    data[key.fileobj].extend(block)
                    require(sum(map(len, data.values())) <= cap, 'owned command output cap')
        require(child.wait(timeout=max(.01, deadline - time.monotonic())) == 0,
                'owned command failed: ' + data[child.stderr].decode(errors='replace')[:1024])
        return bytes(data[child.stdout])
    finally:
        if child.poll() is None:
            child.kill()  # Only our controller/lsof child, never the game.
            child.wait(timeout=2)
        for stream in data:
            stream.close()


class Native:
    monotonic = staticmethod(time.monotonic)
    wall = staticmethod(time.time)
    sleep = staticmethod(time.sleep)
    process_identity = staticmethod(phases.native_identity)

    def __init__(self, config):
        self.config = config
        self.directory = self.lock = self.trace_directory = -1
        try:
            self.directory = phases.open_directory(config['sessionDirectory'])
            self.lock = os.open('.scene-capture.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=self.directory)
            info = os.fstat(self.lock)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and info.st_nlink == 1
                    and stat.S_IMODE(info.st_mode) == 0o600 and info.st_size == 0, 'unsafe capture lock')
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.trace = Path(config['traceFile'])
            self.tool = Path(config['controller']['path'])
            self.trace_directory = phases.open_directory(self.trace.parent)
            self.trace_directory_identity = self.directory_identity(self.trace_directory)
            raw, self.marker_identity = owned_read(self.trace.parent / '.cs2trace-session', 64)
            require(raw == MARKER and re.fullmatch(r'dxmt-trace-session-[0-9a-f]{32}', self.trace.parent.name), 'session marker/name')
            require(re.fullmatch(r'events-[1-9][0-9]*\.bin', self.trace.name), 'producer filename')
            self.controller_pin()
        except BaseException:
            self.close()
            raise

    def close(self):
        for name in ('lock', 'directory', 'trace_directory'):
            fd = getattr(self, name)
            if fd >= 0:
                os.close(fd)
                setattr(self, name, -1)

    def controller_pin(self):
        raw, _ = owned_read(self.tool, 16 * MIB)
        require(digest(raw) == self.config['controller']['sha256'], 'controller pin mismatch')

    @staticmethod
    def directory_identity(fd):
        info = os.fstat(fd)
        return (info.st_dev, info.st_ino)

    def verify_session(self):
        fd = phases.open_directory(self.trace.parent)
        try:
            require(self.directory_identity(fd) == self.trace_directory_identity, 'trace session replaced')
        finally:
            os.close(fd)
        raw, identity = owned_read(self.trace.parent / '.cs2trace-session', 64)
        require(identity == self.marker_identity and raw == MARKER, 'trace marker replaced')

    def command(self, command, *args):
        self.verify_session()
        self.controller_pin()
        tail = [] if command == 'inspect' else [str(args[0]), self.config['traceIdentity'], *map(str, args[1:])]
        return phases.loads(bounded_command([str(self.tool), command, str(self.trace), *tail]))

    def emit(self, name, value):
        raw = (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()
        require(len(raw) <= 65536, 'receipt cap')
        fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=self.directory)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.fsync(self.directory)

    def markers(self):
        try:
            raw = phases.read_file(self.directory, 'markers.jsonl')
        except FileNotFoundError:
            return []
        return [phases.loads(line) for line in raw.splitlines() if line.strip()]

    def verify_trace_held(self, role):
        require(self.process_identity(role['pid']) == role, 'game changed before mapping')
        raw = bounded_command(['/usr/sbin/lsof', '-nP', '-a', '-p', str(role['pid']), '-F', 'n'], MIB)
        paths = {line[1:] for line in raw.decode().splitlines() if line.startswith('n')}
        require(str(self.trace) in paths and self.process_identity(role['pid']) == role, 'trace not held by exact game birth')

    def fresh(self):
        return fresh_source(self.trace, self.config['traceIdentity'], self.read_trace)

    def proof(self):
        return stopped_prefix(self.trace, self.config['traceIdentity'], 1, self.read_trace)

    def read_trace(self, path, cap):
        self.verify_session()
        result = owned_read(path, cap)
        self.verify_session()
        return result

    def check_launch(self, p):
        launch = self.config['launch']
        require(launch.get('traceProfileRequested') == p['profile']
                and type(launch.get('traceDetailRecordLimit')) is int
                and launch['traceDetailRecordLimit'] == p['detailLimit'], 'launch profile/detail mismatch')
        binding = self.config['sourceBinding']
        for name in ('hubRevision', 'wineRevision', 'dxmtRevision'):
            require(type(binding.get(name)) is str and re.fullmatch('[0-9a-f]{40}', binding[name]), 'source revision binding')
        for name in ('buildReceiptSha256', 'installedManifestSha256', 'rendererSha256'):
            require(type(binding.get(name)) is str and re.fullmatch('[0-9a-f]{64}', binding[name]), 'source artifact binding')
        require(launch.get('rendererSha256') == binding['rendererSha256'], 'launch renderer mismatch')
        modules = binding.get('loadedModules')
        require(type(modules) is dict and 1 <= len(modules) <= 32 and all(type(k) is str and 1 <= len(k) <= 1024
                and type(v) is str and re.fullmatch('[0-9a-f]{64}', v) for k, v in modules.items()), 'loaded module binding required')
        require(type(self.config['notBeforeWall']) in (int, float) and math.isfinite(self.config['notBeforeWall'])
                and self.config['notBeforeWall'] > 0, 'launch wall boundary required')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    args = parser.parse_args()
    raw, _ = owned_read(args.config, 65536)
    config = phases.loads(raw)
    require(config.get('schema') == 1, 'config schema')
    os.umask(0o077)
    backend = Native(config)
    def interrupted(signum, frame):
        raise InterruptedError('operator interrupted by signal ' + str(signum))
    previous = signal.signal(signal.SIGTERM, interrupted)
    try:
        result = Capture(backend, config).run()
    finally:
        signal.signal(signal.SIGTERM, previous)
        backend.close()
    print(json.dumps(result, sort_keys=True))
    return 0 if result['phase'] == 'stopped_reference_saved' else 1


if __name__ == '__main__':
    raise SystemExit(main())
