#pragma once
#include "Metal.hpp"
#include "airconv_public.h"
#include "cs2_trace.hpp"
#include "sha1/sha1_util.hpp"
#include "util_env.hpp"
#include "log/log.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <cstdio>

namespace dxmt::cs2archive {
// Experimental graphics PSO cache only. Archives are local to this Mac/compiler compatibility version.
// Each PSO has its own archive: unrelated compilations never share a mutable
// Metal archive or hold a global lock across compilation.
constexpr uint64_t Budget=256ULL*1024*1024, MaxEntry=8ULL*1024*1024;
inline std::string winpath(const std::string &p) {
  std::string out="Z:"+p; for(auto &c:out)if(c=='/')c='\\'; return out;
}
inline uint64_t size(const std::string &p) {
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if(!GetFileAttributesExA(p.c_str(),GetFileExInfoStandard,&d) || (d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return 0;
  return (uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;
}
struct Store {
  std::string directory;
  std::mutex publish_mutex;
  uint64_t bytes=0;
  unsigned files=0;
  std::atomic<unsigned> logs{0};
  Store() {
    auto exe=env::getExeBaseName();
    if(exe!="cs2" && exe!="compile-probe" && exe!="prepare-pipelines")return;
    directory=env::getEnvVar("DXMT_CS2_PIPELINE_CACHE");
    if(directory.empty()||directory[0]!='/'){directory.clear();return;}
    WIN32_FIND_DATAA data{};
    HANDLE h=FindFirstFileA(winpath(directory+"/*.metalarc").c_str(),&data);
    if(h!=INVALID_HANDLE_VALUE){do {
      if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
        bytes+=(uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow;files++;
      }
    }while(FindNextFileA(h,&data));FindClose(h);}
  }
  void report(const char *kind,const std::string &key) {
    if(logs.fetch_add(1)<128)std::fprintf(stderr,"CS2PSO %s %s\n",kind,key.c_str());
  }
};
inline Store& store(){static auto s=new Store;return *s;}

inline Sha1Digest key(WMT::Device device,const WMTRenderPipelineInfo &i,
                       const Sha1Digest &vs,const Sha1Digest &ps) {
  Sha1HashState h;
  constexpr char archive_namespace[] = "cs2-pso-archive-v2";
  h.update(archive_namespace, sizeof(archive_namespace) - 1);
  // Shader source/variant hashes do not change when AIRCONV output changes.
  // Match the compiler-owned compatibility version used by the shader cache.
  constexpr uint32_t compiler_version = AIRCONV_VERSION;
  h.update(compiler_version);
  h.update(vs);h.update(ps);
  h.update(device.registryID());
  uint64_t major=0,minor=0,patch=0;WMTGetOSVersion(&major,&minor,&patch);
  h.update(major);h.update(minor);h.update(patch);
  // Explicit fields: never hash padding, pointers, or ephemeral Metal handles.
  for(auto &c:i.colors){h.update(c.pixel_format);h.update(c.alpha_blend_operation);h.update(c.rgb_blend_operation);
    h.update(c.src_rgb_blend_factor);h.update(c.dst_rgb_blend_factor);h.update(c.src_alpha_blend_factor);
    h.update(c.dst_alpha_blend_factor);h.update(c.write_mask);h.update(c.blending_enabled);}
  h.update(i.alpha_to_coverage_enabled);h.update(i.logic_operation_enabled);h.update(i.logic_operation);
  h.update(i.rasterization_enabled);h.update(i.raster_sample_count);h.update(i.depth_pixel_format);h.update(i.stencil_pixel_format);
  h.update(i.immutable_vertex_buffers);h.update(i.immutable_fragment_buffers);h.update(i.input_primitive_topology);
  h.update(i.tessellation_partition_mode);h.update(i.max_tessellation_factor);h.update(i.tessellation_output_winding_order);h.update(i.tessellation_factor_step);
  return h.final();
}
struct Pending {
  WMT::Reference<WMT::BinaryArchive> archive;
  std::string path;
  // Called by the compilation worker AFTER ready/notify, never the render
  // thread. The scheduler is joined before pipelines are destroyed.
  void save() {
    if(!archive || path.empty())return;
    auto held=std::move(archive);auto &s=store();
    auto temp=path+"."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetCurrentThreadId())+".tmp";
    WMT::Reference<WMT::Error> error;
    {cs2trace::Scope timing{.type=19};held.serialize(temp.c_str(),error);}
    auto wtemp=winpath(temp),wpath=winpath(path);auto length=size(wtemp);
    bool saved=false;
    if(!error && length && length<=MaxEntry){
      std::lock_guard<std::mutex> lock(s.publish_mutex);
      uint64_t old=size(wpath);
      if(s.bytes>=old && s.bytes-old+length<=Budget && (old||s.files<4096)){
        saved=MoveFileExA(wtemp.c_str(),wpath.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
        if(saved){s.bytes=s.bytes-old+length;if(!old)s.files++;}
      }
    }
    if(!saved)DeleteFileA(wtemp.c_str());
    s.report(saved?"saved":"save-skipped",path.substr(path.find_last_of('/')+1));
    path.clear();
  }
};
inline WMT::Reference<WMT::RenderPipelineState> create(
    WMT::Device device,WMTRenderPipelineInfo info,const Sha1Digest &vs,const Sha1Digest &ps,
    WMT::Error &error,Pending &pending,uint64_t object) {
  auto &s=store();
  if(s.directory.empty())return device.newRenderPipelineState(info,error);
  auto digest=key(device,info,vs,ps).string();auto path=s.directory+"/"+digest+".metalarc";
  auto gen=cs2trace::generation(); auto began=cs2trace::now();
  auto length=size(winpath(path));
  if(length && length<=MaxEntry){
    WMT::Reference<WMT::Error> load_error;
    auto archive=device.newBinaryArchive(path.c_str(),load_error);
    if(archive && !load_error){
      obj_handle_t handle=archive.handle;
      info.binary_archives_for_lookup.set(&handle);info.num_binary_archives_for_lookup=1;
      info.fail_on_binary_archive_miss=true;
      WMT::Reference<WMT::Error> hit_error;
      auto hit=device.newRenderPipelineState(info,hit_error);
      if(hit && !hit_error){
        cs2trace::record(gen,17,began,cs2trace::now(),object,1);s.report("hit",digest);return hit;
      }
    }
  }
  cs2trace::record(gen,17,began,cs2trace::now(),object,2);s.report("miss",digest);
  info.binary_archives_for_lookup.set(nullptr);info.num_binary_archives_for_lookup=0;info.fail_on_binary_archive_miss=false;
  bool can_save;
  {std::lock_guard<std::mutex> lock(s.publish_mutex);can_save=s.bytes<Budget && s.files<4096;}
  WMT::Reference<WMT::Error> archive_error;
  WMT::Reference<WMT::BinaryArchive> fresh;
  if(can_save)fresh=device.newBinaryArchive(nullptr,archive_error);
  if(fresh && !archive_error)info.binary_archive_for_serialization=fresh.handle;
  auto result=device.newRenderPipelineState(info,error);
  if(!result && info.binary_archive_for_serialization){
    // An optional cache must never prevent normal pipeline creation.
    if(error){error.release();error.handle=0;}
    info.binary_archive_for_serialization=0;
    result=device.newRenderPipelineState(info,error);fresh={};
  }
  if(result && !error && fresh){pending.archive=std::move(fresh);pending.path=path;}
  return result;
}
} // namespace dxmt::cs2archive
