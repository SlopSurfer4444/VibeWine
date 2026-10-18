#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace dxmt {

struct DisplayModeSize {
  uint32_t width, height;
  bool operator==(const DisplayModeSize &other) const {
    return width == other.width && height == other.height;
  }
};

// Keep complete dimension groups, including every refresh-rate variant. Input
// is the ordinary sorted, format/interlace-filtered list; never invent a mode.
// DS3's resolution menu has 32 slots, independent of refresh rate.
template <typename Mode>
void LimitDisplayModeResolutions(std::vector<Mode> &modes,
                                DisplayModeSize current, DisplayModeSize desktop) {
  constexpr size_t limit = 32;
  std::vector<DisplayModeSize> sizes;
  for (const auto &mode : modes) {
    DisplayModeSize size{mode.Width, mode.Height};
    if (std::find(sizes.begin(), sizes.end(), size) == sizes.end())
      sizes.push_back(size);
  }
  if (sizes.size() <= limit)
    return;

  // Desktop/current queries can fail or name a mode absent from this format's
  // list. Such hints are ignored. Retain the largest enumerated pixel area as
  // well, so a virtual current mode cannot displace the native-size candidate.
  const auto largest = *std::max_element(sizes.begin(), sizes.end(),
    [](DisplayModeSize a, DisplayModeSize b) {
      return uint64_t(a.width) * a.height < uint64_t(b.width) * b.height;
    });
  std::vector<DisplayModeSize> selected;
  auto select = [&](DisplayModeSize size) {
    if (selected.size() < limit &&
        std::find(sizes.begin(), sizes.end(), size) != sizes.end() &&
        std::find(selected.begin(), selected.end(), size) == selected.end())
      selected.push_back(size);
  };
  for (auto size : std::array<DisplayModeSize, 5>{{{1920, 1080}, {2560, 1440}, current, desktop, largest}})
    select(size);
  for (auto size : sizes)
    select(size);

  modes.erase(std::remove_if(modes.begin(), modes.end(), [&](const Mode &mode) {
    return std::find(selected.begin(), selected.end(), DisplayModeSize{mode.Width, mode.Height}) == selected.end();
  }), modes.end());
}

} // namespace dxmt
