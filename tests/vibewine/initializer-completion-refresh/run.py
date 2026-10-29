#!/usr/bin/env python3
"""Production initializer completion-opportunity and lifetime regression."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT)
    parser.add_argument('--source-ref')
    parser.add_argument('--mode', choices=('default', 'off', 'on'), default='on')
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    parser.add_argument('--case', choices=('all', 'latest', 'pending', 'bounded'), default='all')
    parser.add_argument('--mutation', choices=('no-refresh', 'always-poll', 'unsafe-frontier'))
    args = parser.parse_args()
    root = args.source_dir.resolve()
    transaction = ROOT / 'tests/vibewine/initializer-transaction'
    spec = importlib.util.spec_from_file_location('transaction_runner', transaction / 'run.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    compiler, sandbox = shutil.which('clang++'), shutil.which('sandbox-exec')
    if not compiler or not sandbox:
        parser.error('macOS clang++ and sandbox-exec required; no unsandboxed fallback')
    resolved = None
    if args.source_ref:
        resolved = subprocess.run(['git', '-C', str(root), 'rev-parse', '--verify', args.source_ref+'^{commit}'],
                                  capture_output=True, text=True, check=True, timeout=10).stdout.strip()
    paths = module.PRODUCTION + module.DEPENDENCIES
    data = {name: subprocess.run(['git', '-C', str(root), 'show', resolved+':'+name],
                               capture_output=True, check=True, timeout=10).stdout if resolved else
            (root/name).read_bytes() for name in paths}
    contents = {name: value.decode() for name, value in data.items()}
    inputs = {'transaction/'+name: (transaction/name).read_bytes() for name in module.FIXTURES}
    inputs['transaction/run.py'] = (transaction/'run.py').read_bytes()
    inputs.update({name: (HERE/name).read_bytes() for name in ('cases.hpp', 'run.py')})
    cpp = contents['src/dxmt/dxmt_resource_initializer.cpp']
    if args.mutation == 'no-refresh':
        cpp = replace_once(cpp, 'if (current_seq_id_ > 1 && cached_coherent_seq_id < current_seq_id_ - 1 &&',
                           'if (false && current_seq_id_ > 1 && cached_coherent_seq_id < current_seq_id_ - 1 &&')
    elif args.mutation == 'always-poll':
        cpp = replace_once(cpp, 'upload_poll_seq_id_ != current_seq_id_) {', 'true) {')
    elif args.mutation == 'unsafe-frontier':
        old = 'uploadReuseBoundary(current_seq_id_, cached_coherent_seq_id)'
        assert cpp.count(old) == 2
        cpp = cpp.replace(old, 'current_seq_id_')
    contents['src/dxmt/dxmt_resource_initializer.cpp'] = cpp
    prelude = inputs['transaction/fixture-prelude.hpp'].decode()
    prelude = replace_once(prelude, 'struct Observation {', '''struct DelayedRead { Payload payload; uint64_t hash; };
struct DelayedBatch { uint64_t signal; std::vector<DelayedRead> reads; };
struct Observation {
 bool delay=false,capture_payload=false;unsigned event_reads=0;
 std::vector<DelayedBatch> delayed;
 std::unordered_map<obj_handle_t,std::vector<unsigned char>> backing;''')
    prelude = replace_once(prelude, 'inline Observation obs;', '''inline Observation obs;
inline uint64_t payload_hash(const Payload &p) {
 const auto &bytes=obs.backing.at(p.src);assert(p.offset<=bytes.size()&&p.length<=bytes.size()-p.offset);
 uint64_t hash=1469598103934665603ull;
 for(size_t i=0;i<p.length;++i)hash=(hash^bytes[p.offset+i])*1099511628211ull;
 return hash;
}''')
    prelude = replace_once(prelude, '++obs.commits;obs.signal=batch.signal;obs.clears+=batch.clears;',
                          '''++obs.commits;if(!obs.delay)obs.signal=batch.signal;obs.clears+=batch.clears;
 if(obs.delay) {
  DelayedBatch pending{batch.signal,{}};
  if(obs.capture_payload)for(auto &p:batch.commands)if(p.src)pending.reads.push_back({p,payload_hash(p)});
  obs.delayed.push_back(std::move(pending));
 }''')
    prelude = replace_once(prelude, 'assert(event);return obs.signal;', 'assert(event);++obs.event_reads;return obs.signal;')
    prelude = replace_once(prelude, 'obs.capacities[h]=info->length;return h;',
                          'obs.capacities[h]=info->length;if(obs.capture_payload)obs.backing[h].resize(info->length);return h;')
    prelude = replace_once(prelude, '++obs.updates;if(obs.capture_updates',
                          '++obs.updates;if(obs.capture_payload)std::memcpy(obs.backing.at(buffer).data()+offset,data.ptr,length);if(obs.capture_updates')
    main_cases = replace_once(inputs['transaction/fixture-main.hpp'].decode(), 'int main(){',
                              'void run_existing_transaction_cases(){')
    context = contents['src/dxmt/dxmt_context.hpp']
    begin = context.index('inline std::size_t\nalign_forward_adjustment')
    end = context.index('\nenum class PipelineStage', begin)
    strip = module.strip_dependencies
    generated = '\n'.join(('#include "fixture-prelude.hpp"', 'namespace dxmt {', context[begin:end], '}',
        strip(contents[paths[0]]), strip(contents[paths[1]]), '#include "fixture-resources.hpp"',
        strip(contents[paths[2]]), '#define private public', strip(contents[paths[3]]), '#undef private',
        '#define malloc fixture_malloc', strip(contents[paths[4]]), '#undef malloc',
        '#include "fixture-main.hpp"', '#include "cases.hpp"'))
    print(json.dumps({'scope': 'complete production initializer/ring/tracker; delayed source-byte CPU mock, no GPU',
        'source_ref': resolved, 'mode': args.mode, 'arch': args.arch, 'mutation': args.mutation, 'case': args.case,
        'source_sha256': {name: hashlib.sha256(value).hexdigest() for name, value in data.items()},
        'fixture_sha256': {name: hashlib.sha256(value).hexdigest() for name, value in inputs.items()},
        'generated_sha256': hashlib.sha256(generated.encode()).hexdigest(),
        'mock_sha256': hashlib.sha256(prelude.encode()).hexdigest(),
        'sanitizers': ['address', 'undefined']}, sort_keys=True), flush=True)
    with tempfile.TemporaryDirectory(prefix='dxmt-initializer-refresh-') as directory:
        scratch = Path(directory).resolve()
        for name, value in data.items():
            target = scratch/name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(value)
        (scratch/'fixture-prelude.hpp').write_text(prelude)
        (scratch/'fixture-main.hpp').write_text(main_cases)
        (scratch/'fixture-resources.hpp').write_bytes(inputs['transaction/fixture-resources.hpp'])
        (scratch/'cases.hpp').write_bytes(inputs['cases.hpp'])
        (scratch/'production.cpp').write_text(generated)
        flags = [] if args.mode == 'default' else ['-DDXMT_INITIALIZER_INCLUSIVE_REUSE='+str(int(args.mode == 'on'))]
        command = [compiler, '-std=c++20', '-O1', '-g', '-arch', args.arch, '-fsanitize=address,undefined',
            '-fno-sanitize-recover=all', *flags, '-DINCLUSIVE='+str(int(args.mode == 'on')),
            '-DCASE='+str(('all', 'latest', 'pending', 'bounded').index(args.case)),
            '-I', str(scratch), '-I', str(scratch/'src/winemetal'), '-I', str(scratch/'src/util'),
            '-I', str(scratch/'include'), str(scratch/'production.cpp'), '-o', str(scratch/'fixture')]
        module.execute(command, scratch, 'compile', 60)
        profile = '(version 1)(allow default)(deny network*)(deny file-write*)' + \
                  '(allow file-write* (subpath '+json.dumps(str(scratch))+'))'
        module.execute([sandbox, '-p', profile, str(scratch/'fixture')], scratch, 'fixture', 45,
            {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})


if __name__ == '__main__':
    main()
