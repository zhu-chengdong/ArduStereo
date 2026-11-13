# ArduStereo 0.4.0 — Browser recording for ROS Noetic

Use a single launch to preview both views, adjust camera controls and record full-resolution stereo data. Click **Start recording** to create a ROS bag. Click **Stop recording** to save the segment and continue previewing. The console shows the recording time and the path on the ROS host. Stopping the camera or exiting the launch also finalizes the active segment.

**Install** in a sourced ROS Noetic catkin workspace with the prerequisites listed in the [installation guide](https://github.com/zhu-chengdong/ArduStereo/blob/v0.4.0/docs/installation.md). Choose the source ZIP or clone the v0.4.0 tag.

**Validation.** Local Ubuntu 20.04 / ROS Noetic tests used 2560 × 800 synthetic input at a target of 20 FPS and the real rosbag recorder. Three segments closed by stopping recording, stopping capture and exiting launch contained 39, 40 and 40 messages on each of the four stereo topics. All three bags contained diagnostics and opened successfully. Seventeen viewer tests and three recorder fault tests passed. Jetson Orin Nano / B0266 validation of this version's integrated recorder is pending. The previous hardware qualification is documented with the v0.3.0-startup-init-fix1 release.

The attached source ZIP is generated from the tagged commit. SHA256SUMS.txt covers the source ZIP and the machine-readable release-acceptance.json.
