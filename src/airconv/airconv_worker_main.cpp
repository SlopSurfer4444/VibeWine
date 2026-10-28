/* SPDX-License-Identifier: MIT */
#include "airconv_worker_protocol.hpp"
#include "airconv_worker_recipe.hpp"
#include <cerrno>
#include <poll.h>
#include <sys/resource.h>
#include <unistd.h>
using namespace dxmt::worker;
static bool exact(int fd,void* p,size_t n,bool output=false) {
  auto* bytes=static_cast<uint8_t*>(p);
  while(n) {
    pollfd f{fd,short(output?POLLOUT:POLLIN),0};
    int ready;do {ready=poll(&f,1,60000);}while(ready<0 && errno==EINTR);
    if(ready<=0)return false;
    auto got=output?write(fd,bytes,n):read(fd,bytes,n);
    if(got<0 && errno==EINTR)continue;
    if(got<=0)return false;bytes+=got;n-=got;
  }
  return true;
}
static std::string diagnostic(sm50_error_t e) {
  if(!e)return "converter failed without diagnostic";
  char s[MaxDiagnostic+1]{};SM50GetErrorMessage(e,s,MaxDiagnostic);SM50FreeError(e);return std::string(s,strnlen(s,MaxDiagnostic));
}
int main() {
  if(!configured())return 78;
  // Parent sets stdio only; no paths, environment overrides or dynamic plugins are accepted.
  rlimit core{0,0};setrlimit(RLIMIT_CORE,&core);
  for(uint32_t count=0;count<MaxRequests;++count) {
    uint8_t h[HeaderSize];if(!exact(0,h,sizeof h))return 0;
    if(!validHeader(h,false)||get32(h+12)!=1||get32(h+40)!=IdentitySize)return 65;
    uint64_t generation=get64(h+16),request=get64(h+24);
    auto dxbcSize=get32(h+32),recipeSize=get32(h+36);
    if(!generation||!request||dxbcSize<32||dxbcSize>MaxDXBC||recipeSize>MaxRecipe)return 65;
    char identity[IdentitySize];if(!exact(0,identity,sizeof identity)||memcmp(identity,DXMT_WORKER_BUILD_ID,IdentitySize))return 66;
    WireBytes dxbc(dxbcSize),recipe(recipeSize);if(!exact(0,dxbc.data(),dxbc.size())||!exact(0,recipe.data(),recipe.size()))return 65;
    WireBytes air,reflection;std::string why;uint32_t status=0;OwnedRecipe owned;
    if(!ordinaryDXBC(dxbc)||!decodeRecipe(recipe,owned,why)) {status=1;if(why.empty())why="unsupported ordinary shader";}
    else {
      sm50_shader_t shader=nullptr;sm50_error_t error=nullptr;MTL_SHADER_REFLECTION refl{};
      if(SM50Initialize(dxbc.data(),dxbc.size(),&shader,&refl,&error)) {status=2;why=diagnostic(error);}
      else if(!shader||refl.NumConstantBuffers>2048||refl.NumArguments>2048) {status=3;why="invalid converter reflection";}
      else {
        std::vector<MTL_SM50_SHADER_ARGUMENT> cb(refl.NumConstantBuffers),args(refl.NumArguments);
        MTL_SM50_SHADER_ARGUMENT dummy{};SM50GetArgumentsInfo(shader,cb.empty()?&dummy:cb.data(),args.empty()?&dummy:args.data());
        sm50_bitcode_t compiled=nullptr;
        if(SM50Compile(shader,owned.args(),owned.function.c_str(),&compiled,&error)) {status=2;why=diagnostic(error);}
        else if(!compiled) {status=3;why="null converter bitcode";}
        else {
          SM50_COMPILED_BITCODE raw{};SM50GetCompiledBitcode(compiled,&raw);
          if(!raw.Data||raw.Size<16||raw.Size>MaxAIR||memcmp(raw.Data,"MTLB",4)) {status=3;why="invalid converter AIR";}
          else {auto p=static_cast<const uint8_t*>(raw.Data);air.assign(p,p+raw.Size);reflection=serializeReflection(refl,cb,args);}
          SM50DestroyBitcode(compiled);
        }
      }
      if(shader)SM50Destroy(shader);
    }
    if(reflection.size()>MaxReflection){status=3;why="converter reflection byte cap";}
    if(status){air.clear();reflection.clear();}
    why.resize(std::min<size_t>(why.size(),MaxDiagnostic));
    auto response=header(true,status,generation,request,air.size(),reflection.size(),why.size());
    response.insert(response.end(),air.begin(),air.end());response.insert(response.end(),reflection.begin(),reflection.end());response.insert(response.end(),why.begin(),why.end());
    if(!exact(1,response.data(),response.size(),true))return 74;
  }
  return 0;
}
