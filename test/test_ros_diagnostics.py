#!/usr/bin/env python3
"""Real ROS messages with synthetic/injected sources; no camera is opened."""
import os
import signal
import subprocess
import tempfile
import threading
import time
import unittest
import uuid

import rospy
import rostest
from diagnostic_msgs.msg import DiagnosticArray
from sensor_msgs.msg import Image


class NodeRun:
    def __init__(self, scenario=None, extra=None):
        self.ns = "/diag_" + uuid.uuid4().hex
        self.lock = threading.Lock()
        self.statuses, self.images = [], []
        self.log = tempfile.TemporaryFile(mode="w+")
        self.subs = [rospy.Subscriber("/diagnostics", DiagnosticArray, self.diagnostic, queue_size=100),
                     rospy.Subscriber(self.ns + "/left/image_raw", Image, self.image, queue_size=100)]
        params = {"source": "synthetic", "width": 16, "height": 8, "split_column": 8,
                  "warmup_frames": 0, "diagnostics_period": 0.05, "diagnostics_window": 1.0,
                  "frame_timeout": 0.3, "max_read_failures": 3}
        params.update(extra or {})
        for key, value in params.items():
            rospy.set_param(self.ns + "/ardustereo/" + key, value)
        exe = "ardustereo_fault_test_node" if scenario else "ardustereo_node"
        command = ["rosrun", "ardustereo", exe]
        if scenario:
            command.append(scenario)
        command += ["__ns:=" + self.ns, "__name:=ardustereo"]
        self.process = subprocess.Popen(command, stdout=self.log, stderr=subprocess.STDOUT,
                                        env=os.environ.copy())

    def diagnostic(self, message):
        for status in message.status:
            if status.name == self.ns + "/ardustereo: acquisition":
                values = {v.key: v.value for v in status.values}
                with self.lock:
                    self.statuses.append((time.monotonic(), status.level, values))

    def image(self, message):
        with self.lock:
            self.images.append((time.monotonic(), message.header.stamp.to_nsec()))

    def records(self):
        with self.lock:
            return list(self.statuses), list(self.images)

    def wait(self, predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not rospy.is_shutdown():
            if predicate(*self.records()):
                return
            time.sleep(0.02)
        raise AssertionError("Diagnostic condition not reached; " + self.output())

    def output(self):
        self.log.flush()
        self.log.seek(0)
        return self.log.read()

    def close(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
                raise AssertionError("Injected/synthetic node failed to stop after SIGINT")
        for sub in self.subs:
            sub.unregister()
        rospy.delete_param(self.ns)


class DiagnosticContract(unittest.TestCase):
    def test_production_synthetic_rates_and_unknown_device_drops(self):
        run = NodeRun(extra={"diagnostic_expected_fps": 20.0})
        try:
            run.wait(lambda statuses, images: len(images) >= 45 and
                     any(float(v["publication_window_seconds"]) >= 1 for _, _, v in statuses))
            statuses, images = run.records()
            valid = [(level, v) for _, level, v in statuses
                     if float(v["publication_window_seconds"]) >= 1 and v["state"] == "running"]
            self.assertTrue(valid)
            level, values = valid[-1]
            self.assertEqual(level, 0)
            self.assertEqual(values["device_drop_count"], "unavailable")
            self.assertEqual(values["subscriber_received_count"], "unavailable")
            self.assertEqual(values["rate_clock"], "steady_monotonic")
            self.assertEqual(values["image_timestamp"], "host_receive_not_sensor_exposure")
            for key in ("capture_fps", "publication_fps"):
                self.assertGreater(float(values[key]), 17)
                self.assertLess(float(values[key]), 23)
            observed = (len(images)-1)/(images[-1][0]-images[0][0])
            self.assertLess(abs(observed-float(values["publication_fps"])), 4)
            self.assertGreater(int(values["published_pairs"]), 0)
        finally:
            run.close()
        self.assertEqual(run.process.returncode, 0, run.output())
        self.assertIn("final state=stopped", run.output())

    def test_empty_read_recovery_has_no_stale_publication(self):
        run = NodeRun("recover", {"max_read_failures": 10})
        try:
            run.wait(lambda statuses, images:
                     any(v["state"] == "degraded" and int(v["empty_reads"]) > 0 for _, _, v in statuses) and
                     any(v["state"] == "running" and int(v["empty_reads"]) == 5 for _, _, v in statuses))
            statuses, images = run.records()
            for _, _, v in statuses:
                difference = int(v["captured_frames"]) - int(v["published_pairs"])
                self.assertIn(difference, (0, 1))  # Snapshot may fall during message construction.
                if int(v["consecutive_empty_reads"]) > 0:
                    self.assertEqual(difference, 0)
            self.assertEqual(len({stamp for _, stamp in images}), len(images))
            self.assertTrue(any(v["consecutive_empty_reads"] == "0" and v["empty_reads"] == "5"
                                for _, _, v in statuses))
        finally:
            run.close()

    def test_independent_diagnostics_during_finite_read_stall(self):
        run = NodeRun("finite_stall")
        try:
            run.wait(lambda statuses, images:
                     sum(v["state"] == "stale" and v["read_in_progress"] == "true"
                         for _, _, v in statuses) >= 3 and
                     any(v["state"] == "running" and int(v["captured_frames"]) >= 12
                         for _, _, v in statuses))
            statuses, _ = run.records()
            stale = [(level, v) for _, level, v in statuses if v["state"] == "stale"]
            self.assertTrue(all(level == 3 for level, _ in stale))
            self.assertTrue(all(v["captured_frames"] == "10" for _, v in stale))
            self.assertTrue(any(float(v["capture_fps"]) == 0 for _, v in stale))
        finally:
            run.close()

    def test_terminal_faults_exit_and_release_source(self):
        cases = [("open_exception", "startup", "read_attempts=0"),
                 ("empty_limit", "empty_read_limit", "empty_reads=3"),
                 ("read_exception", "source_read", "read_exceptions=1"),
                 ("bad_frame", "frame_validation", "frame_rejections=1")]
        for scenario, stage, counter in cases:
            with self.subTest(scenario=scenario):
                run = NodeRun(scenario)
                try:
                    code = run.process.wait(timeout=8)
                    output = run.output()
                    self.assertNotEqual(code, 0, output)
                    self.assertIn("final state=error error_stage=" + stage, output)
                    self.assertIn(counter, output)
                    self.assertEqual(output.count("FAULT_TEST_SOURCE_CLOSED"), 1, output)
                    if scenario != "open_exception":
                        self.assertIn("captured_frames=10 published_pairs=10", output)
                finally:
                    run.close()


if __name__ == "__main__":
    rospy.init_node("test_diagnostics_contract")
    rostest.rosrun("ardustereo", "diagnostics_contract", DiagnosticContract)
