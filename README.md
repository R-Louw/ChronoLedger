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

## Data

The default ledger is stored at:

```text
$XDG_DATA_HOME/chronoledger/ledger.json
```

When `XDG_DATA_HOME` is unset, GLib resolves the normal per-user data directory. Set `CHRONOLEDGER_FILE` to use a different file.

Persistence uses schema version 6. Imports are limited to 16 MiB and 10,000 tasks. Active tasks are restored as paused because monotonic timestamps cannot be carried safely across processes or boots. Import replaces ledger records only; local notification and presentation preferences remain unchanged. Export remains a complete backup and includes both ledger records and preferences.

## Keyboard controls

- `1`, `2`, `3`: switch between Focus, Ledger, and Calibration.
- `Up`, `Down`: select an open allocation in Focus.
- `Space` or `Enter`: start or pause the selected allocation.
- `D`: complete the selected allocation.
- `Ctrl+N`: open the new-allocation editor in Focus.
- `Ctrl+I`, `Ctrl+E`: import or export.
- `A`: cycle anti-aliasing modes.
- `C`: toggle cursor shader effects in Calibration.
- `R`: restore the Balanced presentation profile in Calibration.
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
src/cursor_state.[ch]        pointer mapping, bounded trail sampling, phosphor lifetime
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
src/time_parse.[ch]          duration parsing and formatting
src/power_policy.[ch]        foreground/background rendering policy
src/render_policy.[ch]       render invalidation and progress cadence policy
data/                        desktop, AppStream, icon, resource, and shader metadata
```
See [DESIGN.md](DESIGN.md) for the current module boundaries and runtime invariants. Shader-specific optimization notes are in [docs/SHADER_OPTIMIZATIONS.md](docs/SHADER_OPTIMIZATIONS.md).

## Repository checks

For ordinary local validation, run `./tools/check`. Changes to code exercised by the deterministic tests that involve ownership, persistence, or teardown should also run `./tools/check memory`, which uses a separate ASan/UBSan build directory. Both commands compile the application and run deterministic model, persistence, policy, layout, snapshot, and shader-resource contract tests. The sanitizer command does not launch the GTK/GL application, so interactive callback and graphics teardown changes still need a focused display-backed smoke test.

GitHub Actions checks the release build, the warning-strict debug build, and the ASan/UBSan test build on Ubuntu for pushes and pull requests. Local Arch Linux builds remain the primary development path.

## License

The project metadata currently identifies ChronoLedger as proprietary and this repository does not grant an open-source license. Unless a license is added separately, no additional permission to copy, modify, or redistribute the source is granted.
