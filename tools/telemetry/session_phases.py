#!/usr/bin/env python3
"""Bounded phase annotations and offline joins for whole-session-monitor captures."""
import argparse
import collections
import ctypes
import errno
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import time
import zlib

MIB = 1024 * 1024
CAPS = {'markers.jsonl': MIB, 'events.jsonl': 16 * MIB,
        'metal.jsonstream.gz': 32 * MIB, 'status.json': MIB}
MAX_EXPANDED = 256 * MIB
MAX_OBJECT = MIB
MAX_RECORDS = 65536
PHASES = ('menu', 'loading', 'gameplay', 'exit', 'unknown')
MEMORY = ('physicalFootprintBytes', 'residentBytes', 'wiredBytes', 'pageins',
          'diskReadBytes', 'diskWriteBytes')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def number(value):
    require(type(value) in (int, float) and 0 <= value <= 1e30 and math.isfinite(value),
            'invalid finite nonnegative number')
    return value


def identity(value):
    require(type(value) is dict and type(value.get('pid')) is int and 1 < value['pid'] < 2**31
            and type(value.get('birth')) is str and
            re.fullmatch(r'[1-9][0-9]{0,19}:[0-9]{1,6}', value['birth']) is not None
            and int(value['birth'].split(':')[1]) < 1000000, 'PID and birth required')
    return {k: value[k] for k in ('pid', 'birth')}


def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def loads(raw):
    def invalid(value):
        raise ValueError('nonfinite JSON constant')
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid)


def open_directory(path):
    path = Path(path)
    require(path.is_absolute() and str(path) == os.path.normpath(str(path)), 'canonical absolute directory required')
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in path.parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = child
        info = os.fstat(fd)
        require(info.st_uid == os.getuid() and stat.S_IMODE(info.st_mode) == 0o700,
                'private owned session directory required')
        return fd
    except BaseException:
        os.close(fd)
        raise


