#!/usr/bin/env python3
"""Focused native fixture only; never builds a renderer or native bridge."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=HERE.parents[2] / 'source')
parser.add_argument('--sanitizer', choices=['none', 'address', 'thread'], default='none')
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--baseline-archive', type=Path)
parser.add_argument('--mutation', choices=['under-deps-lock', 'skip-persistence', 'capture-task'])
args = parser.parse_args()
if args.baseline and args.mutation:
    parser.error('baseline and mutation are separate controls')
if args.mutation == 'capture-task' and args.sanitizer != 'address':
    parser.error('capture-task control requires --sanitizer address')
source = args.source.resolve()
paths = ['src/dxmt/dxmt_tasks.hpp', 'src/d3d11/d3d11_shader.hpp',
         'src/d3d11/d3d11_pipeline_cache.cpp', 'src/d3d11/d3d11_pipeline.cpp',
         'src/d3d11/cs2_recipe.hpp', 'src/d3d11/cs2_pipeline_archive.hpp']
binding = json.loads((HERE.parent / 'baseline-binding.json').read_text())
if args.baseline:
    if args.baseline_archive is None: parser.error('--baseline requires --baseline-archive PATH')
    archive = args.baseline_archive
    if hashlib.sha256(archive.read_bytes()).hexdigest() != binding['archive_sha256']:
        raise RuntimeError('installed-source archive identity changed')
    texts = {}
    with tarfile.open(archive) as package:
        for path in paths:
            members = [m for m in package.getmembers() if m.isfile() and
                       (m.name == path or m.name.endswith('/' + path))]
            if len(members) != 1:
                raise RuntimeError(f'archive source not unique: {path}')
            texts[path] = package.extractfile(members[0]).read().decode()
            if hashlib.sha256(texts[path].encode()).hexdigest() != binding['compared_files'][path]['archive']:
                raise RuntimeError(f'baseline source identity mismatch: {path}')
else:
    texts = {path: (source / path).read_text() for path in paths}

def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == '{': depth += 1
        if text[index] == '}': depth -= 1
        if depth == 0:
            end = index + 1
            if text[end:end+1] == ';': end += 1
            return text[start:end]
    raise RuntimeError(f'unclosed extracted block: {marker}')

scheduler = re.sub(r'^\s*#\s*(?:include\b[^\n]*|pragma\s+once[^\n]*)\n', '',
                   texts[paths[0]], flags=re.M)
work = block(texts[paths[1]], 'class ThreadpoolWork {')
trait = block(texts[paths[2]], 'template <> struct task_trait<ThreadpoolWork *> {')
pipeline = texts[paths[3]].split('class MTLCompiledComputePipelineImpl', 1)[0]
completion = block(pipeline, '  void SetIsDone(bool state)')
has_post = 'TakePostCompletionWork()' in pipeline
post = block(pipeline, '  std::function<void()> TakePostCompletionWork()') if has_post else ''
if args.mutation == 'under-deps-lock':
    original = '          task_trait.set_done(continuation);'
    if scheduler.count(original) != 1: raise RuntimeError('deps-lock mutation seam changed')
    scheduler = scheduler.replace(original, original + '\n          if (post_completion) { post_completion(); post_completion = {}; }')
if args.mutation == 'skip-persistence':
    original = '        if (post_completion)\n          post_completion();'
    if scheduler.count(original) != 1: raise RuntimeError('post-completion mutation seam changed')
    scheduler = scheduler.replace(original, '        /* deliberately omitted post-completion persistence */')
if args.mutation == 'capture-task':
    original = 'return [pending = std::move(archive_pending_), recipe = recipe_]() mutable {'
    if post.count(original) != 1: raise RuntimeError('payload ownership mutation seam changed')
    post = post.replace(original, 'return [this]() mutable {').replace('pending.save();', 'archive_pending_.save();').replace('cs2recipe::save(recipe);', 'cs2recipe::save(recipe_);')

with tempfile.TemporaryDirectory(prefix='persistence-detach-fixture-') as directory:
    build = Path(directory)
    (build/'production_scheduler.inc').write_text(scheduler)
    (build/'production_work.inc').write_text(work + '\n' + trait)
    (build/'production_graphics_completion.inc').write_text(completion + '\n' + post)
    command = ['clang++', '-std=c++20', '-pthread', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-inconsistent-missing-override',
               f'-DFIXTURE_HAS_POST={int(has_post)}', '-I', str(build),
               str(HERE/'persistence_regression.cpp'), '-o', str(build/'persistence-regression')]
    if args.sanitizer != 'none': command += [f'-fsanitize={args.sanitizer}', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True, timeout=120)
    result = subprocess.run([str(build/'persistence-regression')], capture_output=True, text=True, timeout=35)
    observation = {
        'baseline': args.baseline, 'mutation': args.mutation, 'sanitizer': args.sanitizer,
        'compiler_command': command, 'binary_returncode': result.returncode,
        'stdout': result.stdout, 'stderr': result.stderr,
        'bound_sources': {path: hashlib.sha256(texts[path].encode()).hexdigest() for path in paths},
        'extracted_sha256': {name: hashlib.sha256((build/name).read_bytes()).hexdigest() for name in
                            ['production_scheduler.inc', 'production_work.inc', 'production_graphics_completion.inc']},
    }
    observations = HERE/'observations'
    observations.mkdir(exist_ok=True)
    observation_name = ('baseline' if args.baseline else args.mutation or 'candidate') + '-' + args.sanitizer + '.json'
    (observations/observation_name).write_text(json.dumps(observation, indent=2) + '\n')
    expected_failure = args.baseline or args.mutation
    if expected_failure:
        if result.returncode == 0: raise RuntimeError('negative control was not detected')
        if args.mutation == 'capture-task':
            if 'ERROR: AddressSanitizer: heap-use-after-free' not in result.stderr:
                raise RuntimeError('ownership mutation did not yield expected ASan UAF:\n' + result.stderr)
            print('PASS sensitivity capture-task: AddressSanitizer heap-use-after-free detected')
        elif not result.stderr.startswith('FAIL '):
            raise RuntimeError('negative control failed outside a fixture oracle:\n' + result.stderr)
        else:
            print('PASS sensitivity ' + (args.mutation or 'bound-installed-baseline') + ': ' + result.stderr.strip())
    else:
        print(result.stdout, end='')
        if result.returncode: raise RuntimeError('fixture failed:\n' + result.stderr)
for path in paths:
    print('SOURCE_SHA256', hashlib.sha256(texts[path].encode()).hexdigest(), path)
print('BASELINE_ARCHIVE_SHA256', binding['archive_sha256'])
print('BASELINE_PUBLIC_HEAD', binding['public_head'])
