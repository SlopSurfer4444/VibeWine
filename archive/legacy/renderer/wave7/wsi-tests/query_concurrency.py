#!/usr/bin/env python3
"""Compile actual query/context/mutex fragments; GPU and trace dependencies are adapters.

The deterministic overlap test parks End inside Issue before state mutation and
observes whether GetData can enter CheckEventState. A lockless mutation must fail
this check, so a passing candidate is evidence of exclusion rather than timing.
No Metal, Wine, application, or installed runtime behavior is exercised.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def balanced(text, opening):
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth:
                return text[opening:i + 1]
    raise ValueError("Unbalanced source fragment")


def method(text, name):
    match = re.search(r"\b" + name + r"\([^\n]*\) override\s*\{", text)
    if not match:
        raise ValueError(f"Method not found: {name}")
    return text[match.start():match.end() - 1] + balanced(text, match.end() - 1)


PREAMBLE = r'''
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#define STDMETHODCALLTYPE
#define unlikely(x) (x)
#define likely(x) (x)
#define ERR(...) ((void)0)
using UINT = unsigned; using BOOL = int; using HANDLE = void*; using HRESULT = int;
constexpr BOOL FALSE = 0;
constexpr HRESULT S_OK=0, S_FALSE=1, E_INVALIDARG=-1, DXGI_ERROR_INVALID_CALL=-2, E_FAIL=-3;
constexpr UINT D3D11_ASYNC_GETDATA_DONOTFLUSH=1;
enum D3D11_QUERY { D3D11_QUERY_EVENT, D3D11_QUERY_TIMESTAMP_DISJOINT,
 D3D11_QUERY_TIMESTAMP, D3D11_QUERY_OCCLUSION, D3D11_QUERY_OCCLUSION_PREDICATE,
 D3D11_QUERY_PIPELINE_STATISTICS };
struct D3D11_QUERY_DESC { D3D11_QUERY Query; UINT MiscFlags; };
using D3D11_QUERY_DESC1 = D3D11_QUERY_DESC;
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT { uint64_t Frequency; BOOL Disjoint; };
struct D3D11_QUERY_DATA_PIPELINE_STATISTICS { uint64_t fields[11]; };
using D3D11_CONTEXT_TYPE = int; constexpr int D3D11_CONTEXT_TYPE_ALL=0;
struct MTLD3D11Device {};
struct ID3D11Asynchronous { virtual UINT GetDataSize()=0; virtual ~ID3D11Asynchronous()=default; };
struct ID3D11Query : ID3D11Asynchronous { virtual void GetDesc(D3D11_QUERY_DESC*)=0; };
using ID3D11Query1 = ID3D11Query;
template<class Query> struct MTLD3DQueryBase : Query {
 D3D11_QUERY_DESC1 desc;
 MTLD3DQueryBase(MTLD3D11Device*, const D3D11_QUERY_DESC1* d):desc(*d){}
 void GetDesc(D3D11_QUERY_DESC* out) override { *out=desc; }
};
struct Probe {
 std::mutex m; std::condition_variable cv;
 bool park_issue=false, issue_entered=false, release_issue=false, check_entered=false;
 void issue() { std::unique_lock lock(m); if (!park_issue) return;
   issue_entered=true; cv.notify_all(); cv.wait(lock,[&]{return release_issue;}); }
 void check() { std::lock_guard lock(m); check_entered=true; cv.notify_all(); }
};
Probe* probe=nullptr;
namespace dxmt {
namespace this_thread { uint32_t get_id() {
 static std::atomic_uint next{1}; thread_local uint32_t tid=next.fetch_add(1); return tid;
} }
'''

ADAPTERS = r'''
struct ArgumentEncodingContext {
 void signalEvent(uint64_t){} template<class T> void sampleTimestamp(T){}
 template<class T> void endVisibilityResultQuery(T){}
};
template<class T> T* Rc(T* p) { return p; }
struct MTLD3D11OcclusionQuery : ID3D11Query1 {
 virtual void* End() { return nullptr; } virtual HRESULT GetData(void*) { return S_FALSE; }
};
struct MTLD3D11TimestampQuery : ID3D11Query1 {
 virtual void* End() { return nullptr; } virtual HRESULT GetData(void*) { return S_FALSE; }
};
namespace cs2trace {
enum class CommitReason { GetDataNotReady, ExplicitFlush };
struct ApiScope { int result=0; template<class... T> ApiScope(T...){};
 template<class F> auto invoke(F f) { return f(); } };
struct ReasonScope { template<class... T> ReasonScope(T...){} };
uint64_t operationGeneration(uint64_t) { return 0; }
struct SyncScope { uint64_t gen,type,object,flags,queue,frame; };
}
struct Queue {
 struct Stats { unsigned event_stall=0; } stats;
 uint64_t current=1, signaled=0; int event=0;
 uint64_t traceSerial() const { return 0; }
 uint64_t GetNextEventSeqId() { return ++current; }
 uint64_t GetCurrentEventSeqId() const { return current; }
 uint64_t SignaledEventSeqId() const { return signaled; }
 uint64_t CurrentFrameSeq() const { return 1; }
 template<class F> void UpdateCurrentFrameStatistics(F f) { f(stats); }
 int GetSharedEventListener() { return 0; }
};
template<class... T> void MTLSharedEvent_setWin32EventAtValue(T...) {}
struct Context {
 d3d11_device_mutex mutex;
 Queue cmd_queue;
 struct { bool has_dirty_op_since_last_event=true; } ctx_state;
 bool promote_flush=false;
 unsigned commits=0;
 void InvalidateCurrentPass(bool){}
 template<class F> void EmitOP(F){}
 template<class F> void EmitST(F){}
 void Commit() { ++commits; ctx_state.has_dirty_op_since_last_event=false; promote_flush=false; }
'''

TESTS = r'''
};
void require(bool b, const char* msg) { if (!b) throw std::runtime_error(msg); }
using Event = MTLD3D11EventQueryImpl<BOOL>;
void behavior() {
 D3D11_QUERY_DESC1 desc{D3D11_QUERY_EVENT,0}; Event q(nullptr,&desc);
 Context c; c.mutex.set_protected(true); BOOL out=99;
 require(c.GetData(nullptr,&out,sizeof(out),0)==E_INVALIDARG,"null async");
 require(c.GetData(&q,nullptr,sizeof(out),0)==E_INVALIDARG,"null data");
 require(c.GetData(&q,&out,3,0)==E_INVALIDARG,"wrong data size");
 require(c.GetData(&q,&out,sizeof(out),0)==DXGI_ERROR_INVALID_CALL,"undefined query");
 c.End(nullptr); c.End(&q);
 require(c.GetData(&q,nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE,"zero-size poll");
 require(c.commits==0 && c.cmd_queue.stats.event_stall==0,"DONOTFLUSH committed");
 require(c.GetData(&q,&out,sizeof(out),0)==S_FALSE && out==FALSE,"pending event result");
 require(c.commits==1 && c.cmd_queue.stats.event_stall==1,"pending flush/reentrant lock");
 c.cmd_queue.signaled=c.cmd_queue.current;
 require(c.GetData(&q,&out,sizeof(out),0)==S_OK && out==1,"ready event result");
 require(c.cmd_queue.stats.event_stall==1,"ready unexpectedly flushed");
 Context stall; stall.mutex.set_protected(true); Event pending(nullptr,&desc); stall.End(&pending);
 for (unsigned i=0;i<kEventStallThreshold;++i)
   require(stall.GetData(&pending,&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE,"pending before threshold");
 require(stall.commits==0 && stall.cmd_queue.stats.event_stall==0,"flushed before stall threshold");
 require(stall.GetData(&pending,&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE,"stall result");
 require(stall.commits==1 && stall.cmd_queue.stats.event_stall==1,"stall must override DONOTFLUSH");
 // Reissuing a previously stalled query must reset its stall counter.
 stall.ctx_state.has_dirty_op_since_last_event=true; stall.End(&pending);
 stall.GetData(&pending,&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH);
 require(stall.cmd_queue.stats.event_stall==1,"End did not reset stall counter");
}
bool exclusion(bool protection) {
 D3D11_QUERY_DESC1 desc{D3D11_QUERY_EVENT,0}; Event q(nullptr,&desc);
 Context c; c.mutex.set_protected(protection);
 Probe p; p.park_issue=true; probe=&p;
 auto end=std::async(std::launch::async,[&]{c.End(&q);});
 { std::unique_lock lock(p.m); require(p.cv.wait_for(lock,std::chrono::seconds(2),[&]{return p.issue_entered;}),"End never entered Issue"); }
 std::promise<void> launched; auto entered=launched.get_future();
 auto get=std::async(std::launch::async,[&]{ launched.set_value(); BOOL out;
   return c.GetData(&q,&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH); });
 entered.wait();
 bool overlapped;
 { std::unique_lock lock(p.m);
   overlapped=p.cv.wait_for(lock,std::chrono::milliseconds(200),[&]{return p.check_entered;});
   // In the deliberately unlocked control, finish GetData before releasing Issue:
   // the test's observation is not itself an unsynchronized query-state access.
 }
 if (overlapped) (void)get.get();
 { std::lock_guard lock(p.m); p.release_issue=true; p.cv.notify_all(); }
 end.get(); if (!overlapped) (void)get.get(); probe=nullptr;
 return !overlapped;
}
} // namespace dxmt
int main(int argc,char** argv) {
 try {
   dxmt::behavior();
   bool locked=dxmt::exclusion(true);
   bool unprotected=dxmt::exclusion(false);
   bool negative=argc>1;
   dxmt::require(!unprotected,"unprotected calibration did not overlap");
   dxmt::require(locked != negative,negative ? "lockless negative control escaped detection" : "protected End/GetData overlap");
   std::cout << (negative ? "NEGATIVE_CONTROL_DETECTED" : "QUERY_BEHAVIOR_AND_EXCLUSION_PASS") << '\n';
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "source")
    args = parser.parse_args()
    folder = args.source / "src/d3d11"
    paths = [folder / n for n in ("d3d11_context_imm.cpp", "d3d11_query.hpp", "d3d11_multithread.hpp", "d3d11_multithread.cpp")]
    context, query, mutex_h, mutex_cpp = [p.read_text() for p in paths]
    get_data = "HRESULT " + method(context, "GetData")
    lock = "std::lock_guard<d3d11_device_mutex> lock(mutex);"
    if get_data.count(lock) != 1:
        raise SystemExit("Expected exactly one candidate GetData device mutex guard")
    methods = "\n".join("void " + method(context, n) for n in ("End", "Flush", "Flush1"))
    methods = methods.replace(" override", "")
    get_data = get_data.replace(" override", "")
    mutex_start = mutex_h.index("class d3d11_device_mutex")
    mutex_class = mutex_h[mutex_start:mutex_h.index("\nclass D3D11Multithread", mutex_start)]
    mutex_impl = mutex_cpp[mutex_cpp.index("void d3d11_device_mutex::lock()"):mutex_cpp.rindex("}; // namespace")]
    query_fragment = query[query.index("enum class QueryState"):query.index("class MTLD3D11OcclusionQuery")]
    query_fragment = query_fragment.replace("void Issue(uint64_t current_seq_id) override {", "void Issue(uint64_t current_seq_id) override { if (probe) probe->issue();")
    query_fragment = query_fragment.replace("EventState CheckEventState(uint64_t coherent_seq_id) override {", "EventState CheckEventState(uint64_t coherent_seq_id) override { if (probe) probe->check();")
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("No C++ compiler available")
    results = []
    with tempfile.TemporaryDirectory(prefix="wave7-query-") as tmp:
        tmp = Path(tmp)
        for negative in (False, True):
            candidate = get_data.replace(lock, "", 1) if negative else get_data
            code = PREAMBLE + mutex_class + mutex_impl + query_fragment + ADAPTERS + methods + candidate + TESTS
            cpp = tmp / ("negative.cpp" if negative else "candidate.cpp")
            exe = cpp.with_suffix("")
            cpp.write_text(code)
            subprocess.run([compiler, "-std=c++20", "-O1", "-pthread", str(cpp), "-o", str(exe)], check=True, timeout=60)
            run = subprocess.run([str(exe)] + (["negative"] if negative else []), check=True, timeout=15, capture_output=True, text=True)
            results.append(run.stdout.strip())
    print(json.dumps({"source": str(args.source.resolve()), "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, "results": results, "limits": "Extracted CPU behavior with GPU/trace adapters; no Wine/Metal/live game validation. Protection is enabled before threads start; concurrent protection toggles are outside scope."}, indent=2))


if __name__ == "__main__":
    main()
