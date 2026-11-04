#pragma once
#include "trace_core.hpp"
namespace probe {
inline const char *name(uint32_t type) {
  switch(type){case 30:return "StagingMap CPU fence wait";case 31:return "GetData not-ready Flush";case 32:return "GPUIdle CPU fence wait";case 33:return "Commit reason";default:return "unknown";}
}
inline const char *reason(uint32_t flags) {
  switch(flags){case 0:return "Unknown";case 1:return "Present";case 2:return "StagingMap";case 3:return "GetDataNotReady";case 4:return "ExplicitFlush";case 5:return "GPUIdle";case 6:return "FenceSignal";case 7:return "FenceWait";case 8:return "PassPromotion";default:return "invalid";}
}
inline const char *flagName(uint32_t type,uint32_t flags) {
  if(type==33)return reason(flags);
  if(type==30){switch(flags){case 1:return "READ";case 2:return "WRITE";case 3:return "READ_WRITE";default:return "invalid";}}
  if(type==31){switch(flags){case 0:return "EVENT";case 1:return "OCCLUSION";case 2:return "TIMESTAMP";case 3:return "TIMESTAMP_DISJOINT";case 5:return "OCCLUSION_PREDICATE";default:return "invalid";}}
  if(type==32)return flags==0?"none":"invalid";
  return "unknown";
}
inline bool valid(const trace::Decoded &e) {
  if(e.type<30 || e.type>33)return true;
  if(!e.object || !e.padding0 || e.end<e.start)return false;
  if(std::string(flagName(e.type,e.flags))=="invalid")return false;
  return e.type!=33 || e.start==e.end;
}
} // namespace probe
