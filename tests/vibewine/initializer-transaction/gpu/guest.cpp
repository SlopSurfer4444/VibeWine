// SPDX-License-Identifier: LGPL-2.1-or-later
// Procedural initializer correctness guest; no game, window or swapchain.
#define main baseline_gpu_main
#include "../../gpu-wave/guest.cpp"
#undef main
#include <vector>

static std::array<unsigned char,4> texel(UINT x, UINT y, UINT z, UINT mip, UINT slice) {
  return {static_cast<unsigned char>(x*3+y*5+z*11+mip*17),
          static_cast<unsigned char>(x^y^slice*41),static_cast<unsigned char>(slice*71+z*19),255};
}
static void buffer_zero(ID3D11Device *device, ID3D11DeviceContext *ctx) {
  D3D11_BUFFER_DESC d{}; d.ByteWidth=1024;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  Ref<ID3D11Buffer> src,read;step(device->CreateBuffer(&d,nullptr,src.out()),"private_zero_buffer_create");
  d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  step(device->CreateBuffer(&d,nullptr,read.out()),"zero_buffer_readback_create");
  ctx->CopyResource(read.p,src.p);complete(device,ctx);
  D3D11_MAPPED_SUBRESOURCE m{};step(ctx->Map(read.p,0,D3D11_MAP_READ,0,&m),"zero_buffer_map");
  bool good=m.pData!=nullptr;
  if(m.pData) for(UINT i=0;i<d.ByteWidth;i++)good &= static_cast<unsigned char *>(m.pData)[i]==0;
  ctx->Unmap(read.p,0);require(good,"private_buffer_all_bytes_zero");
}
static void verify_2d(ID3D11Device *device,ID3D11DeviceContext *ctx,ID3D11Texture2D *src,bool zero) {
  D3D11_TEXTURE2D_DESC d{};src->GetDesc(&d);const UINT mips=d.MipLevels,slices=d.ArraySize;
  d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
  Ref<ID3D11Texture2D> read;step(device->CreateTexture2D(&d,nullptr,read.out()),"texture_readback_create");
  ctx->CopyResource(read.p,src);complete(device,ctx);
  for(UINT slice=0;slice<slices;slice++)for(UINT mip=0;mip<mips;mip++) {
    const UINT w=std::max(1u,d.Width>>mip),h=std::max(1u,d.Height>>mip),sub=mip+slice*mips;
    D3D11_MAPPED_SUBRESOURCE m{};step(ctx->Map(read.p,sub,D3D11_MAP_READ,0,&m),"texture_subresource_map");
    bool good=m.pData&&m.RowPitch>=w*4,bad=true;
    if(good)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++)for(UINT c=0;c<4;c++) {
      const auto expected=zero?0:texel(x,y,0,mip,slice)[c];
      const auto actual=static_cast<unsigned char *>(m.pData)[y*m.RowPitch+x*4+c];good &= actual==expected;
      bad &= actual==(expected^((x==0&&y==0&&c==0)?1:0));
    }
    ctx->Unmap(read.p,sub);require(good,"initializer_all_texture_pixels_exact");
    if(!zero&&selected("upload"))require(bad,"negative_initializer_upload_golden",true);
  }
}
static void upload_array(ID3D11Device *device,ID3D11DeviceContext *ctx) {
  constexpr UINT mips=3,slices=2;std::array<std::vector<unsigned char>,mips*slices> bytes;
  std::array<D3D11_SUBRESOURCE_DATA,mips*slices> data{};
  for(UINT s=0;s<slices;s++)for(UINT m=0;m<mips;m++) {
    UINT w=64>>m,h=64>>m,i=m+s*mips,pitch=w*4+16;bytes[i].assign(pitch*h,0x6d);
    for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++) {auto p=texel(x,y,0,m,s);std::memcpy(bytes[i].data()+y*pitch+x*4,p.data(),4);}
    data[i]={bytes[i].data(),pitch,pitch*h};
  }
  D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=64;d.MipLevels=mips;d.ArraySize=slices;
  d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  Ref<ID3D11Texture2D> texture;step(device->CreateTexture2D(&d,data.data(),texture.out()),"padded_array_mip_upload_create");
  verify_2d(device,ctx,texture.p,false);
}
static void zero_growth(ID3D11Device *device,ID3D11DeviceContext *ctx) {
  D3D11_TEXTURE2D_DESC d{};d.Width=64;d.Height=d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  Ref<ID3D11Texture2D> a,b;step(device->CreateTexture2D(&d,nullptr,a.out()),"small_zero_texture_create");
  d.Height=2;step(device->CreateTexture2D(&d,nullptr,b.out()),"larger_zero_texture_create");
  verify_2d(device,ctx,a.p,true);verify_2d(device,ctx,b.p,true);
}
static void render_zero(ID3D11Device *device,ID3D11DeviceContext *ctx) {
  D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=64;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
  Ref<ID3D11Texture2D> texture;step(device->CreateTexture2D(&d,nullptr,texture.out()),"render_clear_zero_create");
  verify_2d(device,ctx,texture.p,true);
}
static void depth_case(ID3D11Device *device,ID3D11DeviceContext *ctx,Pipeline &pipeline,DXGI_FORMAT format,bool upload) {
  constexpr UINT row=kWidth*8+16;std::array<unsigned char,row*kHeight> bytes{};
  const float depth=upload?0.25f:0.0f;const unsigned char stencil=upload?0x5a:0;
  for(UINT y=0;y<kHeight;y++)for(UINT x=0;x<kWidth;x++) {
    if(format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {std::memcpy(bytes.data()+y*row+x*8,&depth,4);bytes[y*row+x*8+4]=stencil;}
    else {const uint32_t packed=0x400000u|(uint32_t(stencil)<<24);std::memcpy(bytes.data()+y*row+x*4,&packed,4);}
  }
  D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=kWidth;d.MipLevels=d.ArraySize=1;d.Format=format;
  d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  D3D11_SUBRESOURCE_DATA data{bytes.data(),row,row*kHeight};Ref<ID3D11Texture2D> texture;Ref<ID3D11DepthStencilView> view;
  step(device->CreateTexture2D(&d,upload?&data:nullptr,texture.out()),"depth_stencil_initializer_create");
  step(device->CreateDepthStencilView(texture.p,nullptr,view.out()),"initialized_depth_view_create");
  D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  ds.DepthFunc=upload?D3D11_COMPARISON_LESS:D3D11_COMPARISON_EQUAL;ds.StencilEnable=TRUE;
  ds.StencilReadMask=ds.StencilWriteMask=0xff;ds.FrontFace.StencilFunc=ds.BackFace.StencilFunc=D3D11_COMPARISON_EQUAL;
  ds.FrontFace.StencilFailOp=ds.FrontFace.StencilDepthFailOp=ds.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;ds.BackFace=ds.FrontFace;
  Ref<ID3D11DepthStencilState> state;step(device->CreateDepthStencilState(&ds,state.out()),"initialized_depth_state_create");
  pipeline.bind(ctx,0,nullptr,view.p);ctx->PSSetShader(nullptr,nullptr,0);ctx->OMSetDepthStencilState(state.p,stencil);
  pipeline.values(ctx,{0,0,0,0},upload?0.125f:0.0f);auto samples=occlusion(device,ctx,false);
  require(samples==UINT64(kWidth)*kHeight,"initialized_depth_stencil_near_and_reference_match");
  if(upload&&selected("depth"))require(samples==4095,"negative_initializer_depth_query",true);
  pipeline.values(ctx,{0,0,0,0},upload?0.75f:0.5f);require(occlusion(device,ctx,false)==0,"initialized_depth_far_rejected");
  pipeline.values(ctx,{0,0,0,0},upload?0.125f:0.0f);ctx->OMSetDepthStencilState(state.p,stencil^1);
  require(occlusion(device,ctx,false)==0,"initialized_stencil_wrong_reference_rejected");ctx->OMSetRenderTargets(0,nullptr,nullptr);
}
static void volume_upload(ID3D11Device *device,ID3D11DeviceContext *ctx) {
  constexpr UINT w=8,h=8,zs=4,row=w*4+16,image=row*h+16;std::array<unsigned char,image*zs> bytes{};
  for(UINT z=0;z<zs;z++)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++) {auto p=texel(x,y,z,0,0);std::memcpy(bytes.data()+z*image+y*row+x*4,p.data(),4);}
  D3D11_TEXTURE3D_DESC d{};d.Width=w;d.Height=h;d.Depth=zs;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{bytes.data(),row,image};
  Ref<ID3D11Texture3D> src,read;step(device->CreateTexture3D(&d,&data,src.out()),"volume_upload_create");
  d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  step(device->CreateTexture3D(&d,nullptr,read.out()),"volume_readback_create");ctx->CopyResource(read.p,src.p);complete(device,ctx);
  D3D11_MAPPED_SUBRESOURCE m{};step(ctx->Map(read.p,0,D3D11_MAP_READ,0,&m),"volume_readback_map");
  bool good=m.pData&&m.RowPitch>=w*4&&m.DepthPitch>=m.RowPitch*h;
  if(good)for(UINT z=0;z<zs;z++)for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++)for(UINT c=0;c<4;c++)
    good &= static_cast<unsigned char *>(m.pData)[z*m.DepthPitch+y*m.RowPitch+x*4+c]==texel(x,y,z,0,0)[c];
  ctx->Unmap(read.p,0);require(good,"initializer_padded_volume_all_pixels_exact");
}
static void repair_workflow() {
  Ref<IDXGIFactory1> factory;step(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void **>(factory.out())),"dxgi_factory_create");
  Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
  step(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,requested,1,D3D11_SDK_VERSION,device.out(),&level,ctx.out()),"hardware_device_create");
  require(level==D3D_FEATURE_LEVEL_11_0,"feature_level11");module(L"d3d11.dll","d3d11.dll");module(L"dxgi.dll","dxgi.dll");module(L"winemetal.dll","winemetal.dll");module(L"d3dcompiler_47.dll","d3dcompiler_47.dll");
  wchar_t ack[32768]{};auto n=GetEnvironmentVariableW(L"VIBEWINE_GPU_MAP_ACK",ack,32768);require(n<32768,"module_ack_path");
  if(n){auto t=GetTickCount64();while(GetFileAttributesW(ack)==INVALID_FILE_ATTRIBUTES){require(GetTickCount64()-t<15000,"module_ack_deadline");Sleep(10);}}else Sleep(1000);
  stage("private_buffer_zero",ctx.p,[&]{buffer_zero(device.p,ctx.p);});
  stage("padded_upload_subresources",ctx.p,[&]{upload_array(device.p,ctx.p);});
  stage("zero_texture_growth",ctx.p,[&]{zero_growth(device.p,ctx.p);});
  stage("render_target_zero",ctx.p,[&]{render_zero(device.p,ctx.p);});
  Pipeline pipeline(device.p);
  stage("depth_stencil_zero",ctx.p,[&]{depth_case(device.p,ctx.p,pipeline,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,false);});
  stage("depth_stencil_upload",ctx.p,[&]{depth_case(device.p,ctx.p,pipeline,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,true);depth_case(device.p,ctx.p,pipeline,DXGI_FORMAT_D24_UNORM_S8_UINT,true);});
  stage("padded_volume_upload",ctx.p,[&]{volume_upload(device.p,ctx.p);});step(device->GetDeviceRemovedReason(),"device_not_removed");
}
int main(int argc,char **argv) {
  setvbuf(stdout,nullptr,_IONBF,0);started=GetTickCount64();
  if(argc==3&&!std::strcmp(argv[1],"--negative-control")&&(!std::strcmp(argv[2],"upload")||!std::strcmp(argv[2],"depth")))negative_control=argv[2];else if(argc!=1)return 64;
  std::printf("{\"schema\":1,\"type\":\"start\",\"probe\":\"initializer-transaction-gpu\",\"negative_control\":");json_string(negative_control);std::printf("}\n");
  finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!finished)return 10;HANDLE guard=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);if(!guard){CloseHandle(finished);return 10;}
  int rc=0;try{repair_workflow();summary("pass");}catch(const Failure &f){if(stage_name)report_stage(f.unsupported?"unsupported":"fail",f.detail,f.hr);bool rejected=f.negative&&!selected("none");summary(rejected?"negative_control_rejected":f.unsupported?"unsupported":"fail",rejected?1:0,f.detail,f.hr);rc=rejected?20:f.unsupported?2:10;}catch(...){if(stage_name)report_stage("fail","unexpected_exception");summary("fail");rc=10;}
  SetEvent(finished);WaitForSingleObject(guard,1000);CloseHandle(guard);CloseHandle(finished);return rc;
}
