#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise real generated Darwin sed pipelines; never run configure or Wine."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = 'ae1d0e73b4384130dd6db33c49c28be02999b500'


def old(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT,
                                   text=True, timeout=30)


def branches(text):
    return [line for line in text.splitlines()
            if 'dylib) ac_cv_lib_soname_' in line and '$OTOOL -L conftest' in line]


def extract(line, rows):
    # Keep the actual backtick shell context (including its escaping), replacing
    # only otool's output with fixture stdin. This executes system sed/grep only.
    pipeline = line.split(' | ', 1)[1].split('`', 1)[0]
    shell = 'result=`/bin/cat | ' + pipeline + '`; printf "%s" "$result"'
    return subprocess.check_output(['/bin/sh', '-c', shell], input=rows, text=True,
                                   env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'}, timeout=5)


def row(name):
    return '\t' + name + ' (compatibility version 27.0.0, current version 27.6.0)\n'


def main():
    current = (ROOT / 'configure').read_text()
    previous = old('configure')
    lines = branches(current)
    assert len(lines) == len(branches(previous)) == 27
    freetype = next(line for line in lines if 'soname_freetype=' in line)
    former = next(line for line in branches(previous) if 'soname_freetype=' in line)
    cases = {
        'bare install name': (row('libfreetype.dylib'), 'libfreetype.dylib'),
        'absolute versioned': (row('/opt/local/lib/libfreetype.6.dylib'), 'libfreetype.6.dylib'),
        'rpath': (row('@rpath/libfreetype.6.dylib'), 'libfreetype.6.dylib'),
        'loader path': (row('@loader_path/../Frameworks/libfreetype.dylib'), 'libfreetype.dylib'),
        'spaces in directory': (row('/Applications/Some App/libfreetype.dylib'), 'libfreetype.dylib'),
        'trailing whitespace': (row('libfreetype.dylib').rstrip() + ' \t\n', 'libfreetype.dylib'),
        'first valid match': ('conftest:\n' + row('libwrong.dylib') + row('notlibfreetype.dylib') + row('libfreetype.dylib.backup') + row('libfreetype.6.dylib') + row('libfreetype.7.dylib'), 'libfreetype.6.dylib'),
        'wrong prefix': (row('notlibfreetype.dylib'), ''),
        'wrong suffix': (row('libfreetype.dylib.backup'), ''),
        'substring only': (row('libfreetype_extra.dylib'), ''),
        'header': ('/tmp/libfreetype.dylib:\n', ''),
        'indented header': ('\t/tmp/libfreetype.dylib:\n', ''),
        'no dependency indentation': (row('libfreetype.dylib').lstrip(), ''),
        'unrecognized metadata': ('\tlibfreetype.dylib (unrelated metadata)\n', ''),
        'empty': ('', ''),
    }
    for name, (data, expected) in cases.items():
        assert extract(freetype, data) == expected, name
    assert extract(former, row('libfreetype.dylib')) != 'libfreetype.dylib', 'regression discriminator'
    assert extract(former, row('/opt/local/lib/libfreetype.6.dylib')) == 'libfreetype.6.dylib'

    macro = next(line for line in (ROOT / 'aclocal.m4').read_text().splitlines()
                 if 'dylib) AS_VAR_SET' in line)
    template = macro.split(' | ', 1)[1].split('`', 1)[0]
    for before, after in zip(branches(previous), lines):
        pattern = before.split('sed -e "s/^.*\\/\\(', 1)[1].split(r'\.[0-9A-Za-z.]*dylib', 1)[0]
        expected = template.replace('ac_lib_pattern', pattern).replace('[[', '[').replace(']]', ']')
        assert after.split(' | ', 1)[1].split('`', 1)[0] == expected
    for library, basename in [('gnutls', 'libgnutls-deb0.30.dylib'), ('SDL2', 'libSDL2-2.0.0.dylib'), ('EGL', 'libEGL.1.dylib')]:
        line = next(line for line in lines if 'soname_' + library + '=' in line)
        assert extract(line, row('@rpath/' + basename)) == basename, library
        assert extract(line, row('not' + basename)) == '', library + ' pattern boundary'
    # The generated-file delta is exactly the 27 Darwin branches. ELF, DLL,
    # compiler probes, licensing and every other configure byte remain intact.
    def without_darwin(text):
        return '\n'.join(line for line in text.split('\n')
                         if not ('dylib) ac_cv_lib_soname_' in line and '$OTOOL -L conftest' in line))
    assert without_darwin(current) == without_darwin(previous), 'non-Darwin generated change'
    previous_macro = old('aclocal.m4')
    old_line = next(line for line in previous_macro.splitlines() if 'dylib) AS_VAR_SET' in line)
    assert (ROOT / 'aclocal.m4').read_text().replace(macro, old_line) == previous_macro
    print('PASS: 15 Darwin fixtures, old-fails/new-passes, 27 synchronized expansions, 3 other-library controls; DLL/ELF and all other bytes unchanged')


if __name__ == '__main__':
    main()
