#!/usr/bin/env python3
"""Actual buffer/texture view owners, native wrappers and containers; intercepted native boundary."""
import argparse, hashlib, json, platform, re, shutil, statistics, subprocess, tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BASE='8d0cef8dbcc8d1efbd0db97d4a0aee9e86d592d0'
FILES=['src/dxmt/dxmt_'+kind+ext for kind in ('buffer','texture') for ext in ('.cpp','.hpp')]
def block(text,marker):
    start=text.index(marker);pos=text.index('{',start)+1;depth=1
    while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[start:pos]
def replace(text,before,after):
    if text.count(before)!=1:raise RuntimeError(('Unbound control',before,text.count(before)))
    return text.replace(before,after)
def generate(sources,root,instrument=True):
    bcpp,bhpp,tcpp,thpp=sources
    types='\n'.join(block(thpp,m)+';' for m in ['struct TextureViewDescriptor','struct TextureViewKey','class TextureView {','class TextureViewRef :'])
    types=types.replace('class TextureView {','class Texture;\nclass TextureAllocation;\nclass TextureView {',1)
    starts=['void\nTextureView::incRef()','void\nTextureView::decRef()',
      'TextureView::TextureView(TextureAllocation *allocation) :','TextureView::TextureView(TextureAllocation *allocation, unsigned index, TextureViewDescriptor descriptor) :',
      'void\nTexture::prepareAllocationViews(', 'TextureViewKey\nTexture::createView(',
      'TextureView &\nTexture::view(TextureViewKey key) {','TextureView &\nTexture::view(TextureViewKey key, TextureAllocation* allocation)',
      'TextureViewKey Texture::checkViewUseArray(', 'TextureViewKey Texture::checkViewUseFormat(', 'Rc<TextureAllocation>\nTexture::rename(']
    methods='\n'.join(block(tcpp,m) for m in starts)
    # Count executed descriptor comparisons, without changing their control flow.
    if instrument:methods=replace(methods,'  for (; i < version_; i++) {','  for (; i < version_; i++) {\n    ++descriptor_comparisons;')
    declarations=re.search(r'  void prepareAllocationViews\([^;]+;',thpp).group()+'''
  TextureViewKey createView(TextureViewDescriptor const &);
  TextureView &view(TextureViewKey);TextureView &view(TextureViewKey,TextureAllocation *);
  TextureViewKey checkViewUseArray(TextureViewKey,bool);TextureViewKey checkViewUseFormat(TextureViewKey,WMTPixelFormat);
  Rc<TextureAllocation> rename(Rc<TextureAllocation> &&);'''
    btypes='\n'.join(block(bhpp,m)+';' for m in ['enum class BufferAllocationFlag','struct BufferViewKey','struct BufferViewDescriptor','struct BufferView {'])
    bowner=block(bhpp,'class Buffer {').replace('private:','public:')+';'
    bmethods='\n'.join(block(bcpp,m) for m in ['WMT::Texture\nBuffer::view(BufferViewKey key) {','WMT::Texture\nBuffer::view(BufferViewKey key, BufferAllocation *allocation)',
      'BufferView const &\nBuffer::view_(BufferViewKey key) {','BufferView const &\nBuffer::view_(BufferViewKey key, BufferAllocation *allocation)',
      'DXMT_RESOURCE_RESIDENCY_STATE &\nBuffer::residency(BufferViewKey key) {','DXMT_RESOURCE_RESIDENCY_STATE &\nBuffer::residency(BufferViewKey key, BufferAllocation *allocation)',
      'void\nBuffer::prepareAllocationViews(', 'BufferViewKey\nBuffer::createView(', 'Rc<BufferAllocation>\nBuffer::rename('])
    fmt=block((root/'src/dxmt/dxmt_format.cpp').read_text(),'uint32_t\nMTLGetTexelSize(')
    text=(HERE/'mocks.hpp').read_text()
    for marker,value in [('// ACTUAL_VIEW_TYPES',types),('// ACTUAL_OWNER_DECLARATIONS',declarations),('// ACTUAL_METHODS',methods),
        ('// ACTUAL_BUFFER_TYPES',btypes),('// ACTUAL_BUFFER_OWNER',bowner),('// ACTUAL_BUFFER_METHODS',bmethods),('// ACTUAL_TEXEL_SIZE',fmt)]:text=replace(text,marker,value)
    # Forward declaration required by extracted production buffer owner methods.
    return text
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bench',action='store_true');p.add_argument('--arch',choices=('native','x86_64'),default='native');p.add_argument('--source-dir',type=Path,default=ROOT);a=p.parse_args();root=a.source_dir
    current=[(root/f).read_text() for f in FILES]
    baseline=[subprocess.check_output(['git','-C',str(root),'show',BASE+':'+f],text=True) for f in FILES]
    candidate=generate(current,root);old=generate(baseline,root)
    variants=[('candidate',candidate,[],True),('old-counts',old,['baseline'],True),('old-target',old,[],False)]
    controls=[
      ('eager-buffer-tail','  // so a wrapper/vector allocation failure resumes without duplicating views.\n  for (; allocation->version_ <= index;', '  // so a wrapper/vector allocation failure resumes without duplicating views.\n  for (; allocation->version_ < version_;'),
      ('buffer-residency-eager','    prepareAllocationViews(allocation, key.index);\n  }\n  return allocation->cached_view_[key.index]->residency;','    prepareAllocationViews(allocation, version_ - 1);\n  }\n  return allocation->cached_view_[key.index]->residency;'),
      ('no-array-memo','if (view.alternateArrayIndex != ~0u)', 'if (false && view.alternateArrayIndex != ~0u)'),
      ('wrong-array-key','viewDescriptors_[key.index].alternateArrayIndex = alternate.index;', 'viewDescriptors_[key.index].alternateArrayIndex = key.index;'),
      ('inherit-format-memo','  viewDescriptors_.back().alternateArrayIndex = ~0u;', '  /* unsafe inherited memo */'),
      ('buffer-wrong-residency','  return allocation->cached_view_[key.index]->residency;','  return allocation->cached_view_[0]->residency;'),
      ('buffer-wrong-gpu-id','std::move(view), info.gpu_resource_id, allocation->suballocation_size_ / texel_size','std::move(view), 0, allocation->suballocation_size_ / texel_size'),
      ('buffer-wrong-format','    info.pixel_format = format;', '    info.pixel_format = WMTPixelFormatInvalid;'),
      ('buffer-wrong-parent','allocation->obj_.newTexture(info, 0, total_length)', 'WMT::Buffer{999}.newTexture(info, 0, total_length)'),
      ('buffer-no-lock','  std::unique_lock<dxmt::mutex> lock(mutex_);\n  assert(index < version_);\n  // Materialize','  assert(index < version_);\n  // no lock\n  // Materialize'),
    ]
    for name,before,after in controls:variants.append((name,replace(candidate,before,after),[],False))
    delayed=replace(candidate,'  // so a wrapper/vector allocation failure resumes without duplicating views.\n  for (; allocation->version_ <= index; allocation->version_++) {\n    auto version = allocation->version_;','  // delayed prefix control\n  for (auto version = allocation->version_; version <= index; version++) {')
    delayed=replace(delayed,'    ));\n  }\n}\nBufferViewKey','    ));\n  }\n  allocation->version_ = index + 1;\n}\nBufferViewKey')
    variants.append(('buffer-late-prefix',delayed,[],False))
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results=[];timings={}
    with tempfile.TemporaryDirectory(prefix='dxmt-cpu-view-') as d:
      scratch=Path(d)
      for name,text,args,accepted in variants:
        (scratch/'owners-under-test.hpp').write_text(text);binary=scratch/name
        cmd=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
          '-I'+str(scratch),*[ '-I'+str(root/f) for f in ('src/winemetal','src/util','src/dxmt','include')],str(HERE/'fixture.cpp'),'-o',str(binary)]
        if a.arch=='x86_64':cmd+=['-arch','x86_64']
        subprocess.run(cmd,check=True,timeout=90)
        run=[str(binary),*args]
        if a.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
        out=subprocess.run(run,capture_output=True,text=True,timeout=40);output=out.stdout+out.stderr
        if accepted:assert out.returncode==0,(name,out.returncode,output)
        else:
          assert out.returncode==1 and 'behavioral assertion:' in output,(name,out.returncode,output)
          assert 'AddressSanitizer' not in output and 'runtime error:' not in output,(name,output)
        results.append(dict(variant=name,accepted=accepted,exit=out.returncode,output=output.strip()))
        if name=='candidate':
          invalid=subprocess.run(run+['huge-key'],capture_output=True,text=True,timeout=20);diagnostic=invalid.stdout+invalid.stderr
          assert invalid.returncode<0 and 'index < version_' in diagnostic and 'AddressSanitizer' not in diagnostic and 'runtime error:' not in diagnostic,(invalid.returncode,diagnostic)
          results.append(dict(variant='huge-key-guard',accepted=False,exit=invalid.returncode,output=diagnostic.strip()))
      if a.bench:
        for name,sources in [('baseline',baseline),('candidate',current)]:
          (scratch/'owners-under-test.hpp').write_text(generate(sources,root,instrument=False));binary=scratch/(name+'-bench')
          cmd=[compiler,'-std=c++20','-O2','-DNDEBUG','-Wno-unused-parameter','-I'+str(scratch),*[ '-I'+str(root/f) for f in ('src/winemetal','src/util','src/dxmt','include')],str(HERE/'fixture.cpp'),'-o',str(binary)]
          if a.arch=='x86_64':cmd+=['-arch','x86_64']
          subprocess.run(cmd,check=True,timeout=90);run=[str(binary),'bench']
          if a.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
          text=subprocess.check_output(run,text=True,timeout=40);rows={}
          for line in text.splitlines():
            _,registry,calls,ns,checksum=line.split();rows.setdefault(registry,[]).append(int(ns)/int(calls))
          timings[name]={n:{'median_ns_per_call':statistics.median(v),'min_ns_per_call':min(v),'max_ns_per_call':max(v),'trials':len(v),'calls_per_trial':200000} for n,v in rows.items()}
    paths=FILES+['src/dxmt/dxmt_format.cpp','src/dxmt/dxmt_residency.hpp','src/winemetal/Metal.hpp','src/util/util_svector.hpp','src/util/rc/util_rc_ptr.hpp']+['tests/vibewine/cpu-view-economy/'+f for f in ('run.py','fixture.cpp','mocks.hpp')]
    print(json.dumps(dict(arch=a.arch,base=BASE,sanitizers=['address','undefined'],synthetic_warm_array_bench=timings,bench_limit='actual owner method, mock setup, uncontended locks; synthetic registry sizes; no per-frame workload or FPS inference',scope='actual Buffer/Texture view types+owner bodies+Metal wrappers+texel-size+small_vector/Rc/residency; intercepted native boundary; no Metal/GPU/Wine',sources_sha256={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in paths},results=results),indent=2))
if __name__=='__main__':main()
