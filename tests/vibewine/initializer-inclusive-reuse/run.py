#!/usr/bin/env python3
"""Offline production-extracted initializer inclusive-frontier CPU regression."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def helper(text):
    start = text.index('  static uint64_t uploadReuseBoundary(')
    end = text.index('\n  }', start) + len('\n  }')
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT)
    args = parser.parse_args()
    root = args.source_dir.resolve()
    transaction = root / 'tests/vibewine/initializer-transaction'
    spec = importlib.util.spec_from_file_location('transaction_runner', transaction / 'run.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    compiler, sandbox = shutil.which('clang++'), shutil.which('sandbox-exec')
    if not compiler or not sandbox:
        parser.error('macOS clang++ and sandbox-exec required; no unsandboxed fallback')
    paths = module.PRODUCTION + module.DEPENDENCIES
    data = {name: (root/name).read_bytes() for name in paths}
    contents = {name: value.decode() for name,value in data.items()}
    inputs = {'transaction/' + name: (transaction/name).read_bytes() for name in module.FIXTURES}
    inputs.update({name:(HERE/name).read_bytes() for name in ('initializer-cases.hpp','ring-cases.hpp','run.py')})
    old_ring = (root/'tests/vibewine/ring-checked/fixture.cpp').read_bytes()
    inputs['ring-checked/fixture.cpp'] = old_ring
    context = contents['src/dxmt/dxmt_context.hpp']
    begin = context.index('inline std::size_t\nalign_forward_adjustment')
    end = context.index('\nenum class PipelineStage',begin)
    strip = module.strip_dependencies
    initializer = '\n'.join(('#include "fixture-prelude.hpp"','namespace dxmt {',context[begin:end],'}',
        strip(contents[paths[0]]),strip(contents[paths[1]]),'#include "fixture-resources.hpp"',
        strip(contents[paths[2]]),'#define private public',strip(contents[paths[3]]),'#undef private',
        '#define malloc fixture_malloc',strip(contents[paths[4]]),'#undef malloc',
        '#include "fixture-main.hpp"','#include "initializer-cases.hpp"'))
    ring = '\n'.join(line for line in contents[paths[2]].splitlines() if line not in
        ('#include "Metal.hpp"','#include "log/log.hpp"','#include "thread.hpp"','#include "util_math.hpp"'))
    ring_fixture = old_ring.decode().split('int main() {',1)[0] + '\nnamespace dxmt { class ResourceInitializer { public:\n' + \
        helper(contents[paths[3]]) + '\n}; }\n#include "ring-cases.hpp"\n'
    print(json.dumps({'scope':'complete production initializer/ring/tracker and actual Metal.hpp; native CPU mocks, no GPU',
        'source_sha256':{name:hashlib.sha256(value).hexdigest() for name,value in data.items()},
        'fixture_sha256':{name:hashlib.sha256(value).hexdigest() for name,value in inputs.items()},
        'generated_sha256':{name:hashlib.sha256(value.encode()).hexdigest() for name,value in
            {'initializer':initializer,'ring':ring_fixture}.items()},'modes':['default','off','on'],
        'strict_generic_fixture':'unchanged; run separately','sanitizers':['address','undefined']},sort_keys=True),flush=True)
    with tempfile.TemporaryDirectory(prefix='dxmt-initializer-inclusive-') as directory:
        scratch = Path(directory).resolve()
        for name,value in data.items():
            target=scratch/name; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(value)
        for name in module.FIXTURES:
            value=inputs['transaction/'+name].decode()
            if name=='fixture-main.hpp':
                assert value.count('int main(){')==1
                value=value.replace('int main(){','void run_existing_transaction_cases(){',1)
            (scratch/name).write_text(value)
        for name in ('initializer-cases.hpp','ring-cases.hpp'):
            (scratch/name).write_bytes(inputs[name])
        (scratch/'initializer.cpp').write_text(initializer)
        (scratch/'ring-under-test.hpp').write_text(ring)
        (scratch/'ring.cpp').write_text(ring_fixture)
        profile='(version 1)(allow default)(deny network*)(deny file-write*)' + \
            '(allow file-write* (subpath '+json.dumps(str(scratch))+'))'
        environment={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
        for mode,flags in (('default',[]),('off',['-DDXMT_INITIALIZER_INCLUSIVE_REUSE=0']),
                           ('on',['-DDXMT_INITIALIZER_INCLUSIVE_REUSE=1'])):
            for kind in ('initializer','ring'):
                executable=scratch/(mode+'-'+kind)
                command=[compiler,'-std=c++20','-O1','-g','-pthread','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',*flags,'-DINCLUSIVE='+str(int(mode=='on')),
                    '-I',str(scratch),'-I',str(scratch/'src/winemetal'),'-I',str(scratch/'src/util'),
                    '-I',str(scratch/'include'),str(scratch/(kind+'.cpp')),'-o',str(executable)]
                module.execute(command,scratch,mode+'-'+kind+'-compile',60)
                module.execute([sandbox,'-p',profile,str(executable)],scratch,mode+'-'+kind,30,environment)
                print('PASS',mode,kind,flush=True)


if __name__=='__main__':
    main()
