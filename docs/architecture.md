# Architecture

```mermaid
flowchart LR
  YAML[YAML configuration] --> BACK[OpenCV V4L2 source]
  DRIVER[Existing camera driver] --> BACK
  BACK --> CORE[ROS-independent C++ core]
  CORE --> ROS[ROS1 adapter]
  ROS --> TOPICS[Left/right Image + CameraInfo + composite]
  CORE --> DIAG[Monotonic diagnostics]
  DIAG --> META[Optional record companion]
  TOPICS --> SNAP[On-demand exact-stamp snapshot]
  META --> FILES[Metadata and diagnostic files]
  SNAP --> FILES
```

`ardustereo_core` contains image contracts, normalization/splitting, lifecycle and runtime monitoring. `ardustereo_opencv` provides the actual Linux V4L2 source and observed startup-control mapping. `ardustereo_ros` supplies standard messages and configuration/calibration validation. The Python companion adds metadata/snapshots without changing the C++ capture loop.

See [interfaces](interfaces.md) and [diagnostics](diagnostics.md) for timing and failure contracts. ROS2 remains a possible future adapter, not an implemented capability.
