#!/usr/bin/env python3
"""Automatic experiment metadata and on-demand stereo snapshots; no device setters."""
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import sys
import threading
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
from session_store import ExactPair, SessionStore

import rospy
from diagnostic_msgs.msg import DiagnosticArray
from sensor_msgs.msg import CameraInfo, Image
from std_srvs.srv import Trigger, TriggerResponse


def header(message):
    return {'stamp_ns': message.header.stamp.to_nsec(), 'frame_id': message.header.frame_id,
            'seq': message.header.seq}


def packet(message, image):
    result = {'header': header(message), 'width': message.width, 'height': message.height}
    if image:
        result.update(encoding=message.encoding, step=message.step,
                      is_bigendian=message.is_bigendian, data=bytes(message.data))
    else:
        result.update(distortion_model=message.distortion_model,
            D=list(message.D), K=list(message.K), R=list(message.R), P=list(message.P),
            binning_x=message.binning_x, binning_y=message.binning_y,
            roi={key: getattr(message.roi, key) for key in ['x_offset', 'y_offset', 'height', 'width', 'do_rectify']})
    return result


def read_controls(config):
    if config['source'] != 'v4l2':
        return {'status': 'NOT_APPLICABLE_SYNTHETIC', 'values': None}
    import fcntl
    fd = os.open(config['device'], os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        values = {}
        for name, ident in [('frame_rate', 0x00981906), ('exposure', 0x00980911), ('analogue_gain', 0x009e0903)]:
            raw = bytearray(struct.pack('=Ii', ident, 0))
            fcntl.ioctl(fd, 0xc008561b, raw, True)  # VIDIOC_G_CTRL only; never S_CTRL.
            values[name] = struct.unpack('=Ii', raw)[1]
        return {'status': 'READ', 'source': 'V4L2_G_CTRL after first observed running diagnostic', 'values': values}
    finally:
        os.close(fd)


class Tools:
    def __init__(self):
        self.stopping = False
        self.condition = threading.Condition()
        self.request_lock = threading.Lock()
        self.pending = None
        self.controls_attempted = False
        self.capture = rospy.resolve_name(rospy.get_param('~capture_node', 'ardustereo'))
        config_path = Path(rospy.get_param('~config_path'))
        parameters = rospy.get_param(self.capture)
        package = Path(rospy.get_param('~package_dir'))
        version = ET.parse(package / 'package.xml').getroot().findtext('version')
        metadata = {'software': 'ArduStereo', 'version': version, 'hostname': platform.node(),
            'architecture': platform.machine(), 'kernel': platform.release(),
            'capture_node': self.capture, 'config_source': str(config_path),
            'tools_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        self.store = SessionStore(rospy.get_param('~session_root'), config_path.read_bytes(), parameters, metadata)
        self.timeout = float(rospy.get_param('~snapshot_timeout', 3.0))
        if not .1 <= self.timeout <= 30:
            raise ValueError('snapshot_timeout must be0.1..30 seconds')
        rospy.on_shutdown(self.close)
        rospy.set_param('~session_directory', str(self.store.path))
        self.subscriber = rospy.Subscriber('/diagnostics', DiagnosticArray, self.diagnostic, queue_size=20)
        self.service = rospy.Service('save_snapshot', Trigger, self.snapshot)
        rospy.loginfo('ArduStereo session directory: %s', self.store.path)
        rospy.loginfo('Snapshot service: %s', rospy.resolve_name('save_snapshot'))

    def diagnostic(self, message):
        for status in message.status:
            if status.name != self.capture + ': acquisition' or self.stopping:
                continue
            event = {'received_monotonic_ns': time.monotonic_ns(), 'header_stamp_ns': message.header.stamp.to_nsec(),
                'level': status.level, 'message': status.message, 'values': {x.key: x.value for x in status.values}}
            try:
                self.store.diagnostic(event)
                if event['values'].get('state') == 'running' and not self.controls_attempted:
                    self.controls_attempted = True
                    try:
                        readback = read_controls(self.store.parameters)
                    except Exception as error:
                        readback = {'status': 'UNAVAILABLE', 'values': None, 'error': str(error)}
                        rospy.logwarn('Session control readback unavailable: %s', error)
                    readback['received_monotonic_ns'] = time.monotonic_ns()
                    self.store.controls(readback)
            except Exception as error:
                rospy.logerr_throttle(5, 'Session record write failed: %s', error)

    def receive(self, topic, message):
        with self.condition:
            if self.pending is None or self.pending['result'] is not None or self.pending['error'] is not None:
                return
            try:
                self.pending['result'] = self.pending['cache'].add(topic, packet(message, topic.endswith('image_raw')))
            except Exception as error:
                self.pending['error'] = str(error)
            self.condition.notify_all()

    def snapshot(self, request):
        if not self.request_lock.acquire(False):
            return TriggerResponse(False, 'A snapshot request is already active')
        subscribers = []
        try:
            with self.condition:
                if self.stopping:
                    return TriggerResponse(False, 'Session is stopping')
                self.pending = {'cache': ExactPair(), 'result': None, 'error': None}
            for topic in ExactPair.TOPICS:
                cls = Image if topic.endswith('image_raw') else CameraInfo
                subscribers.append(rospy.Subscriber(topic, cls,
                    lambda msg, topic=topic: self.receive(topic, msg), queue_size=2, buff_size=4*1024*1024))
            deadline = time.monotonic() + self.timeout
            with self.condition:
                while not self.stopping and self.pending['result'] is None and self.pending['error'] is None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    self.condition.wait(remaining)
                pair, error = self.pending['result'], self.pending['error']
                self.pending = None
            for sub in subscribers:
                sub.unregister()
            subscribers = []
            if pair is None or self.stopping:
                return TriggerResponse(False, error or 'No complete exact-stamp pair before timeout/session stop')
            saved = self.store.snapshot(pair)
            rospy.loginfo('Saved stereo snapshot: %s', saved)
            return TriggerResponse(True, str(saved))
        except Exception as error:
            rospy.logerr('Stereo snapshot failed: %s', error)
            return TriggerResponse(False, str(error))
        finally:
            for sub in subscribers:
                sub.unregister()
            with self.condition:
                self.pending = None
            self.request_lock.release()

    def close(self):
        with self.condition:
            self.stopping = True
            self.condition.notify_all()
        try:
            self.store.finish()
        except Exception as error:
            rospy.logerr('Session summary could not be saved: %s', error)


if __name__ == '__main__':
    rospy.init_node('experiment_tools')
    try:
        tools = Tools()
        rospy.spin()
    except Exception as error:
        rospy.logfatal('Experiment tools stopped: %s', error)
        sys.exit(1)
