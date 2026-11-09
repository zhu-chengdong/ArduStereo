# ArduStereo 0.2.2

面向已验证的 Arducam B0266 / Camarray HAT 与 ROS Noetic 组合。正常采集只需一条 launch，YAML 统一指定 GREY、2560×800 和原始帧率/曝光/增益值，默认 20/681/100。

构建和依赖见 [英文首页](README.md) 与 [安装说明](docs/installation.md)。先加载工作区环境，再运行 `roslaunch ardustereo stereo.launch`。默认同时提供拼接图、左右 mono8 图像、CameraInfo 和诊断；未加载物理标定时，CameraInfo 的 K[0] 为0。

每次启动自动记录配置、参数、版本、控制初始读回、诊断与退出摘要。采集中可运行 `rosservice call /camera/save_snapshot "{}"` 保存一组新到达、完全同时间戳的左右 PGM 及 CameraInfo JSON；这不是持续图像录制。

本版完成了当前 Jetson Orin Nano 8GB / Ubuntu20.04 / L4T35.5.0 / Noetic / OpenCV4.5.4 的实测。一小时收到71822个完整消息组，约19.9506FPS。20条短窗帧率告警有实际节奏波动依据，旧同格式基线输出中也有类似波动。原“全程无WARN”门槛未通过，不能改写成零告警或传感器零丢帧。详见 [验证资料](validation/README.md)。

现有厂商驱动和视频设备是前提。其他 Jetson、内核、驱动、ROS2 和物理标定/曝光同步不自动纳入已验证范围。源码没有打包驱动安装脚本或修改系统环境的步骤。
