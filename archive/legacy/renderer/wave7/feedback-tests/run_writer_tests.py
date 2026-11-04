"""Compile exact presenter drain/close bodies with real native callbacks + v2 writer."""
from pathlib import Path
import subprocess,hashlib,json
ROOT=Path(__file__).resolve().parent
PRESENTER=ROOT.parent/'source/src/dxmt/dxmt_presenter.cpp'
PROTOCOL=ROOT.parent/'protocol/src/d3d11/cs2_trace_protocol.hpp'
BRIDGE=ROOT.parents[2]/'native/wave7/source/bridge'
for name, production in (
    ('wave7_feedback.h', BRIDGE/'wave7_feedback.h'),
    ('wave7_feedback.inc', BRIDGE/'src/winemetal/unix/wave7_feedback.inc'),
):
    if (ROOT/'native-close/candidate'/name).read_bytes()!=production.read_bytes():
        raise RuntimeError('Feedback fixture candidate differs from final production: '+name)
if PROTOCOL.read_bytes()!=(ROOT.parent/'source/src/d3d11/cs2_trace_protocol.hpp').read_bytes():
    raise RuntimeError('Consumer protocol differs from final renderer protocol')
def extract(text,name):
    begin=text.index('void Presenter::'+name+'(');brace=text.index('{',begin);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[begin:end]
def main():
    presenter=(PRESENTER).read_text()
    bodies='\n'.join(extract(presenter,n) for n in ['drainPresentationFeedback','closePresentationFeedback'])
    prefix=r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include "../protocol/src/d3d11/cs2_trace_protocol.hpp"
#include "native-close/candidate/wave7_feedback.inc"
#include <Block.h>
@interface TestDrawable : NSObject { @public MTLDrawablePresentedHandler handler; double time; }
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block;
- (double)presentedTime;
- (void)fire;
@end
@implementation TestDrawable
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block { handler=Block_copy(block); }
- (double)presentedTime { return time; }
- (void)fire { handler((id<MTLDrawable>)self); }
- (void)dealloc { if(handler)Block_release(handler);[super dealloc]; }
@end
static dxmt::cs2trace::Header *trace_header;
namespace dxmt::cs2trace {
uint64_t now(){static uint64_t next=1;return next++;}
void record(uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object=0,uint32_t flags=0,uint64_t d0=0,uint64_t d1=0){
  writeEvent(trace_header,gen,type,start,end,object,flags,d0,d1,1);
}
}
namespace cs2trace=dxmt::cs2trace;
static TestDrawable *callback_before_close;
static W7FBRequest feedbackRequest(uint32_t op){W7FBRequest r{};r.size=sizeof(r);r.version=W7FB_VERSION;r.op=op;return r;}
static bool feedbackCall(W7FBRequest &r){
 assert(!cs2trace::controlWriters(trace_header->control));
 if(r.op==W7FB_CLOSE&&callback_before_close){auto d=callback_before_close;callback_before_close=nil;[d fire];}
 return w7fb_dispatch(&r)==W7FB_REPLY;
}
class Presenter {public:
 uint64_t feedback_session_=0,swapchain_serial_=20,feedback_capabilities_=3;
 uint32_t feedback_generation_=10;
 void drainPresentationFeedback();void closePresentationFeedback();
};
'''
    suffix=r'''
