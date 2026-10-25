# Native MSync send-pressure fixture

Run on macOS from the Wine component:

```sh
python3 vibewine/tests/msync-pressure/run.py --arch all
python3 vibewine/tests/msync-pressure/run.py --arch all --sanitize
```

`--arch all` compiles and executes arm64 and x86_64 (Rosetta on an Apple Silicon
host). Each architecture executes the real dynamically resolved
`mach_msg2_trap` and the production `mach_msg` fallback independently. Failure
to resolve the trap is a test failure rather than a silent fallback.

The runner extracts the complete current client `server_register_wait`,
`server_remove_wait`, and `signal_all` bodies, server `signal_all`, shared
object/message declarations, and the complete production `mach_msg2` /
`mach_msg2_internal` wrappers. It requires the client/server wrappers to match.
Only the server signal symbol and top-level wrapper symbol are renamed to
coexist in one translation unit. A call-boundary seam audits all seven caller
arguments, then invokes the unchanged production wrapper with those arguments.
The complete source-file and extracted-input SHA-256 hashes are printed.
Attribution begin/end hooks are no-ops in this fixture; the fixture makes no
claim about attribution metrics. Extracted code retains the production LGPL
license header.

Each of the four sends gets a newly allocated, private receive port with an
inserted send right and queue limit one. The fixture queues a sentinel message,
then starts exactly one sender. With the receiver gated closed, it observes
entry into the send seam, the sender's kernel `TH_STATE_WAITING`, a still-full
queue, and no return. There is no other sender wait after entry. It checks the
waiter refcount changes while the sender is blocked, then receives the sentinel
and joins the sender. It requires the actual send to return success and checks
the subsequently received production message's ID, size, object IDs, mutex/
unregister flags, and alert object. No send timeout is added.

The two real Darwin ulock checks use anonymous shared mappings and a sleeper
thread parked in `__ulock_wait`. Each actual signal function must wake that
sleeper successfully before its one-second safety timeout, while zero
`multiple_waiters` prevents a Mach send. Native kernel `WAITING` is observed
before signaling. These checks exercise wake delivery, not a game or Wine
runtime.

Every architecture/backend also runs three independently compiled negative
controls. Bypassing the actual send must fail because the sender returns before
the gated drain. Adding `MACH_SEND_TIMEOUT` to actual production call sites
must fail the argument audit. Removing the actual signal wake must fail the
native ulock delivery check. The driver requires the expected failure category,
so unrelated crashes or timeouts cannot satisfy a negative control.

Private ports are drained, send rights released, and receive rights removed;
threads are joined and mappings unmapped. The runner kills/reaps its owned
fixture after 15 seconds, and the fixture has a ten-second process alarm.
Kernel process teardown releases outstanding rights on failure. Temporary C
files and executables are bounded to four builds per architecture and removed
by the runner's temporary-directory context; no persistent producer or retained
raw capture is created. Fixture source belongs to the Wine test owner. The
fixture provides focused native send/wake evidence; it does not establish
production runtime activation, game performance, memory-pressure behavior,
or measured overhead.
