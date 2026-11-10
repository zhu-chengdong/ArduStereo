#!/usr/bin/env python3
"""Hardware-free viewer contracts: image pairing, HTTP, YAML and process ownership."""
import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from viewer_core import (BoundedCall,Controller,DemoBackend,OwnedCapture,PreviewModel,configuration_with_controls,
                         controls,load_configuration,mono_png,save_configuration)
from viewer import create_server


class ViewerContracts(unittest.TestCase):
    def setUp(self):
        self.fixtures=ROOT/'build/viewer-fixtures'; self.fixtures.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=str(self.fixtures)); self.root=Path(self.temp.name)
        self.config=load_configuration(ROOT/'config/synthetic.yaml')
    def tearDown(self):
        self.root.resolve().relative_to(self.fixtures.resolve())
        self.temp.cleanup()

    def test_control_input_rejects_aliases_bool_and_float(self):
        for v in [{'fps':20,'exposure':1,'analogue_gain':1}, {'frame_rate':True,'exposure':1,'analogue_gain':1},
                  {'frame_rate':20.5,'exposure':1,'analogue_gain':1}, {'frame_rate':0,'exposure':1,'analogue_gain':1}]:
            with self.assertRaises(ValueError): controls(v)

    def test_yaml_preserves_profile_and_never_overwrites(self):
        base=load_configuration(ROOT/'config/b0266.yaml'); original=base.copy()
        new=configuration_with_controls(base,dict(zip(('frame_rate','exposure','analogue_gain'),(10,700,101))))
        one=save_configuration(self.root,new); two=save_configuration(self.root,new)
        self.assertNotEqual(one,two); self.assertEqual(load_configuration(one),new); self.assertEqual(base,original)
        self.assertEqual(new['diagnostic_expected_fps'],10)

    def test_synthetic_yaml_never_claims_hardware_controls(self):
        cfg=configuration_with_controls(self.config,{'frame_rate':10,'exposure':700,'analogue_gain':101})
        self.assertEqual(cfg['synthetic_fps'],10)
        self.assertFalse({'frame_rate','exposure','analogue_gain'} & cfg.keys())

    def test_duplicate_yaml_is_rejected(self):
        p=self.root/'bad.yaml'; p.write_text('source: synthetic\nsource: v4l2\n')
        with self.assertRaises(ValueError): load_configuration(p)

    def test_preview_exact_pair_bound_and_staleness(self):
        now=[0.]; cfg=dict(self.config,width=8,height=2,split_column=4); model=PreviewModel(cfg,lambda:now[0])
        model.image('left',1,4,2,4,'mono8',b'a'*8); model.image('right',2,4,2,4,'mono8',b'b'*8)
        self.assertFalse(model.preview()['fresh'])
        model.image('right',1,4,2,4,'mono8',b'b'*8); self.assertEqual(model.preview()['stamp_ns'],'1')
        for stamp in range(3,30): model.image('left',stamp,4,2,4,'mono8',b'a'*8)
        self.assertEqual(len(model.pending),8)
        now[0]=2.1; self.assertFalse(model.preview()['fresh'])
        with self.assertRaises(ValueError): model.image('left',30,4,2,4,'rgb8',b'a'*8)

    def test_png_strips_padding_and_checks_geometry(self):
        png=mono_png(2,2,3,b'abXcdX'); self.assertEqual(png[:8],b'\x89PNG\r\n\x1a\n')
        pos=8; pixels=b''
        while pos<len(png):
            length=struct.unpack('!I',png[pos:pos+4])[0]; kind=png[pos+4:pos+8]; data=png[pos+8:pos+8+length]
            if kind==b'IDAT': pixels+=data
            pos+=length+12
        self.assertEqual(zlib.decompress(pixels),b'\0ab\0cd')
        with self.assertRaises(ValueError): mono_png(2,2,1,b'xxxx')

    def test_stale_diagnostics_are_not_presented_as_live(self):
        now=[0.]; model=PreviewModel(self.config,lambda:now[0]); model.diagnostics({'capture_fps':20},0,'test')
        self.assertTrue(model.status()['diagnostic_fresh']); now[0]=3.1
        self.assertFalse(model.status()['diagnostic_fresh']); model.clear(); self.assertIsNone(model.status()['diagnostic'])

    def test_timeout_is_unknown_and_retries_are_bounded(self):
        gate=threading.Event(); call=BoundedCall(.02); attempts=[]
        def slow(): attempts.append(1); gate.wait(1); return 'late'
        try:
            with self.assertRaisesRegex(RuntimeError,'outcome may be unknown'): call.run(slow)
            with self.assertRaisesRegex(RuntimeError,'still pending'): call.run(slow)
            self.assertEqual(len(attempts),1)
        finally: gate.set()
        deadline=time.monotonic()+2
        while call.busy.locked() and time.monotonic()<deadline: time.sleep(.01)
        self.assertEqual(call.run(lambda:'readback'),'readback')

    def test_disk_failure_never_returns_saved_yaml(self):
        from unittest.mock import patch
        with patch('viewer_core.os.replace',side_effect=OSError('simulated full disk')):
            with self.assertRaises(OSError): save_configuration(self.root,self.config)
        self.assertFalse(list(self.root.glob('*.yaml'))); self.assertFalse(list(self.root.glob('*.pending')))

    def test_failed_apply_invalidates_confirmed_readback(self):
        backend=DemoBackend(PreviewModel(self.config),self.root); controller=Controller(self.config,self.root,backend,'TEST')
        vals={'frame_rate':20,'exposure':681,'analogue_gain':100}
        try:
            controller.perform('start',vals); controller.perform('refresh'); self.assertIsNotNone(controller.readback)
            with self.assertRaises(ValueError): controller.perform('apply',dict(vals,analogue_gain=999))
            self.assertIsNone(controller.readback); self.assertEqual(controller.requested,vals)
            logs=[json.loads(line) for line in (self.root/'actions.jsonl').read_text().splitlines()]
            self.assertFalse(logs[-1]['result']['success'])
        finally: controller.perform('stop')

    def test_viewer_exit_waits_for_active_operation_and_rejects_new_starts(self):
        backend=DemoBackend(PreviewModel(self.config),self.root); c=Controller(self.config,self.root,backend,'TEST')
        vals={'frame_rate':20,'exposure':681,'analogue_gain':100};c.perform('start',vals)
        entered=threading.Event();release=threading.Event();original=backend.set_controls
        def slow(values): entered.set();release.wait(2);return original(values)
        backend.set_controls=slow
        worker=threading.Thread(target=lambda:c.perform('apply',vals));worker.start();self.assertTrue(entered.wait(1))
        closing=threading.Thread(target=c.close);closing.start()
        try:
            deadline=time.monotonic()+1
            while not c.closing and time.monotonic()<deadline:time.sleep(.01)
            with self.assertRaisesRegex(RuntimeError,'closing'):c.perform('start',vals)
        finally:release.set();worker.join(2);closing.join(2)
        self.assertFalse(worker.is_alive());self.assertFalse(closing.is_alive());self.assertFalse(backend.running())

    def test_http_requires_token_and_only_uses_fixed_routes(self):
        backend=DemoBackend(PreviewModel(self.config),self.root); c=Controller(self.config,self.root,backend,'UI DEMO')
        server=create_server(c,ROOT/'web',0); thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        base='http://127.0.0.1:'+str(server.server_port)
        try:
            page=urllib.request.urlopen(base).read().decode(); self.assertIn('ArduStereo',page)
            for route in ['/../package.xml','/api/unknown']:
                with self.assertRaises(urllib.error.HTTPError): urllib.request.urlopen(base+route)
            req=urllib.request.Request(base+'/api/start',data=b'{}',headers={'Content-Type':'application/json'},method='POST')
            with self.assertRaises(urllib.error.HTTPError) as error: urllib.request.urlopen(req)
            self.assertEqual(error.exception.code,403); self.assertFalse(backend.running())
            token=page.split("const token='")[1].split("'")[0]
            req=urllib.request.Request(base+'/api/save',data=json.dumps({'controls':{'frame_rate':20,'exposure':681,'analogue_gain':100}}).encode(),headers={'Content-Type':'application/json','X-ArduStereo-Token':token},method='POST')
            self.assertTrue(json.loads(urllib.request.urlopen(req).read())['success'])
        finally: server.shutdown(); server.server_close(); thread.join(2); backend.stop()

    @unittest.skipUnless(os.name=='posix','Process-group semantics require POSIX; tested in Linux suite')
    def test_owned_process_exit_and_failed_start_are_logged(self):
        owner=OwnedCapture(lambda _: [sys.executable,'-c','import time; time.sleep(30)'],self.root,grace=2)
        owner.start('unused'); self.assertTrue(owner.running()); result=owner.stop()
        self.assertTrue(result['stopped']); self.assertFalse(result['forced']); self.assertFalse(owner.running())
        self.assertTrue(list(self.root.glob('*/cleanup.json')))
        owner=OwnedCapture(lambda _:['/no-such-ardustereo-executable'],self.root)
        with self.assertRaises(FileNotFoundError): owner.start('unused')
        self.assertFalse(owner.handles)

    @unittest.skipUnless(os.name=='posix','Process-group semantics require POSIX; tested in Linux suite')
    def test_unresponsive_owned_process_gets_bounded_cleanup(self):
        owner=OwnedCapture(lambda _: [sys.executable,'-c','import signal,time; signal.signal(signal.SIGINT,signal.SIG_IGN); print("ready",flush=True); time.sleep(30)'],self.root,grace=.15)
        owner.start('unused'); deadline=time.monotonic()+2
        while time.monotonic()<deadline and not (owner.folder/'stdout.txt').read_bytes(): time.sleep(.02)
        began=time.monotonic(); result=owner.stop()
        self.assertTrue(result['stopped']); self.assertTrue(result['forced']); self.assertLess(time.monotonic()-began,3)

    @unittest.skipUnless(os.name=='posix','Owned process groups require POSIX')
    def test_stop_does_not_touch_an_unrelated_process(self):
        foreign=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],start_new_session=True)
        owner=OwnedCapture(lambda _: [sys.executable,'-c','import time;time.sleep(30)'],self.root,grace=1)
        try:
            owner.start('unused'); owner.stop(); self.assertIsNone(foreign.poll())
        finally: foreign.terminate(); foreign.wait(2)

    @unittest.skipUnless(os.name=='posix','Owned descendant cleanup requires POSIX')
    def test_dead_launch_leader_does_not_leave_live_owned_descendant(self):
        ready=self.root/'child-ready.txt'
        child='import os,signal,time;from pathlib import Path;signal.signal(signal.SIGINT,signal.SIG_IGN);Path(__import__("sys").argv[1]).write_text(str(os.getpid()));time.sleep(30)'
        leader='import subprocess,sys;subprocess.Popen([sys.executable,"-c",sys.argv[1],sys.argv[2]],start_new_session=True)'
        owner=OwnedCapture(lambda _:[sys.executable,'-c',leader,child,str(ready)],self.root,grace=.15)
        owner.start('unused'); deadline=time.monotonic()+3
        while time.monotonic()<deadline and (not ready.exists() or owner.running()): time.sleep(.02)
        self.assertTrue(ready.exists()); self.assertFalse(owner.running())
        result=owner.stop(); self.assertTrue(result['stopped']); self.assertTrue(result['forced'])
        stat=Path('/proc')/ready.read_text()/'stat'
        if stat.exists():
            text=stat.read_text(); self.assertIn(text[text.rfind(')')+2:].split()[0],('Z','X'))

    def test_exited_capture_invalidates_readback_and_reports_exit(self):
        class Dead:
            returncode=7
            def poll(self): return self.returncode
        backend=DemoBackend(PreviewModel(self.config),self.root)
        backend.launch=type('Launch',(),{'process':Dead()})()
        controller=Controller(self.config,self.root,backend,'TEST');controller.readback={'frame_rate':20}
        state=controller.state();self.assertTrue(state['acquisition_exited']);self.assertEqual(state['exit_code'],7)
        self.assertIsNone(state['readback'])


if __name__=='__main__': unittest.main(verbosity=2)
