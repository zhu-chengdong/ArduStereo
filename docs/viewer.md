# Viewer workflow

Run `roslaunch ardustereo viewer.launch` in a sourced workspace and open `http://127.0.0.1:8765/`. Click Start to create the viewer-owned acquisition process. On a remote Jetson, use existing SSH authentication and forward the loopback port, substituting your own account and host:

```bash
ssh -N -L 8765:127.0.0.1:8765 user@jetson-host
```

The server binds to loopback only. It does not change networking/firewall settings. A per-page token and same-origin checks protect writes; there are no remote page resources or browser camera permissions.

## Daily actions

Before recording, click **Choose folder…** beside **Save to**. Browse directories on the camera computer, or type an absolute path in **Location** and click **Open**. Click **Use this folder** to write new bag files directly there. The choice persists across launches using the same viewer root. You can switch folders between segments. The session logs keep their original location.

1. Start acquisition and check both images.
2. Change one raw control, click Apply, then confirm readback. Requested values alone are not proof of image response.
3. Wait at least one full5s window after a frame-rate change. Actual acquisition/publication rates are not the preview refresh rate.
4. With both previews updating, click **Start recording**. **Recording** appears after the recorder opens the bag. The elapsed time and destination appear next to the control. Click **Stop recording** and wait for **Saved**. The camera keeps previewing and the next click starts a separate file.
5. Save YAML to a new file for future runs; this saves inputs and does not itself apply them.
6. Stop before releasing the camera; restart is supported. Exiting the viewer terminal cleans up its owned process. Closing just the browser does not stop acquisition.

The viewer refuses to take over a pre-existing external node in the same namespace. It does not open a second OpenCV camera. Controls are queried for names/types/ranges/steps; service requests use `/camera/get_controls` and `/camera/set_controls`. A timeout means the write outcome is unknown, not cancelled; the UI does not blindly resend. Rollback is best effort, not a driver-level atomic transaction.

Preview pairs by common stamp, caches at most8 pending stamps and encodes PNG at most5 times/s with max640px width per image. Full-resolution ROS outputs retain their own rate. Stale preview/diagnostics are not displayed as current live state. Encoding and subscriptions consume CPU; measured costs are in validation, not a claim of free preview.

Each segment is saved under the viewer session's `recordings/` directory on the ROS host. The recorder subscribes to both raw image and CameraInfo topics in the configured camera namespace and to `/diagnostics`. Stopping the camera or exiting the viewer terminal closes an open segment first. A recording error leaves the files and process logs available in the same directory. Replay a saved file with `rosbag play /path/to/segment.bag`.

The viewer saves actions and per-capture logs under `~/ardustereo_gui/`. Acquisition metadata records are under `~/ardustereo_sessions/`. Paired snapshots contain raw PGM images, CameraInfo and checksums.

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

The English console places each requested value beside the last confirmed readback. Edits stay in the input until applied. Capture and publish rates below the images show the recent measurement window. The source label identifies the camera, ROS synthetic input or offline demo, and the status indicator follows diagnostics. The new page was tested offline. The [hardware validation](../validation/README.md) was performed with the earlier release.
