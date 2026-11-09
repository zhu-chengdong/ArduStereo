"""ROS-free session files and exact stereo pairing. Standard library only."""
import collections
import datetime
import hashlib
import json
import os
from pathlib import Path
import threading
import time
import uuid


def atomic_json(path, value):
    raw = (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')
    temporary = path.with_name('.' + path.name + '-' + uuid.uuid4().hex)
    with temporary.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(str(temporary), str(path))


class ExactPair:
    TOPICS = ('left/image_raw', 'right/image_raw', 'left/camera_info', 'right/camera_info')

    def __init__(self, capacity=8):
        self.capacity = capacity
        self.pending = collections.OrderedDict()

    def add(self, topic, value):
        if topic not in self.TOPICS:
            raise ValueError('Unknown snapshot topic')
        stamp = value['header']['stamp_ns']
        if type(stamp) is not int or stamp <= 0:
            raise ValueError('Snapshot requires a positive exact timestamp')
        pair = self.pending.setdefault(stamp, {})
        if topic in pair:
            raise ValueError('Duplicate snapshot message for one timestamp')
        pair[topic] = value
        if all(key in pair for key in self.TOPICS):
            del self.pending[stamp]
            return pair
        while len(self.pending) > self.capacity:
            self.pending.popitem(last=False)
        return None


def validate_pair(pair, config):
    stamp = None
    for side, width in [('left', config['split_column']),
                        ('right', config['width'] - config['split_column'])]:
        image, info = pair[side + '/image_raw'], pair[side + '/camera_info']
        header = image['header']
        if info['header']['stamp_ns'] != header['stamp_ns'] or info['header']['frame_id'] != header['frame_id']:
            raise ValueError('Image and CameraInfo headers differ')
        if stamp is not None and stamp != header['stamp_ns']:
            raise ValueError('Left and right timestamps differ')
        stamp = header['stamp_ns']
        if not header['frame_id'] or header['frame_id'] != config[side + '_frame_id']:
            raise ValueError('Snapshot frame ID differs from configuration')
        if (image['width'], image['height']) != (width, config['height']):
            raise ValueError('Snapshot dimensions differ from configuration')
        if (info['width'], info['height']) != (width, config['height']):
            raise ValueError('CameraInfo dimensions differ from image')
        if image['encoding'] != 'mono8' or image['step'] < width:
            raise ValueError('Only valid mono8 images can be saved')
        if len(image['data']) != image['step'] * image['height']:
            raise ValueError('Snapshot image data length differs from step*height')
    return stamp


class SessionStore:
    def __init__(self, output_root, config_bytes, parameters, metadata):
        self.lock = threading.RLock()
        self.started = time.monotonic_ns()
        self.closed = False
        self.latest = None
        self.snapshots = 0
        self.parameters = parameters
        now = datetime.datetime.now(datetime.timezone.utc)
        root = Path(output_root).expanduser()
        root.mkdir(parents=True, exist_ok=True)
        self.path = root / ('session_' + now.strftime('%Y%m%dT%H%M%SZ_') + uuid.uuid4().hex[:12])
        self.path.mkdir(mode=0o700 if os.name == 'posix' else 0o777)
        (self.path / 'snapshots').mkdir()
        with (self.path / 'requested.yaml').open('xb') as stream:
            stream.write(config_bytes)
        atomic_json(self.path / 'parameters.json', parameters)
        atomic_json(self.path / 'session.json', dict(metadata,
            session_id=self.path.name, started_system_utc=now.isoformat(),
            system_clock_plausible=now.year >= 2020, started_monotonic_ns=self.started,
            config_sha256=hashlib.sha256(config_bytes).hexdigest(),
            completion_rule='summary.json exists only after graceful tools shutdown',
            image_recording='on-demand exact-stamp stereo snapshots only'))
        self.stream = (self.path / 'diagnostics.jsonl').open('x', encoding='utf-8')

    def diagnostic(self, event):
        with self.lock:
            if self.closed:
                return
            self.stream.write(json.dumps(event, ensure_ascii=False, allow_nan=False) + '\n')
            self.stream.flush()
            self.latest = event

    def controls(self, value):
        with self.lock:
            if not self.closed:
                atomic_json(self.path / 'initial_control_readback.json', value)

    def snapshot(self, pair):
        with self.lock:
            if self.closed:
                raise RuntimeError('Session is closing')
            stamp = validate_pair(pair, self.parameters)
            ident = 'pair_' + str(stamp) + '_' + uuid.uuid4().hex[:8]
            temporary = self.path / 'snapshots' / ('.pending_' + ident)
            target = self.path / 'snapshots' / ident
            temporary.mkdir()
            try:
                files = []
                for side in ('left', 'right'):
                    image = pair[side + '/image_raw']
                    name = side + '.pgm'
                    packed = b''.join(image['data'][y*image['step']:y*image['step']+image['width']]
                                      for y in range(image['height']))
                    raw = ('P5\n%d %d\n255\n' % (image['width'], image['height'])).encode('ascii') + packed
                    with (temporary / name).open('xb') as stream:
                        stream.write(raw)
                        stream.flush()
                        os.fsync(stream.fileno())
                    files.append({'path': name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()})
                    info_path = temporary / (side + '_camera_info.json')
                    atomic_json(info_path, pair[side + '/camera_info'])
                    files.append({'path': info_path.name, 'bytes': info_path.stat().st_size,
                                  'sha256': hashlib.sha256(info_path.read_bytes()).hexdigest()})
                atomic_json(temporary / 'snapshot.json', {'status': 'COMPLETE', 'stamp_ns': stamp,
                    'pairing': 'exact image/CameraInfo timestamp equality, not exposure synchrony', 'files': files})
                temporary.rename(target)
                self.snapshots += 1
                return target
            except Exception:
                # An incomplete directory remains visibly pending, never a successful pair.
                raise

    def finish(self):
        with self.lock:
            if self.closed:
                return
            self.closed = True
            self.stream.flush()
            os.fsync(self.stream.fileno())
            self.stream.close()
            values = self.latest['values'] if self.latest else {}
            terminal = values.get('state') in ('stopped', 'error')
            atomic_json(self.path / 'summary.json', {
                'tools_shutdown': 'graceful', 'tools_elapsed_seconds': (time.monotonic_ns()-self.started)/1e9,
                'snapshot_pairs_saved': self.snapshots,
                'last_observed_diagnostic': self.latest,
                'last_observed_published_pairs': values.get('published_pairs'),
                'last_observed_capture_fps': values.get('capture_fps'),
                'capture_terminal_state_observed': terminal,
                'counters_are_final': terminal,
                'counter_note': 'Last received diagnostic counters; capture and tools may stop simultaneously.'})
