# ChronoLedger design baseline

This document describes the production architecture after removal of test, sample-data, profiling, benchmark, and audit scaffolding. It is intentionally limited to live application behavior so that subsequent refactoring starts from explicit boundaries.

## Runtime structure

ChronoLedger runs as one GTK application with one `GtkGLArea` and an invisible native entry used for text input. GTK object access, task mutations, persistence callbacks, and rendering coordination remain on the main thread.

The visible interface is split into four modes:

- **Focus** manages open allocations and the active timer.
- **Ledger** summarizes completed work and exposes import, export, and clear operations.
- **Routines** is a bounded acknowledgment queue, independent of timed allocations.
- **Calibration** controls presentation and renderer settings.

## Module boundaries

### `main.c`

Owns application construction and the highest-level lifecycle only:

- application and window creation
- document startup and recovery initialization
- top-level save/checkpoint coordination
- subsystem installation and teardown
- startup migration and diagnostics

GTK event translation is delegated to `app_input.c`; dialogs and transactional destructive operations are delegated to `app_dialogs.c`; frame scheduling, monitor state, power policy, and GL callbacks are delegated to `app_runtime.c`. `main.c` should remain an orchestration boundary rather than accumulating view, renderer, or domain policy.

### UI modules

`ui.c` is the UI façade. It coordinates one frame preparation, exposes invalidation, and maps preparation failures to stable diagnostic text.

`ui_scene.c` owns Cairo surface allocation/release, static-layer construction, text-mask composition, and dynamic scene composition. Mode-specific drawing is delegated to `ui_focus_view.c`, `ui_ledger_view.c`, and `ui_calibration_view.c`.

`ui_painter.c` owns reusable Cairo/Pango primitives, typography, buttons, fields, sliders, toggles, status rendering, and task display formatting. `ui_input.c` owns window movement/resizing and translates typed control hits into application actions.

The compact stacked Focus chamber reserves separate content and action bands. It needs at least 300 logical pixels plus a 116-pixel queue and their gap; shorter tiles use the side-by-side composition. The allocation form is shown only when it also fits that budget. Dial placement includes the gauge ticks outside its nominal box.

`ui_layout.c` is a pure layout policy module. It rebuilds a complete `UiLayout` from logical dimensions and reports only whether the density class changed; it does not access GTK objects, document state, renderer state, or `AppState`.

`cursor_state.c` owns inverse pointer mapping, bounded timestamped cross resampling and pruning, and analytic phosphor decay. It accepts only scene, input, settings, preferences, and the opaque renderer operation needed to clear cursor damage.

`shader_profile.c` owns presentation defaults and profile interpolation. It mutates only `AppPresentationState` and reports which renderer/UI invalidations the controller must apply.

### Application/controller modules

`app_input.c` installs GTK event controllers and translates keyboard, pointer, scroll, and queued motion into typed application operations. `app_dialogs.c` owns import/export alerts and destructive confirmation flows, including save-or-rollback ledger replacement. `app_runtime.c` owns monitor refresh tracking, frame-clock and low-power scheduling, GL realize/unrealize/render callbacks, and runtime progress updates.

`AppState` is a lifetime aggregate rather than a flat shared-memory structure. It groups GTK shell objects, presentation/layout state, document/session state, CPU scene caches, pointer state, frame scheduling state, and one opaque renderer handle. Common value types live in `app_types.h`; controller, UI, and renderer APIs are separated across `app.h`, `ui.h`, and `renderer.h`.

Modules receive narrower state where a full aggregate is unnecessary. Snapshot construction accepts only `AppSessionState`, while control hit testing accepts only presentation and session state.

### `app_actions.c`

Owns user-command execution. Keyboard and pointer inputs construct the same typed `AppAction` values, so model mutations, persistence, selection, annunciators, and calibration changes have one implementation path. Action payloads are validated before execution and stale task identifiers are rejected.

