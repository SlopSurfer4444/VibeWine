#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Extract bounded, licensed Wine test bytecode. No compiler or GPU is invoked."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

CASES = [('blend-vs', 'test_blend', 'vs_code'),
         ('blend-ps', 'test_blend', 'ps_code'),
         ('mrt-vs', 'test_shader_stage_input_output_matching', 'vs_code'),
         ('mrt-ps', 'test_shader_stage_input_output_matching', 'ps_code'),
         ('noop-cs', 'test_device_context_state', 'simple_cs')]

def extract(source):
    snapshot=source if isinstance(source,bytes) else source.read_bytes()
    text = snapshot.decode()
    result = {}
    for name, function, array in CASES:
        match = re.search(r'^static void ' + function + r'\(void\)\n\{(.*?)^\}', text, re.S | re.M)
        assert match, function
        found = re.search(r'static const DWORD ' + array + r'\[\]\s*=\s*\{(.*?)\n    \};', match[1], re.S)
        assert found, array
        payload = re.sub(r'#if 0.*?#endif', '', found[1], flags=re.S)
        words = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', payload)]
        data = struct.pack('<' + 'I'*len(words), *words)
        assert data[:4] == b'DXBC' and struct.unpack_from('<I', data, 24)[0] == len(data)
        assert len(data) <= 4096
        result[name] = (data, function, array)
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wine-source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    source_bytes=a.wine_source.read_bytes()
    manifest = {'license': 'LGPL-2.1-or-later (Wine d3d11 tests)',
                'source': str(a.wine_source.resolve()),
                'source_sha256': hashlib.sha256(source_bytes).hexdigest(), 'entries': []}
    for name, (data, function, array) in extract(source_bytes).items():
        path = a.output / (name + '.dxbc')
        assert not path.exists() or path.read_bytes() == data, 'refusing to replace different input'
        path.write_bytes(data)
        manifest['entries'].append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                    'function': function, 'array': array})
    (a.output / 'corpus.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'status': 'PREPARED', 'entries': len(manifest['entries']), 'compiler': False, 'gpu': False}))

if __name__ == '__main__':
    main()
