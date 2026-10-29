#!/usr/bin/env python3
"""Plan or explicitly install the user-owned, finite VibeWine Bench app.

No downloads, Wine execution, control registry, permissions, or game changes.
The coordinator must settle the private runtime before its same-volume move.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import sys
import tempfile
import uuid

OWNER = 'vibewine-bench-v1'


def move_no_replace(source, destination):
    """Darwin atomic directory publication; a concurrent target is preserved."""
    if sys.platform != 'darwin':
        raise ValueError('atomic no-replace directory publication requires Darwin renamex_np')
    library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    rename = getattr(library, 'renamex_np', None)
    if rename is None:
        raise ValueError('Darwin renamex_np unavailable; no overwrite fallback')
    rename.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    # macOS SDK sys/stdio.h: RENAME_EXCL = 0x00000004.
    if rename(os.fsencode(source), os.fsencode(destination), 0x00000004) != 0:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def safe_file(path, cap=32*1024*1024):
    path = Path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > cap:
        raise ValueError('regular single-link bounded file required: ' + str(path))
    return path


def safe_parents(path):
    for item in reversed((Path(path), *Path(path).parents)):
        if not item.exists() and not item.is_symlink():
            continue
        info = item.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid not in (0, os.getuid()) or info.st_mode & 0o022:
            raise ValueError('unsafe destination ancestor: ' + str(item))


def write_json(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def collect(source, guest, controller):
    benchmarks = source/'tools/benchmarks'
    files = {
        'tools/benchmarks/run.py': benchmarks/'run.py',
        'tools/benchmarks/analyze.py': benchmarks/'analyze.py',
        'tools/benchmarks/launch.py': benchmarks/'launch.py',
        'source/install.py': benchmarks/'install.py',
        'source/README.md': benchmarks/'README.md',
        'source/guest/main.cpp': benchmarks/'guest/main.cpp',
        'source/guest/PROVENANCE.md': benchmarks/'guest/PROVENANCE.md',
        'licenses/DXMT-LICENSE': source/'components/dxmt/LICENSE',
        'licenses/DXMT-COPYING.LIB': source/'components/dxmt/COPYING.LIB',
        'bin/dx11-benchmark.exe': guest,
        'bin/cs2trace': controller,
    }
    # Keep the maintained analyzer module closure; exclude tests/collectors.
    for name in ('analyze', 'wave7_analysis', 'detail_analysis', 'compiler_attribution',
                 'presentation_analysis', 'helper_compile_analysis', 'visibility_analysis', 'completion_analysis'):
        files['tools/telemetry/'+name+'.py'] = source/'tools/telemetry'/ (name+'.py')
    for name in ('trace_controller.cpp', 'trace_core.hpp', 'probe_semantics.hpp'):
        files['source/telemetry/'+name] = source/'tools/telemetry'/name
    receipt = guest.parent/'build-receipt.json'
    if receipt.is_file():
        files['source/guest/build-receipt.json'] = receipt
    return {relative: safe_file(path) for relative, path in files.items()}


def known_install(destination, app, intent):
    marker = destination/'install-receipt.json'
    safe_file(marker, 1024*1024)
    receipt = json.loads(marker.read_text())
    if receipt.get('owner') != OWNER or receipt.get('intent') != intent:
        raise ValueError('existing destination is unknown or has different install inputs')
    for base, key in ((destination, 'files'), (app, 'appFiles')):
        if base.is_symlink() or not base.is_dir():
            raise ValueError('installed target is missing or a symlink')
        for name, expected in receipt[key].items():
            if sha(safe_file(base/name)) != expected:
                raise ValueError('installed payload changed: '+str(base/name))
    return receipt


def install(args):
    source = args.source_root.expanduser().absolute()
    runtime = args.runtime_dir.expanduser().absolute()
    destination = args.destination.expanduser().absolute()
    app = (args.app or Path.home()/'Applications/VibeWine Bench.app').expanduser().absolute()
    guest, controller = args.guest.expanduser().absolute(), args.controller.expanduser().absolute()
    safe_parents(destination.parent)
    safe_parents(app.parent)
    files = collect(source, guest, controller)
    intent = dict(sourceRoot=str(source), runtimeSource=str(runtime), destination=str(destination), app=str(app),
                  payload={name: sha(path) for name, path in files.items()})
    if destination.exists() or destination.is_symlink():
        receipt = known_install(destination, app, intent)
        return dict(status='already-installed-verified', destination=str(destination), app=str(app), owner=receipt['owner'])
    if app.exists() or app.is_symlink():
        raise ValueError('app path already exists; refusing overwrite')
    safe_parents(runtime)
    for name in ('launcher.py', 'wine', 'sandbox.sb', 'canary.py', 'runtime-pins.json', 'canary-control/denied-read.txt'):
        safe_file(runtime/name, 2*1024*1024)
    if not (runtime/'state/prefix').is_dir() or (runtime/'state/prefix').is_symlink():
        raise ValueError('existing private runtime prefix required')
    runtime_pins = json.loads((runtime/'runtime-pins.json').read_text())
    module_suffixes = ('/x86_64-windows/d3d11.dll', '/x86_64-windows/dxgi.dll',
                       '/x86_64-windows/winemetal.dll', '/x86_64-unix/winemetal.so')
    modules = [row['path'] for row in runtime_pins['core_modules'] if row['path'].endswith(module_suffixes)]
    if len(modules) != 4:
        raise ValueError('four exact matched graphics module pins required')
    plan = dict(status='planned-not-installed', owner=OWNER, destination=str(destination), app=str(app),
                runtimeMove=dict(source=str(runtime), destination=str(destination/'runtime'), sameVolumeRequired=True),
                payloadFiles=len(files), payloadBytes=sum(p.stat().st_size for p in files.values()),
                defaultRun=dict(scenes=6, duration=10, warmup=1, vsync=1, width=1280, height=720,
                                profile='presentation-waits'),
                externalEngineModules=modules, nativeExecution=False,
                retention=dict(rawByteCap=1024**3, rawTTLdays=7, reportTTLdays=30,
                               cleanup='manual exact-owned cleanup after all writers settle; active code/prefix/cache held'))
    if not args.apply:
        return plan
    if not args.runtime_quiescent:
        raise ValueError('--apply requires coordinator assertion --runtime-quiescent; installer never kills services')
    destination.parent.mkdir(parents=True, exist_ok=True)
    app.parent.mkdir(parents=True, exist_ok=True)
    if runtime.stat().st_dev != destination.parent.stat().st_dev:
        raise ValueError('runtime adoption requires same-volume rename; refusing implicit copy')
    stage = Path(tempfile.mkdtemp(prefix='.VibeWineBench-stage-', dir=destination.parent))
    app_stage = Path(tempfile.mkdtemp(prefix='.VibeWineBench-app-stage-', dir=app.parent))
    moved = published = False
    try:
        for relative, original in files.items():
            target = stage/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            target.chmod(0o700 if relative.startswith('bin/') else 0o600)
            if sha(target) != intent['payload'][relative]:
                raise ValueError('payload copy changed')
        config = dict(schema=1, owner=OWNER, support=str(destination), runtime=str(destination/'runtime'),
                      python=sys.executable, modules=modules,
                      defaults=dict(duration=10, warmup=1, vsync=1, width=1280, height=720,
                                    profile='presentation-waits'),
                      rawByteCap=1024**3, rawTTLdays=7, reportTTLdays=30)
        write_json(stage/'config.json', config)
        macos = app_stage/'Contents/MacOS'
        macos.mkdir(parents=True)
        # Shell quoting supports spaces and quotes in user-supplied paths.
        import shlex
        command = '#!/bin/sh\nexec '+shlex.join([sys.executable, '-B', str(destination/'tools/benchmarks/launch.py'),
                                               '--config', str(destination/'config.json')])+'\n'
        (macos/'VibeWine Bench').write_text(command)
        (macos/'VibeWine Bench').chmod(0o700)
        with (app_stage/'Contents/Info.plist').open('xb') as stream:
            plistlib.dump(dict(CFBundleName='VibeWine Bench', CFBundleDisplayName='VibeWine Bench',
                              CFBundleIdentifier='local.vibewine.bench', CFBundleExecutable='VibeWine Bench',
                              CFBundlePackageType='APPL', CFBundleVersion='1', LSUIElement=True), stream)
        owned = {name: sha(stage/name) for name in files}
        owned['config.json'] = sha(stage/'config.json')
        for name in ('launcher.py', 'wine', 'sandbox.sb', 'canary.py', 'runtime-pins.json', 'canary-control/denied-read.txt'):
            owned['runtime/'+name] = sha(runtime/name)
        app_files = {str(p.relative_to(app_stage)): sha(p) for p in app_stage.rglob('*') if p.is_file()}
        write_json(stage/'install-receipt.json', dict(schema=1, owner=OWNER, installId=str(uuid.uuid4()),
                    intent=intent, files=owned, appFiles=app_files, runtimeAdopted=True,
                    retention=plan['retention'], nativeExecutedByInstaller=False))
        # Coordinator has settled all prefix children. No binaries are copied.
        move_no_replace(runtime, stage/'runtime')
        moved = True
        if destination.exists() or app.exists():
            raise ValueError('destination appeared during staging; refusing overwrite')
        move_no_replace(stage, destination)
        published = True
        move_no_replace(app_stage, app)
        known_install(destination, app, intent)
        return dict(status='installed-files-verified-not-launched', app=str(app), destination=str(destination),
                    runtime=str(destination/'runtime'), nativeExecution=False)
    except Exception:
        if moved and not published and not runtime.exists():
            move_no_replace(stage/'runtime', runtime)
        # After publication retain the installed payload for explicit recovery.
        # Unpublished staging contains only this invocation's copied payload.
        raise
    finally:
        # A runtime that could not be restored remains held, never rmtree'd.
        if not published and stage.exists() and not (stage/'runtime').exists():
            shutil.rmtree(stage)
        if app_stage.exists():
            shutil.rmtree(app_stage)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime-dir', 'guest', 'controller', 'source-root', 'destination'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--app', type=Path, help='default ~/Applications/VibeWine Bench.app')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--runtime-quiescent', action='store_true')
    args = parser.parse_args(argv)
    try:
        print(json.dumps(install(args), indent=2))
        return 0
    except Exception as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
