#!/usr/bin/env python3
"""Actual-header bounded diagnostic contract, with only the platform mutex stubbed."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="vibewine-idle-diagnostics-") as directory:
    work = Path(directory)
    (work / "thread.hpp").write_text(
        "#pragma once\n#include <mutex>\nnamespace dxmt {using mutex=std::mutex;}\n")
    source = '#include "' + str(ROOT / 'src/dxmt/dxmt_idle_diagnostics.hpp') + '"\n'
    source += r'''
#include <cassert>
#include <cstring>
int main() {
  using namespace dxmt;
  char line[1024]{};
  IdleRetirementDomain::Sample sample{};
  IdleRetirementDiagnostics off(false), on(true);
  assert(!off.record(1, sample, 1, 2, line) && !off.enabled());
  assert(on.record(1, sample, 1, 2, line));
  assert(strstr(line,"VIBE_IDLE_START pid=1 queue=2 enabled=1 time_ns=1"));
  assert(!strstr(line,"sampled="));
  sample.scanned_resources=3; sample.registered_resources=19; sample.excluded_resources=2;
  sample.completed_idle={8,99,1}; sample.unclassified={7,0,7};
  sample.released_count=5; sample.released_bytes=512;
  assert(!on.record(2, sample, 1, 2, line));
  assert(on.record(10000000001ull, sample, 1, 2, line));
  assert(strstr(line,"sampled=3 registered=19 excluded=2"));
  assert(strstr(line,"released_window=10/1024"));
  assert(strstr(line,"idle=8/99/1") && strstr(line,"unclassified=7/0/7"));
  for(unsigned i=2; i<=63; ++i) {
    assert(on.record(1+10000000000ull*i, sample, 1, 2, line));
    assert(strlen(line)<=1012);
  }
  assert(!on.enabled() && !on.record(10000000000000ull, sample, 1, 2, line));
  // A zero clock epoch still acknowledges once and preserves the rate bound.
  IdleRetirementDiagnostics zero(true);
  assert(zero.record(0, sample, 1, 2, line));
  assert(!zero.record(0, sample, 1, 2, line));
  assert(!zero.record(9999999999ull, sample, 1, 2, line));
  assert(zero.record(10000000000ull, sample, 1, 2, line));
  assert(!zero.record(1, sample, 1, 2, line));
  IdleRetirementDiagnostics worst(true);
  assert(worst.record(1, sample, ~0ull, ~0ull, line));
  sample.current=sample.inflight=sample.completed_idle=sample.unclassified={~0ull,~0ull,~0ull};
  sample.registered_resources=sample.scanned_resources=sample.excluded_resources=~0ull;
  sample.skipped_busy=sample.coherent_seq=~0ull;
  assert(worst.record(10000000001ull, sample, ~0ull, ~0ull, line));
  assert(strlen(line)<=1012);
}
'''
    (work / "fixture.cpp").write_text(source)
    subprocess.run(["clang++", "-std=c++20", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(work),
                    "-I", str(ROOT / "src/winemetal"), str(work / "fixture.cpp"),
                    "-o", str(work / "fixture")], check=True, timeout=60)
    subprocess.run([str(work / "fixture")], check=True, timeout=10)
print("PASS idle diagnostic enable acknowledgement/rate/coverage/64-record/1KiB/unknown-tail limits")
