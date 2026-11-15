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
