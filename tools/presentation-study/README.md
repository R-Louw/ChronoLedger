# GTK presentation comparison

Development-only tools; the production target contains no benchmark counters.
Arrange a focused testing interval with the owner first (roughly nine minutes).
Leave the production process running and use normal compositor allocations.

From the repository root:

```sh
meson setup build-presentation --buildtype=release -Dwerror=true
meson compile -C build-presentation
python tools/presentation-study/build
python tools/presentation-study/run
python tools/presentation-study/analyze
```

Output defaults to `/tmp/chronoledger-presentation-validation`; supply `--output`
to each tool for a new directory on subsequent runs. The runner refuses existing
sample directories. To resume after an interruption, retain/archive the invalid sample directory and use `run --start-run N`; the analyzer still requires all three complete pairs. A one-time Hyprland focus request targets only the test title before warm-up, matching the existing lifecycle harness. It never continually overrides owner input.

It links normal release objects/resources, includes production
startup/input code and invokes the shared policy before `gtk_init`. Every process
uses `io.github.chronoledger.PresentationStudy`, non-unique registration, a fresh
ledger, no notifications, and the default Balanced effects/FXAA High.

Six runs alternate explicit `vulkan` and an unset selector (application default
`gl` on the target). The second pair reverses order. Each run contains warm-up,
30 seconds of a 1500-second timer, motion warm-up, 30 seconds of timer plus
synthetic 125 Hz input, settled idle, hidden timer and hidden idle. Four queued
allocations match the earlier study. The 8 ms motion source exists only during
motion; there is no GPU query or profiler in measured runs. A 1 Hz heartbeat
checks foreground focus. Dimension/focus violations invalidate a sample.

CSV records all-thread process CPU time, draws/uploads, after-paint callbacks,
dimensions, frame intervals during motion, latest synthetic event age at draw,
and power-policy mode. Event age is an internal responsiveness proxy, not
physical input-to-photon latency. After-paint callbacks are not necessarily GPU
draws. Application draw counters exclude GTK/compositor GPU work. Report CPU
ms/s and variation alongside percentages; 1000 ms/s is one fully occupied core.
No power or GPU saving is inferred from this measurement.

The analyzer requires all three pairs, matching dimensions, zero measurement
violations, >=30% CPU reduction in both active workloads in each pair, motion
cadence within 5% of control and no >2 ms p95 frame/event-age regression. It also
checks no draws after idle settling or while hidden. Review logs and capture
quality/lifecycle evidence separately; passing CPU analysis alone is insufficient
for promotion. Noise or failures require investigation, not weaker thresholds.

The existing cursor-study harness covers real GTK controls, panels, routines,
focus, resize, GL teardown and native/custom cursor behavior. Actual-executable
checks must additionally exercise startup without the harness. Runtime GTK 4.12
is still supported, but only GTK >=4.24 on Linux receives the new default;
validate the older-version policy boundary in a subprocess when that runtime is
not installed, and record the lack of a real older-runtime display test.
