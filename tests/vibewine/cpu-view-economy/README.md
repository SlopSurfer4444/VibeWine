# CPU view economy

Base: `8d0cef8dbcc8d1efbd0db97d4a0aee9e86d592d0`.

Buffer `view_` and `residency` now materialize a contiguous allocation-local prefix
through the requested descriptor, following the existing texture prefix design.
First use of index K creates K+1 native buffer views, rather than every registered
format. Both entry points use the same prefix. Later higher keys append only the
missing suffix; registering an unused descriptor does not invalidate earlier
views. The prefix count advances after each successful append, so C++ wrapper or
vector allocation failure preserves earlier wrappers, native references and
residency, and retry resumes at the first missing entry. A debug assertion checks
the requested index under the descriptor mutex before indexing.

Texture array/nonarray conversion stores one alternate descriptor index per
registered descriptor. Warm mismatching requests retain the shared lock but skip
conversion, exclusive registration and descriptor scanning. The memo is protected
by the existing descriptor mutex and contains no allocation/native handles.
Registration resets copied memo metadata: format aliases must not inherit another
format's result. No reverse memo is installed, because array-to-scalar conversion
normalizes the array size and round trips need not recover the original view.
The extra in-memory storage is four bytes per registered descriptor (the
TextureViewDescriptor size changes from 8 to 12 bytes), reclaimed with its Texture;
there is no independent cache, persistent producer, eviction or TTL.

Allocation cache mutation retains the existing encode-thread ownership contract;
this does not permit arbitrary concurrent access to one mutable allocation.
Concurrent descriptor registration and memo lookup use shared/exclusive locks.
Rename retains independent allocation-local wrappers and residency. Native nil
creation retains the existing policy: cache a nonnull wrapper with nil texture
and zero native GPU ID, without retry. Failed Wine Unix-call output handling is
outside this change.

## Checks

```
python3 tests/vibewine/cpu-view-economy/run.py --bench
python3 tests/vibewine/cpu-view-economy/run.py --arch x86_64 --bench
python3 tests/vibewine/cpu-view-economy/native.py
python3 tests/vibewine/cpu-view-economy/native.py --arch x86_64
python3 tests/vibewine/texture-view-prefix/run.py
python3 tests/vibewine/texture-view-prefix/run.py --arch x86_64
```

`run.py` extracts actual buffer/texture view types, Buffer owner declaration,
view/registration/rename/alias method bodies, and actual texel-size implementation.
It compiles the actual Metal C++ wrappers, small_vector, Rc, Flags and residency
function. Allocation and texture setup, OS mutex wrappers and native functions
are fixture substitutes. The descriptor-comparison counter is inserted into the
exercised registration loop for work counts; benchmarks omit instrumentation and
sanitizers, using the same actual method bodies with O2 and uncontended locks.
The fixture covers low/later-high/lower/repeated keys, highest-first, registration
epochs, residency-first creation and membership, exact native arguments and GPU
IDs, readonly/writable and Apple-family atomic usage, nil, wrapper/vector failure
cleanup/resume, rename/explicit old-allocation replay, each convertible texture
family, multislice non-reversibility, format alias isolation, cross-Texture
identity, and concurrent cold memo registration. Huge invalid keys terminate on
the debug guard before indexing.

The baseline passes its old-count expectations and is rejected by the candidate
work target. Connected mutations alter exercised owner code: eager buffer tail,
eager residency, disabled memo, wrong memo key, inherited format memo, wrong
residency slot, native GPU ID, native format, native parent, missing buffer lock,
and delayed prefix publication. Rejection must be a behavioral assertion without
sanitizer faults. ASan/UBSan checks run on arm64 and x86_64 via Rosetta.

`native.py` uses the same actual C++ owner bodies/wrappers and extracts actual
native Metal descriptor/new-view creation bodies. Mechanical casts adapt C to
Objective-C++; thunk adapters and resource setup are fixture code. Real Metal
compute reads 784 uint pixels: original buffer, renamed buffer, old-buffer replay,
and an array mip/slice converted to a scalar view. Baseline/candidate output must
match the exact independent pixel oracle and digest. A source mutation replacing
the native buffer-view format with R8Uint must fail the pixel oracle. Both
architectures run with ASan/UBSan. No Wine process, installed runtime or game is
launched. This is native fixture parity, not full D3D or game image parity.

The tests do not compile the full production translation units. A matched full
build, production workload frequency, actual frame CPU cost, compatibility and FPS
remain integration/runtime checks; fixture call counts and timing prove only
the exercised owner work.
