#!/usr/bin/env python3
"""Exercise extracted DXMT GS/TS marshal code on CPU; never submit GPU work.

The baseline is an expected-failure control. Only the Metal attributes/address
space keywords are removed; a guard is inserted at the loop entry so defective
source terminates safely. Production function bodies otherwise stay verbatim.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_BASELINE = HERE.parents[2] / "renderer/wave7/source/src/dxmt/dxmt_command.metal"
DEFAULT_CANDIDATE = HERE.parents[1] / "d3d11/dxmt_command.metal"

PRELUDE = r'''
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using uint = unsigned int;
using ushort = unsigned short;
using ulong = unsigned long;
struct uint2 { uint x, y; };
struct IterationLimit : std::exception {};
static unsigned iterations = 0, iteration_limit = 0;
static void iteration_guard() {
  if (iterations == iteration_limit) throw IterationLimit{};
  ++iterations;
}
'''

HARNESS = r'''
struct Spec {
  uint vertices, instances, divisor, per_group;
  ulong cap;
  uint end;
  std::array<uint, 3> expected;
};
constexpr std::array<uint, 3> untouched{0xA1B2C3D4u,0x12345678u,0x76543210u};
bool same(const DXMTDispatchArguments& a, const std::array<uint, 3>& b) {
  return a.x == b[0] && a.y == b[1] && a.z == b[2];
}

void run(const std::string& kind, const std::string& name,
         const std::vector<Spec>& specs, bool expect_guard) {
  std::vector<uint2> inputs;
  std::vector<DXMTDispatchArguments> outputs;
  for (const auto& s : specs) {
    inputs.push_back({s.vertices, s.instances});
    outputs.push_back({untouched[0], untouched[1], untouched[2]});
  }
  iterations = 0;
  iteration_limit = static_cast<unsigned>(specs.size() + 2);
  bool guarded = false;
  try {
    if (kind == "gs") {
      std::vector<DXMTGSDispatchMarshal> tasks;
      for (unsigned i=0; i<specs.size(); ++i) {
        const auto& s = specs[i];
        tasks.push_back({inputs[i], outputs[i], s.cap, s.divisor, s.end});
      }
      gs_draw_arguments_marshal(tasks.data());
    } else {
      std::vector<DXMTTSDispatchMarshal> tasks;
      for (unsigned i=0; i<specs.size(); ++i) {
        const auto& s = specs[i];
        tasks.push_back({inputs[i], outputs[i], s.cap,
          static_cast<ushort>(s.divisor), static_cast<ushort>(s.per_group), s.end});
      }
      ts_draw_arguments_marshal(tasks.data());
    }
  } catch (const IterationLimit&) { guarded = true; }
  if (guarded != expect_guard) throw std::runtime_error(kind+":"+name+": wrong termination");
  unsigned expected_iterations = 0;
  if (expect_guard) {
    expected_iterations = iteration_limit;
    if (!same(outputs[0], specs[0].expected)) throw std::runtime_error("baseline first task wrong");
    for (unsigned i=1; i<specs.size(); ++i)
      if (!same(outputs[i], untouched)) throw std::runtime_error("baseline unexpectedly advanced");
  } else {
    bool ended = false;
    for (unsigned i=0; i<specs.size(); ++i) {
      if (!same(outputs[i], ended ? untouched : specs[i].expected))
        throw std::runtime_error(kind+":"+name+": wrong output at task "+std::to_string(i));
      if (!ended) ++expected_iterations;
      ended = ended || specs[i].end;
    }
  }
  if (iterations != expected_iterations) throw std::runtime_error("wrong visit count");
  std::cout << kind << ":" << name << ":" << (guarded ? "guarded_expected_failure" : "passed")
            << ":iterations=" << iterations << ":observed_outputs=";
  for (unsigned i=0; i<outputs.size(); ++i) {
    if (i >= 3 && i != outputs.size()-1) continue;
    const auto& o = outputs[i];
    std::cout << i << "=(" << o.x << "," << o.y << "," << o.z << ");";
  }
  std::cout << "\n";
}

int main(int argc, char** argv) {
  bool baseline = argc == 2 && std::string(argv[1]) == "baseline";
  try {
    for (const std::string kind : {"gs", "ts"}) {
      const bool gs = kind == "gs";
      Spec a = gs ? Spec{65,2,32,1,6,0,{3,2,1}} : Spec{39,2,3,4,8,0,{4,2,1}};
      Spec b = gs ? Spec{129,3,32,1,14,0,{0,0,0}} : Spec{60,3,3,4,14,0,{0,0,0}};
      Spec c = gs ? Spec{32,4,32,1,4,1,{1,4,1}} : Spec{36,4,3,4,12,1,{3,4,1}};
      Spec poison = gs ? Spec{99,7,1,1,1000,1,{99,7,1}} : Spec{99,7,1,1,1000,1,{99,7,1}};
      Spec first = a; first.end = 1;
      run(kind, "sentinel_first_cap_equal", {first, poison}, false);
      first = b; first.end = 1;
      run(kind, "sentinel_first_cap_exceeded", {first, poison}, false);
      run(kind, "three_tasks_cap_equal_exceeded_and_terminator", {a,b,c,poison}, baseline);
      Spec last = b; last.end = 1;
      run(kind, "two_tasks_zero_dispatch_terminator", {a,last,poison}, baseline);
      std::vector<Spec> many;
      for (unsigned i=0; i<64; ++i) {
        Spec s = i%2 ? b : a; s.end = i == 63;
        many.push_back(s);
      }
      many.push_back(poison);
      run(kind, "64_tasks_alternating_cap", many, baseline);
    }
  } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
}
'''


def extract(source):
    begin = source.index("struct DXMTDispatchArguments {")
    end = source.index("struct depth_stencil_out", begin)
    block = source[begin:end]
    if block.count("for(;;) {") != 2:
        raise ValueError("Source shape changed: expected exactly two marshal loops")
    block = re.sub(r"\[\[[^\]]*\]\]", "", block)
    block = re.sub(r"\b(?:constant|device)\s+", "", block)
    block = block.replace("for(;;) {", "for(;;) {\n    iteration_guard();")
    return block


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=pathlib.Path, default=DEFAULT_BASELINE)
    parser.add_argument("--candidate", type=pathlib.Path, default=DEFAULT_CANDIDATE)
    parser.add_argument("--output", type=pathlib.Path, default=HERE / "results.json")
    args = parser.parse_args()
    results = {"evidence_scope": "CPU C++ shim of actual extracted Metal marshal bodies; no GPU execution",
               "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
               "transformations": ["remove Metal attributes", "remove constant/device address-space keywords",
                                   "inject bounded iteration_guard at each loop entry"], "sources": {}}
    with tempfile.TemporaryDirectory(prefix="marshal-", dir=HERE) as work:
        work = pathlib.Path(work)
        for mode, path in (("baseline", args.baseline), ("candidate", args.candidate)):
            raw = path.read_bytes()
            block = extract(raw.decode())
            cpp = work / (mode + ".cpp")
            executable = work / mode
            cpp.write_text(PRELUDE + block + HARNESS)
            command = ["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-O1",
                       str(cpp), "-o", str(executable)]
            subprocess.run(command, check=True, timeout=60, capture_output=True, text=True)
            run = subprocess.run([str(executable), mode], check=True, timeout=10, capture_output=True, text=True)
            results["sources"][mode] = {"path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
                "extracted_transformed_sha256": hashlib.sha256(block.encode()).hexdigest(),
                "compile_options": command[1:-3], "observations": run.stdout.splitlines(), "stderr": run.stderr}
    results["verdict"] = "PASS: candidate dispatch/termination fixtures pass; baseline multi-task controls hit bounded guard"
    args.output.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
