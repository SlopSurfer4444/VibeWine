# MSync pointer-table ownership fixture

Run from the checkout on macOS:

```sh
python3 vibewine/tests/msync-memory-ownership/run_table.py --arch all --sanitize
python3 vibewine/tests/msync-memory-ownership/run_table.py --revision 5ed4c56b --expect-regression --arch all --sanitize
```

The runner extracts the actual client `get_shm_slow`/`get_shm` and server
`get_shm`, their table declarations, and the shared production table header.
It prints SHA-256 identities for the production extraction and full source.
It does not build or run Wine or a game. Both architectures execute natively or
through the host's existing Rosetta support; unavailable support is a failure.

For growth, one reader pauses after evaluating the address of a production
leaf slot and before loading it. A writer grows the table past entry 127, then
releases that reader. The old implementation uses the actual extracted callers
with a pre-load gate on their plain array accesses; a realloc seam always moves,
copies and frees the old allocation. ASan must report heap-use-after-free for
both old client and old server. The fixed helper uses the same gate on its
atomic loads; its leaf slot remains valid. This is a controlled lifetime test,
not a probabilistic stress run or a TSAN data-race proof.

Additional fixed checks cover entries 127/128, 255/256, 511/512, 1023/1024,
65535, the last valid protocol index and page offsets. Allocation and map seams
inject failures into the extracted production callers; each must terminate
with status 1, its diagnostic, and no sanitizer finding. An invalid protocol
index must also terminate. Real Mach page mappings back successful map calls;
the client RPC itself is stubbed at `request_shm_from_server`. This table-only
checkpoint does not validate RPC transport or Mach-right ownership. The separate
Mach ownership candidate remains held with unresolved aggregate Rosetta checks;
production RPC transport, cleanup and export-error policy retain their baseline
behavior, apart from the server reading the new stable table. The fixture substitutes `fatal_error`
with its terminating formatting contract and fixes the logical page size to
4096 for deterministic boundary indices on both host architectures.

Build sources and binaries live in a temporary directory removed by the
runner. Each child has a 15-second alarm and a 20-second parent deadline.
All table segments and Mach mappings are intentionally process-owned until
exit, matching production's lifetime policy. No persistent producer is started.


Evidence owner is the invoking branch owner, with root as integration consumer.
Scope is the exact explicitly selected output log/result paths. Logs have a
7-day TTL and 32 MiB aggregate cap; generated sources/results have a 30-day TTL
and 8 MiB aggregate cap. Active review and explicit holds exclude retirement.
Cleanup is manual through that owner after checking holds and path ownership.
The fixture installs no collector or automatic cleanup service.