def read_file(directory, name):
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
    try:
        before = os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_uid == os.getuid() and before.st_nlink == 1
                and stat.S_IMODE(before.st_mode) == 0o600 and before.st_size <= CAPS[name], 'unsafe file or file cap: ' + name)
        data = bytearray()
        while len(data) <= CAPS[name]:
            block = os.read(fd, min(65536, CAPS[name] + 1 - len(data)))
            if not block:
                break
            data.extend(block)
        after = os.fstat(fd)
        require(len(data) <= CAPS[name] and (before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'input changed while reading')
        return bytes(data)
    finally:
        os.close(fd)


def records(raw, *, max_object=MAX_OBJECT):
    """Incremental concatenated-object framing, following existing MetalWatch."""
    buf = bytearray()
    stack = []
    string = escape = False
    count = 0
    for block in raw:
        for byte in block:
            if not stack:
                if byte in b' \r\n\t':
                    continue
                require(byte == 123, 'expected JSON object; no resynchronization')
            buf.append(byte)
            require(len(buf) <= max_object, 'JSON object cap')
            if string:
                if escape:
                    escape = False
                elif byte == 92:
                    escape = True
                elif byte == 34:
                    string = False
            elif byte == 34:
                string = True
            elif byte in (123, 91):
                stack.append(byte)
                require(len(stack) <= 64, 'JSON depth cap')
            elif byte in (125, 93):
                require(stack and stack.pop() == (123 if byte == 125 else 91), 'mismatched JSON delimiter')
                if not stack:
                    count += 1
                    require(count <= MAX_RECORDS, 'record count cap')
                    yield loads(bytes(buf))
                    buf.clear()
    require(not buf, 'truncated JSON object')


def inflate(data, state):
    dec = zlib.decompressobj(31)
    total = 0
    for offset in range(0, len(data), 4096):
        block = data[offset:offset + 4096]
        while block:
            out = dec.decompress(block, min(65536, MAX_EXPANDED - total + 1))
            total += len(out)
            require(total <= MAX_EXPANDED, 'expanded gzip cap')
            block = dec.unconsumed_tail
            if out:
                yield out
    require(not dec.unused_data, 'trailing or concatenated gzip member')
    state['gzipComplete'] = dec.eof
    require(dec.eof or state['allowPartial'], 'truncated gzip; explicit --allow-partial required')


def marker_record(bound, phase, evidence, source, clock):
    require(phase in PHASES, 'unknown phase label')
    require(source in ('user-observation', 'controller-observation', 'process-reconciliation'), 'invalid provenance')
    require(source != 'process-reconciliation' or phase in ('exit', 'unknown'), 'process reconciliation cannot label a visual phase')
    require(type(evidence) is str and 1 <= len(evidence.encode()) <= 4096, 'bounded observation evidence required')
    require(set(clock) == {'mct', 'monotonic', 'wall'}, 'explicit clock anchors required')
    for value in clock.values():
        number(value)
    return dict(schema=1, identity=identity(bound), phase=phase, provenance=source,
                evidence=evidence, clock='mach_continuous_seconds',
                monotonicScope='marker-producer-process-local', producerPID=os.getpid(), **clock)


def validate_markers(markers, bound):
    require(len(markers) <= 4096, 'marker record cap')
    previous = None
    for row in markers:
        require(type(row) is dict and row.get('schema') == 1 and row.get('clock') == 'mach_continuous_seconds', 'marker schema or clock')
        require(identity(row.get('identity')) == bound, 'marker PID/birth mismatch')
        marker_record(bound, row.get('phase'), row.get('evidence'), row.get('provenance'),
                      {k: row.get(k) for k in ('mct', 'monotonic', 'wall')})
        if previous:
            require(all(row[k] > previous[k] for k in ('mct', 'wall')), 'unordered or overlapping marker boundaries')
            require(abs((row['wall'] - previous['wall']) - (row['mct'] - previous['mct'])) <= .1,
                    'marker wall/MCT discontinuity; memory alignment unsafe')
        previous = row
    return markers


def native_identity(pid):
    # Same fixed Darwin PROC_PIDTBSDINFO ABI as existing runner.process_identity.
    class BSDInfo(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint32) for n in ('flags', 'status', 'xstatus', 'pid', 'ppid', 'uid', 'gid', 'ruid', 'rgid', 'svuid', 'svgid', 'rfu')] + [
            ('comm', ctypes.c_char * 16), ('name', ctypes.c_char * 32)] + [(n, ctypes.c_uint32) for n in ('nfiles', 'pgid', 'pjobc', 'tdev', 'tpgid')] + [
            ('nice', ctypes.c_int32), ('seconds', ctypes.c_uint64), ('micros', ctypes.c_uint64)]
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = (ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int)
    lib.proc_pidinfo.restype = ctypes.c_int
    value = BSDInfo()
    size = lib.proc_pidinfo(pid, 3, 0, ctypes.byref(value), ctypes.sizeof(value))
    if size == 0 and ctypes.get_errno() == errno.ESRCH:
        return None
    require(size == ctypes.sizeof(value) and value.pid == pid and value.uid == os.getuid(), 'live identity unavailable')
    return dict(pid=pid, birth=f'{value.seconds}:{value.micros}')


def native_clock():
    class Timebase(ctypes.Structure):
        _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]
    lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
    lib.mach_continuous_time.restype = ctypes.c_uint64
    lib.mach_timebase_info.argtypes = (ctypes.POINTER(Timebase),)
    tb = Timebase()
    require(lib.mach_timebase_info(ctypes.byref(tb)) == 0 and tb.denom, 'MCT timebase unavailable')
    return dict(mct=lib.mach_continuous_time() * tb.numer / tb.denom / 1e9,
                monotonic=time.monotonic(), wall=time.time())


def append_marker(directory, bound, phase, evidence, provenance, *, get_identity=native_identity, get_clock=native_clock):
    bound = identity(bound)
    status = loads(read_file(directory, 'status.json'))
    require(identity(status['roles']['game']) == bound, 'capture PID/birth mismatch')
    actual = get_identity(bound['pid'])
    absent_exit = phase == 'exit' and provenance == 'process-reconciliation' and actual is None
    require(actual == bound or absent_exit, 'live PID/birth mismatch')
    fd = os.open('markers.jsonl', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=directory)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and info.st_nlink == 1
                and stat.S_IMODE(info.st_mode) == 0o600 and info.st_size <= MIB, 'unsafe marker file')
        raw = os.read(fd, MIB + 1)
        require(len(raw) <= MIB and (not raw or raw.endswith(b'\n')), 'marker cap or partial append')
        previous = list(records([raw]))
        row = marker_record(bound, phase, evidence, provenance, get_clock())
        validate_markers(previous + [row], bound)
        encoded = (json.dumps(row, separators=(',', ':'), allow_nan=False) + '\n').encode()
        require(len(raw) + len(encoded) <= MIB, 'marker byte cap')
        # Lock held over validation and append. No screen polling or timed-run work.
        require(get_identity(bound['pid']) == actual, 'process changed during marker append')
        require(os.write(fd, encoded) == len(encoded), 'partial marker write')
        os.fsync(fd)
        return row
    finally:
        os.close(fd)


