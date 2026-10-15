#pragma once

#include "util_flags.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#ifdef DXMT_DEBUG
#include <map>
#include <mutex>
#endif
#include <utility>
#include <chrono>

namespace dxmt {

using clock = std::chrono::high_resolution_clock;

enum class FeatureCompatibility {
    UnsupportedGeometryDraw,
    UnsupportedTessellationOutputPrimitive,
    UnsupportedIndirectTessellationDraw,
    UnsupportedGeometryTessellationDraw,
    UnsupportedDrawAuto,
    UnsupportedPredication,
    UnsupportedStreamOutputAppending,
    UnsupportedMultipleStreamOutput,
  };

enum class ScalerType {
  None,
  Spatial,
  Temporal,
};

struct ScalerInfo {
  ScalerType type = ScalerType::None;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
  bool auto_exposure;
  bool motion_vector_highres;
};

struct FrameStatistics {
  Flags<FeatureCompatibility> compatibility_flags;
  uint32_t command_buffer_count = 0;
  uint32_t sync_count = 0;
  clock::duration sync_interval{};
  clock::duration commit_interval{};
  uint32_t render_pass_count = 0;
  uint32_t render_pass_optimized = 0;
  uint32_t clear_pass_count = 0;
  uint32_t clear_pass_optimized = 0;
  uint32_t resolve_pass_optimized = 0;
  uint32_t compute_pass_count = 0;
  uint32_t blit_pass_count = 0;
  uint32_t event_stall = 0;
  uint32_t latency = 0;
  clock::duration encode_prepare_interval{};
  clock::duration encode_flush_interval{};
  clock::duration drawable_blocking_interval{};
  clock::duration present_latency_interval{};
  ScalerInfo last_scaler_info{};

  void
  reset() {
    compatibility_flags.clrAll();
    command_buffer_count = 0;
    sync_count = 0;
    sync_interval = {};
    commit_interval = {};
    render_pass_count = 0;
    render_pass_optimized = 0;
    clear_pass_count = 0;
    clear_pass_optimized = 0;
    resolve_pass_optimized = 0;
    compute_pass_count = 0;
    blit_pass_count = 0;
    event_stall = 0;
    latency = 0;
    encode_prepare_interval = {};
    encode_flush_interval = {};
    drawable_blocking_interval = {};
    present_latency_interval = {};
    last_scaler_info = {};
  };
};

constexpr size_t kFrameStatisticsCount = 16;

// A HUD snapshot owns its data. No references to mutable producer state escape.
struct FrameStatisticsSnapshot {
  FrameStatistics frame{};
  FrameStatistics min{};
  FrameStatistics max{};
  FrameStatistics average{};
  size_t sample_count = 0;
};

class FrameStatisticsContainer {
#ifdef DXMT_DEBUG
  struct PendingFrame {
    FrameStatistics data{};
    bool cpu_complete = false;
    bool encoding_complete = false;
  };
  struct CompletedFrame {
    FrameStatistics data{};
    uint64_t id = 0;
    bool valid = false;
  };
  mutable std::mutex mutex_;
  // In-flight frames cannot alias when the CPU laps the completed-sample ring.
  std::map<uint64_t, PendingFrame> pending_;
  std::array<CompletedFrame, kFrameStatisticsCount> completed_{};
  uint64_t latest_completed_ = 0;
  bool has_completed_ = false;

  void
  complete(uint64_t frame, bool cpu) {
    std::lock_guard lock(mutex_);
    auto found = pending_.find(frame);
    if (found == pending_.end() && has_completed_ && frame <= latest_completed_)
      return; // duplicate completion cannot recreate an already published frame
    auto &pending = pending_[frame];
    (cpu ? pending.cpu_complete : pending.encoding_complete) = true;
    if (pending.cpu_complete && pending.encoding_complete) {
      auto slot = std::min_element(completed_.begin(), completed_.end(), [](const auto &a, const auto &b) {
        if (a.valid != b.valid)
          return !a.valid;
        return a.id < b.id;
      });
      if (!slot->valid || slot->id < frame)
        *slot = {pending.data, frame, true};
      latest_completed_ = has_completed_ ? std::max(latest_completed_, frame) : frame;
      has_completed_ = true;
      pending_.erase(frame);
    }
  }

public:
  // Callbacks only mutate/copy statistics. Never call native APIs, wait, format,
  // or reenter the container while holding this lock.
  template <typename F>
  void
  update(uint64_t frame, F &&mutate) {
    std::lock_guard lock(mutex_);
    auto found = pending_.find(frame);
    if (found == pending_.end() && has_completed_ && frame <= latest_completed_)
      return; // late updates cannot corrupt a finalized sample
    std::forward<F>(mutate)(pending_[frame].data);
  }

