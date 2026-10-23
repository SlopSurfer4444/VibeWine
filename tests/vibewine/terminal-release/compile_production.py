#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile production TU with exact Meson headers/flags, without fixture seam."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative-ref', default='12a7ebf1ddf0689d1f054ccb02b1e30d4c5fe6e5',
                        help='Guard revision missing the production API declaration')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    relative = 'src/winemetal/winemetal_thunks.c'
    production = root / relative
    entries = json.loads(args.compile_commands.read_text())
    matches = [entry for entry in entries
               if (Path(entry['directory']) / entry['file']).resolve() == production]
    if len(matches) != 1:
        raise RuntimeError('expected one production thunk compile command')
    entry = matches[0]
    original = entry.get('arguments') or shlex.split(entry['command'])
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base = []
    iterator = iter(original)
    for token in iterator:
        if token in ['-o', '-MF', '-MQ', '-MT']:
            next(iterator)
        elif token not in ['-MD', '-MMD', '-MP']:
            base.append(token)
    # The fixture include directory must never appear in this check.
    if any('tests/vibewine/terminal-release' in token for token in base):
        raise RuntimeError('fixture path in production compilation')
    old = subprocess.check_output(['git', 'show', args.negative_ref + ':' + relative],
                                  cwd=root)
    baseline = output / 'missing-declaration.c'
    baseline.write_bytes(old)
    report = {'scope': 'exact Meson production TU compilation; real headers, no fixture seam',
              'source_sha256': hashlib.sha256(production.read_bytes()).hexdigest(),
              'negative_ref': args.negative_ref, 'cases': []}
    for name, source in [('negative', baseline), ('candidate', production)]:
        command = [str(source) if (Path(entry['directory']) / token).resolve() == production
                   else token for token in base]
        command += ['-Werror=implicit-function-declaration', '-o', str(output / (name + '.obj'))]
        completed = subprocess.run(command, cwd=entry['directory'], capture_output=True, timeout=30)
        diagnostics = completed.stdout + completed.stderr
        if len(diagnostics) > 65536:
            raise RuntimeError('compiler diagnostic cap exceeded')
        (output / (name + '.log')).write_bytes(diagnostics)
        if name == 'negative':
            if completed.returncode == 0 or b"undeclared function 'RtlDllShutdownInProgress'" not in diagnostics:
                raise RuntimeError('negative control did not expose the missing declaration')
        elif completed.returncode:
            raise RuntimeError(diagnostics.decode(errors='replace'))
        report['cases'].append({'name': name, 'returncode': completed.returncode, 'command': command})
    objdump = Path(base[0]).parent / 'llvm-objdump'
    relocations = subprocess.check_output([str(objdump), '-r', str(output / 'candidate.obj')], timeout=15)
    if b'__imp_RtlDllShutdownInProgress' not in relocations:
        raise RuntimeError('candidate does not reference the direct NTDLL import')
    report['direct_import_relocation'] = '__imp_RtlDllShutdownInProgress'
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
