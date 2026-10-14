# Independent architecture, correctness and performance audit

Act as a skeptical Wine/macOS, Win32, Cocoa/Metal and accessibility engineer. Our objective is broadly reusable Windows-game integration on Apple Silicon: reliable launch/quit/fullscreen/Retina/input, genuine accessibility, and measurable frame-pacing/FPS improvement without lowering graphics. Audit the supplied code, not just this narrative. Do not assume patches or test counts prove real-game behavior. If repository files are inaccessible, say exactly which files you need; never invent source inspection.

## Context and evidence limits

Our modified native macdriver, paired x86/x64 PE drivers and oleacc are installed as a signed five-binary closure in two owned runtimes. The tested DS3 renderer is a custom DXMT v0.80-derived build with prior early-compilation/prewarm and DS3-specific work. XInput lifecycle and shader-bitcode error-path cleanup are separate source candidates, not installed and not proven FPS improvements. This export is an audit snapshot, not an official Wine release or universal installer.

DS3 screen and controller work in WINDOW mode with a borderless fullscreen helper. User previously reproduced a severe black-screen lock on exit after enabling true in-game fullscreen, requiring hardware reboot. Recent borderless exit returned the display normally; natural process termination was not independently established in that observation. Do not conflate borderless with exclusive fullscreen or deliberately reproduce a machine lock without bounded recovery.

Latest 120-second DS3 gameplay capture at 1920x1080: 117.0706 observed seconds, 4023 presented frames, 34.3639 presented FPS, CPU wall mean23.684ms, on-GPU mean17.876ms, drawable wait mean0.01955ms. 0.9291sec gaps, one zero-present interval and41skipped frames retained. Owner reports running/fighting. All compiler counters unavailable. CPU wall is not CPU busy time; times overlap; one-second interval percentiles are not frame-time percentiles/1% lows. No matched old/new, cold/warm, quiet/busy comparison; active development and thermal effects may confound. Older informal55–60FPS reports were not matched controlled benchmarks. No gain/regression attribution is established.

Accessibility: isolated genuine Windows controls were exposed via native AX and CUA presses delivered observable checkbox/radio events. This does not establish CUA support for arbitrary rendered game UI. A timed-out readiness probe rejected an action; transport/queue success is not completed mutation. Earlier getter timeouts and quarantined providers drove bounded read recovery; inspect races, identity and lifetime rules closely.

Ubisoft authentication restriction reproduced on old/new Wine under same VPN route; Prince acceptance postponed. Do not attribute server anti-abuse responses to graphics changes or propose bypassing authentication/anti-cheat.

Installed helper recovery separately revealed coupling to mutable canonical policy: byte rollback worked but old helper rejected new metadata. Source-only immutable-policy successor exists outside this export; don't confuse workstation harness complexity with Wine runtime defects.

## Required audit tracks

1. Architecture map: Windows APIs -> Wine/server/macdriver -> Cocoa, x86 translation, graphics DXMT/Metal, input and accessibility. Identify actual source ownership for each symptom. Separate Wine vs renderer vs launcher vs game fixes.
2. Correctness review with file/function/line references: display capture/release ownership, failure unwind, mode restoration, quit/crash/sleep/deactivate races, main-thread/AppKit reentrancy, Retina scale vs logical/physical geometry, restore/reopen focus and helper windows.
3. Accessibility: native/PE protocol, leases, bounded provider reads, stale HWND reuse, child lifetimes, COM release/reentrant getters, action exactly-once vs unknown delivery, readonly/protected controls, architecture compatibility, polling costs. Seek regressions and false-success paths. Suggest expansion beyond BUTTON without overstating game UI semantics.
4. Input: cursor clipping on focus transitions, USB/Bluetooth reconnect, vibration, duplicate Steam/native input, device generation, neutral state, sleep. Distinguish static applicability from observed loaded providers.
5. Performance: analyze possible CPU submission/locking/Rosetta/server IPC, DXMT command encoding/synchronization, frame pacing/present, shader/pipeline cache persistence, memory pressure, GPU saturation, background compilation and thermal pressure. Rank plausible hypotheses without fabricating causality from aggregate data. Identify concrete code paths and discriminating probes. No blanket flags, disabling protections or arbitrary graphics downgrade.
6. Fullscreen failure plan: develop a state-machine/ownership hypothesis and minimal reproduction that can be recovered remotely or by an independent watchdog before testing the full game. Specify what happens if WindowServer/main thread is blocked; a helper in the same stuck process is not adequate recovery. Don't assume a watchdog can recover a GPU/system hang.
7. Reusability: distinguish general upstream-worthy patches from game-specific compatibility profiles. Propose a small sustainable architecture reducing per-game setup; avoid replacing simple fixes with a giant harness.
8. Verification audit: which fixtures execute production code vs mocks, what test claims don't prove, missing race/error cases, provenance/signing/rebuild reproducibility, license attribution and upstream submission prerequisites.

## Deliverables

First confirm the exact repository commit and files actually read. Then deliver:
- Prioritized real findings (severity, file/function/line, failure mechanism, reproduction, minimal repair, regression test). Label hypotheses separately.
- Top10 next experiments ordered by information gain/cost/risk, with expected discriminating outcomes, instrumentation and stop/rollback conditions.
- A concrete next development wave with independent workstreams, dependencies, integration boundaries and acceptance criteria. Preserve the working new DS3 build and game data.
- A fixed-scene measurement protocol: same resolution/quality/caps, repeated warm runs, optional preserved-cache first run, foreground state, load/thermal/power, observer overhead, per-frame metrics when actually available. Define what would demonstrate a real improvement and what cannot.
- Code sketches or patch suggestions only where source evidence supports them; identify unverified APIs and version assumptions.
- Challenge our premises: what should we stop doing, what is upstream already, what is likely wasted effort?

Use official/upstream primary sources when researching APIs or existing fixes, cite exact links/versions. Seek counterexamples and regressions before recommendations. Prefer a thorough grounded report over cheerleading or a generic optimization checklist. Do not execute deployments or publish anything; this request is audit/advice only.
