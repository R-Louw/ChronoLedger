# ChronoLedger correctness audit

Date: 2026-11-15. Scope: the complete current first-party tree, including the uncommitted implementation, tests, shaders, build scripts, CI, documentation, and desktop resources. The original audit made no production changes. The follow-up below records the two authorized corrections; original finding evidence and original validation results remain historical.

## Readiness

Both confirmed rendering/interaction findings are now resolved in the current working tree. Compact Focus reserves content and action space; scene phosphor history decays for a bounded interval and receives a final clean frame. Neither original finding demonstrated lost ledger data or incorrect persisted elapsed time. No high-severity data-loss, memory-access, recovery, or timer-arithmetic defect was confirmed.

The model, recovery transaction, and renderer-independent timer have substantial positive evidence, including deterministic checks and a real-display renderer-loss exercise. The follow-up regression evidence supports proceeding with related feature work. It does not establish complete interactive or graphics portability coverage: desktop GL, the wider manual checklist, and a clean whole-GUI leak result remain verification gaps.

Severity is judged for a personal, single-user instrument interface: P1 would mean serious data loss, timing failure, or crash; P2 means a reproducible functional defect worth correcting before related feature work. Intentional phosphor styling, hidden controls below their supported layout size, and enterprise/concurrent-use requirements are not findings.

## Findings, ordered by practical impact

### F01 — P2: compact Focus actions overlap the selected task title and timer

**Disposition: fixed.** See the implementation follow-up for changes and verification. The trigger, coordinates, and evidence below describe the original audited tree.

