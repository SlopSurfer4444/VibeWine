# Optional candidates — not installed

These source changes are separate from the installed native driver snapshot. Neither has live-game acceptance or a demonstrated FPS gain.

- `dxmt/`: one-line cleanup of owned bitcode on a Metal-library creation error path, MIT-licensed DXMT source; license retained. Source archive SHA-256 `efb7fd03d2652a2574b1427ea3f65afc9da872327111138ff20163952d5f5e0e`, upstream commit unknown. Success and shader-cache paths are intentionally unchanged. The contained methods and mock fixture permit inspection of the ownership claim. This is not the full translator or a shader-performance optimization.
- `xinput/`: Wine LGPL-2.1-or-later patch and candidate source, with retained copyright header. Separates desired vibration state from HID transmission and clears stale controller slot state. Hardware/controller reconnect and actual provider selection still need verification.

These optional fixtures are not part of `tests/run.py` and were not rerun during export. Prior local verification is summarized in the parent audit briefing; do not treat compilation or mocks as installed acceptance.
