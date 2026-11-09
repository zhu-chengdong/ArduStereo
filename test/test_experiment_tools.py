#!/usr/bin/env python3
"""ROS integration tests with synthetic/no publishers; never opens a camera."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
import unittest
import uuid

import rosgraph
import rospy
import rostest
from sensor_msgs.msg import CameraInfo, Image
from std_srvs.srv import Trigger


def parameters():
    return dict(source='synthetic', device='unused', width=16, height=8, split_column=8,
        input_format='mono8', mono16_shift=-1, fourcc='', buffer_size=1, warmup_frames=0,
        queue_size=1, max_read_failures=10, publish_composite=True, composite_frame_id='camera',
        left_frame_id='left_optical', right_frame_id='right_optical', left_camera_info_url='',
        right_camera_info_url='', require_calibration=False, synthetic_fps=20.0,
        diagnostics_period=.1, diagnostics_window=.5, frame_timeout=.5,
        diagnostic_expected_fps=0.0, diagnostic_min_fps_ratio=.9)


class Run:
    def __init__(self, standalone=False, root_is_file=False, enabled=True):
        self.ns = '/tools_test_' + uuid.uuid4().hex[:10]
        self.root = Path(os.environ.get('ARDUSTEREO_TEST_ARTIFACTS', os.environ.get('ROS_HOME', '/tmp'))) / self.ns.lstrip('/')
        self.root.mkdir(parents=True)
        self.config = parameters()
        self.config_file = self.root / 'config.yaml'
        self.config_file.write_text(json.dumps(self.config))  # JSON is a YAML subset.
        self.output = self.root / 'sessions'
        if root_is_file:
            self.output.write_text('not a directory')
        self.log = (self.root / 'process.log').open('xb')
        if standalone:
            package = subprocess.check_output(['rospack', 'find', 'ardustereo'], text=True).strip()
            rospy.set_param(self.ns + '/ardustereo', self.config)
            rospy.set_param(self.ns + '/experiment_tools', dict(capture_node='ardustereo',
                config_path=str(self.config_file), package_dir=package, session_root=str(self.output), snapshot_timeout=.8))
            argv = ['rosrun', 'ardustereo', 'experiment_tools.py', '__ns:=' + self.ns, '__name:=experiment_tools']
        else:
            argv = ['roslaunch', 'ardustereo', 'stereo.launch', 'config:=' + str(self.config_file),
                'camera_ns:=' + self.ns.lstrip('/'), 'session_root:=' + str(self.output),
                'session_tools:=' + ('true' if enabled else 'false'), 'snapshot_timeout:=0.8']
        self.process = subprocess.Popen(argv, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        self.closed = False

    def ready(self):
        rospy.wait_for_service(self.ns + '/save_snapshot', timeout=12)
        self.session = Path(rospy.get_param(self.ns + '/experiment_tools/session_directory'))
        return self

    def call(self):
        return rospy.ServiceProxy(self.ns + '/save_snapshot', Trigger)()

    def image_subscribers(self):
        state = rosgraph.Master('/experiment_tools_test').getSystemState()
        return {topic: nodes for topic, nodes in state[1] if topic.startswith(self.ns + '/') and topic.endswith('image_raw')}

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=2)
                raise AssertionError('Owned tools test did not stop after SIGINT')
        self.log.close()
        if rospy.has_param(self.ns):
            rospy.delete_param(self.ns)


class ToolsContract(unittest.TestCase):
    def setUp(self):
        self.runs = []

    def tearDown(self):
        for run in reversed(self.runs):
            run.close()

    def start(self, **kwargs):
        run = Run(**kwargs)
        self.runs.append(run)
        return run

    def wait(self, predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.03)
        self.fail('Condition timed out')

    def test_session_two_fresh_snapshots_and_subscription_release(self):
        run = self.start().ready()
        self.wait(lambda: (run.session / 'initial_control_readback.json').exists())
        self.assertEqual(json.loads((run.session / 'initial_control_readback.json').read_text())['status'], 'NOT_APPLICABLE_SYNTHETIC')
        self.assertFalse(any(run.ns + '/experiment_tools' in nodes for nodes in run.image_subscribers().values()))
        stamps = []
        for _ in range(2):
            response = run.call()
            self.assertTrue(response.success, response.message)
            folder = Path(response.message)
            m = json.loads((folder / 'snapshot.json').read_text())
            stamps.append(m['stamp_ns'])
            for x in m['files']:
                self.assertEqual(hashlib.sha256((folder / x['path']).read_bytes()).hexdigest(), x['sha256'])
            left = (folder / 'left.pgm').read_bytes().split(b'\n', 3)[3]
            right = (folder / 'right.pgm').read_bytes().split(b'\n', 3)[3]
            self.assertEqual(len(left), 64)
            self.assertTrue(all((b-a) % 256 == 8 for a, b in zip(left, right)))
            for side in ('left', 'right'):
                info = json.loads((folder / (side + '_camera_info.json')).read_text())
                self.assertEqual(info['header']['stamp_ns'], m['stamp_ns'])
            self.wait(lambda: not any(run.ns + '/experiment_tools' in nodes for nodes in run.image_subscribers().values()))
        self.assertLess(stamps[0], stamps[1])
        run.close()
        summary = json.loads((run.session / 'summary.json').read_text())
        self.assertEqual(summary['snapshot_pairs_saved'], 2)
        self.assertIn('counters_are_final', summary)
        self.assertEqual(summary['tools_shutdown'], 'graceful')

    def test_timeout_busy_and_cleanup_without_image_publishers(self):
        run = self.start(standalone=True).ready()
        responses = []
        worker = threading.Thread(target=lambda: responses.append(run.call()))
        worker.start()
        self.wait(lambda: any(run.ns + '/experiment_tools' in nodes for nodes in run.image_subscribers().values()))
        busy = run.call()
        self.assertFalse(busy.success)
        self.assertIn('already active', busy.message)
        worker.join(3)
        self.assertFalse(worker.is_alive())
        self.assertFalse(responses[0].success)
        self.assertIn('timeout', responses[0].message)
        self.wait(lambda: not any(run.ns + '/experiment_tools' in nodes for nodes in run.image_subscribers().values()))
        self.assertEqual(list((run.session / 'snapshots').glob('pair_*')), [])

    def test_mismatched_stamps_rejected_then_next_request_recovers(self):
        run = self.start(standalone=True).ready()
        publishers = {}
        for side in ('left', 'right'):
            publishers[side + '/image_raw'] = rospy.Publisher(run.ns + '/' + side + '/image_raw', Image, queue_size=2)
            publishers[side + '/camera_info'] = rospy.Publisher(run.ns + '/' + side + '/camera_info', CameraInfo, queue_size=2)
        stop = threading.Event()
        aligned = threading.Event()

        def publish():
            stamp = 1000000000
            while not stop.is_set():
                stamp += 1000
                for side in ('left', 'right'):
                    value = stamp if aligned.is_set() or side == 'left' else stamp + 1
                    image = Image()
                    image.header.stamp = rospy.Time(value // 1000000000, value % 1000000000)
                    image.header.frame_id = side + '_optical'
                    image.width, image.height, image.step, image.encoding = 8, 8, 8, 'mono8'
                    image.data = bytes([42]*64)
                    info = CameraInfo()
                    info.header = image.header
                    info.width, info.height = 8, 8
                    publishers[side + '/image_raw'].publish(image)
                    publishers[side + '/camera_info'].publish(info)
                stop.wait(.03)

        worker = threading.Thread(target=publish)
        worker.start()
        try:
            mismatch = run.call()
            self.assertFalse(mismatch.success)
            self.assertEqual(list((run.session / 'snapshots').glob('pair_*')), [])
            aligned.set()
            recovered = run.call()
            self.assertTrue(recovered.success, recovered.message)
        finally:
            stop.set()
            worker.join(2)
            for pub in publishers.values():
                pub.unregister()

    def test_record_path_failure_does_not_stop_capture(self):
        run = self.start(root_is_file=True)
        a = rospy.wait_for_message(run.ns + '/left/image_raw', Image, timeout=10)
        b = rospy.wait_for_message(run.ns + '/left/image_raw', Image, timeout=5)
        self.assertLess(a.header.stamp.to_nsec(), b.header.stamp.to_nsec())
        self.assertIsNone(run.process.poll())

    def test_optional_tools_disabled_keeps_original_capture_path(self):
        run = self.start(enabled=False)
        image = rospy.wait_for_message(run.ns + '/left/image_raw', Image, timeout=10)
        self.assertEqual(image.width, 8)
        self.assertFalse(rospy.has_param(run.ns + '/experiment_tools/session_directory'))
        self.assertFalse(run.output.exists())


if __name__ == '__main__':
    rospy.init_node('experiment_tools_contract', anonymous=True)
    rostest.rosrun('ardustereo', 'experiment_tools', ToolsContract)