**Locations:** [src/ui_layout.c:143](../src/ui_layout.c#L143), [src/ui_layout.c:218](../src/ui_layout.c#L218), [src/ui_layout.c:270](../src/ui_layout.c#L270); text placement at [src/ui_focus_view.c:20](../src/ui_focus_view.c#L20), [src/ui_focus_view.c:102](../src/ui_focus_view.c#L102), and button painting at [src/ui_focus_view.c:140](../src/ui_focus_view.c#L140).

**Trigger / reproduction:** In an isolated instance with temporary data, select an open allocation such as “Review timing and persistence”, then resize the Focus content to **760×650 logical pixels**. This is a supported compact size, above the 560×360 minimum. The same composition is used for ready, paused, and active allocations.

**Impact:** The task title runs across COMPLETE and the extension controls; ENGAGE/HOLD also intrudes on the timer text. The core timer screen becomes visually ambiguous even though the individual button hit regions remain valid.

**Evidence and execution path:** An offscreen Cairo/Pango reproduction linked the current production drawing code, constructed a valid selected 25-minute task and snapshot, and rendered the complete CPU scene at 760×650. Visual inspection confirmed the overlap. The computed stacked chamber is 212 pixels high; details start at `y=186` with height `106`. The title is centered at `y=252`, while the action row occupies **y=250–292**. The timer is centered at `y=240`, immediately against that row. The action placement uses `details.y + details.height - 42`, while the view independently assumes fixed vertical text offsets. Shrinking the chamber to retain the queue and allocation form therefore violates the text/action spacing requirement. This is not merely an ellipsized long title or an out-of-range window size.

**Focused correction:** Reserve separate vertical space for the dial/text and actions in the stacked composition. When that minimum cannot fit alongside the form and queue, choose a smaller composition or hide/defer the form according to available height. Coordinate the text and action layout rather than moving one label independently.

**Verification:** Render selected ready, paused, active, and expired tasks at 760×650 and near the form/composition thresholds, including short and 200-character titles. Check text/dial bounds against action bounds and inspect the rendered scene on a display. Existing layout tests only prove density and Calibration/control geometry; they do not detect this text overlap. A supplementary button-bound/hit-center sweep across all three modes also passed, illustrating that gap.

### F02 — P2: scene phosphor history can freeze obsolete hover highlights

**Disposition: fixed.** See the implementation follow-up for changes and verification. The execution path and pixel differences below describe the original audited tree.

**Locations:** render scheduling at [src/app_runtime.c:479](../src/app_runtime.c#L479); cached CRT gating and elapsed-time clamp at [src/renderer.c:59](../src/renderer.c#L59); decay at [src/renderer_effects.c:406](../src/renderer_effects.c#L406); retained emission at [data/shaders/persistence.frag:19](../data/shaders/persistence.frag#L19). Hover is part of the composed scene at [src/renderer_scene.c:447](../src/renderer_scene.c#L447). The pointer-exit guard is [src/app_input.c:142](../src/app_input.c#L142).

**Trigger / reproduction:** Leave an idle, timer-free Focus view visible. At the advanced Calibration controls, set Persistence high, return to Focus, hover a mode button, then move out of the window and leave it idle. Also reproducible, more faintly, with the default Balanced persistence value.

**Impact:** Corners belonging to a departed hover can remain in the cached picture until another scene invalidation. At high persistence they visibly suggest a stale hover state. This is a frozen remnant rather than the intended finite phosphor decay; no ledger or timer data is affected.

**Evidence and execution path:** A display-backed harness used the real GTK motion controller's `leave` signal and production renderer on NVIDIA GLES 3.2. It suppressed unrelated message expiry, sampled the final FBO after pointer exit, waited 2.1 seconds, and sampled again. The pixels were identical and `last_crt_update_us` had not advanced. Requesting only a history reset, with the same clean scene, changed 3,141 color channels near the hovered button, with a maximum difference of **50/255** at Persistence=1.0. The default-value run showed a maximum difference of **7/255** in that region.

The exit callback correctly removes hover geometry and clears the independent cursor history. It does **not** retire the main scene history. That history retains the previous bright corners. Once `final_scene_valid` becomes true, cursor-only frames reuse the CRT cache, and there is no scene-history decay/cleanup condition in the scheduler. Even a later scene update caps the decay interval at 0.1 seconds, so it cannot age history by the actual idle interval. These guards explain why the recent hover invalidation fix does not fully satisfy the documented “hover corners disappear” check.

**Focused correction:** Give scene history an explicit finite retirement policy. Either keep a bounded decay/cleanup sequence that invalidates the final cache until old scene emission is gone, or exclude/reset transient hover history when its geometry is removed. Apply real elapsed time to persistence decay, independently of the animation-step clamp. Preserve the event-driven idle behavior after cleanup.

**Verification:** With no active timer or transient message, test hover→leave and hover→empty space at default and maximum persistence. After the defined tail, compare the output against a clean-history render and verify rendering returns to idle. Test cursor movement during that interval too: cursor presentation alone must not leave the main CRT history frozen. Repeat on desktop GL when a context is available.

## Original audited revision and preserved work

- HEAD: `21da76eccafc00a41fe19ac9d47d521a02df658e` — “Remove stale code and narrow helper scope”, committed 2026-08-13.
- The audited implementation is the working tree, **not HEAD alone**: 26 existing paths were modified at entry. They cover the model, persistence, controller/callbacks, runtime policy, snapshots, UI/layout, renderer geometry, build definitions, tests, README, DESIGN, and CHANGELOG.
- Eight pre-existing untracked paths were included: `docs/VALIDATION.md`, `src/chrono_json.c`, `src/chrono_json.h`, and `tests/test_controller.c`, `tests/test_layout.c`, `tests/test_ordering.c`, `tests/test_persistence.c`, `tests/test_timing.c`.
- SHA-256 of `git diff --binary HEAD` before adding this report: `20eb0384335186ea8686b0b0ca160eadf8b0f8cd4d6a66023eb5e29a039ac1b0`.
- All 97 original first-party paths were fingerprinted; their contents remained unchanged during the audit. The only intentional repository addition is this report. Temporary harnesses, images, builds, logs, and ledgers are under `/tmp/chronoledger-audit/`; ordinary checker products remain in the ignored build directories. Runtime harnesses used distinct non-unique application IDs, avoiding activation of a personal production instance.

## Original validation and environment

Host: Arch Linux x86_64, Linux 7.2.14-arch1-1, GCC 16.2.1, Meson 1.12.1, Ninja 1.13.2. Current installed libraries report GTK 4.24.1, GLib 2.90.1, JSON-GLib 1.10.8, epoxy 1.5.10, Cairo 1.18.6, and PangoCairo 1.58.2. The pre-existing Meson dependency caches initially reported older GTK/GLib versions; the additional fresh release build used the current environment.

| Check | Result and scope |
| --- | --- |
| `./tools/check` | Passed: warning-strict debug application build; 25 core and 3 controller subtests. |
| `./tools/check memory` | Initial sandbox run could not run LeakSanitizer because of its runtime/ptrace restriction. Repeated outside that restriction with default leak detection enabled: **all 28 subtests passed**, no ASan/UBSan/LSan report. A leak-disabled sandbox rerun also passed, but is not the basis for the leak result. |
| Fresh release build | Passed with `--buildtype=release -Dwerror=true`, plus both test targets / 28 subtests. Initial attempt hit a read-only ccache directory; rerun with `CCACHE_DISABLE=1` succeeded. |
| Shader compilation/linking | glslangValidator 16.6.0 accepted all **16 program pairs**: eight fragment programs with their matching vertex source under both production desktop GLSL 130 and GLES 300 ES preambles. This is compiler validation, not driver execution. |
| Real-display GLES | Wayland, GTK 4.24.1, NVIDIA GeForce RTX 3060, driver 615.78.08, GLES 3.2. Production shader compilation/linking, required uniform discovery, PBO uploads, half-float AA targets, rendering, and close/unrealize ran successfully. F02 was reproduced through this pipeline. |
| Renderer-loss lifecycle | Normal and sanitizer harnesses make the GL area current, discard renderer resources during an active temporary allocation, refresh policy, and continue the main loop. Background maintenance persists elapsed time and expiry, then removes its source. The checkpoint deadline was shortened in harness state to exercise the callback without waiting 30 seconds; the production interval was not modified. Renderer recreation and subsequent rendering also succeeded. Both normal and ASan/UBSan follow-ups closed with a chooser pending and emitted Cancel into the harness's own native chooser without an invalid-access/UB failure; final parent release was not observed. |
| GUI sanitizer/leak distinction | The leak-enabled GUI harness completed its behavioral assertions without an invalid-access/UB report, then exited with **907,916 bytes / 25,766 allocations** reported by LSan. Direct allocation roots were Fontconfig, GTK, NVIDIA allocator, and libdbus. This is not a clean GUI leak pass or a confirmed first-party leak. Follow-up ASan/UBSan execution disables leak detection; it does not establish leak freedom. |
| Layout and CPU drawing | Offscreen production Cairo/Pango scene reproduced F01. Supplementary control-bound, overlap, and hit-center sweep: all three modes, widths 560–2000 by 20 and heights 360–1100 by 10; no control-geometry failures. Text rendering is a separate check and failed at the documented Focus reproduction. |
| Desktop resources | `desktop-file-validate` accepted the launcher with a multiple-main-category hint. `appstreamcli validate --no-net` returned failure for a missing homepage warning, plus missing content-rating/developer-info notices. These concern catalog metadata, not this personal application's timer or persistence correctness. |

Desktop GL context creation failed on both Wayland and X11; the application displayed its diagnostic instead of rendering. Thus desktop driver execution is unverified despite successful offline shader validation. Other GPU vendors, the portable RGBA8 AA path, allocation-failure injection, actual system suspend/resume, and graphics resets were not dynamically exercised.

## Subsystem coverage and contract checks

| Subsystem | Review and evidence |
| --- | --- |
| Documentation and integration | Read applicable AGENTS, README, DESIGN, VALIDATION, changelog, and shader optimization notes. Reviewed all build/install/run/check scripts, CI YAML, resource manifest, launcher, AppStream, and icon. No external services or personal ledger were used. |
| Domain timing and ownership | Reviewed every model transition, validation boundary, copy/transfer path, elapsed arithmetic, completed ordering/index invalidation, preferences, document ownership, and duration parsing/formatting. Tests cover fractional pause/switch/save/expiry, maximum integers, duplicate IDs, failed transitions, completion cache lifetime, and 10,000-task snapshots. No arithmetic or borrowed-index failure was confirmed. |
| Persistence and recovery | Traced bounded reads, strict JSON syntax/member/numeric/Unicode/depth checks, schema migration, required task arrays, serialization limits, atomic local replacement, and import/export semantics. Verified recovery mutation guards and save-or-rollback controller paths against tests; failed import restores the prior ledger/lock, and successful import preserves preferences. Full export intentionally includes preferences; active records intentionally restore paused. |
| GTK/controller lifecycle | Reviewed all async records and finish functions, window-owned state, closing guards, signal lifetimes, tick removal, and the standalone timeout. Callbacks retain windows/records and resolve state on completion; no raw-state async retention was found. The deterministic controller target creates completed GTasks rather than native windows. Real-display close/unrealize and renderer-loss scheduling add coverage beyond it. |
| Snapshots and renderer boundary | Traced synchronous capture and consumption of borrowed task strings and copied timing/preferences through UI and renderer. No retained snapshot pointer or render-time model mutation was found. Gauge geometry uses the same focus task as text, including paused selection. |
| UI/input | Reviewed all modes, drawing primitives, responsive layouts, hit testing, keyboard/pointer translation, slider/profile interruption, recovery disabling, validation messages, scrolling, and row deletion resolution. Completion rows and deletion use the same model order. F01 is an interaction between individually valid control geometry and independently positioned content. |
| GL/shaders/cursor | Reviewed every shader and renderer pass, UV/channel conventions, target allocation/commit paths, upload bounds, PBO fallback, history resets, cursor sampling/damage/scissor handling, GL resource release, and scheduling/cache invalidation. F02 is the confirmed cache/history interaction. Native GLES and offline dual-API compilation do not establish other-driver portability. |

## Original verification gaps and unverified concerns

The full static scope is accounted for. Dynamic coverage is deliberately narrower:

- The entire manual table in `docs/VALIDATION.md` was not completed as physical keyboard/mouse interaction. In particular, display-backed recovery import with forced write failure, export cancellation, completion-row deletion, profile/slider interruption, and an exact 560×360 native-window resize still need a manual pass. Existing deterministic tests/static paths cover much of their logic, but do not substitute for that pass.
- The pending native chooser retained its parent beyond close. Follow-up normal and ASan/UBSan runs emitted Cancel into the harness's own `GtkFileChooserDialog`, but the parent weak-finalization callback still had not run one second later. Completion and final release were not established. The source explicitly owns the parent while a result is outstanding and guards callbacks with `closing`; this observation alone does not establish a first-party leak or use-after-free. Verify actual chooser completion and final object release before claiming full callback teardown coverage.
- Whole-GUI leak freedom remains unverified. The observed exit leak roots are in libraries, and the harness initially exits with a chooser pending. A fully completed-dialog run and a minimal GTK/Pango baseline would be needed to attribute residual leaks. Do not infer a ChronoLedger ownership defect from those exit totals alone.
- Notification delivery, audible expiry, minimize/restore with an active timer on the actual compositor, and system suspend were not end-to-end tested. The independent background callback and expiry persistence were tested directly on a real display.

The original recommendation was to resolve F01 and F02 and obtain the missing interaction/portability evidence. The corrections and new verification are recorded below; broader gaps remain as qualified there.


## Implementation follow-up — 2026-11-15

The user authorized implementation, regression tests, and documentation changes after the original audit. Both findings were reconfirmed against the current code before editing. Unrelated pre-existing work and personal ledger data were preserved. All runtime checks used distinct application IDs and ledgers under `/tmp/chronoledger-fixes/`; development harnesses stayed outside production code.

### F01 correction and verification

[src/ui_layout.c](../src/ui_layout.c) now budgets at least 300 logical pixels for the stacked chamber and 116 for its queue. Shorter tiles use the existing side-by-side composition, and the allocation form appears only when its additional height fits. The stacked dial is centered above the action band, its tick extent is accommodated, and the details rectangle ends above the action row. Desktop and minimal compositions retain their existing drawing rules.

The regression `/layout/stacked-focus-content-clearance` in [tests/test_layout.c](../tests/test_layout.c) fails against the original implementation (insufficient content height), then passes with the fix. It sweeps compact widths and heights one logical pixel apart around composition/form transitions, checking content clearance and gauge ticks against the action row. The wider control-bounds/hit-center sweep across all modes also reports zero failures.

Offscreen production Cairo/Pango renders cover ready, paused, active and expired selections, with ordinary and 200-character titles. At 760×650 the corrected action row is y=418–460, below the content; the overlapping original row was y=250–292. Display-backed GLES captures at **760×650, 760×608** (minimum stacked chamber height), and **560×360** were inspected and show separated text, gauge and controls. The form is intentionally deferred when its height would compromise that clearance.

### F02 correction and verification

[src/render_policy.c](../src/render_policy.c) defines a finite scene-history deadline and exponential decay from real elapsed time. [src/renderer.c](../src/renderer.c) charges that deadline only when a changed filtered scene has history to retire. [src/renderer_effects.c](../src/renderer_effects.c) invalidates only the final CRT cache during decay, resets history for the cleanup frame, and clears the deadline on explicit resets/disabled persistence. Target replacement and resource teardown discard the old lifetime too. [src/app_runtime.c](../src/app_runtime.c) advances this policy on native ticks; a non-interactive draw settles history immediately because that scheduling mode has no native tail ticks. No standalone source, asynchronous callback, model mutation, or persistent setting was added.

The tail lasts ten half-lives: about 0.61 seconds at default persistence and at most 6.45 seconds after the accepted scene change, followed by its cleanup frame. Decay-only work uses the existing 12 Hz progress cadence, preventing a running allocation from turning it into continuous native-rate CRT work. Cursor-only and decay-only frames do not extend the deadline. Current scene presentation and the existing phosphor shaders remain intact; full-window work returns to idle after cleanup.

Two new deterministic tests in [tests/test_core.c](../tests/test_core.c) verify finite retirement, one-time cleanup, decay cadence, idle behavior, disabled persistence, timestamp saturation, and decay equivalence across elapsed-time partitions including a two-second idle gap. This specifically prevents applying the former 0.1-second animation clamp to persistence.

A real-display pixel harness exercised hover→leave at default persistence and hover→blank space at maximum persistence, with additional cursor movement during the tail. It verified the deadline did not recharge, compared the settled final FBO against an explicitly clean-history render (**zero differing channels, maximum difference 0/255**), then confirmed `last_crt_update_us` stopped advancing. These checks passed in normal and ASan/UBSan builds. The final cadence observer also verified at least 83,333 microseconds between decay-only CRT updates, with cleanup exempt from that delay. Final runs used a fixed-size GL child, waited for accepted frames, and held foreground policy through a test-only linker wrapper: host focus/tiling changes and sampling a pending render had disrupted earlier harness attempts. GTK event controllers, the native frame clock and the GL driver remained real; this is controlled scheduling evidence rather than a physical mouse/focus test. Direct delivery of a non-interactive render also verified immediate settlement. The original audit measured stale-image differences of 7/255 and 50/255 for the corresponding persistence settings.

### Follow-up validation and limits

- `./tools/check`: passed, **28 core + 3 controller subtests**. This includes the three new regressions. The deliberately failing pre-fix Focus run is retained as regression evidence.
- `./tools/check memory`: passed with **ASan, UBSan and leak detection enabled**, outside the sandbox runtime restriction identified in the audit.
- GUI history checks passed with ASan/UBSan and `detect_leaks=0`. They do not establish GUI leak freedom; the original library-rooted LSan exit reports remain a limitation.
- Normal and ASan/UBSan display harnesses exercised renderer resource loss during a temporary allocation, background checkpoint/expiry, recreation, and close with a pending chooser. Both follow-ups also observed final parent-window release. This supplies additional teardown evidence, rather than a guarantee for every chooser backend.
- Environment remains the audit's NVIDIA GLES 3.2 / GTK 4.24.1 display. Desktop GL was retried on Wayland and GTK could not create that context (check skipped with status 77); desktop driver execution remains unverified. The broader manual/other-driver checks retain the original limitations above. No shader bodies or context-selection code changed.

DESIGN, VALIDATION, and CHANGELOG were updated to describe the corrected layout budget, finite history behavior, and focused checks. Both audit findings are closed; the remaining portability, full manual-interaction and GUI leak limitations are not silently treated as passes.
