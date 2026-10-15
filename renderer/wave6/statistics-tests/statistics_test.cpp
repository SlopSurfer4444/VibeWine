#include "dxmt_statistics.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace dxmt;

static void complete(FrameStatisticsContainer &stats, uint64_t id, bool cpu_first = true) {
  if (cpu_first) { stats.cpuComplete(id); stats.encodingComplete(id); }
  else { stats.encodingComplete(id); stats.cpuComplete(id); }
}

static void arithmetic() {
  FrameStatisticsContainer stats;
  assert(stats.snapshot(100).sample_count == 0);
  const uint32_t commands[]{2, 4, 9}, sync[]{20, 40, 90}, stalls[]{5, 7, 11};
  for (uint64_t id = 0; id < 3; ++id) {
    stats.update(id, [&](auto &s) {
      s.command_buffer_count = commands[id]; s.sync_count = sync[id]; s.event_stall = stalls[id];
      s.commit_interval = clock::duration(commands[id] * 5);
      s.sync_interval = clock::duration(sync[id]);
      s.encode_prepare_interval = clock::duration(commands[id] * 2);
      s.encode_flush_interval = clock::duration(commands[id] * 4);
      s.drawable_blocking_interval = clock::duration(commands[id]);
      s.present_latency_interval = clock::duration(commands[id] * 3);
      s.render_pass_count = commands[id];
      s.compatibility_flags.set(FeatureCompatibility::UnsupportedDrawAuto);
      s.last_scaler_info = {ScalerType::Spatial, 1280, 720, 2560, 1440, false, false};
    });
    if (id == 0) {
      stats.encodingComplete(id);
      assert(stats.snapshot(100).sample_count == 0); // unfinished CPU timing is invalid
      stats.cpuComplete(id);
      auto startup = stats.snapshot(100);
      assert(startup.sample_count == 1 && startup.average.command_buffer_count == 2);
    } else complete(stats, id, id % 2);
  }
  auto s = stats.snapshot(3);
  assert(s.sample_count == 3 && s.frame.command_buffer_count == 9);
  assert(s.min.command_buffer_count == 2 && s.max.command_buffer_count == 9 && s.average.command_buffer_count == 5);
  assert(s.min.event_stall == 5 && s.max.event_stall == 11 && s.average.event_stall == 7);
  assert(s.min.sync_count == 20 && s.average.sync_count == 50);
  assert(s.min.commit_interval.count() == 10 && s.max.commit_interval.count() == 45 && s.average.commit_interval.count() == 25);
  assert(s.average.encode_prepare_interval.count() == 10 && s.average.encode_flush_interval.count() == 20);
  assert(s.average.drawable_blocking_interval.count() == 5 && s.average.present_latency_interval.count() == 15);
  assert(s.average.render_pass_count == 5);
  assert(s.frame.last_scaler_info.output_width == 2560 && s.frame.compatibility_flags.test(FeatureCompatibility::UnsupportedDrawAuto));
  bool mutated = false;
  stats.update(2, [&](auto &) { mutated = true; });
  stats.cpuComplete(2); stats.encodingComplete(2);
  assert(!mutated && stats.snapshot(3).sample_count == 3);
  assert(s.frame.command_buffer_count == 9); // immutable copied snapshot
  assert(stats.snapshot(0).sample_count == 0);
  assert(stats.snapshot(1).sample_count == 1);

  FrameStatisticsContainer wide;
  for (uint64_t i = 0; i < 15; ++i) {
    wide.update(i, [](auto &s) {
      s.command_buffer_count = std::numeric_limits<uint32_t>::max();
      s.commit_interval = clock::duration(std::numeric_limits<clock::duration::rep>::max());
    });
    complete(wide, i);
  }
  auto w = wide.snapshot(15);
  assert(w.average.command_buffer_count == std::numeric_limits<uint32_t>::max());
  assert(w.average.commit_interval.count() == std::numeric_limits<clock::duration::rep>::max());
}

static void out_of_order() {
  FrameStatisticsContainer stats;
  const uint64_t ids[]{0, 16, 32, 48}; // used to alias the same slot
  for (auto id : ids) stats.update(id, [=](auto &s) { s.command_buffer_count = id + 1; });
  complete(stats, 48); complete(stats, 0, false); complete(stats, 32); complete(stats, 16, false);
  auto s = stats.snapshot(49);
  assert(s.sample_count == 4 && s.frame.command_buffer_count == 49);
  assert(s.min.command_buffer_count == 1 && s.max.command_buffer_count == 49 && s.average.command_buffer_count == 25);
}

