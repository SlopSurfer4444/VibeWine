using namespace dxmt;
void check(bool value,const char *name) { if(!value) {std::cerr<<"behavioral assertion: "<<name<<"\n";std::exit(1);} }
WMT::Device dev(){WMT::Device d;d.handle=7;return d;}
void success(InitResult r){check(r.status==InitStatus::Success&&r.sequence>0,"successful sequence");}
void failure(InitResult r){check(r.status==InitStatus::OutOfMemory&&r.sequence==0,"OOM result");}
unsigned cases=0; bool legacy=false;
void passed(const char *name){++cases;std::cout<<"PASS "<<name<<" updates="<<obs.updates<<" commits="<<obs.commits<<"\n";}
uint32_t bits(float value){uint32_t result;memcpy(&result,&value,4);return result;}
void payload(bool d24,unsigned width,unsigned height,unsigned level,unsigned slice,size_t pitch_extra) {
 obs={};Texture t;t.w=width;t.h=height;t.format=WMTPixelFormatDepth32Float_Stencil8;
 const unsigned w=std::max(1u,(level<32?width>>level:1u)), h=std::max(1u,(level<32?height>>level:1u));
 const size_t pixel=d24?4:8, pitch=w*pixel+pitch_extra;
 // Deliberately unaligned source with distinct guards, source row padding,
 // D24 boundaries and D32 NaN payloads/signed zeros/infinities/subnormals.
 const std::array<uint32_t,16> depths{0,1,2,0x7fffff,0x800000,0xfffffe,0xffffff,0x123456,0x7fc01234,0xff800000,0x80000000,0x7f800000,0x7f800001,0xffffffff,0x00000001,0x3f000001};
 std::vector<unsigned char> source(2+h*pitch,0xA7);auto *data=source.data()+1;
 std::vector<unsigned char> golden_depth(size_t(w)*h*4),golden_stencil(size_t(w)*h);
 for(unsigned row=0;row<h;++row)for(unsigned x=0;x<w;++x){size_t i=size_t(row)*w+x;
  uint32_t v=depths[(i+slice)%depths.size()];uint8_t stencil=uint8_t(0x31+i*29+slice);
  if(d24){v=(v&0xffffff)|(uint32_t(stencil)<<24);memcpy(data+row*pitch+x*pixel,&v,4);
   // Independent scalar reference: exact integer numerator, double quotient,
   // then the correctly rounded float result. No reciprocal multiplication.
   uint32_t result=bits(float(double(v&0xffffff)/16777215.0));memcpy(golden_depth.data()+i*4,&result,4);
  }else{memcpy(data+row*pitch+x*pixel,&v,4);data[row*pitch+x*pixel+4]=stencil;memcpy(golden_depth.data()+i*4,&v,4);}
  golden_stencil[i]=stencil;
 }
 auto source_before=source;TextureAllocation ta;ResourceInitializer r(dev());
 // Seed the same staging owner so plane offsets are nonzero and must not
 // overwrite a preceding live allocation or each other's alignment padding.
 std::array<size_t,1> seed_size{43};std::array<ResourceInitializer::UploadHeap::Reservation,1> seed{};
 check(r.gpu_command_heap_allocator.allocateChecked(1,0,seed_size,256,seed),"seed reservation");
 success(r.initWithData(&t,&ta,slice,level,data,pitch,0,d24?MTL_DXGI_FORMAT_EMULATED_D24:0));
 check(!r.idle()&&obs.copies==0&&obs.commits==0,"pair pending before commit");
 const unsigned expected=legacy?2*w*h:2*h;
 check(obs.updates==expected,"bulk row bridge call count");
 check(obs.copied_bytes==uint64_t(w)*h*5,"exactly five payload bytes per texel");
 if(!legacy)check(obs.calloc_calls==1&&obs.calloc_bytes==size_t(w)*5&&obs.scratch_frees==1&&!obs.scratch_ptr,"one bounded owned scratch allocation");
 // Force a failed native encoder after fill. Both pending planes and their
 // exact bytes survive until retry, and scratch need not remain live.
 const auto cursor=r.cpu_command_heap_offset;auto *head=r.blit_cmd_head.next.ptr;
 obs.fail_blit=true;bool threw=false;try{r.flushToWait();}catch(const MTLD3DError&){threw=true;}
 check(threw&&obs.commits==0&&r.cpu_command_heap_offset==cursor&&r.blit_cmd_head.next.ptr==head&&ta.releases==0,"failed submit preserves pair and destination hold");
 obs.fail_blit=false;obs.planes.clear();r.flushToWait();check(obs.copies==2&&obs.planes.size()==2&&ta.releases==1,"two committed blits and destination release");
 for(unsigned plane=0;plane<2;++plane){const auto &c=obs.planes[plane].command;const size_t logical=plane?w:size_t(w)*4;
  const size_t stride=plane?(size_t(w)+3)&~size_t(3):size_t(w)*4;
  check(c.type==WMTBlitCommandCopyFromBufferToTextureWithBlitOption&&c.dst==ta.texture().handle&&c.src_offset>0&&c.bytes_per_row==stride&&c.bytes_per_image==0&&c.size.width==w&&c.size.height==h&&c.size.depth==1&&c.slice==slice&&c.level==level&&c.origin.x==0&&c.origin.y==0&&c.origin.z==0&&c.options==(plane?WMTBlitOptionStencilFromDepthStencil:WMTBlitOptionDepthFromDepthStencil),"plane metadata/stride/mip/slice/options");
  auto &actual=obs.bytes.at(c.src);auto &golden=plane?golden_stencil:golden_depth;
  for(unsigned row=0;row<h;++row){check(!memcmp(actual.data()+c.src_offset+row*stride,golden.data()+row*logical,logical),"exact depth/stencil payload");
   for(size_t p=logical;p<stride;++p)check(actual[c.src_offset+row*stride+p]==0xCD,"stencil padding untouched");}
 }
 // Validate every other byte in the allocator block, not just near guards.
 auto &buffer=obs.bytes.at(obs.planes[0].command.src);
 for(size_t p=0;p<buffer.size();++p){bool written=false;for(auto &v:obs.planes){const auto &c=v.command;if(c.src!=obs.planes[0].command.src)continue;
   if(p>=c.src_offset&&p<c.src_offset+c.bytes_per_row*h){const size_t col=(p-c.src_offset)%c.bytes_per_row;written|=col<(c.options==WMTBlitOptionDepthFromDepthStencil?size_t(w)*4:w);}}
  if(!written)check(buffer[p]==0xCD,"all allocator sentinels preserved");}
 check(source==source_before,"caller source/guards/padding preserved");passed(d24?"D24 data/rows/padding/mips/slices":"D32 bit patterns/rows/padding/mips/slices");
}
void scratch_failure(){
 obs={};Texture t;t.format=WMTPixelFormatDepth32Float_Stencil8;BufferAllocation ba;TextureAllocation ta;ba.incRef();ta.incRef();
 {ResourceInitializer r(dev());success(r.initWithZero(&ba,17,13));ba.decRef();check(ba.releases==0,"pending A owns the sole remaining reference");const auto cursor=r.cpu_command_heap_offset;auto *head=r.blit_cmd_head.next.ptr;
  r.cpu_command_heap_offset=r.cpu_command_heap_size;obs.fail_calloc=true;unsigned char source[128]{};
  failure(r.initWithData(&t,&ta,3,0,source,32,0,0));
  check(obs.calloc_calls==1&&obs.updates==0&&obs.new_buffers==0&&obs.commits==0&&r.blit_cmd_head.next.ptr==head&&r.cpu_command_heap_offset==r.cpu_command_heap_size&&ba.releases==0&&ta.releases==0,"actual calloc failure preserves pending/full CPU heap and holds");
  ta.decRef();check(ta.releases==1,"failed B acquires no pending hold");
  obs.fail_calloc=false;r.cpu_command_heap_offset=cursor;success(r.initWithZero(&ba,33,13));r.flushToWait();check(obs.fills==2&&obs.copies==0&&ba.releases==1,"valid A/failed B/valid C exact prefix and pending hold release");
 }check(ba.releases==1&&ta.releases==1,"holds released exactly once");passed("actual calloc failure, full heap, pending holds and recovery");
}
void bad_source_range(){obs={};Texture t;t.h=3;t.format=WMTPixelFormatDepth32Float_Stencil8;TextureAllocation ta;ResourceInitializer r(dev());unsigned char data[32]{};
 failure(r.initWithData(&t,&ta,0,0,data,SIZE_MAX,0,0));check(obs.calloc_calls==0&&obs.updates==0&&obs.new_buffers==0&&r.idle(),"overflow before allocation or publication");passed("source-pitch overflow before scratch/reserve");}
