"""Failure paths must never report a saved segment."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from viewer_recording import BagRecorder


class RecordingFaultTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.recorder = BagRecorder(self.directory.name, '/rig/stereo', lambda: True)

    def settle(self):
        self.recorder.worker.join(6)
        self.assertFalse(self.recorder.worker.is_alive())
        status = self.recorder.status()
        self.assertEqual(status['state'], 'error')
        return status

    def test_missing_tool(self):
        with patch.dict('os.environ', {'PATH': self.directory.name}):
            self.recorder.start()
            status = self.settle()
        self.assertIn('rosbag', status['message'])
        self.assertFalse(Path(status['path']).exists())

    def test_select_folder_persists_and_retains_old_choice_on_failure(self):
        base = Path(self.directory.name)
        dest = base / 'Run A 水下实验'
        dest.mkdir()
        rec = BagRecorder(base / 'viewer_one', '/camera', lambda: True)
        self.assertIn(dest.name, rec.browse(str(base))['folders'])
        rec.select_directory(str(dest))
        with self.assertRaises(FileNotFoundError):
            rec.select_directory(str(base / 'missing'))
        nxt = BagRecorder(base / 'viewer_two', '/camera', lambda: True)
        self.assertEqual(nxt.status()['directory'], str(dest.resolve()))
        with patch('viewer_recording.tempfile.TemporaryFile', side_effect=PermissionError('read-only')):
            with self.assertRaises(PermissionError):
                nxt.select_directory(str(base))
        self.assertEqual(nxt.status()['directory'], str(dest.resolve()))
        dest.rename(base / 'moved')
        with self.assertRaises(FileNotFoundError):
            nxt.start()
        self.assertFalse(dest.exists())

    def test_low_space(self):
        with patch('viewer_recording.shutil.disk_usage') as usage:
            usage.return_value.free = 60000
            self.recorder.start()
            self.assertIn('Less than 1 GiB free', self.settle()['message'])

    def test_partial_output_from_failed_process_is_preserved(self):
        cmd = Path(self.directory.name) / 'rosbag'
        cmd.write_text('#!/bin/sh\ntouch "$4.active"\nexit 3\n')
        cmd.chmod(0o755)
        import os
        with patch.dict(os.environ, {'PATH':self.directory.name + ':' + os.environ['PATH']}):
            self.recorder.start()
            status = self.settle()
        self.assertTrue(Path(status['path'] + '.active').exists())
        self.assertIn('exited unexpectedly', status['message'])
        log = next(self.recorder.root.glob('logs/*/launch.json'))
        self.assertIn('/rig/stereo/right/camera_info', json.loads(log.read_text())['argv'])
