"""Small viewer model, preview encoder and owned-process lifecycle. No ROS import."""
import base64
from collections import OrderedDict, deque
import copy
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import threading
import time
import uuid
import zlib
import yaml

CONTROL_KEYS = ('frame_rate', 'exposure', 'analogue_gain')


def controls(values):
    if not isinstance(values, dict) or set(values) != set(CONTROL_KEYS):
        raise ValueError('Expected frame_rate, exposure and analogue_gain')
    if any(type(v) is not int or v < 0 or v > 2147483647 for v in values.values()) or values['frame_rate'] < 1:
        raise ValueError('Controls must be raw nonnegative integers; frame_rate must be positive')
    return dict(values)


def load_configuration(path):
    raw = Path(path).read_bytes()
    if len(raw) > 65536:
        raise ValueError('Configuration too large')
    class UniqueLoader(yaml.SafeLoader):
        pass
    def unique(loader, node, deep=False):
        result = {}
        for k, v in node.value:
            key = loader.construct_object(k, deep=deep)
            if key in result:
                raise ValueError('Duplicate YAML key: ' + str(key))
            result[key] = loader.construct_object(v, deep=deep)
        return result
    UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique)
    value = yaml.load(raw, Loader=UniqueLoader)
    if not isinstance(value, dict) or value.get('source') not in ('v4l2', 'synthetic'):
        raise ValueError('Expected an ArduStereo v4l2 or synthetic YAML profile')
    for key in ('width', 'height', 'split_column'):
        if type(value.get(key)) is not int or not 0 < value[key] <= 16384:
            raise ValueError('Invalid image geometry')
    if value['split_column'] >= value['width']:
        raise ValueError('Invalid stereo split')
    return value


def configuration_with_controls(base, requested):
    requested = controls(requested)
    out = copy.deepcopy(base)
    if out['source'] == 'synthetic':
        for key in CONTROL_KEYS:
            out.pop(key, None)
        out['synthetic_fps'] = float(requested['frame_rate'])
    else:
        out.update(requested)
    out['diagnostic_expected_fps'] = float(requested['frame_rate'])
    return out


def save_configuration(root, config):
    root = Path(root); root.mkdir(parents=True, exist_ok=True)
    target = root / ('settings_' + uuid.uuid4().hex + '.yaml')
    temporary = target.with_suffix('.pending')
    try:
        with temporary.open('x', encoding='utf-8', newline='\n') as stream:
            yaml.safe_dump(config, stream, sort_keys=False)
            stream.flush(); os.fsync(stream.fileno())
        os.replace(str(temporary), str(target))
    finally:
        if temporary.exists():
            temporary.unlink()
    return str(target)


