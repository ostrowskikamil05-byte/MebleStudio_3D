"""Resource cleanup and Canvas binding lifecycle without Tk or OpenGL."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock,patch
import numpy as np
from core import Project,Camera
from gpu_viewport import GPUViewport,GPUUnavailable,paint_gpu,close_gpu


class GPULifecycleTests(unittest.TestCase):
    def test_close_of_partially_initialized_viewport_releases_resources_once(self):
        view=GPUViewport.__new__(GPUViewport)
        view.renderer=Mock();view.window=Mock();view.context=object()
        view.close();view.close()
        view.renderer.RemoveAllViewProps.assert_called_once_with()
        view.renderer.RemoveAllLights.assert_called_once_with()
        view.renderer.SetPass.assert_called_once_with(None)
        view.window.Finalize.assert_called_once_with()
        self.assertIsNone(view.context);self.assertEqual(view.groups,[])
        self.assertIsNone(view.depth_buffer);self.assertIsNone(view.id_buffer)

    def test_failed_initial_context_is_finalized_before_error_is_propagated(self):
        window=Mock();renderer=Mock()
        window.Render.side_effect=GPUUnavailable('initial context failure')
        api={'vtkRenderWindow':lambda:window,'vtkRenderer':lambda:renderer}
        with patch('gpu_viewport._vtk',return_value=api):
            with self.assertRaisesRegex(GPUUnavailable,'initial context failure'):GPUViewport()
        window.Finalize.assert_called_once_with()
        renderer.RemoveAllViewProps.assert_called_once_with()
        renderer.RemoveAllLights.assert_called_once_with()

    def test_repeated_readback_failures_use_one_binding_and_close_current_viewport(self):
        bindings=[];instances=[]
        cpu_depth=np.ones((1,1));cpu_ids=np.zeros((1,1),dtype=np.int32)
        canvas=SimpleNamespace(_depth_buffer=cpu_depth,_id_buffer=cpu_ids,_depth_ids=('cpu',),
            bind=lambda sequence,callback,add:bindings.append((sequence,callback,add)),create_image=Mock())
        class StubViewport:
            def __init__(self):
                self.closed=False;self.close_count=0;self.ordinal=len(instances);instances.append(self)
                self.depth_buffer=np.ones((1,1));self.id_buffer=np.zeros((1,1),dtype=np.int32)
                self.ids=('part',);self.sample_scale=1;self.diagnostics={};self.output_roi=(0,0,0,0);self.drawn=[]
            def frame(self,*args,**kwargs):
                if self.ordinal<3:raise GPUUnavailable('readback failure')
                return np.full((1,1,4),255,dtype=np.uint8)
            def close(self):
                if not self.closed:self.close_count+=1;self.closed=True
        with patch('gpu_viewport.GPUViewport',StubViewport),patch('gpu_viewport.ImageTk.PhotoImage',return_value=object()):
            for _ in range(3):
                self.assertFalse(paint_gpu(canvas,project=Project(),camera=Camera(),size=(1,1))['ok'])
                self.assertIsNone(canvas._gpu_viewport)
                self.assertIs(canvas._depth_buffer,cpu_depth);self.assertIs(canvas._id_buffer,cpu_ids)
            self.assertTrue(paint_gpu(canvas,project=Project(),camera=Camera(),size=(1,1))['ok'])
            self.assertEqual(len(bindings),1)
            self.assertEqual(bindings[0][0],'<Destroy>');self.assertEqual(bindings[0][2],'+')
            # A child destroy cannot close the live renderer. The Canvas
            # callback must find the latest viewport rather than the first.
            bindings[0][1](SimpleNamespace(widget=object()))
            self.assertFalse(instances[-1].closed)
            bindings[0][1](SimpleNamespace(widget=canvas))
            self.assertIsNone(canvas._gpu_viewport);self.assertTrue(instances[-1].closed)
            close_gpu(canvas)
            self.assertEqual([v.close_count for v in instances],[1,1,1,1])


if __name__=='__main__':unittest.main()
