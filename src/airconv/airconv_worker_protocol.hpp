/* SPDX-License-Identifier: MIT
 * Private native-worker protocol. Never transfer compiler pointers or struct padding. */
#pragma once
#include "airconv_public.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifndef DXMT_WORKER_BUILD_ID
#define DXMT_WORKER_BUILD_ID "unconfigured"
#endif
namespace dxmt::worker {
using WireBytes = std::vector<uint8_t>;
constexpr uint32_t Protocol = 2, MaxDXBC = 1024 * 1024, MaxRecipe = 2 * 1024 * 1024 + 128 * 1024;
constexpr uint32_t MaxAIR = 4 * 1024 * 1024, MaxReflection = 65536, MaxDiagnostic = 4096;
constexpr size_t HeaderSize = 48, IdentitySize = 64;
constexpr uint32_t MaxRequests = 256;
inline uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
inline uint64_t get64(const uint8_t* p) { return get32(p) | (uint64_t(get32(p+4))<<32); }
inline void put32(WireBytes& b, uint32_t x) { for (unsigned i=0;i<4;++i) b.push_back(x>>(8*i)); }
inline void put64(WireBytes& b, uint64_t x) { put32(b,x); put32(b,x>>32); }
inline bool configured() { return sizeof(DXMT_WORKER_BUILD_ID)==IdentitySize+1; }
inline WireBytes header(bool response, uint32_t status, uint64_t generation, uint64_t request,
                        uint32_t a, uint32_t b, uint32_t c) {
  const char* magic = response ? "DXMTRP02" : "DXMTRQ02";
  WireBytes out(magic,magic+8);put32(out,Protocol);put32(out,status);
  put64(out,generation);put64(out,request);put32(out,a);put32(out,b);put32(out,c);put32(out,0);return out;
}
inline bool validHeader(const uint8_t* p, bool response) {
  return !memcmp(p,response?"DXMTRP02":"DXMTRQ02",8) && get32(p+8)==Protocol && !get32(p+44);
}
// Ordinary stages only. Validate all container extents before dereferencing tokens.
inline bool ordinaryDXBC(const WireBytes& b) {
  if (b.size()<32 || b.size()>MaxDXBC || memcmp(b.data(),"DXBC",4) || get32(b.data()+24)!=b.size()) return false;
  auto count=get32(b.data()+28); if (count>(b.size()-32)/4) return false;
  bool found=false;
  for (uint32_t i=0;i<count;++i) {
    auto offset=get32(b.data()+32+4*i);
    if (offset>b.size()-8) return false;
    auto size=get32(b.data()+offset+4); if (size>b.size()-offset-8) return false;
    if (memcmp(b.data()+offset,"SHDR",4) && memcmp(b.data()+offset,"SHEX",4)) continue;
    if (found || size<8 || size%4) return false;
    auto words=get32(b.data()+offset+12), stage=get32(b.data()+offset+8)>>16;
    if (words<2 || words>size/4 || (stage!=0 && stage!=1 && stage!=5)) return false;
    found=true;
  }
  return found;
}
inline WireBytes serializeReflection(const MTL_SHADER_REFLECTION& r,
    const std::vector<MTL_SM50_SHADER_ARGUMENT>& cb, const std::vector<MTL_SM50_SHADER_ARGUMENT>& args) {
  WireBytes b;put32(b,1);put32(b,r.ConstanttBufferTableBindIndex);put32(b,r.ArgumentBufferBindIndex);
  put32(b,r.NumConstantBuffers);put32(b,r.NumArguments);
  uint32_t words[3]{};memcpy(words,&r.ThreadgroupSize,sizeof words);for (auto w:words) put32(b,w);
  put32(b,r.ConstantBufferSlotMask);put32(b,r.SamplerSlotMask);put64(b,r.UAVSlotMask);put64(b,r.SRVSlotMaskHi);put64(b,r.SRVSlotMaskLo);
  put32(b,r.NumOutputElement);put32(b,r.ThreadsPerPatch);put32(b,r.ArgumentTableQwords);
  for (auto* list:{&cb,&args}) for (auto& a:*list) {put32(b,uint32_t(a.Type));put32(b,a.SM50BindingSlot);put32(b,a.Flags);put32(b,a.StructurePtrOffset);}
  return b;
}
}
