"""Record full-resolution ROS topics independently of the camera preview."""
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import time
import uuid

from viewer_core import OwnedCapture


def check_bag(path, stereo_topics):
    import rosbag
    # Read the closed bag's connection and chunk summaries, not its pixel data.
    with rosbag.Bag(str(path), 'r', skip_index=True) as bag:
        for topic in stereo_topics:
            if not bag.get_message_count([topic]):
                raise ValueError('Missing stereo messages on ' + topic)


class BagRecorder:
    def __init__(self, root, namespace, camera_running):
        self.root = Path(root).resolve() / 'recordings'
        self.preference = self.root.parent.parent / 'recording-folder.json'
        self.destination = self.root
        self.identity = None
        if self.preference.is_file():
            pref = json.loads(self.preference.read_text(encoding='utf-8'))
            self.destination = Path(pref['path'])
            self.identity = tuple(pref['identity'])
        self.camera_running = camera_running
        self.images = [namespace + '/' + side + '/image_raw' for side in ('left', 'right')]
        self.topics = self.images + [namespace + '/' + side + '/camera_info'
                                     for side in ('left', 'right')] + ['/diagnostics']
        self.lock = threading.Lock()
        self.worker = None
        self.finish = threading.Event()
        self.state = 'idle'
        self.path = None
        self.message = ''
        self.began = None
        self.elapsed = 0.0

    def status(self):
        with self.lock:
            seconds = self.elapsed if self.began is None else time.monotonic() - self.began
            return dict(available=True, state=self.state, path=str(self.path or ''),
                        seconds=int(seconds), message=self.message, directory=str(self.destination))

    @staticmethod
    def _directory(value):
        if not isinstance(value, str) or not value or '\x00' in value:
            raise ValueError('Choose a folder')
        path = Path(value).expanduser()
        if not path.is_absolute():
            raise ValueError('Enter an absolute path on the camera computer')
        path = path.resolve(strict=True)
        if not path.is_dir():
            raise ValueError('Choose an existing directory')
        return path

    def browse(self, value):
        if value is None:
            with self.lock:
                initial = self.destination
            value = str(initial if initial.is_dir() else Path.home())
        path = self._directory(value)
        with os.scandir(str(path)) as entries:
            folders = sorted((entry.name for entry in entries
                              if not entry.name.startswith('.') and entry.is_dir()),
                             key=str.casefold)
        return dict(path=str(path), parent=str(path.parent), home=str(Path.home()),
                    folders=folders, writable=os.access(str(path), os.W_OK | os.X_OK))

    def select_directory(self, value):
        with self.lock:
            if self.worker and self.worker.is_alive():
                raise RuntimeError('Finish the current recording before changing folders')
            path = self._directory(value)
            with tempfile.TemporaryFile(dir=str(path)):
                pass
            stat = path.stat()
            identity = (stat.st_dev, stat.st_ino)
            self.preference.parent.mkdir(parents=True, exist_ok=True)
            pending = None
            try:
                with tempfile.NamedTemporaryFile(dir=str(self.preference.parent), mode='w',
                                                 encoding='utf-8', delete=False) as temp:
                    pending = Path(temp.name)
                    json.dump(dict(path=str(path), identity=identity), temp)
                os.replace(str(pending), str(self.preference))
            finally:
                if pending and pending.exists():
                    pending.unlink()
            self.destination, self.identity = path, identity
            return dict(success=True, directory=str(path))

    def start(self):
        with self.lock:
            if self.worker and self.worker.is_alive():
                raise RuntimeError('A recording is already in progress')
            if self.identity is None:
                self.root.mkdir(parents=True, exist_ok=True)
            else:
                stat = self.destination.stat()
                if (stat.st_dev, stat.st_ino) != self.identity:
                    raise RuntimeError('Recording folder changed. Choose the destination again.')
                with tempfile.TemporaryFile(dir=str(self.destination)):
                    pass
            name = 'stereo_' + time.strftime('%Y%m%d_%H%M%S_') + uuid.uuid4().hex[:12]
            self.path = self.destination / (name + '.bag')
            self.state = 'starting'
            self.message = ''
            self.began = None
            self.elapsed = 0.0
            self.finish.clear()
            self.worker = threading.Thread(target=self._run, name='rosbag-recorder')
            self.worker.start()

    def stop(self, wait=False):
        with self.lock:
            worker = self.worker
            if worker and worker.is_alive():
                self.finish.set()
                if self.state in ('starting', 'recording'):
                    self._finalizing()
        if wait and worker:
            worker.join()

    def _finalizing(self):
        if self.began is not None:
            self.elapsed = time.monotonic() - self.began
            self.began = None
        self.state = 'finalizing'

    def _run(self):
        bag_path = self.path
        owner = OwnedCapture(lambda p: ['rosbag', 'record', '--buffsize=128', '-O', p] + self.topics,
                             self.root / 'logs', grace=8)
        active = Path(str(bag_path) + '.active')
        error = ''
        try:
            if shutil.disk_usage(bag_path.parent).free <= 1024**3:
                raise RuntimeError('Less than 1 GiB free for rosbag')
            owner.start(bag_path)
            deadline = time.monotonic() + 12
            while not self.finish.wait(0.15):
                if not owner.running():
                    raise RuntimeError('Recorder exited unexpectedly')
                if not self.camera_running():
                    raise RuntimeError('Camera stopped during recording')
                if shutil.disk_usage(bag_path.parent).free <= 1024**3:
                    raise RuntimeError('Recording interrupted by low disk space')
                with self.lock:
                    if self.state == 'starting':
                        if active.is_file():
                            self.state = 'recording'
                            self.began = time.monotonic()
                        elif time.monotonic() > deadline:
                            raise RuntimeError('Timed out waiting for the bag to open')
        except Exception as exc:
            error = str(exc)
        finally:
            with self.lock:
                self._finalizing()
            try:
                cleanup = owner.stop()
                if cleanup['forced'] or cleanup['exit_code'] not in (0, None):
                    raise RuntimeError('Recorder did not exit cleanly')
                if active.exists() or not bag_path.is_file():
                    raise RuntimeError('No finalized bag was found')
                check_bag(bag_path, self.topics[:-1])
            except Exception as exc:
                error = '; '.join(filter(None, (error, str(exc))))
            with self.lock:
                self.message = error
                self.state = 'error' if error else 'saved'
