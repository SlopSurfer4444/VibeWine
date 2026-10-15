// Integrated-only seams: WSI suite exercises the unopted branch. The actual
// DS3 header is independently exercised by tests/policy.cpp and exclusive tests.
#include "../api-seam.hpp"
namespace dxmt::wsi {
enum class DS3BorderlessResult { Disabled, Deferred, Refused, Unchanged, Applied, Failed, RollbackFailed };
inline bool ds3BorderlessEnabled() { return false; }
inline DS3BorderlessResult applyDS3Borderless(HWND, bool) { return DS3BorderlessResult::Disabled; }
}
// Only platform/COM/presenter seams are doubled. Production methods follow.
namespace wsi = dxmt::wsi;
#define STDMETHODCALLTYPE
#define IID_PPV_ARGS(x) x
#define ERR(...) do {} while (false)
#define WARN(...) do { ++fake::warnings; } while (false)
using HRESULT = int32_t;
constexpr HRESULT S_OK = 0, E_FAIL = -1;
constexpr HRESULT DXGI_ERROR_INVALID_CALL = -2, DXGI_ERROR_NOT_CURRENTLY_AVAILABLE = -3;
constexpr HRESULT DXGI_STATUS_MODE_CHANGE_IN_PROGRESS = 1;
inline bool FAILED(HRESULT hr) { return hr < 0; }
struct DXGI_RATIONAL { UINT Numerator, Denominator; };
constexpr UINT DXGI_FORMAT_UNKNOWN = 0, DXGI_FORMAT_R8G8B8A8_UNORM = 28;
constexpr UINT DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED = 0;
constexpr UINT DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST = 2, DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST = 3;
constexpr UINT DXGI_MODE_SCALING_UNSPECIFIED = 0, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 2;
struct DXGI_MODE_DESC1 { UINT Width, Height; DXGI_RATIONAL RefreshRate; UINT Format, ScanlineOrdering, Scaling; BOOL Stereo{}; };
using DXGI_MODE_DESC = DXGI_MODE_DESC1;
struct DXGI_OUTPUT_DESC { HMONITOR Monitor; };
struct IDXGIOutput1 {
  HMONITOR monitor = fake::monitor;
  HRESULT QueryInterface(IDXGIOutput1 **out) {
    if (fake::trip("query_output")) return E_FAIL;
    *out = this; return S_OK;
  }
  HRESULT GetDesc(DXGI_OUTPUT_DESC *desc) {
    if (fake::trip("output_desc")) return E_FAIL;
    desc->Monitor = monitor; return S_OK;
  }
  HRESULT FindClosestMatchingMode1(DXGI_MODE_DESC1 *preferred, DXGI_MODE_DESC1 *selected, void *) {
    if (fake::trip("closest_mode")) return E_FAIL;
    *selected = *preferred;
    if (!selected->Width) selected->Width = 1280;
    if (!selected->Height) selected->Height = 720;
    return S_OK;
  }
};
using IDXGIOutput = IDXGIOutput1;
template<class T> struct Com {
  T *value = nullptr;
  Com() = default;
  Com(T *p) : value(p) {}
  Com &operator=(T *p) { value = p; return *this; }
  T **operator&() { return &value; }
  T *operator->() const { return value; }
  T *ptr() const { return value; }
  bool operator==(std::nullptr_t) const { return value == nullptr; }
};
namespace dxmt { using mutex = std::mutex; }
inline UINT GetMonitorFormatBpp(UINT) { return 32; }
struct FixturePresenter { int gamma_resets = 0; void changeGammaRamp(void *) { ++gamma_resets; } };
class FixtureSwapchain {
public:
  uint64_t trace_serial_=1;
  HWND hWnd = fake::hwnd;
  dxmt::mutex mutex_;
  ModeSetGuard modeset_guard_{};
  struct { UINT Width = 1600, Height = 900, Format = DXGI_FORMAT_R8G8B8A8_UNORM;
           UINT Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; } desc_;
  struct { BOOL Windowed = TRUE; DXGI_RATIONAL RefreshRate = {60,1};
           UINT ScanlineOrdering = 0, Scaling = 0; } fullscreen_desc_;
  HMONITOR monitor_ = fake::monitor;
  Com<IDXGIOutput1> target_;
  dxmt::wsi::DXMTWindowState window_state_;
  WCHAR pending_display_name_[32] = {};
  double init_refresh_rate_ = DBL_MAX;
  FixturePresenter presenter_storage;
  FixturePresenter *presenter = &presenter_storage;
  IDXGIOutput1 default_output;
  HRESULT GetOutputFromMonitor(HMONITOR monitor, IDXGIOutput1 **out) {
    if (fake::trip("get_output") || monitor != fake::monitor) return E_FAIL;
    *out = &default_output; return S_OK;
  }