Protected recovery rejects ledger mutations, allocation editing, and persisted preference changes at this command boundary. Navigation, import/export, and transient presentation adjustments remain available. Destructive store replacement also checks the lock; only explicit import temporarily releases it during the synchronous save-or-rollback transaction. Recovery and form validation messages remain visible independently of transient annunciator expiry.

### `ui_controls.c`

Owns typed control hit testing and highlight metadata. `UiControlRef` replaces numeric token ranges and carries a control kind plus an optional row or option index. Hit regions are nonoverlapping; slider targets remain at least 24 logical pixels high without stealing the edges of adjacent controls.

### `app_view.c`

Builds one immutable value snapshot for each synchronous render dispatch. The snapshot captures the frame monotonic timestamp, local preferences, active/focus state, visible queue and ledger rows, completed statistics, and the bounded ledger chart window. UI and renderer code therefore consume one coherent model view instead of rescanning mutable task state at different times during the same frame. Scalar values and preferences are copied; task identifiers and titles are borrowed from opaque tasks and are valid while the owning document remains unchanged on the GTK main thread.

Completed rows use the model's completion order, also used to resolve row deletion. The chart takes the latest bounded completions and displays them chronologically. Gauge geometry consumes the same focus task as the text, including a paused selected allocation.

### Renderer modules

`renderer_context.c` owns renderer allocation/destruction, GL context realization, program compilation/linking, uniform discovery, render-target lifetime, and context teardown. The concrete `Renderer` representation remains private to the renderer subsystem through `renderer_state.h`; application and UI code use only `renderer.h`.

`renderer_shaders.c` loads GLSL from the compiled application `GResource` and adds the appropriate desktop OpenGL or GLES version preamble. Shader bodies live under `data/shaders/` rather than in C string literals.

`renderer.c` is the render-graph orchestrator. Individual passes are separated by ownership and failure domain:

- `renderer_upload.c` uploads the prepared Cairo scene and text mask through bounded direct/PBO paths.
- `renderer_scene.c` builds semantic geometry and produces the composed scene target.
- `renderer_cursor.c` owns cursor damage, cross geometry, AA, analytic afterglow, phosphor, and final cursor compositing.
- `renderer_effects.c` owns selective scene AA, persistence history, final CRT caching, and presentation.
- `renderer_support.c` contains small bounded numeric and GL helpers shared only inside the renderer subsystem.

`renderer_internal.h` defines the renderer-private frame context and pass contracts; it is not an application API.

Each draw receives one immutable `RendererFrameInput` assembled by `app_runtime.c`. The contract contains borrowed const views of presentation, CPU scene, cursor input, and the frame snapshot, plus copied timing, upload, and hover metadata. The renderer does not invoke UI construction, inspect GTK widgets during drawing, display application diagnostics, or mutate borrowed application state. It returns a typed status and limited commit metadata to the controller. Renderer-owned CRT timing remains private and is reset explicitly when interactive scheduling restarts.

GPU dimensions and framebuffer state are committed only after successful allocation and completeness checks. Cached dimensions therefore describe valid resources, not attempted allocations.

### `chrono_model.c`

Owns task and store invariants. Both `ChronoTask` and `ChronoStore` are opaque outside the model implementation; callers inspect tasks through read-only accessors and enumerate stores through bounded accessors rather than reading backing storage or task fields. Public mutations validate the complete store before changing it and leave the store unchanged on failure. Persistence receives one private, validated adoption path so maximum-size imports remain linear.

Important invariants include:

- IDs are nonempty, bounded, and unique.
- Titles are valid UTF-8 and bounded.
- At most one task is active.
- Allocation arithmetic is nonnegative and internally consistent.
- Active timestamps are positive and monotonic operations cannot regress.
- Completed timestamps do not predate creation.
- Task status agrees with remaining time, active state, and completion time.

