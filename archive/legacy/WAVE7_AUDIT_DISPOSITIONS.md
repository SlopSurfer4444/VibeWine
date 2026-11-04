# Wave7 source changes and limits

This is an authored technical summary, not a copy of private audit material.

The renderer captures presentation state and retains queued resources; resize and
menu transitions preserve validation and resource ownership. Display queries use
read-only snapshots and separate transition state from query locking. Unsupported
nonzero display mode changes can be refused explicitly; this does not prove the
requested game resolution works. Optional capture feedback cannot turn an unknown
old-bridge state into proof of release.

Native display properties are primed before publication and copied without a
main-thread round trip. Synchronous layer setters use the optional versioned Wine
main executor, whose request source is serviced in Wine's query-response run-loop
mode. Existing fallback compatibility remains. The deferred asynchronous layer
protocol was rejected because it could lose a sole Present or starve under churn;
it is not included in the compiled candidate source exported here.

Source fixtures cover retained lifetimes, settings snapshots, listener ordering,
executor query mode, getter recursion, and error paths. Release statistics clock
collection is gated without removing waits. Present encode timing is named for
what it measures; it is not displayed frame time. Actual game acceptance remains
as listed in WAVE7_ACCEPTANCE.md. No fixture is a claim of live scheduling proof.
