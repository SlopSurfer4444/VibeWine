#include "owners-under-test.hpp"
#include <chrono>
using namespace dxmt;
static bool baseline;
static unsigned scenarios;
static unsigned warm_comparisons;
static std::vector<BufferViewKey> buffer_keys(Buffer &b) {
  std::vector<BufferViewKey> keys;
  for(auto f:{WMTPixelFormatR8Uint,WMTPixelFormatR16Uint,WMTPixelFormatR32Uint,WMTPixelFormatR32Sint,
      WMTPixelFormatRG32Uint,WMTPixelFormatRGBA8Unorm,WMTPixelFormatRGBA16Uint,WMTPixelFormatRGBA32Uint})keys.push_back(b.createView({f}));
  return keys;
}
static void check_buffer(Buffer &b,BufferAllocation &a) {
  require(a.version_==a.cached_view_.size(),"buffer exact prefix epoch");
  for(unsigned i=0;i<a.version_;++i) {
    const auto &v=*a.cached_view_[i];require(v.gpu_resource_id==(v.texture.handle?v.texture.handle+10000:0),"buffer GPU ID from native result");
    require(v.suballocation_texel==a.suballocation_size_/MTLGetTexelSize(b.viewDescriptors_[i].format),"buffer suballocation texel stride");
  }
}
static void buffer_prefix_and_residency() {
  ++scenarios;
  Buffer b(256,{});auto keys=buffer_keys(b);b.current_=new BufferAllocation(10);
  auto start=buffer_count;auto *low=&b.view_(keys[1]);
  auto expected=baseline?keys.size():2;
  require(buffer_count-start==expected && b.current_->version_==expected,"buffer low request prepares required prefix");
  auto requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;
  require(CheckResourceResidency(b.residency(keys[1]),20,requested),"buffer first residency emission");
  auto *state=&b.residency(keys[1]);auto count=buffer_count;
  require(&b.view_(keys[0])==b.current_->cached_view_[0].get() && &b.view_(keys[1])==low && buffer_count==count,"buffer lower/repeated no native work");
  b.view_(keys.back());require(buffer_count-start==keys.size(),"buffer later higher creates each required view once");
  require(&b.view_(keys[1])==low && &b.residency(keys[1])==state,"buffer identity across small_vector growth");
  requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;require(!CheckResourceResidency(*state,20,requested),"buffer existing residency survives prefix growth");
  auto alias=b.createView({WMTPixelFormatR8Uint});require(alias.index==keys[0].index,"buffer descriptor dedup");
  auto tail=b.createView({WMTPixelFormatR8Sint});count=buffer_count;b.view_(keys[1]);
  require(buffer_count==count+(baseline?1:0),"unused buffer descriptor does not force tail");
  b.view_(tail);require(buffer_count==count+1,"later appended buffer key resolves once");check_buffer(b,*b.current_);
  // Residency-first entry point must obey the same prefix, and bind to the
  // texture whose state it returns (same consumer sequence as makeResident).
  Buffer r(256,{});auto rkeys=buffer_keys(r);r.current_=new BufferAllocation(11);start=buffer_count;
  auto &rs=r.residency(rkeys[2]);require(buffer_count-start==(baseline?rkeys.size():3),"residency-first requested buffer prefix");
  require(&rs==&r.view_(rkeys[2]).residency,"residency belongs to exact requested native view");
  requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;require(CheckResourceResidency(rs,21,requested),"residency-first emits");
  requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE;require(CheckResourceResidency(rs,21,requested) && requested==(DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ|DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE),"residency read/write membership merges");
  requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;require(CheckResourceResidency(rs,22,requested),"new encoder emits again");
  check_buffer(r,*r.current_);
}
static void buffer_native_arguments() {
  ++scenarios;
  for(bool gpu:{false,true})for(bool family:{false,true}) {
    apple8=family;Flags<BufferAllocationFlag> flags;if(gpu)flags.set(BufferAllocationFlag::GpuPrivate);
    Buffer b(256,{});auto keys=buffer_keys(b);b.current_=new BufferAllocation(12,flags);auto start=buffer_count;b.view_(keys.back());
    for(unsigned i=0;i<keys.size();++i) {
      const auto &e=buffer_events[start+i];auto fmt=b.viewDescriptors_[i].format;unsigned size=MTLGetTexelSize(fmt);
      auto usage=WMTTextureUsageShaderRead;if(gpu){usage|=WMTTextureUsageShaderWrite;if(fmt==WMTPixelFormatR32Uint||fmt==WMTPixelFormatR32Sint||(fmt==WMTPixelFormatRG32Uint&&family))usage|=WMTTextureUsageShaderAtomic;}
      require(e.parent==12 && e.offset==0 && e.row==1024 && e.info.type==WMTTextureTypeTextureBuffer && e.info.width==1024/size && e.info.height==1 && e.info.depth==1 && e.info.array_length==1 && e.info.mipmap_level_count==1 && e.info.sample_count==1 && e.info.pixel_format==fmt && e.info.options==WMTResourceStorageModeShared && e.info.usage==usage,"buffer native args exact format/parent/layout/options/usage");
    }
  }
  apple8=true;
}
static void buffer_rename_nil_lock() {
  ++scenarios;
  Buffer b(256,{});auto keys=buffer_keys(b);b.current_=new BufferAllocation(13);auto *old=&b.view_(keys[1]);auto oldall=b.current_;
  auto next=Rc<BufferAllocation>(new BufferAllocation(14));auto retained=b.rename(std::move(next));
  require(retained.ptr()==oldall.ptr() && &b.view_(keys[1],retained.ptr())==old,"buffer rename preserves explicit old allocation/replay");
  auto count=buffer_count;auto *fresh=&b.view_(keys[0]);require(fresh!=old && fresh->texture.handle!=old->texture.handle && buffer_count-count==(baseline?keys.size():1),"buffer renamed allocation independent prefix");
  Buffer nil(256,{});auto nk=buffer_keys(nil);nil.current_=new BufferAllocation(15);nil_next_view=true;count=buffer_count;
  auto *failed=&nil.view_(nk[0]);require(!failed->texture && failed->gpu_resource_id==0,"buffer native nil wrapper preserved");
  auto after=buffer_count;require(&nil.view_(nk[0])==failed && buffer_count==after && after-count==(baseline?nk.size():1),"buffer native nil remains cached without retry");
  Buffer locked(256,{});auto lk=buffer_keys(locked);locked.current_=new BufferAllocation(16);
  native_hook=[&]{bool excluded=false;std::thread writer([&]{if(locked.mutex_.try_lock())locked.mutex_.unlock();else excluded=true;});writer.join();require(excluded,"buffer descriptor lock excludes registration");};
  locked.view_(lk[0]);native_hook={};
}
static void buffer_exception_prefix() {
  ++scenarios;
  Buffer b(256,{});auto keys=buffer_keys(b);b.current_=new BufferAllocation(17);
  fail_new_after=1;bool threw=false;auto start=buffer_count;
  try{b.view_(keys[6]);}catch(const std::bad_alloc&){threw=true;}fail_new_after=-1;
  // First wrapper succeeds inline; second wrapper allocation fails after native
  // view creation. The temporary native reference must be released.
  require(threw && b.current_->version_==1 && b.current_->cached_view_.size()==1,"buffer partial wrapper failure publishes exact prefix");
  require(buffer_count==start+2 && native_refs[buffer_events[buffer_count-1].result]==0,"buffer failed wrapper releases temporary native view");
  auto *saved=b.current_->cached_view_[0].get();
  fail_new_after=1;threw=false;
  try{b.view_(keys[6]);}catch(const std::bad_alloc&){threw=true;}fail_new_after=-1;
  require(threw && b.current_->version_==1 && b.current_->cached_view_.size()==1,"buffer vector growth failure preserves exact prefix");
  require(native_refs[buffer_events[buffer_count-1].result]==0,"buffer vector growth failure releases wrapper/native view");
  start=buffer_count;b.view_(keys[6]);require(buffer_count-start==6 && b.current_->version_==7,"buffer retry resumes missing prefix only");
  require(b.current_->cached_view_[0].get()==saved,"buffer failure/retry retains earlier wrapper");check_buffer(b,*b.current_);
}
static TextureViewKey texture_key(Texture &t,WMTTextureType type,unsigned array=1,WMTPixelFormat fmt=WMTPixelFormatRGBA8Unorm) {
  return t.createView({.format=fmt,.type=type,.firstMiplevel=1,.miplevelCount=2,.firstArraySlice=1,.arraySize=array});
}
static void texture_memo() {
  ++scenarios;
  for(auto pair:{std::pair{WMTTextureType1D,WMTTextureType1DArray},std::pair{WMTTextureType2D,WMTTextureType2DArray},std::pair{WMTTextureType2DMultisample,WMTTextureType2DMultisampleArray},std::pair{WMTTextureTypeCube,WMTTextureTypeCubeArray}}) {
    Texture t;auto key=texture_key(t,pair.second,12);auto single=t.checkViewUseArray(key,false);auto d=t.viewDescriptors_[single.index];
    require(d.type==pair.first && d.arraySize==(pair.first==WMTTextureTypeCube?6:1) && d.firstMiplevel==1 && d.miplevelCount==2 && d.firstArraySlice==1,"array conversion preserves exact normalized descriptor");
    auto comparisons=descriptor_comparisons.load();auto size=t.version_;
    for(unsigned i=0;i<1000;++i) require(uint64_t(t.checkViewUseArray(key,false))==uint64_t(single),"warm array memo exact key");
    warm_comparisons+=descriptor_comparisons.load()-comparisons;
    require((descriptor_comparisons.load()>comparisons)==baseline && t.version_==size,"warm array memo avoids descriptor comparisons and registration");
    auto round=t.checkViewUseArray(single,true);require(round.index!=key.index && t.viewDescriptors_[round.index].arraySize==d.arraySize,"array conversion is not reciprocal for multislice view");
    require(uint64_t(t.checkViewUseArray(key,true))==uint64_t(key),"matching array request returns original key");
    auto formatted=t.checkViewUseFormat(key,WMTPixelFormatBGRA8Unorm);auto converted=t.checkViewUseArray(formatted,false);
    require(t.viewDescriptors_[converted.index].format==WMTPixelFormatBGRA8Unorm && converted.index!=single.index,"format aliases never inherit old-format alternate memo");
    Texture other;auto otherkey=texture_key(other,pair.second,12,WMTPixelFormatBGRA8Unorm);auto otheralias=other.checkViewUseArray(otherkey,false);
    require(other.viewDescriptors_[otheralias.index].format==WMTPixelFormatBGRA8Unorm,"memo never crosses texture descriptor identity");
  }
}
static void texture_concurrent_registration() {
  ++scenarios;
  Texture t;auto key=texture_key(t,WMTTextureType2DArray,8);std::array<TextureViewKey,8> aliases;std::atomic<unsigned> ready=0;std::atomic<bool> go=false;
  std::vector<std::thread> threads;for(unsigned i=0;i<8;++i)threads.emplace_back([&,i]{++ready;while(!go.load())std::this_thread::yield();aliases[i]=t.checkViewUseArray(key,false);});
  while(ready.load()!=8)std::this_thread::yield();go=true;for(auto &th:threads)th.join();
  for(auto alias:aliases)require(uint64_t(alias)==uint64_t(aliases[0]),"concurrent cold conversions deduplicate exact descriptor");
  require(t.version_==3,"concurrent cold conversions register one alias");
  auto comparison=descriptor_comparisons.load();for(unsigned i=0;i<1000;++i)t.checkViewUseArray(key,false);
  require((descriptor_comparisons.load()>comparison)==baseline,"warm concurrent alias stops descriptor scans");
  auto alloc=Rc<TextureAllocation>(new TextureAllocation(&t,18));t.current_=alloc;auto *old=&t.view(aliases[0]);auto next=Rc<TextureAllocation>(new TextureAllocation(&t,19));auto retained=t.rename(std::move(next));
  require(&t.view(aliases[0],retained.ptr())==old && &t.view(aliases[0])!=old,"memo key valid across allocation rename/replay");
}
static int benchmark() {
  for(unsigned registry:{2u,128u}) {
    Texture t;TextureViewKey key;
    for(unsigned i=0;i<registry;++i)key=t.createView({.format=WMTPixelFormatR32Uint,.type=WMTTextureType2DArray,.firstMiplevel=0,.miplevelCount=1,.firstArraySlice=i,.arraySize=2});
    t.checkViewUseArray(key,false);uint64_t checksum=0;constexpr unsigned calls=200000;
    for(unsigned trial=0;trial<5;++trial){auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<calls;++i)checksum+=uint64_t(t.checkViewUseArray(key,false));auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();std::cout<<"BENCH "<<registry<<" "<<calls<<" "<<ns<<" "<<checksum<<"\n";}
  }return 0;
}
int main(int argc,char **argv) {
  baseline=argc>1&&std::string(argv[1])=="baseline";
  if(argc>1&&std::string(argv[1])=="bench")return benchmark();
  if(argc>1&&std::string(argv[1])=="huge-key") {
    Buffer b(256,{});buffer_keys(b);b.current_=new BufferAllocation(20);
    b.view_(BufferViewKey(~0u,std::nullopt));return 2;
  }
  try {
    buffer_prefix_and_residency();buffer_native_arguments();buffer_rename_nil_lock();if(!baseline)buffer_exception_prefix();texture_memo();texture_concurrent_registration();
    require(std::all_of(native_refs.begin(),native_refs.end(),[](unsigned n){return !n;}),"all native view/allocation references released");
    std::cout<<"PASS "<<scenarios<<" actual-owner scenarios: buffer native calls first index1/8="<<(baseline?8:2)<<"; array warm descriptor comparisons="<<warm_comparisons<<"; rename, residency, nil, exceptions, concurrent registration\n";return 0;
  }catch(const std::exception &e){fail_new_after=-1;native_hook={};std::cerr<<e.what()<<"\n";return 1;}
}
