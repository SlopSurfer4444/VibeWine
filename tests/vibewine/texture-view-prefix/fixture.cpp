#include "owners-under-test.hpp"
using namespace dxmt;
static bool baseline;
static unsigned scenarios;
static unsigned expected_prefix(unsigned request,unsigned registered) { return baseline?registered:request+1; }
static std::vector<TextureViewKey> register_views(Texture &texture,unsigned count=8) {
  std::vector<TextureViewKey> keys;
  for(unsigned i=0;i<count;++i) {
    TextureViewDescriptor descriptor{.format=i%2?WMTPixelFormatBGRA8Unorm:WMTPixelFormatRGBA8Unorm,
      .type=WMTTextureType2DArray,.firstMiplevel=i%4,.miplevelCount=1,.firstArraySlice=i/4,.arraySize=1};
    keys.push_back(texture.createView(descriptor));
  }
  return keys;
}
static Rc<TextureAllocation> allocation(Texture &t,obj_handle_t handle) { return new TextureAllocation(&t,handle); }
static void check_prefix(Texture &t,TextureAllocation &a) {
  require(a.version_==a.cached_view_.size(),"materialized prefix count equals cache size");
  for(unsigned i=0;i<a.version_;++i) {
    auto &view=*a.cached_view_[i];
    require(view.allocation==&a && view.key.index==i,"cached wrapper retains allocation and descriptor index");
    require(uint64_t(view.key)==uint64_t(TextureViewKey(t.viewDescriptors_[i],i,t.miplevelCount())),"view key exact mip/slice metadata");
    if(i) require(view.gpuResourceID==(view.texture.handle?view.texture.handle+10000:0),"native GPU resource ID retained");
    if(!i) require(view.texture.handle==a.texture().handle && view.gpuResourceID==a.gpuResourceID,"slot zero wraps original native texture");
  }
}
static void check_native_args(Texture &t,size_t event_begin,unsigned first_index,unsigned count,obj_handle_t parent) {
  for(unsigned i=0;i<count;++i) {
    const auto &event=native_events[event_begin+i];const auto &desc=t.viewDescriptors_[first_index+i];
    require(event.parent==parent && event.format==desc.format && event.type==desc.type &&
            event.first_level==desc.firstMiplevel && event.levels==desc.miplevelCount &&
            event.first_slice==desc.firstArraySlice && event.slices==desc.arraySize,"exact newTextureView arguments");
    require(event.swizzle.r==WMTTextureSwizzleRed && event.swizzle.g==WMTTextureSwizzleGreen &&
            event.swizzle.b==WMTTextureSwizzleBlue && event.swizzle.a==WMTTextureSwizzleAlpha,"original swizzle retained");
  }
}
static void low_high_epoch() {
  ++scenarios;
  Texture t;auto keys=register_views(t);t.current_=allocation(t,1);
  auto start=native_count;
  auto &low=t.view(keys[1]);auto *saved=&low;
  const auto prefix=expected_prefix(keys[1].index,t.version_);
  require(t.current_->version_==prefix && native_count-start==prefix-1,"fresh low request creates only required prefix");
  check_prefix(t,*t.current_);check_native_args(t,start,1,prefix-1,1);
  auto requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;
  require(CheckResourceResidency(low.residency,42,requested),"initial residency emission");
  auto count=native_count;
  require(&t.view(keys[0])==t.current_->cached_view_[keys[0].index].ptr(),"high then lower preserves slot identity");
  require(&t.view(keys[1])==saved && native_count==count,"repeat request does no native work");
  auto &high=t.view(keys.back());(void)high;
  require(t.current_->version_==t.version_ && native_count-start==t.version_-1,"highest index completes prefix once");
  require(&t.view(keys[1])==saved,"heap wrapper identity survives small-vector growth");
  requested=DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ;
  require(!CheckResourceResidency(t.view(keys[1]).residency,42,requested),"residency state survives prefix growth");
  count=native_count;
  auto changed=t.viewDescriptors_[1];changed.type=WMTTextureType2D;changed.arraySize=1;
  auto appended=t.createView(changed);
  require(t.createView(changed).index==appended.index,"descriptor aliases deduplicate");
  require(&t.view(keys[1])==saved,"unused descriptor epoch preserves old view object");
  require(native_count==count+(baseline?1:0),"new descriptor epoch does not force unused tail");
  auto &later=t.view(appended);require(later.key.index==appended.index,"later appended view resolves correct slot");
  require(native_count==count+1,"appended descriptor created once when requested");
  check_prefix(t,*t.current_);
}
static void high_first_and_generation() {
  ++scenarios;
  Texture t;auto keys=register_views(t);auto old=allocation(t,2);t.current_=old;
  auto start=native_count;auto *high=&t.view(keys.back());
  require(native_count-start==t.version_-1,"highest first matches predecessor native count");
  auto *low=&t.view(keys[0]);auto count=native_count;
  require(&t.view(keys.back())==high && native_count==count,"arbitrary high low order is stable");
  auto next=allocation(t,3);auto retained=t.rename(std::move(next));
  require(retained.ptr()==old.ptr() && t.current_.ptr()!=old.ptr(),"rename transfers independent allocation generations");
  auto *new_low=&t.view(keys[0]);
  require(new_low!=low && new_low->allocation==t.current_.ptr(),"new allocation owns independent wrappers");
  require(&t.view(keys[0],old.ptr())==low && native_count==count+(baseline?t.version_-1:keys[0].index),"explicit old-allocation lookup retains old cache");
  require(&t.view(keys.back(),old.ptr())==high,"old high view survives rename");
  check_prefix(t,*old);check_prefix(t,*t.current_);
}
static void alias_and_native_nil() {
  ++scenarios;
  Texture t;t.current_=allocation(t,4);auto keys=register_views(t,1);
  auto alias=t.checkViewUseArray(keys[0],false);
  require(alias.index==t.checkViewUseArray(keys[0],false).index,"array alias registration deduplicates");
  auto formatted=t.checkViewUseFormat(alias,WMTPixelFormatBGRA8Unorm);
  require(formatted.index==t.checkViewUseFormat(alias,WMTPixelFormatBGRA8Unorm).index,"format alias registration deduplicates");
  auto *view=&t.view(formatted);require(view->key.index==formatted.index,"format/array alias lookup uses exact descriptor");
  check_prefix(t,*t.current_);
  Texture nil;auto nilkeys=register_views(nil,1);nil.current_=allocation(nil,5);
  nil_next_view=true;
  auto start=native_count;auto *failed=&nil.view(nilkeys[0]);
  require(failed && !failed->texture && failed->gpuResourceID==0 && native_count==start+1,"native nil result keeps existing wrapper policy");
  require(&nil.view(nilkeys[0])==failed && native_count==start+1,"native nil wrapper remains cached without retry");
  check_prefix(nil,*nil.current_);
}
static void descriptor_lock() {
  ++scenarios;
  Texture t;auto keys=register_views(t,1);t.current_=allocation(t,6);
  native_hook=[&] {
    bool excluded=false;
    std::thread writer([&] {if(t.mutex_.try_lock()) t.mutex_.unlock();else excluded=true;});writer.join();
    require(excluded,"descriptor shared lock excludes concurrent registration during native creation");
  };
  t.view(keys[0]);native_hook={};
}
static void exception_prefix() {
  ++scenarios;
  Texture t;auto keys=register_views(t);t.current_=allocation(t,7);
  // Materialize only slot zero, with registered tail left untouched. The old
  // owner is tested by work-count controls rather than this candidate invariant.
  t.view(t.fullView);
  auto *original=t.current_->cached_view_[0].ptr();
  fail_new_after=0;
  bool threw=false;
  try {t.view(keys[6]);} catch(const std::bad_alloc &) {threw=true;}
  fail_new_after=-1;
  require(threw && t.current_->version_==1 && t.current_->cached_view_.size()==1,"wrapper allocation failure publishes no prefix entry");
  // Allow wrappers 1..4; the next allocation is growth beyond inline capacity4.
  // The temporary fourth native view must be released when that growth fails.
  auto begin=native_count;
  fail_new_after=4;threw=false;
  try {t.view(keys[6]);} catch(const std::bad_alloc &) {threw=true;}
  fail_new_after=-1;
  require(threw && t.current_->version_==4 && t.current_->cached_view_.size()==4,"partial append failure preserves exact prefix epoch");
  require(native_count==begin+4 && native_refs[native_events[native_count-1].result]==0,"failed vector growth releases temporary native view");
  auto *one=t.current_->cached_view_[1].ptr();
  t.view(keys[6]);
  require(t.current_->version_==keys[6].index+1 && t.current_->cached_view_.size()==keys[6].index+1,"retry resumes first missing descriptor");
  require(t.current_->cached_view_[0].ptr()==original && t.current_->cached_view_[1].ptr()==one,"partial-failure retry retains existing objects");
  check_prefix(t,*t.current_);
}
int main(int argc,char **argv) {
  baseline=argc>1 && std::string(argv[1])=="baseline";
  try {
    {Texture only;only.current_=allocation(only,8);auto before=native_count;only.view(only.fullView);
     require(native_count==before && only.current_->cached_view_.size()==1,"single descriptor needs no native view");}
    low_high_epoch();high_first_and_generation();alias_and_native_nil();descriptor_lock();
    if(!baseline) exception_prefix();
    require(std::all_of(native_refs.begin(),native_refs.end(),[](unsigned n){return !n;}),"all native references released");
    std::cout<<"PASS: "<<scenarios<<" production-owner scenarios; prefix counts/epochs, high-low requests, rename/old cache, aliases, residency, native nil, lock, allocation failure cleanup\n";
    return 0;
  } catch(const std::exception &error) {fail_new_after=-1;native_hook={};std::cerr<<error.what()<<"\n";return 1;}
}
