# Terminal SIGQUIT reentry regression

This host-native POSIX fixture compiles the actual `abort_thread`, `exit_thread`,
`pthread_exit_wrapper`, `abort_process`, `get_unix_exit_code` and
`read_reply_data` function bodies, plus the shared terminal mask helper and
`exit_process`, from the selected Wine source. It also extracts
the actual server signal-mask initialization and asserts the x86-64 SIGQUIT
handler's `abort_thread(0)` edge remains present.

One worker mode reads EOF from a real pipe and enters production
`abort_thread(0)`; the other calls production `exit_thread(7)` directly. Each
decrements the real shared atomic thread count and executes the production five
FD closes and `pthread_exit`. The normal path also executes its real first
`prev_teb` atomic exchange (there is no older TEB to join in this first-exit case).
A close interceptor delivers a real, thread-directed SIGQUIT after one selected
syscall boundary. Its native signal handler takes the same direct
`abort_thread(0)` edge; it does not emulate Wine's x86 context setup. The main
thread remains alive and intends to terminate with exit status 20.

On the unfixed body, every injected cleanup boundary allows the dying worker to
decrement the count twice and terminate the process with status 0. The candidate
blocks SIGQUIT locally before each terminal decrement, lets cleanup complete once,
and preserves status 20 from the remaining main thread. The fixture additionally
checks all inherited server signals stay blocked, all five real FDs close, the
main thread's SIGQUIT mask stays unchanged, and an ordinary SIGQUIT delivered
before either terminal path still terminates the worker. No-signal abort/normal
exit are separate controls, including preservation of the normal worker's status
7 returned through `pthread_join`.

```
python3 vibewine/tests/thread-exit/run.py
python3 vibewine/tests/thread-exit/run.py --sanitize
python3 vibewine/tests/thread-exit/run.py --revision ad5b79cf --expect-regression
```

The runner accepts 1–32 repetitions and `--kind abort|normal|both`, gives
compilation 30 seconds and each of at most 448 isolated cases two seconds. Each
fixture emits at most 4 KiB of binary
events; the JSON report contains bounded decoded observations. Scratch sources
and binaries are owned by this runner, kept in one private temporary directory,
and removed on completion. No Wine prefix, user cache, GPU or application is
started. If a caller saves reports, they belong to that caller's bounded evidence
contract; this test starts no persistent data producer.

The Windows x86-64 GS/TEB-specific assembly in `pthread_exit_wrapper` is disabled
in the generated host fixture because a native pthread has no Wine TEB. The
remaining production bodies are extracted verbatim. Windows interlocked
decrement is supplied by a sequentially consistent native atomic primitive;
`ntdll_get_thread_data()` uses fixture TLS with real FD ownership.

This proves the shutdown interleaving exists and the local mask closes that
interleaving. It does **not** prove this was the cause of a previously observed
GPU guest exit 0, exercise Wine's full signal/stack machinery, establish gameplay
or performance, or exercise a subsequent normal exit's previous-TEB retirement.
