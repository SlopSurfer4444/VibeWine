#!/usr/bin/env python3
"""Exercise the actual ClearRTV cache block with fake Metal creation and clock.

Only the clock identifier is substituted in extracted code. Pipeline metadata
and logging use lightweight stubs; no game, GPU, or installed runtime is used.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / 'src/dxmt/dxmt_command_clear.hpp').read_text()
start = header.index('    if (!pso_cache_.contains(key.u64)) {')
end = header.index('\n    auto width = texture->width(view);', start)
block = header[start:end].replace('std::chrono::steady_clock', 'TestClock')
code = r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <iostream>
struct TestClock {
  using time_point = std::chrono::steady_clock::time_point;
  static inline time_point current{};
  static inline int reads = 0;
  static time_point now() { ++reads; return current; }
};
struct Error {
  explicit operator bool() const { return false; }
  Error description() const { return {}; }
  const char *getUTF8String() const { return "error"; }
};
struct WMTRenderPipelineInfo {
  int raster_sample_count, vertex_function, fragment_function;
  int depth_pixel_format, stencil_pixel_format;
  struct { int pixel_format; } colors[1];
  bool rasterization_enabled;
  int input_primitive_topology;
};
constexpr int WMTPixelFormatInvalid=0, WMTPrimitiveTopologyClassTriangle=3;
namespace WMT {
using RenderPipelineState = int;
void InitializeRenderPipelineInfo(WMTRenderPipelineInfo &p) { p = {}; }
}
bool IsIntegerFormat(int) { return false; }
int MTLGetUnsignedIntegerFormat(int f) { return f; }
struct Texture { int sampleCount() { return 1; } };
struct Device {
  int calls=0; bool succeeds=false;
  int newRenderPipelineState(const WMTRenderPipelineInfo &, Error &) {
    ++calls; return succeeds ? 42 : 0;
  }
};
struct Fixture {
  Device device_;
  int logs=0, encodes=0;
  int vs_clear_=1, fs_clear_depth_=2, fs_clear_uint_=3, fs_clear_sint_=4, fs_clear_float_=5;
  std::unordered_map<uint64_t,int> pso_cache_;
  std::unordered_map<uint64_t,TestClock::time_point> pso_retry_after_;
  template<class... T> void log(T&&...) { ++logs; }
  void begin(uint64_t id=1) {
    struct { uint64_t u64; } key{id};
    int format=1, dsv_flag=0;
    Texture t; auto texture=&t; Error err;
#define ERR(...) log(__VA_ARGS__)
''' + block + r'''
#undef ERR
    ++encodes;
  }
};
int main() {
  Fixture f;
  f.begin();
  assert(f.device_.calls==1 && f.logs==1 && f.encodes==0 && f.pso_cache_.empty());
  for(int n=0;n<100;n++) f.begin();
  assert(f.device_.calls==1 && f.logs==1 && f.encodes==0);
  TestClock::current += std::chrono::milliseconds(999); f.begin();
  assert(f.device_.calls==1);
  TestClock::current += std::chrono::milliseconds(1); f.device_.succeeds=true; f.begin();
  assert(f.device_.calls==2 && f.logs==1 && f.encodes==1);
  assert(f.pso_cache_.at(1)==42 && f.pso_retry_after_.empty());
  int reads=TestClock::reads;
  for(int n=0;n<100;n++) f.begin();
  assert(f.device_.calls==2 && f.encodes==101 && TestClock::reads==reads);
  Fixture persistent;
  for(int n=0;n<20;n++) {
    persistent.begin(); persistent.begin();
    TestClock::current += std::chrono::seconds(1);
  }
  assert(persistent.device_.calls==20 && persistent.logs==1);
  assert(persistent.pso_cache_.empty() && persistent.encodes==0);
  // A separate key must not inherit the first key's cooldown.
  persistent.begin(2);
  assert(persistent.device_.calls==21 && persistent.logs==2);
  // A completed recovery removes failure state, allowing a new episode to log.
  f.pso_cache_.clear(); f.device_.succeeds=false; f.begin();
  assert(f.logs==2 && f.pso_cache_.empty() && f.encodes==101);
  std::cout << "PASS transient recovery, cooldown boundary, persistent failures, per-key isolation, cache-hit clock, new episode\n";
}
'''
with tempfile.TemporaryDirectory(prefix='vibewine-clear-retry-') as directory:
    cpp = Path(directory) / 'test.cpp'
    executable = Path(directory) / 'test'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(cpp), '-o', str(executable)], check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=10,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
