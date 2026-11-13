# ArduStereo

**最新正式版 [v0.4.0](https://github.com/zhu-chengdong/ArduStereo/releases/latest)**　[下载源码 ZIP](https://github.com/zhu-chengdong/ArduStereo/releases/download/v0.4.0/ArduStereo-0.4.0-source.zip) · [安装与构建](README.md#build-and-use) · [历史版本](https://github.com/zhu-chengdong/ArduStereo/releases)

ArduStereo 在一个浏览器界面中提供 B0266 双目预览、参数调节和全分辨率 ROS 录包。完成构建并加载工作空间后，在一个终端运行 `roslaunch ardustereo viewer.launch`，然后在 Jetson 浏览器打开 `http://127.0.0.1:8765/`。

1. 点击 **Start camera**，确认左右画面和实际帧率。
2. 修改帧率、曝光或模拟增益，点击 **Apply settings**，查看驱动读回值。
3. 点击 **Start recording**，查看录制状态、时长和 ROS 主机上的文件路径。
4. 点击 **Stop recording**，等界面显示 **Saved**。此时可以继续预览、再录一段，或点击 **Stop camera**。

v0.4.0 已在本地 ROS Noetic 上通过全分辨率合成输入录包及退出清理测试。本版界面录包在 Jetson Orin Nano 与 B0266 上的实机复验待完成。此前设备上的测试对应 [v0.3.0-startup-init-fix1](https://github.com/zhu-chengdong/ArduStereo/releases/tag/v0.3.0-startup-init-fix1)。[使用说明](docs/viewer.md) · [发布说明](RELEASE_NOTES.md) · [兼容性](docs/compatibility.md)