static void overlapping_boundary_waits() {
  FrameStatisticsContainer stats;
  stats.update(0, [](auto &s) { s.command_buffer_count = 1; });
  stats.encodingComplete(0); // Present 0 still waits on CPU side
  stats.update(1, [](auto &s) { s.command_buffer_count = 2; s.present_latency_interval = clock::duration(11); });
  complete(stats, 1);
  assert(stats.snapshot(2).sample_count == 1);
  // Earlier boundary returns after the next Present has already finalized.
  stats.update(0, [](auto &s) { s.present_latency_interval = clock::duration(99); });
  stats.cpuComplete(0);
  auto s = stats.snapshot(2);
  assert(s.sample_count == 2 && s.frame.present_latency_interval.count() == 11);
  assert(s.average.present_latency_interval.count() == 55);
  assert(stats.snapshot(1).frame.present_latency_interval.count() == 99);
}

static void delayed_encoder(bool delayed) {
  constexpr uint64_t total = 512;
  FrameStatisticsContainer stats;
  std::atomic<uint64_t> submitted{0};
  std::atomic<bool> start_encoder{false}, finished{false}, observed{false};
  std::thread cpu([&] {
    for (uint64_t f = 0; f < total; ++f) {
      stats.update(f, [=](auto &s) { s.command_buffer_count = f + 1; });
      submitted.store(f + 1, std::memory_order_release);
      if (!delayed) std::this_thread::yield();
      stats.update(f, [=](auto &s) { s.sync_count = (f + 1) * 2; });
      stats.cpuComplete(f);
    }
  });
  std::thread encoder([&] {
    while (!start_encoder.load(std::memory_order_acquire)) std::this_thread::yield();
    for (uint64_t f = 0; f < total; ++f) {
      while (submitted.load(std::memory_order_acquire) <= f) std::this_thread::yield();
      // Several chunks update the same frame before its final marker.
      for (unsigned chunk = 0; chunk < 3; ++chunk)
        stats.update(f, [=](auto &s) { s.render_pass_count += f + 1; });
      stats.update(f, [=](auto &s) { s.event_stall = (f + 1) * 4; s.commit_interval = clock::duration((f + 1) * 5); });
      stats.encodingComplete(f);
      if (f == 0)
        while (!observed.load(std::memory_order_acquire)) std::this_thread::yield();
      if (f % 8 == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    finished.store(true, std::memory_order_release);
  });
  if (delayed) {
    cpu.join(); // CPU deliberately laps the 16-slot ring 32 times before encoding
    assert(submitted.load() == total && stats.snapshot(total).sample_count == 0);
  }
  start_encoder.store(true, std::memory_order_release);
  size_t observations = 0;
  while (!finished.load(std::memory_order_acquire)) {
    auto s = stats.snapshot(total);
    if (!s.sample_count) continue;
    ++observations;
    observed.store(true, std::memory_order_release);
    assert(s.sample_count <= 15);
    assert(s.frame.sync_count == s.frame.command_buffer_count * 2);
    assert(s.frame.render_pass_count == s.frame.command_buffer_count * 3);
    assert(s.frame.event_stall == s.frame.command_buffer_count * 4);
    assert(s.min.render_pass_count == s.min.command_buffer_count * 3);
    assert(s.max.render_pass_count == s.max.command_buffer_count * 3);
    assert(s.frame.commit_interval.count() == s.frame.command_buffer_count * 5);
  }
  if (!delayed) cpu.join();
  encoder.join();
  auto s = stats.snapshot(total);
  assert(observations && s.sample_count == 15 && s.frame.command_buffer_count == total);
  assert(s.min.command_buffer_count == total - 14 && s.max.command_buffer_count == total && s.average.command_buffer_count == total - 7);
  assert(s.average.render_pass_count == (total - 7) * 3);
}
int main() { arithmetic(); out_of_order(); overlapping_boundary_waits(); delayed_encoder(true); delayed_encoder(false); std::cout << "statistics oracle + delayed encoder/ring wrap: PASS\n"; }
