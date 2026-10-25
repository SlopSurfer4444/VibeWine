// SPDX-License-Identifier: LGPL-2.1-or-later
// Source-only adjunct. Root owns compilation, matched-runtime admission and GPU execution.
// Reuse reviewed Ref/query/Mapping/Pipeline/module/watchdog helpers and procedural VS.
#define main reused_gpu_wave_main
#include "../../gpu-wave/guest.cpp"
#undef main

static std::array<unsigned char,4> source_pixel(UINT x,UINT y) {
  return {static_cast<unsigned char>(x*3+y*5),static_cast<unsigned char>(x^y),
          static_cast<unsigned char>(255-y*7),255};
}
static void mapped_write(ID3D11DeviceContext *ctx,ID3D11Texture2D *texture,UINT width,UINT height,
                         bool source,UINT &pitch) {
  D3D11_MAPPED_SUBRESOURCE m{};
  step(ctx->Map(texture,0,D3D11_MAP_WRITE,0,&m),"staging_write_map");
  if(!m.pData || m.RowPitch<width*4) {ctx->Unmap(texture,0);require(false,"staging_write_layout");}
  pitch=m.RowPitch;
  for(UINT y=0;y<height;++y) {
    auto *row=static_cast<unsigned char *>(m.pData)+y*m.RowPitch;
    std::memset(row,source?0x37:0xa5,m.RowPitch);
    if(source) for(UINT x=0;x<width;++x) {auto p=source_pixel(x,y);std::memcpy(row+x*4,p.data(),4);}
  }
  ctx->Unmap(texture,0);
}
static void staging_case(ID3D11Device *device,ID3D11DeviceContext *ctx,UINT width,bool partial) {
  constexpr UINT height=8;
  D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;
  d.CPUAccessFlags=D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
  Ref<ID3D11Texture2D> source,destination;
  step(device->CreateTexture2D(&d,nullptr,source.out()),"staging_source_create");
  step(device->CreateTexture2D(&d,nullptr,destination.out()),"staging_destination_create");
  UINT source_pitch=0,destination_pitch=0;
  mapped_write(ctx,source.p,width,height,true,source_pitch);
  mapped_write(ctx,destination.p,width,height,false,destination_pitch);
  std::printf("{\"schema\":1,\"type\":\"layout\",\"width\":%u,\"height\":%u,\"source_pitch\":%u,\"destination_pitch\":%u,\"partial\":%s}\n",
              width,height,source_pitch,destination_pitch,partial?"true":"false");
  if(width==64 && (source_pitch!=width*4 || destination_pitch!=width*4))
    throw Failure{"tight_pitch_unavailable_actual_branch_not_admitted",E_FAIL,true};
  if(width==63 && (source_pitch<=width*4 || destination_pitch<=width*4))
    throw Failure{"padded_pitch_unavailable_padding_control_not_admitted",E_FAIL,true};
  D3D11_BOX box{0,0,0,width,height,1};UINT dx=0,dy=0;
  if(partial) {box={3,1,0,20,6,1};dx=5;dy=2;}
  if(!partial) ctx->CopyResource(destination.p,source.p);
  else ctx->CopySubresourceRegion(destination.p,0,dx,dy,0,source.p,0,&box);
  complete(device,ctx);
  Mapping map(ctx,destination.p);
  require(map.value.RowPitch==destination_pitch,"readback_preserves_observed_staging_pitch");
  bool good=true,bad=true;
  for(UINT y=0;y<height;++y) for(UINT byte=0;byte<destination_pitch;++byte) {
    unsigned char expected=0xa5;
    const UINT x=byte/4,c=byte%4;
    if(byte<width*4 && x>=dx && x<dx+box.right-box.left && y>=dy && y<dy+box.bottom-box.top)
      expected=source_pixel(box.left+x-dx,box.top+y-dy)[c];
    const auto actual=static_cast<const unsigned char *>(map.value.pData)[y*map.value.RowPitch+byte];
    good &= actual==expected;
    if(y==dy && byte==dx*4) expected^=1;
    bad &= actual==expected;
  }
  require(good,"staging_payload_and_all_row_padding_exact");
  if(selected("staging")) require(bad,"negative_control_staging_golden",true);
}
static void tight_region(ID3D11Device *device,ID3D11DeviceContext *ctx) {
  constexpr UINT width=64,height=8;
  D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;
  d.CPUAccessFlags=D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
  Ref<ID3D11Texture2D> source,destination;
  step(device->CreateTexture2D(&d,nullptr,source.out()),"tight_region_source_create");
  step(device->CreateTexture2D(&d,nullptr,destination.out()),"tight_region_destination_create");
  UINT sp=0,dp=0;mapped_write(ctx,source.p,width,height,true,sp);mapped_write(ctx,destination.p,width,height,false,dp);
  if(sp!=256 || dp!=256) throw Failure{"tight_region_pitch_unavailable",E_FAIL,true};
  const D3D11_BOX box{0,1,0,width,7,1};ctx->CopySubresourceRegion(destination.p,0,0,2,0,source.p,0,&box);
  complete(device,ctx);Mapping map(ctx,destination.p);require(map.value.RowPitch==dp,"tight_region_read_pitch");
  bool good=true;
  for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) for(UINT c=0;c<4;++c) {
    const auto expected=y>=2?source_pixel(x,y-1)[c]:0xa5;
    good &= static_cast<const unsigned char *>(map.value.pData)[y*map.value.RowPitch+x*4+c]==expected;
  }
  require(good,"tight_full_rows_region_exact_and_outside_rows_untouched");
}
static const char sampling_shader[]=R"(
Texture2DArray<float4> input_texture:register(t0);
float4 ps_array(float4 position:SV_POSITION):SV_TARGET {
  return input_texture.Load(int4(int2(position.xy),0,0));
}
Texture2D<float4> input_2d:register(t1);
float4 ps_2d(float4 position:SV_POSITION):SV_TARGET {
  return input_2d.Load(int3(int2(position.xy),0));
}
)";
static void sample_view(ID3D11Device *device,ID3D11DeviceContext *ctx,Pipeline &pipeline,
                        ID3D11PixelShader *shader,ID3D11ShaderResourceView *view,UINT slot,
                        std::array<unsigned char,4> expected) {
  ColorTarget target(device);pipeline.bind(ctx,1,&target.view.p);
  pipeline.values(ctx,{0,0,0,1},0.5f);ctx->PSSetShader(shader,nullptr,0);
  ctx->PSSetShaderResources(slot,1,&view);clear(ctx,target.view.p);ctx->Draw(3,0);
  ID3D11ShaderResourceView *none=nullptr;ctx->PSSetShaderResources(slot,1,&none);
  ctx->OMSetRenderTargets(0,nullptr,nullptr);
  // Fixed independent UNORM/sRGB byte goldens: 128->55,64->13,192->134,32->4.
  // Reuse every-pixel readback and its connected changed-golden rejection.
  rgba(device,ctx,target.image.p,expected,selected("view"));
}
static void view_prefix(ID3D11Device *device,ID3D11DeviceContext *ctx,Pipeline &pipeline) {
  std::array<unsigned char,kWidth*kHeight*4> first{},second{};
  const std::array<unsigned char,4> a{128,64,192,255},b{64,128,32,255};
  for(UINT i=0;i<kWidth*kHeight;++i) {std::memcpy(first.data()+i*4,a.data(),4);std::memcpy(second.data()+i*4,b.data(),4);}
  D3D11_TEXTURE2D_DESC d{};d.Width=kWidth;d.Height=kHeight;d.MipLevels=1;d.ArraySize=2;
  d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA data[2]={{first.data(),kWidth*4,UINT(first.size())},{second.data(),kWidth*4,UINT(second.size())}};
  Ref<ID3D11Texture2D> texture;step(device->CreateTexture2D(&d,data,texture.out()),"typeless_array_create");
  Ref<ID3D11ShaderResourceView> low,low_srgb,high_unorm,high,tail;
  auto make=[&](DXGI_FORMAT format,UINT slice,UINT slices,Ref<ID3D11ShaderResourceView> &out) {
    D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    s.Texture2DArray={0,1,slice,slices};step(device->CreateShaderResourceView(texture.p,&s,out.out()),"typed_array_srv_register");
  };
  make(DXGI_FORMAT_R8G8B8A8_UNORM,0,1,low);make(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,0,1,low_srgb);
  make(DXGI_FORMAT_R8G8B8A8_UNORM,1,1,high_unorm);make(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,1,1,high);
  Ref<ID3DBlob> code;compile(sampling_shader,sizeof(sampling_shader)-1,"ps_array","ps_5_0",code);
  Ref<ID3D11PixelShader> shader;step(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,shader.out()),"array_sample_shader_create");
  sample_view(device,ctx,pipeline,shader.p,low.p,0,a); // early prefix first
  make(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,0,2,tail);      // append descriptor epoch
  sample_view(device,ctx,pipeline,shader.p,high.p,0,{13,55,4,255});
  sample_view(device,ctx,pipeline,shader.p,low.p,0,a); // high -> low, cached metadata
  sample_view(device,ctx,pipeline,shader.p,tail.p,0,{55,13,134,255});
}
static void dynamic_view_rename(ID3D11Device *device,ID3D11DeviceContext *ctx,Pipeline &pipeline) {
  D3D11_TEXTURE2D_DESC d{};d.Width=kWidth;d.Height=kHeight;d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DYNAMIC;
  d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  Ref<ID3D11Texture2D> texture;step(device->CreateTexture2D(&d,nullptr,texture.out()),"dynamic_typeless_create");
  Ref<ID3D11ShaderResourceView> low,high;
  D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;s.Texture2D={0,1};
  s.Format=DXGI_FORMAT_R8G8B8A8_UNORM;step(device->CreateShaderResourceView(texture.p,&s,low.out()),"dynamic_unorm_register");
  s.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;step(device->CreateShaderResourceView(texture.p,&s,high.out()),"dynamic_srgb_register");
  Ref<ID3DBlob> code;compile(sampling_shader,sizeof(sampling_shader)-1,"ps_2d","ps_5_0",code);
  Ref<ID3D11PixelShader> shader;step(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,shader.out()),"dynamic_sample_shader_create");
  const std::array<std::array<unsigned char,4>,2> colors{{{128,64,192,255},{64,128,32,255}}};
  const std::array<std::array<unsigned char,4>,2> decoded{{{55,13,134,255},{13,55,4,255}}};
  for(UINT generation=0;generation<2;++generation) {
    D3D11_MAPPED_SUBRESOURCE m{};step(ctx->Map(texture.p,0,D3D11_MAP_WRITE_DISCARD,0,&m),"dynamic_write_discard_rename");
    if(!m.pData || m.RowPitch<kWidth*4) {ctx->Unmap(texture.p,0);require(false,"dynamic_map_layout");}
    for(UINT y=0;y<kHeight;++y) for(UINT x=0;x<kWidth;++x)
      std::memcpy(static_cast<unsigned char *>(m.pData)+y*m.RowPitch+x*4,colors[generation].data(),4);
    ctx->Unmap(texture.p,0);
    sample_view(device,ctx,pipeline,shader.p,high.p,1,decoded[generation]);
    sample_view(device,ctx,pipeline,shader.p,low.p,1,colors[generation]);
  }
}
static void targeted_workflow() {
  Ref<IDXGIFactory1> factory;step(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void **>(factory.out())),"dxgi_factory_create");
  Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> ctx;const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level{};
  step(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,requested,1,D3D11_SDK_VERSION,device.out(),&level,ctx.out()),"hardware_device_create");
  require(level==D3D_FEATURE_LEVEL_11_0,"feature_level_11");
  module(L"d3d11.dll","d3d11.dll");module(L"dxgi.dll","dxgi.dll");module(L"winemetal.dll","winemetal.dll");module(L"d3dcompiler_47.dll","d3dcompiler_47.dll");
  wchar_t ack[32768]{};const auto n=GetEnvironmentVariableW(L"VIBEWINE_GPU_MAP_ACK",ack,32768);require(n<32768,"module_ack_path");
  if(n) {const auto begin=GetTickCount64();while(GetFileAttributesW(ack)==INVALID_FILE_ATTRIBUTES) {require(GetTickCount64()-begin<15000,"module_ack_deadline");Sleep(10);}}
  else Sleep(1000);
  stage("staging_tight_full_copy",ctx.p,[&]{staging_case(device.p,ctx.p,64,false);});
  stage("staging_tight_fullrow_region",ctx.p,[&]{tight_region(device.p,ctx.p);});
  stage("staging_padded_full_copy",ctx.p,[&]{staging_case(device.p,ctx.p,63,false);});
  stage("staging_padded_partial_region",ctx.p,[&]{staging_case(device.p,ctx.p,63,true);});
  Pipeline pipeline(device.p);
  stage("typeless_view_prefix_epoch",ctx.p,[&]{view_prefix(device.p,ctx.p,pipeline);});
  stage("dynamic_view_write_discard",ctx.p,[&]{dynamic_view_rename(device.p,ctx.p,pipeline);});
  step(device->GetDeviceRemovedReason(),"device_not_removed");
}
int main(int argc,char **argv) {
  setvbuf(stdout,nullptr,_IONBF,0);started=GetTickCount64();
  if(argc==3 && !std::strcmp(argv[1],"--negative-control") && (!std::strcmp(argv[2],"staging") || !std::strcmp(argv[2],"view"))) negative_control=argv[2];
  else if(argc!=1) return 64;
  std::printf("{\"schema\":1,\"type\":\"start\",\"probe\":\"staging-view-gpu\",\"negative_control\":");json_string(negative_control);std::printf("}\n");
  finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!finished) return 10;
  HANDLE guard=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);if(!guard) {CloseHandle(finished);return 10;}
  int result=0;
  try {targeted_workflow();summary("pass");}
  catch(const Failure &failure) {
    if(stage_name) report_stage(failure.unsupported?"unsupported":"fail",failure.detail,failure.hr);
    const bool rejected=failure.negative&&!selected("none");summary(rejected?"negative_control_rejected":failure.unsupported?"unsupported":"fail",rejected?1:0,failure.detail,failure.hr);
    result=rejected?20:failure.unsupported?2:10;
  } catch(...) {if(stage_name) report_stage("fail","unexpected_exception");summary("fail");result=10;}
  SetEvent(finished);WaitForSingleObject(guard,1000);CloseHandle(guard);CloseHandle(finished);return result;
}
