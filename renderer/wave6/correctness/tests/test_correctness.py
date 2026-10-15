#!/usr/bin/env python3
"""Native tests extracted from candidate source. No duplicated fixed algorithms."""
from pathlib import Path
import json, subprocess, sys
root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'source'
base = root.parents[1]/'wine-wave5/renderer/diagnostic/source'
work = root/'tests/generated'; work.mkdir(exist_ok=True)
expect_regressions = '--expect-regressions' in sys.argv
results=[]
def compile_run(name, code):
    cpp=work/(name+'.cpp'); exe=work/name
    cpp.write_text(code)
    command=['clang++','-std=c++20','-O1','-g','-ftrivial-auto-var-init=pattern','-pthread','-Wno-ignored-attributes',str(cpp),'-o',str(exe)]
    r=subprocess.run(command,capture_output=True,text=True)
    if r.returncode: raise RuntimeError(r.stdout+r.stderr)
    try: r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
    except subprocess.TimeoutExpired: raise RuntimeError(name+' timed out')
    results.append({'test':name,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
    if r.returncode and not expect_regressions: raise RuntimeError(name+': '+r.stdout+r.stderr)
prelude=r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
using namespace std::chrono_literals;
int SetThreadPriority(int,int) {return 1;} int GetCurrentThread(){return 0;}
constexpr int THREAD_PRIORITY_TIME_CRITICAL=1;
namespace seam {
std::mutex m; std::condition_variable cv;
bool enabled=false, seen=false, release=false, shutdown_lock=false, notify=false;
thread_local bool destroying=false;
void lock_attempt(){if(destroying){std::lock_guard l(m);shutdown_lock=true;cv.notify_all();}}
void before_wait(){std::unique_lock l(m);if(!enabled)return;seen=true;cv.notify_all();cv.wait(l,[]{return release;});}
void notification(){if(destroying){std::lock_guard l(m);notify=true;cv.notify_all();}}
}
namespace dxmt {
struct mutex { std::mutex m; void lock(){seam::lock_attempt();m.lock();} void unlock(){m.unlock();} };
struct condition_variable {
std::condition_variable_any cv;
void notify_all(){seam::notification();cv.notify_all();} void notify_one(){cv.notify_one();}
template<class P> void wait(std::unique_lock<mutex>&l,P pred){while(!pred()){seam::before_wait();cv.wait(l);}}
};
struct thread:std::thread {using std::thread::thread; static unsigned hardware_concurrency(){return 2;}};
}
'''
tasks=(source/'src/dxmt/dxmt_tasks.hpp').read_text()
tasks='\n'.join(l for l in tasks.splitlines() if not l.startswith('#'))
# Expose state only in native fixture to inspect cancellation without racing C++ lifetime.
tasks=tasks.replace('private:', 'public:')
scheduler_test=r'''
struct Task {std::atomic<bool> done{false};std::function<Task*()> run;};
namespace dxmt {template<> struct task_trait<Task*> {
Task* run_task(Task*t){return t->run();} bool get_done(Task*t){return t->done;}
void set_done(Task*t){t->done=true;}
};}
void until(std::function<bool()> p){auto end=std::chrono::steady_clock::now()+3s;while(!p()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
int main(){
// Force last false predicate -> wait window, and observe shutdown's lock attempt.
{
{std::lock_guard l(seam::m);seam::enabled=true;}
auto s=std::make_unique<dxmt::task_scheduler<Task*>>();
{std::unique_lock l(seam::m);assert(seam::cv.wait_for(l,3s,[]{return seam::seen;}));}
std::thread destroyer([&]{seam::destroying=true;s.reset();});
bool uses_lock;
{std::unique_lock l(seam::m);assert(seam::cv.wait_for(l,3s,[]{return seam::shutdown_lock||seam::notify;}));uses_lock=seam::shutdown_lock;seam::release=true;seam::cv.notify_all();}
if(!uses_lock){std::cerr<<"shutdown notified before synchronizing predicate"<<std::endl;std::_Exit(2);}
destroyer.join();
{std::lock_guard l(seam::m);seam::enabled=false;}
}
for(int i=0;i<30;i++){dxmt::task_scheduler<Task*> idle;}
{
dxmt::task_scheduler<Task*> s; Task dependency, continuation;
std::atomic<int> attempts=0;
dependency.run=[&]{return &dependency;};continuation.run=[&]{attempts++;return dependency.done.load()?&continuation:&dependency;};
s.submit(&continuation);until([&]{return attempts==1;});s.submit(&dependency);
until([&]{return continuation.done.load();});assert(attempts==2);
}
{
auto s=std::make_unique<dxmt::task_scheduler<Task*>>();Task running;std::atomic<bool> entered=false,leave=false,destroyed=false;
running.run=[&]{entered=true;until([&]{return leave.load();});return &running;};s->submit(&running);until([&]{return entered.load();});
std::thread d([&]{s.reset();destroyed=true;});std::this_thread::sleep_for(20ms);assert(!destroyed);leave=true;d.join();assert(running.done);
}
{
dxmt::task_scheduler<Task*> s;Task rejected;rejected.run=[&]{std::abort();return &rejected;};
// Race a submitter against the same lock/state seam as destructor, while keeping
// object lifetime valid. The submitter must observe the closed admission gate.
std::atomic<bool> late_started=false;
std::unique_lock close(s.worker_mutex_);
std::thread late([&]{late_started=true;s.submit(&rejected);});
until([&]{return late_started.load();});s.destroyed=true;close.unlock();late.join();
assert(s.task_queue_.empty());assert(s.workers_.size()==2);s.worker_cond_.notify_all();
}
std::cout<<"PASS scheduler predicate/wait shutdown seam, idle, running task, dependency continuation, closed admission\n";
}
'''
compile_run('scheduler',prelude+tasks+scheduler_test)
cache=(source/'src/dxmt/dxmt_shader_cache.hpp').read_text()
start=cache.index('  template <typename T> class LockProtected')
end=cache.index('\n  static ShaderCache',start)
lock=cache[start:end]
compile_run('lock_move',r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <utility>
namespace dxmt {struct mutex {int locks=0,unlocks=0;bool held=false;void lock(){assert(!held);held=true;locks++;}void unlock(){assert(held);held=false;unlocks++;}};class ShaderCache {public:
'''+lock+r'''
};}
struct Value {int value=0;Value()=default;Value(int v):value(v){}operator int()const{return value;}};
int main(){using G=dxmt::ShaderCache::LockProtected<Value>;dxmt::mutex m;std::unique_ptr<G> destination;
{G a(m,42);destination=std::make_unique<G>(std::move(a));assert(!a);assert(a.object==0);assert(*destination);assert(destination->object==42);}
assert(m.held&&m.unlocks==0);{G c(std::move(*destination));destination.reset();assert(m.held&&m.unlocks==0);assert(c.object==42);}
assert(!m.held&&m.locks==1&&m.unlocks==1);{G empty;G moved(std::move(empty));assert(!moved);}assert(m.unlocks==1);
std::cout<<"PASS moved-from destruction preserves lock; chained/default moves; exactly one unlock\n";}
''')
shader=(source/'src/d3d11/d3d11_shader.cpp').read_text()
start=shader.index('                    ShaderVariantTessellationDomain variant)')
block=shader[start:shader.index('\ntemplate <>',start)]
arg_start=block.index('    SM50_SHADER_GS_PASS_THROUGH_DATA')
arg_end=block.index('    sm50_bitcode_t compile_result',arg_start)
args=block[arg_start:arg_end]
hash_start=block.index('  Sha1HashState h;')
hash_end=block.index('  auto variant_digest',hash_start)
hash_code=block[hash_start:hash_end]
public=(source/'src/airconv/airconv_public.h').resolve()
compile_run('domain_arguments',r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "'''+str(public)+r'''"
struct Handle {uint32_t value=77;uint32_t sha1(){return value;}};
struct Variant {Handle* hull_shader_handle;uint32_t gs_passthrough;bool rasterization_disabled;float max_potential_tess_factor;};
struct Sha1HashState {std::vector<unsigned char> bytes;template<class T>void update(const T&t){auto p=reinterpret_cast<const unsigned char*>(&t);bytes.insert(bytes.end(),p,p+sizeof(t));}};
SM50_SHADER_FLAG getGlobalShaderFlag(){return SM50_SHADER_FLAG_DEFUSE_FMA;}
void check(Variant variant){SM50_SHADER_COMMON_DATA common_value{};common_value.type=SM50_SHADER_COMMON;auto common=&common_value;
'''+args+r'''
auto entry=reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(&gs_passthrough);
assert(entry->type==SM50_SHADER_GS_PASS_THROUGH);assert(entry->next==&pso_tess);
assert(gs_passthrough.DataEncoded==variant.gs_passthrough);assert(gs_passthrough.RasterizationDisabled==variant.rasterization_disabled);
assert(pso_tess.type==SM50_SHADER_PSO_TESSELLATOR);assert(pso_tess.max_potential_tess_factor==variant.max_potential_tess_factor);assert(pso_tess.next==common);assert(common->next==nullptr);
'''+hash_code+r'''
Sha1HashState legacy;legacy.update(getGlobalShaderFlag());legacy.update(variant.hull_shader_handle->sha1());legacy.update(variant.gs_passthrough);legacy.update(variant.rasterization_disabled);legacy.update(variant.max_potential_tess_factor);
assert(h.bytes!=legacy.bytes);assert(h.bytes.size()==legacy.bytes.size()+sizeof(uint32_t));
}
int main(){Handle hull;for(bool raster:{false,true})for(uint32_t pass:{0u,0xffffffffu})for(float factor:{1.f,16.f,64.f})check({&hull,pass,raster,factor});std::cout<<"PASS 12 domain argument chains and domain-only cache serialization revision\n";}
''')
if expect_regressions:
    assert all(r['returncode'] != 0 for r in results), 'Baseline unexpectedly accepted a regression'
result_name = 'baseline-regressions.json' if expect_regressions else 'test-results.json'
(root/'tests'/result_name).write_text(json.dumps({'source':str(source),'results':results},indent=2))
print(json.dumps(results,indent=2))
