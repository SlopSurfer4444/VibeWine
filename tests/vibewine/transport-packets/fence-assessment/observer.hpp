#pragma once
#include "winemetal_thunks.h"
#include <array>
#include <vector>

using FenceEvent = std::array<uint64_t, 3>;
struct Observation {
  uint64_t calls = 0, commands = 0, digest = 0;
  std::vector<FenceEvent> events;
};
extern Observation observed;
void reset_observer(bool capture);
void observe_unix_call(unsigned opcode, unixcall_generic_obj_cmd_noret *params);
