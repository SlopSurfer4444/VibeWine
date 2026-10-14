# Audit source map

All paths below are repository-relative under `source/dlls/`.

| Area | Entry points | Review emphasis |
|---|---|---|
| Display ownership and release | `winemac.drv/cocoa_app.m` | Capture/release failure state, stale saved modes, cleanup ownership, physical vs logical state |
| Retina and resolution enumeration | `winemac.drv/display.c`, `macdrv_cocoa.h`, `window.c` | Coordinate units, descriptor absence, scaling order, preserving aspect ratio |
| App lifecycle and reopen | `winemac.drv/cocoa_app.m`, `cocoa_window.m`, `macdrv_main.c` | Session ownership, visibility vs minimization, focus restoration, cursor release |
| Native AX bridge | `winemac.drv/ax_native.inc`, `cocoa_window.m` | Cache generation, object retention, stale identities, threading, read recovery and uncertain actions |
| Windows MSAA provider | `winemac.drv/ax_provider.inc`, `dllmain.c` | COM apartment/thread assumptions, provider reentrancy, bounded traversal, action readiness |
| Cross-boundary protocol | `winemac.drv/ax_protocol.h`, `ax_validation.h`, `unixlib.h` | ABI/layout consistency, validation, bounds, handles, expiration and replay |
| Standard Windows controls | `oleacc/client.c`, remaining `oleacc` source | Role/state accuracy, action semantics, checkbox/radio changes and control lifetime |

The `baseline` directory is the selected archived base, not the previously installed custom runtime. AX tests include explicitly named earlier native-bridge reference snapshots for regression analysis; these are not current production code.

`tests/provider/harness.c` is a supplied mock-adapter template with substitution markers. It is audit material, not a standalone runnable test. The aggregate runner exercises the exported display, Retina, and native AX sources. It does not run the provider template or build a Wine runtime.