  void cpuComplete(uint64_t frame) { complete(frame, true); }
  // Encoding completion means all statistics writes, including flush timing,
  // have finished; it does not claim GPU execution has completed.
  void encodingComplete(uint64_t frame) { complete(frame, false); }

  FrameStatisticsSnapshot
  snapshot(uint64_t before_frame) const {
    std::array<CompletedFrame, kFrameStatisticsCount> samples;
    {
      std::lock_guard lock(mutex_);
      samples = completed_;
    }
    std::sort(samples.begin(), samples.end(), [](const auto &a, const auto &b) { return a.id > b.id; });
    FrameStatisticsSnapshot result;
    std::array<FrameStatistics, kFrameStatisticsCount - 1> frames{};
    for (const auto &sample : samples) {
      if (!sample.valid || sample.id >= before_frame)
        continue;
      frames[result.sample_count++] = sample.data;
      if (result.sample_count == frames.size())
        break;
    }
    if (!result.sample_count)
      return result;
    result.frame = result.min = result.max = frames[0];
    const auto count = result.sample_count;
    // Widen integer totals so a full ring of uint32_t counters cannot wrap.
#define DXMT_AGGREGATE_COUNTER(member) do { \
    uint64_t total = 0; \
    for (size_t i = 0; i < count; ++i) { \
      result.min.member = std::min(result.min.member, frames[i].member); \
      result.max.member = std::max(result.max.member, frames[i].member); \
      total += frames[i].member; \
    } \
    result.average.member = total / count; \
  } while (false)
    DXMT_AGGREGATE_COUNTER(command_buffer_count);
    DXMT_AGGREGATE_COUNTER(sync_count);
    DXMT_AGGREGATE_COUNTER(render_pass_count);
    DXMT_AGGREGATE_COUNTER(render_pass_optimized);
    DXMT_AGGREGATE_COUNTER(clear_pass_count);
    DXMT_AGGREGATE_COUNTER(clear_pass_optimized);
    DXMT_AGGREGATE_COUNTER(resolve_pass_optimized);
    DXMT_AGGREGATE_COUNTER(compute_pass_count);
    DXMT_AGGREGATE_COUNTER(blit_pass_count);
    DXMT_AGGREGATE_COUNTER(event_stall);
    DXMT_AGGREGATE_COUNTER(latency);
#undef DXMT_AGGREGATE_COUNTER
    // Divide before summing to avoid overflow even for very long durations.
#define DXMT_AGGREGATE_DURATION(member) do { \
    clock::duration::rep quotient = 0, remainder = 0; \
    const auto divisor = static_cast<clock::duration::rep>(count); \
    for (size_t i = 0; i < count; ++i) { \
      result.min.member = std::min(result.min.member, frames[i].member); \
      result.max.member = std::max(result.max.member, frames[i].member); \
      quotient += frames[i].member.count() / divisor; \
      remainder += frames[i].member.count() % divisor; \
    } \
    result.average.member = clock::duration(quotient + remainder / divisor); \
  } while (false)
    DXMT_AGGREGATE_DURATION(commit_interval);
    DXMT_AGGREGATE_DURATION(sync_interval);
    DXMT_AGGREGATE_DURATION(encode_prepare_interval);
    DXMT_AGGREGATE_DURATION(encode_flush_interval);
    DXMT_AGGREGATE_DURATION(drawable_blocking_interval);
    DXMT_AGGREGATE_DURATION(present_latency_interval);
#undef DXMT_AGGREGATE_DURATION
    return result;
  }
#else
public:
  // Statistics feed only the DXMT_DEBUG HUD. Release builds keep no producer
  // state and do not invoke mutation callbacks or allocate pending records.
  template <typename F> void update(uint64_t, F &&) {}
  void cpuComplete(uint64_t) {}
  void encodingComplete(uint64_t) {}
  FrameStatisticsSnapshot snapshot(uint64_t) const { return {}; }
#endif
};

} // namespace dxmt