def segment_at(start, end, markers, clock):
    for index, (left, right) in enumerate(zip(markers, markers[1:])):
        if left[clock] <= start and end <= right[clock] and start < right[clock]:
            return left['phase'], index
    if any(start < row[clock] < end for row in markers):
        return 'mixed', -2
    return 'unknown', -1 if not markers or end <= markers[0][clock] else len(markers)


def phase_at(start, end, markers, clock):
    return segment_at(start, end, markers, clock)[0]


def metal_interval(row, bound):
    require(type(row.get('PID')) is int, 'invalid Metal PID')
    if row['PID'] != bound['pid']:
        return None
    number(row.get('Timestamp'))
    layers = row.get('Layers')
    require(type(layers) is list and 1 <= len(layers) <= 128, 'invalid Layers')
    intervals, presented, skipped = [], 0, 0
    for layer in layers:
        require(type(layer) is dict and type(layer.get('Performance Stats')) is dict, 'invalid Performance Stats')
        stats = layer['Performance Stats']
        start, end, total = (number(stats.get(k)) for k in ('Start MCT Seconds', 'End MCT Seconds', 'Total Duration (sec)'))
        require(0 < end - start <= 5 and abs(total - (end - start)) <= .001, 'invalid Metal interval')
        intervals.append((start, end))
        for key in ('Presented Frame Stats', 'Skipped Frame Stats'):
            require(type(stats.get(key)) is dict, 'invalid frame stats')
            count = stats[key].get('Frame Count')
            require(type(count) is int and 0 <= count < 2**31, 'invalid frame count')
            if key.startswith('Presented'):
                presented += count
            else:
                skipped += count
    start, end = intervals[0]
    require(all(abs(a-start) <= 1e-6 and abs(b-end) <= 1e-6 for a,b in intervals), 'unaligned layers')
    return dict(start=start, end=end, duration=end-start, presented=presented, skipped=skipped,
                timestamp=row['Timestamp'], layers=len(layers))


def bucket():
    return dict(intervals=0, observedSeconds=0.0, presentedFrames=0, skippedFrames=0,
                intervalRateHz=None, declaredPhaseSeconds=None, memorySnapshots=[], hostSnapshots=[])


def _host_memory(result):
    """Parse only the existing monitor's known read-only command receipts."""
    if type(result) is not dict or result.get('returncode') != 0 or type(result.get('output')) is not str:
        return None
    argv, output = result.get('argv'), result['output']
    if argv == ['/usr/bin/vm_stat']:
        page = re.search(r'page size of ([0-9]+) bytes', output)
        if not page:
            return None
        page_size = number(int(page[1]))
        require(0 < page_size <= 65536, 'invalid host page size')
        values = {}
        for name in ('Pages free', 'Pages active', 'Pages inactive', 'Pages wired down',
                     'Pages occupied by compressor'):
            match = re.search(r'^' + re.escape(name) + r':\s*([0-9]+)\.', output, re.M)
            if match:
                values[name + ' bytes'] = number(int(match[1]) * page_size)
        for name in ('Pageins', 'Pageouts', 'Swapins', 'Swapouts'):
            match = re.search(r'^' + name + r':\s*([0-9]+)\.', output, re.M)
            if match:
                values[name] = number(int(match[1]))
        return 'vm_stat', values
    if argv == ['/usr/sbin/sysctl', 'vm.swapusage']:
        values = {}
        for key, value, unit in re.findall(r'(total|used|free) = ([0-9.]+)([KMGTP]?)', output):
            values[key + 'Bytes'] = number(float(value) * (1024 ** (' KMGTP'.index(unit) if unit else 0)))
        return 'swapusage', values
    if argv == ['/usr/bin/memory_pressure', '-Q']:
        match = re.search(r'System-wide memory free percentage:\s*([0-9.]+)%', output)
        return 'memory_pressure', {'freePercent': number(float(match[1]))} if match else {}
    return None


