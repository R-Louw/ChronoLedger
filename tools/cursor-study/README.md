# Development cursor checks

These harnesses are separate from the installed application and production target. Run from the repository root after `./tools/check`.

## Offscreen rendering

```
sh tools/cursor-study/build-offscreen
python3 tools/cursor-study/analyze.py
```

Dependencies: normal application development packages plus EGL, ffmpeg and Python Pillow. Output defaults to `/tmp/chronoledger-cursor-after`; a different output directory can be supplied to both commands. RGBA readbacks are large (several GB); MP4s and comparison.png are small. Delete raw `.rgba` files after analysis if no longer needed.

The harness uses production cursor state, geometry, AA and phosphor code, a surfaceless EGL context, and a flat 800×360 background at scale 1. It does not launch GTK windows or read any ledger. It supplies a fixed 720 Hz input stream independently of 60/144 Hz presentation. The path moves 300 px/s horizontally for .6 seconds, then 900 px/s for .3 seconds, reverses diagonally (900 horizontal, 200 vertical px/s) for .3 seconds, and stops until 8 seconds. Persistence is .24 and 1; glow .70, mask .36 enabled, vignette .44, FXAA High. A separate run bypasses cursor effects. A stall run omits updates/draws from just after 1.2 s until 2.5 s while retaining the last presented image.

Analysis asserts identical matched-time images at 60/144 Hz, identical output after the stall versus uninterrupted rendering, and no old path after expiry. It creates a contact sheet. Optionally supply the previous assessment's artifact directory as the second argument to include its original before captures:

```
python3 tools/cursor-study/analyze.py /tmp/chronoledger-cursor-after /tmp/chronoledger-cursor-study
```

The original assessment sampled input once per rendered frame. Consequently before/after crops establish the change in appearance on the same path and speeds; they are not a controlled comparison of input delivery rates. The new refresh-rate checks hold the input stream fixed. Neither harness measures physical mouse latency.

## Real display and lifecycle

```
python3 tools/cursor-study/build-display
CHRONOLEDGER_FILE=/tmp/chronoledger-cursor-after/disposable-ledger.json \
CURSOR_STUDY_OUTPUT=/tmp/chronoledger-cursor-after \
G_DEBUG=fatal-criticals /tmp/chronoledger-cursor-after/display
```

Use a new temporary ledger path for each run. The application identity is `io.github.chronoledger.CursorStudy`, with non-unique registration. The harness opens one test window and briefly a small transient focus probe, then closes only its own windows. It respects actual compositor allocations, checking a supported size and a below-minimum 360×240 allocation. It does not close or signal the production instance, move the physical pointer, change saved production preferences or install a build. It needs display socket access; a sandbox may prevent connecting.

The harness names its own window `ChronoLedger validation`; on the owner's Lua-based Hyprland it activates only that title through `hyprctl eval`. Other compositors must grant that test window focus for active cursor checks. It uses actual allocations and never rearranges other windows. GTK 4.24 can emit the previously documented `gdk_frame_timings_throttling_hint` warning on rapid remap; the command above records warnings and keeps criticals fatal. Use `fatal-warnings,fatal-criticals` when checking whether that framework warning still reproduces.

Checks inject cursor callbacks and UI commands into the real GTK application. They verify mapped hover coordinates, a primary action, enter/leave, effects bypass, native-panel fallback, minimum-size fallback, real focus loss/regain, unmap/remap, GL resource release/recreation and normal close. Focus after remapping is compositor-owned: an inactive remapped window must retain its native pointer. The same harness also checks all four function-key mappings and ordinary digits, creates a scheduled confirmation through the GTK editor, verifies overdue persistence while the editor is open, tests the due-during-edit schedule lock, confirms through the visible scene control, and checks background maintenance while unmapped. A widget snapshot captures the scrollable editor at 560×360, and scene captures show overdue/waiting states. Real-display sweeps at .24 and 1 persistence exercise the production cursor path with live frame-clock scheduling.

Application-only framebuffer PNGs are read from the GL area after a production renderer draw, never from the desktop. The compositor-owned native cursor is not in those PNGs; its `GdkCursor` name is asserted in the log.

After promotion, `python3 tools/cursor-study/build-display --release` links the same display harness against `build-promote` application objects/resources instead of debug objects.

For an instrumented display run, first run `./tools/check memory`, then `python3 tools/cursor-study/build-display --asan` and run `display-asan` with the same isolation variables and `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Native GTK/driver process-global allocations are excluded from this GUI leak check; deterministic memory checks retain leak detection.

Limits: synthetic offscreen and callback-driven display evidence do not establish physical hardware-to-screen latency, every driver/compositor, or full system suspend/resume. Sample storage is deliberately bounded; extreme prolonged motion can evict the oldest afterglow. Natural overlap of swept cross arms produces a ribbon on straight paths; there is no separate centerline primitive or saved preference that enables one.
