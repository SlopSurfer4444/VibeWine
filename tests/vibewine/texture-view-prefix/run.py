#!/usr/bin/env python3
"""Compile production texture-view types/owners and native wrapper calls; CPU only."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BASE='a21579fb2a03aef5fbe93d7bcba1ea882e63a192'
CPP='src/dxmt/dxmt_texture.cpp'
HPP='src/dxmt/dxmt_texture.hpp'

def block(text,marker):
    start=text.index(marker);cursor=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}');cursor+=1
    return text[start:cursor]

def replace(text,before,after):
    if text.count(before)!=1:raise RuntimeError(('Unbound control',before,text.count(before)))
    return text.replace(before,after)

def generate(cpp,hpp):
    classes='\n'.join(block(hpp,marker)+';' for marker in ['struct TextureViewDescriptor','struct TextureViewKey','class TextureView {','class TextureViewRef :'])
    classes=classes.replace('class TextureView {','class Texture;\nclass TextureAllocation;\nclass TextureView {',1)
    starts=['void\nTextureView::incRef()','void\nTextureView::decRef()',
            'TextureView::TextureView(TextureAllocation *allocation) :',
            'TextureView::TextureView(TextureAllocation *allocation, unsigned index, TextureViewDescriptor descriptor) :',
            'void\nTexture::prepareAllocationViews(', 'TextureViewKey\nTexture::createView(',
            'TextureView &\nTexture::view(TextureViewKey key) {',
            'TextureView &\nTexture::view(TextureViewKey key, TextureAllocation* allocation)',
            'TextureViewKey Texture::checkViewUseArray(', 'TextureViewKey Texture::checkViewUseFormat(',
            'Rc<TextureAllocation>\nTexture::rename(']
    methods='\n'.join(block(cpp,start) for start in starts)
    declaration=re.search(r'  void prepareAllocationViews\([^;]+;',hpp).group()
    declarations=declaration+'''\n  TextureViewKey createView(TextureViewDescriptor const &);
  TextureView &view(TextureViewKey);
  TextureView &view(TextureViewKey,TextureAllocation *);
  TextureViewKey checkViewUseArray(TextureViewKey,bool);
  TextureViewKey checkViewUseFormat(TextureViewKey,WMTPixelFormat);
  Rc<TextureAllocation> rename(Rc<TextureAllocation> &&);'''
    text=(HERE/'mocks.hpp').read_text()
    for marker,body in [('// ACTUAL_VIEW_TYPES',classes),('// ACTUAL_OWNER_DECLARATIONS',declarations),('// ACTUAL_METHODS',methods)]:
        text=replace(text,marker,body)
    return text

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arch',choices=('native','x86_64'),default='native')
    p.add_argument('--source-dir',type=Path,default=ROOT)
    args=p.parse_args();root=args.source_dir
    cpp=(root/CPP).read_text();hpp=(root/HPP).read_text()
    oldcpp=subprocess.check_output(['git','-C',str(root),'show',BASE+':'+CPP],text=True)
    oldhpp=subprocess.check_output(['git','-C',str(root),'show',BASE+':'+HPP],text=True)
    current=generate(cpp,hpp);old=generate(oldcpp,oldhpp)
    variants=[('candidate',current,[],True),('old-counts',old,['baseline'],True),('old-target',old,[],False)]
    # Every control edits the exercised production owner/constructor, never a
    # mock cache. The unchanged scenario oracle must reject its behavior.
    controls=[
      ('eager-tail','allocation->version_ <= index','allocation->version_ < version_'),
      ('reset-prefix','  std::shared_lock<dxmt::shared_mutex> lock(mutex_);\n  // Keep a contiguous materialized prefix,','  allocation->version_ = 1;\n  std::shared_lock<dxmt::shared_mutex> lock(mutex_);\n  // Keep a contiguous materialized prefix,'),
      ('omit-descriptor-lock','  std::shared_lock<dxmt::shared_mutex> lock(mutex_);\n  // Keep a contiguous materialized prefix,','  // no descriptor lock\n  // Keep a contiguous materialized prefix,'),
      ('wrong-native-format','      descriptor.format, descriptor.type, descriptor.firstMiplevel, descriptor.miplevelCount,','      WMTPixelFormatInvalid, descriptor.type, descriptor.firstMiplevel, descriptor.miplevelCount,'),
      ('wrong-native-parent','  auto parent = allocation->texture();','  auto parent = allocation->texture(); parent.handle = 999;'),
      ('omit-gpu-id','  );\n}\nvoid\nTexture::prepareAllocationViews','  );\n  gpuResourceID = 0;\n}\nvoid\nTexture::prepareAllocationViews'),
    ]
    for name,before,after in controls:variants.append((name,replace(current,before,after),[],False))
    # This mutation intentionally preserves ordinary successful loops while
    # publishing the requested epoch too late after partial construction.
    delayed=replace(current,'  for (; allocation->version_ <= index; allocation->version_++) {\n    auto version = allocation->version_;',
                    '  for (auto version = allocation->version_; version <= index; version++) {')
    delayed=replace(delayed,'    allocation->cached_view_.push_back(new TextureView(allocation, version, viewDescriptors_[version]));\n  }',
                    '    allocation->cached_view_.push_back(new TextureView(allocation, version, viewDescriptors_[version]));\n  }\n  allocation->version_ = index + 1;')
    variants.append(('publish-epoch-only-at-end',delayed,[],False))
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-view-prefix-') as directory:
        scratch=Path(directory)
        for name,text,arguments,accepted in variants:
            (scratch/'owners-under-test.hpp').write_text(text);binary=scratch/name
            command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                     '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                     '-I'+str(scratch),'-I'+str(root/'src/winemetal'),'-I'+str(root/'src/util'),'-I'+str(root/'src/dxmt'),'-I'+str(root/'include'),
                     str(HERE/'fixture.cpp'),'-o',str(binary)]
            if args.arch=='x86_64':command+=['-arch','x86_64']
            subprocess.run(command,check=True,timeout=90)
            run=[str(binary),*arguments]
            if args.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
            outcome=subprocess.run(run,capture_output=True,text=True,timeout=30);output=outcome.stdout+outcome.stderr
            if accepted:assert outcome.returncode==0,(name,outcome.returncode,output)
            else:
                assert outcome.returncode==1 and 'behavioral assertion:' in output,(name,outcome.returncode,output)
                assert 'AddressSanitizer' not in output and 'runtime error:' not in output,(name,output)
            results.append(dict(variant=name,accepted=accepted,exit=outcome.returncode,output=output.strip()))
    paths=[CPP,HPP,'src/util/util_svector.hpp','src/util/rc/util_rc_ptr.hpp','src/dxmt/dxmt_residency.hpp',
           'src/winemetal/Metal.hpp','tests/vibewine/texture-view-prefix/mocks.hpp',
           'tests/vibewine/texture-view-prefix/fixture.cpp','tests/vibewine/texture-view-prefix/run.py']
    print(json.dumps(dict(arch=args.arch,baseline=BASE,sanitizers=['address','undefined'],
                         scope='actual TextureView constructors/types and Texture owner methods; actual small_vector/Rc/residency; intercepted native functions; no Metal/GPU/Wine',
                         sources_sha256={path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in paths},results=results),indent=2))

if __name__=='__main__':main()
