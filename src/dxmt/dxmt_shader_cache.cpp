#include "dxmt_shader_cache.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include "../d3d11/cs2_trace.hpp"

namespace dxmt {

ShaderCache &
ShaderCache::getInstance(WMTMetalVersion version) {
  static dxmt::mutex mutex;
  static std::unordered_map<WMTMetalVersion, std::unique_ptr<ShaderCache>> caches;

  std::lock_guard<dxmt::mutex> lock(mutex);
  auto iter = caches.find(version);
  if (iter == caches.end()) {
    auto inserted = caches.insert({version, std::make_unique<ShaderCache>(version)});
    return *inserted.first->second;
  }
  return *iter->second;
}

ShaderCache::ShaderCache(WMTMetalVersion metal_version) {
  trace_serial_ = cs2trace::newSerial();
  if (env::getEnvVar("DXMT_SHADER_CACHE") == "0")
    return;
  std::string path;
  if (path = env::getEnvVar("DXMT_SHADER_CACHE_PATH"); !path.empty() && path.starts_with("/")) {
    if (!path.ends_with('/'))
      path += "/";
  } else {
    path = str::format("dxmt/", env::getExeName(), "/");
  }
  path += str::format("shaders_", (unsigned int)metal_version, ".db");
  const auto initialize = [&](uint32_t writer) {
    const auto trace = cs2trace::beginDetail(trace_serial_);
    const auto start = trace.gen ? cs2trace::now() : 0;
    uint32_t status = WMTCacheStatusUnavailable;
    auto result = Cache_alloc_init_checked(path.c_str(), kDXMTShaderCacheVersion, writer, &status);
    if (trace.gen)
      cs2trace::detailRecord(trace.gen, 94, start, cs2trace::now(), trace_serial_, status, trace.serial, 0);
    return result;
  };
  writer_state_->writer = WMT::Reference<WMT::CacheWriter>(initialize(1));
  scache_reader_ = WMT::Reference<WMT::CacheReader>(initialize(0));
}

WMT::Reference<WMT::DispatchData>
ShaderCache::read(const void *key, uint64_t length) {
  if (!scache_reader_)
    return {};
  std::lock_guard<dxmt::mutex> lock(scache_reader_mutex_);
  const auto trace = cs2trace::beginDetail(trace_serial_);
  const auto start = trace.gen ? cs2trace::now() : 0;
  uint32_t status = WMTCacheStatusUnavailable;
  auto data = scache_reader_.get_checked(key, length, status);
  if (trace.gen)
    cs2trace::detailRecord(trace.gen, 93, start, cs2trace::now(), trace_serial_, status, trace.serial, 0);
  return data;
}

} // namespace dxmt
