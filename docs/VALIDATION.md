# Validation

## Hyprland development environment

The owner uses Hyprland and continues other work while Codex develops. Tiling may allocate half-screen, quarter-screen, or another size instead of ChronoLedger's requested/default dimensions. This sizing alone is expected compositor behavior. Evaluate the UI within its actual allocation; investigate genuine clipping, overlap, or unusable controls.

If a check needs exclusive screen space or additional windows arranged by the owner (such as one or two terminals), ask for that setup and explain why. Preserve ongoing work and the current session. Continue independent tasks while waiting. Never close a production instance to make a test convenient; use isolated temporary data and application identity.

Run `./tools/check` after every change. Run `./tools/check memory` for ownership, persistence, or teardown changes covered by the deterministic targets. This enables ASan, UBSan, and leak detection. If the environment cannot run LeakSanitizer, record that limitation explicitly; disabling leak detection only validates ASan/UBSan, not leaks.

The core target covers malformed-input preservation, strict JSON boundaries, schemas 1–6 migrating to schema 7, fractional pause/switch/save/expiry arithmetic, maximum integer durations, completed-index invalidation after deletion and copying, 10,000-task snapshots, Calibration bounds and hit regions at density boundaries, stacked Focus content/action clearance, and bounded scene-history retirement and elapsed-time decay. The controller target uses completed GTK dialog tasks and retained GObjects to cover recovery mutation blocking, failed replacement rollback, explicit import, preference preservation, and cancellation versus real dialog errors. Neither target creates native windows or exercises GL context teardown.

For display-backed checks, keep the production session running. Setting `CHRONOLEDGER_FILE` selects a non-unique development instance so activation cannot be forwarded to the personal instance. Use a temporary ledger:

```sh
validation_dir=$(mktemp -d)
CHRONOLEDGER_FILE="$validation_dir/ledger.json" ./build-agent/chronoledger
```

Check these behaviors on a real display:

| Exercise | Expected result |
| --- | --- |
| Create, engage, pause, resume, and complete an allocation | Timing and the progress arc agree through each transition. |
| Complete allocations in a different order from creation | Recent completions appear first; deleting a row removes that allocation. |
| Resize to 560×360 and across compact/full boundaries | Calibration controls fit above the status message, labels remain readable, and button centers activate the visible control. |
| Submit an empty title or invalid estimate | A persistent explanation appears; editing or a successful commit clears it. |
| Select a profile and immediately adjust a slider | The manual slider value remains after the transition duration. |
| Hover a control, then leave the window or move onto blank space | Both the cursor tail and obsolete hover corners disappear after their finite decay. Repeat at Balanced and maximum Persistence; after the deadline (at most 6.45 seconds from the last accepted scene change) and cleanup frame, scene output matches a clean-history render and CRT updates stop. Cursor-only movement must not extend scene history. |
| Resize Focus to 760×650 and across the short/stacked and form thresholds | Selected ready, paused, active and expired task titles, metadata, dial/ticks, and action row remain separate. The new-allocation form appears only when there is room. |
| Open import/export, then Cancel or close the chooser | No failure alert appears. Actual file errors still produce an alert. |
| Close while a file dialog is pending | Callback completion is harmless and scheduling sources are removed. |
| Background or minimize an active timer | Expiry and checkpoints continue; returning restores rendering. |
| Start with malformed JSON at the temporary data path | Recovery is visibly read-only, mutation controls are disabled, and closing preserves the original bytes. |
| Import valid data during recovery, including a forced write failure first | Failure preserves the document and lock; success unlocks editing and preserves local preferences. |

For renderer-loss lifecycle changes, use a development-only harness on a real display: start an allocation, make the GL area current before releasing its renderer resources, refresh power policy, and keep the main loop running. Verify that the timer checkpoints and expires without a renderer, then close with a pending chooser. Run this harness with ASan/UBSan as well as a normal build. Do not add fault-injection switches or test callbacks to production code. Check both desktop GL and GLES when changing shader or context behavior; a single successful driver run does not establish portability.

