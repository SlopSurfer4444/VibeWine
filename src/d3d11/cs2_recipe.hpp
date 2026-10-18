#pragma once
#include "cs2_pipeline_archive.hpp"
#include <cstring>
#include <cstddef>
namespace dxmt::cs2recipe {
struct Ingredient {
  Sha1Digest source{},variant{},cache_key{};
  char function[128]{};
  uint32_t metal_version=0;
};
static_assert(sizeof(Ingredient)==192);
struct Recipe {
  char magic[8]{};
  uint32_t version=1,info_size=sizeof(WMTRenderPipelineInfo);
  WMTRenderPipelineInfo info{};
  Ingredient vertex{},pixel{};
};
inline Recipe make(WMTRenderPipelineInfo info,const Ingredient &vs,const Ingredient &ps) {
  Recipe r{};std::memcpy(r.magic,"CS2RCP01",8);
  info.vertex_function=0;info.fragment_function=0;info.binary_archive_for_serialization=0;
  info.binary_archives_for_lookup.set(nullptr);info.num_binary_archives_for_lookup=0;info.fail_on_binary_archive_miss=false;
  std::memset(info.padding,0,sizeof(info.padding));r.info=info;r.vertex=vs;r.pixel=ps;return r;
}
inline const std::string &directory() {
  static const std::string dir=env::getEnvVar("DXMT_CS2_RECIPE_DIR");
  return dir;
}
inline bool enabled(const Recipe &r) { return !directory().empty() && r.magic[0]=='C'; }
inline void save(const Recipe &r) {
  const auto &dir=directory();
  static std::atomic<unsigned> attempts{0};
  if(!enabled(r)||attempts.fetch_add(1)>=4096)return;
  auto hash=Sha1HashState::compute(&r,sizeof(r)).string();auto path=cs2archive::winpath(dir+"/"+hash+".recipe");
  if(cs2archive::size(path)==sizeof(r))return;
  auto temp=path+"."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetCurrentThreadId())+".tmp";
  HANDLE f=CreateFileA(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;
  DWORD written=0;bool ok=WriteFile(f,&r,sizeof(r),&written,nullptr)&&written==sizeof(r);CloseHandle(f);
  if(!ok||!MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileA(temp.c_str());
}
}
