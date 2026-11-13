# 0.4.0 — 2026-11-13

English stereo preview and parameter readback, with in-browser ROS bag recording, time and file status. Each segment can be saved while preview continues. See [release notes](RELEASE_NOTES.md).

# Public candidate0.3.0-startup-init-fix1 —2026-11-10

Publish the exact tested GUI/startup-initialization candidate with current installation/use documentation, de-identified numeric evidence and hardware-free CI. No acquisition implementation changes during publication. All rate WARNs and original v0.2.2 history retained. See RELEASE_NOTES.md.

# 0.2.2 — 2026-11-09

- Independent session metadata/diagnostic recorder, enabled by the existing launch.
- On-demand exact-stamp left/right images and CameraInfo snapshot service.
- Core C++ source unchanged; no dependency installation or platform changes.
- Offline unit/ROS and target30s/15s hardware validation passed.

# Changelog

## 0.2.1 (development)

- Explicit GREY B0266 default.
- Raw frame_rate/exposure/analogue_gain YAML controls applied and read back after stream start.
- Preserve original source and require real20/681/100 launch/restart validation.

# Changelog

## 0.2.0 development snapshot — 2026-11-06

- Add ROS-independent monotonic acquisition/publication rate windows and runtime states.
- Publish standard `/diagnostics` messages on a separate callback queue during source reads.
- Distinguish empty reads, rejected frames, source exceptions and completed stereo publications.
- Add deterministic clock/source fault tests and real ROS synthetic/injected-source tests.
- Preserve a final stderr summary after ROS logging shuts down on SIGINT.

Physical B0266/Jetson acceptance, camera controls and exact device drop counts remain pending.


## 0.1.0 development snapshot — 2026-11-06

- Extract ROS-independent frame operations and capture-session lifecycle.
- Preserve an OpenCV CAP_V4L2 backend with explicit format checks and readback.
- Add configurable ROS 1 side-by-side stereo image and CameraInfo interfaces.
- Add YAML/launch files and an explicit synthetic source for hardware-free tests.
- Add core tests, ROS integration tests and standalone core installation support.

This is a local development snapshot. It has not been deployed to Jetson,
published as a release, or accepted against the physical B0266. Exposure,
gain and camera frame-rate control mappings remain deferred.
