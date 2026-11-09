# Image, parameter and error contracts

## Configuration

`stereo.launch` loads one YAML file into the node's private parameter namespace.
`camera_ns` selects the public topic namespace; the node itself uses relative
topic names. Launching more than one physical rig also requires distinct
optical-frame IDs in their respective YAML files. Unknown fields and ROS
parameter type errors are fatal.

The shipped files show all accepted parameters. `source` is `v4l2` or the
explicit `synthetic` source. Geometry must be positive and the split column
must be strictly inside the composite image. The two widths may differ.
`queue_size` is 1–100, `buffer_size` 1–64, `warmup_frames` 0–1000, and
`max_read_failures` 1–10000. Buffer acceptance/readback is logged, not treated
as a measurement of sensor buffering. `synthetic_fps` (0,500] paces only the
synthetic generator and never configures camera FPS.

Default `input_format: mono8` accepts only an 8-bit single-channel Mat. For
`mono16`, the explicit `mono16_shift` must be 0–8; each unsigned sample is
right-shifted by that amount, and an output above 255 is rejected. No contrast
normalization, BGR conversion or automatic bit-alignment inference occurs.
The backend assumes OpenCV CV_16UC1 uses native-endian values; that assumption
must be checked for each actual driver/format before enabling mono16. A
nonempty FourCC has exactly four characters and must survive OpenCV readback.

The B0266 profile explicitly selects `fourcc: GREY`. `frame_rate`, `exposure`
and `analogue_gain` are driver raw integers, with shipped targets20/681/100.
A missing control is left unset (-1); zero and other negative values are rejected.
The backend starts the stream, validates the observed control IDs/names/ranges,
applies the requested controls through V4L2 ioctls and reads all values back
before warmup/publication. Any setting/readback error stops startup; partial
writes trigger an attempt to restore preceding values. The synthetic source
rejects hardware-control requests. No external `v4l2-ctl` setter is invoked.

## Publication

Every accepted input yields one left/right pair. Images own packed, row-copied
data with `encoding=mono8`, `step=width`, `is_bigendian=0` and data length
`step*height`. Composite output is controlled by `publish_composite`.

One system-clock timestamp is recorded just after an OpenCV read returns,
before normalization/splitting. The synthetic source records host time when
generating a frame. The ROS adapter copies this same time into both images,
both CameraInfo messages and the optional composite. This defines software
pairing, not exposure synchronization. `/use_sim_time=true` is rejected for
these acquisition sources. There is no fabricated driver sequence or device
drop count. Application publication counts are not sensor frame counters.

Left/right frame IDs default to distinct optical-frame names. TF is not
published because the rig transform has not been established for this version.
The composite uses a compatibility frame ID and has no CameraInfo.

Without calibration URLs, zeroed CameraInfo at the output resolution is
published. With URLs, camera_info_manager loads explicitly requested standard
ROS YAML and the node checks dimensions, finite values, distortion model,
matrix structure and rotation orthonormality. This is a data contract check,
not a physical calibration validation. Cropped/binned CameraInfo is rejected.
Configured camera_info_manager instances expose their usual set_camera_info
service; updates are revalidated before the next pair is published. Empty URLs
do not implicitly load unrelated files from the user's ROS home directory.

## Failure and lifecycle

Invalid parameters or calibration fail before a source is opened. Failed
opening/warmup closes partially acquired resources. A failed read clears the
output and publishes no stale frame; consecutive failures have a bounded
count, wall-clock backoff and a nonzero final exit status. An unexpected size,
type or invalid conversion stops the node rather than publishing corrupt data.
Normal scope exit uses RAII to release the source.

The OpenCV read call itself may block. This version has no independently
validated watchdog or safe read cancellation. A failure-count limit therefore
does not imply a bounded elapsed timeout, and SIGINT responsiveness remains a
hardware/backend acceptance item. No automatic reconnect is promised.

## Runtime diagnostics

This release provides standard `/diagnostics` output. Exact metrics, window
definitions, thresholds and shutdown limitations are specified in
[diagnostics](diagnostics.md). `diagnostic_expected_fps` is a monitoring reference
only. It does not request a camera frame rate. The separate `frame_rate`
parameter applies the actual driver control.

The optional companion service and files are specified in [experiment records](experiment_records.md).