def host_memory(result):
    try:
        parsed = _host_memory(result)
        return parsed if parsed and parsed[1] else None
    except (ValueError, OverflowError):
        return None  # malformed or unavailable optional host receipt is not zero


def summarize_host(rows, *, segmented=False):
    groups = {}
    for row in rows:
        parsed = host_memory(row['result'])
        if parsed:
            kind, values = parsed
            groups.setdefault(kind, []).append(dict(wall=row['wall'], segment=row['segment'], values=values))
    return dict(samples=len(rows), unavailable=len(rows) - sum(len(items) for items in groups.values()), parsed={kind: dict(samples=len(items), first=items[0], last=items[-1],
        delta={k: items[-1]['values'][k] - items[0]['values'][k] for k in items[0]['values']
               if len(items) >= 2 and k in items[-1]['values']
               and (not segmented or len({r['segment'] for r in items}) == 1)},
        deltaScope='suppressed across disjoint phase spans' if segmented and len({r['segment'] for r in items}) > 1 else 'first-to-last sample') for kind, items in groups.items()},
        scope='host-wide snapshots; no attribution to game allocation or GPU residency')


def analyze(metal_data, event_data, marker_data, status, bound, *, allow_partial=False):
    bound = identity(bound)
    require(identity(status['roles']['game']) == bound, 'status PID/birth mismatch')
    require(status.get('final') is True or allow_partial, 'capture active/incomplete; explicit --allow-partial required')
    require(status.get('rawComplete') is True or allow_partial, 'collector raw incomplete; explicit --allow-partial required')
    markers = validate_markers(list(records([marker_data])), bound)
    report = dict(schema=1, identity=bound, complete=False, gzipComplete=False,
                  allowPartial=allow_partial, phases={p: bucket() for p in (*PHASES, 'mixed')},
                  whole=bucket(), discards=collections.Counter(), errors=[], markers=markers,
                  limits=['Counts describe reported Metal frames across aligned layers; intervalRateHz is count/observed duration, not displayed FPS.',
                          'No per-frame distribution, 1% lows, GPU residency or combat inference is available.',
                          'Markers are supplied observations, not proof of continuous visual state; pre-first and post-last are unknown.',
                          'Memory delta is last minus first available in-phase snapshot, never an exact boundary or causal delta; suppressed for disjoint phase spans.',
                          'Memory uses shared Unix wall timestamps; marker monotonic is producer-local and is never joined to monitor monotonic.'])
    for left, right in zip(markers, markers[1:]):
        item = report['phases'][left['phase']]
        item['declaredPhaseSeconds'] = (item['declaredPhaseSeconds'] or 0) + right['mct'] - left['mct']
    last = None
    try:
        for row in records(inflate(metal_data, report)):
            interval = metal_interval(row, bound)
            if interval is None:
                report['discards']['foreignPID'] += 1
                continue
            if last and (interval['start'] < last['end'] - 1e-6 or interval['timestamp'] <= last['timestamp']):
                report['discards']['duplicateOrOverlapping'] += 1
                continue
            if last and interval['start'] > last['end'] + 1e-6:
                report['discards']['coverageGaps'] += 1
            last = interval
            phase = phase_at(interval['start'], interval['end'], markers, 'mct')
            for item in (report['whole'], report['phases'][phase]):
                item['intervals'] += 1
                item['observedSeconds'] += interval['duration']
                item['presentedFrames'] += interval['presented']
                item['skippedFrames'] += interval['skipped']
    except (ValueError, KeyError, TypeError, UnicodeError, zlib.error, RecursionError) as error:
        if not allow_partial:
            raise ValueError('invalid Metal stream: ' + str(error)) from error
        report['errors'].append('Metal prefix only: ' + str(error))
    previous_event = None
    try:
        for row in records([event_data]):
            at, mono = number(row.get('wall')), number(row.get('monotonic'))
            if previous_event is not None:
                previous_wall, previous_mono = previous_event
                require(at >= previous_wall and mono >= previous_mono, 'unordered event timestamps')
                require(abs((at - previous_wall) - (mono - previous_mono)) <= .1, 'event wall/monotonic discontinuity; memory alignment unsafe')
            previous_event = at, mono
            if row.get('type') == 'role_identity_changed':
                report['errors'].append('collector reported role identity change')
                break
            phase, segment = segment_at(at, at, markers, 'wall')
            if row.get('type') == 'process_snapshot' and row.get('role') == 'game':
                memory = row.get('memory')
                if type(memory) is dict and memory.get('status') == 'ok':
                    sample = dict(wall=at, segment=segment, **{k: number(memory[k]) for k in MEMORY if k in memory})
                    for item in (report['whole'], report['phases'][phase]):
                        item['memorySnapshots'].append(sample)
                else:
                    report['discards']['unavailableMemory'] += 1
            elif row.get('type') == 'system_snapshot':
                # Parse the bounded command receipt later. Host scope is not game allocation.
                for item in (report['whole'], report['phases'][phase]):
                    item['hostSnapshots'].append(dict(wall=at, segment=segment, result=row.get('result')))
    except (ValueError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        if not allow_partial:
            raise ValueError('invalid event stream: ' + str(error)) from error
        report['errors'].append('Events prefix only: ' + str(error))
    for item in [report['whole'], *report['phases'].values()]:
        if item['declaredPhaseSeconds']:
            item['observedCoverageFraction'] = item['observedSeconds'] / item['declaredPhaseSeconds']
        else:
            item['observedCoverageFraction'] = None
        if item['observedSeconds']:
            item['intervalRateHz'] = item['presentedFrames'] / item['observedSeconds']
        samples = item.pop('memorySnapshots')
        item['memory'] = dict(samples=len(samples), first=samples[0] if samples else None,
                              last=samples[-1] if samples else None,
                              delta={k: samples[-1][k] - samples[0][k] for k in MEMORY
                                     if len(samples) >= 2 and k in samples[-1] and k in samples[0]
                                     and (item is report['whole'] or len({r['segment'] for r in samples}) == 1)},
                              deltaScope='suppressed across disjoint phase spans' if item is not report['whole'] and len({r['segment'] for r in samples}) > 1 else 'first-to-last sample')
        host = item.pop('hostSnapshots')
        item['hostMemory'] = summarize_host(host, segmented=item is not report['whole'])
    report['metricsAvailable'] = report['whole']['intervals'] > 0
    report['complete'] = bool(status.get('final') and status.get('rawComplete') and
                              not status.get('finalPartialJSON') and report['gzipComplete'] and not report['errors'])
    if not report['complete'] and not allow_partial:
        raise ValueError('capture is incomplete; explicit --allow-partial required')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('mark', 'report'):
        p = sub.add_parser(name)
        p.add_argument('--session', required=True, help='Exact private monitor output directory')
        p.add_argument('--pid', type=int, required=True)
        p.add_argument('--birth', required=True)
        if name == 'mark':
            p.add_argument('--phase', choices=PHASES, required=True)
            p.add_argument('--evidence', required=True)
            p.add_argument('--provenance', choices=('user-observation', 'controller-observation', 'process-reconciliation'), required=True)
        else:
            p.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    bound = dict(pid=args.pid, birth=args.birth)
    directory = open_directory(args.session)
    try:
        if args.command == 'mark':
            result = append_marker(directory, bound, args.phase, args.evidence, args.provenance)
        else:
            inputs = {}
            for name in CAPS:
                try:
                    inputs[name] = read_file(directory, name)
                except FileNotFoundError:
                    require(name == 'markers.jsonl', 'required capture input missing: ' + name)
                    inputs[name] = b''
            result = analyze(inputs['metal.jsonstream.gz'], inputs['events.jsonl'], inputs['markers.jsonl'],
                             loads(inputs['status.json']), bound, allow_partial=args.allow_partial)
            result['sourceSha256'] = {name: hashlib.sha256(raw).hexdigest() for name, raw in inputs.items()}
        encoded = json.dumps(result, indent=2, allow_nan=False)
        require(len(encoded.encode()) <= 4 * MIB, 'curated summary exceeds 4 MiB cap')
        print(encoded)
    finally:
        os.close(directory)


if __name__ == '__main__':
    main()
