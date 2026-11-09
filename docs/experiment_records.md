# Experiment records and paired snapshots

The optional `experiment_tools.py` companion is enabled by default in stereo.launch. `session_tools:=false` disables it. It normally subscribes only to diagnostics; it temporarily subscribes to left/right Image and CameraInfo during a snapshot request.

| Record | Meaning |
|---|---|
| requested.yaml | Exact bytes of the selected launch configuration |
| parameters.json | Loaded camera private parameters |
| session.json | Version/environment/config hash and wall-clock plausibility |
| initial_control_readback.json | One read-only control query after first observed running state; unavailable/synthetic is explicit |
| diagnostics.jsonl | Received diagnostics with monotonic arrival times |
| summary.json | Written on normal companion shutdown; last observed counts and snapshot total |
| snapshots/pair_* | Complete on-demand image pair, two CameraInfo JSON files and SHA256 manifest |

`last_observed_published_pairs` is not automatically a final count. `counters_are_final` is true only if a terminal stopped/error diagnostic was actually received. A missing summary is not proof of complete shutdown. The initial control readback is not continuous monitoring of later external changes.

Call `/camera/save_snapshot` using std_srvs/Trigger. The default timeout is3s; the exact-stamp cache holds at most8 timestamps. Images must be fresh, valid mono8 messages of the configured dimensions/frame IDs. Concurrent requests return busy; timeout, mismatch and file failures return failure. Completed snapshots are renamed from a hidden pending directory only after all files close successfully.

Each pair includes left.pgm, right.pgm, left_camera_info.json, right_camera_info.json and snapshot.json. JSON preserves the ROS calibration-message fields; it is neither a physical calibration result nor a ready-made camera_info_manager YAML. Pairing proves common software timestamps, not sensor exposure simultaneity.

Use these records to trace configuration and inspect stereo pairs. Continuous SLAM image recording or rosbag workflows remain separate consumers of the standard image topics.
