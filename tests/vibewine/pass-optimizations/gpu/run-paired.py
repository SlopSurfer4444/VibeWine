#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run only root-supplied isolated launchers; never construct/select a Wine prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import threading

from analyze import validate

CAP = 64 * 1024


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def supplied(path, executable=False):
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ValueError('supply an absolute regular nonsymlink file')
    if executable and not os.access(path, os.X_OK):
        raise ValueError('launcher is not executable')
    return path


def run(launcher, guest, directory, negative):
    argv = [str(launcher), str(guest), *(['--negative-pixel'] if negative else [])]
    # Launcher contract: it owns external sandbox, a disposable private server,
    # copied/hash-bound matched runtime, cache/tmp paths, and environment. The
    # adapter deliberately has no Wine/runtime/prefix fallback of its own.
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True)
    streams = {'stdout': bytearray(), 'stderr': bytearray()}
    totals = {'stdout': 0, 'stderr': 0}
    def drain(name, pipe):
        while chunk := pipe.read(8192):
            totals[name] += len(chunk)
            streams[name].extend(chunk[:max(0, CAP - len(streams[name]))])
        pipe.close()
    readers = [threading.Thread(target=drain, args=(name, pipe), daemon=True)
               for name, pipe in [('stdout', process.stdout), ('stderr', process.stderr)]]
    for reader in readers: reader.start()
    timed_out = False
    try:
        process.wait(timeout=50)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    for reader in readers: reader.join(timeout=2)
    if any(reader.is_alive() for reader in readers):
        # Only our process group, never a global/installed Wine server.
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        for reader in readers: reader.join(timeout=2)
    if any(reader.is_alive() for reader in readers):
        raise RuntimeError('launcher output pipe remains live; parent must reconcile private server')
    for name in streams: (directory / (name + '.log')).write_bytes(streams[name])
    result = {'argv': argv, 'exit': process.returncode, 'timedOut': timed_out,
              'bytes': totals, 'launcherSha256': digest(launcher)}
    try:
        if timed_out or any(total > CAP for total in totals.values()):
            raise ValueError('run timed out or exceeded bounded evidence cap')
        result['outputValidation'] = validate(bytes(streams['stdout']), process.returncode, negative)
        result['outputChecksPassed'] = True
    except (UnicodeError, ValueError) as error:
        result['outputChecksPassed'] = False
        result['error'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-launcher', type=Path, required=True)
    parser.add_argument('--candidate-launcher', type=Path, required=True)
    parser.add_argument('--guest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        baseline = supplied(args.baseline_launcher, True)
        candidate = supplied(args.candidate_launcher, True)
        guest = supplied(args.guest)
        output = args.output
        if not output.is_absolute() or output.exists() or output.is_symlink() or not output.parent.is_dir():
            raise ValueError('output must be a new absolute directory under an existing parent')
        if guest.stat().st_size > 32 * 1024 * 1024: raise ValueError('guest exceeds binary cap')
        output.mkdir(mode=0o700)
        receipt = {'schema': 1, 'guestSha256': digest(guest), 'runs': {},
                   'status': 'incomplete', 'scope': 'image correctness only; parent separately admits isolation and loaded PE/native hashes'}
        try:
            for label, launcher, negative in [('baseline', baseline, False), ('candidate', candidate, False),
                                               ('negative', candidate, True)]:
                directory = output / label; directory.mkdir(mode=0o700)
                receipt['runs'][label] = run(launcher, guest, directory, negative)
                if not receipt['runs'][label]['outputChecksPassed']:
                    raise ValueError(f'{label}: output checks failed; inspect run.json and captured logs')
            receipt['status'] = 'output_checks_passed_pending_parent_mapping_admission'
        finally:
            encoded = (json.dumps(receipt, indent=2) + '\n').encode()
            if len(encoded) > 32 * 1024: raise ValueError('receipt cap exceeded')
            (output / 'run.json').write_bytes(encoded)
    except (OSError, UnicodeError, ValueError, RuntimeError) as error:
        parser.exit(1, f'FAIL {error}\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
