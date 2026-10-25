#!/usr/bin/env python3
"""Compile actual Error caller TUs with an existing matched cross-toolchain database.

Rebind source paths to this checkout, retain only read access to generated build
headers, remove object/dependency output options, use -fsyntax-only. No build writes.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import re
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('database', type=Path, nargs='+')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[2]
# Header callers not textually mentioning Error are included explicitly.
callers = set()
for source in (root / 'src').rglob('*.cpp'):
    if re.search(r'WMT::(?:Reference<WMT::)?Error|cs2_pipeline_archive', source.read_text()):
        callers.add(source.relative_to(root).as_posix())
callers.update(['src/d3d11/d3d11_pipeline_ts.cpp', 'src/d3d11/d3d11_pipeline_gs.cpp'])
with tempfile.TemporaryDirectory(prefix='.tmp-compile-', dir=here) as directory:
    for database in args.database:
        commands = json.loads(database.read_text())
        selected = []
        for entry in commands:
            old_cwd = Path(entry['directory'])
            old_source = (old_cwd / entry['file']).resolve()
            if '/src/' not in str(old_source):
                continue
            suffix = 'src/' + str(old_source).split('/src/', 1)[1]
            if suffix not in callers:
                continue
            old_root = Path(str(old_source)[:-len(suffix)])
            def rebind(path):
                resolved = (old_cwd / path).resolve()
                try:
                    return str(root / resolved.relative_to(old_root))
                except ValueError:
                    return str(resolved)
            command = shlex.split(entry['command'])
            out = [command[0]]
            i = 1
            while i < len(command):
                arg = command[i]
                if arg in ['-o', '-MF', '-MQ', '-MT']:
                    i += 2; continue
                if arg in ['-MD', '-MMD', '-c']:
                    i += 1; continue
                if arg.startswith('-I'):
                    out.append('-I' + rebind(arg[2:]))
                elif arg == entry['file']:
                    out.append(str(root / suffix))
                elif arg.startswith('-fmacro-prefix-map='):
                    pass
                else:
                    out.append(arg)
                i += 1
            out.append('-fsyntax-only')
            selected.append((suffix, out))
        missing = callers - {file for file, _ in selected}
        assert all(file.startswith('src/d3d12/') for file in missing), (database, missing)
        # Existing game builds disable D3D12. Its Meson target shares these same
        # dependencies; add its local include directory and compile real sources.
        template = next(command for file, command in selected if file == 'src/d3d11/d3d11_pipeline.cpp')
        for file in sorted(missing):
            selected.append((file, [str(root / file) if arg == str(root / 'src/d3d11/d3d11_pipeline.cpp') else arg
                for arg in template] + ['-I' + str(root / 'src/d3d12')]))
        assert {file for file, _ in selected} == callers, (database, missing)
        def compile_one(item):
            file, command = item
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (file, result.returncode, result.stderr[-16000:])
            return file
        with ThreadPoolExecutor(max_workers=2) as pool:
            for file in pool.map(compile_one, selected):
                print('PASS', database.parent.name, file, flush=True)
        print('PASS all', len(selected), 'actual production Error caller translation units', database)
