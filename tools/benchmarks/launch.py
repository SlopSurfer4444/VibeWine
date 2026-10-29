#!/usr/bin/env python3
"""One-click finite benchmark. Opens a qualified report after the guest closes."""
import argparse
import datetime
import fcntl
import hashlib
import html
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import time
import uuid

OWNER = 'vibewine-bench-v1'
SCENES = 'cube,drawstress,instanced,dynamicupload,pixelheavy,textured'
RESERVE = 224*1024*1024
RUNNER_BOOTSTRAP = '''import runpy,signal,sys
from pathlib import Path
def cancelled(signum, frame):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    raise RuntimeError("one-click owner requested bounded cancellation")
signal.signal(signal.SIGTERM,cancelled)
sys.argv=sys.argv[1:]
sys.path.insert(0,str(Path(sys.argv[0]).parent))
runpy.run_path(sys.argv[0],run_name="__main__")
'''


def regular(path, cap=32*1024*1024):
    info = Path(path).lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.getuid() or info.st_size > cap:
        raise ValueError('unsafe installed file: '+str(path))


def sha(path):
    regular(path)
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def load_config(path):
    path = Path(path).absolute()
    regular(path, 65536)
    config = json.loads(path.read_text())
    support = Path(config['support'])
    if config.get('schema') != 1 or config.get('owner') != OWNER or path != support/'config.json':
        raise ValueError('installation config binding')
    for directory in (support, support/'runtime', support/'runtime/state', support/'runtime/state/results'):
        info = directory.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o022:
            raise ValueError('unsafe installed directory: '+str(directory))
    receipt_path = support/'install-receipt.json'
    regular(receipt_path, 1024*1024)
    receipt = json.loads(receipt_path.read_text())
    if receipt.get('owner') != OWNER:
        raise ValueError('installation receipt owner')
    for relative, expected in receipt['files'].items():
        relative_path = Path(relative)
        if relative_path.is_absolute() or '..' in relative_path.parts or sha(support/relative) != expected:
            raise ValueError('installation bytes changed: '+relative)
    if config['runtime'] != str(support/'runtime') or config['defaults'] != dict(duration=10, warmup=1, vsync=1, width=1280, height=720,
                                                                             profile='presentation-waits'):
        raise ValueError('private runtime/default binding changed')
    return config


def raw_size(root):
    total = 0
    for path in root.rglob('*'):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            raise ValueError('unknown symlink in result retention scope')
        if stat.S_ISREG(info.st_mode):
            total += info.st_size
        elif not stat.S_ISDIR(info.st_mode):
            raise ValueError('unknown object in result retention scope')
    return total


