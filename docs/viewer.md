# Viewer workflow

Run `roslaunch ardustereo viewer.launch` in a sourced workspace and open `http://127.0.0.1:8765/`. Click Start to create the viewer-owned acquisition process. On a remote Jetson, use existing SSH authentication and forward the loopback port, substituting your own account and host:

```bash
ssh -N -L 8765:127.0.0.1:8765 user@jetson-host
```

The server binds to loopback only. It does not change networking/firewall settings. A per-page token and same-origin checks protect writes; there are no remote page resources or browser camera permissions.

## Daily actions

1. Start acquisition and check both images.
2. Change one raw control, click Apply, then confirm readback. Requested values alone are not proof of image response.
3. Wait at least one full5s window after a frame-rate change. Actual acquisition/publication rates are not the preview refresh rate.
4. Save a paired snapshot or subscribe from a ROS recording/research tool.
5. Save YAML to a new file for future runs; this saves inputs and does not itself apply them.
6. Stop before releasing the camera; restart is supported. Exiting the viewer terminal cleans up its owned process. Closing just the browser does not stop acquisition.

The viewer refuses to take over a pre-existing external node in the same namespace. It does not open a second OpenCV camera. Controls are queried for names/types/ranges/steps; service requests use `/camera/get_controls` and `/camera/set_controls`. A timeout means the write outcome is unknown, not cancelled; the UI does not blindly resend. Rollback is best effort, not a driver-level atomic transaction.

Preview pairs by common stamp, caches at most8 pending stamps and encodes PNG at most5 times/s with max640px width per image. Full-resolution ROS outputs retain their own rate. Stale preview/diagnostics are not displayed as current live state. Encoding and subscriptions consume CPU; measured costs are in validation, not a claim of free preview.

The viewer saves actions and per-capture logs under `~/ardustereo_gui/`. Acquisition metadata records are separately under `~/ardustereo_sessions/`. Snapshots contain fresh raw PGM images, CameraInfo and checksums. Continuous recording uses an external recorder; no GUI record button is implemented.

## Without a camera

```bash
roslaunch ardustereo viewer.launch config:=$(rospack find ardustereo)/config/viewer_synthetic.yaml
```

For a pure UI demo without ROS, from the source directory:

```bash
python3 scripts/viewer.py --demo --config config/viewer_synthetic.yaml
```

Demo mode is explicitly marked. It has no ROS CameraInfo and therefore refuses a complete paired snapshot. Synthetic mode tests software frequency and fake controls, not physical exposure/gain effects.

## Tested status

The fixed candidate completed real controls/restore, preview, snapshots/YAML and stops/restarts on the documented rig. The earlier generic statement that all hardware UI validation is pending is obsolete in the logs but remains in the frozen UI banner. Also, the last timestamp text may remain after stop; live image/rate state is cleared. See [validation](../validation/README.md) for measured results and qualifications.
