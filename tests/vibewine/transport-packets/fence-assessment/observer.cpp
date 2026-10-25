// CPU adapter only; no Metal messages and no real Wine transition.
#include "observer.hpp"
#include <cassert>
Observation observed;
static bool capture_events;
void reset_observer(bool capture) {
  observed.calls = observed.commands = observed.digest = 0;
  observed.events.clear();
  capture_events = capture;
}
void observe_unix_call(unsigned opcode, unixcall_generic_obj_cmd_noret *params) {
  assert(opcode == 38);
  ++observed.calls;
  if (!params->encoder) return;
  const auto *node = static_cast<const wmtcmd_base *>(params->cmd_head.get());
  unsigned visited = 0;
  for (; node; node = static_cast<const wmtcmd_base *>(node->next.ptr)) {
    assert(++visited <= 8);
    assert(node->type == WMTRenderCommandWaitForFence || node->type == WMTRenderCommandUpdateFence);
    const auto &body = *reinterpret_cast<const wmtcmd_render_fence_op *>(node);
    ++observed.commands;
    observed.digest = (observed.digest * 1099511628211ull) ^ body.type ^ (body.fence << 7) ^ body.stages;
    if (capture_events) observed.events.push_back({body.type, body.fence, body.stages});
  }
}