For routines, test interval/daily creation, title and schedule editing, enable/disable, acknowledgment and deletion; inspect long titles at the minimum supported allocation. Acknowledge after several missed intervals and verify exactly one next due timestamp. Restart with an overdue routine and with a disabled routine. Check F1–F4 while editing and verify digits remain ordinary text. Close/reopen with each profile, manual sliders, palette and AA changes, and verify selected tab restoration. Recovery close must preserve malformed bytes and import must retain local routines and preferences.

## Recurring tasks implementation verification (2026-11-15)

Normal deterministic checks pass (29 core, 3 controller subtests), including interval coalescing/disabled due predicates, daily midnight scheduling, local routine/presentation serialization, malformed local-state rejection and minimum-size routine control bounds. ASan/UBSan with leak detection passes outside the sandbox; the sandbox alone cannot run LeakSanitizer's process inspection.

A temporary development harness reuses the production controller, native GTK routine editor, Cairo scene and GL renderer with isolated data and identity. On the real Hyprland allocation (938×1021), create/edit, disable/enable, acknowledge, F4 selection, digits not selecting tabs, normal close and saved tab/shader/routine restoration passed. The final framebuffer was inspected. This is signal-driven controller coverage, not physical keyboard/mouse input. Full suspend, every timezone/DST transition, audible delivery and future-login startup remain untested. No shader or GL context implementation changed.

The same real-display editor/close harness also passed under ASan/UBSan (`detect_leaks=0` for the native GUI run; deterministic leak detection remained enabled). Release build/tests passed before atomic promotion. Desktop-file validation and Lua syntax validation passed; the session was not restarted. Configuration backups and the pre-migration executable were retained. Test artifacts/logs are under `/tmp/chronoledger-fixes/` and `/tmp/chronoledger-promote.log`.

## UI and Calibration verification (2026-11-15)

Warning-strict checks pass with 30 core and 6 controller subtests. Additional regressions cover all Calibration controls scrolled into view at density boundaries, a separate row for the five new schemes, five-slider compatibility and six-slider/palette round trips, rejected out-of-range curvature, live shader-equivalent pointer mapping across the curvature range, manual profile interruption and panel/native completion adaptation.

A temporary development harness uses isolated JSON files and application identity on the real Hyprland display. Its ordinary allocation was 464×502 (the existing below-minimum notice is expected); constrained test windows exercised 960×800 and the supported minimum 560×360. No production process was closed. Final framebuffer images were inspected for the five new schemes, Mask at 0/50/100%, disabled Mask, maximum curvature and scrolled Calibration. The added schemes occupy a separate row of five at ordinary widths; minimum width reflows them to four plus one. Mask produces clearly increasing aperture contrast. On the unchanged header/profile region, disabled Mask is pixel-identical to zero; mean absolute RGB changes across that region are 2.19 at 50% and 4.03 at 100% (including its black pixels).

Display-backed signal-driven checks passed routine empty-title validation, interval/daily save, removal cancellation and confirmation, F-key isolation, Tab traversal, Escape dismissal, initial Cancel focus, input re-enabling, normal close with an editor open, saved settings reload, timer expiry behind a panel, and expiry after releasing renderer resources with its GL context current. The native panel remains available after renderer loss. GTK snapshot images confirm that compact panels keep actions visible while their bodies scroll.

Shaders ran on desktop OpenGL 4.6 and OpenGL ES 3.2 on the session's RTX 3060. The GTK/GL harness also passed under ASan/UBSan with `detect_leaks=0` for the GUI/driver run; deterministic memory checks use leak detection outside the sandbox because sandboxed process inspection prevents LeakSanitizer from running. Artifacts and logs are under `/tmp/chronoledger-fixes/` (`sized`, `small`, `small-gl-final`, `gl-complete`, and `small-asan-complete`), with normal checks at `/tmp/chrono-check.log` and final memory/release results at `/tmp/chrono-promote.log`.

Limits: these are controller/widget-signal checks and framebuffer inspection, not physical mouse/keyboard automation. Other GPU drivers, full suspend/resume, audible notification delivery and interactive portal file-picker operation were not re-tested in this change. Native file selection stays unchanged; cancellation/error handling remains covered by controller tests. Native panel text and controls deliberately stay outside scene curvature, persistence and mask to retain reliable GTK editing and failure reporting; palette and glow follow the selected presentation.
