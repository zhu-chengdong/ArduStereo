# Compatibility and evidence boundary

| Combination | Evidence |
|---|---|
| Jetson Orin Nano8GB / Ubuntu20.04 / L4T35.5.0 / kernel5.10.192-tegra / Noetic / OpenCV4.5.4 / B0266 | ARM build/tests, startup raw controls, split interfaces, short snapshots/restart, 60-minute capture and 10-minute comparison |
| Isolated x86_64 Ubuntu20.04 / Noetic / OpenCV4.2.0 | Hardware-free build, core/ROS/file tests; not another physical-camera platform |
| Other Jetson models, JetPack/L4T versions, Arducam firmware/driver variants or cameras | Not physically validated by this release |
| ROS2 | No ROS2 adapter supplied |

The observed format is packed GREY2560x800 with two1280x800 halves. Optional mono16 code paths have contract tests but no B0266 hardware acceptance here. Explicit GREY matters: the untouched legacy node negotiated Y16 even after an external pre-open format request.

The observed Arducam module SHA256 was `cb4523d34994ebf273fb90c75b3d1ef1e381549bf3dbf417bac0b8ce24da9587`. This fingerprint identifies the tested module; it does not establish a general driver version or permission to redistribute that binary.

Camera calibration, physical left/right identity and sensor exposure synchronization require separate rig-specific verification. Software splitting and equal host timestamps do not supply that evidence. No broad stability, hot-plug reconnect, depth or SLAM-accuracy claim is made from these runs.
