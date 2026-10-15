#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "production_presenter.inc"

namespace fixture {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
struct Object {
  int references = 1;
  uint64_t width = 0, height = 0;
  obj_handle_t texture = 0;
  std::array<uint32_t, 6> constants{};
  WMTPixelFormat format = WMTPixelFormatInvalid;
  std::vector<float> pixels;
};
struct Pass {
  obj_handle_t encoder = 0, texture = 0, gamma = 0, pipeline = 0;
  WMTLayerProps props{};
  WMTColorSpace colorspace = WMTColorSpaceSRGB;
  WMTViewport viewport{};
  dxmt::DXMTPresentMetadata metadata{};
  int waits = 0, updates = 0, draws = 0, ends = 0;
};
std::mutex mutex;
std::map<obj_handle_t, Object> objects;
obj_handle_t next_handle = 10;
WMTLayerProps applied_props{};
WMTColorSpace applied_colorspace = WMTColorSpaceSRGB;
std::vector<std::string> calls;
std::vector<Pass> passes;
std::vector<uint32_t> feedback_operations;
bool feedback_supported = false, feedback_register_unsupported = false, feedback_session_open = false;
constexpr uint64_t feedback_token = 4242;
std::vector<W7FBRequest> feedback_registrations;
std::vector<W7FBRecord> feedback_poll_records;
W7FBRecord feedback_open_identity{};
// Independent routing sentinels. pending represents native handler tickets,
// not outstanding callbacks, displayed drops, or GPU utilization.
const W7FBStats feedback_stats{17, 11, 3, 5, 7, 2, 13};
obj_handle_t blit_encoder = 0;
std::vector<std::pair<uint16_t, obj_handle_t>> blit_fences;
int blit_ends = 0, native_presents = 0;
double native_present_after = 0;
std::optional<std::pair<uint64_t, uint64_t>> actual_geometry;
bool missing_drawable = false, missing_texture = false;
bool missing_encoder = false;
std::optional<WMTPixelFormat> actual_format;
bool fail_texture = false;
int pipeline_calls_until_failure = -1;
uint64_t display_version = 0;
WMTColorSpace display_colorspace = WMTColorSpaceSRGB;
WMTEDRValue edr{1, 1};

Object &live(obj_handle_t handle) {
  require(handle != 0, "invalid native call on null object");
  auto it = objects.find(handle);
  require(it != objects.end() && it->second.references > 0, "native use of released object");
  return it->second;
}
obj_handle_t allocate(Object object = {}) {
  auto handle = next_handle++;
  objects.emplace(handle, std::move(object));
  return handle;
}
WMT::Texture texture(uint64_t width, uint64_t height) {
  std::lock_guard lock(mutex);
  Object object;
  object.width = width; object.height = height;
  return WMT::Texture{allocate(object)};
}
Pass &pass(obj_handle_t encoder) {
  require(!passes.empty() && passes.back().encoder == encoder, "unexpected encoder order");
  return passes.back();
}
void clear_calls() {
  std::lock_guard lock(mutex);
  calls.clear(); passes.clear();
}
dxmt::DXMTGammaRamp gamma(uint64_t version, float red) {
  dxmt::DXMTGammaRamp ramp{};
  ramp.version = version;
  for (unsigned i = 0; i < dxmt::DXMT_GAMMA_CP_COUNT; ++i) {
    ramp.red[i] = red; ramp.green[i] = 0.5f; ramp.blue[i] = 2.0f;
  }
  return ramp;
}
// This wrapper is the sole API adaptation if the production signature changes.
WMT::MetalDrawable encode(dxmt::Presenter &presenter, const dxmt::Presenter::PresentState &state,
                         WMT::Texture backbuffer) {
  return presenter.encodeCommands(WMT::CommandBuffer{4}, backbuffer, *state.configuration,
      state.metadata,
      [](WMT::RenderCommandEncoder encoder) {
        std::lock_guard lock(mutex); ++pass(encoder.handle).waits;
      },
      [](WMT::RenderCommandEncoder encoder) {
        std::lock_guard lock(mutex); ++pass(encoder.handle).updates;
      });
}
void assert_pass(const Pass &p, double requested_width, double requested_height,
                 double actual_width, double actual_height, bool matched, bool gamma_enabled) {
  require(p.props.drawable_width == requested_width && p.props.drawable_height == requested_height,
          "frame used another revision's native layer geometry");
  require(p.viewport.width == actual_width && p.viewport.height == actual_height,
          "viewport did not use actual drawable texture geometry");
  require(p.waits == 1 && p.updates == 1 && p.draws == 1 && p.ends == 1,
          "present pass callbacks/encoding incomplete");
  const auto &pipeline = live(p.pipeline);
  require(pipeline.constants[0] == matched, "blit/scale selected from requested instead of actual geometry");
  require(pipeline.constants[5] == gamma_enabled, "frame used another revision's gamma pipeline");
  require(pipeline.format == p.props.pixel_format, "pipeline and native pixel format disagree");
}

void delayed_snapshot_and_lifetime() {
  dxmt::InternalCommandLibrary library;
  dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
  presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm_sRGB, WMTColorSpaceSRGB, 640, 360, 1);
  auto ramp1 = gamma(1, -0.25f);
  presenter.changeGammaRamp(&ramp1);
  auto old = presenter.synchronizeLayerProperties();
  static_assert(std::is_const_v<typename decltype(old.configuration)::element_type>);
  const auto old_revision = old.configuration;
  std::promise<void> begin_update;
  auto permission = begin_update.get_future();
  std::optional<dxmt::Presenter::PresentState> newer;
  std::exception_ptr error;
  std::thread updater([&] {
    try {
      permission.wait();
      presenter.changeLayerProperties(WMTPixelFormatRGBA16Float, WMTColorSpaceHDR_scRGB, 1280, 720, 4);
      auto ramp2 = gamma(2, 0.75f);
      presenter.changeGammaRamp(&ramp2);
      newer.emplace(presenter.synchronizeLayerProperties());
    } catch (...) { error = std::current_exception(); }
  });
  begin_update.set_value();
  updater.join(); // old remains alive: a CPU fence wait here would hit runner timeout.
  if (error) std::rethrow_exception(error);
  require(newer->frame_id > old.frame_id, "frame identity did not advance");
  require(newer->configuration != old_revision, "changed presentation configuration reused old snapshot");
  require(newer->configuration->revision > old.configuration->revision, "configuration revision did not advance");
  require(old.metadata.edr_scale == 1 && newer->metadata.edr_scale == 0.8f,
          "metadata did not remain bound to captured colorspace");
  require(calls.empty(), "CPU update/capture applied native layer state before encoding");

