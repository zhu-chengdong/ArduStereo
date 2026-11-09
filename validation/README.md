# Published validation evidence

This directory contains numeric summaries and relative timing data from author-run tests on one validated rig. Original images, unredacted system/network logs and private task journals are not part of the public distribution. No new acquisition was performed during publication preparation.

`results/hardware_summary.json` records the actual test scope, controls, counts, CPU/RSS and shutdowns. A3600s run delivered71822 complete five-message groups at19.9506FPS. It had20 low-rate diagnostic samples, minimum5s16.8FPS, so the original strict no-WARN criterion failed. Measurement-period read faults and incomplete groups were0; an additional empty read occurred during SIGINT cleanup outside the measurement window and is not a sensor-loss count.

The final10-minute legacyGREY/current pair delivered19.927/19.972FPS. The untouched legacy program selectedY16, even after a pre-openGREY request, and was not comparable. The measured legacy variant only added `cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('G','R','E','Y'));` before setting width. Its original source SHA256 is recorded in THIRD_PARTY_NOTICES.md. The legacy compiler flags were empty; current flags were `-O2 -g -DNDEBUG`. Output features also differ. These are current deployment costs, not a controlled attribution to refactoring or algorithmic speed.

A legacy pilot overlapped log compression/retrieval and was excluded from the final resource comparison. Its numeric replay is retained as `old_pilot`; it is not an additional independent final comparison. The clean legacy repeat used the same GREY-only binary without concurrent compilation or retrieval.

## Recompute common timing metrics

```bash
python3 validation/verify_timing.py
```

`data/*_timing.csv.gz` contains all saved composite callback/header timestamps, including prefix/drain rows. `t_receive_ns` is relative to the measurement start; the measured window is `[0, valid_seconds*1e9)`. `t_header_ns` has a separate arbitrary additive origin. Intervals and pairing stamps are preserved exactly; absolute date/boot-time information is removed. Do not interpret the separate zero points as transport latency or sensor exposure timing. `timing_provenance.json` retains the original input SHA256 and transformation.

`data/*_diagnostics.csv` gives the actual diagnostic callback time relative to the same measurement window and original rates/counters. All20 long-run warnings were supported by independent callback windows, hostHeader windows and source counter increments. The old node had no equivalent source counters; its minimum5s15.2FPS and10 low windows are receiver-side offline calculations, not native legacy warnings. Shifting the1s evaluation phase changes some threshold-near samples; it does not remove the observed dips.

## Replay the frozen C++ rate calculation

Build the core-only library first, then compile this numerical helper:

```bash
c++ -std=c++14 -Iinclude validation/replay_statistics.cpp build/core/libardustereo_core.a -pthread -o build/replay_statistics
gzip -dc validation/replay_inputs/long.txt.gz > build/long-events.txt
build/replay_statistics build/long-events.txt build/long-replayed.csv
```

Inputs contain only relative fake-clock `E` frame events and `Q` snapshot requests. No CaptureSource/OpenCV/ROS/camera instance is used. Eight local x86_64 replay scenarios produced12600 snapshots matching independent event counts; an ideal20Hz-hour model had0WARN. The simulated16-event omission is a numerical test case, not measured camera loss. Expected results are in `results/replay_expected.json`.

The observed output gaps also occurred in the same-format legacy flow. Hardware, driver and scheduling contributions remain unseparated: no sensor sequence/exposure timestamps or per-call blocking durations were recorded, and attempted kernel-log reads were permission-denied. Empty kernel stdout must not be treated as proof of no driver error.
