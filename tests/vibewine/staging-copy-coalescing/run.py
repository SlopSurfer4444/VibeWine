#!/usr/bin/env python3
"""Compile the full production copy method; native/context boundary is counted."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
PATH='src/d3d11/d3d11_context_impl.cpp'
BASE='a21579fb2a03aef5fbe93d7bcba1ea882e63a192'

def method(text):
    start=text.index('  void\n  CopyTextureBitcast(TextureCopyCommand &&cmd) {')
    cursor=text.index('{',start)+1
    depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}')
        cursor+=1
    return text[start:cursor]

def replace(text,old,new):
    if text.count(old)!=1: raise RuntimeError(f'Unbound mutation: {old!r}: {text.count(old)}')
    return text.replace(old,new)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    parser.add_argument('--source-dir',type=Path,default=ROOT)
    args=parser.parse_args();root=args.source_dir
    production=(root/PATH).read_text();current=method(production)
    old=method(subprocess.check_output(['git','-C',str(root),'show',BASE+':'+PATH],text=True))
    variants=[('candidate',current,[],True),('old-counts',old,['baseline'],True),('old-target',old,[],False)]
    mutations=[
      ('disable-coalescing','const auto rows_per_copy = contiguous_rows ? cmd.SrcSize.height : 1;','const auto rows_per_copy = contiguous_rows ? 1 : 1;'),
      ('ignore-distinct-buffer','src->buffer().handle != dst->buffer().handle','true'),
      ('ignore-row-pitches','row_bytes == src_->bytesPerRow && row_bytes == dst_->bytesPerRow','true'),
      ('ignore-format','cmd.Src.Format == cmd.Dst.Format','true'),
      ('ignore-texel-size','cmd.SrcFormat.BytesPerTexel == cmd.DstFormat.BytesPerTexel','true'),
      ('ignore-x-origin','cmd.SrcOrigin.x == 0 && cmd.DstOrigin.x == 0','true'),
      ('ignore-image-extents','cmd.SrcSize.height <= src_->bytesPerImage / row_bytes &&\n                                       cmd.SrcSize.height <= dst_->bytesPerImage / row_bytes','true'),
      ('copy-one-row','cmdcp.copy_length = row_bytes * rows_per_copy;','cmdcp.copy_length = row_bytes;'),
      ('omit-source-use','        UseCopySource(staging_src);','        /* missing source ownership protocol */'),
      ('omit-promote-flush','        promote_flush = true;','        /* missing staging flush promotion */'),
      ('omit-src-suballocation','cmdcp.src_offset = src_sub_offset + src_offset;','cmdcp.src_offset = src_offset;'),
    ]
    for name,before,after in mutations:
        # The full method contains other source uses/flushes; change only the
        # first staging-to-staging site, without altering any production file.
        if name in ('omit-source-use','omit-promote-flush'):
            changed=current.replace(before,after,1)
            if changed==current: raise RuntimeError('Unbound owner mutation')
        else: changed=replace(current,before,after)
        variants.append((name,changed,[],False))
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-staging-copy-') as directory:
        scratch=Path(directory)
        for name,body,arguments,accepted in variants:
            (scratch/'method-under-test.hpp').write_text(body)
            binary=scratch/name
            command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror',
                     '-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined',
                     '-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                     '-I'+str(HERE),'-I'+str(scratch),'-I'+str(root/'src/winemetal'),'-I'+str(root/'src/util'),
                     str(HERE/'fixture.cpp'),'-o',str(binary)]
            if args.arch=='x86_64':command+=['-arch','x86_64']
            subprocess.run(command,check=True,timeout=90)
            run=[str(binary),*arguments]
            if args.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
            outcome=subprocess.run(run,capture_output=True,text=True,timeout=30)
            output=outcome.stdout+outcome.stderr
            if accepted:
                assert outcome.returncode==0,(name,outcome.returncode,output)
            else:
                assert outcome.returncode==1 and 'behavioral assertion:' in output,(name,outcome.returncode,output)
                assert 'AddressSanitizer' not in output and 'runtime error:' not in output,(name,output)
            results.append(dict(variant=name,accepted=accepted,exit=outcome.returncode,output=output.strip()))
    paths=[PATH,'src/winemetal/winemetal.h','src/winemetal/Metal.hpp','src/util/rc/util_rc_ptr.hpp',
           'tests/vibewine/staging-copy-coalescing/mocks.hpp','tests/vibewine/staging-copy-coalescing/fixture.cpp',
           'tests/vibewine/staging-copy-coalescing/run.py']
    print(json.dumps(dict(arch=args.arch,baseline=BASE,sanitizers=['address','undefined'],
                         scope='full production CopyTextureBitcast, actual wire structs/Rc; native/context boundary substituted; no Metal/GPU/Wine',
                         sources_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
                         results=results),indent=2))

if __name__=='__main__':main()
