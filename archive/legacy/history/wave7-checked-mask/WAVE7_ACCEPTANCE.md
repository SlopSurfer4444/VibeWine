# Wave7 checked graphics successor: evidence and limits

This snapshot contains checked native layer application, synchronous pipeline preflight,
Retina mask coordinates, and feedback accounting/decoder events 77 and 78. The
selected renderer, bridge and native driver identities are in WAVE7_PROVENANCE.json.
The owner reports installation through reviewed source commit 64fdb5d. Native
admission is a separate delivery and is not included in this graphics stage.

| Evidence | Scope |
|---|---|
| Renderer build | 69 translation units recompiled; final artifact and source binding reviewed. |
| Presenter and native checks | Source-connected presenter regular/ASan/TSan checks and negative controls; 12 native scenarios in regular and ASan/UBSan builds. |
| Mask checks | Five extracted-production/CoreGraphics cases, with three rejected mutations. |
| Feedback checks | 74 decoder tests; native CLOSE checks and six writer/callback groups in regular and sanitizer builds. |
| Actual private GPU virtual fixture | Owner reports PASS in 13.46 seconds: 1080→1440→1080 buffer sizes and procedural GPU circle, natural exit. Display samples remained uncaptured and unchanged. |
| Actual private GPU queued-close fixture | Owner reports PASS in 3.79 seconds: 81/81 readbacks, eight pending frames with owner completion, exit 0 and private cleanup. |
| Actual Dark Souls III | Requested virtual fullscreen/resolution, keyboard, relaunch and performance acceptance remain pending for this combination. No FPS gain measured. |

The GPU fixture results are actual runtime fixture observations, not DS3 gameplay
acceptance. Earlier candidate1 keyboard-fixture and menu-mouse observations are
historical, retained under history/wave7-candidate1. They are not automatically
transferred to this successor. No raw traces, profiles, configurations, recordings
or authentication material are distributed.

Pipeline preflight fails before frame-admission side effects; it does not refund
an application-consumed external waitable token. A later encode-thread native
apply failure cannot retroactively change an already returned Present HRESULT.
CLOSE accounting exposes queued discards; it does not guarantee last-frame or
capture-boundary callback finalization. Events 77/78 are CPU instants, not GPU
present duration or proof of dropped displayed frames. Portable public recipes
are adapted from recorded commands; a clean-room or bit-identical rebuild has
not been established. No new test aggregate was run merely to stage these files.
