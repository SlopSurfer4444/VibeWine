#!/usr/bin/env python3
"""Bounded real x64 bridge/native ARM acceptance; no compiler or GPU invocation.

Adapted from the owned 2026-10-28 trial.py. All executables must be built from
this candidate with one build identity. Output is a fresh, caller-owned directory.
No case retries an uncertain dispatch. Result JSON contains only observed passes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import selectors
import signal
import statistics
import struct
import subprocess
import sys
import time

OUTPUT_CAP = 40 * 1024 * 1024
RESULT_CAP = 8 * 1024 * 1024
STREAM_CAP = 512 * 1024
STDERR_CAP = 8192


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if not __debug__:
        raise RuntimeError('acceptance checks require Python assertions; do not use -O')
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('worker', 'bridge-executable', 'full-recipe-executable', 'cap-executable', 'fixtures', 'output-dir'):
        parser.add_argument('--' + option, required=True, type=Path)
    parser.add_argument('--timeout', type=float, default=85, help='Whole-run wall cap, 30..90 seconds (default 85)')
    args = parser.parse_args()
    if not 30 <= args.timeout <= 90:
        parser.error('--timeout must be 30..90 seconds')
    executables = {key: getattr(args, key).resolve(strict=True) for key in ('worker', 'bridge_executable', 'full_recipe_executable', 'cap_executable')}
    for path in executables.values():
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error('not an executable file: ' + str(path))
    fixtures = args.fixtures.resolve(strict=True)
    manifest = json.loads((fixtures / 'manifest.json').read_text())
    if manifest.get('schema') != 'dxmt-worker-fixtures-v1':
        parser.error('unsupported fixture manifest')
    corpus = {}
    for entry in manifest['fixtures']:
        path = fixtures / entry['name']
        if Path(entry['name']).name != entry['name'] or path.is_symlink():
            parser.error('fixture must be a plain basename')
        if path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            parser.error('fixture size/hash mismatch: ' + str(path))
        if entry['role'] in corpus:
            parser.error('duplicate fixture role')
        corpus[entry['role']] = path
    if set(corpus) != {'vs', 'ps', 'cs', 'resources'} or sum(p.stat().st_size for p in corpus.values()) > 1024 * 1024:
        parser.error('exact bounded VS/PS/CS/resources corpus required')
    output = args.output_dir.absolute()
    output.mkdir(parents=True, exist_ok=False)
    faults = output / 'faults'
    faults.mkdir()
    started = time.monotonic()
    end = started + args.timeout
    records = []
    result = {'schema': 'dxmt-worker-bridge-acceptance-v1', 'status': 'running',
              'scope': 'real x64 bridge/native ARM, direct byte oracle, no GPU/game/FPS claim',
              'executables': {k: {'path': str(p), 'sha256': digest(p)} for k, p in executables.items()},
              'fixture_manifest_sha256': digest(fixtures / 'manifest.json'),
              'runner_sha256': digest(Path(__file__).resolve()), 'checks': records}

    def save():
        size = sum(p.stat().st_size for p in output.rglob('*') if p.is_file())
        if size > OUTPUT_CAP:
            raise RuntimeError('40 MiB output cap exceeded')
        result['wall_seconds'] = time.monotonic() - started
        temporary = output / 'results.json.tmp'
        payload = json.dumps(result, indent=2) + '\n'
        if len(payload.encode()) > RESULT_CAP:
            raise RuntimeError('8 MiB result cap exceeded')
        temporary.write_text(payload)
        temporary.replace(output / 'results.json')

    def run(command, *, env_extra=None, input_bytes=None, pass_fds=(), timeout=15, child_pidfile=None):
        remaining = min(timeout, end - time.monotonic())
        if remaining <= 0:
            raise TimeoutError('whole-run deadline')
        env = dict(os.environ)
        for key in ('EXPECT_REMOTE', 'EXPECT_LOCAL', 'DXMT_WORKER_CANCEL_READY'):
            env.pop(key, None)
        env['DXMT_AIRCONV_WORKER'] = str(executables['worker'])
        env.update(env_extra or {})
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        began = time.monotonic()
        proc = subprocess.Popen(list(map(str, command)), env=env, stdin=subprocess.PIPE if input_bytes is not None else subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True, pass_fds=pass_fds)
        streams = [bytearray(), bytearray()]
        selector = selectors.DefaultSelector()
        try:
            if input_bytes is not None:
                # Direct-wire probes are <=112 bytes and cannot fill the pipe.
                assert len(input_bytes) <= 112
                try:
                    proc.stdin.write(input_bytes)
                    proc.stdin.close()
                except BrokenPipeError:
                    proc.stdin.close()
            for index, stream in enumerate((proc.stdout, proc.stderr)):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, index)
            while selector.get_map() or proc.poll() is None:
                if time.monotonic() - began >= remaining:
                    raise TimeoutError('case or whole-run deadline: ' + str(command[0]))
                for key, _ in selector.select(min(0.05, remaining)):
                    chunk = os.read(key.fd, 8192)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    else:
                        streams[key.data].extend(chunk)
                        if len(streams[key.data]) > (STREAM_CAP if key.data == 0 else STDERR_CAP):
                            raise RuntimeError('subprocess output cap exceeded')
            proc.wait(timeout=0.5)
            # Check bridge-owned reap before the harness safety net can hide a
            # surviving or zombie fault child. Appended PIDs also expose retries.
            if child_pidfile is not None:
                reaped(child_pidfile)
        finally:
            # Includes any fault shim still alive after its bridge exits; no orphan
            # from a failed assertion, timeout, or interrupted aggregate is retained.
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait(timeout=2)
            selector.close()
            proc.stdout.close()
            proc.stderr.close()
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        return {'exit': proc.returncode, 'stdout': bytes(streams[0]), 'stderr': bytes(streams[1]),
                'child_absent_before_harness_cleanup': child_pidfile is not None,
                'wall_ms': (time.monotonic() - began) * 1000,
                'cpu_ms': 1000 * (after.ru_utime + after.ru_stime - usage.ru_utime - usage.ru_stime)}

    def checked(label, command, **kwargs):
        observation = run(command, **kwargs)
        if observation['exit'] != 0:
            raise AssertionError((label, observation['exit'], observation['stderr'].decode(errors='replace')[-2000:]))
        parsed = [json.loads(line) for line in observation.pop('stdout').decode().splitlines()]
        if not parsed:
            raise AssertionError((label, 'missing JSON observations'))
        observation.update(control=label, records=parsed, stderr=observation['stderr'].decode(errors='replace'))
        records.append(observation)
        save()
        return observation

    def bridge(mode, role='ps', worker=None, **kwargs):
        env = dict(kwargs.pop('env_extra', {}))
        if worker is not None:
            env['DXMT_AIRCONV_WORKER'] = str(worker)
        return checked(mode + ':' + role, [executables['bridge_executable'], mode, corpus[role]], env_extra=env, **kwargs)

    def reaped(pidfile):
        admitted = pidfile.read_text().splitlines()
        assert len(admitted) == 1, ('fault dispatched more than once', admitted)
        pid = int(admitted[0])
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return pid
        raise AssertionError(('unreaped bridge child', pid))

    base = '''import os,time,signal,struct,json

def exact(n):
 data=b''
 while len(data)<n:
  part=os.read(0,n-len(data))
  if not part:raise SystemExit(1)
  data+=part
 return data
h=exact(48)
magic,v,op,gen,rid,a,b,c,z=struct.unpack('<8sIIQQIIII',h)
remaining=a+b+c
while remaining:
 part=exact(min(65536,remaining))
 remaining-=len(part)
'''

    def shim(name, code):
        path = faults / name
        pidfile = faults / (name + '.pid')
        path.write_text('#!' + sys.executable + '\n' + base + f'open({str(pidfile)!r},"a").write(str(os.getpid())+"\\n")\n' + code + '\n')
        path.chmod(0o700)
        return path, pidfile

    try:
        worker_info = checked('worker_identity', [executables['worker'], '--build-info'], timeout=20)['records'][0]
        bridge_info = checked('bridge_identity', [executables['bridge_executable'], '--build-info'], timeout=20)['records'][0]
        identity = worker_info['build_id']
        assert re.fullmatch('[0-9a-f]{64}', identity)
        assert bridge_info['build_id'] == identity
        assert worker_info['architecture'] == 'arm64' and bridge_info['architecture'] == 'x86_64'
        assert worker_info['protocol'] == bridge_info['protocol'] == 2
        result['build_id'] = identity
        rows = []
        for role in ('vs', 'ps', 'cs', 'resources'):
            observation = checked('parity:' + role, [executables['bridge_executable'], 'parity', corpus[role], output / role], env_extra={'EXPECT_REMOTE': '1'})
            samples = observation['records']
            assert len(samples) == 12 and samples[-1]['successes'] == 12
            local, remote = output / (role + '-local.metallib'), output / (role + '-remote.metallib')
            assert local.read_bytes() == remote.read_bytes() and local.stat().st_size > 16
            observation['air_sha256'] = digest(local)
            rows.append(observation)
        concurrent = bridge('concurrent', 'resources', env_extra={'EXPECT_REMOTE': '1'})
        assert concurrent['records'][-1]['concurrent_passed'] == 8
        assert 12 <= concurrent['records'][-1]['successes'] < 20
        rotation = bridge('rotate', env_extra={'EXPECT_REMOTE': '1'})
        assert rotation['records'][-1]['generation'] == 2 and rotation['records'][-1]['successes'] == 258
        bridge('invalid')
        bridge('cycle')
        bridge('parity', worker=output / 'absent-worker', env_extra={'EXPECT_LOCAL': '1'})
        for role in ('vs', 'ps', 'cs'):
            full = checked('full_recipe:' + role, [executables['full_recipe_executable'], corpus[role]])
            assert full['records'][0]['remote_successes'] == 1 and full['records'][0]['all_nine_nodes'] and full['records'][0]['air_equal']
        checked('retained_error_cap', [executables['cap_executable'], corpus['ps']])
        fault_cases = {
            'eof_live': 'os.close(1);time.sleep(60)',
            'crash': 'os.kill(os.getpid(),signal.SIGKILL)',
            'response_cap': "os.write(1,struct.pack('<8sIIQQIIII',b'DXMTRP02',2,0,gen,rid,4*1024*1024+1,0,0,0));time.sleep(60)",
            'wrong_generation': "os.write(1,struct.pack('<8sIIQQIIII',b'DXMTRP02',2,2,gen+1,rid,0,0,1,0)+b'x');time.sleep(60)",
            'wrong_request': "os.write(1,struct.pack('<8sIIQQIIII',b'DXMTRP02',2,2,gen,rid+1,0,0,1,0)+b'x');time.sleep(60)",
            'bad_envelope': "os.write(1,struct.pack('<8sIIQQIIII',b'DXMTRP02',2,0,gen,rid,16,1,0,0)+b'MTLB'+b'0'*13);time.sleep(60)",
            'deadline': 'time.sleep(60)',
            'closed_input': 'os.close(0);time.sleep(60)',
            'cancel': 'time.sleep(60)',
        }
        for name, code in fault_cases.items():
            path, pidfile = shim(name, code)
            observation = bridge('cancel' if name == 'cancel' else 'error', worker=path,
                                 child_pidfile=pidfile,
                                 env_extra={'DXMT_WORKER_CANCEL_READY': str(pidfile)} if name == 'cancel' else {})
            observation.update(fault=name, reaped_pid=reaped(pidfile))
            if name == 'closed_input':
                assert observation['cpu_ms'] < 1000, ('half-close CPU busy loop', observation)
        marker = faults / 'fd-marker'
        marker.write_bytes(b'owned marker')
        with marker.open('rb') as fd:
            st = os.fstat(fd.fileno())
            report = faults / 'fd-env.json'
            path, pidfile = shim('fd-env', f'''leaked=False
for name in os.listdir('/dev/fd'):
 try:
  value=os.fstat(int(name));leaked |= value.st_dev=={st.st_dev} and value.st_ino=={st.st_ino}
 except OSError:pass
open({str(report)!r},'w').write(json.dumps({{'leaked':leaked,'keys':sorted(os.environ)}}))
os.close(1)
time.sleep(60)''')
            observation = bridge('error', worker=path, env_extra={'DXMT_OWNED_POISON': 'must-not-propagate'}, pass_fds=(fd.fileno(),), child_pidfile=pidfile)
            isolation = json.loads(report.read_text())
            assert not isolation['leaked'] and 'DXMT_OWNED_POISON' not in isolation['keys']
            observation.update(fault='fd_environment_isolation', isolation=isolation, reaped_pid=reaped(pidfile))
        wrong_identity = (('1' if identity[0] != '1' else '0') + identity[1:]).encode()
        probes = {'wrong_version': (99, 1, 1, 1, 32, 0, 64, 0), 'oversize_dxbc': (2, 1, 1, 1, 1024*1024+1, 0, 64, 0),
                  'oversize_recipe': (2, 1, 1, 1, 32, 2*1024*1024+128*1024+1, 64, 0),
                  'wrong_identity': (2, 1, 1, 1, 32, 0, 64, 0), 'zero_generation': (2, 1, 0, 1, 32, 0, 64, 0),
                  'zero_request': (2, 1, 1, 0, 32, 0, 64, 0), 'reserved_bits': (2, 1, 1, 1, 32, 0, 64, 1)}
        for name, fields in probes.items():
            wire = struct.pack('<8sIIQQIIII', b'DXMTRQ02', *fields) + wrong_identity
            observation = run([executables['worker']], input_bytes=wire, timeout=2)
            assert observation['exit'] == (66 if name == 'wrong_identity' else 65) and not observation['stdout']
            records.append({'control': name, 'exit': observation['exit'], 'wall_ms': observation['wall_ms']})
        result['primary_parity_requests'] = sum(row['records'][-1]['successes'] for row in rows)
        result['timings'] = {row['control']: {'cold_remote_ms': row['records'][0]['remote_ms'],
                                           'warm_remote_median_ms': statistics.median(v['remote_ms'] for v in row['records'][1:]),
                                           'direct_median_ms': statistics.median(v['direct_ms'] for v in row['records'][1:])} for row in rows}
        for key, path in executables.items():
            assert digest(path) == result['executables'][key]['sha256'], ('executable changed during run', key)
        assert digest(fixtures / 'manifest.json') == result['fixture_manifest_sha256']
        result['status'] = 'passed'
        save()
        print(json.dumps({'status': result['status'], 'primary_parity_requests': result['primary_parity_requests'], 'control_groups': len(records), 'results': str(output / 'results.json')}))
    except BaseException as error:
        result.update(status='failed', failure=type(error).__name__ + ': ' + str(error))
        save()
        raise


if __name__ == '__main__':
    main()