def supervise_runner(command, log, log_path, results, cap, runtime, output, _popen=None):
    """Hold reuse before spawning; every exceptional path settles or retains hold."""
    hold = runtime/'state/cleanup-hold.json'
    record = dict(owner=OWNER, token=uuid.uuid4().hex, output=str(output),
                  reason='owned runner active or completion unresolved; owner recovery required if retained')
    raw = json.dumps(record, sort_keys=True)+'\n'
    with hold.open('x') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    held = hold.lstat()
    child = None
    code = None
    forced = False
    try:
        child = (_popen or subprocess.Popen)(command, stdout=log, stderr=subprocess.STDOUT,
                                             env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        deadline = time.monotonic()+360
        while child.poll() is None:
            if time.monotonic() > deadline or log_path.stat().st_size > 1024*1024 or raw_size(results) > cap:
                raise TimeoutError('suite deadline or aggregate output cap')
            time.sleep(.25)
        code = child.returncode
        return code
    finally:
        # Includes FileNotFoundError/OSError during directory scans, unexpected
        # exceptions, KeyboardInterrupt and SystemExit. Preserve the original
        # exception while keeping the preexisting hold on any uncertain path.
        original = sys.exc_info()[1]
        try:
            if child is not None and child.poll() is None:
                try:
                    child.terminate()  # bootstrap unwinds runner trace/guest finally blocks
                    child.wait(timeout=55)
                except BaseException:
                    forced = True
                    try:
                        child.kill()
                        child.wait(timeout=5)
                    except BaseException as cleanup_error:
                        print('owned runner settlement unresolved: '+str(cleanup_error), file=sys.stderr)
            # A nonzero/cancelled/forcibly stopped runner does not establish
            # that separate guest groups and server work settled. Keep its hold.
            settled_success = child is not None and child.poll() == 0 and code == 0 and not forced and original is None
            if settled_success:
                current = hold.lstat()
                if (current.st_dev, current.st_ino) != (held.st_dev, held.st_ino) or hold.read_text() != raw:
                    raise ValueError('cleanup hold identity changed; preserved')
                hold.unlink()
        except BaseException as cleanup_error:
            if original is None:
                raise
            print('cleanup retained its hold: '+str(cleanup_error), file=sys.stderr)


def render_report(output, status, error='', selected_profile=None):
    def pair_stats(value):
        if not isinstance(value, dict):
            return 'Нет данных'
        median, p99 = value.get('median_ms'), value.get('p99_ms')
        if not all(type(x) in (int, float) and math.isfinite(x) and x >= 0 for x in (median, p99)):
            return 'Нет пригодных данных'
        return '%.3f / %.3f (n=%s)' % (median, p99, value.get('samples', '?'))
    rows = []
    suite_path = output/'suite.json'
    suite = json.loads(suite_path.read_text()) if suite_path.is_file() else {}
    actual_profile = suite.get('profile')
    if actual_profile is not None and selected_profile is not None and actual_profile != selected_profile:
        raise ValueError('suite report profile differs from selected configuration')
    profile_label = actual_profile or selected_profile or 'не указан в исходных данных'
    for scene in suite.get('scenes', []):
        directory = Path(scene['directory'])
        if directory.parent != output:
            raise ValueError('scene report escaped owned suite')
        run_path = directory/'run.json'
        run = json.loads(run_path.read_text()) if run_path.is_file() else {}
        analysis = run.get('analysis', {})
        cpu = analysis.get('cpu', {}).get('frame', {})
        trace = analysis.get('trace', {})
        queue = trace.get('queueChunkMetrics', {})
        presentation = trace.get('nativePresentation', {})
        cadence = presentation.get('cadence', [])
        rates = [str(round(item['observed_interval_rate_hz'], 2)) for item in cadence
                 if type(item.get('observed_interval_rate_hz')) in (int, float)
                 and item.get('stream_state') == 'observed' and item.get('intervals') is not None]
        coverage = trace.get('presentationCoverage', {})
        joined = coverage.get('joinedFrameCount', 0)
        zeros = presentation.get('zeroPresentedTimeObservations', 0)
        quality = ('Проверка покрытия пройдена' if coverage.get('capture_usable') is True
                   else 'Частичное покрытие; полная проверка не пройдена')
        quality += '; связанных кадров: '+str(joined)+'; нулевых времён presentation: '+str(zeros)
        values = [scene['scene'], scene['status'], cpu.get('medianMs', '—'), cpu.get('p99Ms', '—'),
                  pair_stats(queue.get('gpu_command_buffer_durations')),
                  pair_stats(queue.get('encode_setup_commit')),
                  pair_stats(queue.get('publish_to_encode')),
                  ', '.join(rates) if rates else 'Нет пригодных наблюдений',
                  quality]
        rows.append('<tr>'+''.join('<td>'+html.escape(str(value))+'</td>' for value in values)+'</tr>')
    contents = '''<!doctype html><html lang="ru"><meta charset="utf-8"><title>VibeWine Bench</title>
<style>body{font:16px system-ui;margin:40px;max-width:1200px;color:#182331;background:#f5f7fa}h1{font-size:30px}table{border-collapse:collapse;width:100%;background:white}td,th{padding:10px;border:1px solid #d9e0e8;text-align:left}td:last-child{font:12px monospace;max-width:300px;overflow-wrap:anywhere}.note{padding:16px;background:#e7eef8;border-radius:12px}a{color:#125db0}</style>
<h1>VibeWine Bench</h1><p>Статус: <strong>STATUS</strong></p>
<p class="note">Шесть синтетических сцен, 1280×720, vsync 1, прогрев 1 с, измерение до 10 с на сцену. Обычно около 1–2 минут плюс запуск среды.
Профиль трассировки: PROFILE. CPU/GPU измерения принимаются отдельно от полноты native presentation: их наличие не подтверждает полный FPS. Неполное покрытие и отсутствующие времена показа сохраняются как ограничения.
CPU — время одного вызова цикла кадра приложения, включая Present; это не интервал между началами кадров. Native cadence — частичные наблюдения обратной связи drawable, а не физическая развёртка экрана.
GPU command buffer — отдельные интервалы часов GPU; они могут перекрываться и не складываются во время занятости GPU или время кадра. Encode и ожидание очереди — интервалы native CPU steady clock, отдельно от GPU.
Полная пригодность показывается отдельно; частичное наблюдение не становится доказанным FPS. Это не результат Dark Souls III и не доказательство ускорения.</p>
<div style="overflow:auto"><table><thead><tr><th>Сцена</th><th>Статус</th><th>CPU вызов median, ms</th><th>CPU вызов p99, ms</th><th>GPU command buffer median / p99, ms</th><th>CPU encode median / p99, ms</th><th>CPU очередь median / p99, ms</th><th>Частичная native cadence, Hz</th><th>Качество наблюдений</th></tr></thead><tbody>ROWS</tbody></table></div>
<p>ERROR</p><p><a href="suite.json">Исходный suite.json</a> · <a href="launcher.log">Журнал запуска</a></p>
<p>Не сравнивайте разные прогрев, кеши, питание или фон. Для вывода об изменении нужны повторные одинаковые A/A и затем A/B.</p>
<p>Raw cap: 1 GiB, TTL 7 дней; отчёты 30 дней. Автоудаления нет. Ручная очистка только точных завершённых каталогов после сохранения нужных доказательств; активные данные и удержанные ошибки сохраняются.</p></html>'''
    contents = contents.replace('STATUS', html.escape(status)).replace('ROWS', ''.join(rows)).replace('ERROR', html.escape(error)).replace('PROFILE', html.escape(profile_label))
    report = output/'report.html'
    report.write_text(contents)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--no-open', action='store_true', help='run without opening report UI')
    parser.add_argument('--plan', action='store_true', help='read/verify only; no Wine/controller execution')
    args = parser.parse_args(argv)
    report = None
    runtime = None
    try:
        config = load_config(args.config)
        support = Path(config['support'])
        runtime = Path(config['runtime'])
        if (runtime/'state/cleanup-hold.json').exists():
            raise ValueError('Previous runner cleanup is unresolved; the owner must reconcile that exact run before another launch.')
        results = runtime/'state/results'
        output = results/('suite-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
        command = [config['python'], '-B', '-c', RUNNER_BOOTSTRAP, str(support/'tools/benchmarks/run.py'),
                   '--wine', str(runtime/'wine'), '--prefix', str(runtime/'state/prefix'),
                   '--guest', str(support/'bin/dx11-benchmark.exe'), '--trace-controller', str(support/'bin/cs2trace'),
                   '--output', str(output), '--scenes', SCENES, '--duration', '10', '--warmup', '1',
                   '--vsync', '1', '--width', '1280', '--height', '720',
                   '--profile', config['defaults']['profile']]
        for module in config['modules']:
            command += ['--module', module]
        used = raw_size(results)
        if used + RESERVE > config['rawByteCap']:
            raise ValueError('Raw allowance exhausted; retain required evidence and manually retire exact completed suites before another run.')
        if args.plan:
            print(json.dumps(dict(status='planned-not-launched', command=command, rawBytes=used, reserveBytes=RESERVE), indent=2))
            return 0
        lock_path = runtime/'state/one-click.lock'
        fd = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, 'w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # Runner requires a new output directory; put its bounded console log
            # beside it, then move the completed log into its result directory.
            log_path = results/('.launcher-'+uuid.uuid4().hex+'.log')
            with log_path.open('xb') as log:
                code = supervise_runner(command, log, log_path, results, config['rawByteCap'], runtime, output)
            output.mkdir(mode=0o700, exist_ok=True)
            log_path.rename(output/'launcher.log')
            report = render_report(output, 'complete' if code == 0 else 'failed',
                                   '' if code == 0 else 'Запуск завершился с ошибкой; сохранены исходные данные и журнал.',
                                   selected_profile=config['defaults']['profile'])
            print(json.dumps(dict(status='complete' if code == 0 else 'failed', report=str(report))))
        if not args.no_open:
            subprocess.run(['/usr/bin/open', str(report)], check=False, timeout=10)
        return code
    except Exception as error:
        print(str(error), file=sys.stderr)
        # A double-click has no Terminal window. Keep one bounded app-owned
        # failure report, with no deletion of prior raw evidence.
        if runtime is not None and not args.plan:
            try:
                error_path = runtime/'state/last-launch-error.html'
                marker = '<!doctype html><!-- VibeWine Bench owned error report v1 -->'
                if error_path.exists():
                    regular(error_path, 65536)
                    if not error_path.read_text().startswith(marker):
                        raise ValueError('unknown error-report target preserved')
                fd = os.open(error_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
                with os.fdopen(fd, 'w') as stream:
                    stream.write(marker+'<meta charset="utf-8"><title>VibeWine Bench</title>'
                                 '<h1>Запуск не завершён</h1><p>'+html.escape(str(error))+'</p>'
                                 '<p>Исходные результаты сохранены. Если состояние завершения неизвестно, новый запуск заблокирован до проверки владельцем.</p>')
                if not args.no_open:
                    subprocess.run(['/usr/bin/open', str(error_path)], check=False, timeout=10)
            except Exception as report_error:
                print('failure report unavailable: '+str(report_error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
