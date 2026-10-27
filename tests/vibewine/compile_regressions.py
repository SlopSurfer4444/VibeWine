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
            "-I", str(source / "src/airconv"),
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
# Exercise each actual consumer with different compatibility generations. The
# shared header is compiled as-is; only the value visible to consumer extracts
# is varied. Both database endpoints must pass the shader table version intact.
shader_version = next(line for line in cache.splitlines()
                      if line.startswith('constexpr ') and 'kDXMTShaderCacheVersion =' in line)
cache_cpp = (source / 'src/dxmt/dxmt_shader_cache.cpp').read_text()
assert 'Cache_alloc_init_checked(path.c_str(), kDXMTShaderCacheVersion, writer, &status)' in cache_cpp
assert 'Reference<WMT::CacheWriter>(initialize(1))' in cache_cpp
assert 'Reference<WMT::CacheReader>(initialize(0))' in cache_cpp
backend = (source / 'src/winemetal/unix/cache.c').read_text()
assert 'version:(uint64_t)version' in backend
assert '@"cache_%llu", version' in backend
compat_header = source / 'src/util/compiler_cache_compat.hpp'
variants = ''
for name, version, epoch in [('current', 'AIRCONV_VERSION', 'kVibeWineCompilerCacheEpoch'),
                             ('next_local', 'AIRCONV_VERSION', 'kVibeWineCompilerCacheEpoch + 1'),
                             ('next_upstream', 'AIRCONV_VERSION + 1', 'kVibeWineCompilerCacheEpoch'),
                             ('wide', '0xffffffffu', '0xffffffffu')]:
    variants += ('namespace ' + name + ' {\n'
                 'constexpr uint64_t kDXMTCompilerCacheCompatibility = '
                 'dxmt::makeCompilerCacheCompatibility(' + version + ', ' + epoch + ');\n'
                 + shader_version + '\n'
                 'std::vector<unsigned char> archive() {\n' + namespace_code
                 + '  return h.bytes;\n}\n}\n')
compile_run('compiler_cache_compatibility', r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>
#include "''' + str(compat_header) + r'''"
using dxmt::kVibeWineCompilerCacheEpoch;
struct Sha1HashState {
  std::vector<unsigned char> bytes;
  void update(const void *p, size_t n) {
    auto b = static_cast<const unsigned char *>(p);
    bytes.insert(bytes.end(), b, b + n);
  }
  template<class T> void update(const T &x) { update(&x, sizeof(x)); }
};
''' + variants + r'''
uint64_t serialized_version(const std::vector<unsigned char> &bytes) {
  constexpr size_t prefix_size = sizeof("cs2-pso-archive-v2") - 1;
  assert(bytes.size() == prefix_size + sizeof(uint64_t));
  uint64_t version;
  std::memcpy(&version, bytes.data() + prefix_size, sizeof(version));
  return version;
}
int main() {
  static_assert(std::is_same_v<decltype(current::kDXMTShaderCacheVersion), const uint64_t>);
  static_assert(sizeof(current::kDXMTShaderCacheVersion) == 8);
  static_assert(kVibeWineCompilerCacheEpoch == 4);
  static_assert(current::kDXMTShaderCacheVersion != dxmt::makeCompilerCacheCompatibility(AIRCONV_VERSION, 2));
  static_assert(current::kDXMTShaderCacheVersion != dxmt::makeCompilerCacheCompatibility(AIRCONV_VERSION, 3));
  static_assert(AIRCONV_VERSION == 26);
  static_assert(dxmt::kDXMTCompilerCacheCompatibility == current::kDXMTShaderCacheVersion);
  static_assert((current::kDXMTShaderCacheVersion >> 32) == AIRCONV_VERSION);
  static_assert(uint32_t(current::kDXMTShaderCacheVersion) == kVibeWineCompilerCacheEpoch);
  static_assert(wide::kDXMTShaderCacheVersion == UINT64_MAX);
  static_assert(dxmt::makeCompilerCacheCompatibility(0x80000000u, 0) == (uint64_t(1) << 63));
  static_assert(dxmt::makeCompilerCacheCompatibility(0, 0xffffffffu) == 0xffffffffULL);
  assert(current::kDXMTShaderCacheVersion != next_local::kDXMTShaderCacheVersion);
  assert(current::kDXMTShaderCacheVersion != next_upstream::kDXMTShaderCacheVersion);
  assert(current::kDXMTShaderCacheVersion != AIRCONV_VERSION);
  assert(current::archive() != next_local::archive());
  assert(current::archive() != next_upstream::archive());
  assert(serialized_version(current::archive()) == current::kDXMTShaderCacheVersion);
  assert(serialized_version(next_local::archive()) == next_local::kDXMTShaderCacheVersion);
  assert(serialized_version(next_upstream::archive()) == next_upstream::kDXMTShaderCacheVersion);
  assert(serialized_version(wide::archive()) == wide::kDXMTShaderCacheVersion);
  Sha1HashState legacy;
  legacy.update("cs2-pso-archive-v2", sizeof("cs2-pso-archive-v2") - 1);
  legacy.update(uint32_t(AIRCONV_VERSION));
  assert(current::archive() != legacy.bytes);
  Sha1HashState old;
  old.update("cs2-pso-archive-v1", 18);
  assert(current::archive() != old.bytes);
  assert(current::archive() == current::archive());
  std::cout << "PASS shared shader-table/PSO 64-bit key; local/upstream revisions; legacy separation; no compiler ABI change\n";
}
''')

compile_run('compiler_cache_cleanup_generation', r'''
#define DXMT_SHADER_CLEANUP 1
#include "''' + str(compat_header) + r'''"
static_assert(AIRCONV_VERSION == 26);
static_assert(dxmt::kVibeWineCompilerCacheEpoch == 5);
static_assert(dxmt::kDXMTCompilerCacheCompatibility != dxmt::makeCompilerCacheCompatibility(AIRCONV_VERSION, 2));
static_assert(dxmt::kDXMTCompilerCacheCompatibility != dxmt::makeCompilerCacheCompatibility(AIRCONV_VERSION, 3));
static_assert(dxmt::kDXMTCompilerCacheCompatibility != dxmt::makeCompilerCacheCompatibility(AIRCONV_VERSION, 4));
int main() {}
''')
