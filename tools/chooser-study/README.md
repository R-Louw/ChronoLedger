# Native chooser close lifecycle

Development-only Hyprland checks of the **actual executable**, using a preload
observer rather than application test switches. Read `AGENTS.md` before display
work. Use normal compositor allocations, isolated identity and temporary data;
coordinate exclusive space or owner-arranged windows if needed. These checks
briefly focus only the child and its uniquely titled native chooser. Never stop
production or restart shared session services. They use the existing session bus,
not a private bus, and do not change global configuration.

From the repository root, build a release executable and the observer:

```sh
meson compile -C build-presentation
./tools/chooser-study/build
./tools/chooser-study/run --renderer gl --operation import --phase initializing \
  --output /tmp/chronoledger-chooser-fix/example
```

The output directory must be new. The runner supplies a fresh `CHRONOLEDGER_FILE`
(`.Development`, non-unique), temporary XDG directories and fatal GTK criticals.
It delivers Ctrl+I/Ctrl+E through Hyprland to that child. Its uniquely titled
native chooser can belong to the existing portal service; only that matching
window receives Escape/selection input. No service is stopped or reconfigured.

Cover both `--renderer gl|vulkan`, `--operation import|export` and
`--phase initializing|presented`. The initialization observer queues the same
high-priority close as the original reproducer; on native Wayland portal runs it
asserts no chooser request has yet been submitted (parent export is unfinished).
Presented runs wait for the actual native window before requesting parent close.
Repeated closes must preserve the parent surface. The runner then dismisses the
**real** chooser; the callback must consume the result, stop scheduling, unrealize
the GL area, release the retained window, GL area, dialog and GTask, and exit 0.
It also checks that no test window remains and the saved temporary ledger parses.
Survival for a timeout is not a passing result.

Add `--response select` to use an actual temporary file selection after close.
Import must not open a replacement panel or mutate the ledger; export must not
write the selected path. The chosen `GFile` must be released. `--phase normal
--response select` checks ordinary export and native import followed by a
signal-driven Replace action, persistence and close. A normal cancellation uses
`--phase normal` alone. The observer changes only the test chooser title/initial
path, wraps the real GTK APIs, and asserts production state; it does not replace
their results. Import path typing currently supports simple lowercase/digit
temporary paths with `/`, `-`, `.`, `_`.

Also cover `--backend x11` under XWayland and `--no-portals` for the GTK local
native fallback. `--desktop-gl` requests desktop GL only in the child. For real
display sanitizer checks use `--executable build-asan/chronoledger --sanitize`;
the runner puts libasan first in the child's preload list and halts on ASan/UBSan
errors. GUI leak detection is disabled for process-global GTK/driver allocations;
explicit weak notifications verify the operation's resource release. Separately,
`./tools/check memory` keeps leak detection enabled for deterministic controller
and core tests. These tests do not substitute for the display evidence.

`--save-failure` (a deferred-close phase with cancellation) starts from valid
temporary JSON, then replaces only that test primary path with an empty
directory. The deferred retry must keep the window open with its save-error
panel. The observer removes that test obstacle and retries ordinary close,
which must save and terminate normally.

On failure the runner attempts to dismiss only its test chooser and terminates
only its own child. Retain logs, including failed input/setup runs. A chooser
service that never responds can still delay a pending parent close: the
application intentionally has no timeout that destroys an initializing parent.
