#pragma once
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "src/util/rc/util_rc_ptr.hpp"

using HRESULT = int32_t;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = int32_t(0x80004005u),
                  E_OUTOFMEMORY = int32_t(0x8007000eu), E_INVALIDARG = int32_t(0x80070057u);
#define FAILED(hr) ((hr) < 0)
#define D3D11_ASSERT(expr) require(bool(expr), "production assertion")
#define ERR(...) ((void)0)
#define DXMT_IDLE_RETIREMENT 0
using UINT = unsigned;
using D3DKMT_HANDLE = uint32_t;
using mach_port_t = uint32_t;
constexpr unsigned D3D11_USAGE_DEFAULT = 0, D3D11_USAGE_IMMUTABLE = 1, D3D11_USAGE_DYNAMIC = 2;
constexpr unsigned D3D11_BIND_RENDER_TARGET = 1, D3D11_BIND_DEPTH_STENCIL = 2, D3D11_BIND_UNORDERED_ACCESS = 4;
constexpr unsigned kD3D11OutputBindFlags = 7;
constexpr unsigned D3D11_RESOURCE_MISC_TEXTURECUBE = 1, D3D11_RESOURCE_MISC_SHARED = 2,
                   D3D11_RESOURCE_MISC_SHARED_NTHANDLE = 4, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX = 8,
                   D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 16, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS = 32;
constexpr int D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP = 1;

inline void require(bool predicate, const char* message) {
  if (!predicate) { std::fprintf(stderr, "CONTRACT: %s\n", message); std::exit(1); }
}

