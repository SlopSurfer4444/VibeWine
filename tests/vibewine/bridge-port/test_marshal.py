#!/usr/bin/env python3
"""CPU execution of actual Metal marshal bodies with bounded loops and sanitizer.
Reuse: native-acquire current/tests/task-marshal; expected-output edge cases now
cover guarded arithmetic and current buffer binding constants. No GPU execution.
"""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile,os
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
OUT=REPO/'build/bridge-port-marshal'
OUT.mkdir(parents=True,exist_ok=True)

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
  (void)argc; (void)argv; bool baseline = false;
  try {
    for (const std::string kind : {"gs", "ts"}) {
      const bool gs = kind == "gs";
      Spec a = gs ? Spec{65,2,32,1,6,0,{3,2,1}} : Spec{39,2,3,4,8,0,{4,2,1}};
      Spec b = gs ? Spec{129,3,32,1,14,0,{0,0,0}} : Spec{60,3,3,4,14,0,{0,0,0}};
      Spec c = gs ? Spec{32,4,32,1,4,1,{1,4,1}} : Spec{36,4,3,4,12,1,{3,4,1}};
      Spec poison = gs ? Spec{99,7,1,1,1000,1,{99,7,1}} : Spec{99,7,1,1,1000,1,{99,7,1}};
      run(kind, "zero_vertices", {{0,2,1,1,100,1,{0,0,0}}}, false);
      run(kind, "zero_instances", {{1,0,1,1,100,1,{0,0,0}}}, false);
      run(kind, "zero_divisor", {{1,1,0,1,100,1,{0,0,0}}}, false);
      if (!gs) {
        run(kind, "zero_group", {{3,1,3,0,100,1,{0,0,0}}}, false);
        run(kind, "incomplete_patch", {{2,1,3,1,100,1,{0,0,0}}}, false);
      }
      run(kind, "overflow_cap_exceeded", {{0xffffffffu,2,1,1,0xfffffffful,1,{0,0,0}}}, false);
      run(kind, "wide_cap_exact", {{0xffffffffu,2,1,1,0x1fffffffeul,1,{0xffffffffu,2,1}}}, false);
      // A 32-bit count product wraps to zero here despite valid dispatch.
      run(kind, "input_product_wrap_zero", {{65536u,65536u,1,1,0x100000000ul,1,{65536u,65536u,1}}}, false);
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
    source=REPO/'src/dxmt/dxmt_command.metal'
    raw=source.read_text()
    # Source extraction keeps the current binding attributes until the shim
    # strips attributes; their presence is checked independently.
    assert raw.count('tasks [[buffer(kCustomBufferArgumentIndex0)]]') == 2
    block=extract(raw)
    controls={"candidate":block,
      "unguarded_division_control":block.replace('task.control_point_count\n        ? task.draw_arguments.x / (uint)task.control_point_count : 0','task.draw_arguments.x / (uint)task.control_point_count'),
      "narrow_product_control":block.replace('(ulong)x * (ulong)task.draw_arguments.y','x * task.draw_arguments.y')}
    results=[]
    with tempfile.TemporaryDirectory(prefix='marshal-port-') as scratch:
      for name,body in controls.items():
        cpp=Path(scratch)/(name+'.cpp');binary=cpp.with_suffix('')
        cpp.write_text(PRELUDE+body+HARNESS)
        subprocess.run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-O1',str(cpp),'-o',str(binary)],check=True,capture_output=True,text=True,timeout=60)
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        assert (run.returncode==0)==(name=='candidate'),(name,run.stdout,run.stderr)
        if name=='unguarded_division_control': assert 'division by zero' in run.stderr
        if name=='narrow_product_control': assert 'gs:overflow_cap_exceeded: wrong output at task 0' in run.stderr
        results.append(dict(variant=name,returncode=run.returncode,output=run.stdout,stderr=run.stderr))
    receipt=dict(scope='CPU shim of current Metal routines; no GPU/Metal/Wine/game',sha256=hashlib.sha256(source.read_bytes()).hexdigest(),results=results)
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(passed=True,variants=len(results),receipt=str(OUT/'receipt.json'))))
if __name__=='__main__':main()
