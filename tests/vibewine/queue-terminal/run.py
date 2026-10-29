#!/usr/bin/env python3
"""Run exact queue admission/shutdown/registration, staging Map loop and encoder discard bodies."""
import argparse,hashlib,json,subprocess,tempfile,shutil,re
from pathlib import Path

def body(text, marker):
 start=text.index(marker); op=text.index('{',start); end=op+1; depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def main():
 p=argparse.ArgumentParser();p.add_argument('--mutation',choices=['drop-active','drop-drain','drop-map-failure','admit-after-failure','submit-after-failure','drop-native-guard','ctor-early-register']);a=p.parse_args()
 here=Path(__file__).resolve().parent;root=here.parents[2]
 paths=['src/dxmt/dxmt_command_queue.cpp','src/dxmt/dxmt_context.cpp','src/d3d11/d3d11_context_imm.cpp','src/util/util_cpu_fence.hpp','src/dxmt/dxmt_command_queue.hpp','src/d3d11/d3d11_fence.cpp','src/d3d11/d3d11_swapchain.cpp']
 data={f:(root/f).read_text() for f in paths}
 queue=data[paths[0]];context=data[paths[1]];imm=data[paths[2]]
 generated={
 'methods.hpp':'\n'.join(body(queue,mark) for mark in ['void\nCommandQueue::Fail(', 'void\nCommandQueue::RegisterFailureWake(', 'void\nCommandQueue::UnregisterFailureWake(', 'int32_t\nCommandQueue::RegisterEvent(', 'void\nCommandQueue::CommitCurrentChunk()', 'void\nCommandQueue::ShutdownAndJoin()']),
 'discard.hpp':body(context,'void\nArgumentEncodingContext::discardCommands()'),
 'map.hpp':body(body(imm,'  Map(ID3D11Resource *pResource'),'      while (true) {'),
 'fence.hpp':data[paths[3]],
 'removed-reason.hpp':body(data[paths[4]],'  int32_t DeviceRemovedReason() const noexcept'),
 'semaphore-wake.hpp':body(data[paths[6]],'    failure_wake_.notify = []')+';',
 'fence-consumer.hpp':body(data[paths[5]],'  UINT64 STDMETHODCALLTYPE GetCompletedValue()')+'\n'+body(data[paths[5]],'  HRESULT STDMETHODCALLTYPE SetEventOnCompletion('),
 'submit.hpp':body(queue,'void\nCommandQueue::CommitChunkInternal(').split('  std::lock_guard<dxmt::mutex> submit_lock(failure_mutex_);',1)[1].rsplit('}',1)[0],
 }
 guards=re.findall(r'(auto (\w+) = cmdbuf\.\w*CommandEncoder\w*\([^;]*\);)\n\s*(if \(!\2\) throw std::bad_alloc\{\};)', context)
 if len(guards)!=11:raise RuntimeError('all 11 native encoder creation guards required')
 generated['native-guards.hpp']='\n'.join('static void guard_'+str(i)+'(bool valid){Factory cmdbuf{valid};Info render_pass_info{},info{},sample_buffer_info{};'+decl+check+'}' for i,(decl,name,check) in enumerate(guards))+'\nstatic void native_guards(){'+''.join('guard_'+str(i)+'(true);try{guard_'+str(i)+'(false);assert(false);}catch(const std::bad_alloc&){}' for i in range(len(guards)))+'}'
 if a.mutation=='drop-native-guard':generated['native-guards.hpp']=re.sub(r'if \(!\w+\) throw std::bad_alloc\{\};','',generated['native-guards.hpp'],count=1)
 ctor=data[paths[6]]
 ctor_start=ctor.index('    present_semaphore_ = CreateSemaphore(')
 ctor_end=ctor.index('    failure_wake_.context = this;',ctor_start)
 generated['ctor-semaphore.hpp']=ctor[ctor_start:ctor_end]
 generated['ctor-latency.hpp']=body(ctor,'    if (desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)')
 if a.mutation=='ctor-early-register':generated['ctor-latency.hpp']='pDevice->GetDXMTDevice().queue().RegisterFailureWake(failure_wake_);\n'+generated['ctor-latency.hpp']
 final_start=ctor.index('    semaphore_guard.adopted = true;')
 final_end=ctor.index('\n  };',final_start)
 generated['ctor-register.hpp']=ctor[final_start:final_end]
 generated['submit.hpp']='  std::lock_guard<dxmt::mutex> submit_lock(failure_mutex_);'+generated['submit.hpp']
 if a.mutation=='submit-after-failure': generated['submit.hpp']=generated['submit.hpp'].replace('if (FailureReason() || stopped.load(std::memory_order_acquire)) return;','(void)stopped;')
 if a.mutation=='drop-active': generated['discard.hpp']=generated['discard.hpp'].replace('if (encoder_current && !current_owned) destroy(encoder_current);','(void)current_owned;')
 if a.mutation=='drop-drain': generated['methods.hpp']=generated['methods.hpp'].replace('chunk.attached_cmdbuf.waitUntilCompleted();','(void)chunk;')
 if a.mutation=='drop-map-failure': generated['map.hpp']=generated['map.hpp'].replace('if (!cmd_queue.WaitCPUFence(coherent_seq_id + uint64_t(result))) return cmd_queue.FailureReason();','cmd_queue.WaitCPUFence(coherent_seq_id + uint64_t(result));')
 if a.mutation=='admit-after-failure': generated['methods.hpp']=generated['methods.hpp'].replace('if (FailureReason() || stopped.load(std::memory_order_acquire)) return;','if (stopped.load(std::memory_order_acquire)) return;')
 print(json.dumps({'source_sha256':{f:hashlib.sha256(t.encode()).hexdigest() for f,t in data.items()},'fixture_sha256':{f:hashlib.sha256((here/f).read_bytes()).hexdigest() for f in ['run.py','fixture.cpp']},'generated_sha256':{f:hashlib.sha256(t.encode()).hexdigest() for f,t in generated.items()},'mutation':a.mutation}),flush=True)
 with tempfile.TemporaryDirectory(prefix='queue-terminal-') as tmp:
  work=Path(tmp)
  for f,t in generated.items():(work/f).write_text(t)
  cmd=[shutil.which('clang++'),'-std=c++20','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(work),str(here/'fixture.cpp'),'-o',str(work/'fixture')]
  subprocess.run(cmd,check=True,timeout=60)
  r=subprocess.run([str(work/'fixture')],capture_output=True,text=True,timeout=8)
  print(r.stdout,end='');print(r.stderr,end='')
  if a.mutation:
   if not r.returncode:raise RuntimeError('mutation survived')
   print('REJECTED mutation',a.mutation,'exit',r.returncode)
  elif r.returncode:raise RuntimeError('candidate failed '+str(r.returncode))
if __name__=='__main__':main()
