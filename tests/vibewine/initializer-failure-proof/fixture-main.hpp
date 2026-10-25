using namespace dxmt;
WMT::Device dev(){WMT::Device d;d.handle=7;return d;}
void poison(ResourceInitializer &r){memset(r.cpu_command_heap,0xA5,r.cpu_command_heap_size);}
void print(const char *name,uint64_t returned){std::cout<<name<<" returned="<<returned<<" signaled="<<obs.signal<<" commits="<<obs.commits<<" fills="<<obs.fills<<" copies="<<obs.copies<<" clears="<<obs.clears<<" invalid="<<obs.invalid<<" null="<<obs.null_handles<<" buffer_calls="<<obs.new_buffers<<"\n";}
int main(){
 Texture t;TextureAllocation ta;BufferAllocation ba;char data[256]={};
 {obs={};ResourceInitializer r(dev());poison(r);unsigned submitted=0;while(obs.commits==0&&submitted<100000){r.initWithZero(&ba,0,16);++submitted;}r.flushToWait();assert(obs.commits==1&&submitted==obs.fills+1);print("natural_cpu_exhaustion_one_lost",r.current_seq_id_);std::cout<<"natural_submitted="<<submitted<<" encoded="<<obs.fills<<"\n";}
 for(int kind=0;kind<5;++kind){obs={};ResourceInitializer r(dev());poison(r);r.cpu_command_heap_offset=r.cpu_command_heap_size;
 uint64_t s=kind==0?r.initWithZero(&ba,0,16):kind==1?r.initDepthStencilWithZero(&t,&ta,0,0,3):kind==2?r.initRenderTargetWithZero(&t,&ta,0,0):kind==3?r.initWithZero(&t,&ta,0,0):r.initWithData(&t,&ta,0,0,data,16,64,0);
 assert(s==2&&obs.commits==1);assert(r.flushToWait()==0);assert(obs.fills+obs.copies+obs.clears==0);print(("cpu_full_path_"+std::to_string(kind)).c_str(),s);
 }
 {obs={};ResourceInitializer r(dev());poison(r);for(int i=0;i<29;++i)assert(r.retainAllocation(&ba));r.cpu_command_heap_offset=r.cpu_command_heap_size;auto s=r.initWithZero(&ba,0,16);assert(s==2&&r.flushToWait()==0&&obs.fills==0);print("ref_storage_full",s);}
 {obs={};obs.fail_buffer_at=1;ResourceInitializer r(dev());poison(r);auto s=r.initWithData(&t,&ta,0,0,data,16,64,0);assert(s==2&&obs.invalid==1&&obs.commits==1);print("upload_first_fail",s);auto calls=obs.new_buffers;r.initWithData(&t,&ta,0,0,data,16,64,0);assert(obs.new_buffers==calls&&obs.invalid==2);print("failed_ring_block_reused",r.current_seq_id_);}
 {obs={};ResourceInitializer r(dev());poison(r);t.format=WMTPixelFormatDepth32Float_Stencil8;r.cpu_command_heap_offset=r.cpu_command_heap_size-sizeof(wmtcmd_blit_copy_from_buffer_to_texture_withblitoption);auto s=r.initWithData(&t,&ta,0,0,data,32,128,0);assert(obs.invalid==1&&s==2);print("depth_second_command_full",s);}
 for(int fail=1;fail<=2;++fail){obs={};obs.fail_buffer_at=fail;ResourceInitializer r(dev());poison(r);t.w=4096;t.h=2049;auto s=r.initWithData(&t,&ta,0,0,data,32768,0,0);assert(s==2&&obs.invalid==2&&obs.updates==0);print(fail==1?"depth_first_upload_fail":"depth_second_upload_fail",s);}
 {obs={};ResourceInitializer r(dev());poison(r);t={};r.initWithZero(&t,&ta,0,0);r.flushToWait();t.w=8;auto s=r.initWithZero(&t,&ta,0,0);assert(s==4&&obs.commits==3&&obs.copies==1&&r.flushToWait()==0);print("zero_growth_drops_copy",s);}
 {obs={};obs.fail_all=true;ResourceInitializer r(dev());poison(r);t={};auto s=r.initWithZero(&t,&ta,0,0);assert(s==2&&obs.fills==1&&obs.null_handles==1);print("zero_creation_null_published",s);r.initWithZero(&t,&ta,0,0);assert(obs.new_buffers==1);print("zero_null_cached",r.current_seq_id_);}
 {obs={};ResourceInitializer r(dev());poison(r);t={};r.initWithZero(&ba,0,16);r.initDepthStencilWithZero(&t,&ta,0,0,3);r.initRenderTargetWithZero(&t,&ta,0,0);r.initWithZero(&t,&ta,0,0);r.initWithData(&t,&ta,0,0,data,16,64,0);t.format=WMTPixelFormatDepth32Float_Stencil8;r.initWithData(&t,&ta,0,0,data,32,128,0);auto s=r.flushToWait();assert(s==1&&obs.fills==2&&obs.copies==4&&obs.clears==2&&obs.invalid==0&&obs.null_handles==0);print("success_control_all_paths",s);}
 std::cout<<"KNOWN DEFECTS REPRODUCED: 16 bounded observations; NOT correctness acceptance; ASan+UBSan enabled\n";
}
