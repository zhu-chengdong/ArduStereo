#!/usr/bin/env python3
"""No camera access. Run with rostest in a real ROS Noetic environment."""
import os
import subprocess
import threading
import time
import unittest
import uuid

import rospy
import rostest
from sensor_msgs.msg import CameraInfo, Image


class StereoContract(unittest.TestCase):
    def setUp(self):
        self.lock = threading.Lock()
        self.frames = {}
        self.silent_left = []
        self.silent_composite = []
        self.calibrated = {}
        self.subs = []
        for name, cls in [("image_raw", Image), ("left/image_raw", Image),
                          ("right/image_raw", Image), ("left/camera_info", CameraInfo),
                          ("right/camera_info", CameraInfo)]:
            self.subs.append(rospy.Subscriber("/test_camera/" + name, cls,
                lambda m, n=name: self.receive(self.frames, n, m), queue_size=100))
        for side in ("left", "right"):
            self.subs.append(rospy.Subscriber("/calibrated_camera/" + side + "/camera_info",
                CameraInfo, lambda m, n=side: self.receive(self.calibrated, n, m), queue_size=100))
        self.subs.append(rospy.Subscriber("/silent_camera/left/image_raw", Image,
            lambda m: self.collect(self.silent_left, m), queue_size=100))
        self.subs.append(rospy.Subscriber("/silent_camera/image_raw", Image,
            lambda m: self.collect(self.silent_composite, m), queue_size=100))

    def tearDown(self):
        for sub in self.subs:
            sub.unregister()

    def receive(self, target, name, msg):
        with self.lock:
            target.setdefault(msg.header.stamp.to_nsec(), {})[name] = msg
            while len(target) > 300:
                del target[min(target)]

    def collect(self, target, msg):
        with self.lock:
            target.append(msg)

    def wait_for(self, predicate, seconds=12):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not rospy.is_shutdown():
            with self.lock:
                if predicate():
                    return
            time.sleep(0.02)
        self.fail("Timed out waiting for synthetic ROS messages")

    def test_image_and_camera_info_contract(self):
        self.wait_for(lambda: sum(len(v) == 5 for v in self.frames.values()) >= 10)
        with self.lock:
            complete = [(t, dict(v)) for t, v in self.frames.items() if len(v) == 5]
        for stamp, msgs in complete:
            self.assertGreater(stamp, 0)
            raw = msgs["image_raw"]
            left, right = msgs["left/image_raw"], msgs["right/image_raw"]
            self.assertEqual((raw.width, raw.height), (16, 8))
            joined = b"".join(bytes(left.data[y*8:(y+1)*8]) +
                              bytes(right.data[y*8:(y+1)*8]) for y in range(8))
            self.assertEqual(joined, bytes(raw.data))
            for y in range(8):
                for x in range(16):
                    self.assertEqual(raw.data[y*16+x], (raw.data[0]+x+17*y) % 256)
            for side, img in (("left", left), ("right", right)):
                info = msgs[side+"/camera_info"]
                self.assertEqual((img.width, img.height, img.step), (8, 8, 8))
                self.assertEqual((img.encoding, img.is_bigendian, len(img.data)), ("mono8", 0, 64))
                self.assertEqual(img.header.stamp.to_nsec(), stamp)
                self.assertEqual(info.header.stamp, img.header.stamp)
                self.assertEqual(info.header.frame_id, img.header.frame_id)
                self.assertEqual(img.header.frame_id, "synthetic_"+side+"_optical_frame")
                self.assertEqual((info.width, info.height), (8, 8))
                self.assertTrue(all(v == 0 for v in list(info.K)+list(info.R)+list(info.P)+list(info.D)))

    def test_composite_can_be_disabled(self):
        self.wait_for(lambda: len(self.silent_left) >= 10)
        with self.lock:
            self.assertEqual(len(self.silent_composite), 0)

    def test_explicit_calibration_preserves_projection(self):
        self.wait_for(lambda: sum(len(v) == 2 for v in self.calibrated.values()) >= 5)
        with self.lock:
            pairs = [dict(v) for v in self.calibrated.values() if len(v) == 2]
        for pair in pairs:
            self.assertEqual(pair["left"].K[0], 5)
            self.assertEqual(pair["right"].P[3], -0.5)
            self.assertEqual(pair["left"].P[3], 0)

    def test_invalid_parameters_exit_without_opening_hardware(self):
        package_path = subprocess.check_output(["rospack", "find", "ardustereo"], text=True).strip()
        cases = [(["_split_column:=0"], "split_column"),
                 (["_diagnostics_period:=0"], "Invalid diagnostics_period"),
                 (["_diagnostic_expected_fps:=-1"], "Invalid diagnostic"),
                 (["_frame_timeout:=0"], "Invalid diagnostic"),
                 (["_exposure:=681"], "Hardware controls require source=v4l2"),
                 (["_frame_rate:=0"], "positive raw integers"),
                 (["_analogue_gain:=0"], "positive raw integers"),
                 (["_require_calibration:=true"], "Calibration URL is required"),
                 (["_width:=wrong_type"], "wrong type"),
                 (["_width:=16.5"], "wrong type"),
                 (["_left_camera_info_url:=file://"+package_path+"/test/fixtures/synthetic_left.yaml",
                   "_right_camera_info_url:=file://"+package_path+"/test/fixtures/synthetic_right.yaml",
                   "_width:=18", "_height:=8", "_split_column:=8"], "Calibration dimensions"),
                 (["_source:=v4l2", "_device:=/ardustereo-nonexistent-test-device"],
                  "Cannot open V4L2 device")]
        for params, expected in cases:
            source = [] if any(p.startswith("_source:=") for p in params) else ["_source:=synthetic"]
            command = ["rosrun", "ardustereo", "ardustereo_node",
                       "__name:=invalid_"+uuid.uuid4().hex] + source + params
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=10, env=os.environ.copy())
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(expected, result.stdout)


if __name__ == "__main__":
    rospy.init_node("test_stereo_contract")
    rostest.rosrun("ardustereo", "stereo_contract", StereoContract)
