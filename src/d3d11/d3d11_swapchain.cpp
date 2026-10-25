#include "cs2_trace.hpp"
#include "d3d11_swapchain.hpp"
#include "d3d11_spatial_geometry.hpp"
#include "com/com_guid.hpp"
#include "config/config.hpp"
#include "d3d11_private.h"
#include "dxgi_interfaces.h"
#include "dxgi_object.hpp"
#include "dxgi_output.hpp"
#include "d3d11_context.hpp"
#include "dxmt_context.hpp"
#include "dxmt_hud_state.hpp"
#include "dxmt_statistics.hpp"
#include "dxmt_presenter.hpp"
#include "log/log.hpp"
#include "d3d11_resource.hpp"
#include "d3d11_device.hpp"
#include "util_cpu_fence.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include "util_win32_compat.h"
#include "util_hotpatch.h"
#include "wsi_monitor.hpp"
#include "wsi_platform_win32.hpp"
#include "wsi_window.hpp"
#include "ds3_borderless.hpp"
#include "dxmt_info.hpp"
#include "dxmt_presenter.hpp"
#include <algorithm>
#include <atomic>
#include <iterator>
#include <string_view>
#include <cfloat>
#include <cstring>
#include <format>

/**
Ref: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency
This value is 1 by default.
*/
constexpr size_t kSwapchainLatency = 1;

namespace dxmt {

WMTPixelFormat ConvertSwapChainFormat(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return WMTPixelFormatBGRA8Unorm_sRGB;
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
    return WMTPixelFormatBGRA8Unorm;
  case DXGI_FORMAT_R10G10B10A2_UNORM:
    return WMTPixelFormatRGB10A2Unorm;
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return WMTPixelFormatRGBA16Float;
  default:
    return WMTPixelFormatInvalid;
  }
}

WMTColorSpace ConvertColorSpace(DXGI_COLOR_SPACE_TYPE color_space, bool hdr) {
  switch (color_space) {
  case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
    return WMTColorSpaceSRGB;
  case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
    return hdr ? WMTColorSpaceHDR_scRGB : WMTColorSpaceSRGBLinear;
  case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
    return WMTColorSpaceHDR_PQ;
  case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
    return WMTColorSpaceBT2020;
  default:
    return WMTColorSpaceInvalid;
  }
}

/**
 FIXME: duplicated implementation in dxgi_output.cpp
*/
uint32_t
GetMonitorFormatBpp(DXGI_FORMAT Format) {
  switch (Format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    return 32;

  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return 64;

  default:
    Logger::warn(str::format("GetMonitorFormatBpp: Unknown format: ", Format));
    return 32;
  }
}

class ModeSetGuard {
  std::atomic_flag in_progress_;
public:
  class ModeSetInProgress {
    ModeSetGuard &guard_;
    bool protected_;

  public:
    ModeSetInProgress(ModeSetGuard &guard) : guard_(guard) {
      protected_ = guard_.in_progress_.test_and_set();
    }
    ~ModeSetInProgress() {
      if (!protected_) {
        guard_.in_progress_.clear();
      }
    }
    operator bool() {
      return protected_;
    }
  };
};

template <bool EnableMetalFX>
class MTLD3D11SwapChain final : public MTLDXGISubObject<IDXGISwapChain4, MTLD3D11Device> {
public:
  MTLD3D11SwapChain(
      IDXGIFactory1 *pFactory, MTLD3D11Device *pDevice, IMTLDXGIDevice *pDXGIDevice,
       HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc
  ) :
      MTLDXGISubObject(pDevice),
      factory_(pFactory),
      dxgi_device_(pDXGIDevice),
      presentation_count_(0),
      desc_(*pDesc),
      device_context_(pDevice->GetImmediateContextPrivate()),
      hWnd(hWnd),
      monitor_(wsi::getWindowMonitor(hWnd)),
      hud(WMT::DeveloperHUDProperties::instance()) {

    native_view_ = WMT::CreateMetalViewFromHWND((intptr_t)hWnd, pDevice->GetMTLDevice(), layer_weak_);

    if (!native_view_) {
      ERR("Failed to acquire native Metal view for swapchain");
      initialization_result_ = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
      return;
    }

    if constexpr (EnableMetalFX) {
      scale_factor = std::max(Config::getInstance().getOption<float>("d3d11.metalSpatialUpscaleFactor", 2), 1.0f);
#if defined(DXMT_FIXED_OUTPUT_METALFX) && DXMT_FIXED_OUTPUT_METALFX
      fixed_output_ = Config::getInstance().getOption<bool>("d3d11.metalfxPerformanceUpscale", false);
      if (fixed_output_) {
        fixed_output_width_ = Config::getInstance().getOption<int>("d3d11.metalfxOutputWidth", 0);
        fixed_output_height_ = Config::getInstance().getOption<int>("d3d11.metalfxOutputHeight", 0);
      }
#endif
    }

    presenter = Rc(new Presenter(pDevice->GetMTLDevice(), layer_weak_,
                                 pDevice->GetDXMTDevice().queue().cmd_library,
                                 fixed_output_ ? 1.0f : scale_factor, desc_.SampleDesc.Count, trace_serial_));
    if (!presenter->supportsCheckedLayerConfiguration()) {
      ERR("Native Metal bridge lacks checked synchronous layer configuration");
      initialization_result_ = DXGI_ERROR_UNSUPPORTED;
      return;
    }

    frame_latency = kSwapchainLatency;
    present_semaphore_ = CreateSemaphore(nullptr, frame_latency,
                                         DXGI_MAX_SWAP_CHAIN_BUFFERS, nullptr);

    // without this flag, there is still a DXGIDevice level of frame latency control
    if (desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
      frame_latency_fence_ = std::make_unique<CpuFence>();
    }

    if (pFullscreenDesc) {
      fullscreen_desc_ = *pFullscreenDesc;
    } else {
      fullscreen_desc_.Windowed = true;
    }

    preferred_max_frame_rate =
        Config::getInstance().getOption<int>("d3d11.preferredMaxFrameRate", 0);
    RefreshDisplayTiming(monitor_);

    handle_alt_tab_ = Config::getInstance().getOption<bool>("dxgi.handleAltTab", false);

    hud.initialize(GetVersionDescriptionText(device_->GetDirectXVersion(), device_->GetFeatureLevel()));

    backbuffer_desc_ = D3D11_TEXTURE2D_DESC1 {
      .Width = desc_.Width,
      .Height = desc_.Height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = desc_.Format,
      .SampleDesc = desc_.SampleDesc,
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_RENDER_TARGET,
      .CPUAccessFlags = {},
      .MiscFlags = {},
      .TextureLayout = {},
    };

    if (desc_.BufferUsage & DXGI_USAGE_SHADER_INPUT)
      backbuffer_desc_.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (desc_.BufferUsage & DXGI_USAGE_UNORDERED_ACCESS)
      backbuffer_desc_.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    initialization_result_ = ResizeBuffers(0, desc_.Width, desc_.Height, DXGI_FORMAT_UNKNOWN, desc_.Flags);
    if (FAILED(initialization_result_)) return;
    // Allocate first: failed construction must not leave a newly enlarged HWND.
    auto borderless = wsi::applyDS3Borderless(hWnd, fullscreen_desc_.Windowed);
    if (borderless != wsi::DS3BorderlessResult::Disabled &&
        borderless != wsi::DS3BorderlessResult::Applied &&
        borderless != wsi::DS3BorderlessResult::Unchanged) {
      WARN("DS3 native borderless initialization failed ", static_cast<int>(borderless));
      initialization_result_ = E_FAIL;
      return;
    }
    if (!fullscreen_desc_.Windowed) {
      fullscreen_desc_.Windowed = TRUE;
      initialization_result_ = EnterFullscreenMode(nullptr);
    }
  };

