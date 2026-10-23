#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
using UINT = uint32_t;
using UINT16 = uint16_t;
using HRESULT = int32_t;
constexpr HRESULT S_OK=0, E_INVALIDARG=-1, DXGI_ERROR_UNSUPPORTED=-2;
using DXGI_HDR_METADATA_TYPE = uint32_t;
constexpr UINT DXGI_HDR_METADATA_TYPE_NONE=0, DXGI_HDR_METADATA_TYPE_HDR10=1;
#define STDMETHODCALLTYPE
// API_TYPE
// WMT_TYPE
using WMTColorSpace = int;
constexpr WMTColorSpace WMTColorSpaceHDR_PQ=1, WMTColorSpaceHDR_scRGB=2;
#define WMT_COLORSPACE_IS_HDR(x) ((x)==1 || (x)==2)
struct WMTLayerProps {};
namespace WMT { struct Texture {}; struct RenderPipelineState {}; template<class T> using Reference=std::shared_ptr<T>; }
struct WMTEDRValue {float maximum_edr_color_component_value=2, maximum_potential_edr_color_component_value=4;};
// Native display discovery and PSO compilation are injected seams.
void WMTQueryDisplaySettingForLayer(uint64_t, uint64_t *version, WMTColorSpace *, WMTHDRMetadata *, WMTEDRValue *) { *version=0; }
namespace cs2trace { struct Scope {uint64_t type,object;}; inline uint32_t generation(){return 23;} }
// PRESENT_METADATA
class Presenter {
public:
// CONFIGURATION
// PRESENT_STATE
  void changeHDRMetadata(const WMTHDRMetadata *metadata);
  PresentState synchronizeLayerProperties();
  bool buildRenderPipelineState(bool, bool metadata, bool, bool) { compiled_with_metadata=metadata; ++builds; return true; }
  std::mutex configuration_mutex_;
  WMTHDRMetadata hdr_metadata_{}, display_hdr_metadata_{};
  WMTEDRValue display_edr_value_{};
  bool has_hdr_metadata_=false, configuration_dirty_=true, pso_valid_=false, compiled_with_metadata=false;
  uint64_t display_setting_version_=0, configuration_revision_=0, gamma_version_=0, frame_requested_=0;
  WMTColorSpace colorspace_=WMTColorSpaceHDR_PQ, display_colorspace_=0;
  unsigned sample_count_=1, builds=0;
  struct {uint64_t handle=0;} layer_;
  WMTLayerProps layer_props_;
  WMT::Reference<WMT::Texture> gamma_lut_texture_;
  WMT::Reference<WMT::RenderPipelineState> present_blit_,present_scale_;
  std::shared_ptr<const PresentationConfiguration> configuration_;
};
// CHANGE
// SYNCHRONIZE
struct Swapchain {
  Presenter store;
  Presenter *presenter=&store;
// SET_HDR
};
static void require(bool condition, const char *message) {if(!condition)throw std::runtime_error(message);}
int main() {
 try {
  Swapchain chain;
  DXGI_HDR_METADATA_HDR10 value={{101,102},{201,202},{301,302},{401,402},4000,17,2000,1000};
  // Size/type errors must preserve a previously accepted pending value.
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,sizeof(value),&value)==S_OK,"valid HDR10 rejected");
  require(chain.store.has_hdr_metadata_,"HDR10 silently discarded");
  const auto saved=chain.store.hdr_metadata_;
  for(UINT size : {0u,UINT(sizeof(value)-1),UINT(sizeof(value)+1)})
   require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,size,&value)==E_INVALIDARG,"invalid HDR10 size accepted");
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,sizeof(value),nullptr)==E_INVALIDARG,"null HDR10 accepted");
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE,1,nullptr)==E_INVALIDARG,"nonzero NONE accepted");
  require(chain.SetHDRMetaData(2,sizeof(value),&value)==DXGI_ERROR_UNSUPPORTED,"unsupported HDR type accepted");
  require(chain.SetHDRMetaData(UINT(-1),0,nullptr)==DXGI_ERROR_UNSUPPORTED,"unknown HDR type accepted");
  require(std::memcmp(&saved,&chain.store.hdr_metadata_,sizeof(saved))==0 && chain.store.has_hdr_metadata_,"invalid request changed metadata");
  require(saved.red_primary[0]==101 && saved.red_primary[1]==102 && saved.green_primary[0]==201 && saved.green_primary[1]==202 && saved.blue_primary[0]==301 && saved.blue_primary[1]==302 && saved.white_point[0]==401 && saved.white_point[1]==402 && saved.max_mastering_luminance==4000 && saved.min_mastering_luminance==17 && saved.max_content_light_level==2000 && saved.max_frame_average_light_level==1000,"HDR field conversion corrupted");
  auto first=chain.store.synchronizeLayerProperties();
  require(first.metadata.max_content_luminance==4000 && first.configuration && chain.store.compiled_with_metadata,"HDR snapshot missing metadata");
  require(chain.store.builds==1,"unexpected initial pipeline builds");
  // The payload may be unaligned and expires immediately after the API call.
  std::array<unsigned char,sizeof(value)+1> bytes{};
  value.MaxMasteringLuminance=8000;
  std::memcpy(bytes.data()+1,&value,sizeof(value));
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,sizeof(value),bytes.data()+1)==S_OK,"unaligned HDR10 rejected");
  std::memset(bytes.data(),0,bytes.size());
  auto second=chain.store.synchronizeLayerProperties();
  require(second.metadata.max_content_luminance==8000 && second.configuration && second.configuration->revision>first.configuration->revision,"caller bytes retained or metadata revision unchanged");
  require(first.metadata.max_content_luminance==4000 && first.configuration->revision==1,"queued earlier HDR state mutated");
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE,0,&value)==S_OK && !chain.store.has_hdr_metadata_,"NONE did not clear metadata");
  auto cleared=chain.store.synchronizeLayerProperties();
  require(!chain.store.compiled_with_metadata && cleared.metadata.max_content_luminance==10000 && second.metadata.max_content_luminance==8000,"clear retroactively changed queued state or left metadata enabled");
  require(chain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE,0,nullptr)==S_OK,"idempotent NONE rejected");
  std::cout<<"PASS HDR validation; all fields; unaligned transient caller payload; production presenter copy and immutable queued state; clear\n";
 } catch(const std::exception &error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
