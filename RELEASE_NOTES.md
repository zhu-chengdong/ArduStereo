# ArduStereo 0.4.1 — Choose where to save recordings

Click **Choose folder…** next to **Save to**, browse the camera computer and click **Use this folder**. Bags are written directly into the chosen directory. Use **Stop recording** to finish a segment before choosing another location. The choice is remembered for subsequent launches using the same viewer root.

Tested with Arducam B0266 on Jetson Orin Nano and ROS Noetic. Two consecutive recordings in separate directories opened successfully. Their four stereo topics contained 46 and 53 messages each, with three diagnostics in each bag. The read-only directory was unavailable for selection, and finishing the recordings preserved live preview. Four recorder regression tests passed on ARM.