  actual_geometry = {{320, 180}};
  require(bool(encode(presenter, old, texture(640, 360))), "old drawable missing");
  assert_pass(passes[0], 640, 360, 320, 180, false, true);
  require(passes[0].colorspace == WMTColorSpaceSRGB, "old colorspace was overwritten");
  require(live(passes[0].gamma).pixels[0] == 0.0f && live(passes[0].gamma).pixels[2] == 1.0f,
          "old gamma image was overwritten or clamp changed");
  auto old_gamma = passes[0].gamma;
  auto old_pipeline = passes[0].pipeline;
  actual_geometry = {{1280, 720}};
  require(bool(encode(presenter, *newer, texture(1280, 720))), "new drawable missing");
  assert_pass(passes[1], 1280, 720, 1280, 720, true, true);
  require(passes[1].colorspace == WMTColorSpaceHDR_scRGB, "new colorspace not applied");
  require(passes[1].gamma != old_gamma && live(passes[1].gamma).pixels[0] == 0.75f,
          "gamma update did not allocate immutable texture");
  require(live(old_gamma).pixels[0] == 0.0f && live(old_pipeline).references > 0,
          "old frame resources did not survive new revision");
  require(calls == std::vector<std::string>({"props", "colorspace", "drawable", "encoder",
                                           "props", "colorspace", "drawable", "encoder"}),
          "native layer changes not ordered with frame encoding");
  presenter.changeGammaRamp(nullptr);
  auto reset = presenter.synchronizeLayerProperties();
  require(bool(encode(presenter, reset, texture(1280, 720))), "reset drawable missing");
  assert_pass(passes[2], 1280, 720, 1280, 720, true, false);
  // Re-encoding a retained revision after later native mutation still restores it.
  actual_geometry = {{640, 360}};
  encode(presenter, old, texture(640, 360));
  assert_pass(passes[3], 640, 360, 640, 360, true, true);
  require(passes[3].gamma == old_gamma, "retained gamma identity changed");
  std::cout << "PASS delayed_old_frame_resize_gamma_pipeline_native_order_actual_drawable\n";
}

void absent_drawables() {
  clear_calls();
  dxmt::InternalCommandLibrary library;
  dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
  presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
  auto state = presenter.synchronizeLayerProperties();
  missing_drawable = true;
  require(!encode(presenter, state, texture(640, 360)), "missing drawable returned non-null");
  require(passes.empty(), "missing drawable started encoding");
  missing_drawable = false;
  missing_texture = true;
  require(!encode(presenter, state, texture(640, 360)), "drawable with missing texture accepted");
  require(passes.empty(), "missing drawable texture started encoding");
  missing_texture = false;
  actual_geometry = {{0, 360}};
  require(!encode(presenter, state, texture(640, 360)), "zero-width drawable accepted");
  require(passes.empty(), "zero-size drawable started encoding");
  actual_geometry = {{640, 360}};
  actual_format = WMTPixelFormatRGBA16Float;
  require(!encode(presenter, state, texture(640, 360)), "wrong-format drawable accepted");
  require(passes.empty(), "wrong-format drawable started encoding");
  actual_format.reset();
  missing_encoder = true;
  require(!encode(presenter, state, texture(640, 360)), "null render encoder accepted");
  require(passes.empty(), "null render encoder called fences or commands");
  missing_encoder = false;
  std::cout << "PASS missing_drawable_missing_texture_zero_size_wrong_format_null_encoder_no_invalid_native_calls\n";
}

