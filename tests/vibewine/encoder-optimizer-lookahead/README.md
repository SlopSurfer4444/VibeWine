# Bounded encoder optimizer experiment

Build option -Dvibewine_bounded_encoder_optimizer=true defines
DXMT_BOUNDED_ENCODER_OPTIMIZER=1 and limits each optional merge search in
ArgumentEncodingContext::flushCommands to the next 64 physical encoder-array
positions. Null entries consume the window too. The default is false/0 and keeps
the existing unbounded search.

The stop occurs between completed relation checks. It retains all earlier
mutations, including partial depth/stencil clear folding and absorbed resolves,
and leaves the normal encoder emission loop in place. It performs no extra
transformation for pairs outside the window. Event/timestamp synchronization
breaks, command lists, fence transfer and query handling remain in their existing
production methods.

Run the focused CPU check:

    python3 tests/vibewine/encoder-optimizer-lookahead/run.py

The runner reuses pass-optimizations/run.py's extraction/adapters. The actual
production optimizer driver, encoder structures, relation/signature/dependency
methods and real command ABI/FenceSet are compiled with ASan/UBSan. Instrumentation
counts actual relation invocations and candidate visits; it does not substitute
a model loop. Default and explicit 0 receipts must equal the unbounded source at
e1772a425721b4f694a52c20ef92fff7332bafe4. A combined variant also enables the
existing dead-store and physical-residency experiments.
The bounded variant also compiles for the other Darwin architecture.

Coverage includes 63/64/65/256/1024 independent renders and long Null tails, exact
64/65-position merge boundaries, draw preservation/order, visibility participation,
wait/update fence identities, signal/wait/timestamp barriers, a suffix merged
before its predecessor is considered, partial clear folding before cutoff, and
resolve absorption immediately before cutoff. Existing pass-optimization cases
run too. Negative controls remove the bound, exempt Null positions, or drop an
encoder when the window ends; each must fail a behavioral assertion.

The source bound limits candidate traversal to at most 64 positions per eligible
former encoder. This is a bound on scan/check count, not CPU time: relation
methods and successful merges retain their existing costs. Fewer merges can
increase native pass count and attachment bandwidth. This experiment establishes
no Metal image equivalence, GPU timing, frame-time or game FPS improvement and
remains off pending matched-build and controlled GPU/game comparison.

This test directory owns the fixed fixtures. Generated translation units and
binaries live in a private temporary directory for one invocation and are removed
on exit; no runtime, installed data, capture stream or persistent producer is used.
