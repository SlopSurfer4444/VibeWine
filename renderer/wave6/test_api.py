from pathlib import Path
import hashlib,json,subprocess
r=Path(__file__).resolve().parent
s=(r/'source/src/d3d11/cs2_trace.hpp').read_text()
a=s.index('struct ApiScope {');b=s.index('// Diagnostic attribution',a)
helper=s[a:b]
test=r/'consumer/tests_api.cpp'
test.write_text('''#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#include "../source/src/d3d11/cs2_trace_protocol.hpp"
namespace dxmt::cs2trace {
static uint32_t active=0;static uint64_t tick=100,clockReads=0;
struct Row{uint32_t gen,type,result;uint64_t start,end,obj,arg0,arg1;};
static std::vector<Row> rows;
inline uint32_t generation(){return active;}
inline uint64_t now(){++clockReads;return tick;}
inline void record(uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t obj,uint32_t result,uint64_t arg0,uint64_t arg1){if(gen==active)rows.push_back({gen,type,result,start,end,obj,arg0,arg1});}
'''+helper+'''}
using namespace dxmt::cs2trace;
int main(){
 {ApiScope a(40,1);auto value=a.invoke([]{tick+=50;return int32_t(7);});assert(value==7);}assert(rows.empty()&&clockReads==0);
 active=7;tick=100;
 {ApiScope a(40,2,3,4);auto value=a.invoke([]{tick+=200;return int32_t(0);});assert(value==0);}assert(rows.size()==1&&rows[0].start==100&&rows[0].end==300&&rows[0].arg0==3&&rows[0].arg1==4&&rows[0].result==0);
 {ApiScope a(41,2);a.invoke([]{tick+=2;return int32_t(-1);});}assert(rows.back().result==0xffffffffu&&rows.back().end-rows.back().start==2);
 {ApiScope a(43,2);a.result=0;tick+=3;}assert(rows.back().result==0);
 {ApiScope a(43,2);a.result=1;tick+=4;}assert(rows.back().result==1);
 auto count=rows.size();{ApiScope a(42,2);active=8;a.invoke([]{tick+=5;return int32_t(0);});}assert(rows.size()==count);
 active=0;count=rows.size();{ApiScope a(42,2);active=9;a.invoke([]{tick+=5;return int32_t(0);});}assert(rows.size()==count);
 std::cout<<"7 exact API helper checks passed; no Wine/WSI runtime exercised\\n";
}
''')
subprocess.run(['clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(test),'-o',str(r/'consumer/tests_api')],check=True)
result=subprocess.run([str(r/'consumer/tests_api')],capture_output=True,text=True,check=True)
# Source placement checks establish entry coverage; native WSI execution remains pending.
swap=(r/'source/src/d3d11/d3d11_swapchain.cpp').read_text();imm=(r/'source/src/d3d11/d3d11_context_imm.cpp').read_text()
for source,signature,api in [(swap,'Present1(UINT SyncInterval',40),(swap,'ResizeBuffers(UINT BufferCount',44),(swap,'ResizeTarget(const DXGI_MODE_DESC',45),(swap,'SetFullscreenState(BOOL Fullscreen',46),(imm,'GetData(ID3D11Asynchronous',41),(imm,'Map(ID3D11Resource',42)]:
 body=source[source.index(signature):];body=body[body.index('{')+1:];assert body.lstrip().startswith('cs2trace::ApiScope api('+str(api)+',')
assert 'GetDataFlags &= ~D3D11_ASYNC_GETDATA_DONOTFLUSH;' in imm
assert swap.count('cs2trace::Present trace_present')==1
(r/'consumer/test-results-api.json').write_text(json.dumps({'helper_source_sha256':hashlib.sha256(s.encode()).hexdigest(),'exact_helper_sha256':hashlib.sha256(helper.encode()).hexdigest(),'helper_checks':7,'source_boundary_checks':6,'DONOTFLUSH_compatibility_preserved':True,'legacy_body_timer_preserved':True,'result':result.stdout.strip(),'live_wsi':False},indent=2)+'\n')
print(result.stdout,end='')
