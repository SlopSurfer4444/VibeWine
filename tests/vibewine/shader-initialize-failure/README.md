# Shader initialization failure without an error object

`PipelineCache::CreateShader` previously left `sm50_error_t err` uninitialized,
then retrieved/freed that pointer on every `SM50Initialize` failure. The Windows
thunk returns -1 on nonzero `UNIX_CALL` status without initializing supplied
outputs. A transport failure before native initialization can therefore leave
`err` indeterminate. Native initialization's own error-output zeroing applies
only when that native function executes. `SM50GetErrorMessageString` does not
accept a null error either, so initializing the pointer alone is insufficient.
This is a source-permitted failure path; no occurrence in a real runtime is claimed.

The small general correctness fix initializes only `err` and retrieves/frees an
error only when present, otherwise emitting one fallback through the existing
error logger. It leaves successful initialization, SHA1 lookup/publication,
wrapper ownership and failure retry policy intact. It does not zero reflection,
change thunk outputs, introduce synchronization or cache failures.

Run the focused CPU check:

    python3 tests/vibewine/shader-initialize-failure/run.py --arch arm64
    python3 tests/vibewine/shader-initialize-failure/run.py --arch x86_64

Both configurations use ASan/UBSan, warnings as errors and `-fno-exceptions` for
C++. The runner compiles the entire unchanged actual `airconv_thunks.c` as C2x,
actual protocol/public headers and actual SHA1 implementation. A private Wine
header substitutes only the `UNIX_CALL` boundary. The C++ fixture compiles actual
extracted `CreateShader` and `CachedSM50Shader` constructor/destructor bodies in
a small owner adapter. It uses the actual inline `SM50GetErrorMessageString`.

Coverage includes transport failure without any output writes; native failure
with an owned error/message/free exactly once; native failure with explicit null
error; nonzero transport status after an owned output; success with nonempty and
empty argument arrays; distinct SHA1 keys; warm-hit pointer identity/bypassed
initializer; failure followed by successful retry; and handle/argument cleanup
through the actual wrapper destructor. The final fixture observes nine
initializations, two message calls, two error frees, three argument-info calls,
and four successful handles released once each.

The historical caller at `a21579fb2a03aef5fbe93d7bcba1ea882e63a192` must fail
an ownership assertion on the untouched-output failure path. Compiler pattern
initialization makes that old path deterministic; the mock boundary rejects the
invalid pointer before a native dereference. A pointer-initialization-only
control likewise rejects null message access, proving that the guard matters.
A missing-free control rejects lost owned-error cleanup. Controls must fail
these assertions without sanitizer errors.

The native parser, actual Wine transport, error allocations and shader handles
are adapters. These checks do not demonstrate native parser destruction,
duplicate-parse concurrency, installed Windows/Wine ABI, Metal, game or FPS
behavior. An independent source review found no material issue. A complete
production build remains the integration owner's gate.

Fixed source fixtures belong to this directory. Each invocation owns a private
temporary directory, with a 64 MiB aggregate scratch cap, 60-second compiler and
20-second executable timeout, removed on normal exit/errors. Hard-killed
leftovers need manual ownership/process/hold validation. Retained raw reports
follow the root wave's seven-day/64 MiB contract with active/analysis/recovery
holds. The production change uses the existing failure logger and adds no data
producer, cache, retention policy, cleanup service or runtime configuration.
No installed runtime, game, cache, user data or Git index is touched.
