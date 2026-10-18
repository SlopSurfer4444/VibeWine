# Bounded metadata logger: native controller and regression fixtures

These fixtures exercise the shared `include/wine/netdiag.h` protocol and the
shipping `include/wine/netdiag_unix.h` implementation. They are standalone host
C programs, not a Wine installation or a gameplay acceptance test. The native
fixture substitutes only executable identity and Windows PID/thread identity.
It uses a local Unix socketpair; it sends no external packets and changes no
network configuration. All build and capture files belong to one automatically
removed temporary directory.

From the source root:

```sh
python3 dlls/ws2_32/tests/netdiag/run.py
```

The runner compiles both C files with C11, optimization, warnings-as-errors and
pthread support. `CC` can select another host compiler. It checks:

* A fresh directory with mode 0700 and a singly linked capture file with mode
  0600, exactly `WINE_NETDIAG_FILE_SIZE` bytes, zeroed and touched before use.
* Exact executable/token matching, no opt-in, malformed protocol/header/file
  rejection, relative paths, permissions, hardlinks and final symlinks.
* Exclusive process claim before arm, duration bounds, permanent one-shot
  stop/expiry, and no rearm after a prepared session is stopped.
* Existing socketpair I/O after stop and expiry, a real local nonblocking receive
  error, and errno preservation during initialization, inactivity, expiry,
  normal recording, lifetime contention, invalid handles and record overflow.
* Observed pre-existing socket lifetimes, OPEN/CLOSE generations, stale closes
  and handle reuse; concurrent publication; bounded saturation/drop accounting;
  dump snapshots that skip uncommitted slots.

The tests do not establish PE/Unix bridge correctness, actual resolver behavior,
game socket coverage, Wine runtime integration, or performance overhead in a
game. The fixture's fixed mock Windows identities are not OS observations.

## Controller for a separate explicitly authorized trial

Build outside the source tree, from the source root:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -I include \
  dlls/ws2_32/tests/netdiag/netdiag-tool.c -o /absolute/build/netdiag-tool
