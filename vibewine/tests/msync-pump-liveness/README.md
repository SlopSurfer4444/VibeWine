# MSync reply-pump liveness

On Darwin, from the Wine component, with a campaign compiler/native slot:

```sh
python3 vibewine/tests/msync-pump-liveness/run.py --arch arm64 --sanitize
python3 vibewine/tests/msync-pump-liveness/run.py --arch x86_64 --sanitize
python3 vibewine/tests/msync-pump-liveness/run.py --arch arm64 --sanitize --revision 271e5cdaa93c5a49c7cb219479cff36ead7cd8c8 --expect-blocked
python3 vibewine/tests/msync-pump-liveness/run.py --arch arm64 --sanitize --mutation missing-failed-send-cleanup
```

`--generate-only` reads/extracts/hashes without a compiler or native process.
`--output` names a new private evidence directory. One temporary executable runs
serial trap/fallback cases for full queue/finite timeout, full queue/alert,
dead client and normal mapping. The mutation runs only the two full-timeout
cases. A compiler has a 60-second cap; each executable has a 12-second subprocess
cap, nine-second internal alarm, and child seven-second watchdogs. No installed
Wine prefix, game, GPU or UI is used.

The runner extracts the actual client deadline/ulock helpers, registration/
unregistration, contention check and complete multiwait body. It extracts actual
server list operations, mapping reply, wake/ACK helpers, receive wrapper,
decoding and complete pump body. The identical client/server Mach wrapper runs
the real dynamically resolved `mach_msg2_trap` or the real `mach_msg` fallback;
a missing trap fails. Only outer wrapper and ACK entry symbols are renamed for
observations. An export seam records a real kernel-created name without
inventing an export, timeout or return status. File, extracted and generated
source SHA-256 bindings are printed and retained with optional results.

Two private clients are forked before any thread starts. A process-local
bootstrap special port supplies the private send right across fork; the parent
immediately restores its original special port and verifies readback. No
launchd service is registered. Clients use private ports and shared anonymous
mappings. The offender fills its qlimit-one reply queue with a sentinel and
sends a normal wire-format mapping request. The second client runs extracted
multiwait. The parent starts the actual pump and observes entry into the real
reply send before releasing the second client. Its ACK must arrive while the
offender still refuses to drain; its finite wait or alert cancellation must
finish and unregister. The old failure is specifically absent ACK before queue
release, with normal completion after release, not a killed unexplained wait.
The congested client's mapping reply is intentionally dropped; this proves
independent-client progress, not successful completion of that client's RPC.

A FIFO stop fence, consumed by the transport seam before production dispatch,
joins the pump after unregister/drain. Nodes and all object/alert waiter counts
must balance. Dead-client destroys and reaps the offender before pump receive,
then requires independent ACK and a real invalid-destination result. Normal-map
directly calls the extracted server send, receives its actual descriptor, maps
it, checks bytes and releases mapping/message/port.

Each invocation's exported-entry and valid destination names must disappear
after cleanup; a dead receiver delivers the explicit `MACH_PORT_DEAD` sentinel,
which has no allocated owned name. Removing actual failed-send `mach_msg_destroy`
must fail the exported-entry oracle under both real backends. These are targeted
owned-right checks, not aggregate IPC-space neutrality or a classification of
the held Rosetta failures.

Limits: object/node/mapping domains are reduced and private. The real page-table
helpers use fixture-owned pages; calloc/free replaces the server pool; native
monotonic 100 ns ticks supply the NT clock seam. Offender requests are handcrafted.
The client mapping RPC is not extracted, and successful mapping consumption is
a same-task control. No mapping-RPC cancellation, whole Wine lifecycle, WaitAll
or arbitrary packet admission claim follows. Run existing ACK/registration/
WaitAll suites for their controls. Existing client mapping receive-right issues
and the held full candidate remain separate work.

Temporary C/executables are removed at runner exit. Direct children are tracked
from fork and synchronously reaped; cleanup retries interrupted waitpid probes,
and only an unreaped direct child may be killed (its PID cannot be reused).
Independent child watchdogs bound orphan duration on supervisor failure.
Optional retained generated source/logs belong only to the explicit private
output: owner MSYNC branch until root admission, logs seven days/32 MiB,
curated results thirty days/8 MiB, active/unresolved/recovery holds excluded.
Builds remain within 256 MiB branch and campaign 40 GiB/20 GiB reserve limits.
Manual exact-owner/reference/hold cleanup only; no persistent producer or
cleanup service is installed.