Elapsed time consists of whole seconds plus a remainder in `[0, 999999]` microseconds. `allocated_seconds - elapsed_seconds == remaining_seconds` continues to hold; the remaining count includes a partially consumed second. Exhausted tasks have a zero remainder. Interval arithmetic splits seconds and fractional microseconds before addition so maximum-sized allocations never need to be multiplied into a microsecond integer. Copies, task switches, and serialization preserve the remainder; completed statistics carry fractional sums into whole seconds before applying saturation.

The store also owns a lazy index of borrowed completed-task pointers, ordered by completion time descending, creation time descending, then ID ascending. Adding, completing, removing, or clearing tasks invalidates this index before any owned task is freed. Rebuilding the index does not change task values or insertion order and runs on the same thread as store access. Cursor frames reuse the index while the ledger is unchanged.

### `chrono_preferences.c` and `chrono_document.c`

`ChronoPreferences` owns notification and presentation choices independently from ledger records. `ChronoDocument` is the application-level ownership boundary that contains one consistent `ChronoStore` and one preference value. Store exchanges validate the replacement before ownership transfers, allowing save-or-rollback transactions without exposing the model container.

### `chrono_persistence.c`

Owns schema migration, strict validation, bounded I/O, and atomic replacement. Normal startup and explicit import use the same strict task parser. Invalid local data is not silently reduced and is not overwritten by the automatic save path. The import API returns ledger records only, which makes preservation of local preferences an API contract rather than a caller convention.

`chrono_json.c` validates JSON syntax, UTF-8 and escapes, decoded member uniqueness, integer range, finite floating-point range, and a 128-container depth limit before JSON-GLib builds objects. It rejects embedded NUL characters before strings reach C-string APIs. Every supported schema requires a `tasks` array. Schema 7 stores `elapsed_remainder_us`; schemas 1–6 migrate with a zero remainder.

Legacy compatibility is limited to recognized persisted fields. Bundled sample records from older schemas are discarded during migration and are never written by the current application.

### Policy modules

`power_policy.c` decides whether rendering is interactive, background-clock only, or asleep. An active allocation always retains background maintenance when interactive rendering is unavailable, including after renderer failure or GL unrealize. Graphics readiness gates rendering, not expiry, notifications, or checkpoints. Closing stops both scheduling paths, and callbacks cannot restart them once the window is closing. `render_policy.c` decides whether a frame is required and limits progress-only invalidation to meaningful visual steps.

## Ownership

- `AppState` owns explicit shell, presentation, session, scene, input, and runtime substates plus one opaque renderer handle attached to the window lifetime.
- `ChronoDocument` owns one consistent `ChronoStore` plus local `ChronoPreferences`.
- `ChronoStore` privately owns opaque `ChronoTask` objects through a `GPtrArray` free function.
- `AppViewSnapshot` borrows task strings only for one synchronous main-thread render and copies every mutable scalar and preference value it exposes.
- `chrono_store_add_task()` transfers ownership only on success.
- Dialog callback records own any duplicated IDs or imported stores until the callback resolves.
- Ledger replacement candidates are consumed by one controller transaction that saves, commits, or restores the previous owned store.
- Cursor motion and cross-sample buffers are fixed-size arrays embedded in `AppInputState`; `cursor_state.c` mutates them synchronously and no producer retains access outside the main thread.

## Persistence behavior

The primary file is resolved through `chrono_default_data_path()`. Writes use a bounded serialized representation and `GFile` replacement semantics. Save failures remain visible and the in-memory store remains available for retry.

If the primary file cannot be read or validated, ChronoLedger enters a protected read-only recovery state. Automatic saves and document edits are blocked until the user explicitly imports a valid replacement. A failed replacement restores the previous ledger and recovery lock. Import replaces only ledger records and preserves local preferences; export includes both as a complete backup.

## GTK presentation selection