  HRESULT CleanupFullscreenForDestruction() {
    HRESULT hr = S_OK;
    if (pending_display_name_[0] || window_state_.captured)
      hr = LeaveFullscreenMode();
    if (FAILED(hr))
      WARN("DXGI: Swapchain destruction leaves fullscreen cleanup incomplete: ",
           hr, " display pending=", pending_display_name_[0] != 0,
           " style=", window_state_.style_pending, " exstyle=", window_state_.exstyle_pending,
           " present rect=", window_state_.present_rect_pending, " position=", window_state_.position_pending);
    // The HWND can outlive the swapchain. Its property must no longer refer to
    // window_state_ before that member is freed, even when physical cleanup failed.
    if (!wsi::detachFullscreenState(hWnd, &window_state_)) {
      WARN("DXGI: Swapchain destruction could not detach fullscreen ownership token");
      if (!FAILED(hr))
        hr = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    return hr;
  }

  ~MTLD3D11SwapChain() {
    cs2trace::ApiScope trace_teardown(48,trace_serial_);
    trace_teardown.result=static_cast<uint32_t>(CleanupFullscreenForDestruction());
    if (native_view_) {
      device_context_->WaitUntilGPUIdle();
      { cs2trace::ApiScope trace_release(49,trace_serial_);
        WMT::ReleaseMetalView(native_view_);
        trace_release.result=0; }
      native_view_ = {};
    }
    if (present_semaphore_) CloseHandle(present_semaphore_);
  };

  HRESULT InitializationResult() const { return initialization_result_; }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) ||
        riid == __uuidof(IDXGISwapChain2) ||
        riid == __uuidof(IDXGISwapChain3) ||
        riid == __uuidof(IDXGISwapChain4)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IDXGISwapChain1), riid)) {
      WARN("DXGISwapChain: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetParent(REFIID riid, void **parent) final {
    return factory_->QueryInterface(riid, parent);
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  Present(UINT sync_interval, UINT flags) final {
    return Present1(sync_interval, flags, nullptr);
  };

  HRESULT
  STDMETHODCALLTYPE
  GetBuffer(UINT buffer_idx, REFIID riid, void **surface) final {
    if (!surface) return E_POINTER;
    *surface = nullptr;
    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    if (buffer_idx == 0) {
      if (!backbuffer_) return DXGI_ERROR_INVALID_CALL;
      return backbuffer_->QueryInterface(riid, surface);
    } else {
      ERR("Non zero-index buffer is not supported");
      return DXGI_ERROR_UNSUPPORTED;
    }
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) final {
    cs2trace::ApiScope api(46,trace_serial_,uint64_t(Fullscreen),(uint64_t)pTarget);
    return api.invoke([&]() -> HRESULT {
    // This admitted DS3 route uses geometric borderless with DXGI windowed.
    // Reject an accidental menu/Alt+Enter request before querying outputs or
    // changing modes. FALSE still takes the normal WSI cleanup path.
    if (Fullscreen && wsi::ds3BorderlessEnabled())
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    Com<IDXGIOutput1> target;

    if (!Fullscreen && pTarget)
      return DXGI_ERROR_INVALID_CALL;

    if (pTarget) {
      DXGI_OUTPUT_DESC desc;

      HRESULT hr = pTarget->QueryInterface(IID_PPV_ARGS(&target));
      if (FAILED(hr))
        return hr;
      hr = target->GetDesc(&desc);
      if (FAILED(hr))
        return hr;

      if (!fullscreen_desc_.Windowed && Fullscreen && monitor_ != desc.Monitor) {
        HRESULT hr = LeaveFullscreenMode();
        if (FAILED(hr))
          return hr;
      }
    }

    if (fullscreen_desc_.Windowed && Fullscreen)
      return EnterFullscreenMode(target.ptr());
    else if (!Fullscreen)
      // A prior exit can be logically windowed with physical cleanup pending.
      return LeaveFullscreenMode();

    return S_OK;

    });
  };

  HRESULT EnterFullscreenMode(IDXGIOutput1* pTarget) {
    ModeSetGuard::ModeSetInProgress modeset_inprogress(modeset_guard_);
    if (modeset_inprogress)
      return DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;

    Com<IDXGIOutput1> output = pTarget;

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    if (output == nullptr) {
      if (FAILED(GetOutputFromMonitor(wsi::getWindowMonitor(hWnd), &output))) {
       ERR("DXGI: EnterFullscreenMode: Cannot query containing output");
        return E_FAIL;
      }
    }

    std::unique_lock<dxmt::mutex> lock(mutex_);
    if (pending_display_name_[0] || window_state_.captured)
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    // Fullscreen construction also enters here with Windowed initially false.
    // Failed entry must never leave a successful no-op fullscreen fast path.
    fullscreen_desc_.Windowed = TRUE;
    DXGI_OUTPUT_DESC desc = {};
    HRESULT output_hr = output->GetDesc(&desc);
    if (FAILED(output_hr))
      return output_hr;

    DXGI_SWAP_CHAIN_DESC1 buffer_desc;
    { std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_); buffer_desc = desc_; }
    DXGI_MODE_DESC1 preferred_display_mode = {
        buffer_desc.Width,
        buffer_desc.Height,
        fullscreen_desc_.RefreshRate,
        buffer_desc.Format,
        DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
        DXGI_MODE_SCALING_UNSPECIFIED
    };
    if (FAILED(ChangeDisplayMode(output.ptr(), &preferred_display_mode))) {
      ERR("DXGI: EnterFullscreenMode: Failed to change display mode");
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    // Display cleanup is recorded separately until the window entry completes.

    // Move the window so that it covers the entire output
    bool modeSwitch = (desc_.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) != 0u;

    monitor_ = desc.Monitor;
    target_  = std::move(output);

    lock = {};

    if (!wsi::enterFullscreenMode(desc.Monitor, hWnd, &window_state_, modeSwitch)) {
      std::lock_guard<dxmt::mutex> cleanup_lock(mutex_);
      target_ = nullptr;
      ERR("DXGI: EnterFullscreenMode: Failed to enter fullscreen mode");
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    std::lock_guard<dxmt::mutex> commit_lock(mutex_);
    fullscreen_desc_.Windowed = FALSE;
    return S_OK;
  }


  HRESULT LeaveFullscreenMode() {
    ModeSetGuard::ModeSetInProgress modeset_inprogress(modeset_guard_);
    if (modeset_inprogress)
      return DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;

    std::unique_lock<dxmt::mutex> lock(mutex_);
    HRESULT display_hr = S_OK;
    if (pending_display_name_[0]) {
      display_hr = RestoreDisplayMode(pending_display_name_);
      if (FAILED(display_hr))
        WARN("DXGI: LeaveFullscreenMode: Display restore remains pending");
      else
        pending_display_name_[0] = 0;
    }

    // Logical exit is useful even if physical display release failed. Keep its
    // original device name separately so another exit can retry the obligation.
    fullscreen_desc_.Windowed = TRUE;
    target_ = nullptr;
    monitor_ = wsi::getWindowMonitor(hWnd);
    lock = {};

    // WSI knows which individual mutations completed and validates the window
    // property before touching it, including when the HWND has been recycled.
    bool window_restored = wsi::leaveFullscreenMode(hWnd, &window_state_, true);
    presenter->changeGammaRamp(nullptr);
    if (!window_restored) {
      ERR("DXGI: LeaveFullscreenMode: Window restore remains pending");
      return FAILED(display_hr) ? display_hr : DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    return display_hr;
  }

  HRESULT
  ChangeDisplayMode(IDXGIOutput1 *pOutput, DXGI_MODE_DESC1 *pDisplayMode) {
    if (!pOutput)
      return DXGI_ERROR_INVALID_CALL;

    // Find a mode that the output supports

    DXGI_MODE_DESC1 preferred_mode = *pDisplayMode;
    DXGI_MODE_DESC1 selected_mode = {};

    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)) {
      preferred_mode.Width = 0;
      preferred_mode.Height = 0;
    }

    if (preferred_mode.Format == DXGI_FORMAT_UNKNOWN) {
      std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
      preferred_mode.Format = desc_.Format;
    }

    HRESULT hr = pOutput->FindClosestMatchingMode1(&preferred_mode, &selected_mode, nullptr);

    if (FAILED(hr)) {
      ERR("DXGI: Failed to query closest mode:"
          "\n"
          "  Format: ",
          preferred_mode.Format,
          "\n"
          "  Mode:   ",
          preferred_mode.Width, "x", preferred_mode.Height, "@",
          preferred_mode.RefreshRate.Numerator / std::max(preferred_mode.RefreshRate.Denominator, 1u));
      return hr;
    }

    if (!selected_mode.RefreshRate.Denominator)
      selected_mode.RefreshRate.Denominator = 1;

    DXGI_OUTPUT_DESC output_desc = {};
    hr = pOutput->GetDesc(&output_desc);
    if (FAILED(hr))
      return hr;
    WCHAR display_name[32] = {};
    if (!wsi::getDisplayName(output_desc.Monitor, display_name))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    // Reject stale/replaced output identity rather than overwrite an obligation.
    if (pending_display_name_[0] &&
        std::wstring_view(pending_display_name_) != std::wstring_view(display_name))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    wsi::WsiMode wsi_mode{
        selected_mode.Width,
        selected_mode.Height,
        {selected_mode.RefreshRate.Numerator, selected_mode.RefreshRate.Denominator},
        GetMonitorFormatBpp(selected_mode.Format),
        selected_mode.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST ||
            selected_mode.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST
    };
    // Preserve identity even on a failed mode-set, which may be partial.
    std::copy(std::begin(display_name), std::end(display_name), std::begin(pending_display_name_));
    if (!wsi::setWindowMode(output_desc.Monitor, hWnd, wsi_mode))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    *pDisplayMode = selected_mode;
    // A selected mode can omit refresh. Query the applied mode rather than
    // treating a zero/unknown rate as an unlimited scheduling duration.
    RefreshDisplayTiming(output_desc.Monitor);
    return S_OK;
  }

  HRESULT
  RestoreDisplayMode(const WCHAR *deviceName) {
    if (!deviceName || !deviceName[0])
      return DXGI_ERROR_INVALID_CALL;

    if (!wsi::restoreDisplayMode(deviceName))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    RefreshDisplayTiming(wsi::getWindowMonitor(hWnd));
    return S_OK;
  }

  void RefreshDisplayTiming(HMONITOR monitor) {
    uint64_t generation;
    {
      std::lock_guard<dxmt::mutex> lock(display_timing_mutex_);
      generation = ++display_timing_generation_;
    }
    // Query outside the timing lock: Win32 callbacks can reenter the context.
    wsi::WsiMode current_mode = {};
    bool valid = wsi::getCurrentDisplayMode(monitor, &current_mode) &&
        current_mode.refreshRate.numerator != 0 &&
        current_mode.refreshRate.denominator != 0;
    std::lock_guard<dxmt::mutex> lock(display_timing_mutex_);
    if (generation != display_timing_generation_)
      return; // A later transition owns the timing observation now.
    display_timing_monitor_ = monitor;
    display_timing_valid_ = valid;
    if (valid)
      display_refresh_rate_ = double(current_mode.refreshRate.numerator) /
                              double(current_mode.refreshRate.denominator);
    // Retain the last finite observation on transient query failure. Before
    // any valid observation, use a finite nominal cadence for SyncInterval.
  }

  bool NeedsDisplayTimingRefresh(HMONITOR monitor) {
    std::lock_guard<dxmt::mutex> lock(display_timing_mutex_);
    return monitor != display_timing_monitor_ || !display_timing_valid_;
  }

  double PresentationDuration(UINT sync_interval) {
    std::lock_guard<dxmt::mutex> lock(display_timing_mutex_);
    const double rate = preferred_max_frame_rate > 0
        ? preferred_max_frame_rate : display_refresh_rate_;
    return std::max(sync_interval / rate,
                    preferred_max_frame_rate > 0 ? 1.0 / rate : 0.0);
  }

  HRESULT
  STDMETHODCALLTYPE
  GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) final {
    HRESULT hr = S_OK;

    if (handle_alt_tab_ && !fullscreen_desc_.Windowed && !wsi::isForeground(hWnd))
      SetFullscreenState(FALSE, nullptr);

    if (pFullscreen != nullptr)
      *pFullscreen = !fullscreen_desc_.Windowed;

    if (ppTarget != nullptr)
      *ppTarget = target_.ref();

    return hr;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) final {
    if (!pDesc)
      return E_INVALIDARG;

    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    pDesc->BufferDesc.Width = desc_.Width;
    pDesc->BufferDesc.Height = desc_.Height;
    pDesc->BufferDesc.RefreshRate = fullscreen_desc_.RefreshRate;
    pDesc->BufferDesc.Format = desc_.Format;
    pDesc->BufferDesc.ScanlineOrdering = fullscreen_desc_.ScanlineOrdering;
    pDesc->BufferDesc.Scaling = fullscreen_desc_.Scaling;
    pDesc->SampleDesc = desc_.SampleDesc;
    pDesc->BufferUsage = desc_.BufferUsage;
    pDesc->BufferCount = desc_.BufferCount;
    pDesc->OutputWindow = hWnd;
    pDesc->Windowed = fullscreen_desc_.Windowed;
    pDesc->SwapEffect = desc_.SwapEffect;
    pDesc->Flags = desc_.Flags;
    return S_OK;
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                UINT flags) final {
    cs2trace::ApiScope api(44,trace_serial_,(uint64_t(Width)<<32)|Height,(uint64_t(BufferCount)<<32)|flags);
    TraceResizeState(api.gen, 44);
    HRESULT result = api.invoke([&]() -> HRESULT {
    // Lock ordering matches Present. No HWND mutation is made under either lock.
    std::unique_lock<d3d11_device_mutex> device_lock(device_->mutex);
    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    auto next_desc = desc_;
    uint32_t render_width = Width, render_height = Height;
    auto borderless = wsi::resolveDS3RenderSize(hWnd, fullscreen_desc_.Windowed,
        render_width, render_height, desc_.Width, desc_.Height);
    if (borderless == wsi::DS3BorderlessResult::Disabled && (!Width || !Height)) {
      RECT client = {};
      if (!GetClientRect(hWnd, &client) || client.right < client.left || client.bottom < client.top)
        return DXGI_ERROR_INVALID_CALL;
      if (!Width) render_width = client.right - client.left;
      if (!Height) render_height = client.bottom - client.top;
    } else if (borderless != wsi::DS3BorderlessResult::Disabled &&
               borderless != wsi::DS3BorderlessResult::Unchanged) {
      return DXGI_ERROR_INVALID_CALL;
    }
    next_desc.Width = render_width;
    next_desc.Height = render_height;
    if (Format != DXGI_FORMAT_UNKNOWN) {
      if (ConvertSwapChainFormat(Format) == WMTPixelFormatInvalid)
        return DXGI_ERROR_INVALID_CALL;
      next_desc.Format = Format;
    }

    SpatialOutputGeometry next_output;
    if (fixed_output_ && !ResolveFixedSpatialOutput(render_width, render_height,
          fixed_output_width_, fixed_output_height_, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION, next_output))
      return E_INVALIDARG;

    auto next_buffer_desc = backbuffer_desc_;
    next_buffer_desc.Width = std::max(1u, render_width);
    next_buffer_desc.Height = std::max(1u, render_height);
    next_buffer_desc.Format = next_desc.Format;
    // All fallible resource preparation precedes publication. Public temporary
    // references clean up failed candidates without dropping the committed set.
    Com<D3D11ResourceCommon> next_public_buffer;
    HRESULT hr = dxmt::CreateDeviceTexture2D(device_, &next_buffer_desc, nullptr,
        reinterpret_cast<ID3D11Texture2D1 **>(&next_public_buffer));
    if (FAILED(hr)) return hr;

    if constexpr (EnableMetalFX) {
      Com<D3D11ResourceCommon> next_upscaled;
      Rc<SpatialScaler> next_scaler;
      // The native spatial descriptor currently uses perceptual color. Keep
      // HDR/scRGB and multisample data on the established presentation path.
      const bool supported_format = next_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
          next_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
          next_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
          next_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
      const bool use_scaler = !fixed_output_ ||
          (next_output.upscale && supported_format && next_desc.SampleDesc.Count == 1 &&
           colorspace_ == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
      if (use_scaler) {
        auto upscaled_desc = next_buffer_desc;
        double scaled_width = fixed_output_ ? next_output.width : double(next_buffer_desc.Width) * scale_factor;
        double scaled_height = fixed_output_ ? next_output.height : double(next_buffer_desc.Height) * scale_factor;
        if (!(scaled_width >= 1 && scaled_width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
              scaled_height >= 1 && scaled_height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION))
          return E_INVALIDARG;
        upscaled_desc.Width = static_cast<UINT>(scaled_width);
        upscaled_desc.Height = static_cast<UINT>(scaled_height);
        hr = dxmt::CreateDeviceTexture2D(device_, &upscaled_desc, nullptr,
            reinterpret_cast<ID3D11Texture2D1 **>(&next_upscaled));
        if (FAILED(hr)) return hr;
        WMTFXSpatialScalerInfo info = {};
        info.input_height = next_buffer_desc.Height;
        info.input_width = next_buffer_desc.Width;
        info.output_height = upscaled_desc.Height;
        info.output_width = upscaled_desc.Width;
        info.color_format = next_public_buffer->texture()->pixelFormat();
        info.output_format = next_upscaled->texture()->pixelFormat();
        next_scaler = new SpatialScaler(device_->GetMTLDevice(), info);
        if (!next_scaler->scaler() || !next_scaler->fence()) {
          if (!fixed_output_) return E_FAIL;
          // A nil optional scaler/fence never reaches encoding and must not
          // reuse a scaler belonging to an earlier buffer generation.
          next_scaler = nullptr;
          next_upscaled = nullptr;
        }
      }
      if (fixed_output_ && !next_scaler)
        WARN("Fixed-output MetalFX unavailable for this buffer; using ordinary presentation");
      upscaled_backbuffer_ = std::move(next_upscaled);
      metalfx_scaler = std::move(next_scaler);
    }
    output_geometry_ = next_output;

    backbuffer_ = next_public_buffer.prvRef();
    backbuffer_desc_ = next_buffer_desc;
    desc_ = next_desc;
    ApplyLayerProps();

    return S_OK;

    });
    TraceResizeState(api.gen, 44 | (1u << 16));
    return result;
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  ResizeTarget(const DXGI_MODE_DESC *pDesc) final {
    cs2trace::ApiScope api(45,trace_serial_,pDesc?(uint64_t(pDesc->Width)<<32)|pDesc->Height:0,pDesc?(uint64_t(pDesc->RefreshRate.Numerator)<<32)|pDesc->RefreshRate.Denominator:0);
    TraceResizeState(api.gen, 45);
    HRESULT result = api.invoke([&]() -> HRESULT {
    if (!pDesc)
      return DXGI_ERROR_INVALID_CALL;

    // Window and display changes may synchronously dispatch a window procedure
    // which calls ResizeTarget again. Share the Enter/Leave transition guard so
    // nested or concurrent target changes cannot wait on our nonrecursive mutex
    // or mutate window_state_ while a fullscreen transition is in progress.
    ModeSetGuard::ModeSetInProgress modeset_inprogress(modeset_guard_);
    if (modeset_inprogress)
      return DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_INVALID_CALL;

    if (fullscreen_desc_.Windowed && wsi::ds3BorderlessEnabled()) {
      if (pending_display_name_[0] || window_state_.captured)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
      // This fallback owns a monitor-sized client, not a virtual target-size
      // surface. Accept only a target already satisfied by that real client.
      // Retaining a speculative target for zero ResizeBuffers would invent an
      // API semantic before the actual DS3 menu sequence has been captured.
      RECT client = {};
      if (!pDesc->Width || !pDesc->Height || !GetClientRect(hWnd, &client) ||
          client.right < client.left || client.bottom < client.top)
        return DXGI_ERROR_INVALID_CALL;
      if (client.right - client.left != pDesc->Width || client.bottom - client.top != pDesc->Height)
        return DXGI_ERROR_UNSUPPORTED;
      return S_OK;
    }

    std::unique_lock<dxmt::mutex> lock(mutex_);

    if (fullscreen_desc_.Windowed && (pending_display_name_[0] || window_state_.captured))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    // Promote display mode
    DXGI_MODE_DESC1 newDisplayMode = {};
    newDisplayMode.Width = pDesc->Width;
    newDisplayMode.Height = pDesc->Height;
    newDisplayMode.RefreshRate = pDesc->RefreshRate;
    newDisplayMode.Format = pDesc->Format;
    newDisplayMode.ScanlineOrdering = pDesc->ScanlineOrdering;
    newDisplayMode.Scaling = pDesc->Scaling;

    // Commit descriptor fields only after the corresponding mode operation.
    if (fullscreen_desc_.Windowed) {
      lock.unlock();
      if (!wsi::resizeWindow(hWnd, &window_state_, newDisplayMode.Width, newDisplayMode.Height))
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
      lock.lock();
    } else {
      HRESULT hr = ChangeDisplayMode(target_.ptr(), &newDisplayMode);
      if (FAILED(hr))
        return hr;
    }
    if (newDisplayMode.RefreshRate.Numerator != 0)
      fullscreen_desc_.RefreshRate = newDisplayMode.RefreshRate;
    fullscreen_desc_.ScanlineOrdering = newDisplayMode.ScanlineOrdering;
    fullscreen_desc_.Scaling = newDisplayMode.Scaling;
    if (!fullscreen_desc_.Windowed) {
      lock = {};
      if (!wsi::updateFullscreenWindow(monitor_, hWnd, false))
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    return S_OK;

    });
    TraceResizeState(api.gen, 45 | (1u << 16));
    return result;
  };

  // Two bounded records per phase: committed GPU state and the Win32 client.
  // No snapshot claims that native view/drawable geometry has been observed.
  void TraceResizeState(uint32_t generation, uint32_t flags) {
    if (!generation) return;
    uint64_t dimensions, texture_dimensions = 0;
    {
      std::lock_guard<dxmt::mutex> lock(buffer_mutex_);
      dimensions = (uint64_t(desc_.Width) << 32) | desc_.Height;
      if (backbuffer_) {
        auto texture = backbuffer_->texture();
        texture_dimensions = (uint64_t(texture->width()) << 32) | texture->height();
      }
    }
    auto t = cs2trace::now();
    cs2trace::record(generation, 61, t, t, trace_serial_, flags, dimensions, texture_dimensions);
    RECT client = {};
    bool valid = GetClientRect(hWnd, &client);
    DWORD owner = GetWindowThreadProcessId(hWnd, nullptr);
    t = cs2trace::now();
    cs2trace::record(generation, 62, t, t, trace_serial_, flags | (valid ? 0 : (1u << 17)),
        (uint64_t(owner) << 32) | GetCurrentThreadId(),
        (uint64_t(uint32_t(client.right - client.left)) << 32) | uint32_t(client.bottom - client.top));
  }

  void ApplyLayerProps() {
    auto target_color_space =
        ConvertColorSpace(desc_.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
                              ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                              : colorspace_,
                          LayerSupportEDR());
    if (presenter->changeLayerProperties(
            ConvertSwapChainFormat(desc_.Format), target_color_space,
            fixed_output_ ? output_geometry_.width : desc_.Width * scale_factor,
            fixed_output_ ? output_geometry_.height : desc_.Height * scale_factor, desc_.SampleDesc.Count,
            fixed_output_
        ))
      device_context_->WaitUntilGPUIdle();
  };

  HRESULT GetOutputFromMonitor(
          HMONITOR                  Monitor,
          IDXGIOutput1**            ppOutput) {
    if (!ppOutput)
      return DXGI_ERROR_INVALID_CALL;

    Com<IDXGIAdapter> adapter;
    Com<IDXGIOutput> output;

    if (FAILED(dxgi_device_->GetAdapter(&adapter)))
      return E_FAIL;

    for (uint32_t i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); i++) {
      DXGI_OUTPUT_DESC outputDesc;
      output->GetDesc(&outputDesc);

      if (outputDesc.Monitor == Monitor)
        return output->QueryInterface(IID_PPV_ARGS(ppOutput));

      output = nullptr;
    }

    return DXGI_ERROR_NOT_FOUND;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetContainingOutput(IDXGIOutput **ppOutput) final {
    InitReturnPtr(ppOutput);

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_INVALID_CALL;

    Com<IDXGIOutput1> output;

    if (target_ == nullptr) {
      HRESULT hr = GetOutputFromMonitor(wsi::getWindowMonitor(hWnd), &output);

      if (FAILED(hr))
        return hr;
    } else {
      output = target_;
    }

    *ppOutput = output.ref();
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetFrameStatistics(DXGI_FRAME_STATISTICS *stats) final {
    DEBUG("DXGISwapChain::GetFrameStatistics: stub");
    std::unique_lock<d3d11_device_mutex> lock(device_->mutex);
    stats->PresentCount = presentation_count_;
    stats->SyncRefreshCount = presentation_count_;
    stats->PresentRefreshCount = presentation_count_;
    stats->SyncGPUTime = {};
    stats->SyncQPCTime = {};
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetLastPresentCount(UINT *last_present_count) final {
    if (last_present_count == NULL) {
      return E_POINTER;
    }
    std::unique_lock<d3d11_device_mutex> lock(device_->mutex);
    *last_present_count = presentation_count_;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc) final {
    if (pDesc == NULL) {
      return E_POINTER;
    }
    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    *pDesc = desc_;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) final {
    if (pDesc == NULL) {
      return E_POINTER;
    }
    *pDesc = fullscreen_desc_;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetHwnd(HWND *pHwnd) final {
    if (pHwnd == NULL) {
      return E_POINTER;
    }
    *pHwnd = hWnd;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetCoreWindow(REFIID refiid, void **ppUnk) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  };

  class SyncFrameState {
    uint64_t frame_id_ = 0;
    CpuFence *frame_latency_fence_ = nullptr;

  public:
    SyncFrameState() {}
    SyncFrameState(uint64_t frame_id, CpuFence *frame_latency_fence)
        : frame_id_(frame_id), frame_latency_fence_(frame_latency_fence) {}

    SyncFrameState(const SyncFrameState &) = delete;
    SyncFrameState(SyncFrameState &&move) {
      frame_id_ = move.frame_id_;
      frame_latency_fence_ = move.frame_latency_fence_;
      move.frame_latency_fence_ = nullptr;
    };
    ~SyncFrameState() {
      if (frame_latency_fence_) {
        frame_latency_fence_->signal(frame_id_);
        frame_latency_fence_ = nullptr;
      }
    }
  };

  SyncFrameState SyncFrame(uint64_t current_frame_id) {
    if (frame_latency_fence_) {
      if (current_frame_id > frame_latency) {
        cs2trace::PresentationScope exact(97,trace_serial_,current_frame_id,current_frame_id-frame_latency,frame_latency);
        cs2trace::Scope trace{.type=8,.object=current_frame_id};
        frame_latency_fence_->wait(current_frame_id - frame_latency);
      }
      return SyncFrameState(current_frame_id, frame_latency_fence_.get());
    }
    return SyncFrameState();
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  Present1(UINT SyncInterval, UINT PresentFlags,
           const DXGI_PRESENT_PARAMETERS *pPresentParameters) final {
    cs2trace::ApiScope api(40,trace_serial_,SyncInterval,PresentFlags);
    return api.invoke([&]() -> HRESULT {
    if (SyncInterval > 4)
      return DXGI_ERROR_INVALID_CALL;

    HRESULT hr = S_OK;
    bool window_minimized = wsi::isMinimized(hWnd);
    DXGI_SWAP_CHAIN_DESC1 present_desc;
    { std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_); present_desc = desc_; }
    if ((window_minimized || present_desc.Width == 0 || present_desc.Height == 0)
        // MSDN: You will not receive DXGI_STATUS_OCCLUDED if you're using a flip model swap chain.
        && present_desc.SwapEffect <= DXGI_SWAP_EFFECT_SEQUENTIAL)
      hr = DXGI_STATUS_OCCLUDED;
    bool should_exit_fs = handle_alt_tab_ // At the moment this is still broken for certain games
                          && !fullscreen_desc_.Windowed && !window_minimized && !wsi::isForeground(hWnd);
    if (hr == S_OK && should_exit_fs)
      hr = DXGI_STATUS_OCCLUDED;
    if (PresentFlags & DXGI_PRESENT_TEST)
      return hr;

    cs2trace::Present trace_present((uint64_t)this);

    if (should_exit_fs)
      SetFullscreenState(FALSE, nullptr);

    std::unique_lock<d3d11_device_mutex> lock(device_->mutex, std::defer_lock);
    { cs2trace::Scope trace{.type=6,.object=(uint64_t)this}; lock.lock(); }
    std::unique_lock<dxmt::mutex> buffer_lock(buffer_mutex_);
    if (!backbuffer_) return DXGI_ERROR_INVALID_CALL;

    { cs2trace::Scope trace{.type=7,.object=(uint64_t)this}; device_context_->PrepareFlush(); }
    if (hr == DXGI_STATUS_OCCLUDED) {
      // flush commands without presenting
      device_context_->Commit();
      return hr;
    }

    HMONITOR timing_monitor = wsi::getWindowMonitor(hWnd);
    if (NeedsDisplayTimingRefresh(timing_monitor))
      RefreshDisplayTiming(timing_monitor);
    const double vsync_duration = PresentationDuration(SyncInterval);

    if (target_) {
      auto output = static_cast<MTLDXGIOutput *>(target_.ptr());
      presenter->changeGammaRamp(output->GetGammaRamp());
    }
    // Pipeline preparation runs on this Present caller. Refuse a known failure
    // before consuming a frame count, latency credit, or queue frame sequence.
    const auto state = presenter->synchronizeLayerProperties();
    if (!state.configuration)
      return E_FAIL;

    // Admit exactly one logical frame before making any of its scene work
    // visible to the GPU. The final presentation chunk owns the admission
    // token until retirement, including missing-drawable/failure paths.
    auto sync_state = SyncFrame(++presentation_count_);
#if defined(DXMT_SCENE_SUBMISSION) && DXMT_SCENE_SUBMISSION
    // Reuse Flush's dirty/promote gate: no extra empty scene chunk. Submission
    // stays on the existing ordered queue; frame completion belongs below.
    { cs2trace::ReasonScope reason(cs2trace::CommitReason::Present,device_->GetDXMTDevice().queue().traceSerial());
      device_context_->Flush(); }
#endif
    auto &cmd_queue = device_->GetDXMTDevice().queue();
    if (auto gen = cs2trace::generation()) {
      auto t = cs2trace::now();
      cs2trace::record(gen,25,t,t,trace_serial_,0,cmd_queue.traceSerial(),cmd_queue.CurrentFrameSeq()-1);
      // Native presenter and queue frame counters have different origins.
      if (cs2trace::profile()==cs2trace::Profile::PresentationWaits)
        cs2trace::record(gen,99,t,t,trace_serial_,0,state.frame_id,cmd_queue.CurrentFrameSeq()-1);
    }
    auto chunk = cmd_queue.CurrentChunk();
    chunk->signal_frame_latency_fence_ = cmd_queue.CurrentFrameSeq();
    if constexpr (EnableMetalFX) {
      chunk->emitcc([
        this, vsync_duration, backbuffer = backbuffer_->texture(),
        sync_state = std::move(sync_state),
        upscaled = upscaled_backbuffer_ && (!fixed_output_ ||
            colorspace_ == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
            ? upscaled_backbuffer_->texture() : Rc<Texture>{},
        scaler = this->metalfx_scaler, state = state
      ](ArgumentEncodingContext &ctx) mutable {
        ScalerInfo scaler_info{};
        if (upscaled && scaler) {
          scaler_info.type = ScalerType::Spatial;
          scaler_info.input_width = backbuffer->width();
          scaler_info.input_height = backbuffer->height();
          scaler_info.output_width = upscaled->width();
          scaler_info.output_height = upscaled->height();
          ctx.updateFrameStatistics([&](auto &statistics) { statistics.last_scaler_info = scaler_info; });
          ctx.upscale(backbuffer, upscaled, scaler);
          ctx.present(upscaled, presenter, vsync_duration, state);
        } else {
          ctx.updateFrameStatistics([&](auto &statistics) { statistics.last_scaler_info = scaler_info; });
          ctx.present(backbuffer, presenter, vsync_duration, state);
        }
        ReleaseSemaphore(present_semaphore_, 1, nullptr);
        this->UpdateStatistics(ctx.queue().statistics, ctx.currentFrameId());
      });
    } else {
      chunk->emitcc([
        this, vsync_duration, state = state,
        sync_state = std::move(sync_state),
        backbuffer = backbuffer_->texture()
      ](ArgumentEncodingContext &ctx) mutable {
        ctx.present(backbuffer, presenter, vsync_duration, state);
        ReleaseSemaphore(present_semaphore_, 1, nullptr);
        this->UpdateStatistics(ctx.queue().statistics, ctx.currentFrameId());
      });
    }
    { cs2trace::ReasonScope reason(cs2trace::CommitReason::Present,cmd_queue.traceSerial());
      cs2trace::Scope trace{.type=10,.object=(uint64_t)this}; device_context_->Commit(); }

    const auto presented_frame = cmd_queue.AdvanceFrame();
    const auto max_latency = cmd_queue.GetMaxLatency();
    buffer_lock.unlock();
    lock.unlock(); // since PresentBoundary() will and should only stall current thread

    cmd_queue.PresentBoundary(presented_frame, max_latency);

    return hr;

    });
  };

  void UpdateStatistics(const FrameStatisticsContainer& statistics, uint64_t frame_id) {
#ifdef DXMT_DEBUG
    const auto snapshot = statistics.snapshot(frame_id);
    const auto &frame = snapshot.frame; // latest completed frame before this present
    const auto &average = snapshot.average;
    hud.begin();
    Flags<FeatureCompatibility> flags = frame.compatibility_flags;
    char text[] = "---------------------------";
    if (flags.test(FeatureCompatibility::UnsupportedGeometryDraw)) {
      text[3] = 'G';
    }
    if (flags.test(FeatureCompatibility::UnsupportedTessellationOutputPrimitive)) {
      text[5] = 'T';
      text[6] = 'O';
    }
    if (flags.test(FeatureCompatibility::UnsupportedIndirectTessellationDraw)) {
      text[8] = 'I';
      text[9] = 'T';
    }
    if (flags.test(FeatureCompatibility::UnsupportedGeometryTessellationDraw)) {
      text[11] = 'G';
      text[12] = 'T';
    }
    if (flags.test(FeatureCompatibility::UnsupportedDrawAuto)) {
      text[14] = 'A';
    }
    if (flags.test(FeatureCompatibility::UnsupportedPredication)) {
      text[16] = 'P';
    }
    if (flags.test(FeatureCompatibility::UnsupportedStreamOutputAppending)) {
      text[18] = 'S';
      text[19] = 'A';
    }
    if (flags.test(FeatureCompatibility::UnsupportedMultipleStreamOutput)) {
      text[21] = 'M';
      text[22] = 'S';
    }
    hud.printLine(text);
    hud.printLine(std::format(
        "Commit: {:2} -{:4.1f} -{:4.1f}", std::min(frame.command_buffer_count, 99u),
        std::min(average.commit_interval.count() / 1000000.0, 99.9),
        std::min(snapshot.max.commit_interval.count() / 1000000.0, 99.9)
    ));
    hud.printLine(std::format(
        "Sync:   {:2} {:4.1f}  {:2} {:4.1f} {:2}", std::min(frame.sync_count, 99u),
        std::min(average.sync_interval.count() / 1000000.0, 99.9), std::min(snapshot.max.event_stall, 99u),
        std::min(average.present_latency_interval.count() / 1000000.0, 99.9), frame.latency
    ));
    hud.printLine(std::format(
        "Encode: {:4.1f}+{:4.1f}+{:4.1f}={:4.1f}", std::min(average.encode_prepare_interval.count() / 1000000.0, 99.9),
        std::min((average.encode_flush_interval - average.present_encode_interval).count() / 1000000.0, 99.9),
        std::min(average.present_encode_interval.count() / 1000000.0, 99.9),
        std::min((average.encode_prepare_interval + average.encode_flush_interval).count() / 1000000.0, 99.9)
    ));
    hud.printLine(std::format(
        "Render:{:3}+{:<3} Clear:{:3}+{:<2}", std::min(frame.render_pass_count - frame.render_pass_optimized, 999u),
        std::min(frame.render_pass_optimized, 999u),
        std::min(frame.clear_pass_count - frame.clear_pass_optimized, 999u), std::min(frame.clear_pass_optimized, 99u)
    ));
    {
      /* scaler info */
      auto &info = frame.last_scaler_info;
      if (info.type == ScalerType::Temporal) {
        auto &info = frame.last_scaler_info;
        hud.printLine(std::format(
            "MetalFX: Temporal {} {}", info.auto_exposure ? "AEXP" : "", info.motion_vector_highres ? "HMV" : ""
        ));
        hud.printLine(std::format(
            "Scale: {:4}x{:4}->{:4}x{:4}", info.input_width, info.input_height, info.output_width, info.output_height
        ));
      }
      if (info.type == ScalerType::Spatial) {
        hud.printLine(std::format(
            "MetalFX: Spatial"
        ));
        hud.printLine(std::format(
            "Scale: {:4}x{:4}->{:4}x{:4}", info.input_width, info.input_height, info.output_width, info.output_height
        ));
      }
    }
    hud.end();
#else
    (void)statistics;
    (void)frame_id;
#endif
  }

  BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() final { return FALSE; };

  HRESULT
  STDMETHODCALLTYPE
  GetRestrictToOutput(IDXGIOutput **ppRestrictToOutput) final {
    InitReturnPtr(ppRestrictToOutput);
    ERR("DXGISwapChain1::GetRestrictToOutput: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  SetBackgroundColor(const DXGI_RGBA *pColor) final {
    ERR("DXGISwapChain1::SetBackgroundColor: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetBackgroundColor(DXGI_RGBA *pColor) final {
    if (!pColor)
      return E_INVALIDARG;
    // TODO(swapchain): check if native returns transparent or opaque black
    *pColor = {0, 0, 0, 0};
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  SetRotation(DXGI_MODE_ROTATION Rotation) final {
    ERR("DXGISwapChain1::SetRotation: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetRotation(DXGI_MODE_ROTATION *pRotation) final {
    if (!pRotation)
      return E_INVALIDARG;
    *pRotation = DXGI_MODE_ROTATION_IDENTITY;
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  SetSourceSize(UINT Width, UINT Height) override {
    ERR("DXGISwapChain2::SetSourceSize: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  GetSourceSize(UINT *pWidth, UINT *pHeight) override {
    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    if (pWidth)
      *pWidth = desc_.Width;
    if (pHeight)
      *pHeight = desc_.Height;
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT max_latency) override {
    if (max_latency == 0 || max_latency > DXGI_MAX_SWAP_CHAIN_BUFFERS) {
      return E_INVALIDARG;
    }
    if (max_latency > frame_latency) {
      ReleaseSemaphore(present_semaphore_, max_latency - frame_latency,
                       nullptr);
    }
    frame_latency = max_latency;

    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *max_latency) override {
    if (max_latency) {
      *max_latency = frame_latency;
    }
    return S_OK;
  };

  HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override {
    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)) {
      return nullptr;
    }

    HANDLE result = nullptr;
    HANDLE processHandle = GetCurrentProcess();

    if (!DuplicateHandle(processHandle, present_semaphore_, processHandle,
                         &result, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      return nullptr;
    }

    return result;
  };

  HRESULT STDMETHODCALLTYPE
  SetMatrixTransform(const DXGI_MATRIX_3X2_F *matrix) override {
    ERR("DXGISwapChain2::SetMatrixTransform: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  GetMatrixTransform(DXGI_MATRIX_3X2_F *matrix) override {
    ERR("DXGISwapChain2::GetMatrixTransform: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
      DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) override {
    if (!pColorSpaceSupport)
      return E_INVALIDARG;
    *pColorSpaceSupport = CGColorSpace_checkColorSpaceSupported(ConvertColorSpace(ColorSpace, false))
            ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT
            : 0;
    return S_OK;
  };

  UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {
    // TODO(swapchain): can be non-zero once sequential swapchain is implemented
    return 0;
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                         DXGI_FORMAT Format, UINT SwapChainFlags,
                         const UINT *pCreationNodeMask,
                         IUnknown *const *ppPresentQueue) override {
    WARN("DXGISwapChain3::ResizeBuffers1: ignoring d3d12 related parameters");
    return ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
  }

  HRESULT STDMETHODCALLTYPE
  SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override {
    // Match Present/Resize lock order so a queued frame captures its color and
    // optional scaler decision from one generation.
    std::unique_lock<d3d11_device_mutex> device_lock(device_->mutex);
    std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
    auto target_color_space = ConvertColorSpace(ColorSpace, LayerSupportEDR());
    if (presenter->changeLayerColorSpace(target_color_space))
      device_context_->WaitUntilGPUIdle();
    colorspace_ = ColorSpace;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type,
                                           UINT Size,
                                           void *pMetaData) override {
    if (Type == DXGI_HDR_METADATA_TYPE_NONE) {
      if (Size != 0)
        return E_INVALIDARG;
      presenter->changeHDRMetadata(nullptr);
      return S_OK;
    }
    if (Type == DXGI_HDR_METADATA_TYPE_HDR10) {
      if (Size != sizeof(DXGI_HDR_METADATA_HDR10) || !pMetaData)
        return E_INVALIDARG;
      // The API payload need not be aligned and is not a WMTHDRMetadata object.
      // Copy it before conversion; the presenter owns a further locked copy and
      // publishes immutable per-frame state rather than retaining caller bytes.
      DXGI_HDR_METADATA_HDR10 metadata;
      std::memcpy(&metadata, pMetaData, sizeof(metadata));
      const WMTHDRMetadata converted = {
          {metadata.RedPrimary[0], metadata.RedPrimary[1]},
          {metadata.GreenPrimary[0], metadata.GreenPrimary[1]},
          {metadata.BluePrimary[0], metadata.BluePrimary[1]},
          {metadata.WhitePoint[0], metadata.WhitePoint[1]},
          metadata.MaxMasteringLuminance, metadata.MinMasteringLuminance,
          metadata.MaxContentLightLevel, metadata.MaxFrameAverageLightLevel};
      presenter->changeHDRMetadata(&converted);
      return S_OK;
    }
    return DXGI_ERROR_UNSUPPORTED;
  }

private:
  const uint64_t trace_serial_=cs2trace::newSerial();
  bool LayerSupportEDR() {
    WMTEDRValue edr_value;
    MetalLayer_getEDRValue(layer_weak_, &edr_value);
    return edr_value.maximum_potential_edr_color_component_value > 1.0f;
  };

  Com<IDXGIFactory1> factory_;
  Com<IMTLDXGIDevice> dxgi_device_;
  WMT::Object native_view_;
  WMT::MetalLayer layer_weak_;
  ULONG presentation_count_;
  DXGI_SWAP_CHAIN_DESC1 desc_;
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc_ = {};
  D3D11_TEXTURE2D_DESC1 backbuffer_desc_;
  IMTLD3D11DeviceContext* device_context_;
  Com<D3D11ResourceCommon, false> backbuffer_;
  HANDLE present_semaphore_ = nullptr;
  std::unique_ptr<CpuFence> frame_latency_fence_;
  HWND hWnd;
  HMONITOR monitor_;
  // Logical windowed state does not discharge a failed physical display restore.
  WCHAR pending_display_name_[32] = {};
  Com<IDXGIOutput1> target_;
  wsi::DXMTWindowState window_state_;
  uint32_t frame_latency;
  DXGI_COLOR_SPACE_TYPE colorspace_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  dxmt::mutex display_timing_mutex_;
  double display_refresh_rate_ = 60.0;
  bool display_timing_valid_ = false;
  uint64_t display_timing_generation_ = 0;
  HMONITOR display_timing_monitor_ = nullptr;
  int preferred_max_frame_rate = 0;
  HUDState hud;
  Rc<Presenter> presenter;
  ModeSetGuard modeset_guard_;
  dxmt::mutex mutex_;
  dxmt::mutex buffer_mutex_;

  bool handle_alt_tab_;
  HRESULT initialization_result_ = S_OK;

  std::conditional<EnableMetalFX, Rc<SpatialScaler>, std::monostate>::type metalfx_scaler;
  std::conditional<EnableMetalFX, Com<D3D11ResourceCommon>, std::monostate>::type upscaled_backbuffer_;
  float scale_factor = 1.0;
  bool fixed_output_ = false;
  int fixed_output_width_ = 0;
  int fixed_output_height_ = 0;
  SpatialOutputGeometry output_geometry_;
};

HRESULT
CreateSwapChain(
    IDXGIFactory1 *pFactory, MTLD3D11Device *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGISwapChain1 **ppSwapChain
) {
  if (ppSwapChain == NULL)
    return DXGI_ERROR_INVALID_CALL;
  InitReturnPtr(ppSwapChain);
  if (pDesc == NULL)
    return DXGI_ERROR_INVALID_CALL;

  DWORD window_process_id;
  GetWindowThreadProcessId(hWnd, &window_process_id);
  if (GetProcessId(GetCurrentProcess()) != window_process_id) {
    ERR("CreateSwapChain: cross-process swapchain not supported yet");
    return E_FAIL;
  }

  // Initial fullscreen descriptors bypass SetFullscreenState. The admitted
  // DS3 borderless route must refuse these before constructing an exclusive
  // swapchain or performing any output/mode/window mutation.
  if (pFullscreenDesc && !pFullscreenDesc->Windowed && wsi::ds3BorderlessEnabled())
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

  Com<IMTLDXGIDevice> layer_factory;
  if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&layer_factory)))) {
    ERR("CreateSwapChain: failed to get IMTLDXGIDevice");
    return E_FAIL;
  }
  DXGI_SWAP_CHAIN_DESC1 borderless_desc = *pDesc;
  if ((!pFullscreenDesc || pFullscreenDesc->Windowed) && wsi::ds3BorderlessEnabled()) {
    // Snapshot initial size without geometry mutation. The constructor applies
    // borderless geometry only after successful allocation.
    auto result = wsi::resolveDS3RenderSize(hWnd, true, borderless_desc.Width, borderless_desc.Height);
    WARN("DS3 native borderless initial size result ", static_cast<int>(result),
         " render ", borderless_desc.Width, "x", borderless_desc.Height);
    if (result != wsi::DS3BorderlessResult::Applied && result != wsi::DS3BorderlessResult::Unchanged)
      return E_FAIL;
    pDesc = &borderless_desc;
  }

  if ((pDesc->SwapEffect != DXGI_SWAP_EFFECT_DISCARD &&
       pDesc->SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD) &&
      pDesc->BufferCount != 1) {
    WARN("CreateSwapChain: unsupported swap effect ", pDesc->SwapEffect, " with backbuffer size ", pDesc->BufferCount);
  }
  if (env::getEnvVar("DXMT_METALFX_SPATIAL_SWAPCHAIN") == "1") {
    if (pDevice->GetMTLDevice().supportsFXSpatialScaler()) {
      auto *swapchain = new MTLD3D11SwapChain<true>(
          pFactory, pDevice, layer_factory.ptr(), hWnd, pDesc, pFullscreenDesc
      );
      HRESULT initialized = swapchain->InitializationResult();
      if (FAILED(initialized)) { swapchain->Release(); return initialized; }
      *ppSwapChain = swapchain;
      return S_OK;
    } else {
      WARN("MetalFX spatial scaler is not supported on this device");
    }
  }
  auto *swapchain = new MTLD3D11SwapChain<false>(
      pFactory, pDevice, layer_factory.ptr(), hWnd, pDesc, pFullscreenDesc
  );
  HRESULT initialized = swapchain->InitializationResult();
  if (FAILED(initialized)) { swapchain->Release(); return initialized; }
  *ppSwapChain = swapchain;
  return S_OK;
};

} // namespace dxmt
