# ChronoLedger design baseline

This document describes the production architecture after removal of test, sample-data, profiling, benchmark, and audit scaffolding. It is intentionally limited to live application behavior so that subsequent refactoring starts from explicit boundaries.

## Runtime structure

ChronoLedger runs as one GTK application with one `GtkGLArea` and an invisible native entry used for text input. GTK object access, task mutations, persistence callbacks, and rendering coordination remain on the main thread.

The visible interface is split into three modes:

- **Focus** manages open allocations and the active timer.
- **Ledger** summarizes completed work and exposes import, export, and clear operations.
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

`ui_layout.c` is a pure layout policy module. It rebuilds a complete `UiLayout` from logical dimensions and reports only whether the density class changed; it does not access GTK objects, document state, renderer state, or `AppState`.

`cursor_state.c` owns inverse pointer mapping, bounded cursor-trail resampling and pruning, and cursor-phosphor lifetime. It accepts only scene, input, settings, preferences, and the opaque renderer operations needed to clear or invalidate cursor history.

`shader_profile.c` owns presentation defaults and profile interpolation. It mutates only `AppPresentationState` and reports which renderer/UI invalidations the controller must apply.

### Application/controller modules

`app_input.c` installs GTK event controllers and translates keyboard, pointer, scroll, and queued motion into typed application operations. `app_dialogs.c` owns import/export alerts and destructive confirmation flows, including save-or-rollback ledger replacement. `app_runtime.c` owns monitor refresh tracking, frame-clock and low-power scheduling, GL realize/unrealize/render callbacks, and runtime progress updates.

`AppState` is a lifetime aggregate rather than a flat shared-memory structure. It groups GTK shell objects, presentation/layout state, document/session state, CPU scene caches, pointer state, frame scheduling state, and one opaque renderer handle. Common value types live in `app_types.h`; controller, UI, and renderer APIs are separated across `app.h`, `ui.h`, and `renderer.h`.

Modules receive narrower state where a full aggregate is unnecessary. Snapshot construction accepts only `AppSessionState`, while control hit testing accepts only presentation and session state.

### `app_actions.c`

Owns user-command execution. Keyboard and pointer inputs construct the same typed `AppAction` values, so model mutations, persistence, selection, annunciators, and calibration changes have one implementation path. Action payloads are validated before execution and stale task identifiers are rejected.

### `ui_controls.c`

Owns typed control hit testing and highlight metadata. `UiControlRef` replaces numeric token ranges and carries a control kind plus an optional row or option index. Hit regions are nonoverlapping; slider targets remain at least 24 logical pixels high without stealing the edges of adjacent controls.

### `app_view.c`

Builds one immutable value snapshot for each synchronous render dispatch. The snapshot captures the frame monotonic timestamp, local preferences, active/focus state, visible queue and ledger rows, completed statistics, and the bounded ledger chart window. UI and renderer code therefore consume one coherent model view instead of rescanning mutable task state at different times during the same frame. Scalar values and preferences are copied; task identifiers and titles are borrowed from opaque tasks and are valid while the owning document remains unchanged on the GTK main thread.

### Renderer modules

`renderer_context.c` owns renderer allocation/destruction, GL context realization, program compilation/linking, uniform discovery, render-target lifetime, and context teardown. The concrete `Renderer` representation remains private to the renderer subsystem through `renderer_state.h`; application and UI code use only `renderer.h`.

`renderer_shaders.c` loads GLSL from the compiled application `GResource` and adds the appropriate desktop OpenGL or GLES version preamble. Shader bodies live under `data/shaders/` rather than in C string literals.

`renderer.c` is the render-graph orchestrator. Individual passes are separated by ownership and failure domain:

- `renderer_upload.c` uploads the prepared Cairo scene and text mask through bounded direct/PBO paths.
- `renderer_scene.c` builds semantic geometry and produces the composed scene target.
- `renderer_cursor.c` owns cursor damage, geometry, AA, temporal history, phosphor, and final cursor compositing.
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

### `chrono_preferences.c` and `chrono_document.c`

`ChronoPreferences` owns notification and presentation choices independently from ledger records. `ChronoDocument` is the application-level ownership boundary that contains one consistent `ChronoStore` and one preference value. Store exchanges validate the replacement before ownership transfers, allowing save-or-rollback transactions without exposing the model container.

### `chrono_persistence.c`

Owns schema migration, strict validation, bounded I/O, and atomic replacement. Normal startup and explicit import use the same strict task parser. Invalid local data is not silently reduced and is not overwritten by the automatic save path. The import API returns ledger records only, which makes preservation of local preferences an API contract rather than a caller convention.

Legacy compatibility is limited to recognized persisted fields. Bundled sample records from older schemas are discarded during migration and are never written by the current application.

### Policy modules

`power_policy.c` decides whether rendering is interactive, background-clock only, or asleep. `render_policy.c` decides whether a frame is required and limits progress-only invalidation to meaningful visual steps.

## Ownership

- `AppState` owns explicit shell, presentation, session, scene, input, and runtime substates plus one opaque renderer handle attached to the window lifetime.
- `ChronoDocument` owns one consistent `ChronoStore` plus local `ChronoPreferences`.
- `ChronoStore` privately owns opaque `ChronoTask` objects through a `GPtrArray` free function.
- `AppViewSnapshot` borrows task strings only for one synchronous main-thread render and copies every mutable scalar and preference value it exposes.
- `chrono_store_add_task()` transfers ownership only on success.
- Dialog callback records own any duplicated IDs or imported stores until the callback resolves.
- Ledger replacement candidates are consumed by one controller transaction that saves, commits, or restores the previous owned store.
- Cursor motion and trail buffers are fixed-size arrays embedded in `AppInputState`; `cursor_state.c` mutates them synchronously and no producer retains access outside the main thread.

## Persistence behavior

The primary file is resolved through `chrono_default_data_path()`. Writes use a bounded serialized representation and `GFile` replacement semantics. Save failures remain visible and the in-memory store remains available for retry.

If the primary file cannot be read or validated, ChronoLedger enters a protected recovery state. Automatic saves are blocked until the user explicitly imports a valid replacement. Import replaces only ledger records and preserves local preferences; export includes both as a complete backup.

## Rendering and invalidation

The application scene is event-driven rather than continuously redrawn. Static and dynamic dirty flags control Cairo reconstruction and GPU cache refresh. Once a frame is accepted, `main.c` captures one `AppViewSnapshot`, asks the UI layer to prepare CPU surfaces, then submits one immutable `RendererFrameInput`. Cairo construction and GPU geometry/compositing therefore receive the same timestamp and model snapshot. The renderer representation is private to the renderer subsystem; cache, CRT timing, and cursor-history state are mutated only through renderer functions, and UI/lifecycle code use renderer-owned predicates and commands. Native frame-clock work continues only while one of these conditions requires it:

- scene or cache invalidation
- pointer movement or cursor trail decay
- cursor phosphor cleanup
- presentation-profile transition
- active timer progress crossing a visible step

The cursor uses raw window coordinates so it remains aligned independently of optional scene curvature. When cursor shader effects are disabled, the raw cross and geometry trail are composited without cursor AA, history, glow, mask, or vignette.

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