Before GTK/GApplication initialization or worker creation, `main.c` applies a process-local `GSK_RENDERER=gl` default on Linux with runtime GTK >= 4.24, only when the variable is absent. Caller values are preserved verbatim. GTK >= 4.12 remains supported; earlier GTK runtimes and other platforms retain GTK selection. The startup log distinguishes the request's origin from the actual realized native GSK renderer. GTK owns realization fallback; the application does not swap presentation renderers or restart itself. Existing GL diagnostics and semantic maintenance continue to handle application rendering failure.

Rofi, Hyprland startup and direct execution share this executable-level policy. It introduces no persisted preference, shader change, new scheduling source or desktop environment change. Explicit `CHRONOLEDGER_FILE` launches retain non-unique behavior and use the separate `.Development` application identity. Environment renderer controls are GTK debugging interfaces and need revalidation after dependency upgrades.

## Rendering and invalidation

The application scene is event-driven rather than continuously redrawn. Static and dynamic dirty flags control Cairo reconstruction and GPU cache refresh. Once a frame is accepted, `main.c` captures one `AppViewSnapshot`, asks the UI layer to prepare CPU surfaces, then submits one immutable `RendererFrameInput`. Cairo construction and GPU geometry/compositing therefore receive the same timestamp and model snapshot. The renderer representation is private to the renderer subsystem; cache, CRT timing, and cursor-history state are mutated only through renderer functions, and UI/lifecycle code use renderer-owned predicates and commands. Render requests occur only while one of these conditions requires them; the interactive frame-clock callback itself remains installed even after draws settle:

- scene or cache invalidation
- pointer movement or finite cross-afterglow decay, including one final cleanup frame
- finite scene-history decay and cleanup
- presentation-profile transition
- active timer progress crossing a visible step

Scene changes with valid phosphor history charge a renderer-owned deadline of ten half-lives (about 0.61 seconds at Balanced persistence, at most 6.45 seconds). Decay-only frames run at the bounded 12 Hz progress cadence, reuse the filtered scene and CPU surfaces, invalidate only the final CRT cache, and never extend that deadline. Native cursor frames therefore retain the cached-base optimization even while a timer is running. The last frame resets history to remove quantized remnants, then full-window work returns to idle. Scene persistence uses real monotonic elapsed time rather than the animation-step clamp. Non-interactive draws reset history immediately because their scheduling has no native decay ticks. Context/resource release and explicit history resets discard the deadline.

The cursor uses raw window coordinates so it remains aligned independently of optional scene curvature; hit testing uses the matching CRT source lookup. Its four-arm mint cross is the only deposited shape. There is no centerline stroke. `cursor_state.c` resamples queued input into at most 8192 timestamped cross positions, normally at most 1.5 logical pixels apart. A long input gap starts a newly charged sweep rather than interpolating unknown idle time. The latest raw position always supplies the live cross without position filtering.

Afterglow is reconstructed from absolute monotonic sample ages each rendered frame. Persistence controls both scene persistence and cursor half-life (`0.025 + 0.62 * amount²` seconds); zero bypasses afterglow. Samples expire after ten half-lives (up to 6.45 seconds). Stationary ticks never recharge samples. The renderer combines premultiplied cross emission with `GL_MAX`, so overlapping arms and reversals do not accumulate brightness with input or refresh rate. Only the live cross receives the dark contrast separator. AA and restrained phosphor styling follow on the independent post-CRT plane. There are no recursive cursor history textures or diffusion passes: output quantization does not feed future frames. This exchanges two full-resolution feedback textures and a temporal pass for bounded per-frame geometry work during motion/decay; the scene cache remains reusable. Bounded sample storage can shorten the oldest, faintest tail under extreme input/path pressure; live positioning remains independent of this capacity.

Cursor-only decay reuses the cached scene. The tick observes samples before and after pruning to request one final clean frame, then stops cursor-driven draws. Leaving, panels, focus/background transitions and context loss discard samples. Background motion does not accumulate a hidden path. When cursor shader effects are disabled, only the raw cross and its separator are composited, without afterglow, AA, glow, mask or vignette.