static void reset_trace(uint32_t gen=10,uint64_t capacity=cs2trace::Capacity){
 assert(!trace_header||!cs2trace::controlWriters(trace_header->control));free(trace_header);
 trace_header=(cs2trace::Header*)calloc(1,4096+cs2trace::Capacity*sizeof(cs2trace::Event));assert(trace_header);
 trace_header->magic=cs2trace::Magic;trace_header->version=2;trace_header->capacity=capacity;
 trace_header->control=cs2trace::controlWord(gen,cs2trace::CaptureState::Open);
}
static cs2trace::Event *events(){return (cs2trace::Event*)((char*)trace_header+4096);}
static unsigned count_type(unsigned type){unsigned n=0;for(uint64_t i=0;i<trace_header->next&&i<trace_header->capacity;++i)if(events()[i].committed==i+1&&events()[i].type==type)++n;return n;}
static cs2trace::Event only_event(unsigned type){assert(count_type(type)==1);for(uint64_t i=0;i<trace_header->next;++i)if(events()[i].type==type)return events()[i];abort();}
static Presenter make_presenter(){Presenter p;auto r=feedbackRequest(W7FB_OPEN);r.record.generation=10;r.record.swapchain_serial=20;assert(feedbackCall(r)&&r.status==0);p.feedback_session_=r.session;return p;}
static TestDrawable *register_frame(Presenter &p,uint64_t frame){
 auto d=[[TestDrawable alloc]init];d->time=100+frame*.016;
 auto r=feedbackRequest(W7FB_REGISTER);r.session=p.feedback_session_;r.drawable=(uintptr_t)d;
 r.record.generation=10;r.record.swapchain_serial=20;r.record.frame_id=frame;r.record.config_revision=7;
 r.record.drawable_width=1920;r.record.drawable_height=1080;assert(feedbackCall(r)&&r.status==0);return d;
}
int main(){@autoreleasepool{
 // Continuous interior: actual native callback ring -> exact presenter drain -> actual Event writer.
 reset_trace();auto p=make_presenter();
 for(unsigned i=1;i<=3;++i){auto d=register_frame(p,i);[d fire];[d release];p.drainPresentationFeedback();}
 assert(count_type(71)==3&&count_type(72)==3&&count_type(73)==3);
 p.closePresentationFeedback();assert(only_event(78).padding[1]==0&&w7fb_live_sessions==0);

 // Single frame remains native-only without another drain; stopping v2 cannot finalize it.
 reset_trace();p=make_presenter();auto d=register_frame(p,4);[d fire];
 assert(trace_header->next==0);auto status=feedbackRequest(W7FB_STATUS);status.session=p.feedback_session_;feedbackCall(status);
 assert(status.stats.queued==1&&status.stats.completed==1&&status.stats.pending==1);
 assert(!cs2trace::controlWriters(trace_header->control));
 std::atomic_ref<uint64_t>(trace_header->control).store(cs2trace::controlWord(10,cs2trace::CaptureState::Closed));
 p.closePresentationFeedback();assert(trace_header->next==0&&trace_header->boundaryRejected>=3);[d release];assert(w7fb_live_sessions==0);

 // Exact close race: production drain sees EMPTY, callback queues immediately before native CLOSE.
 reset_trace();p=make_presenter();d=register_frame(p,5);callback_before_close=d;
 p.closePresentationFeedback();auto closed=only_event(75);auto discarded=only_event(78);
 assert(count_type(71)==0&&discarded.flags==0&&discarded.padding[1]==1);
 assert(closed.padding[0]==1&&closed.padding[1]==0); // live fired ticket, no abandoned callback.
 [d release];assert(w7fb_live_sessions==0);

 // Unknown legacy CLOSE semantics stay explicitly unknown even with an otherwise valid reply.
 reset_trace();p=make_presenter();p.feedback_capabilities_=W7FB_CAP_PRESENTED_HANDLER;d=register_frame(p,6);callback_before_close=d;
 p.closePresentationFeedback();discarded=only_event(78);assert(discarded.flags==W7FB_UNSUPPORTED&&discarded.padding[1]==0);
 [d release];assert(w7fb_live_sessions==0);

 // PE buffer loss is independent of native callback overflow.
 reset_trace(10,2);p=make_presenter();d=register_frame(p,7);[d fire];[d release];p.drainPresentationFeedback();
 assert(count_type(71)==1&&count_type(72)==1&&count_type(73)==0&&trace_header->dropped>0);
 status=feedbackRequest(W7FB_STATUS);status.session=p.feedback_session_;feedbackCall(status);assert(status.stats.overflow==0&&status.stats.queued==0);
 p.closePresentationFeedback();assert(w7fb_live_sessions==0);

 // Old generation cannot publish into rearmed generation, and no writer admission is held across native calls.
 reset_trace(11);p=make_presenter();d=register_frame(p,8);[d fire];[d release];p.closePresentationFeedback();
 assert(trace_header->next==0&&trace_header->boundaryRejected==0&&cs2trace::controlWriters(trace_header->control)==0&&w7fb_live_sessions==0);
 free(trace_header);trace_header=nullptr;
 puts("PASS 6 groups: actual native callbacks + exact presenter drain/close + actual v2 writer; boundary, discard, legacy, loss, generation");
}return 0;}
'''
    source=ROOT/'presenter/writer_fixture.mm';source.parent.mkdir(exist_ok=True);source.write_text(prefix+bodies+suffix)
    receipt={'scope':'production native include and exact production presenter method bodies, real v2 Event writer; mocked drawable, no Metal/GPU/live process','source_sha256':{},'runs':[]}
    for path in [source,PRESENTER,PROTOCOL,ROOT/'native-close/candidate/wave7_feedback.inc',ROOT/'native-close/candidate/wave7_feedback.h']:
        receipt['source_sha256'][str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    for variant,flags in [('regular',[]),('asan',['-fsanitize=address,undefined'])]:
        binary=ROOT/'presenter'/('writer_fixture_'+variant)
        cmd=['clang++','-std=c++20','-x','objective-c++','-fblocks','-fno-objc-arc','-Wall','-Wextra','-Werror',*flags,'-I'+str(ROOT),str(source),'-framework','Foundation','-framework','Metal','-o',str(binary)]
        r=subprocess.run(cmd,text=True,capture_output=True);assert not r.returncode,r.stderr
        r=subprocess.run([str(binary)],text=True,capture_output=True);(ROOT/'presenter'/('writer-'+variant+'.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
        receipt['runs'].append({'variant':variant,'compile_command':cmd,'exit_code':r.returncode,'stdout':r.stdout,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
    (ROOT/'presenter/writer-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS regular+ASan/UBSan real-writer/native-callback/presenter-method fixture')
if __name__=='__main__':main()
