#!/usr/bin/env python3
"""Compile the verbatim production sampling method against transport/clock seams."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
text=(ROOT/'src/dxmt/dxmt_presenter.cpp').read_text()
start=text.index('void Presenter::observeVisibility(');end=text.index('void Presenter::observePresentation(',start)
method=text[start:end]
prefix=r"""
#include <cassert>
#include <cstdint>
#include <vector>
#include "wave7_visibility.h"
using obj_handle_t=uint64_t;enum WMTCommandBufferProperty:unsigned{};
namespace cs2trace {
enum class Profile {Full,CompileWaits,PresentationWaits,CombinedBounded};
static bool presentationScopesAllowed(Profile p){return p==Profile::PresentationWaits || p==Profile::CombinedBounded;}
static Profile selected=Profile::Full;static uint32_t gen=7;static uint64_t clock_ns=1;
struct Event {uint32_t type,flags;uint64_t p0,p1;};static std::vector<Event> events;
static Profile profile(){return selected;}static uint32_t generation(){return gen;}static uint64_t now(){return clock_ns;}
static void record(uint32_t g,uint32_t type,uint64_t,uint64_t,uint64_t,uint32_t flags,uint64_t p0,uint64_t p1) {
 assert(g==gen);events.push_back({type,flags,p0,p1});
}
}
static unsigned calls;static bool supported=true,close_during_call=false;
static uint64_t MTLCommandBuffer_property(obj_handle_t pointer,WMTCommandBufferProperty property) {
 assert(unsigned(property)==W7VS_PROPERTY);++calls;auto &r=*reinterpret_cast<W7VSRequest*>(pointer);
 assert(r.size==96 && r.version==1 && r.layer==123 && r.generation==cs2trace::gen);
 if(close_during_call)cs2trace::gen=0;
 if(!supported)return 0;
 r.status=W7VS_OK;r.snapshot.layer_identity=9;r.snapshot.sequence=4;return W7VS_REPLY;
}
class Presenter {public:
 struct {uint64_t handle=123;}layer_;
 uint64_t swapchain_serial_=11;uint32_t visibility_generation_=0;uint64_t visibility_last_poll_ns_=0;
 void observeVisibility(uint32_t,uint64_t);
};
"""
body=r"""
int main() {
 Presenter p;
 p.observeVisibility(7,4);assert(!calls); // Full default profile never requests.
 cs2trace::selected=cs2trace::Profile::CompileWaits;p.observeVisibility(7,4);assert(!calls);
 cs2trace::selected=cs2trace::Profile::PresentationWaits;
 p.observeVisibility(0,4);p.observeVisibility(8,4);assert(!calls);
 p.observeVisibility(7,4);assert(calls==1 && cs2trace::events.size()==4);
 for(unsigned i=0;i<100;i++)p.observeVisibility(7,4);assert(calls==1);
 cs2trace::clock_ns+=W7VS_REFRESH_NS-1;p.observeVisibility(7,4);assert(calls==1);
 ++cs2trace::clock_ns;p.observeVisibility(7,4);assert(calls==2);
 // A rearmed generation samples immediately even within old cadence.
 cs2trace::gen=8;p.observeVisibility(8,5);assert(calls==3 && cs2trace::events.back().p1==5);
 supported=false;cs2trace::clock_ns+=W7VS_REFRESH_NS;p.observeVisibility(8,5);
 assert(calls==4 && cs2trace::events[12].flags==W7VS_UNSUPPORTED);
 auto count=cs2trace::events.size();close_during_call=true;cs2trace::clock_ns+=W7VS_REFRESH_NS;
 p.observeVisibility(8,5);assert(calls==5 && cs2trace::events.size()==count);
 p.observeVisibility(0,5);assert(calls==5);
 // Combined profile admits the same bounded visibility sampler.
 close_during_call=false;supported=true;cs2trace::gen=9;
 cs2trace::selected=cs2trace::Profile::CombinedBounded;
 p.observeVisibility(9,6);assert(calls==6 && cs2trace::events.size()==count+4);
 for(unsigned i=0;i<100;i++)p.observeVisibility(9,6);assert(calls==6);
}
"""
with tempfile.TemporaryDirectory(prefix='visibility-producer-') as temp:
 p=Path(temp);src=p/'fixture.cpp';src.write_text(prefix+method+body)
 binary=p/'fixture'
 subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'src/winemetal'),str(src),'-o',str(binary)],check=True,timeout=60)
 subprocess.run([str(binary)],check=True,timeout=15)
print('PASS: verbatim producer sampling method default-off, closed/stale generations, exact cadence, rearm, unsupported, stop-during-transport and combined admission')
