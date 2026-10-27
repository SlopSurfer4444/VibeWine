// SPDX-License-Identifier: MIT
// Real project D3D/DXGI declarations; production method bodies are injected.
#include "d3d11_3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string_view>

template <typename Desc> struct Resource {
  Desc desc;
  /* RESOURCE_USAGE */
};

template <typename R> struct DXGIResource {
  R *resource_;
  /* DXGI_FORWARD */
};

static unsigned assertions;
static void check(bool condition, const char *message) {
  ++assertions;
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", message);
    std::exit(2);
  }
}

template <typename Desc>
static void exercise(D3D11_USAGE kind, UINT cpu, UINT bind, UINT misc, DXGI_USAGE expected) {
  Resource<Desc> resource{};
  resource.desc.Usage = kind;
  resource.desc.CPUAccessFlags = cpu;
  resource.desc.BindFlags = bind;
  resource.desc.MiscFlags = misc;
  Desc before;
  std::memcpy(&before, &resource.desc, sizeof(before));
  DXGIResource<Resource<Desc>> dxgi{&resource};
  struct Output { UINT left; DXGI_USAGE usage; UINT right; } output{0x12345678, ~0u, 0x87654321};
  check(dxgi.GetUsage(&output.usage) == S_OK, "success status");
  check(output.usage == expected, "usage bits");
  check(output.left == 0x12345678 && output.right == 0x87654321, "output bounds");
  check(!std::memcmp(&before, &resource.desc, sizeof(before)), "descriptor unchanged on success");
  check(dxgi.GetUsage(nullptr) == E_INVALIDARG, "null output status");
  check(!std::memcmp(&before, &resource.desc, sizeof(before)), "descriptor unchanged on null output");
  output.usage = 0;
  check(dxgi.GetUsage(&output.usage) == S_OK && output.usage == expected, "repeat overwrites output");
}

template <typename Desc> static void matrix(std::string_view mode, bool buffer) {
  if (mode == "shader") {
    exercise<Desc>(D3D11_USAGE_DEFAULT, 0, D3D11_BIND_SHADER_RESOURCE, 0, DXGI_USAGE_SHADER_INPUT);
  } else if (mode == "render") {
    exercise<Desc>(D3D11_USAGE_DEFAULT, 0, D3D11_BIND_RENDER_TARGET, 0, DXGI_USAGE_RENDER_TARGET_OUTPUT);
  } else if (mode == "unordered") {
    exercise<Desc>(D3D11_USAGE_DEFAULT, 0, D3D11_BIND_UNORDERED_ACCESS, 0, DXGI_USAGE_UNORDERED_ACCESS);
  } else if (mode == "combined") {
    exercise<Desc>(D3D11_USAGE_DEFAULT, 0,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS, 0,
      DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS);
  } else if (mode == "cpu-bindings") {
    for (auto kind : {D3D11_USAGE_DEFAULT, D3D11_USAGE_IMMUTABLE, D3D11_USAGE_DYNAMIC, D3D11_USAGE_STAGING})
      for (UINT cpu : {0u, UINT(D3D11_CPU_ACCESS_READ), UINT(D3D11_CPU_ACCESS_WRITE),
                      UINT(D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)})
        exercise<Desc>(kind, cpu, D3D11_BIND_SHADER_RESOURCE, 0, DXGI_USAGE_SHADER_INPUT);
  } else if (mode == "controls") {
    // Also include descriptor combinations rejected during actual creation:
    // this isolated query must not reinterpret CPU flags or mutate metadata.
    for (auto kind : {D3D11_USAGE_DEFAULT, D3D11_USAGE_IMMUTABLE, D3D11_USAGE_DYNAMIC, D3D11_USAGE_STAGING})
      for (UINT cpu : {0u, UINT(D3D11_CPU_ACCESS_READ), UINT(D3D11_CPU_ACCESS_WRITE),
                      UINT(D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)})
        exercise<Desc>(kind, cpu, 0, 0, 0);
    for (UINT bind : {UINT(D3D11_BIND_DEPTH_STENCIL), UINT(D3D11_BIND_DECODER), UINT(D3D11_BIND_VIDEO_ENCODER)})
      exercise<Desc>(D3D11_USAGE_DEFAULT, 0, bind, 0, 0);
    if (buffer) {
      for (UINT bind : {UINT(D3D11_BIND_VERTEX_BUFFER), UINT(D3D11_BIND_INDEX_BUFFER),
                       UINT(D3D11_BIND_CONSTANT_BUFFER), UINT(D3D11_BIND_STREAM_OUTPUT)})
        exercise<Desc>(D3D11_USAGE_DEFAULT, 0, bind, 0, 0);
    }
  } else if (mode == "shared") {
    for (UINT misc : {UINT(D3D11_RESOURCE_MISC_SHARED), UINT(D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX),
                     UINT(D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)})
      exercise<Desc>(D3D11_USAGE_DEFAULT, 0,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, misc,
        DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) return 3;
  const std::string_view mode(argv[1]);
  const std::string_view resource(argv[2]);
  if (mode != "shader" && mode != "render" && mode != "unordered" && mode != "combined"
      && mode != "cpu-bindings" && mode != "controls" && mode != "shared") return 3;
  if (resource == "buffer") matrix<D3D11_BUFFER_DESC>(mode, true);
  else if (resource == "texture1d") matrix<D3D11_TEXTURE1D_DESC>(mode, false);
  else if (resource == "texture2d1") matrix<D3D11_TEXTURE2D_DESC1>(mode, false);
  else if (resource == "texture3d1") matrix<D3D11_TEXTURE3D_DESC1>(mode, false);
  // Public descriptions also carry the same common fields used by GetUsage.
  else if (resource == "texture2d") matrix<D3D11_TEXTURE2D_DESC>(mode, false);
  else if (resource == "texture3d") matrix<D3D11_TEXTURE3D_DESC>(mode, false);
  else return 3;
  std::printf("PASS %s %s %u assertions\n", argv[1], argv[2], assertions);
}
