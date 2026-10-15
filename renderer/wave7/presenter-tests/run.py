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
parser.add_argument("--source", type=Path, default=HERE.parent / "source")
parser.add_argument("--debug", action="store_true")
parser.add_argument("--sanitizer", choices=["none", "address", "thread"], default="none")
parser.add_argument("--mutation", choices=["requested-geometry", "mutable-gamma", "cpu-native-apply", "raw-snapshot-resources", "context-fence-update"])
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
#include "rc/util_rc.hpp"
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
struct Event {
  uint32_t generation, type;
  uint64_t start, end, object;
  uint32_t flags;
  uint64_t detail0, detail1;
};
inline std::atomic<uint32_t> active_generation{0};
inline std::mutex events_mutex;
inline std::vector<Event> events;
inline uint32_t generation() { return active_generation.load(); }
inline uint64_t now() { return 123456789; }
inline void record(uint32_t generation, uint32_t type, uint64_t start, uint64_t end,
                   uint64_t object=0, uint32_t flags=0, uint64_t detail0=0, uint64_t detail1=0) {
  std::lock_guard lock(events_mutex);
  events.push_back({generation, type, start, end, object, flags, detail0, detail1});
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
    }
    if args.mutation and args.mutation != "context-fence-update":
        for original, replacement in mutations[args.mutation]:
            if original not in production:
                raise RuntimeError(f"mutation seam missing: {original}")
            production = production.replace(original, replacement)
    generated.write_text(production)
    context_text = context.read_text()
    present_case = context_text[context_text.index("    case EncoderType::Present: {"):
                                context_text.index("    case EncoderType::Clear: {")]
    if args.mutation == "context-fence-update":
        original = "data->fence_update.forEach([&](auto id) { encoder.updateFence(fence_pool_[id]); });"
        if present_case.count(original) != 1:
            raise RuntimeError("context fence mutation seam changed")
        present_case = present_case.replace(original, "/* deliberately omitted fallback update */")
    (build / "production_present_case.inc").write_text(present_case)
    binary = build / "presenter-regression"
    command = ["clang++", "-std=c++20", "-pthread", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-I", str(build), "-I", str(source / "src/winemetal"),
               "-I", str(source / "src/util"), str(HERE / "presenter_regression.cpp"), "-o", str(binary)]
    if args.debug:
        command += ["-DDXMT_DEBUG"]
    if args.sanitizer != "none":
        command += [f"-fsanitize={args.sanitizer}", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True, timeout=120)
    if args.mutation:
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
        if result.returncode == 0:
            raise RuntimeError(f"fixture failed to detect mutation {args.mutation}")
        if not result.stderr.startswith("FAIL "):
            raise RuntimeError(f"mutation failed outside fixture oracle: {result.stderr}")
        print(f"PASS regression_sensitivity {args.mutation}: {result.stderr.strip()}")
    else:
        subprocess.run([str(binary)], check=True, timeout=45)
for path in [header, implementation, context, source / "src/winemetal/Metal.hpp", source / "src/winemetal/winemetal.h",
             source / "src/winemetal/wave7_feedback.h"]:
    print(f"SOURCE_SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()} {path.relative_to(source)}")
