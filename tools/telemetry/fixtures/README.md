# Telemetry protocol regression inputs

`writer_interleaving.cpp` exercises the maintained writer's close, overflow and
generation barriers. `legacy_trace_core.hpp` is the actual version-1 decoder used
as a compatibility rejection control. These are test inputs, not another active
collector implementation.

Both files are retained byte for byte from VibeWine commit
[`1d88dc98`](https://github.com/SlopSurfer4444/VibeWine/commit/1d88dc989d71709821ac0f60fbaeb3806aac8c68):

| Input | Original path | SHA-256 |
| --- | --- | --- |
| `writer_interleaving.cpp` | `archive/legacy/renderer/wave6/consumer/tests_protocol.cpp` | `faf75fcccdc04f37b0675b842b28673eedba97369b278cf4c011d4cd3a983a2b` |
| `legacy_trace_core.hpp` | `archive/legacy/renderer/wave6/consumer/legacy/trace_core.hpp` | `ef68b0770dd5fa1d7aaa1f633797ac1d3348b388bcf4e64476128abc6629dbcc` |

The repository's LGPL-2.1-or-later license applies. `test_detail_protocol.py`
assembles them in temporary storage, adapts only the driver's capacity arguments,
and compiles against the current production protocol. It removes temporary
outputs after the test.
