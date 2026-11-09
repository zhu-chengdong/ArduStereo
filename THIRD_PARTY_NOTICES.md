# Provenance and dependencies

The OpenCV V4L2 acquisition lifecycle is derived from Chengdong Zhu's
`mono_cam_cpp_node.cpp`, distributed in the ROV V1 HardwareX source package
under MIT. The original file's SHA-256 is
`d65982d218cd8dea2da5ad1d295b7001040ee55916b7cdd71bc7fe114d7fd7de`.
The historical source copies are preserved outside this development directory.

ArduStereo does not include an Arducam kernel driver, firmware, device-tree
binary, ROS distribution, or OpenCV binary. Dependencies retain their upstream
licenses. In particular, consult the exact installed ROS, camera_info_manager,
OpenCV and Arducam packages before redistributing their binaries. The MIT
license in this directory applies to the ArduStereo source, not those projects.

Portable compiler/build tools used for the local acceptance run are kept in
a separate private acceptance directory. They are not runtime dependencies or
part of this software source package.