The native pointer is hidden only after a successful custom presentation in an interactive, unobscured GL area of at least 560×360 logical pixels. Startup, undersized layouts, inactive windows, panels, diagnostics and GL failure/loss retain the native pointer. Resizing or restoring focus uses the native pointer until a successful frame replaces it; compositor focus decisions do not force animation in inactive windows.

## Refactoring constraints

Refactoring should preserve these contracts:

1. Failed model operations are atomic.
2. Persistence never converts malformed input into a silently reduced ledger.
3. GTK and render-visible state are mutated only on the main thread.
4. Buffer capacities and upload sizes remain explicitly bounded before narrowing to GL integer types.
5. GL resource metadata changes only after successful allocation.
6. Source callbacks cannot outlive their owning `AppState` or GL resources.
7. User-visible success messages never mask persistence failures.
8. Presentation effects may be disabled without removing core functionality.
9. Ledger imports cannot replace local notification or presentation preferences.
10. A public `ChronoDocument` owns a valid store for its entire usable lifetime.
11. A render dispatch observes one captured model/preference snapshot and does not rescan live ledger state.
12. Task representation remains private to the model; persistence uses only the private validated construction seam.
13. The concrete renderer representation remains private to renderer-subsystem implementation files.
14. New module APIs should accept the narrowest applicable application substate.
15. Layout calculation remains independent of GTK, model, persistence, and renderer state.
16. A renderer draw consumes borrowed const frame inputs and reports effects through typed return values rather than mutating controller/UI state.

New tests and tooling should be developed outside the production target or in a clearly separated development layer rather than embedded in runtime state, UI controls, or renderer paths.

## Local routines and presentation persistence

`ChronoPreferences` contains bounded value-owned routine records (32, 256-byte UTF-8 titles), selected tab, profile and shader settings. Optional `local_state` in schema 7 is strictly validated; absent state uses defaults. Local routines follow the existing import-preserves-preferences/export-full-backup contract. Normal close and explicit export capture current presentation before serialization; routine commands save immediately. Geometry is compositor-owned and never persisted.

`app_routines.c` presents one routine per page through the shader-composited Cairo scene and uses the shared in-window GTK operator panel for editing and removal confirmation. Panel signal/tick records are bounded by the window widget hierarchy; asynchronous confirmation records retain the parent window and resolve its object data on completion. No asynchronous path retains a raw AppState pointer. Due deadlines use Unix seconds; intervals schedule from acknowledgment, daily deadlines use next local midnight, and no task occurrences are generated. One outstanding reminder collapses missed deadlines. Existing GTK main-thread maintenance handles due announcements, reusing GNotification with stable slot IDs; notification timestamps are transient per-process suppression state. Existing background source lifetime/closing rules apply while routines exist.

Scheduled confirmation extends a routine with a strictly validated optional `confirmation` object inside schema-7 `local_state`: minute of day (0–1439), window minutes (1–120), outstanding deadline (zero while waiting), and latched overdue flag. The existing `due` is always the original trigger; an outstanding deadline must equal `due + window * 60`. Legacy interval and midnight records omit the object and retain their behavior. Missing optional state uses existing defaults; malformed confirmation fields enter the same protected recovery path. Import remains ledger-only and preserves the complete local routine state; export includes it. Older binaries can read the document but cannot preserve this new schedule semantics when saving.

`chrono_routine_maintain()` latches the deadline at the original trigger and overdue at expiration, returning whether persistence is needed. It never creates another occurrence, changes an outstanding deadline, or completes a task. Maintenance runs before announcements on both existing main-thread scheduling paths, independent of selected tab, operator panels or graphics availability. Only state transitions save; a failed save retains the existing visible failure/retry behavior. Recovery skips routine maintenance and rejects routine mutation. The existing source cleanup on close is unchanged.

