using namespace dxmt;
WMT::Device dev(){WMT::Device d;d.handle=7;return d;}
void poison(ResourceInitializer &r){if(r.cpu_command_heap)memset(r.cpu_command_heap,0xA5,r.cpu_command_heap_size);}
void success(InitResult r){assert(r.status==InitStatus::Success&&r.sequence>0&&static_cast<bool>(r));}
void failure(InitResult r,InitStatus status=InitStatus::OutOfMemory){assert(r.status==status&&r.sequence==0&&!static_cast<bool>(r));}
unsigned cases=0;
void checked(const char *name){assert(obs.invalid==0&&obs.null_handles==0);++cases;std::cout<<"PASS "<<name<<" fills="<<obs.fills<<" copies="<<obs.copies<<" clears="<<obs.clears<<" commits="<<obs.commits<<" buffer_calls="<<obs.new_buffers<<"\n";}
InitResult operation(ResourceInitializer &r,int kind,Texture &t,TextureAllocation &ta,BufferAllocation &ba,const void *data){
 switch(kind){case 0:return r.initWithZero(&ba,0,16);case 1:return r.initDepthStencilWithZero(&t,&ta,0,0,3);case 2:return r.initRenderTargetWithZero(&t,&ta,0,0);case 3:return r.initWithZero(&t,&ta,0,0);case 4:return r.initWithData(&t,&ta,0,0,data,16,64,0);default:t.format=WMTPixelFormatDepth32Float_Stencil8;return r.initWithData(&t,&ta,0,0,data,32,128,0);}
}
int main(){
 std::array<unsigned char,256> data{};
 {obs={};BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  for(unsigned i=0;i<18454;++i)success(r.initWithZero(&ba,i,16));
  r.flushToWait();assert(obs.fills==18454&&obs.commits==2&&obs.signal==2);
  for(unsigned i=0;i<18454;++i)assert(obs.encoded[i].offset==i);
  checked("natural_18454_fills_exactly_once");}
 for(int kind=0;kind<6;++kind){obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  success(r.initWithZero(&ba,11,16));r.cpu_command_heap_offset=r.cpu_command_heap_size;
  auto result=operation(r,kind,t,ta,ba,data.data());success(result);assert(result.sequence==2&&obs.commits==1);
  r.flushToWait();assert(obs.commits==2);assert(obs.fills==(kind==0||kind==3?2u:1u));assert(obs.clears==(kind==1||kind==2?1u:0u));assert(obs.copies==(kind==3||kind==4?1u:kind==5?2u:0u));
  checked(("cpu_full_retry_path_"+std::to_string(kind)).c_str());}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  for(unsigned i=0;i<29;++i)success(r.initWithZero(&ba,i,16));
  r.cpu_command_heap_offset=r.cpu_command_heap_size;success(r.initWithZero(&ba,29,16));r.flushToWait();assert(obs.fills==30&&obs.commits==2);checked("retention_storage_rollover");}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  success(r.initWithZero(&ba,11,16));obs.fail_all=true;failure(r.initWithData(&t,&ta,0,0,data.data(),16,64,0));assert(obs.commits<=1&&obs.updates==0);obs.fail_all=false;
  success(r.initWithZero(&ba,33,16));r.flushToWait();assert(obs.fills==2&&obs.copies==0&&obs.encoded[0].offset==11&&obs.encoded[1].offset==33);checked("valid_A_failed_B_valid_C");
  auto calls=obs.new_buffers;success(r.initWithData(&t,&ta,0,0,data.data(),16,64,0));r.flushToWait();assert(obs.copies==1&&obs.new_buffers>calls);checked("failed_native_block_not_cached");}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);t.format=WMTPixelFormatDepth32Float_Stencil8;
  success(r.initWithZero(&ba,11,16));r.cpu_command_heap_offset=r.cpu_command_heap_size-sizeof(wmtcmd_blit_copy_from_buffer_to_texture_withblitoption);
  success(r.initWithData(&t,&ta,0,0,data.data(),32,128,0));r.flushToWait();assert(obs.fills==1&&obs.copies==2&&obs.commits==2);
  const auto &depth=obs.encoded[1],&stencil=obs.encoded[2];assert(depth.src!=stencil.src||depth.offset+depth.length<=stencil.offset||stencil.offset+stencil.length<=depth.offset);
  checked("depth_stencil_cpu_pair_retry_and_disjoint_spans");}
 for(int fail=1;fail<=2;++fail){obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);t.format=WMTPixelFormatDepth32Float_Stencil8;t.w=4096;t.h=2049;
  success(r.initWithZero(&ba,11,16));obs.fail_buffer_at=fail;failure(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));assert(obs.new_buffers>=unsigned(fail)&&obs.new_buffers<=unsigned(fail+1)&&obs.updates==0&&obs.commits<=1);
  success(r.initWithZero(&ba,33,16));r.flushToWait();assert(obs.fills==2&&obs.copies==0);checked(fail==1?"depth_first_upload_fail_atomic":"depth_second_upload_fail_atomic");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);obs.fail_all=true;
  failure(r.initWithZero(&t,&ta,0,0));assert(r.zero_buffer_size_==0&&!r.zero_buffer_&&obs.commits==0&&obs.fills==0);auto calls=obs.new_buffers;
  failure(r.initWithZero(&t,&ta,0,0));assert(obs.new_buffers>calls&&r.zero_buffer_size_==0);obs.fail_all=false;success(r.initWithZero(&t,&ta,0,0));r.flushToWait();assert(obs.fills==1&&obs.copies==1);checked("zero_failure_never_cached_then_recover");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);
  success(r.initWithZero(&t,&ta,0,0));t.w=8;success(r.initWithZero(&t,&ta,0,0));r.flushToWait();assert(obs.fills==2&&obs.copies==2&&r.zero_buffer_size_>=128);checked("zero_growth_retains_both_copies");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);obs.fail_all=true;
  for(unsigned i=0;i<100;++i){failure(r.initWithData(&t,&ta,0,0,data.data(),16,64,0));failure(r.initWithZero(&t,&ta,0,0));}
  assert(obs.new_buffers<=200&&obs.commits==0&&obs.updates==0&&r.flushToWait()==0);checked("persistent_failure_bounded");}
 for(int fault=0;fault<3;++fault){obs={};if(fault==0)obs.fail_malloc=true;if(fault==1)obs.fail_queue=true;if(fault==2)obs.fail_event=true;Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  for(int kind=0;kind<6;++kind){t={};failure(operation(r,kind,t,ta,ba,data.data()));}assert(r.flushToWait()==0&&obs.commits==0&&obs.new_buffers==0&&obs.updates==0);checked(fault==0?"constructor_malloc_failure":fault==1?"constructor_queue_failure":"constructor_event_failure");}
 for(int fault=0;fault<3;++fault){obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  success(r.initWithZero(&ba,11,16));success(r.initRenderTargetWithZero(&t,&ta,0,0));auto sequence=r.current_seq_id_;auto offset=r.cpu_command_heap_offset;
  if(fault==0)obs.fail_command=true;if(fault==1)obs.fail_blit=true;if(fault==2)obs.fail_render=true;
  bool threw=false;try{r.flushToWait();}catch(const MTLD3DError &){threw=true;}assert(threw&&obs.commits==0&&r.current_seq_id_==sequence&&r.cpu_command_heap_offset==offset);
  r.cpu_command_heap_offset=r.cpu_command_heap_size;failure(r.initWithZero(&ba,22,16),InitStatus::SubmissionFailed);assert(obs.commits==0&&r.current_seq_id_==sequence);
  obs.fail_command=obs.fail_blit=obs.fail_render=false;success(r.initWithZero(&ba,33,16));r.flushToWait();assert(obs.fills==2&&obs.clears==1&&obs.encoded[0].offset==11&&obs.encoded[1].offset==33);
  checked(fault==0?"nil_command_preserves_prefix":fault==1?"nil_blit_preserves_prefix":"nil_render_preserves_prefix");}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);
  for(int kind=0;kind<6;++kind)success(operation(r,kind,t,ta,ba,data.data()));assert(r.flushToWait()==1&&obs.fills==2&&obs.copies==4&&obs.clears==2);checked("all_six_paths_success_control");}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);r.cpu_command_heap_offset=r.cpu_command_heap_size;
  for(int kind=0;kind<6;++kind){t={};failure(operation(r,kind,t,ta,ba,data.data()));}assert(obs.commits==0&&obs.updates==0&&r.flushToWait()==0);checked("empty_full_cpu_heap_failure_is_finite");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);
  success(r.initWithZero(&t,&ta,0,0));auto old=r.zero_buffer_.handle;auto size=r.zero_buffer_size_;obs.fail_all=true;t.w=8;failure(r.initWithZero(&t,&ta,0,0));assert(r.zero_buffer_.handle==old&&r.zero_buffer_size_==size&&obs.commits==0);obs.fail_all=false;t.w=4;success(r.initWithZero(&t,&ta,0,0));r.flushToWait();assert(obs.fills==1&&obs.copies==2);checked("zero_growth_failure_preserves_cache_and_prefix");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);t.w=t.h=t.d=std::numeric_limits<unsigned>::max();
  failure(r.initWithZero(&t,&ta,0,0));failure(r.initWithData(&t,&ta,0,0,data.data(),16,64,0));assert(obs.new_buffers==0&&obs.updates==0&&obs.commits==0);checked("texture_size_overflow_no_work");}
 {obs={};BufferAllocation ba;TextureAllocation ta;Texture t;ba.incRef();ta.incRef();
  {ResourceInitializer r(dev());poison(r);obs.fail_all=true;failure(r.initWithData(&t,&ta,0,0,data.data(),16,64,0));success(r.initWithZero(&ba,11,16));assert(ba.releases==0&&ta.releases==0);}
  ba.decRef();ta.decRef();assert(ba.releases==1&&ta.releases==1&&obs.commits==0);checked("pending_and_failed_retention_released");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);
  std::array<size_t,2> sizes{kResourceInitializerGpuUploadHeapSize,kResourceInitializerGpuUploadHeapSize};std::array<ResourceInitializer::UploadHeap::Reservation,2> spans{};
  assert(r.gpu_command_heap_allocator.allocateChecked(1,0,sizes,kResourceInitializerGpuUploadHeapAlignment,spans));assert(obs.new_buffers==2&&spans[0].block!=spans[1].block);
  r.cached_coherent_seq_id=2;r.current_seq_id_=3;obs.signal=2;obs.fail_all=true;t.w=4096;t.h=2048;t.format=WMTPixelFormatDepth32Float_Stencil8;
  std::vector<unsigned char> source(size_t(t.w)*t.h*8);success(r.initWithData(&t,&ta,0,0,source.data(),size_t(t.w)*8,0,0));
  assert(obs.new_buffers==2&&obs.updates==2*t.w*t.h);assert(r.flushToWait()==3&&obs.copies==2&&obs.fills==0&&obs.commits==1);
  assert(obs.encoded[0].src!=obs.encoded[1].src&&obs.encoded[0].offset==0&&obs.encoded[1].offset==0&&obs.encoded[0].length==32*1024*1024&&obs.encoded[1].length==8*1024*1024);checked("depth_stencil_reuses_two_completed_standard_blocks_without_native_allocate");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);t.h=3;
  failure(r.initWithData(&t,&ta,0,0,data.data(),std::numeric_limits<size_t>::max(),0,0));t.format=WMTPixelFormatDepth32Float_Stencil8;
  failure(r.initWithData(&t,&ta,0,0,data.data(),std::numeric_limits<size_t>::max(),0,0));assert(obs.new_buffers==0&&obs.updates==0&&obs.commits==0);checked("source_pitch_overflow_no_allocation");}
 {obs={};Texture t;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());poison(r);r.current_seq_id_=std::numeric_limits<uint64_t>::max();
  for(int kind=0;kind<6;++kind){t={};failure(operation(r,kind,t,ta,ba,data.data()),InitStatus::SubmissionFailed);}assert(r.idle()&&obs.commits==0&&obs.updates==0&&r.current_seq_id_==std::numeric_limits<uint64_t>::max());checked("sequence_exhaustion_no_success_or_wait_sentinel");}
 {obs={};Texture t;TextureAllocation ta;ResourceInitializer r(dev());poison(r);t.w=2;t.h=1;t.format=WMTPixelFormatDepth32Float_Stencil8;obs.capture_updates=true;
  std::array<uint32_t,2> packed{0xABFFFFFFu,0xCD000000u};success(r.initWithData(&t,&ta,0,0,packed.data(),8,0,MTL_DXGI_FORMAT_EMULATED_D24));r.flushToWait();assert(obs.copies==2&&obs.update_samples.size()==4);
  float first_depth=-1,last_depth=-1;std::memcpy(&first_depth,obs.update_samples[0].bytes.data(),4);std::memcpy(&last_depth,obs.update_samples[2].bytes.data(),4);
  assert(first_depth==1.0f&&last_depth==0.0f&&obs.update_samples[1].bytes[0]==0xAB&&obs.update_samples[3].bytes[0]==0xCD);checked("d24_planar_payload_bytes");}
 std::cout<<"TRANSACTION REGRESSION PASSED: "<<cases<<" cases; ASan+UBSan; actual production initializer/ring/tracker and Metal.hpp; mocked native C API, no GPU acceptance\n";
}
