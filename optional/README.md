# Optional candidates — not installed

- `borderless/`: independently reviewed original watchdog-generation helper and extracted-source tests. Real Wine/API/game acceptance remains pending. LGPL-2.1-or-later contribution scope is explicit.
- `sync-probes/`: independently reviewed DXMT diagnostic producer and matching controller/analyzer source. Additional wait/flush/commit attribution is instrumentation, not an FPS optimization. The installed be876 source is separately preserved in `renderer/diagnostic`.
- `xinput/`: retained earlier Wine LGPL-2.1-or-later patch and candidate source. It separates desired vibration state from HID transmission and clears stale controller slot state. Hardware reconnect and actual provider selection still need verification. It is outside the aggregate runner and has no live-game acceptance claim.

No executable binaries, game data or raw game telemetry are included.
