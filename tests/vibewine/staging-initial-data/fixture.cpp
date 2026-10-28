#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <tl/generator.hpp>
using UINT = unsigned;
using HRESULT = int;
constexpr HRESULT S_OK = 0, E_FAIL = -1, E_INVALIDARG = -2;
#define FAILED(x) ((x) < 0)
#define D3D11_ASSERT(x) assert(x)
#define ERR(...) ((void)0)
#define DXMT_IDLE_RETIREMENT 0
constexpr unsigned D3D11_RESOURCE_DIMENSION_TEXTURE1D = 1;
constexpr unsigned D3D11_RESOURCE_DIMENSION_TEXTURE2D = 2;
constexpr unsigned D3D11_RESOURCE_DIMENSION_TEXTURE3D = 3;
constexpr unsigned MTL_DXGI_FORMAT_BC = 1, MTL_DXGI_FORMAT_DEPTH_PLANER = 2,
                   MTL_DXGI_FORMAT_STENCIL_PLANER = 4, WMTPixelFormatInvalid = 0;
enum Format { R8, R32, RGBA8, BC1, BC3, INVALID };
struct D3D11_TEXTURE1D_DESC { UINT Width, MipLevels, ArraySize; Format Format; };
struct D3D11_TEXTURE2D_DESC1 { UINT Width, Height, MipLevels, ArraySize; Format Format; };
struct D3D11_TEXTURE3D_DESC1 { UINT Width, Height, Depth, MipLevels; Format Format; };
struct D3D11_SUBRESOURCE_DATA { const void *pSysMem; UINT SysMemPitch, SysMemSlicePitch; };
struct WMTTextureInfo {};
struct MTL_DXGI_FORMAT_DESC { unsigned PixelFormat = 1, BytesPerTexel = 0, Flag = 0; };
static unsigned layout_queries = 0, fail_query = 0, allocations = 0;
struct Metal {
  unsigned minimumLinearTextureAlignmentForPixelFormat(unsigned) { return 256; }
};
struct MTLD3D11Device { Metal GetMTLDevice() { return {}; } };
HRESULT MTLQueryDXGIFormat(Metal, Format f, MTL_DXGI_FORMAT_DESC &out) {
  if (++layout_queries == fail_query || f == INVALID) return E_FAIL;
  out.BytesPerTexel = f == R8 ? 1 : f == BC1 ? 8 : f == BC3 ? 16 : 4;
  out.Flag = f == BC1 || f == BC3 ? MTL_DXGI_FORMAT_BC : 0;
  return S_OK;
}
unsigned align(unsigned n, unsigned a) { return (n + a - 1) / a * a; }
template<class T> void GetMipmapSize(const T *, uint32_t, uint32_t *, uint32_t *, uint32_t *);
template<class T> HRESULT GetLinearTextureLayout(MTLD3D11Device *, const T &, uint32_t,
                                                uint32_t &, uint32_t &, uint32_t &, bool = true);
// MIPMAPS
// LAYOUTS
using tl::generator;
struct SUBRESOURCE_ENTRY { uint32_t SubresourceId, MipLevel, ArraySlice; };
unsigned D3D11CalcSubresource(unsigned mip, unsigned array, unsigned levels) { return mip + array * levels; }
// ENUMERATORS
struct TextureBase { virtual ~TextureBase() = default; };
struct tag_texture_1d { using DESC1 = D3D11_TEXTURE1D_DESC; using COM_IMPL = TextureBase;
  static constexpr unsigned dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D; };
struct tag_texture_2d { using DESC1 = D3D11_TEXTURE2D_DESC1; using COM_IMPL = TextureBase;
  static constexpr unsigned dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D; };
struct tag_texture_3d { using DESC1 = D3D11_TEXTURE3D_DESC1; using COM_IMPL = TextureBase;
  static constexpr unsigned dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D; };
