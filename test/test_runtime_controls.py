#!/usr/bin/env python3
"""Actual ROS services with explicitly synthetic controls, no camera access."""
import threading
import time
import unittest
import rospy
import rostest
from ardustereo.srv import GetControls, SetControls
from diagnostic_msgs.msg import DiagnosticArray


class RuntimeControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rospy.init_node('runtime_control_contracts')
        rospy.wait_for_service('/runtime_test/get_controls',10)
        cls.get=rospy.ServiceProxy('/runtime_test/get_controls',GetControls)
        cls.set=rospy.ServiceProxy('/runtime_test/set_controls',SetControls)

    def test_runtime_readback_and_rejected_write(self):
        before=self.get(); self.assertTrue(before.success); self.assertTrue(before.simulated)
        self.assertEqual(list(before.minimum),[1,0,0])
        after=self.set(10,700,101)
        self.assertTrue(after.success); self.assertTrue(after.simulated)
        self.assertEqual((after.frame_rate,after.exposure,after.analogue_gain),(10,700,101))
        failed=self.set(121,701,102)
        self.assertFalse(failed.success)
        actual=self.get(); self.assertEqual((actual.frame_rate,actual.exposure,actual.analogue_gain),(10,700,101))
        invalid=self.set(0,700,101); self.assertFalse(invalid.success)

    def test_rate_reference_changes_without_counter_reset(self):
        latest=[]; event=threading.Event()
        def received(msg):
            for status in msg.status:
                if status.name=='/runtime_test/ardustereo: acquisition':
                    latest.append({v.key:v.value for v in status.values}); event.set()
        sub=rospy.Subscriber('/diagnostics',DiagnosticArray,received,queue_size=20)
        try:
            self.assertTrue(event.wait(5)); before=int(latest[-1]['published_pairs'])
            self.assertTrue(self.set(15,710,105).success)
            deadline=time.monotonic()+9
            while time.monotonic()<deadline:
                if latest and float(latest[-1]['expected_fps_reference'])==15 and float(latest[-1]['capture_window_seconds'])>=5:
                    break
                time.sleep(.1)
            after=latest[-1]; self.assertEqual(float(after['expected_fps_reference']),15)
            self.assertGreaterEqual(float(after['capture_window_seconds']),5)
            self.assertGreater(int(after['published_pairs']),before)
            self.assertAlmostEqual(float(after['capture_fps']),15,delta=2)
        finally: sub.unregister()


if __name__=='__main__': rostest.rosrun('ardustereo','runtime_controls',RuntimeControls)