void preparation_failures_and_retry() {
  clear_calls(); actual_geometry.reset();
  dxmt::InternalCommandLibrary library;
  dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
  presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
  auto ramp1 = gamma(1, 0.2f);
  presenter.changeGammaRamp(&ramp1);
  auto initial = presenter.synchronizeLayerProperties();
  require(bool(initial.configuration), "initial preparation failed");
  const auto initial_gamma = initial.configuration->gamma_lut_texture.handle;
  const auto initial_pipeline = initial.configuration->present_blit.handle;
  auto unchanged = presenter.synchronizeLayerProperties();
  require(unchanged.configuration == initial.configuration, "unchanged capture needlessly replaced revision");

  auto ramp2 = gamma(2, 0.8f);
  fail_texture = true;
  presenter.changeGammaRamp(&ramp2);
  auto rejected_gamma = presenter.synchronizeLayerProperties();
  require(rejected_gamma.configuration == initial.configuration,
          "failed gamma allocation published partial revision");
  require(live(initial_gamma).pixels[0] == 0.2f, "failed gamma allocation damaged old texture");
  fail_texture = false;
  presenter.changeGammaRamp(&ramp2);
  auto recovered_gamma = presenter.synchronizeLayerProperties();
  require(recovered_gamma.configuration->gamma_lut_texture.handle != initial_gamma,
          "failed gamma version prevented retry");
  require(live(recovered_gamma.configuration->gamma_lut_texture.handle).pixels[0] == 0.8f,
          "gamma retry content incorrect");

  for (int failure_call : {1, 2}) {
    presenter.changeLayerProperties(failure_call == 1 ? WMTPixelFormatRGBA16Float : WMTPixelFormatBGRA8Unorm,
                                    WMTColorSpaceSRGB, 800, 400, 1);
    pipeline_calls_until_failure = failure_call;
    auto failed = presenter.synchronizeLayerProperties();
    require(!failed.configuration, "pipeline failure published new geometry with stale pipeline");
    require(calls.empty(), "preparation failure applied native layer state");
    pipeline_calls_until_failure = -1;
    auto recovered = presenter.synchronizeLayerProperties();
    require(bool(recovered.configuration), "pipeline build did not retry after failure");
    require(recovered.configuration->layer_props.drawable_width == 800, "pipeline retry lost requested geometry");
  }
  require(live(initial_gamma).pixels[0] == 0.2f && live(initial_pipeline).references > 0,
          "preparation failure/retry released retained old frame resources");
  std::cout << "PASS gamma_allocation_failure_pipeline_blit_scale_failure_retry\n";
}

void snapshot_outlives_presenter() {
  clear_calls();
  dxmt::InternalCommandLibrary library;
  std::shared_ptr<const dxmt::Presenter::PresentationConfiguration> retained;
  obj_handle_t gamma_handle = 0, blit_handle = 0, scale_handle = 0;
  const int layer_references = live(2).references;
  {
    dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
    require(live(2).references > layer_references, "presenter does not retain native layer");
    presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
    auto ramp = gamma(1, 0.4f); presenter.changeGammaRamp(&ramp);
    retained = presenter.synchronizeLayerProperties().configuration;
    gamma_handle = retained->gamma_lut_texture.handle;
    blit_handle = retained->present_blit.handle;
    scale_handle = retained->present_scale.handle;
  }
  require(live(2).references == layer_references, "presenter failed to release retained layer");
  require(live(gamma_handle).pixels[0] == 0.4f && live(blit_handle).references > 0 &&
          live(scale_handle).references > 0, "snapshot lost native resources with presenter destruction");
  retained.reset();
  require(objects.at(gamma_handle).references == 0 && objects.at(blit_handle).references == 0 &&
          objects.at(scale_handle).references == 0, "snapshot leaked its retained native resources");
  std::cout << "PASS snapshot_resource_lifetime_and_layer_ownership\n";
}

void concurrent_capture() {
  clear_calls(); actual_geometry.reset();
  dxmt::InternalCommandLibrary library;
  dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
  presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 320, 1);
  std::promise<void> start;
  auto ready = start.get_future().share();
  std::vector<dxmt::Presenter::PresentState> states;
  states.reserve(600);
  std::exception_ptr update_error, capture_error;
  std::thread updater([&] {
    try {
      ready.wait();
      for (unsigned i = 1; i <= 600; ++i) {
        auto h = 320 + (i % 7);
        presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, h * 2, h, 1);
        auto ramp = gamma(i, float(i % 10) / 10);
        presenter.changeGammaRamp(&ramp);
        WMTHDRMetadata hdr{}; hdr.max_content_light_level = i;
        presenter.changeHDRMetadata(i % 2 ? &hdr : nullptr);
        presenter.changeLayerColorSpace(i % 2 ? WMTColorSpaceSRGB : WMTColorSpaceSRGBLinear);
      }
    } catch (...) { update_error = std::current_exception(); }
  });
  std::thread capture([&] {
    try {
      ready.wait();
      for (unsigned i = 0; i < 600; ++i) states.push_back(presenter.synchronizeLayerProperties());
    } catch (...) { capture_error = std::current_exception(); }
  });
  start.set_value(); updater.join(); capture.join();
  if (update_error) std::rethrow_exception(update_error);
  if (capture_error) std::rethrow_exception(capture_error);
  require(calls.empty(), "concurrent CPU operations changed native layer");
  uint64_t previous_id = 0;
  for (auto &state : states) {
    require(state.frame_id > previous_id, "capture frame IDs not ordered");
    previous_id = state.frame_id;
    encode(presenter, state, texture(640, 320));
    const auto &p = passes.back();
    require(p.props.drawable_width == p.props.drawable_height * 2, "torn width/height snapshot");
    require(p.viewport.width == p.props.drawable_width && p.viewport.height == p.props.drawable_height,
            "captured frame and actual drawable disagree");
    require(live(p.pipeline).format == p.props.pixel_format, "pipeline geometry revision mismatch");
  }
  std::cout << "PASS concurrent_update_capture_600_retained_frames\n";
}

