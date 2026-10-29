// Appended to the complete production initializer/ring/tracker and the existing
// unchanged transaction cases. Native completion is independently controlled.
constexpr size_t full_block = kResourceInitializerGpuUploadHeapSize;
void upload(ResourceInitializer &r, TextureAllocation &ta, unsigned char value) {
  Texture t; t.w=8192; t.h=1024;
  std::vector<unsigned char> data(full_block,value);
  success(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));
}
void complete(uint64_t sequence) {
  assert(sequence>=obs.signal);
  for(auto it=obs.delayed.begin();it!=obs.delayed.end();) {
    if(it->signal>sequence) {++it;continue;}
    for(auto &read:it->reads)
      assert(payload_hash(read.payload)==read.hash && "inflight upload overwritten before completion");
    it=obs.delayed.erase(it);
  }
  obs.signal=sequence;
}
void async_growth() {
  obs={};obs.delay=true;obs.capture_payload=true;TextureAllocation ta;ResourceInitializer r(dev());
  upload(r,ta,0x31);assert(r.flushToWait()==1&&obs.signal==0);
  upload(r,ta,0x52);assert(r.flushToWait()==2&&obs.signal==0);
  assert(obs.new_buffers==2);
  complete(2);auto reads=obs.event_reads;
  upload(r,ta,0x73);
  std::cout<<"OBS async native_allocations="<<obs.new_buffers<<" expected=2\n";
  assert(obs.new_buffers==2 && "completed storage must prevent a third native 32MiB allocation");
  assert(obs.event_reads==reads+1);
  assert(r.flushToWait()==3);complete(3);
  assert(obs.encoded.size()==3&&obs.encoded[2].src==obs.encoded[0].src);
  assert(obs.encoded[1].src!=obs.encoded[2].src);
  checked("async_completed_before_successful_growth_3_to_2_native_allocations");
}
void latest_completed() {
  obs={};obs.delay=true;obs.capture_payload=true;TextureAllocation ta;ResourceInitializer r(dev());
  upload(r,ta,0x11);r.flushToWait();complete(1);
  upload(r,ta,0x22);
  std::cout<<"OBS latest native_allocations="<<obs.new_buffers<<" expected="<<(INCLUSIVE?1:2)<<"\n";
  assert(obs.new_buffers==(INCLUSIVE?1u:2u));
  r.flushToWait();complete(2);
  assert((obs.encoded[0].src==obs.encoded[1].src)==INCLUSIVE);
  checked("inclusive_latest_vs_exclusive_previous_frontier");
}
void pending_payloads() {
  obs={};obs.delay=true;obs.capture_payload=true;TextureAllocation ta;ResourceInitializer r(dev());
  upload(r,ta,0xA1);r.flushToWait();
  upload(r,ta,0xB2); // submitted seq1 is still in flight
  upload(r,ta,0xC3); // pending seq2 already owns its full previous block
  assert(obs.delayed.size()==1);r.flushToWait();complete(2);
  assert(obs.new_buffers==3);
  assert(obs.encoded[0].src!=obs.encoded[1].src&&obs.encoded[1].src!=obs.encoded[2].src);
  checked("inflight_and_pending_full_payloads_remain_readable");
}
void bounded_reads() {
  obs={};obs.delay=true;TextureAllocation ta;ResourceInitializer r(dev());
  Texture t;std::array<unsigned char,64> data{};
  for(int i=0;i<128;++i)success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));
  assert(obs.event_reads==0);r.flushToWait();assert(obs.event_reads==1);
  for(int i=0;i<128;++i)success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));
  assert(obs.event_reads==2 && "only one speculative read per generation");
  r.flushToWait();assert(obs.event_reads==3);
  success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));assert(obs.event_reads==4);
  checked("zero_reads_before_submission_one_per_pending_generation");
}
void no_read_when_fresh() {
  obs={};TextureAllocation ta;ResourceInitializer r(dev());Texture t;std::array<unsigned char,64> data{};
  success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));r.flushToWait();auto reads=obs.event_reads;
  for(int i=0;i<128;++i)success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));
  assert(obs.event_reads==reads&&r.cached_coherent_seq_id==1);
  checked("already_fresh_frontier_does_not_poll_or_regress");
}
void failure_retry() {
  obs={};obs.delay=true;obs.capture_payload=true;TextureAllocation ta;ResourceInitializer r(dev());
  upload(r,ta,0x11);r.flushToWait();upload(r,ta,0x22);r.flushToWait();
  // The first gen3 reservation polls stale0, succeeds by appending a small
  // span to a third backing, then completion advances within the generation.
  Texture t;std::array<unsigned char,64> data{};
  success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));assert(obs.new_buffers==3);
  complete(2);obs.fail_all=true;auto reads=obs.event_reads;
  upload(r,ta,0x33); // failed native growth, flush valid small prefix, retry
  assert(obs.event_reads==reads+1&&obs.new_buffers==4&&obs.commits==3);
  obs.fail_all=false;r.flushToWait();complete(4);
  checked("completion_after_generation_poll_keeps_finite_oom_retry");
}
void submission_failure() {
  obs={};obs.delay=true;obs.capture_payload=true;TextureAllocation ta;ResourceInitializer r(dev());
  upload(r,ta,0x11);r.flushToWait();upload(r,ta,0x22);
  auto reads=obs.event_reads;auto current=r.current_seq_id_;auto offset=r.cpu_command_heap_offset;
  obs.fail_blit=true;bool threw=false;try{r.flushToWait();}catch(const MTLD3DError&){threw=true;}
  assert(threw&&obs.event_reads==reads&&r.current_seq_id_==current&&r.cpu_command_heap_offset==offset);
  obs.fail_blit=false;Texture t;std::array<unsigned char,64> data{};
  success(r.initWithData(&t,&ta,0,0,data.data(),16,0,0));assert(obs.event_reads==reads);
  r.flushToWait();complete(2);
  assert(obs.commits==2&&obs.copies==3);
  checked("encoder_failure_keeps_generation_prefix_and_poll_budget");
}
int main() {
  std::cout<<std::unitbuf;
  if(CASE==0) {
    run_existing_transaction_cases();
    async_growth();latest_completed();pending_payloads();bounded_reads();no_read_when_fresh();
    failure_retry();submission_failure();
  } else if(CASE==1)latest_completed();
  else if(CASE==2)pending_payloads();
  else if(CASE==3)bounded_reads();
  std::cout<<"COMPLETION REFRESH PASSED inclusive="<<INCLUSIVE<<"; actual production methods, deferred native CPU mock; no GPU acceptance\n";
}
