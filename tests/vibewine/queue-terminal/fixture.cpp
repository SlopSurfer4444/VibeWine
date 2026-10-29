// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>
#include <condition_variable>
#include <unordered_map>
#include "fence.hpp"
#define TRACE(...) ((void)0)
#define DXMT_DEBUG_DISABLED 1
#define DXMT_RESIDENCY_SETS 0
#define ASYNC_ENCODING 1
static bool fail_counter_new=false;
void*operator new(size_t size){if(fail_counter_new)throw std::bad_alloc{};if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc{};}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
using HRESULT=int32_t;using UINT64=uint64_t;using HANDLE=void*;
#define STDMETHODCALLTYPE
#define SUCCEEDED(hr) ((hr)>=0)
constexpr HRESULT E_OUTOFMEMORY=int32_t(0x8007000e);
constexpr bool FALSE=false;
constexpr unsigned INFINITE=0xffffffff,WAIT_OBJECT_0=0;
constexpr HRESULT E_FAIL=int32_t(0x80004005),S_OK=0,DXGI_ERROR_WAS_STILL_DRAWING=int32_t(0x887a000a);
constexpr unsigned D3D11_MAP_READ=1,D3D11_MAP_WRITE=2,D3D11_MAP_FLAG_DO_NOT_WAIT=0x100000;
constexpr int WMTCommandBufferStatusScheduled=3,WMTCommandBufferStatusCompleted=4;
constexpr unsigned kCommandChunkCount=32;
static unsigned signal_calls=0,cancel_calls=0,destroy_calls=0;
static int32_t register_status=0;
struct PEEvent{std::mutex mutex;std::condition_variable changed;bool set=false;};
static std::unordered_map<void*,void(*)(void*)>native_handles;
static std::mutex events_mutex;static std::unordered_map<void*,PEEvent*>events;
static std::atomic<void*>pending_event{nullptr};static bool immediate_event=false;
static void SetEvent(void*handle){++signal_calls;std::lock_guard lock(events_mutex);auto it=events.find(handle);if(it==events.end())return;auto event=it->second;{std::lock_guard inner(event->mutex);event->set=true;}pending_event.compare_exchange_strong(handle,nullptr);event->changed.notify_one();}
static HANDLE CreateEvent(void*,bool,bool,void*){auto event=new PEEvent;std::lock_guard lock(events_mutex);events[event]=event;return event;}
static unsigned WaitForSingleObject(HANDLE handle,unsigned){auto event=static_cast<PEEvent*>(handle);std::unique_lock lock(event->mutex);event->changed.wait(lock,[&]{return event->set;});return WAIT_OBJECT_0;}
static void CloseHandle(HANDLE handle){std::lock_guard lock(events_mutex);auto native=native_handles.find(handle);if(native!=native_handles.end()){auto close=native->second;native_handles.erase(native);close(handle);return;}assert(events.erase(handle)==1);delete static_cast<PEEvent*>(handle);}
static void MTLSharedEvent_setWin32EventAtValue(uint64_t,uint64_t,void*,uint64_t){++cancel_calls;if(auto handle=pending_event.load())SetEvent(handle);}
static int32_t MTLSharedEvent_setWin32EventAtValueChecked(uint64_t,uint64_t,void*handle,uint64_t){if(!register_status){{std::lock_guard lock(events_mutex);if(events.contains(handle))pending_event=handle;}if(immediate_event)SetEvent(handle);}return register_status;}
static void SharedEventListener_destroy(uint64_t){++destroy_calls;}
namespace cs2trace {
struct Scope{int type;uint64_t object;~Scope(){}};
struct ApiScope {int result=0;template<class...T>ApiScope(T...){};};
struct SyncScope {uint64_t gen;int type;uint64_t object;uint32_t flags;uint64_t queue,frame;~SyncScope(){};};
static uint64_t generation(){return 0;}
static uint64_t operationGeneration(uint64_t){return 0;}
static uint64_t now(){return 0;}
template<class...T>void record(T...){}
template<class...T>void commitReason(T...){}
}
namespace WMT {struct SharedEvent{uint64_t handle=1;uint64_t signaledValue()const{return 0;}};struct CaptureManager{static CaptureManager sharedCaptureManager(){return{};}void stopCapture(){}};}
namespace dxmt {
using mutex=std::mutex;
struct QueueFailureWake {QueueFailureWake*next=nullptr;void(*notify)(void*)noexcept=nullptr;void*context=nullptr;bool registered=false;};
struct CommandChunk;
struct Thread{bool joined=false;void join(){joined=true;}};
static bool drained=false;
struct NativeBuffer {
 bool valid=false;int state=WMTCommandBufferStatusScheduled;unsigned waits=0,commits=0;std::function<void()>on_commit;
 void commit(){if(on_commit)on_commit();++commits;}
 explicit operator bool()const{return valid;}
 int status()const{return state;}
 void waitUntilCompleted(){++waits;state=WMTCommandBufferStatusCompleted;drained=true;}
};
struct CommandChunk {
 bool submitted=false;NativeBuffer attached_cmdbuf;
 uint64_t chunk_id=0,chunk_event_id=0,frame_=0,resource_initializer_event_id=0;uint32_t trace_generation=0;
 bool reset_called=false;
 void reset(){assert(!submitted||attached_cmdbuf.status()==WMTCommandBufferStatusCompleted);reset_called=true;}
};
struct ArgumentEncodingContext {
 enum class EncoderType {Null,Render,Compute,Blit,Clear,Resolve,Present,SpatialUpscale,TemporalUpscale,SignalEvent,WaitForEvent,SampleTimestamp};
 struct EncoderData {EncoderType type=EncoderType::Null;EncoderData*next=nullptr;};
 static inline unsigned live=0;
#define NODE(name,kind) struct name:EncoderData{name(){type=EncoderType::kind;++live;}~name(){assert(live);--live;}};
 NODE(RenderEncoderData,Render) NODE(ComputeEncoderData,Compute) NODE(BlitEncoderData,Blit) NODE(ClearEncoderData,Clear) NODE(ResolveEncoderData,Resolve) NODE(PresentData,Present) NODE(SpatialUpscaleData,SpatialUpscale) NODE(TemporalUpscaleData,TemporalUpscale) NODE(SignalEventData,SignalEvent) NODE(WaitForEventData,WaitForEvent) NODE(SampleTimestampData,SampleTimestamp)
#undef NODE
 EncoderData encoder_head{},*encoder_last=&encoder_head,*encoder_current=nullptr;
 unsigned encoder_count_=0;std::span<EncoderData*>flushing_encoders_{};size_t flushing_next_=0;
 std::vector<int>pending_queries_;
 void discardCommands()noexcept;
};
#include "discard.hpp"
struct CommandQueue {
 enum class Failure:int32_t{OutOfMemory=int32_t(0x8007000e),DeviceRemoved=int32_t(0x887a0005)};
 QueueFailureWake*failure_wakes_=nullptr;
 mutex failure_mutex_,shutdown_mutex_;std::atomic<int32_t>failure_reason_{0};
 std::shared_ptr<CpuFence::WaitState>wait_state_=std::make_shared<CpuFence::WaitState>();
 CpuFence cpu_coherent{wait_state_};
 std::atomic_bool stopped{false};bool shutdown_complete_=false;
 std::atomic_uint64_t ready_for_commit{1};
 std::atomic_uint64_t ready_for_encode{1};std::atomic_uint32_t chunk_ongoing{0};
 std::array<CommandChunk,32>chunks{};
 uint64_t shared_event_listener=1,frame_count=0,next_event=0;
 Thread encodeThread,finishThread,event_listener_thread;
 struct Capture{bool takeCaptureOwnership(){return false;}}capture_state;
 ArgumentEncodingContext argument_encoding_ctx;
 struct Allocator{void free_blocks(uint64_t){}}cpu_command_allocator;
 struct Initializer{std::function<void()>on_flush;uint64_t flushToWait(){if(on_flush)on_flush();return 0;}}initializer;
 struct Stats{unsigned command_buffer_count=0;}stats;
 template<class F>void UpdateCurrentFrameStatistics(F f){f(stats);}
 uint64_t traceSerial()const{return 1;}
 uint64_t GetNextEventSeqId(){return ++next_event;}
 std::shared_ptr<CpuFence::WaitState>WaitState(){return wait_state_;}
 int32_t FailureReason()const{return failure_reason_.load();}
#include "removed-reason.hpp"
 bool WaitCPUFence(uint64_t seq){return cpu_coherent.wait(seq);}
 uint64_t CurrentSeqId()const{return ready_for_encode.load();}
 uint64_t CoherentSeqId(){return cpu_coherent.signaledValue();}
 uint64_t CurrentFrameSeq()const{return 1;}
 void Fail(Failure)noexcept;
 void RegisterFailureWake(QueueFailureWake&)noexcept;void UnregisterFailureWake(QueueFailureWake&)noexcept;
 int32_t RegisterEvent(WMT::SharedEvent,void*,uint64_t)noexcept;
 void CommitCurrentChunk();void ShutdownAndJoin();
 void Submit(CommandChunk&chunk,uint64_t seq){auto&cmdbuf=chunk.attached_cmdbuf;
#include "submit.hpp"
 }
};
#include "methods.hpp"
}
using CommandQueue=dxmt::CommandQueue;
constexpr unsigned DXGI_MAX_SWAP_CHAIN_BUFFERS=16;
struct Semaphore{unsigned count=0;bool closed=false;std::function<void()>on_release;};
static bool ReleaseSemaphore(HANDLE handle,unsigned,void*){auto sem=static_cast<Semaphore*>(handle);assert(!sem->closed);if(sem->on_release)sem->on_release();if(sem->count==DXGI_MAX_SWAP_CHAIN_BUFFERS)return false;++sem->count;return true;}
static HANDLE CreateSemaphore(void*,unsigned initial,unsigned,void*){auto sem=new Semaphore;sem->count=initial;std::lock_guard lock(events_mutex);native_handles[sem]=[](void*p){delete static_cast<Semaphore*>(p);};return sem;}
constexpr unsigned DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT=1;
using CpuFence=dxmt::CpuFence;
struct MTLD3D11SwapChain {
 dxmt::QueueFailureWake failure_wake_;Semaphore semaphore;HANDLE present_semaphore_=&semaphore;
 struct {unsigned Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;}desc_;
 std::unique_ptr<CpuFence>frame_latency_fence_;HRESULT initialization_result_=0;
 dxmt::CommandQueue*owner=nullptr;bool owns_handle=false;
 MTLD3D11SwapChain()=default;
 explicit MTLD3D11SwapChain(dxmt::CommandQueue&q,bool inject){
  struct Device{dxmt::CommandQueue&q;Device&GetDXMTDevice(){return*this;}dxmt::CommandQueue&queue(){return q;}}device{q};auto pDevice=&device;
  unsigned frame_latency=3;
#include "ctor-semaphore.hpp"
  owns_handle=true;owner=&q;init();
  fail_counter_new=inject;
#include "ctor-latency.hpp"
  fail_counter_new=false;
#include "ctor-register.hpp"
 }
 ~MTLD3D11SwapChain(){if(owner)owner->UnregisterFailureWake(failure_wake_);if(owns_handle&&present_semaphore_)CloseHandle(present_semaphore_);}
 void init(){failure_wake_.context=this;
#include "semaphore-wake.hpp"
 }
};
struct FenceInterface {virtual UINT64 GetCompletedValue()=0;virtual HRESULT SetEventOnCompletion(UINT64,HANDLE)=0;};
struct FenceConsumer: FenceInterface {
 struct Device{dxmt::CommandQueue&queue_;dxmt::CommandQueue&queue(){return queue_;}};
 struct Parent{Device device;Device&GetDXMTDevice(){return device;}}parent;
 Parent*m_parent=&parent;WMT::SharedEvent event;
 explicit FenceConsumer(dxmt::CommandQueue&q):parent{{q}}{}
#include "fence-consumer.hpp"
};
struct StagingMapResult{static constexpr int Mapped=-3,Renamable=-2,Mappable=0;};
struct Mapped {void*pData=nullptr;unsigned RowPitch=0,DepthPitch=0;};
struct Staging{
 unsigned tries=0;char bytes[8]{};size_t length=8;unsigned bytesPerRow=8,bytesPerImage=8;
 int tryMap(uint64_t coherent,bool,bool){++tries;assert(tries<=3 && "Map must not retry terminal failure");return coherent?0:1;}
 int allocate(uint64_t){return 0;}void*mappedMemory(int){return bytes;}void*mappedImmediateMemory(){return bytes;}
 void updateImmediateName(uint64_t,int){};struct Buffer{int rename(int){return 0;}};Buffer*buffer(){static Buffer b;return &b;}int allocation(int){return 0;}
};
struct ArgumentEncodingContext{};
struct MapSeam {
 dxmt::CommandQueue&cmd_queue;std::shared_ptr<Staging>staging=std::make_shared<Staging>();
 unsigned flushes=0;std::function<void()>on_flush;
 explicit MapSeam(dxmt::CommandQueue&q):cmd_queue(q){}
 void Flush(){++flushes;if(on_flush && flushes==1)on_flush();}
 template<class F>void EmitST(F){}
 HRESULT run(unsigned MapFlags=0){
  auto staging=this->staging;
  uint64_t coherent_seq_id=cmd_queue.cpu_coherent.signaledValue(),current_seq_id=1;unsigned MapType=1;Mapped mapped,*pMappedResource=&mapped;
#include "map.hpp"
 }
};

