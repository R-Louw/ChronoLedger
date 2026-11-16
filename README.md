# ChronoLedger

ChronoLedger is a GTK4 focus-allocation application written in C. It combines a task timer and completed-work ledger with a custom Cairo/Pango interface rendered through an OpenGL phosphor pipeline.

This repository contains the production source for ChronoLedger 1.18.0: the application, runtime data model, persistence layer, renderer, shaders, and desktop integration files. Build products, profiling harnesses, sample-data generators, and benchmark controls are intentionally not tracked.

## Features

- Create focus allocations from a title and duration.
- Start, pause, extend, complete, and delete allocations.
- Review completed work and estimate accuracy in the Ledger view.
- Import and export bounded, schema-versioned JSON data.
- Preserve unreadable local data instead of overwriting it on startup.
- Select Balanced, Authentic, Readable, or Shader-off presentation profiles.
- Configure anti-aliasing, palette, persistence, vignette, mask, curvature, notifications, sound, and cursor shader effects.
- Render a native-rate custom cursor independently from the cached application surface.
- Reduce rendering activity when the window is inactive, unmapped, or suspended.

## Requirements

- C11 compiler
- Meson and Ninja
- GTK 4.12 or newer
- GLib/GIO 2.74 or newer
- JSON-GLib 1.6 or newer
- libepoxy
- Cairo and PangoCairo
- OpenGL 3.0 or OpenGL ES 3.0

## Build

```sh
meson setup build --buildtype=release
meson compile -C build
./build/chronoledger
```

The convenience targets are:

```sh
make
make run
make install
make clean
```

`run.sh` configures or reconfigures a release build, compiles it, and starts the application.

## GTK presentation

On Linux with runtime GTK 4.24 or newer, ChronoLedger prefers GTK GL presentation by setting `GSK_RENDERER=gl` in its own process before GTK initialization, only if the variable is absent. An explicit caller value, including an empty value, is preserved. Older GTK releases and other platforms retain GTK defaults; the GTK 4.12 minimum is unchanged. The application still uses its existing OpenGL/GLES shaders and effects. Startup output reports the request and actual GSK renderer separately.

