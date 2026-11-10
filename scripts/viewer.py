#!/usr/bin/env python3
"""Optional loopback browser UI. Camera starts only after an explicit button click."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import sys
import time
from urllib.parse import urlsplit

# catkin's devel relay executes this file without adding its source directory.
sys.path.insert(0,str(Path(__file__).resolve().parent))
from viewer_core import Controller, DemoBackend, PreviewModel, load_configuration


def create_server(controller, web_root, port):
    token = secrets.token_urlsafe(32)
    web_root = Path(web_root)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass
        def local_host(self):
            host = self.headers.get('Host', '')
            try:
                parsed = urlsplit('//' + host)
                return parsed.hostname in ('localhost','127.0.0.1') and parsed.port is not None
            except ValueError:
                return False
        def send(self, status, body, mime='application/json; charset=utf-8'):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False, allow_nan=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', mime); self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control','no-store'); self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Security-Policy',"default-src 'self'; img-src 'self' data:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'")
            self.end_headers()
            try: self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError): pass
        def do_GET(self):
            if not self.local_host(): return self.send(403, {'success':False,'message':'Loopback Host required'})
            path = urlsplit(self.path).path
            if path == '/':
                data = (web_root/'index.html').read_text(encoding='utf-8').replace('__TOKEN__',token)
                return self.send(200,data.encode('utf-8'),'text/html; charset=utf-8')
            if path == '/api/state': return self.send(200,controller.state())
            if path == '/api/preview': return self.send(200,controller.backend.model.preview())
            if path == '/favicon.ico': return self.send(204,b'','image/x-icon')
            return self.send(404,{'success':False,'message':'Not found'})
        def do_POST(self):
            if not self.local_host() or not secrets.compare_digest(self.headers.get('X-ArduStereo-Token',''),token):
                return self.send(403,{'success':False,'message':'Invalid local UI token'})
            origin = self.headers.get('Origin')
            if origin and origin != 'http://' + self.headers.get('Host',''):
                return self.send(403,{'success':False,'message':'Origin differs from UI host'})
            try:
                if self.headers.get_content_type() != 'application/json': raise ValueError('JSON required')
                length = int(self.headers.get('Content-Length','-1'))
                if not 0 <= length <= 16384: raise ValueError('Request body too large or missing')
                self.connection.settimeout(3)
                payload = json.loads(self.rfile.read(length), parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))
                if not isinstance(payload,dict): raise ValueError('Expected object')
                action = urlsplit(self.path).path.removeprefix('/api/') if sys.version_info >= (3,9) else urlsplit(self.path).path[len('/api/'):]
                if not self.path.startswith('/api/'): raise ValueError('Unknown route')
                return self.send(200, controller.perform(action,payload.get('controls')))
            except (ValueError, TypeError) as error:
                return self.send(400,{'success':False,'message':str(error)})
            except Exception as error:
                return self.send(409,{'success':False,'message':str(error)})
    return ThreadingHTTPServer(('127.0.0.1',port),Handler)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',required=True)
    parser.add_argument('--root',default=str(Path.home()/'ardustereo_gui'))
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--camera-ns',default='camera')
    parser.add_argument('--package-dir',default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument('--demo',action='store_true',help='UI-only simulation; no ROS or camera')
    args=parser.parse_args([v for v in sys.argv[1:] if not v.startswith('__')])
    config=load_configuration(args.config)
    if args.demo and config['source'] != 'synthetic': parser.error('--demo requires a synthetic YAML profile')
    model=PreviewModel(config)
    folder=Path(args.root).expanduser()/('viewer_'+time.strftime('%Y%m%dT%H%M%S')+'_'+secrets.token_hex(5))
    if args.demo:
        backend=DemoBackend(model,folder); mode='UI DEMO — no ROS or camera'
    else:
        from viewer_ros import RosBackend
        backend=RosBackend(model,folder,args.camera_ns)
        mode='ROS SYNTHETIC — no camera' if config['source']=='synthetic' else 'CAMERA — hardware UI not yet validated'
    controller=Controller(config,folder,backend,mode)
    server=None
    try:
        server=create_server(controller,Path(args.package_dir)/'web',args.port)
        print('ArduStereo viewer http://127.0.0.1:%d/ — %s'%(server.server_port,mode),flush=True)
        print('Acquisition is stopped. Close the browser to keep capture; use Stop to release it.',flush=True)
        server.serve_forever(poll_interval=.2)
    except KeyboardInterrupt:
        pass
    finally:
        try: controller.close()
        finally:
            if server: server.server_close()
            if not args.demo: backend.close()


if __name__=='__main__': main()
