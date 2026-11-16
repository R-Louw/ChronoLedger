# Durable user installation

Run `./tools/promote` from the repository. It runs warning-strict tests and ASan/UBSan/leak checks, builds and tests a release binary, stages runtime resources under `~/.local/share/chronoledger-install/release-*`, then atomically replaces `current`. Any failed build/check occurs before promotion. Shaders are embedded GResources; GTK, JSON-GLib, epoxy, Cairo/Pango and their system dependencies remain system packages. No build directory is used at runtime. The script does not launch or stop the application. A running process continues using its existing executable until its next normal restart.

Rofi uses `~/.local/share/applications/io.github.chronoledger.ChronoLedger.desktop`. Hyprland's active `~/.config/hypr/hyprland.lua` starts the same `~/.local/share/chronoledger-install/current/bin/chronoledger` from its existing `hyprland.start` callback. No additional XDG or systemd autostart is added. The old `chronoledger.desktop` is moved out of the applications directory and backed up with the pre-edit Lua file under `~/.local/share/chronoledger-install/config-backup/`. The existing single-instance application behavior prevents duplicate production processes. Reloading Hyprland does not run the startup callback again.

To roll back the executable, close ChronoLedger normally, then run:

```sh
cd ~/.local/share/chronoledger-install
ln -s "$(readlink previous)" rollback.new
mv -Tf rollback.new current
```

Launch from Rofi again. The first migration also retains the old launcher executable under `pre-migration`, referenced by `previous`; it is a recovery copy of the former installation, not a newly validated release. Future promotions set `previous` to the prior validated release. Releases are retained; remove obsolete release directories manually after verifying the current version. Rollback does not change ledger data. Back up the ledger before downgrading: older binaries may ignore newly added local routine/presentation state. Restore the backed-up Lua file and desktop entry to undo desktop integration, taking care to preserve any subsequent personal edits.

## Presentation preference and restart verification

The executable supplies its own GTK GL presentation preference on Linux with runtime GTK >= 4.24 when `GSK_RENDERER` is absent. Both existing desktop launch paths therefore receive the same policy without editing the Rofi entry, Hyprland Lua, session environment or GPU settings. Existing explicit caller settings win. The application renderer and shaders remain OpenGL/GLES. Older GTK runtimes and other platforms retain GTK defaults; the minimum GTK version is still 4.12.

To use the previous presentation choice for one newly started installed process:

```sh
GSK_RENDERER=vulkan ~/.local/share/chronoledger-install/current/bin/chronoledger
```

This does not change an already running process: single-instance activation may simply present that process. Promotion is separate from restart verification. Leave production running during validation and promotion; verify the actual installed GSK renderer after the owner's coordinated normal restart. Startup output distinguishes the requested renderer from the renderer GTK actually realized. GTK owns fallback if initialization fails; driver crashes require a new launch or the executable rollback above. GTK's environment controls are not API-stable, so revalidate on GTK/driver upgrades.

For isolated direct-executable checks, use a fresh `CHRONOLEDGER_FILE`; these launches use `.Development` identity and non-unique registration. Never use the personal ledger for a validation launch. No new XDG/systemd autostart or launcher wrapper is needed.

The initial presentation candidate was held for a GTK 4.24.1 native chooser initialization/close race. The separate application lifecycle correction keeps a chooser's parent realized until its result arrives, then retries normal save/close; dismiss or complete the chooser to finish closing, and late selections are discarded. A nonresponding chooser service can delay closure. The [separate lifecycle validation](VALIDATION.md#separate-native-chooser-lifecycle-correction--2026-11-16) covers actual completion, resource release and process termination with both GTK renderers. Successful validation/promotion remains distinct from verification of a newly started installed process after the owner's normal restart.
