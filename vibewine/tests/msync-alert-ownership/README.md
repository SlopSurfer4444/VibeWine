# MSync alert ownership regression

The native POSIX fixture compiles production `get_inproc_alert_fd`, `do_msync`,
`pthread_exit_wrapper`, `abort_thread`, `exit_thread` and their terminal helpers
from the selected Wine source. A surviving controller joins the worker and reads
its original descriptors with real `fcntl(F_GETFD)` calls. The worker uses real
`close` and `pthread_exit`; descriptor operations are never intercepted. The
normal route also executes the actual first previous-TEB exchange.

The MSync reply is a controlled server-transport seam. In the matching case its
index equals a newly opened sentinel descriptor; it is never guessed or installed
over a runtime descriptor. Cleanup must retain this unrelated sentinel. Other
cases use an unallocated numeric index, leave the field at its initial `-1`, or
fail the server request. In fd mode the seam supplies a separately opened owned
descriptor, which cleanup must close. All four communication descriptors must
close in every case, including MSync mode. Both terminal routes preserve worker
status and leave one controller thread in the production atomic count. Repeated
successful alert acquisition exercises its actual cached return.

```
python3 vibewine/tests/msync-alert-ownership/run.py --sanitize
python3 vibewine/tests/msync-alert-ownership/run.py --sanitize --arch x86_64
python3 vibewine/tests/msync-alert-ownership/run.py --sanitize --non-apple
python3 vibewine/tests/msync-alert-ownership/run.py --sanitize --revision 5ed4c56b --expect-regression
```

The baseline must fail **only** the matching MSync-index cases, with the sentinel
observably closed after both normal and abort exits. Candidate checks require all
cases to pass. `--non-apple` compiles the real non-Apple `do_msync()` implementation
and verifies fd mode. `--arch x86_64` runs a real x86_64 executable under Rosetta
on Apple Silicon; it does not emulate Wine's Windows context.

The fixture omits only the GS/TEB assembly because native pthreads have no Wine
TEB. The server request macros, transport, TEB allocation and interlocked primitive
are seams; terminal bodies and alert acquisition are extracted without edits.
This proves the cleanup ownership branch with actual Darwin fd operations. It
does not execute a complete Wine APC wait, the server/Mach protocol, a subsequent
normal exit's previous-TEB join, or gameplay.

The alert index is borrowed: `get_inproc_alert_fd` returns the existing
`current->alert_sync` index without taking an MSync reference. The server retains
that object until `destroy_thread` releases `alert_sync`; its backend destructor
calls `msync_destroy`. Thus the fix neither closes a Unix descriptor nor calls
`msync_close` for this borrowed index. Retaining a server thread handle beyond
client thread exit remains a full Wine runtime integration case owned by root.

Each runner permits 1–32 repetitions (default 4), at most 512 cases, a 60-second
compile and five seconds per case. Ordinary scratch lives in one private
`TemporaryDirectory` removed on exit; generated source, binaries and compiler
logs are expendable test data with a 7-day manual-review TTL and 32 MiB cap,
excluding active runs and evidence holds. The runner starts no persistent
producer. Saved result reports belong to the caller's evidence contract; the
frontier campaign owner stores logs under `outputs/frontier-wave-20261028/wine-alert/logs`
(7 days, 32 MiB) and results under its sibling `results` (30 days, 8 MiB), with
active work and holds excluded. These limits do not install automatic cleanup.