def mono_png(width, height, step, data, maximum_width=640):
    if type(width) is not int or type(height) is not int or width < 1 or height < 1 or width * height > 16777216 or step < width or len(data) != step * height:
        raise ValueError('Invalid mono8 preview buffer')
    scale = max(1, math.ceil(width / maximum_width))
    rows = [bytes(data[y * step:y * step + width:scale]) for y in range(0, height, scale)]
    pixels = b''.join(b'\0' + row for row in rows)
    def chunk(kind, content):
        return struct.pack('!I', len(content)) + kind + content + struct.pack('!I', zlib.crc32(kind + content) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', len(rows[0]), len(rows), 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels, 1)) + chunk(b'IEND', b'')


class PreviewModel:
    def __init__(self, config, clock=time.monotonic):
        self.config = config; self.clock = clock; self.lock = threading.Lock()
        self.clear()

    def clear(self):
        with self.lock:
            self.pending = OrderedDict(); self.pair = None; self.cache = None
            self.diagnostic = None; self.diagnostic_time = None

    def image(self, side, stamp, width, height, step, encoding, data):
        expected = self.config['split_column'] if side == 'left' else self.config['width'] - self.config['split_column']
        if side not in ('left', 'right') or type(stamp) is not int or stamp <= 0 or width != expected or height != self.config['height'] or encoding != 'mono8' or step < width or len(data) != step * height:
            raise ValueError('Preview image does not match configured mono8 geometry')
        with self.lock:
            entry = self.pending.setdefault(stamp, {})
            entry[side] = (width, height, step, bytes(data))
            while len(self.pending) > 8:
                self.pending.popitem(last=False)
            if len(entry) == 2 and (self.pair is None or stamp > self.pair[0]):
                self.pair = (stamp, self.clock(), entry)

    def diagnostics(self, fields, level, message):
        with self.lock:
            self.diagnostic = dict(fields, level=level, message=message)
            self.diagnostic_time = self.clock()

    def status(self):
        with self.lock:
            age = None if self.diagnostic_time is None else self.clock() - self.diagnostic_time
            return {'diagnostic': copy.deepcopy(self.diagnostic), 'diagnostic_age': age,
                    'diagnostic_fresh': age is not None and age < 3,
                    'preview_fresh': self.pair is not None and self.clock() - self.pair[1] < 2}

    def preview(self):
        # Cache generation is capped at 5Hz even if a client polls faster.
        with self.lock:
            now = self.clock()
            if self.pair is None or now - self.pair[1] >= 2:
                return {'fresh': False}
            if self.cache and now - self.cache[0] < .2:
                return self.cache[1]
            stamp, _, pair = self.pair
            value = {'fresh': True, 'stamp_ns': str(stamp)}
            for side in ('left', 'right'):
                value[side] = 'data:image/png;base64,' + base64.b64encode(mono_png(*pair[side])).decode('ascii')
            self.cache = (now, value)
            return value


class OwnedCapture:
    """Only signals a new process group created by this object; never kills by name."""
    def __init__(self, command, log_root, grace=5):
        self.command = command; self.root = Path(log_root); self.grace = grace
        self.process = None; self.handles = []; self.last_cleanup = None

    def running(self):
        return self.process is not None and self.process.poll() is None

    def start(self, config):
        if self.running():
            raise RuntimeError('Acquisition already started')
        if self.process is not None:
            self.stop() # Also clean descendants left by a failed launch leader.
        folder = self.root / ('launch_' + uuid.uuid4().hex); folder.mkdir(parents=True)
        self.handles = [(folder / name).open('xb') for name in ('stdout.txt', 'stderr.txt')]
        self.owner_marker='ARDUSTEREO_VIEWER_LAUNCH_TOKEN='+uuid.uuid4().hex
        env=dict(os.environ); key,value=self.owner_marker.split('=',1); env[key]=value
        try:
            options = {'start_new_session': True} if os.name == 'posix' else {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
            self.process = subprocess.Popen(self.command(str(config)), stdout=self.handles[0], stderr=self.handles[1], env=env, **options)
        except BaseException:
            for stream in self.handles: stream.close()
            self.handles = []; raise
        (folder / 'launch.json').write_text(json.dumps({'pid':self.process.pid, 'argv':self.command(str(config))}) + '\n')
        self.folder = folder

    def stop(self):
        if self.process is None:
            return {'stopped': True, 'forced': False, 'exit_code': None}
        p = self.process; began = time.monotonic(); forced = False
        def owned_pids():
            if not sys_platform_linux(): return [p.pid] if p.poll() is None else []
            found=[]
            for stat in Path('/proc').glob('[0-9]*/stat'):
                try:
                    text=stat.read_text(); tail=text[text.rfind(')')+2:].split()
                    if tail[0] not in ('Z','X') and self.owner_marker.encode() in (stat.parent/'environ').read_bytes().split(b'\0'):
                        found.append(int(stat.parent.name))
                except (FileNotFoundError,ProcessLookupError,PermissionError): pass
            return found
        def alive():
            p.poll()
            if os.name != 'posix': return p.poll() is None
            if sys_platform_linux():
                return bool(owned_pids())
            try: os.killpg(p.pid, 0); return True
            except ProcessLookupError: return False
        def send(sig):
            if sys_platform_linux():
                targets=owned_pids()
                # Let roslaunch forward the first SIGINT. Its child nodes may
                # have separate process groups, so escalation audits the marker.
                if sig==signal.SIGINT and p.pid in targets: targets=[p.pid]
                for pid in targets:
                    try: os.kill(pid,sig)
                    except ProcessLookupError: pass
            elif os.name == 'posix':
                try: os.killpg(p.pid, sig)
                except ProcessLookupError: pass
            elif p.poll() is None:
                p.terminate()
        for sig, timeout in [(signal.SIGINT, self.grace), (signal.SIGTERM, 2), (signal.SIGKILL if os.name == 'posix' else signal.SIGTERM, 2)]:
            if not alive(): break
            if sig != signal.SIGINT: forced = True
            send(sig); deadline = time.monotonic() + timeout
            while alive() and time.monotonic() < deadline:
                time.sleep(.02)
        if p.poll() is None: p.wait(timeout=1)
        result = {'stopped': not alive(), 'forced': forced, 'exit_code':p.returncode, 'seconds':time.monotonic()-began,
                  'live_owned_pids':owned_pids()}
        for stream in self.handles: stream.close()
        self.handles = []; self.last_cleanup = result
        (self.folder / 'cleanup.json').write_text(json.dumps(result) + '\n')
        if not result['stopped']: raise RuntimeError('Owned acquisition process group still exists')
        self.process = None
        return result


def sys_platform_linux():
    return os.name == 'posix' and Path('/proc/self/stat').is_file()


class BoundedCall:
    """One in-flight service call, with bounded client wait; timeout is not cancellation."""
    def __init__(self, timeout=4):
        self.timeout=timeout; self.busy=threading.Lock()
    def run(self, callback):
        if not self.busy.acquire(False): raise RuntimeError('Previous ROS request still pending; do not retry until readback or stop')
        done=threading.Event(); result=[]
        def worker():
            try: result.append((True,callback()))
            except Exception as error: result.append((False,error))
            finally: self.busy.release(); done.set()
        threading.Thread(target=worker,daemon=True).start()
        if not done.wait(self.timeout):
            raise RuntimeError('ROS request timed out; write outcome may be unknown. Read back or stop acquisition; no automatic retry.')
        ok,value=result[0]
        if not ok: raise RuntimeError(str(value))
        return value


class Controller:
    def __init__(self, config, root, backend, mode):
        self.base = config; self.root = Path(root); self.backend = backend; self.mode = mode
        self.root.mkdir(parents=True, exist_ok=True)
        self.requested = dict(zip(CONTROL_KEYS, (int(config.get('frame_rate',config.get('synthetic_fps',20))), config.get('exposure',681), config.get('analogue_gain',100))))
        self.readback = None; self.operation = threading.Lock(); self.last_error = None
        self.closing = False
        self.recorder = getattr(backend, 'recorder', None)

    def state(self):
        running=self.backend.running()
        launch=getattr(self.backend,'launch',None)
        exited=launch is not None and launch.process is not None and launch.process.poll() is not None
        return dict(self.backend.model.status(), running=running, mode=self.mode,
                    recording=self.recorder.status() if self.recorder else {'available': False},
                    acquisition_exited=exited, exit_code=launch.process.returncode if exited else None,
                    requested=self.requested.copy(), readback=copy.deepcopy(self.readback) if running else None, busy=self.operation.locked(),
                    last_error=self.last_error, log_root=str(self.root), version='0.4.1')

    def perform(self, action, values=None):
        if self.closing: raise RuntimeError('Viewer is closing')
        if not self.operation.acquire(False): raise RuntimeError('Another operation is busy')
        try:
            if self.closing: raise RuntimeError('Viewer is closing')
            result = self._perform(action, values)
            self.last_error = None
            self._record(action, values, result)
            return result
        except Exception as error:
            self.last_error = str(error); self._record(action, values, {'success':False,'message':str(error)})
            raise
        finally: self.operation.release()

    def close(self):
        # A bounded ROS call may still be in progress when the terminal receives
        # SIGINT. Finish that operation before stopping; reject any new starts.
        self.closing = True
        with self.operation:
            if self.recorder:
                self.recorder.stop(wait=True)
            result=self.backend.stop(); self.backend.model.clear(); self.readback=None
            if self.recorder:
                result['recording'] = self.recorder.status()
            self._record('viewer_exit',None,result)
            return result

    def _record(self, action, values, result):
        with (self.root / 'actions.jsonl').open('a', encoding='utf-8') as stream:
            stream.write(json.dumps({'monotonic':time.monotonic(),'action':action,'requested':values,'result':result}, ensure_ascii=False) + '\n')
            stream.flush()

    def _perform(self, action, values):
        if action == 'destination':
            return self.recorder.select_directory(values)
        if action in ('record', 'finish'):
            if not self.recorder:
                raise RuntimeError('Recording requires a ROS session')
            if action == 'record':
                status = self.backend.model.status()
                if not self.backend.running() or not status['preview_fresh']:
                    raise RuntimeError('Wait for a live stereo preview before recording')
                self.recorder.start()
            else:
                self.recorder.stop()
            return {'success': True, 'recording': self.recorder.status()}
        if action == 'start':
            if self.backend.running(): raise RuntimeError('Acquisition already running')
            requested = controls(values); config = configuration_with_controls(self.base, requested)
            path = save_configuration(self.root / 'configurations', config)
            self.backend.start(path); self.requested = requested; self.readback = None
            return {'success':True,'message':'Starting acquisition','config':path}
        if action == 'stop':
            if self.recorder:
                self.recorder.stop(wait=True)
            result = self.backend.stop(); self.readback = None; self.backend.model.clear()
            message='Acquisition stopped using forced cleanup; inspect launch logs' if result.get('forced') else 'Acquisition stopped'
            return dict(result, success=result.get('stopped', False), message=message)
        if action == 'save':
            requested = controls(values)
            if self.readback:
                for i,key in enumerate(CONTROL_KEYS):
                    value = requested[key]; low, high, step = (self.readback[k][i] for k in ('minimum','maximum','step'))
                    if not low <= value <= high or (value-low) % step: raise ValueError('Control outside queried range: '+key)
            path = save_configuration(self.root / 'configurations', configuration_with_controls(self.base, requested))
            return {'success':True,'path':path,'message':'Requested settings saved as a new YAML file'}
        if action not in ('apply','refresh','snapshot'): raise ValueError('Unknown action')
        if not self.backend.running(): raise RuntimeError('Acquisition is stopped')
        if action == 'snapshot': return self.backend.snapshot()
        if action == 'apply':
            requested = controls(values)
            self.readback = None # An unsuccessful/timeout write must not leave a confirmed old label.
            result = self.backend.set_controls(requested)
        else: result = self.backend.get_controls()
        if not result.get('success'): raise RuntimeError(result.get('message','Control request failed'))
        self.readback = result
        if action == 'apply': self.requested = requested
        return result


class DemoBackend:
    """UI-only demo. No ROS imports, device access or real-camera claims."""
    def __init__(self, model, root):
        self.model = model; self.root = Path(root); self.quit = threading.Event(); self.thread = None
        self.values = dict(zip(CONTROL_KEYS,(20,681,100))); self.events = deque()

    def running(self): return self.thread is not None and self.thread.is_alive()
    def start(self, config):
        self.model.clear(); self.events.clear(); self.quit.clear()
        self.values['frame_rate'] = int(load_configuration(config).get('synthetic_fps',20))
        self.thread = threading.Thread(target=self._run, daemon=True); self.thread.start()
    def _run(self):
        count=0; began=time.monotonic()
        while not self.quit.is_set():
            count+=1; now=time.monotonic(); self.events.append(now)
            while self.events and self.events[0] <= now-5: self.events.popleft()
            width=self.model.config['width']; height=self.model.config['height']; split=self.model.config['split_column']
            row=bytes((x+count)%256 for x in range(width)); data=row*height; stamp=time.time_ns()
            for side,offset,size in [('left',0,split),('right',split,width-split)]:
                pixels=b''.join(data[y*width+offset:y*width+offset+size] for y in range(height))
                self.model.image(side,stamp,size,height,size,'mono8',pixels)
            span=min(5,now-began)
            self.model.diagnostics({'state':'running','capture_fps':len(self.events)/span if span>.1 else 0,
                'publication_fps':len(self.events)/span if span>.1 else 0,'expected_fps_reference':self.values['frame_rate'],
                'capture_window_seconds':span},0,'UI DEMO — no ROS or camera')
            self.quit.wait(1/self.values['frame_rate'])
    def stop(self):
        self.quit.set()
        if self.thread: self.thread.join(2)
        return {'stopped':not self.running(),'forced':False,'exit_code':None}
    def get_controls(self): return dict(self.values,success=True,simulated=True,minimum=[1,0,0],maximum=[120,10000,255],step=[1,1,1],message='UI DEMO: artificial limits; no hardware effect')
    def set_controls(self, values):
        values=controls(values)
        if not 1<=values['frame_rate']<=120 or values['exposure']>10000 or values['analogue_gain']>255: raise ValueError('Outside demo limits')
        self.values=values.copy(); return self.get_controls()
    def snapshot(self):
        # UI demo deliberately never reports a real paired capture as saved.
        raise RuntimeError('UI-only demo has no ROS CameraInfo; use ROS synthetic tests for paired snapshots')
