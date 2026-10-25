#include "mocks.hpp"
static bool baseline;
static unsigned scenarios;
static void verify_copy(bool expected_eligible, uint32_t width, uint32_t height, uint32_t depth, uint32_t src_row, uint32_t dst_row,
                        WMTOrigin src_origin={}, WMTOrigin dst_origin={}, uint32_t src_image=0,
                        uint32_t dst_image=0, bool distinct=true, bool same_format=true,
                        unsigned src_texel=4, unsigned dst_texel=4, uint32_t flags=0) {
  ++scenarios;
  src_image=src_image?src_image:src_row*(height+src_origin.y+2);
  dst_image=dst_image?dst_image:dst_row*(height+dst_origin.y+2);
  const uint64_t src_length=src_image*(depth+src_origin.z+1), dst_length=dst_image*(depth+dst_origin.z+1);
  Resource src{new StagingResource(1,src_row,src_image,src_length,64)};
  Resource dst{new StagingResource(distinct?2:1,dst_row,dst_image,dst_length,128)};
  TextureCopyCommand command;
  command.pSrc=&src;command.pDst=&dst;command.SrcOrigin=src_origin;command.DstOrigin=dst_origin;
  command.SrcSize={width,height,depth};command.SrcFormat={flags,src_texel};command.DstFormat={flags,dst_texel};
  command.Dst.Format=same_format?1:2;
  const auto initial_errors=errors;
  FixtureContext owner;owner.CopyTextureBitcast(std::move(command));
  require(owner.events==std::vector<std::string>{"switch","destination","source","emit"},"encoder/use/capture ordering");
  require(owner.promote_flush && owner.queued.size()==1,"flush and one deferred owner operation");
  require(live_staging==2,"captured owners exist");
  // The command's moved captures own both staging resources after callers drop them.
  src.staging=nullptr;dst.staging=nullptr;
  require(live_staging==2,"queued captures retain owners");
  ArgumentEncodingContext encoding;owner.queued.front()(encoding);
  require(errors==initial_errors+bool(flags&MTL_DXGI_FORMAT_BC),"unsupported BC error remains observable");
  require(encoding.accesses.size()==2,"one access for each whole buffer");
  require(encoding.accesses[0].handle==1 && encoding.accesses[0].offset==0 &&
          encoding.accesses[0].length==src_length && encoding.accesses[0].flags==ResourceAccess::Read,"original source access");
  require(encoding.accesses[1].handle==(distinct?2u:1u) && encoding.accesses[1].offset==0 &&
          encoding.accesses[1].length==dst_length && encoding.accesses[1].flags==ResourceAccess::Write,"original destination access");
  require(encoding.retained.size()==2,"encoding holds source/destination allocations");
  if(flags&MTL_DXGI_FORMAT_BC) {
    require(encoding.copies.empty(),"unsupported BC keeps original no-copy behavior");
  } else {
    const uint64_t row_bytes=uint64_t(width)*dst_texel;
    // Eligibility is a scenario oracle, not a copy of the production predicate.
    const bool grouped=!baseline && expected_eligible;
    require(encoding.copies.size()==uint64_t(depth)*(grouped?1:height),"required coalesced/fallback command count");
    size_t index=0;
    for(uint32_t z=0;z<depth;++z) for(uint32_t y=0;y<height;y+=grouped?height:1) {
      const auto &copy=encoding.copies[index++];
      require(copy.type==WMTBlitCommandCopyFromBufferToBuffer && copy.src==1 && copy.dst==(distinct?2u:1u),"exact wire command handles/type");
      require(copy.src_offset==64+(src_origin.z+z)*src_image+(src_origin.y+y)*src_row+src_origin.x*src_texel,"exact source offset");
      require(copy.dst_offset==128+(dst_origin.z+z)*dst_image+(dst_origin.y+y)*dst_row+dst_origin.x*dst_texel,"exact destination offset");
      require(copy.copy_length==row_bytes*(grouped?height:1),"exact copied span length");
    }
    // Execute commands against sentinel bytes and compare with the original
    // row semantics independently. This catches copied row/image padding.
    if(distinct) {
      std::vector<unsigned char> source(src_length+64,0x53), expected(dst_length+128,0xa9), actual=expected;
      for(size_t i=0;i<source.size();++i) source[i]=static_cast<unsigned char>(i*37+11);
      for(uint32_t z=0;z<depth;++z) for(uint32_t y=0;y<height;++y) {
        const auto so=64+(src_origin.z+z)*src_image+(src_origin.y+y)*src_row+src_origin.x*src_texel;
        const auto dest=128+(dst_origin.z+z)*dst_image+(dst_origin.y+y)*dst_row+dst_origin.x*dst_texel;
        require(so+row_bytes<=source.size() && dest+row_bytes<=expected.size(),"reference span in bounds");
        std::memcpy(expected.data()+dest,source.data()+so,row_bytes);
      }
      for(const auto &copy:encoding.copies) {
        require(copy.src_offset+copy.copy_length<=source.size() && copy.dst_offset+copy.copy_length<=actual.size(),"command span in bounds");
        std::memcpy(actual.data()+copy.dst_offset,source.data()+copy.src_offset,copy.copy_length);
      }
      require(actual==expected,"payload and every padding byte match row reference");
    }
  }
  owner.queued.clear();require(live_staging==0,"staging owners released after emission");
  require(encoding.retained.size()==2,"allocation lifetime survives captured owner destruction");
}
int main(int argc,char **argv) {
  baseline=argc>1 && std::string(argv[1])=="baseline";
  try {
    verify_copy(true,16,12,1,64,64);                  // 12 -> 1
    verify_copy(true,8,5,3,32,32);                    // 15 -> 3, per-image strides retained
    verify_copy(true,8,3,2,32,32,{0,2,1},{0,1,2},256,224); // partial Y/Z and unequal image gaps
    verify_copy(true,1,1,1,4,4);                     // no work reduction needed
    verify_copy(false,5,4,1,24,24);                   // row padding: fallback
    verify_copy(false,5,4,1,20,24);                   // destination row padding: fallback
    verify_copy(false,5,4,1,24,20);                   // source row padding: fallback
    verify_copy(false,5,4,2,32,32,{1,0,0},{2,0,0});   // partial row/X: fallback
    verify_copy(false,8,4,2,32,32,{}, {},64,64);       // insufficient image stride: fallback
    verify_copy(false,8,3,1,32,32,{1,0,0},{1,0,0});   // defensive X guard at exact pitch
    verify_copy(false,8,4,1,32,32,{}, {},0,0,false);   // same native handle: fallback
    verify_copy(false,8,4,1,32,32,{}, {},0,0,true,false); // bitcast format: fallback
    verify_copy(false,8,4,1,32,32,{}, {},0,0,true,true,2,4); // texel size mismatch: fallback
    verify_copy(false,8,4,1,32,32,{}, {},0,0,true,true,4,4,MTL_DXGI_FORMAT_BC);
    verify_copy(true,8,4,2,32,32,{}, {},0,0,true,true,4,4,MTL_DXGI_FORMAT_EMULATED_LINEAR_DEPTH_STENCIL);
    verify_copy(true,4,3,2,32,32,{}, {},0,0,true,true,8,8); // depth/stencil raw packed bytes
    verify_copy(false,8,0,1,32,32);             // defensive empty region
    verify_copy(false,8,4,0,32,32);             // defensive empty depth
    std::cout<<"PASS: "<<scenarios<<" actual-method scenarios, exact wire commands, payload/padding, deferred ownership/access/flush; "
             <<(baseline?"baseline row commands":"12->1 and 15->3 commands")<<"\n";
    return 0;
  } catch(const std::exception &error) { std::cerr<<error.what()<<"\n";return 1; }
}
