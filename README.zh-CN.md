# ArduStereo

**最新正式版 [v0.4.1](https://github.com/zhu-chengdong/ArduStereo/releases/latest)**　[下载源码 ZIP](https://github.com/zhu-chengdong/ArduStereo/releases/download/v0.4.1/ArduStereo-0.4.1-source.zip) · [安装与构建](README.md#build-and-use) · [历史版本](https://github.com/zhu-chengdong/ArduStereo/releases)

ArduStereo 在一个浏览器界面中提供 B0266 双目预览、参数调节和全分辨率 ROS 录包。完成构建并加载工作空间后，在一个终端运行 `roslaunch ardustereo viewer.launch`，然后在 Jetson 浏览器打开 `http://127.0.0.1:8765/`。

1. 点击 **Start camera**，确认左右画面和实际帧率。
2. 修改帧率、曝光或模拟增益，点击 **Apply settings**，查看驱动读回值。
3. 在 **Save to** 旁点击 **Choose folder…**，浏览 Jetson 上的目录，点击 **Use this folder** 确认，再点击 **Start recording**，查看录制状态、时长和 ROS 主机上的文件路径。
4. 点击 **Stop recording**，等界面显示 **Saved**。此时可以继续预览、再录一段，或点击 **Stop camera**。

v0.4.1 已在 Jetson Orin Nano 与 B0266 上通过选目录与短录测试。两个指定目录内的 bag 均可打开，四路双目话题分别各有 46 条和 53 条消息。此前设备上的测试对应 [v0.3.0-startup-init-fix1](https://github.com/zhu-chengdong/ArduStereo/releases/tag/v0.3.0-startup-init-fix1)。[使用说明](docs/viewer.md) · [发布说明](RELEASE_NOTES.md) · [兼容性](docs/compatibility.md)
