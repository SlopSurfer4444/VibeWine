#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>

namespace dxmt {

// Read as a string: Config's legacy signed integer parser is not overflow-safe.
// Malformed/overflowing values use the unchanged automatic scheduler policy.
inline uint64_t parse_compiler_worker_limit(std::string_view value) {
  uint64_t limit = 0;
  if (value.empty())
    return 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), limit);
  return error == std::errc{} && end == value.data() + value.size() ? limit : 0;
}

} // namespace dxmt
