# Game-policy map

This directory documents title-specific behavior and test policies. It is not a
profile loader. Engine gates remain in the component that implements them;
private prefixes, saves and launcher configuration are not public profiles.
The map below was checked against the current pinned DXMT source on 24 October 2026.

| Behavior | Owning source / activation | Default / verification |
| --- | --- | --- |
| DS3 resolution-list cap | [`config.cpp`](../components/dxmt/src/util/config/config.cpp) selects case-insensitive `\\DarkSoulsIII.exe$`; [`dxgi_output.cpp`](../components/dxmt/src/dxgi/dxgi_output.cpp) applies the option | General `dxgi.limitDisplayModeResolutions` default is false; the DS3 app profile selects true. Explicit user configuration `False` takes precedence. It retains at most 32 distinct sizes and their refresh variants. `tests/vibewine/mode-limit/test_modes.py` checks the cap and unaffected executable selection. |
| DS3 borderless fallback | [`ds3_borderless.hpp`](../components/dxmt/src/util/ds3_borderless.hpp): exact `DXMT_DS3_BORDERLESS=1`, case-insensitive `\Game\DarkSoulsIII.exe` path suffix, windowed swapchain and window/display safety checks | Off without the variable; unset it to disable. A focused current-component gate fixture is still missing; historical helper/watchdog tests do not verify this implementation. |
| DS3 native aspect fit | [`wave7_ds3_profile.inc`](../components/dxmt/src/winemetal/unix/wave7_ds3_profile.inc): both `DXMT_DS3_MARSHAL_FIX=1` and `DXMT_DS3_ASPECT_FIT=1`, DS3 basename among argv entries 0 or 1 | Off if either variable is absent; unset either to disable. `tests/vibewine/bridge-port/run_profile.py` checks positive and rejected names, argument positions and environment values. |
| Autonomous DS3 test decisions | [`lifecycle-policy.mjs`](ds3/lifecycle-policy.mjs), explicitly imported by the test controller | A finite observation/control policy, not engine configuration or automatic gameplay. See [the scenario and limits](ds3/AUTONOMOUS_TEST.md). |

The general mode count/fill repair, Metal argument guards, allocator ownership
and process-shutdown release repair are **core fixes**, not DS3 policies.
Compiler/wait tracing and virtual XInput are separately armed diagnostics.
See [patch origins](../docs/PATCHES.md) and [current acceptance](../docs/STATUS.md).

A contribution must name the owning gate, reason, activation/deactivation and
unaffected-path check. Do not move an engine hook here merely to make the folder
look self-contained. The earlier [DS3 source snapshot](../current/README.md)
remains available for historical comparison.
