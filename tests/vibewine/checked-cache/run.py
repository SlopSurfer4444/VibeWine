#!/usr/bin/env python3
"""Sacrificial native SQLite cache tests; no live cache, Wine runtime or GPU."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]
native = root / 'src/winemetal/unix/cache.c'
thunks = (root / 'src/winemetal/winemetal_thunks.c').read_text()
checked = thunks[thunks.index('\nWINEMETAL_API obj_handle_t\nCache_alloc_init_checked'):]
tables = (root / 'src/winemetal/unix/winemetal_unix.c').read_text()
for name in ['__wine_unix_call_funcs', '__wine_unix_call_wow64_funcs']:
    body = tables.split(name + '[] = {', 1)[1].split('};', 1)[0]
    entries = [entry.strip() for entry in body.split(',') if entry.strip()]
    assert entries[115:119] == ['&_CacheReader_alloc_init', '&_CacheReader_get',
                                '&_CacheWriter_alloc_init', '&_CacheWriter_set']
    assert entries[146:149] == ['&_Cache_alloc_init_checked', '&_CacheReader_get_checked',
                                '&_CacheWriter_set_checked']

with tempfile.TemporaryDirectory(prefix='dxmt-checked-cache-') as directory:
    work = Path(directory)
    data = work / 'sacrificial'; data.mkdir()
    source = work / 'fixture.m'
    source.write_text((here / 'fixture.m.in').read_text()
                      .replace('CACHE_SOURCE', str(native))
                      .replace('// CHECKED_PE_THUNKS', checked))
    binary = work / 'fixture'
    command = [os.environ.get('CC', 'clang'), '-std=c2x', '-fno-objc-arc', '-fblocks',
               '-O1', '-g', '-fsanitize=address,undefined', '-framework', 'Foundation', '-lsqlite3',
               str(source), '-o', str(binary)]
    subprocess.run(command, check=True, timeout=30)
    subprocess.run([str(binary), str(data)], check=True, timeout=15,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
print('PASS checked cache opcode/layout integration: legacy115-118; additive146-148 in both native tables')