void unsupported_feedback_transport() {
  clear_calls();
  dxmt::cs2trace::events.clear(); feedback_operations.clear();
  dxmt::InternalCommandLibrary library;
  dxmt::cs2trace::active_generation = 1;
  {
    dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1, 99);
    presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
    auto state = presenter.synchronizeLayerProperties();
    require(state.trace_generation == 1, "frame failed to capture trace generation");
    auto drawable = encode(presenter, state, texture(640, 360));
    presenter.observePresentation(drawable, state.frame_id, state.configuration->revision, state.trace_generation);
    presenter.observePresentation(drawable, state.frame_id + 1, state.configuration->revision, 1);
    require(feedback_operations == std::vector<uint32_t>{W7FB_QUERY},
            "old transport should query once and never open/register");
    require(dxmt::cs2trace::events.size() == 1, "unsupported status should emit once per generation");
    const auto &initial = dxmt::cs2trace::events.front();
    require(initial.generation == 1 && initial.type == 70 && initial.object == 99 &&
            initial.flags == W7FB_UNSUPPORTED && initial.detail0 == W7FB_VERSION && initial.detail1 == 0,
            "unsupported status identity/capabilities incorrect");
    dxmt::cs2trace::active_generation = 2;
    presenter.observePresentation(drawable, state.frame_id, state.configuration->revision, 1);
    require(dxmt::cs2trace::events.size() == 1, "stale frame emitted status into a new capture");
    presenter.observePresentation(drawable, state.frame_id + 2, state.configuration->revision, 2);
    require(feedback_operations == std::vector<uint32_t>{W7FB_QUERY},
            "generation transition repeated unsupported capability query");
    require(dxmt::cs2trace::events.size() == 2 && dxmt::cs2trace::events.back().generation == 2 &&
            dxmt::cs2trace::events.back().type == 70 && dxmt::cs2trace::events.back().flags == W7FB_UNSUPPORTED,
            "new capture did not receive unsupported status");
    dxmt::cs2trace::active_generation = 0;
    presenter.observePresentation(drawable, state.frame_id + 3, state.configuration->revision, 0);
  }
  require(feedback_operations == std::vector<uint32_t>{W7FB_QUERY},
          "unsupported transport attempted native session cleanup");
  require(dxmt::cs2trace::events.size() == 2, "inactive capture emitted feedback records");
  dxmt::cs2trace::events.clear();
  std::cout << "PASS unsupported_feedback_transport_cached_query_generation_status\n";
}

// The production context case is compiled verbatim into this bounded wrapper.
// Only unrelated context state/queue infrastructure is replaced.
struct FenceSet {
  std::vector<unsigned> ids;
  template <typename Fn> void forEach(Fn &&fn) { for (auto id : ids) fn(id); }
};
struct PresentData {
  dxmt::Presenter *presenter;
  WMT::Texture backbuffer;
  std::shared_ptr<const dxmt::Presenter::PresentationConfiguration> configuration;
  dxmt::DXMTPresentMetadata metadata;
  uint64_t frame_id;
  uint32_t trace_generation;
  double after;
  FenceSet fence_wait, fence_update;
  bool *destroyed;
  ~PresentData() { *destroyed = true; }
};
enum class EncoderType { Present };
void execute_present_case(PresentData *current, WMT::CommandBuffer cmdbuf) {
  using clock = std::chrono::steady_clock;
  std::array<WMT::Fence, 3> fence_pool_{WMT::Fence{5}, WMT::Fence{6}, WMT::Fence{7}};
  struct Statistics { clock::duration present_encode_interval{}; } statistics;
  [[maybe_unused]] auto updateFrameStatistics = [&](auto &&update) { update(statistics); };
  switch (EncoderType::Present) {
#include "production_present_case.inc"
  }
}
void context_fence_fallback() {
  dxmt::cs2trace::active_generation = 0;
  actual_geometry.reset(); actual_format.reset();
  dxmt::InternalCommandLibrary library;
  dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1);
  presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
  auto state = presenter.synchronizeLayerProperties();
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    clear_calls(); blit_fences.clear(); blit_encoder = 0; blit_ends = 0; native_presents = 0;
    missing_drawable = scenario == 1;
    missing_encoder = scenario == 2;
    actual_format = scenario == 3 ? std::optional<WMTPixelFormat>{WMTPixelFormatRGBA16Float} : std::nullopt;
    bool destroyed = false;
    alignas(PresentData) std::byte storage[sizeof(PresentData)];
    const double after = scenario == 5 ? 0.016 : 0;
    auto data = new (storage) PresentData{&presenter, texture(640, 360),
        scenario == 0 ? nullptr : state.configuration, state.metadata, state.frame_id, 0,
        after, {{0, 1}}, {{2}}, &destroyed};
    execute_present_case(data, WMT::CommandBuffer{4});
    require(destroyed, "context failed to destroy PresentData");
    if (scenario < 4) {
      require(native_presents == 0, "context presented failed/null drawable");
      require(blit_ends == 1, "context fallback did not end blit encoder");
      require(blit_fences == std::vector<std::pair<uint16_t, obj_handle_t>>{
          {WMTBlitCommandWaitForFence, 5}, {WMTBlitCommandWaitForFence, 6}, {WMTBlitCommandUpdateFence, 7}},
          "context fallback lost or reordered dependency fences");
    } else {
      require(native_presents == 1 && native_present_after == after,
              "context did not forward native presentation timing");
      require(blit_ends == 0 && blit_fences.empty(), "successful context present used fallback encoder");
      require(passes.size() == 1 && passes.back().waits == 2 && passes.back().updates == 1 &&
              passes.back().ends == 1, "successful context present lost render fence edges");
    }
  }
  missing_drawable = false; missing_encoder = false; actual_format.reset();
  std::cout << "PASS production_context_present_fallback_fences_and_native_present\n";
}