struct Info{~Info(){}};
struct Factory {
 bool valid;
 struct Encoder {bool valid;explicit operator bool()const{return valid;}~Encoder(){}};
 Encoder renderCommandEncoder(Info){return {valid};}
 Encoder computeCommandEncoder(bool){return {valid};}
 Encoder blitCommandEncoder(){return {valid};}
 Encoder blitCommandEncoderWithSampleBuffers(Info*,unsigned){return {valid};}
};
#include "native-guards.hpp"
static void discard_controls(){
 using C=dxmt::ArgumentEncodingContext;
 { C c;alignas(C::RenderEncoderData)char a[sizeof(C::RenderEncoderData)];alignas(C::ResolveEncoderData)char b[sizeof(C::ResolveEncoderData)];alignas(C::PresentData)char d[sizeof(C::PresentData)];
 auto*x=new(a)C::RenderEncoderData;auto*y=new(b)C::ResolveEncoderData;auto*z=new(d)C::PresentData;
 c.encoder_head.next=x;x->next=y;c.encoder_current=z;c.discardCommands();assert(C::live==0);c.discardCommands();assert(C::live==0); }
 { C c;alignas(C::RenderEncoderData)char a[sizeof(C::RenderEncoderData)];auto*x=new(a)C::RenderEncoderData;c.encoder_head.next=x;c.encoder_current=x;c.discardCommands();assert(C::live==0); }
 { C c;alignas(C::RenderEncoderData)char a[sizeof(C::RenderEncoderData)];alignas(C::ClearEncoderData)char b[sizeof(C::ClearEncoderData)];alignas(C::ComputeEncoderData)char d[sizeof(C::ComputeEncoderData)];
 auto*x=new(a)C::RenderEncoderData;auto*y=new(b)C::ClearEncoderData;auto*z=new(d)C::ComputeEncoderData;
 x->~RenderEncoderData();new(a)C::EncoderData; // optimizer already consumed this prefix and severed next links
 std::array<C::EncoderData*,3>nodes{x,y,z};c.flushing_encoders_=nodes;c.flushing_next_=1;c.discardCommands();assert(C::live==0); }
}
int main(){
 using Q=dxmt::CommandQueue;
 {Q q;try{MTLD3D11SwapChain chain(q,true);assert(false);}catch(const std::bad_alloc&){}fail_counter_new=false;assert(native_handles.empty());q.Fail(Q::Failure::DeviceRemoved);assert(native_handles.empty()&&q.failure_wakes_==nullptr);}
 {Q q;{MTLD3D11SwapChain chain(q,false);assert(q.failure_wakes_==&chain.failure_wake_);q.Fail(Q::Failure::DeviceRemoved);}assert(native_handles.empty()&&q.failure_wakes_==nullptr);}
 {Q q;assert(q.DeviceRemovedReason()==0);q.Fail(Q::Failure::OutOfMemory);assert(q.DeviceRemovedReason()==int32_t(0x887a0020)&&q.FailureReason()==int32_t(Q::Failure::OutOfMemory));}
 {Q q;MTLD3D11SwapChain chain;chain.init();q.RegisterFailureWake(chain.failure_wake_);std::promise<void>release_entered,unregister_entered,release_allowed;auto allowed=release_allowed.get_future().share();std::atomic_bool once=false;chain.semaphore.on_release=[&]{if(!once.exchange(true)){release_entered.set_value();allowed.wait();}};auto failure=std::async(std::launch::async,[&]{q.Fail(Q::Failure::DeviceRemoved);});release_entered.get_future().wait();auto unregister=std::async(std::launch::async,[&]{unregister_entered.set_value();q.UnregisterFailureWake(chain.failure_wake_);});unregister_entered.get_future().wait();assert(unregister.wait_for(std::chrono::seconds(0))==std::future_status::timeout);release_allowed.set_value();failure.get();unregister.get();assert(chain.semaphore.count==DXGI_MAX_SWAP_CHAIN_BUFFERS);chain.semaphore.closed=true;chain.semaphore.count=0;q.Fail(Q::Failure::OutOfMemory);assert(chain.semaphore.count==0);}
 {Q q;unsigned notifications=0;dxmt::QueueFailureWake wake;wake.context=&notifications;wake.notify=[](void*p)noexcept{++*static_cast<unsigned*>(p);};q.RegisterFailureWake(wake);q.Fail(Q::Failure::DeviceRemoved);assert(notifications==1);q.UnregisterFailureWake(wake);q.Fail(Q::Failure::OutOfMemory);assert(notifications==1);q.RegisterFailureWake(wake);assert(notifications==2);q.UnregisterFailureWake(wake);}
 {Q q;unsigned notifications=0;dxmt::QueueFailureWake wake;wake.context=&notifications;wake.notify=[](void*p)noexcept{++*static_cast<unsigned*>(p);};q.RegisterFailureWake(wake);q.UnregisterFailureWake(wake);q.Fail(Q::Failure::DeviceRemoved);assert(notifications==0);}
 {Q q;FenceConsumer fence(q);immediate_event=true;assert(fence.SetEventOnCompletion(1,nullptr)==0);immediate_event=false;assert(events.empty());}
 {Q q;FenceConsumer fence(q);auto wait=std::async(std::launch::async,[&]{return fence.SetEventOnCompletion(1,nullptr);});while(!pending_event.load())std::this_thread::yield();q.Fail(Q::Failure::DeviceRemoved);assert(wait.wait_for(std::chrono::seconds(2))==std::future_status::ready);assert(wait.get()==int32_t(Q::Failure::DeviceRemoved));assert(fence.GetCompletedValue()==UINT64_MAX&&events.empty());}
 {Q q;auto&c=q.chunks[0];q.Fail(Q::Failure::DeviceRemoved);q.Submit(c,1);assert(c.attached_cmdbuf.commits==0&&q.ready_for_commit==1);}
 for(unsigned i=0;i<100;++i){Q q;auto&c=q.chunks[0];c.attached_cmdbuf.on_commit=[&]{assert(!q.FailureReason());};std::atomic_bool go{false};std::thread failure([&]{while(!go.load()){}q.Fail(Q::Failure::DeviceRemoved);});go=true;q.Submit(c,1);failure.join();assert(q.ready_for_commit==1+c.attached_cmdbuf.commits);}
 {Q q;q.chunk_ongoing=31;std::promise<void>entered;auto waiter=std::async(std::launch::async,[&]{entered.set_value();q.CommitCurrentChunk();});entered.get_future().wait();q.Fail(Q::Failure::DeviceRemoved);assert(waiter.wait_for(std::chrono::seconds(2))==std::future_status::ready);waiter.get();assert(q.ready_for_encode==1);}
 {Q q;q.CommitCurrentChunk();assert(q.ready_for_encode==2&&q.chunk_ongoing==1&&!q.FailureReason());}
 {Q q;q.initializer.on_flush=[&]{q.Fail(Q::Failure::DeviceRemoved);};q.CommitCurrentChunk();assert(q.ready_for_encode==1&&q.chunk_ongoing==0);}
 {Q q;q.initializer.on_flush=[] {throw std::bad_alloc{};};q.CommitCurrentChunk();assert(q.FailureReason()==int32_t(Q::Failure::OutOfMemory)&&q.ready_for_encode==1);}
 {Q q;auto&c=q.chunks[0];c.submitted=true;c.attached_cmdbuf.valid=true;q.chunks[1].attached_cmdbuf.valid=true;
 q.ShutdownAndJoin();assert(c.attached_cmdbuf.waits==1&&q.chunks[1].attached_cmdbuf.waits==0&&q.encodeThread.joined&&q.finishThread.joined);auto destroys=destroy_calls;q.ShutdownAndJoin();assert(destroys==destroy_calls);}
 {Q q;register_status=int32_t(0xc0000017);auto signals=signal_calls;assert(q.RegisterEvent({},(void*)1,1)==int32_t(Q::Failure::OutOfMemory));assert(signal_calls==signals+1);register_status=0;}
 {Q q;register_status=int32_t(0xc0000008);assert(q.RegisterEvent({},(void*)1,1)==int32_t(Q::Failure::DeviceRemoved));register_status=0;}
 {Q q;assert(q.RegisterEvent({},(void*)1,1)==0);q.Fail(Q::Failure::DeviceRemoved);assert(q.RegisterEvent({},(void*)2,1)==int32_t(Q::Failure::DeviceRemoved));}
 {Q q;MapSeam map{q};assert(map.run(D3D11_MAP_FLAG_DO_NOT_WAIT)==DXGI_ERROR_WAS_STILL_DRAWING&&map.flushes==0);q.cpu_coherent.signal(1);assert(map.run()==S_OK);}
 {Q q;MapSeam map{q};std::promise<void>flushed;map.on_flush=[&]{flushed.set_value();};auto waiter=std::async(std::launch::async,[&]{return map.run();});flushed.get_future().wait();q.Fail(Q::Failure::DeviceRemoved);assert(waiter.wait_for(std::chrono::seconds(2))==std::future_status::ready);assert(waiter.get()==int32_t(Q::Failure::DeviceRemoved));assert(q.CoherentSeqId()==0);}
 native_guards();discard_controls();
 std::cout<<"PASS actual admission/initializer failure, drained shutdown/idempotence, checked event status, blocked staging Map cancellation/no-wait/completion, active/linked/optimizer-partial encoder destruction\n";
}
