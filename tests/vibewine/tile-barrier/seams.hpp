// SPDX-License-Identifier: MIT
// Native PSO creation and command capture seams; production bodies are generated.
#include <cassert>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <cstring>
#include <unordered_map>
using WMTPixelFormat = unsigned;
using WMTBarrierScope = unsigned;
constexpr unsigned WMTBarrierScopeBuffers = 1, WMTBarrierScopeTextures = 2;
constexpr unsigned WMTRenderStagePreRaster = 1, WMTRenderStageFragment = 2;
constexpr unsigned kBarrierTileSize = 16;
struct TileBarrierPSOKey {
  unsigned color_formats[8]{};
  unsigned raster_sample_count{};
  bool operator==(const TileBarrierPSOKey &) const = default;
};
namespace std { template<> struct hash<TileBarrierPSOKey> {
  size_t operator()(const TileBarrierPSOKey &v) const { return v.color_formats[0] ^ v.raster_sample_count; }
}; }
inline int diagnostics = 0, native_attempts = 0, live_pso_references = 0;
inline bool native_fail = false;
#define ERR(...) (++diagnostics)
struct WMTTileRenderPipelineInfo {
  unsigned color_formats[8]{};
  unsigned raster_sample_count{};
  int tile_function{};
};
namespace WMT {
struct Error {
  struct Text { const char *getUTF8String() const { return "injected tile failure"; } };
  Text description() const { return {}; }
};
using Function = int;
template<typename T> struct Reference {
  T value{};
  Reference() = default;
  Reference(T v) : value(v) { if constexpr(std::is_same_v<T, RenderPipelineState>) live_pso_references += !!value; }
  Reference(const Reference &r) : Reference(r.value) {}
  Reference(Reference &&r) : value(std::exchange(r.value, {})) {}
  ~Reference() { if constexpr(std::is_same_v<T, RenderPipelineState>) live_pso_references -= !!value; }
  operator T() const { return value; }
  Reference &operator=(Reference r) { std::swap(value, r.value); return *this; }
  auto description() const { return value.description(); }
};
inline void InitializeTileRenderPipelineInfo(WMTTileRenderPipelineInfo &v) { v = {}; }
struct Device {
  Reference<RenderPipelineState> newRenderPipelineState(const WMTTileRenderPipelineInfo &info, Reference<Error> &) {
    ++native_attempts;
    assert(info.raster_sample_count > 0 && info.tile_function == 77);
    return native_fail ? nullptr : reinterpret_cast<void *>(uintptr_t(200 + info.color_formats[0]));
  }
};
}
struct InternalCommandLibrary {
  struct Library { WMT::Reference<WMT::Function> newFunction(const char *) { return 77; } };
  Library getLibrary() { return {}; }
};
#include "barrier-state-under-test.hpp"
enum class EncoderType { Render };
struct ArgumentEncodingContext;
#include "tile-declaration-under-test.hpp"
// Optional combined-profile hook seam. Metrics owns tracing behavior; the tile
// fixture preserves the native call and success predicate when that hook is
// integrated around the extracted getPSO native call.
namespace cs2trace {
enum class HelperRoute : uint64_t { TileNativePSO = 4 };
template<class F, class Check>
auto helperCompile(HelperRoute, uint64_t, F &&fn, Check &&success) {
  auto value = std::forward<F>(fn)();
  (void)std::forward<Check>(success)(value);
  return value;
}
}
