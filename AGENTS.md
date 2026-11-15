# Repository agent notes

## Hyprland development environment

The owner uses Hyprland and continues other work while Codex develops. Tiling may allocate half-screen, quarter-screen, or another size instead of ChronoLedger's requested/default dimensions. This sizing alone is expected compositor behavior. Evaluate the UI within its actual allocation; investigate genuine clipping, overlap, or unusable controls.

If a check needs exclusive screen space or additional windows arranged by the owner (such as one or two terminals), ask for that setup and explain why. Preserve ongoing work and the current session. Continue independent tasks while waiting. Never close a production instance to make a test convenient; use isolated temporary data and application identity.

## Before editing

- Read the relevant `DESIGN.md` section before changing lifecycle, persistence/store replacement, callbacks/sources, snapshots, or renderer boundaries; verify the described behavior against the touched implementation.
- GTK objects, model mutations, persistence callbacks, and render-visible state stay on the GTK main thread.
- GTK async callbacks must not retain a raw `AppState *` across the async boundary; retain the window (or a callback record that owns it) and resolve `"chronoledger-state"` when the callback completes, as `app_dialogs.c` does.
- `AppState` is window-owned object data. Widget signals and tick callbacks are bounded by that widget hierarchy; standalone `GSource` IDs must be removed before the state is freed.
- `AppViewSnapshot` task IDs/titles are borrowed only for one synchronous render while the owning document remains unchanged.

## Validation

- Run `./tools/check` for every change. Run `./tools/check memory` when touched code is exercised by the deterministic tests and is ownership-, persistence-, or teardown-sensitive.
- The memory check compiles the whole application but dynamically exercises only the non-GTK test target. It does not validate interactive GTK callbacks, GL context teardown, or driver behavior; smoke-test the affected lifecycle on a real display when those paths change.
