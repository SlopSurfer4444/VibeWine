#!/usr/bin/env python3
"""Compile unmodified presenter declarations/bodies against the real WMT wrapper.

Only include directives are removed from the two presenter files. Platform-heavy
command-library/trace dependencies are replaced by narrow fixtures; all presenter
logic, Metal.hpp, winemetal.h, RcObject and format conversion are real.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, default=HERE.parents[2] / "source")
parser.add_argument("--sanitizer", choices=["none", "address", "thread"], default="none")
parser.add_argument("--mutation", choices=["requested-geometry", "mutable-gamma", "cpu-native-apply", "raw-snapshot-resources", "context-fence-update", "checked-ignore-status", "checked-keep-old-cache", "presenter-missing-final-release", "factory-mask-unsupported", "factory-keep-output", "factory-skip-native-view-release", "preflight-count-failed-state", "preflight-remove-null-guard", "preflight-recapture", "preflight-after-credit", "feedback-ignore-close-capability"])
args = parser.parse_args()
source = args.source.resolve()

def without_includes(text):
    return re.sub(r'^\s*#\s*(?:include\b[^\n]*|pragma\s+once[^\n]*)\n', '', text, flags=re.M)

header = source / "src/dxmt/dxmt_presenter.hpp"
implementation = source / "src/dxmt/dxmt_presenter.cpp"
context = source / "src/dxmt/dxmt_context.cpp"
fmt = (source / "src/dxmt/dxmt_format.cpp").read_text()
fmt_start = fmt.index("WMTPixelFormat\nForget_sRGB(")
fmt_end = fmt.index("\nWMTPixelFormat\nRecall_sRGB", fmt_start)
prefix = '''#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include "Metal.hpp"
#include "wave7_feedback.h"
#include "wave7_layer_apply.h"
#include "rc/util_rc.hpp"
#include "rc/util_rc_ptr.hpp"
#include "cs2_trace_protocol.hpp"
namespace dxmt {
class InternalCommandLibrary {
public:
  WMT::Library getLibrary() { return WMT::Library{3}; }
};
'''
prefix += fmt[fmt_start:fmt_end] + '''
inline bool Is_sRGBVariant(WMTPixelFormat format) { return Forget_sRGB(format) != format; }
namespace cs2trace {
struct Scope { uint64_t type; uint64_t object; ~Scope() {} };
struct ApiScope { uint32_t result = 0; ApiScope(uint32_t, uint64_t) {} };
enum class CommitReason { Present };
struct ReasonScope { ReasonScope(CommitReason, uint64_t) {} };
struct ObservedEvent {
  uint32_t generation, type;
  uint64_t start, end, object;
  uint32_t flags;
  uint64_t detail0, detail1;
};
inline std::atomic<uint32_t> active_generation{0}; // retained only for unused legacy fixture functions
inline std::mutex events_mutex;
inline std::vector<ObservedEvent> events; // observer of actual committed Event records
alignas(Header) inline std::array<uint64_t, (4096 + Capacity * sizeof(Event))/8> trace_storage{};
inline Header *header() { return reinterpret_cast<Header *>(trace_storage.data()); }
inline Event *records() { return reinterpret_cast<Event *>(reinterpret_cast<char *>(trace_storage.data()) + 4096); }
inline void reset_trace(uint32_t gen, CaptureState state=CaptureState::Open) {
  auto h=header();
  if(controlWriters(h->control)) throw std::runtime_error("fixture reset with admitted writer");
  std::memset(trace_storage.data(), 0, sizeof(trace_storage));
  h->magic=Magic;h->version=Version;h->capacity=Capacity;h->control=controlWord(gen,state);
  active_generation=state==CaptureState::Open?gen:0;
  events.clear();
}
inline void rearm_trace(uint32_t gen) {
  auto h=header();
  if(controlWriters(h->control)) throw std::runtime_error("fixture rearm with admitted writer");
  auto old=std::atomic_ref<uint64_t>(h->control).load();
  std::atomic_ref<uint64_t>(h->control).store(controlWord(controlGeneration(old),CaptureState::Closed));
  std::atomic_ref<uint64_t>(h->control).store(controlWord(gen,CaptureState::Open));
  active_generation=gen;
}
inline uint32_t generation() {
  const auto word=std::atomic_ref<uint64_t>(header()->control).load(std::memory_order_relaxed);
  return controlState(word)==CaptureState::Open?controlGeneration(word):0;
}
inline uint64_t now() { return 123456789; }
inline void record(uint32_t gen, uint32_t type, uint64_t start, uint64_t end,
                   uint64_t object=0, uint32_t flags=0, uint64_t detail0=0, uint64_t detail1=0) {
  std::lock_guard lock(events_mutex);
  auto h=header();const auto before=h->next;
  writeEvent(h,gen,type,start,end,object,flags,detail0,detail1,1);
  if(before<Capacity && h->next==before+1 && records()[before].committed==before+1) {
    const auto &e=records()[before];
    events.push_back({e.reserved,e.type,e.start,e.end,e.object,e.flags,e.padding[0],e.padding[1]});
  }
}
}
}
'''
with tempfile.TemporaryDirectory(prefix="presenter-regression-") as directory:
    build = Path(directory)
    generated = build / "production_presenter.inc"
    production = prefix + without_includes(header.read_text()) + "\n" + without_includes(implementation.read_text())
    mutations = {
        "requested-geometry": [
            ("const double width = drawable_texture.width();", "const double width = configuration.layer_props.drawable_width;"),
            ("const double height = drawable_texture.height();", "const double height = configuration.layer_props.drawable_height;")],
        "mutable-gamma": [("encoder.setFragmentTexture(configuration.gamma_lut_texture, 1);",
                           "encoder.setFragmentTexture(gamma_lut_texture_, 1);")],
        "cpu-native-apply": [("return should_invalidated;", "layer_.setProps(layer_props_); return should_invalidated;")],
        "raw-snapshot-resources": [("WMT::Reference<WMT::Texture> gamma_lut_texture;", "WMT::Texture gamma_lut_texture;")],
        "checked-ignore-status": [("if (status != W7LA_OK) {", "if (false) {")],
        "checked-keep-old-cache": [("\n    encoded_configuration_revision_ = 0;", "\n    /* deliberately kept earlier applied cache */")],
        "presenter-missing-final-release": [("  void decRef() {\n    if (RcObject::decRef() == 0)\n      delete this;\n  }\n", "")],
        "preflight-count-failed-state": [("configuration_ ? ++frame_requested_ : 0", "++frame_requested_")],
        "feedback-ignore-close-capability": [("!(feedback_capabilities_ & W7FB_CAP_CLOSE_QUEUED_DISCARD)", "false")],
    }
    if args.mutation and args.mutation != "context-fence-update" and not args.mutation.startswith("factory-") and args.mutation not in ["preflight-remove-null-guard", "preflight-recapture", "preflight-after-credit"]:
        for original, replacement in mutations[args.mutation]:
            if original not in production:
                raise RuntimeError(f"mutation seam missing: {original}")
            production = production.replace(original, replacement)
    generated.write_text(production)
    context_text = context.read_text()
    assert context_text.count("encoder_info->trace_generation = state.trace_generation;") == 1
    present_case = context_text[context_text.index("    case EncoderType::Present: {"):
                                context_text.index("    case EncoderType::Clear: {")]
    if args.mutation == "context-fence-update":
        original = "data->fence_update.forEach([&](auto id) { encoder.updateFence(fence_pool_[id]); });"
        if present_case.count(original) != 1:
            raise RuntimeError("context fence mutation seam changed")
        present_case = present_case.replace(original, "/* deliberately omitted fallback update */")
    (build / "production_present_case.inc").write_text(present_case)
    context_header = (source / "src/dxmt/dxmt_context.hpp").read_text()
    data_start = context_header.index("struct PresentData : EncoderData {")
    data_end = context_header.index("\n};", data_start) + len("\n};")
    (build / "production_present_data.inc").write_text(context_header[data_start:data_end])
    swapchain = source / "src/d3d11/d3d11_swapchain.cpp"
    swapchain_text = swapchain.read_text()
    present_start = swapchain_text.index("  Present1(UINT SyncInterval")
    admission_start = swapchain_text.index("    if (target_) {", present_start)
    admission_end = swapchain_text.index("    return hr;", admission_start) + len("    return hr;")
    admission = swapchain_text[admission_start:admission_end]
    if args.mutation == "preflight-remove-null-guard":
        original = "    if (!state.configuration)\n      return E_FAIL;"
        if admission.count(original) != 1: raise RuntimeError("preflight guard seam changed")
        admission = admission.replace(original, "/* deliberately omitted pipeline null guard */")
    if args.mutation == "preflight-recapture":
        if admission.count("state = state") != 2: raise RuntimeError("preflight capture seam changed")
        admission = admission.replace("state = state", "state = presenter->synchronizeLayerProperties()")
    if args.mutation == "preflight-after-credit":
        original = "    const auto state = presenter->synchronizeLayerProperties();\n    if (!state.configuration)\n      return E_FAIL;"
        if admission.count(original) != 1: raise RuntimeError("preflight order seam changed")
        admission = admission.replace(original, "")
        admission = admission.replace("    if constexpr (EnableMetalFX) {", original + "\n    if constexpr (EnableMetalFX) {")
    (build / "production_present_admission.inc").write_text(admission)
    ctor_start = swapchain_text.index("    native_view_ = WMT::CreateMetalViewFromHWND")
    ctor_end = swapchain_text.index("    frame_latency = kSwapchainLatency;", ctor_start)
    (build / "production_constructor_prefix.inc").write_text(swapchain_text[ctor_start:ctor_end])
    dtor_start = swapchain_text.index("  ~MTLD3D11SwapChain() {") + len("  ~MTLD3D11SwapChain() {")
    dtor_end = swapchain_text.index("\n  };", dtor_start)
    destructor = swapchain_text[dtor_start:dtor_end]
    if args.mutation == "factory-skip-native-view-release":
        if destructor.count("WMT::ReleaseMetalView(native_view_);") != 1:
            raise RuntimeError("native view release mutation seam changed")
        destructor = destructor.replace("WMT::ReleaseMetalView(native_view_);", "/* deliberately omitted view release */")
    (build / "production_destructor_body.inc").write_text(destructor)
    factory_start = swapchain_text.index("HRESULT\nCreateSwapChain(")
    guards_start = swapchain_text.index("  if (ppSwapChain == NULL)", factory_start)
    guards_end = swapchain_text.index("  DWORD window_process_id;", guards_start)
    for mode in ["true", "false"]:
        tail_start = swapchain_text.index(f"auto *swapchain = new MTLD3D11SwapChain<{mode}>(", guards_end)
        tail_end = swapchain_text.index("return S_OK;", tail_start) + len("return S_OK;")
        factory = swapchain_text[guards_start:guards_end] + swapchain_text[tail_start:tail_end]
        if args.mutation == "factory-mask-unsupported":
            original = "if (FAILED(initialized)) { swapchain->Release(); return initialized; }"
            if factory.count(original) != 1:
                raise RuntimeError("factory HRESULT mutation seam changed")
            factory = factory.replace(original, "if (FAILED(initialized)) { swapchain->Release(); return S_OK; }")
        if args.mutation == "factory-keep-output":
            if factory.count("InitReturnPtr(ppSwapChain);") != 1:
                raise RuntimeError("factory null output mutation seam changed")
            factory = factory.replace("InitReturnPtr(ppSwapChain);", "/* deliberately kept poisoned output */")
        (build / f"production_factory_{mode}.inc").write_text(factory)
    com = (source / "src/util/com/com_object.hpp").read_text()
    init_start = com.index("template <typename T> inline void InitReturnPtr")
    init_end = com.index("\n}", init_start) + 2
    (build / "production_init_return_ptr.inc").write_text(com[init_start:init_end])
    binary = build / "presenter-regression"
    saved_argument = "DXMTPresentMetadata metadata, uint32_t trace_generation," in implementation.read_text()
    command = ["clang++", "-std=c++20", "-pthread", "-DDXMT_DEBUG", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-I", str(build), "-I", str(source / "src/winemetal"),
               "-I", str(source / "src/util"), str(HERE / "presenter_regression.cpp"), "-o", str(binary)]
    command += ["-I", str(source / "src/d3d11")]
    if saved_argument: command += ["-DEVENT77_SAVED_ARGUMENT"]
    if args.sanitizer != "none":
        command += [("-fsanitize=address,undefined" if args.sanitizer == "address" else f"-fsanitize={args.sanitizer}"), "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True, timeout=120)
    if args.mutation:
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
        if result.returncode == 0:
            raise RuntimeError(f"fixture failed to detect mutation {args.mutation}")
        if not result.stderr.startswith("FAIL "):
            raise RuntimeError(f"mutation failed outside fixture oracle: {result.stderr}")
        print(f"PASS regression_sensitivity {args.mutation}: {result.stderr.strip()}")
    else:
        import os
        for case in ["zero-b", "a-b", "b-b", "current-b", "closed-b", "success-b", "rearm-during-failure"]:
            environment = dict(os.environ, EVENT77_CASE=case)
            result = subprocess.run([str(binary)], text=True, capture_output=True, env=environment, timeout=45)
            negative = not saved_argument and case in ["zero-b", "a-b", "rearm-during-failure"]
            if negative:
                if result.returncode == 0 or not result.stderr.startswith("FAIL event77 generation attribution expected 0 observed 1"):
                    raise RuntimeError(f"baseline negative control failed to isolate attribution: {case}: {result.stdout} {result.stderr}")
                print("PASS baseline_negative " + case + ": " + result.stderr.strip())
            else:
                if result.returncode: raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout.strip())
for path in [header, implementation, context, source / "src/dxmt/dxmt_context.hpp", source / "src/d3d11/d3d11_swapchain.cpp", source / "src/util/rc/util_rc.hpp",
             source / "src/util/rc/util_rc_ptr.hpp", source / "src/util/com/com_object.hpp",
             source / "src/winemetal/Metal.hpp", source / "src/winemetal/winemetal.h",
             source / "src/winemetal/wave7_feedback.h", source / "src/winemetal/wave7_layer_apply.h"]:
    print(f"SOURCE_SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()} {path.relative_to(source)}")
