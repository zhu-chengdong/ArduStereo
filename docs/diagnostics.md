# Runtime diagnostics and hardware-free fault tests

This release publishes a standard ROS
`diagnostic_msgs/DiagnosticArray` on `/diagnostics` (the topic can be remapped).
Each status is named `<fully-qualified-node-name>: acquisition`. The source
identity is the configured device path, or `synthetic_no_camera`; it is not a
camera serial number. There is no dependency on `diagnostic_updater` or an
aggregator. Standard severity values are defined by the [ROS message source](https://github.com/ros/common_msgs/blob/noetic-devel/diagnostic_msgs/msg/DiagnosticStatus.msg).

## Parameters

| Parameter | Default | Meaning |
|---|---|---|
| `diagnostics_period` | 1.0 s | Independent ROS wall-timer publication interval, range 0.05–60 s |
| `diagnostics_window` | 5.0 s | Requested sliding observation window, range 0.1–300 s |
| `frame_timeout` | 2.0 s | Accepted-frame freshness threshold, range 0.05–300 s |
| `diagnostic_expected_fps` | core0.0; B0266 profile20.0 | Optional publication-rate reference, range 0–10000; zero disables rate warnings |
| `diagnostic_min_fps_ratio` | 0.9 | Minimum reference ratio, range (0,1] |

These parameters monitor the application; none sets a sensor control or camera
frame rate. Until a full requested observation window exists, the optional
low-rate warning is suppressed. Freshness checks are active during startup,
warmup and acquisition. Choose a timeout consistent with the intended frame
period; startup may legitimately exceed the timeout and then recover.

## Counts and rates

All durations, rates and freshness decisions use `std::chrono::steady_clock`.
System-clock adjustments cannot change these calculations. Diagnostic message
headers still use ROS time; image headers retain the host receive time. Neither
header is an exposure timestamp. The ROS timer uses its wall-timer scheduler;
timer scheduling under system-clock discontinuities is not validated here.

`capture_fps` counts accepted runtime frames in `(window_start, now]` divided by
that window's elapsed monotonic seconds. `publication_fps` uses completed stereo
publication batches in the same way. The initial window starts when source open
and warmup complete. A frame exactly on the initial boundary is excluded from
the rate but included in lifetime counters. Each window grows to the requested
length; startup values need their reported observation duration to be interpreted.

Snapshots expire old events even when no new frames arrive, so rates decay to
zero during a stall. Each rate history retains at most 4096 timestamps. If that
capacity shortens the retained interval, the denominator uses the actual interval
and `window_limited=true` explicitly reports the limitation. Rate warnings require
a full requested span and may therefore be unavailable when capacity limits it.
Terminal snapshots freeze at stop/error time.

| Field | Exact scope |
|---|---|
| `read_attempts` | Runtime reads started after warmup; includes a currently pending read |
| `captured_frames` | Reads accepted after shape, byte-length and receive-time validation |
| `published_pairs` | Completed batches of left/right Image and CameraInfo publication calls; includes the composite call when enabled |
| `composite_publications` | Composite calls within completed batches |
| `empty_reads` | Source returned false; this does not identify a driver-level dropped frame |
| `consecutive_empty_reads` | Consecutive false reads, reset by an accepted frame |
| `frame_rejections` | Invalid image/timestamp, including explicitly rejected OpenCV formats |
| `read_exceptions` | Other source read exceptions |
| `last_frame_age_seconds` | Age since the last accepted frame; -1 means no runtime frame yet |
| `read_in_progress` | A runtime source read has started and not yet completed |
| `error_stage` | First terminal stage, retained during resource cleanup |

Warmup reads are excluded from these runtime counters. Startup/warmup failures
are classified as `startup`, with runtime counters still zero. A snapshot may
occur after acceptance but before completion of publication, so accepted count
can temporarily exceed completed publication count. A partial batch that throws
is not counted as a completed pair.

`device_drop_count` and `subscriber_received_count` are the literal string
`unavailable`. OpenCV does not provide an authoritative device sequence here,
and a publisher cannot determine subscriber delivery or rosbag write counts.
Independent consumers must measure their own counts. No estimated shortage is
reported as sensor loss.

## States and failure behavior

| State | Severity | Meaning |
|---|---|---|
| `starting` | WARN | Source/configuration/calibration initialization is in progress |
| `waiting_for_frame` | WARN | Warmup complete; first runtime frame not accepted yet |
| `running` | OK | Recent accepted frames and no active failure/rate warning |
| `degraded` | WARN | An empty read is outstanding, or a full-window rate is below the enabled reference |
| `stale` | STALE | No accepted frame within `frame_timeout`; a source read may still be pending |
| `error` | ERROR | Fatal startup, read, validation, failure-limit or node error |
| `stopped` | OK | Normal exit; not an indication of active acquisition |

Fatal errors and normal stop take precedence over freshness and rate checks.
Freshness takes precedence over temporary failures and low rates. An accepted
frame clears consecutive empty reads; a low-rate warning clears only as its
window recovers. The configured empty-read limit causes a nonzero process exit.
Bad frames and source exceptions are fatal. There is no automatic synthetic
fallback or reconnection. The diagnostic component itself does not mutate controls.

Diagnostics run on a dedicated callback queue and spinner, without holding a
monitor lock across source open/read/close. A finite blocked read can therefore
be reported while acquisition is waiting. This does not interrupt a blocked
driver call, guarantee bounded SIGINT shutdown for an indefinitely blocked
source, or validate hardware reconnect behavior. A total process crash/hang
also cannot be reported by its own thread; an external supervisor is separate.

The final counters/state are always attempted on stderr, independent of ROS
logging. ROS diagnostics at process termination are best effort: after SIGINT,
roscpp may already have closed the publishers and rosconsole. A latched message
does not survive publisher exit. Invalid configuration rejected before the
diagnostic publisher exists is reported by the fatal startup log.

## Hardware-free tests and evidence

`test_diagnostics.cpp` runs the production `MonitoredCapture` path with injected
sources and an explicit clock. It checks exact rates, window expiration, clock
rollback rejection, counters, cleanup, terminal states, warmup boundaries and a
blocked read coordinated by promises, without relying on sleep-based unit timing.

`test_ros_diagnostics.py` uses actual ROS messages with the production synthetic
node and a separately built test-only source executable. It observes recovery,
independent diagnostic callbacks during a 1.2-second injected stall, counters,
nonzero fault exits and normal SIGINT cleanup. Sampling tolerances in the live
ROS rate test accommodate host scheduling; they are not camera performance bounds.
The production executable accepts no fault-injection parameters, and the fault
executable is never installed. Both run the same ROS acquisition implementation.

Executed outcomes and scope are in [validation](validation.md). In the60-minute run,20 low-rate WARN samples were supported by source counters and saved timestamps. Short FPS warnings do not by themselves identify a software fault or a sensor-level dropped frame.
