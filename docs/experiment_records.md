# 自动记录与成对快照

## 会话目录

`stereo.launch`默认启动独立的experiment_tools辅助节点。`session_root`默认是 `$(env HOME)/ardustereo_sessions`，每次创建新目录，不覆盖旧记录。需要关闭这两个辅助功能时可传 `session_tools:=false`；原采集路径继续运行。辅助节点记录路径错误会明确报错，不强制停止采集节点。

| 文件 | 内容 |
|---|---|
| requested.yaml | 本次launch配置文件的原始字节 |
| parameters.json | 采集节点实际加载的私有参数 |
| session.json | 软件版本、环境、启动时刻、配置哈希及时间可信性标记 |
| initial_control_readback.json | 首次观测running后只读取得的三个原始控制值；失败标UNAVAILABLE，合成源标NOT_APPLICABLE_SYNTHETIC |
| diagnostics.jsonl | 仅当前采集节点的诊断，每条带接收单调时刻 |
| summary.json | 辅助节点正常关闭后才产生，保存运行时长、已保存快照数及最后观测诊断 |
| snapshots/pair_* | 按需保存的完整双目快照 |

没有summary.json不能认定会话完整关闭。`last_observed_published_pairs`来自ROS诊断原始字符串，须结合诊断截止时刻使用；`counters_are_final`仅在最后确实收到stopped/error终态时为true。此标记不证明传感器零丢帧，也不代替采集节点退出码。初始控制读回只读一次，不代表持续监控所有外部参数变化。

## 快照服务

默认服务为 `/camera/save_snapshot`，类型std_srvs/Trigger。改变camera_ns时服务随之改变。调用期间临时订阅左右图与CameraInfo，按完全相同的stamp_ns配对，缓存最多8个时间戳；默认超时3秒。只接受符合当前尺寸、frame_id和数据长度的mono8图像。默认常态下没有图像订阅。

每次成功产生left.pgm、right.pgm、left_camera_info.json、right_camera_info.json和snapshot.json。PGM保持8位像素，CameraInfo JSON保留header、尺寸、模型、D/K/R/P、binning和ROI；它是ROS消息内容记录，不宣称已经完成物理标定或可直接作为camera_info_manager的标定YAML。清单含各文件字节数和SHA256。

文件先写入隐藏.pending目录，完整关闭后才将目录改为pair_*并返回成功。失败留下的.pending数据不作为完整快照；并发请求返回忙，不积压后台图像写入。

## 用于SLAM实验

记录用于追溯采集设置，快照用于检查画面、左右关系和准备标定样本。它们不连续保存SLAM图像序列；连续算法输入仍使用现有ROS话题或独立录包流程。本版没有新增录包、增强、深度或SLAM算法。

`system_clock_plausible`仅检查年份是否早于2020，不是准确时钟认证。当前Jetson年限异常会标false，仍保留原始时间和单调时刻，未修改系统时间。