Confirmation clears the outstanding deadline/overdue latch and chooses the next daily local clock instant strictly after `max(now, original trigger)`, suppressing replay after a clock rollback. Late confirmations before today's scheduled time can choose today. Windows are elapsed wall-clock minutes from the trigger, including DST transitions. A booked waiting trigger and outstanding deadline keep their absolute Unix instant across timezone changes; subsequent scheduling uses the current timezone. GLib local-time construction selects standard time for an ambiguous DST time and the end of the gap for a nonexistent time. Forward jumps and sleep collapse all missed days into the original item, which can immediately be overdue on reconciliation. Backward jumps cannot undo a latched due/overdue state. Disabling suppresses untriggered occurrences while allowing existing items to expire or be confirmed; re-enabling reconciles the retained trigger. Schedule fields lock until confirmation; title edits keep the deadline. An item becoming due during editing also locks on Save with an explanation.

Visible order and function keys are Focus (F1), Ledger (F2), Routines (F3), Calibration (F4). Internal mode indices follow that order; persistence explicitly maps to the established schema-7 semantic tab IDs (Focus=0, Ledger=1, Calibration=2, Routines=3) in both directions. This preserves last-selected tab meaning for existing files and round trips without a schema bump. Number keys retain ordinary input behavior.

## Operator panels and Calibration

`app_modal.c` owns a GTK overlay above the GL area, with shared palette parameters from `ui_palette.c`, instrument typography, profile/manual glow, explicit focus outlines and destructive accents. GTK entries and buttons stay crisp and unwarped, independently of the scene's mask, persistence and curvature; they remain available after renderer failure. Only panel bodies scroll in short tiles; actions stay visible. The GL widget is insensitive during a panel and the hidden entry is excluded from focus. A capture controller cycles focus inside the panel and consumes Escape, while the window shortcut controller defers all keys. Dismissal restores the previous focus. Closing cancels the panel before stopping maintenance; it does not run a nested loop or pause timers/reminders. The panel's sizing tick is widget-owned and disappears with the overlay. Native file pickers and the no-overlay GTK alert fallback are retained.

Confirmations use owned `GTask` results and a finish adapter for panel versus native dialog results, preserving window-owned callback records and transactional ledger replacement. Destruction produces a cancel result, and the CSS provider is removed when its panel is released. Destructive operations initially focus Cancel, including routine deletion.

The original twelve palette indices remain stable, followed by Paper, Cobalt, Rose, Olive and Copper. Profile selection and Balanced restoration preserve palette and effect toggles while restoring numeric presentation defaults. Calibration always lays out all controls; a bounded vertical viewport scrolls on wheel, Page Up/Down and Home/End. Layout computation preserves/clamps the offset across resizes. Dynamic drawing and hit testing use the same translated rectangles and viewport. At ordinary widths the additions occupy a separate row of five; narrow layouts reflow to four columns.

Curvature strength is normalized to 0–1 and maps to 0–0.8 in the shader and inverse pointer lookup. Its 40% default reproduces the former fixed 0.32 warp; the toggle gates the effect without losing its setting. The raw cursor plane remains independent of curvature. Profile interpolation and manual adjustments refresh pointer mapping from the live presentation value. Curvature dragging solves the value-dependent source lookup so the moving handle stays under the raw pointer. The on-screen controls now expose five sliders. The serialized schema-7 array remains six slots: glow, persistence, retired centerline slot (written as zero and ignored on read), vignette, mask, curvature. Historical five-slot arrays are also accepted and retain the default curvature value. The retired slot is still range/type validated; removing its runtime control never shifts surviving saved settings.

Mask now applies a four-by-three physical-pixel aperture pattern after clarity restoration and display gamma, using a power response of 0.85 and bounded attenuation. Zero and a disabled toggle both bypass the mask; maximum deliberately exposes the aperture gaps. Cursor effects use the same response only while their own effects toggle is enabled.