For this machine's previous presentation path, launch a new process with `GSK_RENDERER=vulkan ./build/chronoledger` (or the installed executable). This is a per-launch override, not a global setting or a saved preference. GTK can fall back if the requested renderer cannot initialize; that does not provide recovery from a driver crash or make an unavailable application GL context usable. Revalidate the preference after GTK/driver updates because [GTK's environment controls](https://docs.gtk.org/gtk4/running.html) are not API-stable. See [presentation validation](tools/presentation-study/README.md).

## Data

The default ledger is stored at:

```text
$XDG_DATA_HOME/chronoledger/ledger.json
```

When `XDG_DATA_HOME` is unset, GLib resolves the normal per-user data directory. Set `CHRONOLEDGER_FILE` to use a different file. These launches use the non-unique `io.github.chronoledger.ChronoLedger.Development` identity, isolating test registration from the normal production application.

Persistence uses schema version 7 and reads versions 1–6 with a zero subsecond remainder. Pausing, switching allocations, and saving preserve fractional elapsed time. Older application versions cannot read schema 7; keep a backup before downgrading. Imports are limited to 16 MiB, 10,000 tasks, and 128 levels of JSON nesting. Every supported document must contain a `tasks` array, including an explicitly empty ledger. Duplicate JSON members, out-of-range numbers, and NUL characters are rejected before parsing can change their values.

Active tasks are restored as paused because monotonic timestamps cannot be carried safely across processes or boots. Import replaces ledger records only; local notification and presentation preferences remain unchanged. Export remains a complete backup and includes both ledger records and preferences. If startup cannot load the primary file, recovery is read-only until a valid replacement is explicitly imported. The original file is preserved when recovery is closed.

## Keyboard controls

- `F1`–`F4`: switch between Focus, Ledger, Routines, and Calibration, including while editing allocation fields. Modal editors keep these keys within the panel. Number keys remain ordinary input.
- `Up`, `Down`: select an open allocation in Focus.
- `Space` or `Enter`: start or pause the selected allocation.
- `D`: complete the selected allocation.
- `Ctrl+N`: open the new-allocation editor in Focus.
- `Ctrl+I`, `Ctrl+E`: import or export.
- `A`: cycle anti-aliasing modes.
- `C`: toggle cursor shader effects in Calibration.
- `R`: restore Balanced numeric presentation settings in Calibration, preserving the selected palette and toggles.
- Mouse wheel, `Page Up` / `Page Down`, `Home` / `End`: scroll Calibration when its controls exceed the tile height.
- `Escape`: close the window or leave the active text editor.

## Source layout

```text
src/main.c                   application construction, startup, and high-level lifecycle
src/app_input.[ch]           GTK event-controller translation and queued pointer processing
src/app_dialogs.[ch]         import/export, destructive confirmation, and alert dialogs
src/app_runtime.[ch]         frame scheduling, monitor state, GL callbacks, and power policy
src/app.h                    controller operations exposed to application modules
src/app_state.h              subsystem-owned shell/session/scene/input/runtime state
src/app_types.h              shared UI, layout, presentation, and cursor value types
src/app_actions.[ch]         typed command dispatch and application mutations
src/app_view.[ch]            immutable per-render task and preference snapshot
src/ui.[ch]                  frame preparation, invalidation, and UI status interface
src/ui_scene.c               Cairo surface lifetime and scene composition
src/ui_painter.[ch]          shared Cairo/Pango drawing primitives and formatting
src/ui_focus_view.c          Focus-mode drawing
src/ui_ledger_view.c         Ledger-mode drawing
src/ui_calibration_view.c    Calibration-mode drawing
src/ui_input.c               window interaction and typed control input adaptation
src/ui_layout.[ch]           pure responsive layout calculation
src/ui_controls.[ch]         typed hit testing and control highlight metadata
src/cursor_state.[ch]        pointer mapping, bounded cross sampling, analytic afterglow
src/shader_profile.[ch]      presentation-profile defaults and interpolation policy
src/ui_control_types.h       stable control identifiers and indexed references
src/renderer.[ch]            opaque renderer API and frame orchestration
src/renderer_support.c       bounded numeric and GL utility operations
src/renderer_upload.c        Cairo scene and text-mask texture uploads
src/renderer_scene.c         semantic geometry and composed-scene pass
src/renderer_cursor.c        independent cursor geometry and phosphor pipeline
src/renderer_effects.c       AA, persistence, CRT cache, and presentation passes
src/renderer_context.c       GL context, program, target, and renderer lifetime
src/renderer_shaders.[ch]    compiled-resource shader loading and platform preambles
src/renderer_internal.h      renderer-subsystem frame context and pass contracts
src/renderer_state.h         renderer-private GL resource representation
data/shaders/                GLSL source files compiled into the application GResource
src/chrono_model.[ch]        opaque tasks/store, invariants, accessors, and transitions
src/chrono_preferences.[ch]  notification and presentation preferences
src/chrono_document.[ch]     ownership boundary for ledger plus local preferences
src/chrono_persistence.[ch]  JSON loading, migration, validation, and atomic save
src/chrono_json.[ch]         bounded JSON validation before value conversion
src/time_parse.[ch]          duration parsing and formatting
src/power_policy.[ch]        foreground/background rendering policy
src/render_policy.[ch]       render invalidation and progress cadence policy
data/                        desktop, AppStream, icon, resource, and shader metadata
```
See [DESIGN.md](DESIGN.md) for the current module boundaries and runtime invariants. Shader-specific optimization notes are in [docs/SHADER_OPTIMIZATIONS.md](docs/SHADER_OPTIMIZATIONS.md).

## Repository checks

For ordinary local validation, run `./tools/check`. Changes to code exercised by the deterministic tests that involve ownership, persistence, or teardown should also run `./tools/check memory`, which uses a separate ASan/UBSan build directory. Both commands compile the application and run deterministic model, persistence, policy, layout, snapshot, shader-resource, and controller contract tests. The controller target injects completed dialog results without creating native windows. The sanitizer command does not launch the GTK/GL application, so interactive callbacks and graphics teardown still need a focused display-backed smoke test. See [the validation checklist](docs/VALIDATION.md).

GitHub Actions checks the release build, the warning-strict debug build, and the ASan/UBSan test build on Ubuntu for pushes and pull requests. Local Arch Linux builds remain the primary development path.

## License

The project metadata currently identifies ChronoLedger as proprietary and this repository does not grant an open-source license. Unless a license is added separately, no additional permission to copy, modify, or redistribute the source is granted.

## Routines and restored presentation

F3 opens the routine queue. Use Previous/Next to select one of up to 32 routines; New/Edit opens a schedule editor. Choose daily or an integer interval of 1–8760 hours. New routines first become due after that interval, or at next local midnight for daily schedules. A due routine remains outstanding until acknowledged; missed occurrences collapse into this one reminder. Acknowledge schedules the next interval from now, or the next local calendar day. This wall-clock schedule survives sleep and restarts; changing the system clock or timezone affects when reminders become due. Daily schedules follow local calendar days across DST. Disabled routines retain their deadline and may be due immediately when re-enabled. Changing an interval restarts its schedule; title edits retain the deadline.

Choose **Scheduled confirmation** for a fixed daily local time (24-hour hour/minute) and a 1–120 minute window (default 10). The first occurrence is the next future clock time. At that time **Confirm** becomes available with one click; no allocation timer is needed. **DUE** means the window is open; **OVERDUE** means it expired. The original deadline remains outstanding across missed days, restarts and sleep until confirmed. Confirmation shows a success message and **WAITING**, with the next fixed daily time; it never shifts the schedule to the confirmation time. Schedule controls lock while outstanding; title edits remain available. Disabling stops future triggers but preserves an outstanding item, its deadline and the ability to confirm it.

Scheduling uses wall-clock Unix seconds, checked on the GTK main thread while visible, backgrounded or covered by an operator panel. Forward clock jumps reconcile against the original deadline; backward jumps cannot clear an already observed due/overdue state. Confirming after a rollback schedules strictly after both the confirmation time and the original trigger. An already booked occurrence keeps its absolute instant across timezone/calendar changes; the next occurrence uses the current local timezone. DST gaps use the first valid time after the gap, and repeated times use the standard-time occurrence once. Actual system suspend was not required to reconcile missed windows: the next maintenance pass compares the saved deadline with wall time.

Routines do not consume allocation time or create completed ledger rows. Due routines use the existing notification preference, once per outstanding occurrence per process (a restart may notify again). A disabled routine remains editable and removable. Presentation and routines are local preferences: ledger import preserves them, while export includes them in the full backup. Older schema-7 documents remain readable with defaults. Older binaries may discard these new optional fields when saving.

Normal close saves the selected tab, presentation profile, all five manual shader values, AA and palette, alongside existing toggles. A pending profile transition saves its intended target. Window position, tiled size and compositor state are deliberately excluded. Recovery remains read-only and never overwrites malformed data.

See [durable installation and rollback](docs/INSTALLATION.md). Run `./tools/promote` to validate and promote a future working release without disrupting an already running instance.

See [sound and desktop notification findings](docs/EVENTS.md) for the brief follow-up recommendations.

Calibration retains the twelve original schemes and adds **Paper** (neutral phosphor), **Cobalt** (blue/gold), **Rose** (rose/cream), **Olive** (olive/lilac), and **Copper** (copper/teal). The additions form a separate row of five at ordinary widths and reflow in narrow tiles. Presentation profiles preserve the selected scheme.

The Mask slider runs from no aperture texture to pronounced phosphor cells; its toggle bypasses the effect without discarding the value. Curvature has both a toggle and a strength slider, with the default matching the former fixed warp. Pointer hit testing follows the visible scene while the custom cursor remains in raw window coordinates.

App-owned alerts, confirmations and routine editing use an in-window operator panel. Panels share the selected palette and glow, use ordinary GTK keyboard/text editing, trap Tab navigation, cancel on Escape, and restore prior focus. Destructive confirmations start on Cancel. Text fields remain unwarped and available after renderer failure; file pickers remain native. Timers and routines continue while panels are open.

The cursor has a crisp four-arm cross with continuous phosphor afterglow governed by Persistence. The former independent centerline and Cursor trail control have been removed. Older saved settings remain compatible; their retired trail value is ignored. Disabling Cursor effects leaves a plain cross with no afterglow. Below the minimum layout size, while inactive, or if rendering is unavailable, the native pointer stays visible. See [cursor study tooling](tools/cursor-study/README.md) for repeatable motion and lifecycle checks.
