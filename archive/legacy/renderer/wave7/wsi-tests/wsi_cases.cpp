using dxmt::wsi::DXMTWindowState;
namespace wsi = dxmt::wsi;
static int scenarios;
static HANDLE capture_identity(const DXMTWindowState &state) {
#if WSI_CAPTURE_GENERATIONS
  return state.capture_token;
#else
  return reinterpret_cast<HANDLE>(const_cast<DXMTWindowState *>(&state));
#endif
}
static void pass(const std::string &name) { ++scenarios; std::cout << "PASS " << name << '\n'; }
static void original_restored(const fake::Window &original, const DXMTWindowState &state) {
  fake::require(fake::window.style == original.style, "style was not restored");
  fake::require(fake::window.exstyle == original.exstyle, "exstyle/topmost was not restored");
  fake::require(fake::eq(fake::window.rect, original.rect), "coordinates were not restored");
  fake::require(fake::empty(fake::window.present), "present rectangle was not cleared");
  fake::require(!state.captured && fake::window.props.empty(), "cleanup obligation was not retired");
}
// Capture identity belongs to a generation; retain each state at its capture address.
static void enter_into(DXMTWindowState &state) {
  fake::require(wsi::enterFullscreenMode(fake::monitor, fake::hwnd, &state, true), "entry failed");
}
static void entry_failures() {
  for (const char *failure : {"desktop_rect", "get_rect", "get_style", "get_exstyle", "set_prop",
                              "present_rect", "set_style", "set_exstyle", "position"}) {
    fake::reset(); fake::Window original = fake::window; DXMTWindowState state;
    fake::inject(failure);
    fake::require(!wsi::enterFullscreenMode(fake::monitor, fake::hwnd, &state, true),
                  std::string("entry ignored ") + failure);
    fake::require(fake::failed, std::string("unexercised injection ") + failure);
    fake::failure.clear();
    fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "partial entry cleanup failed");
    original_restored(original, state);
    pass(std::string("enter failure and cleanup: ") + failure);
  }
}
static void restore_failures() {
  for (const char *failure : {"get_style", "get_exstyle", "set_style", "set_exstyle", "present_rect", "position"}) {
    fake::reset(); fake::Window original = fake::window; DXMTWindowState state; enter_into(state);
    fake::inject(failure);
    fake::require(!wsi::leaveFullscreenMode(fake::hwnd, &state, true),
                  std::string("leave ignored ") + failure);
    fake::require(fake::failed && state.captured, "failed restore lost its retry state");
    fake::failure.clear();
    fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "leave retry failed");
    original_restored(original, state);
    fake::events.clear();
    fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "idempotent leave failed");
    fake::require(fake::events.empty(), "completed leave repeated window calls");
    pass(std::string("leave failure and retry: ") + failure);
  }
}
static void zero_returns() {
  fake::reset(); fake::window.style = 0; fake::window.exstyle = 0;
  fake::Window original = fake::window; DXMTWindowState state;
  SetLastError(5);
  enter_into(state);
  fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "zero previous style rejected");
  original_restored(original, state); pass("Get/SetWindowLong zero return with ERROR_SUCCESS");
}
static void identity_lifecycle() {
  for (bool reuse : {false, true}) {
    fake::reset(); DXMTWindowState state; enter_into(state);
    fake::window.alive = false; fake::window.props.clear();
    if (reuse) fake::replacement();
    fake::Window replacement = fake::window; fake::events.clear();
    fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "dead owner cleanup failed");
    fake::require(fake::events.empty(), "dead/reused HWND received a mutation");
    fake::require(fake::window.style == replacement.style && fake::window.exstyle == replacement.exstyle &&
                  fake::eq(fake::window.rect, replacement.rect), "recycled window was modified");
    fake::require(!state.captured, "dead owner state not retired");
    pass(reuse ? "HWND reused after destruction" : "HWND destroyed before leave");
  }
  for (const char *boundary : {"present_rect", "set_style", "set_exstyle", "position"}) {
    fake::reset(); DXMTWindowState state;
    bool replaced = false;
    fake::after_call = [&](const std::string &name) {
      if (!replaced && name == boundary) { fake::replacement(); replaced = true; }
    };
    bool result = wsi::enterFullscreenMode(fake::monitor, fake::hwnd, &state, true);
    fake::require(replaced, "replacement boundary was not reached");
    fake::require(!result, "entry committed after its HWND was recycled");
    fake::after_call = {}; fake::events.clear();
    fake::Window replacement = fake::window;
    fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "recycled cleanup failed");
    fake::require(fake::events.empty() && fake::window.style == replacement.style &&
                  fake::window.exstyle == replacement.exstyle && fake::eq(fake::window.rect, replacement.rect),
                  "cleanup modified recycled window");
    pass(std::string("HWND reuse after enter API: ") + boundary);
  }
  fake::reset(); DXMTWindowState first, second; enter_into(first);
  fake::events.clear();
  fake::require(!wsi::enterFullscreenMode(fake::monitor, fake::hwnd, &second, true), "second owner stole token");
  fake::require(!second.captured && first.captured, "first cleanup state changed");
  fake::require(wsi::leaveFullscreenMode(fake::hwnd, &first, true), "first owner could not leave");
  pass("one cleanup owner per HWND");
}
static void display_restore() {
  for (const char *failure : {"display_name", "enum_current", "enum_registry", "display_restore"}) {
    fake::reset(); fake::inject(failure);
    fake::require(!wsi::restoreDisplayMode(fake::monitor), std::string("display restore ignored ") + failure);
    fake::require(fake::failed && !fake::malformed_devmode, "display failure injection not exercised or DEVMODE invalid");
    fake::failure.clear();
    fake::require(wsi::restoreDisplayMode(fake::monitor), "display restore retry failed");
    fake::require(!fake::malformed_devmode, "EnumDisplaySettings saw uninitialized DEVMODE");
    pass(std::string("display restore failure and retry: ") + failure);
  }
  fake::reset();
  fake::require(!wsi::restoreDisplayMode(static_cast<const WCHAR *>(nullptr)), "null device accepted");
  fake::require(!wsi::restoreDisplayMode(L""), "empty device accepted");
  fake::require(fake::enum_count == 0, "invalid display name queried");
  pass("invalid stable display name fails before enumeration");
}
static void application_style_change() {
  fake::reset(); DXMTWindowState state; enter_into(state);
  fake::window.style |= 0x800; fake::window.exstyle |= 0x4000;
  LONG appstyle = fake::window.style, appexstyle = fake::window.exstyle & ~WS_EX_TOPMOST;
  fake::require(wsi::leaveFullscreenMode(fake::hwnd, &state, true), "app-owned style cleanup failed");
  fake::require(fake::window.style == appstyle && fake::window.exstyle == appexstyle,
                "application style change overwritten");
  pass("application style changes survive leave");
}
static void swapchain_entry_failures() {
  for (const char *failure : {"get_output", "output_desc", "closest_mode", "display_name", "monitor_info",
                             "display_set", "present_rect", "set_style", "set_exstyle", "position"}) {
    fake::reset(); fake::Window original = fake::window; FixtureSwapchain chain;
    fake::inject(failure, 1, std::string(failure) == "display_set" ? 2 : 1);
    HRESULT hr = chain.SetFullscreenState(TRUE, nullptr);
    fake::require(FAILED(hr) && fake::failed, std::string("swapchain ignored entry fault ") + failure);
    fake::require(chain.fullscreen_desc_.Windowed, "failed entry reports fullscreen");
    if (chain.pending_display_name_[0] || chain.window_state_.captured) {
      fake::failure.clear(); fake::events.clear();
      fake::require(FAILED(chain.SetFullscreenState(TRUE, nullptr)), "reentry ignored pending cleanup");
      fake::require(std::find(fake::events.begin(),fake::events.end(),"display_set") == fake::events.end(),
                    "reentry overwrote pending display mode");
      DXGI_MODE_DESC resize = {1024,768,{75,1},DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
      fake::require(FAILED(chain.ResizeTarget(&resize)), "windowed resize ignored pending cleanup");
      fake::require(chain.fullscreen_desc_.RefreshRate.Numerator == 60, "rejected resize committed refresh");
    }
    fake::failure.clear();
    fake::require(chain.SetFullscreenState(FALSE, nullptr) == S_OK, "failed entry could not clean up");
    original_restored(original, chain.window_state_);
    fake::require(!chain.pending_display_name_[0], "display cleanup obligation remained");
    pass(std::string("swapchain entry failure, blocked reentry and cleanup: ") + failure);
  }
}
static void swapchain_display_retry() {
  for (const char *failure : {"enum_current", "enum_registry", "display_restore"}) {
    fake::reset(); fake::Window original = fake::window; FixtureSwapchain chain;
    fake::require(chain.SetFullscreenState(TRUE, nullptr) == S_OK, "swapchain entry failed");
    fake::require(!chain.fullscreen_desc_.Windowed && chain.pending_display_name_[0], "entry not committed");
    fake::inject(failure);
    fake::require(FAILED(chain.SetFullscreenState(FALSE, nullptr)), "failed display restore reported success");
    fake::require(fake::failed && chain.fullscreen_desc_.Windowed && chain.target_.ptr() == nullptr,
                  "failed display exit logical state wrong");
    fake::require(std::wcscmp(chain.pending_display_name_, L"DISPLAY1") == 0, "display identity lost");
    original_restored(original, chain.window_state_);
    // Even if the current HWND/monitor identity is unavailable, retry uses the
    // stored device name instead of deriving it from the replacement window.
    fake::inject("display_name");
    fake::require(chain.SetFullscreenState(FALSE, nullptr) == S_OK, "windowed FALSE did not retry display");
    fake::require(!fake::failed && !chain.pending_display_name_[0], "retry queried unstable display identity");
    pass(std::string("swapchain display retry after logical exit: ") + failure);
  }
  fake::reset(); FixtureSwapchain chain; enter_into(chain.window_state_);
  chain.fullscreen_desc_.Windowed = FALSE;
  fake::inject("position");
  fake::require(FAILED(chain.SetFullscreenState(FALSE, nullptr)), "window cleanup failure reported success");
  fake::require(chain.fullscreen_desc_.Windowed && chain.window_state_.captured, "window retry obligation lost");
  fake::failure.clear();
  fake::require(chain.SetFullscreenState(FALSE, nullptr) == S_OK && !chain.window_state_.captured,
                "windowed FALSE did not retry window cleanup");
  pass("swapchain window cleanup retry after logical exit");
}
static void swapchain_resize_and_guard() {
  fake::reset(); FixtureSwapchain chain;
  fake::require(chain.SetFullscreenState(TRUE, nullptr) == S_OK, "resize setup entry failed");
  DXGI_MODE_DESC resize = {1024,768,{75,0},DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
  fake::inject("closest_mode");
  fake::require(FAILED(chain.ResizeTarget(&resize)) && fake::failed, "failed closest mode reported success");
  fake::require(chain.fullscreen_desc_.RefreshRate.Numerator == 60 &&
                chain.fullscreen_desc_.ScanlineOrdering == 0 && chain.fullscreen_desc_.Scaling == 0,
                "failed ResizeTarget committed descriptor");
  fake::failure.clear();
  fake::require(chain.ResizeTarget(&resize) == S_OK, "successful ResizeTarget failed");
  fake::require(chain.fullscreen_desc_.RefreshRate.Numerator == 75 &&
                chain.fullscreen_desc_.RefreshRate.Denominator == 1 && chain.fullscreen_desc_.Scaling == 1,
                "successful ResizeTarget did not commit selected mode");
  fake::require(chain.SetFullscreenState(FALSE, nullptr) == S_OK, "resize cleanup failed");
  pass("ResizeTarget commits selected descriptor after mode success");
  fake::reset(); FixtureSwapchain guarded;
  HRESULT nested = E_FAIL; bool observed = false;
  fake::after_call = [&](const std::string &name) {
    if (!observed && name == "present_rect") {
      observed = true; nested = guarded.ResizeTarget(&resize);
    }
  };
  fake::require(guarded.SetFullscreenState(TRUE, nullptr) == S_OK && observed,
                "nested ResizeTarget seam not reached");
  fake::require(nested == DXGI_STATUS_MODE_CHANGE_IN_PROGRESS &&
                guarded.fullscreen_desc_.RefreshRate.Numerator == 60,
                "nested ResizeTarget bypassed transition guard");
  fake::after_call = {};
  fake::require(guarded.SetFullscreenState(FALSE, nullptr) == S_OK, "guarded cleanup failed");
  pass("actual ModeSetGuard rejects synchronous nested ResizeTarget");
  fake::reset(); FixtureSwapchain invalid; IDXGIOutput1 output;
  fake::require(invalid.SetFullscreenState(FALSE, &output) == DXGI_ERROR_INVALID_CALL &&
                fake::events.empty(), "windowed target invalid call mutated state");
  fake::inject("query_output");
  fake::require(invalid.SetFullscreenState(TRUE, &output) == E_FAIL && fake::failed,
                "QueryInterface error ignored");
  pass("SetFullscreenState validates target and propagates interface error");
}
static void destruction_cleanup() {
  for (const char *failure : {"set_style", "present_rect", "position"}) {
    fake::reset(); auto chain = std::make_unique<FixtureSwapchain>();
    fake::require(chain->SetFullscreenState(TRUE,nullptr) == S_OK, "destruction setup entry failed");
    fake::inject(failure);
    HRESULT hr = chain->CleanupFullscreenForDestruction();
    fake::require(FAILED(hr) && fake::failed && fake::warnings > 0,
                  "destruction concealed incomplete physical cleanup");
    fake::require(chain->window_state_.captured, "destruction claimed failed physical cleanup complete");
    fake::require(GetPropW(fake::hwnd,L"DXMTFullscreenState") == nullptr,
                  "destruction retained pointer to member being freed");
    chain.reset(); fake::failure.clear();
    FixtureSwapchain next;
    fake::require(next.SetFullscreenState(TRUE,nullptr) == S_OK,
                  "next swapchain blocked by destroyed owner marker");
    fake::require(next.SetFullscreenState(FALSE,nullptr) == S_OK, "next swapchain exit failed");
    pass(std::string("terminal destruction detach after physical failure: ") + failure);
  }
  fake::reset(); FixtureSwapchain refused;
  fake::require(refused.SetFullscreenState(TRUE,nullptr) == S_OK, "refused-detach setup failed");
  fake::inject("set_style"); fake::additional_failure = "remove_prop";
  fake::events.clear();
  fake::require(FAILED(refused.CleanupFullscreenForDestruction()) && fake::warnings >= 2,
                "RemoveProp refusal was not reported");
  fake::require(GetPropW(fake::hwnd,L"DXMTFullscreenState") == capture_identity(refused.window_state_) &&
                refused.window_state_.style_pending, "refused detach lost unresolved obligation");
  fake::require(std::find(fake::events.begin(),fake::events.end(),"set_prop") == fake::events.end(),
                "terminal detach overwrote a window property");
  fake::failure.clear(); fake::additional_failure.clear();
  fake::require(wsi::detachFullscreenState(fake::hwnd,&refused.window_state_), "detach retry failed");
  pass("terminal RemoveProp refusal is explicit with physical state preserved");

  fake::reset(); DXMTWindowState state; enter_into(state);
  fake::inject("remove_prop");
  fake::require(!wsi::leaveFullscreenMode(fake::hwnd,&state,true) && fake::failed && state.captured,
                "successful physical leave lost failed property removal retry");
  fake::require(GetPropW(fake::hwnd,L"DXMTFullscreenState") == capture_identity(state),
                "failed removal unexpectedly changed owner marker");
  fake::failure.clear();
  fake::require(wsi::leaveFullscreenMode(fake::hwnd,&state,true) && !state.captured,
                "property removal retry failed");
  pass("normal leave retains property-removal failure for retry");

  fake::reset(); FixtureSwapchain display;
  fake::require(display.SetFullscreenState(TRUE,nullptr) == S_OK, "terminal display setup failed");
  fake::inject("display_restore");
  fake::require(FAILED(display.CleanupFullscreenForDestruction()) && fake::failed && fake::warnings > 0,
                "terminal display failure not reported");
  fake::require(display.pending_display_name_[0] && !display.window_state_.captured &&
                GetPropW(fake::hwnd,L"DXMTFullscreenState") == nullptr,
                "terminal display failure misstated restoration or retained member token");
  pass("terminal display restore failure preserves its unresolved display identity");
}
static void detach_identity() {
  for (bool reuse : {false,true}) {
    fake::reset(); DXMTWindowState first, replacement; enter_into(first);
    if (reuse) fake::replacement();
    fake::window.props[L"DXMTFullscreenState"] = &replacement;
    fake::events.clear();
    fake::require(wsi::detachFullscreenState(fake::hwnd,&first), "foreign owner detach failed");
    fake::require(fake::events.empty() && GetPropW(fake::hwnd,L"DXMTFullscreenState") == &replacement,
                  "detach removed another owner's marker");
    pass(reuse ? "terminal detach preserves recycled HWND marker" : "terminal detach preserves another owner marker");
  }
  fake::reset(); DXMTWindowState state, replacement; enter_into(state);
  fake::after_call = [&](const std::string &name) {
    if (name == "remove_prop") fake::window.props[L"DXMTFullscreenState"] = &replacement;
  };
  fake::require(wsi::detachFullscreenState(fake::hwnd,&state), "callback replacement detach failed");
  fake::require(GetPropW(fake::hwnd,L"DXMTFullscreenState") == &replacement,
                "detach erased marker installed during RemoveProp callback");
  fake::require(std::count(fake::events.begin(),fake::events.end(),"remove_prop") == 1,
                "detach removed replacement marker with repeated API call");
  fake::after_call = {};
  pass("terminal detach preserves marker replaced during removal callback");
  fake::reset(); DXMTWindowState orphan;
#if WSI_CAPTURE_GENERATIONS
  orphan.capture_token = DXMTAllocateFullscreenToken();
#endif
  fake::window.props[L"DXMTFullscreenState"] = capture_identity(orphan);
  fake::require(!orphan.captured && wsi::detachFullscreenState(fake::hwnd,&orphan) &&
                GetPropW(fake::hwnd,L"DXMTFullscreenState") == nullptr,
                "terminal exact-token detach depended on lost capture flag");
  pass("terminal exact-token detach handles cleared capture metadata");
}
static void capture_obligation_ack() {
  for (const char *failure : {"capture_pending", "capture_unsupported", "capture_no_marker"}) {
    for (bool equal : {false,true}) {
      fake::reset(); FixtureSwapchain chain;
      fake::require(chain.SetFullscreenState(TRUE,nullptr) == S_OK, "capture cleanup setup failed");
      fake::equal_mode = equal;
      fake::inject(failure,1,10);
      fake::require(FAILED(chain.SetFullscreenState(FALSE,nullptr)) && fake::failed,
                    "unacknowledged native capture cleanup reported success");
      fake::require(chain.pending_display_name_[0] && chain.fullscreen_desc_.Windowed &&
                    !chain.window_state_.captured, "native lease failure lost separate obligations");
      fake::require(fake::display_changes >= 2, "missing explicit restore attempt for unacknowledged cleanup");
      fake::failure.clear();
      fake::require(chain.SetFullscreenState(FALSE,nullptr) == S_OK && !chain.pending_display_name_[0],
                    "acknowledged capture retry did not retire obligation");
      pass(std::string("native capture obligation ") + failure + (equal ? " equal mode" : " changed mode"));
    }
  }
  fake::reset(); fake::equal_mode = true;
  fake::require(wsi::restoreDisplayMode(L"DISPLAY1") && fake::display_changes == 0,
                "acknowledged equal mode repeated unnecessary display change");
  pass("equal mode fastpath requires independent native cleanup receipt");
}
static void checked_window_updates() {
  for (const char *failure : {"get_rect", "get_style", "get_exstyle", "adjust_rect", "position"}) {
    fake::reset(); DXMTWindowState state;
    fake::inject(failure);
    fake::require(!wsi::resizeWindow(fake::hwnd, &state, 800, 600) && fake::failed,
                  std::string("unchecked resize failure: ") + failure);
    pass(std::string("checked window resize: ") + failure);
  }
  for (bool topmost : {false, true}) {
    for (const char *failure : {"desktop_rect", "present_rect", "position"}) {
      fake::reset(); DXMTWindowState state; enter_into(state); fake::inject(failure);
      fake::require(!wsi::updateFullscreenWindow(fake::monitor, fake::hwnd, topmost) && fake::failed,
                    std::string("unchecked fullscreen update: ") + failure);
      pass(std::string("checked fullscreen update: ") + failure + (topmost ? " topmost" : " move"));
    }
  }
  for (bool fullscreen : {false,true}) {
    fake::reset(); DXMTWindowState state;
    if (fullscreen) enter_into(state);
    fake::after_call = [&](const std::string &name) {
      if (name == "position") fake::window.alive = false;
    };
    bool result = fullscreen ? wsi::updateFullscreenWindow(fake::monitor, fake::hwnd, false)
                             : wsi::resizeWindow(fake::hwnd, &state, 800, 600);
    fake::require(!result && !fake::window.alive, "HWND destruction reported success");
    pass(fullscreen ? "fullscreen update notices callback HWND destruction" : "window resize notices callback HWND destruction");
  }
  fake::reset(); fake::window.style = fake::window.exstyle = 0;
  DXMTWindowState state;
  fake::require(wsi::resizeWindow(fake::hwnd, &state, 800,600), "zero style resize falsely failed");
  pass("checked resize accepts zero styles with ERROR_SUCCESS");
}
static void fullscreen_update_identity() {
  for (bool topmost : {false, true}) {
    for (const char *boundary : {"present_rect", "position"}) {
      for (bool replacement_owner : {false, true}) {
        fake::reset(); DXMTWindowState state, replacement_state; enter_into(state);
        bool replaced=false; fake::Window replacement;
        fake::events.clear();
        fake::after_call = [&](const std::string &name) {
          if (!replaced && name == boundary) {
            fake::replacement();
            if (replacement_owner)
              fake::window.props[L"DXMTFullscreenState"] = &replacement_state;
            replacement=fake::window; replaced=true;
          }
        };
        fake::require(!wsi::updateFullscreenWindow(fake::monitor,fake::hwnd,topmost),
                      "fullscreen update accepted a recycled HWND");
        fake::require(replaced, "fullscreen replacement boundary not exercised");
        fake::require(fake::eq(fake::window.rect,replacement.rect) &&
                      fake::window.style==replacement.style && fake::window.exstyle==replacement.exstyle &&
                      fake::window.props==replacement.props && fake::eq(fake::window.present,replacement.present),
                      "fullscreen update modified replacement window after owner loss");
        fake::after_call={}; fake::events.clear();
        fake::require(wsi::leaveFullscreenMode(fake::hwnd,&state,true) && fake::events.empty(),
                      "old fullscreen owner cleanup changed replacement window");
        pass(std::string("fullscreen update rejects recycled HWND after ") + boundary +
             (topmost ? " topmost" : " move") + (replacement_owner ? " new owner" : " no owner"));
      }
    }
  }
}
// This injected ABA case deliberately reuses both the numeric HWND and state
// storage through real WSI capture, rather than installing a different token.
static void fullscreen_update_same_state_recapture() {
  for (bool topmost : {false, true}) {
    for (const char *boundary : {"present_rect", "position"}) {
      fake::reset(); DXMTWindowState state; enter_into(state);
      const HANDLE old_token=GetPropW(fake::hwnd,L"DXMTFullscreenState");
      bool recaptured=false; fake::Window replacement;
      fake::events.clear();
      fake::after_call = [&](const std::string &name) {
        if (!recaptured && name == boundary) {
          recaptured=true;
          fake::replacement();
          fake::require(wsi::leaveFullscreenMode(fake::hwnd,&state,true), "old WSI state failed to retire");
          fake::require(wsi::enterFullscreenMode(fake::monitor,fake::hwnd,&state,true), "same-storage WSI recapture failed");
          // Give the new captured window distinguishable app-owned geometry.
          fake::window.rect={11,13,711,613};
          replacement=fake::window;
        }
      };
      fake::require(!wsi::updateFullscreenWindow(fake::monitor,fake::hwnd,topmost),
                    "fullscreen update accepted same-state recapture after HWND replacement");
      fake::require(recaptured && old_token!=GetPropW(fake::hwnd,L"DXMTFullscreenState"),
                    "same-storage recapture did not mint a fresh ownership token");
      fake::require(fake::eq(fake::window.rect,replacement.rect) && fake::window.props==replacement.props,
                    "stale update changed recaptured window");
      fake::after_call={};
      fake::require(wsi::leaveFullscreenMode(fake::hwnd,&state,true), "recaptured owner cleanup failed");
      pass(std::string("fullscreen same-state recapture after ") + boundary + (topmost ? " topmost" : " move"));
    }
  }
}
static void initial_fullscreen_status() {
  for (const char *failure : {"get_output", "output_desc", "display_set", "position"}) {
    fake::reset(); FixtureSwapchain chain; chain.fullscreen_desc_.Windowed = FALSE;
    fake::inject(failure, 1, std::string(failure) == "display_set" ? 2 : 1);
    chain.InitializeFromConstructorTail();
    fake::require(FAILED(chain.initialization_result_) && fake::failed,
                  std::string("initial fullscreen entry status lost: ") + failure);
    fake::require(chain.fullscreen_desc_.Windowed, "failed initial fullscreen descriptor not windowed");
    fake::failure.clear();
    fake::require(chain.CleanupFullscreenForDestruction() == S_OK, "initial entry cleanup failed");
    fake::require(!chain.pending_display_name_[0] && !chain.window_state_.captured,
                  "initial entry failure cleanup obligation lost");
    pass(std::string("constructor entry failure propagated and cleanup: ") + failure);
  }
  fake::reset(); FixtureSwapchain failed_buffers; failed_buffers.fullscreen_desc_.Windowed = FALSE;
  fake::inject("resize_buffers"); failed_buffers.InitializeFromConstructorTail();
  fake::require(FAILED(failed_buffers.initialization_result_) &&
                std::count(fake::events.begin(),fake::events.end(),"display_set") == 0,
                "constructor ignored buffer failure before fullscreen");
  pass("constructor buffer failure prevents fullscreen entry");
}
static void pacing_after_restore() {
  for (unsigned hz : {60u,75u,120u}) {
    for (int cap : {0,30,90}) {
      fake::reset(); FixtureSwapchain chain; fake::refresh_hz = hz;
      chain.preferred_max_frame_rate = cap; chain.RefreshDisplayTiming(fake::monitor);
      fake::require(chain.SetFullscreenState(TRUE,nullptr) == S_OK, "pacing fullscreen entry failed");
      fake::refresh_hz = hz;
      fake::require(chain.SetFullscreenState(FALSE,nullptr) == S_OK, "pacing restore failed");
      for (UINT sync : {0u,1u,2u}) {
        double rate = cap ? cap : hz;
        double expected = std::max(sync/rate, cap ? 1.0/rate : 0.0);
        fake::require(std::abs(chain.PresentationDuration(sync)-expected) < 1e-12,
                      "restored pacing duration incorrect");
      }
      pass("restore cadence " + std::to_string(hz) + " Hz, cap " + std::to_string(cap));
    }
  }
  fake::reset(); FixtureSwapchain chain;
  fake::refresh_hz = 75; chain.RefreshDisplayTiming(fake::monitor);
  fake::inject("current_timing"); chain.RefreshDisplayTiming(fake::monitor);
  fake::require(!chain.display_timing_valid_ && chain.PresentationDuration(1) == 1.0/75,
                "failed timing query lost finite previous cadence");
  fake::failure.clear(); fake::refresh_hz = 120;
  fake::require(chain.NeedsDisplayTimingRefresh(fake::monitor), "invalid timing is not retried");
  chain.RefreshDisplayTiming(fake::monitor);
  fake::require(chain.PresentationDuration(2) == 2.0/120, "timing recovery not applied");
  fake::require(chain.NeedsDisplayTimingRefresh(reinterpret_cast<HMONITOR>(0x789)), "topology monitor change not detected");
  fake::refresh_hz = 60;
  fake::timing_query = [&]() { fake::refresh_hz = 144; chain.RefreshDisplayTiming(fake::monitor); };
  chain.RefreshDisplayTiming(fake::monitor);
  fake::require(chain.PresentationDuration(1) == 1.0/144, "stale timing result overwrote later generation");
  pass("timing failure, topology invalidation and stale generation discard");
}
static void capture_generation_boundaries() {
#if WSI_CAPTURE_GENERATIONS
  // A refused property store must leave the caller snapshot and window intact.
  fake::reset(); DXMTWindowState state;
  const auto original=fake::window;
  fake::inject("set_prop");
  fake::require(!wsi::enterFullscreenMode(fake::monitor,fake::hwnd,&state,true) && fake::failed,
                "failed SetProp accepted capture");
  fake::require(!state.captured && !state.capture_token && !state.owner && fake::window.props.empty() &&
                fake::eq(fake::window.rect,original.rect) && fake::window.style==original.style &&
                fake::window.exstyle==original.exstyle && fake::eq(fake::window.present,original.present),
                "failed SetProp changed window or published capture");
  fake::failure.clear();
  fake::require(!wsi::updateFullscreenWindow(fake::monitor,fake::hwnd,false), "ownerless update accepted");
  pass("failed SetProp and absent token preserve capture/window");

  for (const char *boundary : {"get_style","get_exstyle","set_style","set_exstyle","present_rect","position","remove_prop"}) {
    fake::reset(); DXMTWindowState current; enter_into(current);
    const HANDLE old_token=current.capture_token;
    bool recaptured=false; fake::Window replacement; DXMTWindowState recaptured_state;
    fake::after_call = [&](const std::string &name) {
      if (!recaptured && name==boundary) {
        recaptured=true;
        fake::replacement();
        // Driver API recursion can replace the capture in the same storage.
        fake::require(wsi::leaveFullscreenMode(fake::hwnd,&current,true), "nested lost capture failed");
        fake::require(wsi::enterFullscreenMode(fake::monitor,fake::hwnd,&current,true), "nested recapture failed");
        replacement=fake::window; recaptured_state=current;
      }
    };
    fake::require(wsi::leaveFullscreenMode(fake::hwnd,&current,true), "outer old capture leave failed");
    fake::require(recaptured && current.capture_token!=old_token && current.captured &&
                  current.capture_token==recaptured_state.capture_token &&
                  current.style_pending==recaptured_state.style_pending &&
                  current.exstyle_pending==recaptured_state.exstyle_pending &&
                  current.present_rect_pending==recaptured_state.present_rect_pending &&
                  current.position_pending==recaptured_state.position_pending &&
                  fake::window.props==replacement.props && fake::eq(fake::window.rect,replacement.rect),
                  "old leave corrupted same-storage recapture obligations");
    fake::after_call={};
    fake::require(wsi::leaveFullscreenMode(fake::hwnd,&current,true), "new capture cleanup failed");
    pass(std::string("leave preserves same-storage recapture at ")+boundary);
  }
  // Simulate separately linked D3D11/D3D12 WSI capture code. Each production
  // body calls the same extracted DXGI-owned allocator, never a local counter.
  fake::reset(); DXMTWindowState first_module, second_module;
  enter_into(first_module);
  const HANDLE first_id=first_module.capture_token;
  fake::replacement();
  fake::require(dxmt::wsi_peer::enterFullscreenMode(fake::monitor,fake::hwnd,&second_module,true),
                "second WSI copy capture failed");
  fake::require(second_module.capture_token && second_module.capture_token!=first_id,
                "independent WSI copies reused token from different capture");
  const auto second_window=fake::window;
  fake::events.clear();
  fake::require(wsi::leaveFullscreenMode(fake::hwnd,&first_module,true) && fake::events.empty() &&
                fake::window.props==second_window.props && fake::eq(fake::window.rect,second_window.rect),
                "first WSI copy altered second copy capture");
  fake::require(wsi::leaveFullscreenMode(fake::hwnd,&second_module,true), "shared allocator capture cleanup failed");
  pass("two independent production WSI copies share DXGI token allocator");

  // Set allocator near exhaustion: only the last permitted unique ID can issue.
  // This is injected allocator state, not a real OS or practical exhaustion run.
  fake::reset(); DXMTWindowState last, exhausted;
  const auto saved_next=nextFullscreenToken.load();
  nextFullscreenToken.store(~ULONG_PTR(0)-1);
  enter_into(last);
  fake::require(reinterpret_cast<ULONG_PTR>(last.capture_token)==~ULONG_PTR(0)-1, "last capture token incorrect");
  fake::require(wsi::leaveFullscreenMode(fake::hwnd,&last,true), "last capture cleanup failed");
  const auto before=fake::window;
  fake::events.clear();
  for (unsigned i=0;i<3;++i)
    fake::require(!wsi::enterFullscreenMode(fake::monitor,fake::hwnd,&exhausted,true), "exhausted allocator wrapped or succeeded");
  const bool mutated=std::any_of(fake::events.begin(),fake::events.end(),[](const std::string &event) {
    return event=="set_prop" || event=="present_rect" || event=="set_style" || event=="set_exstyle" ||
           event=="position" || event=="remove_prop";
  });
  fake::require(nextFullscreenToken.load()==~ULONG_PTR(0) && !exhausted.captured &&
                !exhausted.capture_token && !exhausted.owner && !mutated &&
                fake::window.props==before.props && fake::eq(fake::window.rect,before.rect) &&
                fake::window.style==before.style && fake::window.exstyle==before.exstyle,
                "exhaustion changed owner/window or wrapped");
  nextFullscreenToken.store(saved_next); // restore this fixture's injected allocator only
  pass("capture token exhaustion refuses without wrap or window/state mutation");
#endif
}
int main() {
  try {
    capture_obligation_ack(); checked_window_updates(); fullscreen_update_identity(); fullscreen_update_same_state_recapture(); initial_fullscreen_status(); pacing_after_restore();
    entry_failures(); restore_failures(); zero_returns(); identity_lifecycle();
    display_restore(); application_style_change();
    swapchain_entry_failures(); swapchain_display_retry(); swapchain_resize_and_guard();
    destruction_cleanup(); detach_identity(); capture_generation_boundaries();
    std::cout << "RESULT " << scenarios << " scenarios passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n'; return 1;
  }
}