void upload_failure(){for(int fail=1;fail<=2;++fail){obs={};Texture t;t.w=4096;t.h=2049;t.format=WMTPixelFormatDepth32Float_Stencil8;TextureAllocation ta;BufferAllocation ba;ResourceInitializer r(dev());unsigned char data[8]{};
 success(r.initWithZero(&ba,11,16));obs.fail_buffer_at=fail;failure(r.initWithData(&t,&ta,0,0,data,32768,0,0));check(obs.updates==0&&(legacy||obs.scratch_frees==1)&&obs.new_buffers<=unsigned(fail+1)&&obs.commits<=1,"both reservations before any fill");
 success(r.initWithZero(&ba,33,16));r.flushToWait();check(obs.fills==2&&obs.copies==0,"failed reservation publishes no plane");passed(fail==1?"first plane reserve failure":"second plane reserve failure");}}
int main(int argc,char **argv){legacy=argc>1&&std::string(argv[1])=="legacy";
 for(bool d24:{true,false})for(auto width:{3u,1u,2u,4u,5u,7u,19u})payload(d24,width,5,0,2,13);
 for(bool d24:{true,false})for(unsigned level:{1u,2u,5u})payload(d24,23,13,level,7,5);
 bad_source_range();upload_failure();if(!legacy)scratch_failure();
 std::cout<<"BULK DEPTH/STENCIL PASSED cases="<<cases<<" mode="<<(legacy?"legacy":"candidate")<<" CPU/mock native C API only\n";
}