void supported_feedback_transport() {
  auto reset_transport = [] {
    feedback_operations.clear(); feedback_registrations.clear(); feedback_poll_records.clear();
    dxmt::cs2trace::events.clear(); feedback_session_open = false;
  };
  auto count_operation = [](uint32_t op) {
    return std::count(feedback_operations.begin(), feedback_operations.end(), op);
  };
  auto events_of_type = [](uint32_t type) {
    std::vector<dxmt::cs2trace::Event> events;
    for (const auto &event : dxmt::cs2trace::events) if (event.type == type) events.push_back(event);
    return events;
  };
  reset_transport(); clear_calls();
  feedback_supported = true;
  dxmt::cs2trace::active_generation = 3;
  actual_geometry = {{320, 180}};
  dxmt::InternalCommandLibrary library;
  {
    dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1, 99);
    presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
    auto state = presenter.synchronizeLayerProperties();
    auto drawable = encode(presenter, state, texture(640, 360));
    presenter.observePresentation(drawable, state.frame_id, state.configuration->revision, state.trace_generation);
    require(count_operation(W7FB_QUERY) == 1 && count_operation(W7FB_OPEN) == 1 &&
            feedback_session_open, "supported feedback failed to open a session");
    require(feedback_open_identity.generation == 3 && feedback_open_identity.swapchain_serial == 99,
            "OPEN identity was not capture/swapchain bound");
    require(feedback_registrations.size() == 1, "supported feedback did not register drawable");
    const auto &registration = feedback_registrations.front();
    require(registration.session == feedback_token && registration.drawable == drawable.handle &&
            registration.record.generation == 3 && registration.record.swapchain_serial == 99 &&
            registration.record.frame_id == state.frame_id &&
            registration.record.config_revision == state.configuration->revision &&
            registration.record.drawable_width == 320 && registration.record.drawable_height == 180,
            "REGISTER lost session/frame/revision/actual drawable identity");
    const auto capability = events_of_type(70);
    require(capability.size() == 1 && capability[0].flags == W7FB_OK &&
            capability[0].detail1 == W7FB_CAP_PRESENTED_HANDLER, "supported capability trace incorrect");

    W7FBRecord record{};
    record.generation = 3; record.swapchain_serial = 99; record.frame_id = 77;
    record.config_revision = 123; record.drawable_width = 800; record.drawable_height = 600;
    record.clock_domain = W7FB_CLOCK_CA_HOST_SECONDS; record.presented_seconds = 1234.5;
    auto stale = record; stale.generation = 2;
    auto wrong_swapchain = record; wrong_swapchain.swapchain_serial = 100;
    auto zero = record; zero.frame_id = 78; zero.presented_seconds = 0;
    auto invalid = record; invalid.frame_id = 79; invalid.presented_seconds = std::numeric_limits<double>::quiet_NaN();
    feedback_poll_records = {stale, wrong_swapchain, record, zero, invalid};
    presenter.observePresentation(drawable, state.frame_id + 1, state.configuration->revision, 3);
    const auto times = events_of_type(71), revisions = events_of_type(72), geometries = events_of_type(73);
    require(times.size() == 3 && revisions.size() == 3 && geometries.size() == 3,
            "POLL failed to route valid records or reject wrong capture/swapchain");
    const std::array<W7FBRecord, 3> expected{record, zero, invalid};
    for (unsigned i = 0; i < expected.size(); ++i) {
      uint64_t bits = 0;
      std::memcpy(&bits, &expected[i].presented_seconds, sizeof(bits));
      const auto expected_flags = W7FB_CLOCK_CA_HOST_SECONDS | (i == 0 ? 0x100u : 0u);
      require(times[i].generation == 3 && times[i].object == 99 && times[i].flags == expected_flags &&
              times[i].detail0 == expected[i].frame_id && times[i].detail1 == bits,
              "presented timestamp bits/domain/validity/frame were not preserved");
      require(revisions[i].detail0 == expected[i].frame_id && revisions[i].detail1 == 123 &&
              geometries[i].detail0 == expected[i].frame_id &&
              geometries[i].detail1 == ((uint64_t(800) << 32) | 600),
              "feedback revision/geometry trace payload incorrect");
      for (const auto &event : {times[i], revisions[i], geometries[i]}) {
        require(event.start == 123456789 && event.end == 123456789,
                "native host timestamp was mixed with PE receipt clock");
      }
    }
    const auto stats = events_of_type(74);
    const std::array<uint64_t, 7> expected_stats{17, 11, 3, 5, 7, 2, 13};
    require(stats.size() >= 7, "native feedback counters were not routed");
    for (unsigned i = 0; i < expected_stats.size(); ++i) {
      const auto &event = stats[stats.size() - 7 + i];
      require(event.flags == i && event.detail0 == feedback_token && event.detail1 == expected_stats[i],
              "native feedback counters mixed pending/overflow/rejection/abandonment");
    }
    dxmt::cs2trace::active_generation = 0;
    presenter.observePresentation({}, state.frame_id + 2, state.configuration->revision, 3);
    require(count_operation(W7FB_CLOSE) == 1 && !feedback_session_open,
            "capture stop did not close supported native session");
    require(count_operation(W7FB_OPEN) == 1 && feedback_registrations.size() == 2,
            "capture stop reopened/registered a stale frame");
    const auto closed = events_of_type(75);
    require(closed.size() == 1 && closed[0].flags == W7FB_OK && closed[0].detail0 == 2 &&
            closed[0].detail1 == 3, "CLOSE pending-ticket/abandonment payload incorrect");
    dxmt::cs2trace::active_generation = 4;
    const auto operations_after_close = feedback_operations.size();
    presenter.observePresentation(drawable, state.frame_id + 3, state.configuration->revision, 3);
    require(feedback_operations.size() == operations_after_close,
            "late old-generation frame reopened native session");
  }
  require(count_operation(W7FB_CLOSE) == 1, "destruction repeated already closed session");

  reset_transport(); feedback_register_unsupported = true;
  dxmt::cs2trace::active_generation = 5;
  {
    dxmt::Presenter presenter(WMT::Device{1}, WMT::MetalLayer{2}, library, 1, 1, 99);
    presenter.changeLayerProperties(WMTPixelFormatBGRA8Unorm, WMTColorSpaceSRGB, 640, 360, 1);
    auto state = presenter.synchronizeLayerProperties();
    auto drawable = encode(presenter, state, texture(640, 360));
    presenter.observePresentation(drawable, state.frame_id, state.configuration->revision, 5);
    require(count_operation(W7FB_REGISTER) == 1 && count_operation(W7FB_CLOSE) == 1 && !feedback_session_open,
            "unsupported REGISTER did not close existing session");
    const auto failures = events_of_type(76), capability = events_of_type(70);
    require(failures.size() == 1 && failures[0].flags == W7FB_UNSUPPORTED &&
            failures[0].detail0 == state.frame_id && failures[0].detail1 == state.configuration->revision,
            "unsupported REGISTER did not trace exact frame/revision");
    require(capability.size() == 2 && capability.back().flags == W7FB_UNSUPPORTED &&
            capability.back().detail1 == 0, "unsupported REGISTER did not disable capability");
    const auto operations_after_disable = feedback_operations.size();
    presenter.observePresentation(drawable, state.frame_id + 1, state.configuration->revision, 5);
    dxmt::cs2trace::active_generation = 6;
    presenter.observePresentation(drawable, state.frame_id + 2, state.configuration->revision, 6);
    require(feedback_operations.size() == operations_after_disable,
            "unsupported REGISTER capability retried/reopened after disable");
    require(events_of_type(70).back().generation == 6 && events_of_type(70).back().flags == W7FB_UNSUPPORTED,
            "disabled transport omitted new generation status");
  }
  require(count_operation(W7FB_CLOSE) == 1, "disabled feedback session closed twice");
  feedback_supported = false; feedback_register_unsupported = false;
  dxmt::cs2trace::active_generation = 0; actual_geometry.reset();
  std::cout << "PASS supported_feedback_registration_poll_payload_clocks_stats_generation_close_disable\n";
}
} // namespace fixture

