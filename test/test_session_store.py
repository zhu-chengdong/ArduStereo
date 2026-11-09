#!/usr/bin/env python3
"""Offline file, pairing and failure tests. Does not import ROS or access devices."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import shutil
import uuid
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from session_store import ExactPair, SessionStore


def config():
    return {'width': 6, 'height': 2, 'split_column': 3,
            'left_frame_id': 'left_optical', 'right_frame_id': 'right_optical'}


def pair(stamp=100, padding=False):
    result = {}
    for side in ('left', 'right'):
        h = {'stamp_ns': stamp, 'frame_id': side + '_optical', 'seq': 1}
        result[side + '/image_raw'] = {'header': h, 'width': 3, 'height': 2, 'encoding': 'mono8',
            'step': 4 if padding else 3, 'data': bytes([1, 2, 3, 99, 4, 5, 6, 99] if padding else [1, 2, 3, 4, 5, 6])}
        result[side + '/camera_info'] = {'header': dict(h), 'width': 3, 'height': 2,
            'distortion_model': '', 'D': [], 'K': [0.0]*9, 'R': [0.0]*9, 'P': [0.0]*12}
    return result


class SessionFiles(unittest.TestCase):
    def setUp(self):
        # Python3.13's Windows700 ACL can exclude a restricted test token.
        self.base = Path(__file__).resolve().parents[1] / 'build/session-unit-fixtures'
        self.base.mkdir(parents=True, exist_ok=True)
        self.temp = self.base / ('session-test-' + uuid.uuid4().hex)
        self.temp.mkdir()
        self.stores = []

    def tearDown(self):
        for store in self.stores:
            store.finish()
        # Validate the exact recursive cleanup target before removing test data.
        assert not self.temp.is_symlink() and self.temp.resolve().parent == self.base.resolve()
        shutil.rmtree(str(self.temp))

    def store(self):
        s = SessionStore(str(self.temp), b'fourcc: GREY\n', config(), {'version': 'test'})
        self.stores.append(s)
        return s

    def test_unique_sessions_and_exact_configuration_copy(self):
        a, b = self.store(), self.store()
        self.assertNotEqual(a.path, b.path)
        self.assertEqual((a.path / 'requested.yaml').read_bytes(), b'fourcc: GREY\n')
        self.assertFalse((a.path / 'summary.json').exists())

    def test_out_of_order_messages_require_exact_stamp_and_all_four(self):
        cache, p = ExactPair(), pair()
        self.assertIsNone(cache.add('right/camera_info', p['right/camera_info']))
        self.assertIsNone(cache.add('left/image_raw', p['left/image_raw']))
        self.assertIsNone(cache.add('right/image_raw', p['right/image_raw']))
        self.assertEqual(cache.add('left/camera_info', p['left/camera_info']), p)

    def test_mismatch_never_pairs_and_pending_memory_is_bounded(self):
        cache = ExactPair(2)
        for n in range(1, 8):
            self.assertIsNone(cache.add('left/image_raw', pair(n)['left/image_raw']))
        self.assertLessEqual(len(cache.pending), 2)
        self.assertIsNone(cache.add('right/image_raw', pair(8)['right/image_raw']))

    def test_duplicates_and_bad_timestamps_are_rejected(self):
        cache, p = ExactPair(), pair()
        cache.add('left/image_raw', p['left/image_raw'])
        with self.assertRaises(ValueError):
            cache.add('left/image_raw', p['left/image_raw'])
        with self.assertRaises(ValueError):
            cache.add('left/image_raw', pair(0)['left/image_raw'])

    def test_snapshot_strips_padding_and_commits_complete_manifest(self):
        store = self.store()
        saved = store.snapshot(pair(padding=True))
        self.assertEqual((saved / 'left.pgm').read_bytes(), b'P5\n3 2\n255\n\x01\x02\x03\x04\x05\x06')
        m = json.loads((saved / 'snapshot.json').read_text())
        self.assertEqual(m['status'], 'COMPLETE')
        self.assertEqual(m['stamp_ns'], 100)
        for x in m['files']:
            self.assertEqual(hashlib.sha256((saved / x['path']).read_bytes()).hexdigest(), x['sha256'])

    def test_snapshot_contract_failures_leave_no_success_directory(self):
        store = self.store()
        cases = []
        p = pair(); p['right/camera_info']['header']['stamp_ns'] = 101; cases.append(p)
        p = pair(); p['left/image_raw']['data'] = b'bad'; cases.append(p)
        p = pair(); p['left/image_raw']['encoding'] = 'rgb8'; cases.append(p)
        p = pair(); p['left/camera_info']['width'] = 7; cases.append(p)
        p = pair(); p['left/image_raw']['header']['frame_id'] = 'wrong'; cases.append(p)
        for p in cases:
            with self.assertRaises(ValueError):
                store.snapshot(p)
        self.assertEqual(list((store.path / 'snapshots').iterdir()), [])

    def test_failed_disk_write_never_claims_complete_pair(self):
        store = self.store()
        with patch('session_store.os.fsync', side_effect=OSError('injected disk failure')):
            with self.assertRaises(OSError):
                store.snapshot(pair())
        self.assertFalse(list((store.path / 'snapshots').glob('pair_*')))
        self.assertEqual(store.snapshots, 0)

    def test_summary_labels_last_observed_counters_and_close_is_idempotent(self):
        store = self.store()
        store.diagnostic({'received_monotonic_ns': 200, 'values': {'state': 'running', 'published_pairs': '17', 'capture_fps': '20'}})
        store.finish(); store.finish()
        summary = json.loads((store.path / 'summary.json').read_text())
        self.assertEqual(summary['last_observed_published_pairs'], '17')
        self.assertFalse(summary['counters_are_final'])
        with self.assertRaises(RuntimeError):
            store.snapshot(pair())
        store.diagnostic({'values': {'state': 'error'}})  # Late callbacks cannot append.

    def test_observed_terminal_state_is_distinguished(self):
        store = self.store()
        store.diagnostic({'received_monotonic_ns': 200, 'values': {'state': 'stopped', 'published_pairs': '18'}})
        store.finish()
        self.assertTrue(json.loads((store.path / 'summary.json').read_text())['counters_are_final'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
