#!/usr/bin/env python3
"""Portable regressions extracted from the actual source; no game or GPU required.

Requires Python 3 and clang++ with AddressSanitizer/UndefinedBehaviorSanitizer.
Every executable is bounded to ten seconds and lives in a temporary directory.
"""
from pathlib import Path
import os
import subprocess
import tempfile

source = Path(__file__).resolve().parents[2]

def compile_run(name, code):
    with tempfile.TemporaryDirectory(prefix="vibewine-compile-test-") as directory:
        work = Path(directory)
        cpp, exe = work / "test.cpp", work / "test"
        cpp.write_text(code)
        subprocess.run([
            os.environ.get("CXX", "clang++"), "-std=c++20", "-O1", "-g",
            "-fsanitize=address,undefined", "-ftrivial-auto-var-init=pattern",
            str(cpp), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        print("PASS " + name, flush=True)

cache=(source/'src/dxmt/dxmt_shader_cache.hpp').read_text()
start=cache.index('  template <typename T> class LockProtected')
end=cache.index('\n  static ShaderCache',start)
lock=cache[start:end]
compile_run('lock_move',r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <utility>
namespace dxmt {struct mutex {int locks=0,unlocks=0;bool held=false;void lock(){assert(!held);held=true;locks++;}void unlock(){assert(held);held=false;unlocks++;}};class ShaderCache {public:
'''+lock+r'''
};}
struct Value {int value=0;Value()=default;Value(int v):value(v){}operator int()const{return value;}};
int main(){using G=dxmt::ShaderCache::LockProtected<Value>;dxmt::mutex m;std::unique_ptr<G> destination;
{G a(m,42);destination=std::make_unique<G>(std::move(a));assert(!a);assert(a.object==0);assert(*destination);assert(destination->object==42);}
assert(m.held&&m.unlocks==0);{G c(std::move(*destination));destination.reset();assert(m.held&&m.unlocks==0);assert(c.object==42);}
assert(!m.held&&m.locks==1&&m.unlocks==1);{G empty;G moved(std::move(empty));assert(!moved);}assert(m.unlocks==1);
std::cout<<"PASS moved-from destruction preserves lock; chained/default moves; exactly one unlock\n";}
''')
shader=(source/'src/d3d11/d3d11_shader.cpp').read_text()
start=shader.index('                    ShaderVariantTessellationDomain variant)')
block=shader[start:shader.index('\ntemplate <>',start)]
arg_start=block.index('    SM50_SHADER_GS_PASS_THROUGH_DATA')
arg_end=block.index('    sm50_bitcode_t compile_result',arg_start)
args=block[arg_start:arg_end]
hash_start=block.index('  Sha1HashState h;')
hash_end=block.index('  auto variant_digest',hash_start)
hash_code=block[hash_start:hash_end]
public=(source/'src/airconv/airconv_public.h').resolve()
compile_run('domain_arguments',r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "'''+str(public)+r'''"
struct Handle {uint32_t value=77;uint32_t sha1(){return value;}};
struct Variant {Handle* hull_shader_handle;uint32_t gs_passthrough;bool rasterization_disabled;float max_potential_tess_factor;};
struct Sha1HashState {std::vector<unsigned char> bytes;template<class T>void update(const T&t){auto p=reinterpret_cast<const unsigned char*>(&t);bytes.insert(bytes.end(),p,p+sizeof(t));}};
SM50_SHADER_FLAG getGlobalShaderFlag(){return SM50_SHADER_FLAG_DEFUSE_FMA;}
void check(Variant variant){SM50_SHADER_COMMON_DATA common_value{};common_value.type=SM50_SHADER_COMMON;auto common=&common_value;
'''+args+r'''
auto entry=reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(&gs_passthrough);
assert(entry->type==SM50_SHADER_GS_PASS_THROUGH);assert(entry->next==&pso_tess);
assert(gs_passthrough.DataEncoded==variant.gs_passthrough);assert(gs_passthrough.RasterizationDisabled==variant.rasterization_disabled);
assert(pso_tess.type==SM50_SHADER_PSO_TESSELLATOR);assert(pso_tess.max_potential_tess_factor==variant.max_potential_tess_factor);assert(pso_tess.next==common);assert(common->next==nullptr);
'''+hash_code+r'''
Sha1HashState legacy;legacy.update(getGlobalShaderFlag());legacy.update(variant.hull_shader_handle->sha1());legacy.update(variant.gs_passthrough);legacy.update(variant.rasterization_disabled);legacy.update(variant.max_potential_tess_factor);
assert(h.bytes!=legacy.bytes);assert(h.bytes.size()==legacy.bytes.size()+sizeof(uint32_t));
}
int main(){Handle hull;for(bool raster:{false,true})for(uint32_t pass:{0u,0xffffffffu})for(float factor:{1.f,16.f,64.f})check({&hull,pass,raster,factor});std::cout<<"PASS 12 domain argument chains and domain-only cache serialization revision\n";}
''')

archive = (source / 'src/d3d11/cs2_pipeline_archive.hpp').read_text()
start = archive.index('  Sha1HashState h;')
end = archive.index('  h.update(vs);', start)
namespace_code = archive[start:end]
compile_run('archive_namespace', r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
struct Sha1HashState {
  std::vector<unsigned char> bytes;
  void update(const void *p, size_t n) {
    auto b = static_cast<const unsigned char *>(p);
    bytes.insert(bytes.end(), b, b + n);
  }
  template<class T> void update(const T &x) { update(&x, sizeof(x)); }
};
#define AIRCONV_VERSION 26
std::vector<unsigned char> current() {
''' + namespace_code + r'''
  return h.bytes;
}
#undef AIRCONV_VERSION
#define AIRCONV_VERSION 27
std::vector<unsigned char> next() {
''' + namespace_code + r'''
  return h.bytes;
}
int main() {
  assert(current() != next());
  Sha1HashState old;
  old.update("cs2-pso-archive-v1", 18);
  assert(current() != old.bytes);
  assert(current() == current());
}
''')
