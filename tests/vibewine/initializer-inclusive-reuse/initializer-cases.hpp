// Compiled after the complete, unchanged production initializer and the old
// transaction fixture (whose main is renamed by the runner).
void equality_upload(bool deny_new) {
  obs={}; Texture t; TextureAllocation ta; ResourceInitializer r(dev()); poison(r);
  t.w=8192; t.h=1024; std::array<unsigned char,64> data{};
  // Metadata-only native mock: capture_updates=false avoids reading this tiny
  // source as a 32 MiB payload. Real payload readback is a separate GPU gate.
  success(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));
  assert(obs.new_buffers==1 && r.flushToWait()==1);
  assert(r.cached_coherent_seq_id==1 && r.current_seq_id_==2);
  auto first=obs.encoded.at(0).src;
  obs.fail_all=deny_new;
  auto second=r.initWithData(&t,&ta,0,0,data.data(),32768,0,0);
  if (INCLUSIVE || !deny_new) {
    success(second); assert(r.flushToWait()==2 && obs.copies==2);
    assert(obs.new_buffers==(INCLUSIVE?1u:2u));
    assert((obs.encoded.at(1).src==first)==INCLUSIVE);
  } else {
    failure(second); assert(r.idle() && obs.copies==1 && obs.new_buffers==2);
  }
  std::cout << "PASS equality_upload deny_new=" << deny_new
            << " inclusive=" << INCLUSIVE << " native_calls=" << obs.new_buffers << "\n";
}
void stale_and_pending_uploads() {
  for (bool pending : {false,true}) {
    obs={}; Texture t; TextureAllocation ta; ResourceInitializer r(dev()); poison(r);
    t.w=8192; t.h=1024; std::array<unsigned char,64> data{};
    success(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));
    if (!pending) { r.flushToWait(); r.cached_coherent_seq_id=0; obs.signal=0; }
    obs.fail_all=true;
    // Pending path may submit A and retry only after completion. Keep the mock
    // signal stale on the idle path, so it must preserve A's backing and fail.
    if (pending) {
      std::array<ResourceInitializer::UploadHeap::Reservation,1> out{};
      assert(!r.gpu_command_heap_allocator.allocateChecked(
          1,ResourceInitializer::uploadReuseBoundary(1,0),std::array<size_t,1>{32*1024*1024},256,out));
      assert(!out[0].block && !r.idle() && obs.commits==0 && obs.updates==1);
    } else {
      failure(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));
      assert(r.idle() && obs.copies==1 && obs.updates==1);
    }
  }
  std::cout << "PASS stale_and_pending_upload_backing_protected\n";
}
void refreshed_completion_upload() {
  obs={}; Texture t; TextureAllocation ta; ResourceInitializer r(dev()); poison(r);
  t.w=8192; t.h=1024; std::array<unsigned char,64> data{};
  success(r.initWithData(&t,&ta,0,0,data.data(),32768,0,0));
  r.flushToWait(); auto first=obs.encoded.at(0).src;
  r.cached_coherent_seq_id=0; obs.fail_all=true;
  auto result=r.initWithData(&t,&ta,0,0,data.data(),32768,0,0);
  if(INCLUSIVE) {
    success(result); r.flushToWait();
    assert(obs.copies==2 && obs.encoded.at(1).src==first && obs.new_buffers==2);
  } else { failure(result); assert(obs.copies==1 && obs.new_buffers==3); }
  std::cout << "PASS completion_refresh_retry inclusive=" << INCLUSIVE << "\n";
}
void equality_two_plane_upload() {
  obs={}; Texture t; TextureAllocation ta; ResourceInitializer r(dev()); poison(r);
  std::array<size_t,2> sizes{kResourceInitializerGpuUploadHeapSize,kResourceInitializerGpuUploadHeapSize};
  std::array<ResourceInitializer::UploadHeap::Reservation,2> spans{};
  assert(r.gpu_command_heap_allocator.allocateChecked(1,0,sizes,256,spans));
  auto first=spans[0].block->buffer.handle, second=spans[1].block->buffer.handle;
  assert(first!=second && obs.new_buffers==2);
  r.cached_coherent_seq_id=1; r.current_seq_id_=2; obs.signal=1; obs.fail_all=true;
  t.w=4096; t.h=2048; t.format=WMTPixelFormatDepth32Float_Stencil8;
  std::vector<unsigned char> source(size_t(t.w)*t.h*8);
  auto result=r.initWithData(&t,&ta,0,0,source.data(),size_t(t.w)*8,0,0);
  if(INCLUSIVE) {
    success(result); assert(obs.new_buffers==2); r.flushToWait();
    assert(obs.copies==2 && obs.encoded[0].src==first && obs.encoded[1].src==second);
    assert(obs.encoded[0].offset==0 && obs.encoded[1].offset==0);
    assert(obs.encoded[0].length==32*1024*1024 && obs.encoded[1].length==8*1024*1024);
  } else { failure(result); assert(obs.updates==0 && obs.copies==0 && r.idle()); }
  std::cout << "PASS equality_two_plane_upload_exact_backing inclusive=" << INCLUSIVE << "\n";
}
int main() {
  run_existing_transaction_cases();
  equality_upload(false); equality_upload(true); stale_and_pending_uploads();
  refreshed_completion_upload(); equality_two_plane_upload();
}
