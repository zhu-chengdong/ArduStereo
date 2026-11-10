"""ROS adapter; serialized calls run with a bounded wait and one daemon worker."""
import math
from pathlib import Path
import re
import threading

import rosgraph
import rospy
from diagnostic_msgs.msg import DiagnosticArray
from sensor_msgs.msg import Image
from std_srvs.srv import Trigger
from ardustereo.srv import GetControls, SetControls
from viewer_core import OwnedCapture, BoundedCall


class RosBackend:
    def __init__(self, model, root, namespace):
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_/]*',namespace) or '//' in namespace or namespace.endswith('/'):
            raise ValueError('Invalid camera namespace')
        self.namespace='/'+namespace; self.model=model; self.calls=BoundedCall()
        rospy.init_node('ardustereo_viewer',disable_signals=True)
        self.launch=OwnedCapture(lambda path:['roslaunch','ardustereo','stereo.launch','camera_ns:='+namespace,'config:='+path],Path(root)/'launches')
        self.subscribers=[rospy.Subscriber(self.namespace+'/'+side+'/image_raw',Image,
            lambda msg,s=side:self._image(s,msg),queue_size=1,buff_size=8*1024*1024) for side in ('left','right')]
        self.subscribers.append(rospy.Subscriber('/diagnostics',DiagnosticArray,self._diagnostics,queue_size=1))
    def _image(self, side, msg):
        try: self.model.image(side,msg.header.stamp.to_nsec(),msg.width,msg.height,msg.step,msg.encoding,msg.data)
        except ValueError as error: rospy.logwarn_throttle(3,'Viewer rejected preview: %s',error)
    def _diagnostics(self, array):
        for status in array.status:
            if status.name != self.namespace+'/ardustereo: acquisition': continue
            values={kv.key:kv.value for kv in status.values}
            for key in ('capture_fps','publication_fps','expected_fps_reference','capture_window_seconds'):
                try:
                    v=float(values.get(key,'')); values[key]=v if math.isfinite(v) else None
                except ValueError: values[key]=None
            self.model.diagnostics(values,status.level,status.message)
    def running(self): return self.launch.running()
    def start(self, config):
        try:
            rosgraph.Master(rospy.get_name()).lookupNode(self.namespace+'/ardustereo')
        except rosgraph.MasterError: pass
        else: raise RuntimeError('A capture node already exists in this namespace; viewer will not stop an external node')
        self.model.clear(); self.launch.start(config)
    def stop(self): return self.launch.stop()
    @staticmethod
    def _result(response):
        return {name:getattr(response,name) for name in response.__slots__}
    def _call(self, name, kind, *args):
        def invoke():
            rospy.wait_for_service(self.namespace+'/'+name,timeout=2)
            response=rospy.ServiceProxy(self.namespace+'/'+name,kind)(*args)
            return self._result(response)
        return self.calls.run(invoke)
    def get_controls(self): return self._call('get_controls',GetControls)
    def set_controls(self, values): return self._call('set_controls',SetControls,values['frame_rate'],values['exposure'],values['analogue_gain'])
    def snapshot(self):
        response=self._call('save_snapshot',Trigger)
        if not response['success']: raise RuntimeError(response['message'])
        return response
    def close(self):
        for sub in self.subscribers: sub.unregister()
        rospy.signal_shutdown('Viewer stopped')
