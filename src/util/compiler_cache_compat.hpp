#pragma once

#include "airconv_public.h"
#include <cstdint>

namespace dxmt {

// Bump when VibeWine compiler output changes without an AIRCONV API version
// change. This namespaces persisted output; it does not change the compiler ABI.
constexpr uint32_t kVibeWineCompilerCacheEpoch = 2;

constexpr uint64_t
makeCompilerCacheCompatibility(uint32_t upstream_version, uint32_t local_epoch) {
  return (uint64_t(upstream_version) << 32) | uint64_t(local_epoch);
}

// Shared by the shader database table and optional graphics PSO archive key.
constexpr uint64_t kDXMTCompilerCacheCompatibility =
    makeCompilerCacheCompatibility(AIRCONV_VERSION, kVibeWineCompilerCacheEpoch);

} // namespace dxmt