extern "C" {
uint64_t MTLCommandBuffer_property(obj_handle_t address, WMTCommandBufferProperty property) {
  std::lock_guard lock(fixture::mutex);
  fixture::require(uint32_t(property) == W7FB_PROPERTY, "unexpected command buffer property");
  auto *request = reinterpret_cast<W7FBRequest *>(uintptr_t(address));
  fixture::require(request && request->size == sizeof(W7FBRequest) && request->version == W7FB_VERSION,
                   "feedback request ABI header incorrect");
  fixture::feedback_operations.push_back(request->op);
  if (!fixture::feedback_supported)
    return 0; // Existing donor bridge behavior for an unknown property.
  request->status = W7FB_OK;
  switch (request->op) {
  case W7FB_QUERY:
    request->capabilities = W7FB_CAP_PRESENTED_HANDLER;
    break;
  case W7FB_OPEN:
    fixture::require(!fixture::feedback_session_open, "PE opened duplicate native session");
    fixture::feedback_session_open = true;
    fixture::feedback_open_identity = request->record;
    request->session = fixture::feedback_token;
    break;
  case W7FB_REGISTER:
  case W7FB_POLL:
  case W7FB_STATUS:
  case W7FB_CLOSE:
    fixture::require(fixture::feedback_session_open && request->session == fixture::feedback_token,
                     "PE used closed/wrong native session token");
    if (request->op == W7FB_REGISTER) {
      fixture::live(request->drawable);
      fixture::feedback_registrations.push_back(*request);
      if (fixture::feedback_register_unsupported) request->status = W7FB_UNSUPPORTED;
    } else if (request->op == W7FB_POLL) {
      if (fixture::feedback_poll_records.empty()) request->status = W7FB_EMPTY;
      else {
        request->record = fixture::feedback_poll_records.front();
        fixture::feedback_poll_records.erase(fixture::feedback_poll_records.begin());
      }
    } else {
      request->stats = fixture::feedback_stats;
      if (request->op == W7FB_CLOSE) fixture::feedback_session_open = false;
    }
    break;
  default: throw std::runtime_error("unexpected PE feedback operation");
  }
  return W7FB_REPLY;
}
void NSObject_retain(obj_handle_t object) {
  if (!object) return;
  std::lock_guard lock(fixture::mutex); ++fixture::live(object).references;
}
void NSObject_release(obj_handle_t object) {
  if (!object) return;
  std::lock_guard lock(fixture::mutex); --fixture::live(object).references;
}
obj_handle_t NSAutoreleasePool_alloc_init() {
  std::lock_guard lock(fixture::mutex); return fixture::allocate();
}
obj_handle_t MTLDevice_newTexture(obj_handle_t device, WMTTextureInfo *info) {
  std::lock_guard lock(fixture::mutex); fixture::live(device);
  if (fixture::fail_texture) return 0;
  fixture::Object object; object.width = info->width; object.height = info->height; object.format = info->pixel_format;
  return fixture::allocate(object);
}
void MTLTexture_replaceRegion(obj_handle_t texture, WMTOrigin, WMTSize size, uint64_t, uint64_t,
                              WMTMemoryPointer data, uint64_t, uint64_t) {
  std::lock_guard lock(fixture::mutex);
  auto &object = fixture::live(texture);
  const auto *pixels = static_cast<const float *>(data.ptr);
  object.pixels.assign(pixels, pixels + size.width * size.height * 4);
}
obj_handle_t MTLLibrary_newFunction(obj_handle_t library, const char *) {
  std::lock_guard lock(fixture::mutex); fixture::live(library); return fixture::allocate();
}
obj_handle_t MTLLibrary_newFunctionWithConstants(obj_handle_t library, const char *,
    const WMTFunctionConstant *constants, uint32_t count, obj_handle_t *) {
  std::lock_guard lock(fixture::mutex); fixture::live(library);
  fixture::Object object;
  for (unsigned i = 0; i < count; ++i) {
    fixture::require(constants[i].index >= 0x100 && constants[i].index < 0x106, "unknown function constant");
    object.constants[constants[i].index - 0x100] = *static_cast<const uint32_t *>(constants[i].data.ptr);
  }
  return fixture::allocate(object);
}
obj_handle_t MTLDevice_newRenderPipelineState(obj_handle_t device, const WMTRenderPipelineInfo *info, obj_handle_t *) {
  std::lock_guard lock(fixture::mutex); fixture::live(device);
  if (fixture::pipeline_calls_until_failure > 0 && --fixture::pipeline_calls_until_failure == 0) return 0;
  fixture::Object object = fixture::live(info->fragment_function);
  object.references = 1; object.format = info->colors[0].pixel_format;
  return fixture::allocate(object);
}
void MetalLayer_getProps(obj_handle_t layer, WMTLayerProps *props) {
  std::lock_guard lock(fixture::mutex); fixture::live(layer);
  *props = {}; props->contents_scale = 1;
}
void MetalLayer_setProps(obj_handle_t layer, const WMTLayerProps *props) {
  std::lock_guard lock(fixture::mutex); fixture::live(layer);
  fixture::applied_props = *props; fixture::calls.push_back("props");
}
bool MetalLayer_setColorSpace(obj_handle_t layer, WMTColorSpace colorspace) {
  std::lock_guard lock(fixture::mutex); fixture::live(layer);
  fixture::applied_colorspace = colorspace; fixture::calls.push_back("colorspace"); return true;
}
void WMTQueryDisplaySettingForLayer(obj_handle_t layer, uint64_t *version, WMTColorSpace *colorspace,
                                  WMTHDRMetadata *metadata, WMTEDRValue *edr) {
  std::lock_guard lock(fixture::mutex); fixture::live(layer);
  *version = fixture::display_version; *colorspace = fixture::display_colorspace;
  *metadata = {}; *edr = fixture::edr;
}
obj_handle_t MetalLayer_nextDrawable(obj_handle_t layer) {
  std::lock_guard lock(fixture::mutex); fixture::live(layer);
  fixture::calls.push_back("drawable");
  if (fixture::missing_drawable) return 0;
  fixture::Object drawable;
  if (!fixture::missing_texture) {
    fixture::Object texture;
    auto geometry = fixture::actual_geometry.value_or(std::pair<uint64_t, uint64_t>{
        fixture::applied_props.drawable_width, fixture::applied_props.drawable_height});
    texture.width = geometry.first; texture.height = geometry.second;
    texture.format = fixture::actual_format.value_or(fixture::applied_props.pixel_format);
    drawable.texture = fixture::allocate(texture);
  }
  return fixture::allocate(drawable);
}
obj_handle_t MetalDrawable_texture(obj_handle_t drawable) {
  std::lock_guard lock(fixture::mutex); return fixture::live(drawable).texture;
}
uint64_t MTLTexture_width(obj_handle_t texture) {
  std::lock_guard lock(fixture::mutex); return fixture::live(texture).width;
}
uint64_t MTLTexture_height(obj_handle_t texture) {
  std::lock_guard lock(fixture::mutex); return fixture::live(texture).height;
}
WMTPixelFormat MTLTexture_pixelFormat(obj_handle_t texture) {
  std::lock_guard lock(fixture::mutex); return fixture::live(texture).format;
}
obj_handle_t MTLCommandBuffer_renderCommandEncoder(obj_handle_t cmdbuf, WMTRenderPassInfo *info) {
  std::lock_guard lock(fixture::mutex); fixture::live(cmdbuf); fixture::live(info->colors[0].texture);
  if (fixture::missing_encoder) return 0;
  fixture::Pass pass;
  pass.encoder = fixture::allocate(); pass.texture = info->colors[0].texture;
  pass.props = fixture::applied_props; pass.colorspace = fixture::applied_colorspace;
  fixture::passes.push_back(pass); fixture::calls.push_back("encoder"); return pass.encoder;
}
void MTLRenderCommandEncoder_encodeCommands(obj_handle_t encoder, const wmtcmd_base *cmd) {
  std::lock_guard lock(fixture::mutex); fixture::live(encoder);
  auto &pass = fixture::pass(encoder);
  switch (cmd->type) {
  case WMTRenderCommandSetFragmentTexture: {
    const auto *texture = reinterpret_cast<const wmtcmd_render_settexture *>(cmd);
    // A disabled gamma lookup may legally bind nil.
    if (texture->texture) fixture::live(texture->texture);
    if (texture->index == 1) pass.gamma = texture->texture;
    break;
  }
  case WMTRenderCommandSetFragmentBytes: {
    const auto *bytes = reinterpret_cast<const wmtcmd_render_setbytes *>(cmd);
    fixture::require(bytes->length == sizeof(pass.metadata), "metadata byte size changed");
    std::memcpy(&pass.metadata, bytes->bytes.ptr, bytes->length); break;
  }
  case WMTRenderCommandSetPSO:
    pass.pipeline = reinterpret_cast<const wmtcmd_render_setpso *>(cmd)->pso;
    fixture::live(pass.pipeline); break;
  case WMTRenderCommandSetViewports: {
    const auto *viewports = reinterpret_cast<const wmtcmd_render_setviewports *>(cmd);
    fixture::require(viewports->viewport_count == 1, "expected one viewport");
    pass.viewport = *static_cast<const WMTViewport *>(viewports->viewports.ptr); break;
  }
  case WMTRenderCommandDraw: ++pass.draws; break;
  case WMTRenderCommandWaitForFence:
  case WMTRenderCommandUpdateFence: {
    const auto *fence = reinterpret_cast<const wmtcmd_render_fence_op *>(cmd);
    fixture::live(fence->fence);
    fixture::require(fence->stages == WMTRenderStageFragment, "wrong presentation fence stage");
    if (cmd->type == WMTRenderCommandWaitForFence) ++pass.waits;
    else ++pass.updates;
    break;
  }
  default: throw std::runtime_error("unexpected production render command");
  }
}
void MTLCommandEncoder_endEncoding(obj_handle_t encoder) {
  std::lock_guard lock(fixture::mutex);
  if (encoder == fixture::blit_encoder) ++fixture::blit_ends;
  else ++fixture::pass(encoder).ends;
}
obj_handle_t MTLCommandBuffer_blitCommandEncoder(obj_handle_t buffer) {
  std::lock_guard lock(fixture::mutex); fixture::live(buffer);
  fixture::blit_encoder = fixture::allocate(); return fixture::blit_encoder;
}
void MTLBlitCommandEncoder_encodeCommands(obj_handle_t encoder, const wmtcmd_base *cmd) {
  std::lock_guard lock(fixture::mutex); fixture::live(encoder);
  fixture::require(encoder == fixture::blit_encoder, "unknown fallback encoder");
  fixture::require(cmd->type == WMTBlitCommandWaitForFence || cmd->type == WMTBlitCommandUpdateFence,
                   "unexpected fallback command");
  auto fence = reinterpret_cast<const wmtcmd_blit_fence_op *>(cmd)->fence;
  fixture::live(fence); fixture::blit_fences.emplace_back(cmd->type, fence);
}
void MTLCommandBuffer_presentDrawable(obj_handle_t buffer, obj_handle_t drawable) {
  std::lock_guard lock(fixture::mutex); fixture::live(buffer); fixture::live(drawable);
  ++fixture::native_presents; fixture::native_present_after = 0;
}
void MTLCommandBuffer_presentDrawableAfterMinimumDuration(obj_handle_t buffer, obj_handle_t drawable, double after) {
  std::lock_guard lock(fixture::mutex); fixture::live(buffer); fixture::live(drawable);
  ++fixture::native_presents; fixture::native_present_after = after;
}
} // extern C

int main() {
  try {
    for (obj_handle_t i = 1; i <= 7; ++i) fixture::objects.emplace(i, fixture::Object{});
    fixture::delayed_snapshot_and_lifetime();
    fixture::absent_drawables();
    fixture::preparation_failures_and_retry();
    fixture::snapshot_outlives_presenter();
    fixture::concurrent_capture();
    fixture::unsupported_feedback_transport();
    fixture::context_fence_fallback();
    fixture::supported_feedback_transport();
    std::cout << "PASS production_presenter_regression_all\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
