#!/usr/bin/env python3
"""Decode an immutable native acquisition snapshot; never writes output paths."""
import argparse
import json
import os
import stat
import struct

HEADER = struct.Struct('<8s31Q')
RECORD = struct.Struct('<10Q')
FIELDS = ('version header_bytes record_bytes capacity threshold_ns pid birth_seconds '
          'birth_microseconds wall_clock cpu_clock started finished qualifying dropped nil clock_errors').split()


def decode(path, expected_identity=None, expected_pid=None, expected_birth=None):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        st = os.fstat(fd)
        identity = f'{st.st_dev}:{st.st_ino}'
        if not stat.S_ISREG(st.st_mode) or st.st_uid != os.geteuid() or stat.S_IMODE(st.st_mode) != 0o600 or st.st_nlink != 1:
            raise ValueError('private owner-controlled regular file required')
        if expected_identity is not None and identity != expected_identity:
            raise ValueError('file identity mismatch')
        if st.st_size != 256 + 1024 * 80:
            raise ValueError('wrong fixed file length')
        data = os.pread(fd, st.st_size, 0)
        if len(data) != st.st_size:
            raise ValueError('short snapshot')
        values = HEADER.unpack_from(data)
        if values[0] not in (b'W7ACQ01\0', b'W7ACQ02\0'):
            raise ValueError('incomplete or unsupported header')
        h = dict(zip(FIELDS, values[1:]))
        version = h['version']
        if (version not in (1, 2) or values[0] != f'W7ACQ0{version}\0'.encode() or
                (h['header_bytes'], h['record_bytes'], h['capacity'], h['threshold_ns'], h['wall_clock'], h['cpu_clock']) != (256, 80, 1024, 100000000, 1, 2) or
                any(values[17 if version == 1 else 18:])):
            raise ValueError('unsupported schema or nonzero reserved header')
        h['skipped_occluded'] = values[17] if version == 2 else 0
        if expected_pid is not None and h['pid'] != expected_pid:
            raise ValueError('PID mismatch')
        birth = f"{h['birth_seconds']}:{h['birth_microseconds']}"
        if expected_birth is not None and birth != expected_birth:
            raise ValueError('process birth mismatch')
        if not h['pid'] or h['birth_microseconds'] >= 1000000 or not h['birth_seconds']:
            raise ValueError('invalid producer identity')
        if not (h['clock_errors'] <= h['qualifying'] <= h['finished'] <= h['started'] and
                h['nil'] <= h['qualifying'] and h['dropped'] == max(h['qualifying'] - 1024, 0)):
            raise ValueError('inconsistent counters or non-quiescent snapshot')
        records, seen, holes = [], set(), []
        for i in range(min(h['qualifying'], 1024)):
            commit, call, thread, layer, drawable, ws, we, cs, ce, flags = RECORD.unpack_from(data, 256 + i * 80)
            if commit == 0:
                holes.append(i)
                continue
            if commit != i + 1 or not (0 < call <= h['started']) or call in seen or flags & ~15 or bool(flags & 1) != (drawable == 0):
                raise ValueError('invalid/duplicate record')
            if (flags & 2 and we < ws) or (not flags & 2 and (ws or we)) or (flags & 4 and ce < cs) or (not flags & 4 and (cs or ce)):
                raise ValueError('invalid clock payload')
            if (flags & 8 and not thread) or (not flags & 8 and thread):
                raise ValueError('invalid native thread identity')
            wall = we - ws if flags & 2 else None
            cpu = ce - cs if flags & 4 else None
            if flags & 6 == 6 and not flags & 1 and wall < h['threshold_ns']:
                raise ValueError('record below threshold without error/nil')
            seen.add(call)
            records.append(dict(slot=i, call=call, native_thread=thread if flags & 8 else None,
                                layer=layer, drawable=drawable, nil=bool(flags & 1), flags=flags,
                                native_wall_start_ns=ws if flags & 2 else None,
                                native_wall_end_ns=we if flags & 2 else None,
                                native_wall_ns=wall, thread_cpu_ns=cpu))
        # Reader is deliberately for stopped-producer files or independently
        # captured immutable snapshots. No live quiescence claim is inferred.
        after = os.fstat(fd)
        if after.st_size != st.st_size or os.pread(fd, 256, 0) != data[:256]:
            raise ValueError('snapshot changed during decoding')
        return dict(schema=1, identity=identity, birth=birth, header=h,
                    scope='immutable snapshot only; native duration comparison with PE12 only, no shared epoch or automatic frame join',
                    wall_clock='CLOCK_MONOTONIC_RAW nanoseconds', cpu_clock='CLOCK_THREAD_CPUTIME_ID nanoseconds',
                    uncommitted_slots=holes, recorded=len(records),
                    unrecorded_finished=h['finished'] - h['qualifying'],
                    records=records)
    finally:
        os.close(fd)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('snapshot')
    p.add_argument('--identity')
    p.add_argument('--pid', type=int)
    p.add_argument('--birth')
    a = p.parse_args()
    try:
        print(json.dumps(decode(a.snapshot, a.identity, a.pid, a.birth), indent=2))
    except (OSError, ValueError) as error:
        p.exit(2, f'{error}\n')