namespace dxmt {
enum class Seam { None, Texture, TextureAllocation, TexturePool, TextureOwner, Buffer, BufferAllocation, BufferOwner, DynamicBuffer, KeyedMutex };
enum class InitStatus { Success, OutOfMemory, SubmissionFailed };
struct InitResult { InitStatus status; uint64_t sequence; explicit operator bool() const { return status == InitStatus::Success; } };
enum class TextureAllocationFlag { GpuPrivate, GpuReadonly, ShaderReadonly, Shared, GpuManaged };
enum class BufferAllocationFlag { CpuWriteCombined, GpuReadonly, GpuManaged, GpuPrivate, SuballocateFromOnePage, CpuPlaced };
template<class T> struct Flags {
  uint32_t bits = 0;
  void set(T bit) { bits |= 1u << unsigned(bit); }
  bool test(T bit) const { return bool(bits & (1u << unsigned(bit))); }
};

struct Audit {
  Seam throw_new = Seam::None;
  bool native_null = false, wrapper_null = false, descriptor_invalid = false, traced = false, normalize_desc = false;
  bool mach_null = false, keyed_fail = false, d3dkmt_fail = false, register_fail = false, invalid_device = false;
  int init_failure_at = -1;
  InitStatus init_failure = InitStatus::OutOfMemory;
  int calls = 0, zeros = 0, uploads = 0, updates = 0, renames = 0, dynamic = 0, pools = 0;
  int d3d_creates = 0, d3d_destroys = 0, registrations = 0, names = 0;
  int fresh_ports = 0, port_releases = 0, registered_ports = 0, port_releases_while_allocation_alive = 0;
  int alive_textures = 0, alive_buffers = 0, alive_allocations = 0, alive_owners = 0, alive_pools = 0, alive_dynamic = 0, alive_mutex = 0;
  uint32_t buffer_flags = 0, texture_flags = 0;
  const void* updated_data = nullptr;
  uint64_t updated_length = 0;
  std::vector<unsigned> visited;
  std::vector<const void*> uploaded_data;
  std::vector<std::pair<size_t, size_t>> pitches;
  std::vector<std::string> ordering;
};
inline Audit audit;

template<class T, Seam seam> class RefObject {
  unsigned refs_ = 0;
public:
  static void* operator new(size_t bytes) {
    if (audit.throw_new == seam) throw std::bad_alloc();
    return ::operator new(bytes);
  }
  static void operator delete(void* memory) noexcept { ::operator delete(memory); }
  void incRef() { ++refs_; }
  void decRef() { require(refs_ > 0, "balanced reference release"); if (!--refs_) delete static_cast<T*>(this); }
};

struct Desc {
  unsigned ByteWidth = 64, Usage = D3D11_USAGE_DEFAULT, MiscFlags = 0, BindFlags = D3D11_BIND_RENDER_TARGET,
           CPUAccessFlags = 0, Format = 17, MipLevels = 3, ArraySize = 2;
};
using D3D11_BUFFER_DESC = Desc;
struct D3D11_SUBRESOURCE_DATA { const void* pSysMem; unsigned SysMemPitch, SysMemSlicePitch; };
struct WMTTextureInfo { unsigned mipmap_level_count, array_length; };
struct MTL_DXGI_FORMAT_DESC { unsigned Flag; };
struct MTLD3D11Device;
struct IMTLMinLODClampable {};
struct tag_buffer { using DESC1 = Desc; };
template<class> class DeviceTexture;
struct tag_texture_2d { using DESC1 = Desc; using COM_IMPL = DeviceTexture<tag_texture_2d>; static constexpr unsigned dimension = 2; };
struct SharedResourceData { char mach_port_name[54]; unsigned dimension; struct { Desc desc1d, desc2d, desc3d; } desc; D3DKMT_HANDLE mutex_handle; };
struct D3DDDI_ALLOCATIONINFO {};
struct D3DDDI_ALLOCATIONINFO2 { void* pSystemMem; };
struct D3DKMT_CREATESTANDARDALLOCATION { int Type; };
struct D3DKMT_CREATEALLOCATION {
  D3DKMT_HANDLE hDevice = 0, hResource = 0, hGlobalShare = 0;
  void* pPrivateRuntimeData = nullptr;
  unsigned PrivateRuntimeDataSize = 0, NumAllocations = 0;
  struct { unsigned StandardAllocation = 0, ExistingSysMem = 0, CreateResource = 0, CreateShared = 0, NtSecuritySharing = 0; } Flags;
  D3DDDI_ALLOCATIONINFO2* pAllocationInfo2 = nullptr;
  D3DKMT_CREATESTANDARDALLOCATION* pStandardAllocation = nullptr;
};
struct D3DKMT_DESTROYALLOCATION { D3DKMT_HANDLE hDevice = 0, hResource = 0; };
inline int D3DKMTCreateAllocation2(D3DKMT_CREATEALLOCATION* request) {
  ++audit.d3d_creates; audit.ordering.push_back("d3d-create");
  if (audit.d3dkmt_fail) return 1;
  request->hResource = 100; request->hGlobalShare = 200; return 0;
}
inline void D3DKMTDestroyAllocation(D3DKMT_DESTROYALLOCATION* request) {
  require(request->hResource == 100, "destroy exact adopted shared handle");
  ++audit.d3d_destroys; audit.ordering.push_back("d3d-destroy");
}
inline bool WMTBootstrapRegister(const char*, mach_port_t port) {
  require(port == 7, "register valid Mach port"); ++audit.registrations; audit.ordering.push_back("register");
  if (!audit.register_fail) ++audit.registered_ports;
  return !audit.register_fail;
}
inline void WMTMachPortDeallocate(mach_port_t port) {
  require(port == 7 && audit.fresh_ports > audit.port_releases, "deallocate exact owned fresh Mach right");
  ++audit.port_releases;
  if (audit.alive_allocations) ++audit.port_releases_while_allocation_alive;
}
inline void MakeUniqueSharedName(char (&name)[54]) { ++audit.names; std::memset(name, 0, sizeof(name)); }

struct Texture;
struct TextureAllocation : RefObject<TextureAllocation, Seam::TextureAllocation> {
  bool native = !audit.native_null;
  mach_port_t machPort = audit.mach_null ? 0 : 7;
  std::array<std::array<unsigned char, 64>, 6> bytes;
  explicit TextureAllocation(Texture*, bool shared) {
    for (auto& sub : bytes) sub.fill(0xcc);
    ++audit.alive_allocations;
    if (shared && machPort) ++audit.fresh_ports;
  }
  ~TextureAllocation() { --audit.alive_allocations; }
  bool texture() const { return native; }
};
struct BufferAllocation : RefObject<BufferAllocation, Seam::BufferAllocation> {
  bool native = !audit.native_null;
  std::array<unsigned char, 64> bytes;
  BufferAllocation() { bytes.fill(0xcc); ++audit.alive_allocations; }
  ~BufferAllocation() { --audit.alive_allocations; }
  bool buffer() const { return native; }
  void updateContents(uint64_t offset, const void* data, uint64_t length) {
    require(native, "native buffer checked before update"); require(offset == 0, "buffer upload offset");
    ++audit.updates; audit.updated_data = data; audit.updated_length = length;
    require(length <= bytes.size(), "fixture buffer bounds"); std::memcpy(bytes.data() + offset, data, length);
  }
};
struct Texture : RefObject<Texture, Seam::Texture> {
  Rc<TextureAllocation> current_;
  Texture(const WMTTextureInfo&, int) { ++audit.alive_textures; }
  ~Texture() { --audit.alive_textures; }
  Rc<TextureAllocation> allocate(Flags<TextureAllocationFlag> flags) {
    audit.texture_flags = flags.bits;
    if (audit.wrapper_null) return {};
    return new TextureAllocation(this, flags.test(TextureAllocationFlag::Shared));
  }
  Rc<TextureAllocation> rename(Rc<TextureAllocation>&& allocation) {
    require(allocation && allocation->texture(), "native texture checked before rename");
    ++audit.renames; auto previous = std::move(current_); current_ = std::move(allocation); return previous;
  }
  TextureAllocation* current() { return current_.ptr(); }
};
struct Buffer : RefObject<Buffer, Seam::Buffer> {
  Rc<BufferAllocation> current_;
  unsigned length_;
  Buffer(unsigned length, int) : length_(length) { ++audit.alive_buffers; }
  ~Buffer() { --audit.alive_buffers; }
  unsigned length() { return length_; }
  Rc<BufferAllocation> allocate(Flags<BufferAllocationFlag> flags) {
    audit.buffer_flags = flags.bits;
    if (audit.wrapper_null) return {};
    return new BufferAllocation;
  }
  Rc<BufferAllocation> rename(Rc<BufferAllocation>&& allocation) {
    require(allocation && allocation->buffer(), "native buffer checked before rename");
    ++audit.renames; auto previous = std::move(current_); current_ = std::move(allocation); return previous;
  }
};
struct DynamicBuffer : RefObject<DynamicBuffer, Seam::DynamicBuffer> {
  DynamicBuffer(Buffer*, Flags<BufferAllocationFlag>) { ++audit.dynamic; ++audit.alive_dynamic; }
  ~DynamicBuffer() { --audit.alive_dynamic; }
};
struct RenamableTexturePool : RefObject<RenamableTexturePool, Seam::TexturePool> {
  Texture* texture_;
  Flags<TextureAllocationFlag> flags_;
  Rc<TextureAllocation> held;
  RenamableTexturePool(Texture* texture, size_t capacity, Flags<TextureAllocationFlag> flags) : texture_(texture), flags_(flags) {
    require(capacity == 32, "renamable pool capacity"); ++audit.pools; ++audit.alive_pools;
  }
  ~RenamableTexturePool() { --audit.alive_pools; }
  Rc<TextureAllocation> getNext(uint64_t frame) { require(frame == 0, "initial renamable frame"); held = texture_->allocate(flags_); return held; }
};
struct KeyedMutex : RefObject<KeyedMutex, Seam::KeyedMutex> {
  KeyedMutex() { ++audit.alive_mutex; }
  ~KeyedMutex() { --audit.alive_mutex; }
  unsigned globalHandle() { return 300; }
  static Rc<KeyedMutex> create(int) { if (audit.keyed_fail) return {}; return new KeyedMutex; }
};
struct Initializer {
  struct TextureCommand { TextureAllocation* allocation; unsigned sub; std::array<unsigned char, 64> bytes{}; };
  std::vector<TextureCommand> pending_textures;
  std::vector<BufferAllocation*> pending_buffers;
  std::vector<Rc<TextureAllocation>> retained_textures;
  std::vector<Rc<BufferAllocation>> retained_buffers;
  InitResult result() {
    int call = audit.calls++;
    audit.ordering.push_back("initialize");
    const auto status = call == audit.init_failure_at ? audit.init_failure : InitStatus::Success;
    return {status, status == InitStatus::Success ? uint64_t(call + 1) : 0};
  }
  InitResult initWithZero(Texture*, TextureAllocation* allocation, unsigned slice, unsigned mip) {
    require(allocation && allocation->texture(), "native texture checked before initializer");
    ++audit.zeros; audit.visited.push_back(slice * 100 + mip);
    auto status = result();
    if (status) { retained_textures.emplace_back(allocation); pending_textures.push_back({allocation, slice * 3 + mip, {}}); }
    return status;
  }
  InitResult initWithData(Texture*, TextureAllocation* allocation, unsigned slice, unsigned mip,
                          const void* data, size_t row, size_t depth, unsigned format) {
    require(format == 91, "format flags passed to initializer");
    require(allocation && allocation->texture(), "native texture checked before initializer");
    ++audit.uploads; audit.visited.push_back(slice * 100 + mip); audit.uploaded_data.push_back(data); audit.pitches.emplace_back(row, depth);
    auto status = result();
    if (status) {
      retained_textures.emplace_back(allocation); TextureCommand command{allocation, slice * 3 + mip, {}};
      std::memcpy(command.bytes.data(), data, command.bytes.size()); pending_textures.push_back(command);
    }
    return status;
  }
  InitResult initWithZero(BufferAllocation* allocation, uint64_t offset, uint64_t length) {
    require(allocation && allocation->buffer(), "native buffer checked before initializer");
    require(offset == 0 && length == 64, "buffer clear bounds"); ++audit.zeros;
    auto status = result(); if (status) { retained_buffers.emplace_back(allocation); pending_buffers.push_back(allocation); } return status;
  }
  void drain() {
    for (auto& command : pending_textures) command.allocation->bytes[command.sub] = command.bytes;
    for (auto* allocation : pending_buffers) allocation->bytes.fill(0);
    pending_textures.clear(); pending_buffers.clear(); retained_textures.clear(); retained_buffers.clear();
  }
};
struct Queue { Initializer initializer; };
struct Device { Queue queue_; Queue& queue() { return queue_; } };
struct MTLD3D11Device {
  Device dxmt;
  int GetMTLDevice() { return 1; }
  Device& GetDXMTDevice() { return dxmt; }
  bool IsTraced() { return audit.traced; }
  D3DKMT_HANDLE GetLocalD3DKMT() { return audit.invalid_device ? 0 : 0xc0000001; }
};
template<class Tag, class... Bases> struct TResourceBase {
  Desc desc;
  MTLD3D11Device* m_parent;
  Rc<Texture> texture_;
  Rc<KeyedMutex> keyed_mutex_;
  TResourceBase(const Desc& descriptor, MTLD3D11Device* parent) : desc(descriptor), m_parent(parent) { ++audit.alive_owners; }
  ~TResourceBase() { --audit.alive_owners; }
};
template<class T> class Com {
  T* pointer_;
public:
  Com(T* pointer) : pointer_(pointer) {}
  ~Com() { delete pointer_; }
  T* operator->() { return pointer_; }
  T* takeOwnership() noexcept { return std::exchange(pointer_, nullptr); }
  Com(const Com&) = delete;
};
template<class T> T* ref(T* pointer) { return pointer; }
inline HRESULT CreateMTLTextureDescriptor(MTLD3D11Device*, const Desc* original, Desc* final, WMTTextureInfo* info) {
  if (audit.descriptor_invalid) return E_INVALIDARG;
  *final = *original;
  if (audit.normalize_desc) { final->Format = 23; final->MipLevels = 2; final->ArraySize = 1; }
  *info = {final->MipLevels, final->ArraySize}; return S_OK;
}
inline void MTLQueryDXGIFormat(int, unsigned format, MTL_DXGI_FORMAT_DESC& descriptor) {
  require(format == (audit.normalize_desc ? 23u : 17u), "normalized texture format forwarded"); descriptor.Flag = 91;
}
struct Subresource { unsigned ArraySlice, MipLevel, SubresourceId; };
inline std::vector<Subresource> EnumerateSubresources(const Desc& descriptor) {
  std::vector<Subresource> result;
  for (unsigned slice = 0; slice < descriptor.ArraySize; ++slice)
    for (unsigned mip = 0; mip < descriptor.MipLevels; ++mip)
      result.push_back({slice, mip, slice * descriptor.MipLevels + mip});
  return result;
}
} // namespace dxmt