template<class T> struct Rc : std::shared_ptr<T> { Rc(T *p) : std::shared_ptr<T>(p) {} };
struct StagingResource {
  std::vector<uint8_t> bytes;
  unsigned length, row, image;
  StagingResource(Metal, unsigned len, unsigned bpr, unsigned bpi)
      : bytes(len + 64, 0xcd), length(len), row(bpr), image(bpi) { ++allocations; }
  void *mappedImmediateMemory() { return bytes.data() + 32; }
};
template<class Tag> struct StagingTexture : TextureBase {
  std::vector<Rc<StagingResource>> subresources;
  StagingTexture(const typename Tag::DESC1 *, MTLD3D11Device *, std::vector<Rc<StagingResource>> &&s)
      : subresources(std::move(s)) {}
};
template<class T> T *ref(T *p) { return p; }
template<class T> HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *, const T *in, T *out, WMTTextureInfo *) {
  *out = *in; return S_OK;
}
void *ptr_add(void *p, size_t n) { return static_cast<uint8_t *>(p) + n; }
const void *ptr_add(const void *p, size_t n) { return static_cast<const uint8_t *>(p) + n; }
static unsigned copy_calls = 0;
void *counted_memcpy(void *dst, const void *src, size_t n) {
  ++copy_calls;
  return std::memcpy(dst, src, n);
}
#define memcpy counted_memcpy
// CREATE
#undef memcpy
static unsigned failures = 0, cases = 0, checked_subresources = 0;
void require(bool ok, const std::string &what) {
  if (!ok) { ++failures; std::fprintf(stderr, "CONTRACT: %s\n", what.c_str()); }
}
uint8_t payload(unsigned sub, unsigned image, unsigned row, unsigned byte) {
  return uint8_t(1 + (sub * 41 + image * 23 + row * 17 + byte * 7) % 190);
}
struct Input {
  std::unique_ptr<uint8_t[]> data;
  unsigned width, rows, depth, logical, pitch, slice, length, copied;
};
template<class Tag> void run_case(const char *label, const typename Tag::DESC1 &desc,
                                  unsigned row_padding = 0, unsigned slice_padding = 0,
                                  bool odd_1d_pitch = false, bool short_pitch = false,
                                  bool initialized = true) {
  ++cases;
  constexpr bool one = Tag::dimension == 1, three = Tag::dimension == 3;
  unsigned levels = desc.MipLevels;
  unsigned arrays = 1;
  if constexpr (!three) arrays = desc.ArraySize;
  std::vector<Input> inputs;
  std::vector<D3D11_SUBRESOURCE_DATA> initial;
  for (unsigned a = 0; a < arrays; ++a) for (unsigned mip = 0; mip < levels; ++mip) {
    unsigned id = a * levels + mip;
    unsigned width = std::max(1u, desc.Width >> mip), height = 1, depth = 1;
    if constexpr (!one) height = std::max(1u, desc.Height >> mip);
    if constexpr (three) depth = std::max(1u, desc.Depth >> mip);
    const bool bc = desc.Format == BC1 || desc.Format == BC3;
    // Oracle uses format block geometry, independent of production layout helpers.
    unsigned logical = bc ? ((width + 3) / 4) * (desc.Format == BC1 ? 8 : 16)
                          : width * (desc.Format == R8 ? 1 : 4);
    unsigned rows = bc ? (height + 3) / 4 : height;
    unsigned pitch = one ? logical : short_pitch ? std::max(1u, logical / 2) : logical + row_padding;
    unsigned copied = std::min(logical, pitch);
    unsigned slice = pitch * rows + slice_padding;
    unsigned length = (depth - 1) * slice + (rows - 1) * pitch + copied;
    Input in{std::make_unique<uint8_t[]>(length), width, rows, depth, logical, pitch, slice, length, copied};
    std::memset(in.data.get(), 0xee, length);
    for (unsigned z = 0; z < depth; ++z) for (unsigned y = 0; y < rows; ++y)
      for (unsigned x = 0; x < copied; ++x) in.data[z * slice + y * pitch + x] = payload(id, z, y, x);
    initial.push_back({in.data.get(), one ? (odd_1d_pitch ? 0x7fffffffu : 0u) : pitch,
                       three ? slice : 0x6fffffffu});
    inputs.push_back(std::move(in));
  }
  layout_queries = allocations = copy_calls = 0; fail_query = 0;
  MTLD3D11Device device;
  TextureBase *texture = nullptr;
  auto hr = CreateStagingTextureInternal<Tag>(&device, &desc, initialized ? initial.data() : nullptr, &texture);
  require(hr == S_OK && texture, std::string(label) + " creation");
  if (!texture) return;
  auto out = static_cast<StagingTexture<Tag> *>(texture);
  require(out->subresources.size() == inputs.size(), std::string(label) + " subresource count");
  unsigned expected_copies = 0;
  for (unsigned id = 0; id < inputs.size(); ++id) {
    ++checked_subresources;
    auto &in = inputs[id]; auto &buffer = *out->subresources[id];
    expected_copies += in.rows * in.depth;
    std::vector<uint8_t> expected(buffer.bytes.size(), 0xcd);
    if (initialized) for (unsigned z = 0; z < in.depth; ++z) for (unsigned y = 0; y < in.rows; ++y)
      for (unsigned x = 0; x < in.copied; ++x)
        expected[32 + z * buffer.image + y * buffer.row + x] = payload(id, z, y, x);
    require(buffer.bytes == expected, std::string(label) + " payload/padding/guards sub=" + std::to_string(id));
    std::vector<uint8_t> source_expected(in.length, 0xee);
    for (unsigned z = 0; z < in.depth; ++z) for (unsigned y = 0; y < in.rows; ++y)
      for (unsigned x = 0; x < in.copied; ++x) source_expected[z * in.slice + y * in.pitch + x] = payload(id, z, y, x);
    require(std::memcmp(source_expected.data(), in.data.get(), in.length) == 0, std::string(label) + " source unchanged");
  }
  require(copy_calls == (initialized ? expected_copies : 0), std::string(label) + " copied row count");
  require(initialized || layout_queries == inputs.size(), std::string(label) + " null initial avoids extra queries");
  delete texture;
}
int main(int argc, char **) {
  for (auto f : {R8, R32, RGBA8}) for (unsigned w : {1u, 3u, 13u}) for (bool pitch : {false, true})
    run_case<tag_texture_1d>("1D tight", {w, 1, 1, f}, 0, 0, pitch);
  for (auto f : {R8, R32, RGBA8})
    run_case<tag_texture_1d>("1D mips arrays", {13, 4, 3, f}, 0, 0, true);
  for (auto f : {R8, R32, RGBA8}) for (unsigned extra : {0u, 1u})
    run_case<tag_texture_1d>("1D at/across alignment", {(f == R8 ? 256u : 64u) + extra, 1, 1, f});
  if (argc > 1) {
    std::printf("cases=%u subresources=%u failures=%u\n", cases, checked_subresources, failures);
    return failures ? 1 : 0;
  }
  for (auto f : {R8, RGBA8, BC1, BC3}) {
    run_case<tag_texture_2d>("2D tight mips arrays", {13, 7, 4, 2, f});
    run_case<tag_texture_2d>("2D padded mips arrays", {7, 5, 3, 2, f}, 19);
    run_case<tag_texture_3d>("3D padded rows slices mips", {7, 5, 3, 3, f}, 17, 43);
  }
  run_case<tag_texture_2d>("2D short pitch", {13, 3, 1, 1, R8}, 0, 0, false, true);
  run_case<tag_texture_3d>("3D short pitch", {13, 3, 2, 1, RGBA8}, 0, 11, false, true);
  run_case<tag_texture_1d>("1D null initial", {13, 4, 2, R8}, 0, 0, false, false, false);
  run_case<tag_texture_2d>("2D null initial", {7, 5, 3, 2, BC1}, 0, 0, false, false, false);
  run_case<tag_texture_3d>("3D null initial", {7, 5, 3, 3, RGBA8}, 0, 0, false, false, false);
  // Failure of either real layout call must occur before allocation, publication
  // or copying. The second query exists only for initialized resources.
  for (unsigned query : {1u, 2u}) {
    ++cases;
    layout_queries = allocations = copy_calls = 0; fail_query = query;
    MTLD3D11Device device;
    D3D11_TEXTURE1D_DESC desc{1, 1, 1, R8};
    uint8_t data = 0x41;
    D3D11_SUBRESOURCE_DATA initial{&data, 0, 0};
    TextureBase *texture = nullptr;
    auto hr = CreateStagingTextureInternal<tag_texture_1d>(&device, &desc, &initial, &texture);
    require(hr == E_FAIL && !texture && allocations == 0 && copy_calls == 0,
            "layout failure has no allocation/publication/copy");
    delete texture;
  }
  std::printf("cases=%u subresources=%u failures=%u\n", cases, checked_subresources, failures);
  return failures ? 1 : 0;
}