```

The interface is:

```text
netdiag-tool init /absolute/fresh-directory 'C:\exact\windows\image.exe' TOKEN
netdiag-tool verify-fresh /absolute/fresh-directory/netdiag.bin 'C:\exact\windows\image.exe' TOKEN
netdiag-tool arm /absolute/fresh-directory/netdiag.bin MILLISECONDS
netdiag-tool stop /absolute/fresh-directory/netdiag.bin
netdiag-tool dump /absolute/fresh-directory/netdiag.bin
```

`TOKEN` is exactly 32 lowercase hexadecimal characters; the executable is the
exact ASCII Windows image path, 1–511 bytes. Init refuses an existing directory,
including an empty one. It never overwrites or truncates an existing capture.
It writes protocol version 1, record size 128 and capacity 16,384. The initial
state and process IDs are zero.

`verify-fresh` compares the complete file with the exact initialized protocol
image for the supplied executable and token. All claim, state, clock, counter,
lifetime, padding and record bytes must remain zero. It returns sanitized JSON
with `fresh:true` and zero process IDs; it prints neither binding value. It
does not modify the file. This is a point-in-time observation, not a reservation
against a target process claiming the mapping immediately afterwards.

Controller file locks are advisory and nonblocking. Arm and stop reject a held
lock immediately instead of queueing an action for later. The logger does not
take these locks.

The target must inherit `VIBEWINE_NETDIAG_FILE` (the absolute capture path) and
`VIBEWINE_NETDIAG_TOKEN`, and reach an instrumented operation so the native logger
can claim the mapping. A matching PID is a claim, not evidence of recorded
socket activity. Inspect `dump` for the nonzero host PID before `arm`. An absent
or rejected opt-in is cached for the process lifetime; changing environment
variables later does not enable that process.

Arm accepts 1–45,000 milliseconds. It requires a claimed, unused prepared file,
sets start/deadline from the host monotonic clock, then publishes armed state
with release ordering. State values are 0 prepared, 1 armed, 2 stopped and
3 expired. Stop publishes a terminal state; it does not close game sockets.
The logger marks expiry when it next checks an armed mapping. Dump can therefore
show state 1 with `enabled:false` after the deadline until the next logger call.
Use a new directory, token and target process for a new capture.

Dump writes JSONL: one header followed by release-committed records only. Header
`count` is the number of records included in this snapshot; `reserved` can be
higher while a writer is committing. `dropped` includes failed reservations and
lifetime-lock contention. Records retain numeric API/phase/flags and raw Windows
address-family values (IPv4 2, IPv6 23). DNS summary family values may instead be
a bitmask when `ND_ENDPOINT_UNKNOWN` is set. Dump includes endpoint bytes and
numeric process/thread identifiers; it omits the binding token, executable,
DNS names and raw socket handles. It never copies private bridge contexts.

Both programs are LGPL-2.1-or-later, as are the shared logger headers.

## Wine integration fixture and observation limits

`windows-fixture.c` is a real x64 Windows console fixture linked to ws2_32. It
uses numeric loopback DNS only, a nonblocking loopback TCP connection and
refusal, and an overlapped UDP receive followed by a loopback send. It creates
no window. Its payload sentinel is checked inside the fixture and must never
appear in the capture or JSON. API assertions print only counts and error codes.
Run it first without opt-in against the intended private runtime, then with a
fresh exact-executable-bound session. The optional `--wait-arm` mode first
creates and closes a socket to claim the prepared file, prints readiness, and
waits thirty seconds for the separate host controller to arm. The scenario's
bounded wait budget starts afterward. Missing recording is an integration
failure even if the API fixture passes.

The private runtime delta is indivisible for this candidate: x86_64 Unix
`ntdll.so`, Unix `ws2_32.so`, and PE `ws2_32.dll`. The added Unix bridge ordinal
requires the paired PE/Unix ws2_32 modules. Do not mix either with an older
ws2_32 module. Preserve the existing PE ntdll and the runtime's other modules;
verify the actual loaded paths and hashes before admitting a capture.

Expected evidence includes anonymous DNS BEGIN/COMPLETE and answer records;
CONNECT attempt/pending/outcome and NETWORK_EVENT observations for FD_CONNECT
and SO_ERROR; socket OPEN/CLOSED lifetimes; and RECV PENDING, native EAGAIN,
SEND attempts, receive completion, and WSAGetOverlappedResult observations.
Pending is not terminal failure. The same logical handle lifetime joins PE and
native records, while each explicit WSAGetOverlappedResult/FD_CONNECT/SO_ERROR
observation receives a fresh operation ID. It does not identify the original
ConnectEx OVERLAPPED request. IOCP-only connect completion remains unobserved.
Existing sockets get an observed lifetime at first instrumented operation;
lookup contention or a full 512-entry table explicitly marks lifetime unknown.
Receive endpoint metadata is available only when the original operation asks
for a source address. Stream operations may therefore have unknown endpoints.

`run_native_hooks.py` extracts and compiles the shipping native function bodies
with hash-bound receipts and narrow external seams. It checks unarmed paths
with protected argument memory, errno preservation even when the observer
changes errno, both actual write-watch EFAULT receive attempts, EINTR retry,
truncated receive byte counts, pending/cancellation/error behavior, and the
original allocation-failure paths. The source's pre-existing terminal fd-lookup
failure returns without release_fileio; this instrumentation preserves that
lifecycle and its error result. No pre-existing cleanup fix is included.

These observations contain no application buffers, DNS names, URL strings,
authentication fields, TLS data, or packet capture. They do not observe Steam's
other processes, server-side errors, TLS/HTTP results, or all application-level
login and save outcomes. Syscall duration is local attempt duration, not RTT.
Record capacity, drop counters, deadline expiry and unknown fields must remain
visible in analysis. No game overhead or root-cause claim follows from compile,
native-seam tests, or a passing loopback fixture alone.
