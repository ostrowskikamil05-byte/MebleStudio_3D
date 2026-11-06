import copy
import ctypes as C
import math
import struct
import unittest
from types import SimpleNamespace as NS
from unittest.mock import patch

from core import Project,Part,Camera,explosion_shift,world
from native_scene_export import build_snapshot,HEADER,OWNER,VERTEX,GHOST,SELECTED
from native_viewport import NativeCamera,NativeHit,NativeOverlay,NativeViewport,paint_native,close_native,flush_native,overlay_commands,NativeUnavailable
from native_viewport import reset_native_failure
from depth_raster import pick_visible,screen_visibility
from attachment_picker import pick as attach_pick
from studio_interaction import pick as editor_pick

def decode(data):
    head=HEADER.unpack_from(data,8);offset=8+HEADER.size;owners=[]
    for _ in range(head[2]):
        fields=OWNER.unpack_from(data,offset);offset+=OWNER.size
        identity=data[offset:offset+fields[2]].decode('utf-8');offset+=fields[2]
        label=data[offset:offset+fields[3]].decode('utf-8');offset+=fields[3]
        owners.append(dict(kind=fields[0],flags=fields[1],id=identity,label=label,lo=fields[4:7],hi=fields[7:10]))
    return owners,[VERTEX.unpack_from(data,offset+i*VERTEX.size) for i in range(head[1])]

class Canvas:
    def __init__(self):self.jobs={};self.cancelled=[];self.bindings={};self.items={}
    def winfo_id(self):return 100
    def winfo_width(self):return 800
    def winfo_height(self):return 600
    def winfo_ismapped(self):return True
    def bind(self,name,fn,add=None):self.bindings[name]=fn
    def after_idle(self,fn):job=str(len(self.jobs)+1);self.jobs[job]=fn;return job
    def after_cancel(self,job):self.cancelled.append(job);self.jobs.pop(job,None)
    def find_all(self):return tuple(self.items)
    def type(self,item):return self.items[item]['type']
    def gettags(self,item):return self.items[item].get('tags',())
    def coords(self,item):return self.items[item]['coords']
    def itemcget(self,item,name):return self.items[item].get(name,'')
    def winfo_rgb(self,color):return tuple(int(color[i:i+2],16)*257 for i in (1,3,5))

class FakeAPI:
    def __init__(self):self.uploads=0;self.frames=0;self.closed=0;self.overlay_counts=[]
    def ms3d_upload(self,handle,data,size,error,capacity):self.uploads+=1;return 1
    def ms3d_frame(self,handle,camera,error,capacity):self.frames+=1;self.camera=C.cast(camera,C.POINTER(NativeCamera))[0];return 1
    def ms3d_resize(self,*args):return 1
    def ms3d_info(self,handle,text,capacity):text.value='{"engine":"DirectX11","software":false}';return 1
    def ms3d_overlay(self,handle,items,count,error,capacity):self.overlay_counts.append(count);return 1
    def ms3d_show(self,*args):return 1
    def ms3d_close(self,*args):self.closed+=1
    def ms3d_pick(self,handle,x,y,mode,hit):
        hit=C.cast(hit,C.POINTER(NativeHit))[0];hit.owner=0;hit.kind=0;hit.flags=0
        hit.world[:]=(12,34,18);hit.depth=7;return 1

def viewport(canvas=None):
    v=NativeViewport.__new__(NativeViewport);v.canvas=canvas or Canvas();v.api=FakeAPI();v.handle=1;v.backend='d3d11'
    v.error=C.create_unicode_buffer(4096);v.closed=False;v.cache_key=None;v.size=None;v.sources=();v.failed_request=None;v.warning=None
    return v

class NativeIntegrationTests(unittest.TestCase):
    def project(self):
        p=Project();p.parts=[Part(u=600,v=450,t=18,x=20,role='door',material=p.materials[0].id),Part(u=400,v=300,t=18,z=100,role='shelf',material=p.materials[0].id)]
        return p
    def test_binary_presentation_flags_scope_and_manufacturing_unchanged(self):
        p=self.project();before=copy.deepcopy(p.data());before_costs=p.costs()
        data,report=build_snapshot(p,xray_ids={p.parts[0].id},selected_ids={p.parts[1].id})
        owners,vertices=decode(data)
        self.assertTrue(owners[0]['flags']&GHOST);self.assertFalse(owners[1]['flags']&GHOST)
        self.assertFalse(owners[0]['flags']&SELECTED);self.assertTrue(owners[1]['flags']&SELECTED)
        self.assertEqual(report['owner_sources'],[(part.id,'part') for part in p.parts])
        self.assertTrue(all(bool(v[-1]&GHOST)==(v[-2]==0) for v in vertices))
        self.assertEqual(p.data(),before);self.assertEqual(p.costs(),before_costs)
    def test_exploded_positions_and_owner_bounds_match_existing_camera(self):
        p=self.project();camera=Camera(center=(120,180,350))
        original,_=build_snapshot(p);exploded,_=build_snapshot(p,camera=camera,explode=.22)
        owners,a=decode(original);exploded_owners,b=decode(exploded)
        for old,new in zip(a,b):
            shift=explosion_shift(p.parts[old[-2]],camera,.22)
            for i in range(3):self.assertAlmostEqual(new[i]-old[i],shift[i],places=3)
        for i,owner in enumerate(owners):
            shift=explosion_shift(p.parts[i],camera,.22)
            for axis in range(3):self.assertAlmostEqual(exploded_owners[i]['lo'][axis],owner['lo'][axis]+shift[axis],places=3)
    def test_orbit_reuses_native_uploaded_geometry_and_source_is_immutable(self):
        p=self.project();before=copy.deepcopy(p.data());v=viewport();camera=Camera()
        result=v.frame(p,camera,(800,600),xray=True,xray_ids={p.parts[0].id})
        camera.yaw+=30;camera.pan=(20,-30);v.frame(p,camera,(800,600),xray=True,xray_ids={p.parts[0].id})
        self.assertEqual(v.api.uploads,1);self.assertEqual(v.api.frames,2);self.assertEqual(v.api.camera.xray,2)
        self.assertEqual(result['diagnostics']['engine'],'DirectX11');self.assertEqual(p.data(),before)
    def test_scoped_xray_or_explode_state_invalidates_only_presentation_upload(self):
        p=self.project();v=viewport();camera=Camera()
        v.frame(p,camera,(800,600));v.frame(p,camera,(800,600),xray=True,xray_ids={p.parts[1].id})
        v.frame(p,camera,(800,600),explode=.22)
        self.assertEqual(v.api.uploads,3)
    def test_native_owner_mapping_preserves_editor_and_attachment_identity(self):
        v=viewport();v.sources=[('part-source','part')]
        hit=v.hit(1,2)
        self.assertEqual(hit['id'],'part-source');self.assertEqual(hit['world'],(12,34,18));self.assertTrue(hit['precise_surface'])
        self.assertEqual(hit['depth'],7)
    def test_native_visibility_blocks_hidden_annotations_without_framebuffer(self):
        c=Canvas();c._native_depth=lambda x,y:dict(depth=50,id='near')
        self.assertFalse(screen_visibility(c,25,30,depth=40,tolerance=.8))
        self.assertTrue(screen_visibility(c,25,30,depth=50,tolerance=.8))
        self.assertFalse(screen_visibility(c,25,30,owner='rear'));self.assertFalse(screen_visibility(c,-2,30,depth=80))
    def test_editor_selection_prefers_native_hit_over_coarse_silhouette(self):
        c=Canvas();c._native_pick=lambda x,y:'native-part'
        app=NS(canvas=c,drawn=[dict(id='coarse',points=[(0,0),(100,0),(100,100)])])
        self.assertEqual(pick_visible(c,20,20),'native-part');self.assertEqual(editor_pick(app,20,20),'native-part')
    def test_attachment_precise_ray_restores_exploded_model_space(self):
        p=self.project();part=p.parts[0];c=Canvas();camera=Camera(center=(200,200,500));shift=explosion_shift(part,camera,.22)
        point=world(part,200,120,18)
        c._native_pick=lambda x,y:part.id;c._native_world=lambda x,y:dict(id=part.id,kind='part',world=tuple(point[i]+shift[i] for i in range(3)))
        c._active_renderer='native';app=NS(canvas=c,drawn=[],camera=camera,project=p,display_project=lambda:p,explode=NS(get=lambda:True))
        result=attach_pick(app,20,20)
        self.assertEqual(result['target_id'],part.id);self.assertEqual(result['local_hit']['face'],'top')
        self.assertAlmostEqual(result['local_hit']['u'],200);self.assertAlmostEqual(result['local_hit']['v'],120)
    def test_failed_backend_switch_retains_previous_functional_renderer(self):
        p=self.project();c=Canvas();v=viewport(c);c._native_viewport=v
        with patch('native_viewport.NativeViewport',side_effect=NativeUnavailable('unsupported Vulkan')):
            result=paint_native(c,p,backend='vulkan',camera=Camera(),size=(800,600))
        self.assertTrue(result['ok']);self.assertIn('unsupported Vulkan',result['warning']);self.assertIs(c._native_viewport,v)
        self.assertEqual(v.api.closed,0);self.assertEqual(v.api.frames,1)
        close_native(c);self.assertEqual(v.api.closed,1);self.assertFalse(c.jobs)
    def test_initial_native_failure_cleans_up_hooks_and_returns_cpu_fallback(self):
        c=Canvas()
        with patch('native_viewport.NativeViewport',side_effect=NativeUnavailable('missing DLL')) as factory:
            result=paint_native(c,self.project(),backend='auto',camera=Camera(),size=(800,600))
            for _ in range(3):paint_native(c,self.project(),backend='auto',camera=Camera(),size=(800,600))
            self.assertEqual(factory.call_count,1)
        self.assertFalse(result['ok']);self.assertEqual(result['engine'],'cpu');self.assertIsNone(c._native_pick)
    def test_app_auto_native_failure_keeps_existing_opengl_context_across_orbits(self):
        from app import App
        p=Project();c=Canvas();retained=object();c._gpu_viewport=retained;c.delete=lambda *args:None;c.create_text=lambda *args,**kwargs:None
        var=lambda value:NS(get=lambda:value)
        app=NS(canvas=c,project=p,camera=Camera(),selected=None,selected_scene=None,
            viewport_engine='auto',viewport_quality='balanced',xray=var(False),xray_scope=var('Cały zestaw'),
            explode=var(False),edit_mode=var('camera'),open_amounts={},display_project=lambda:p,
            selected_part=lambda:None,inspector_tabs=NS(index=lambda value:0,select=lambda:0))
        def existing_gpu(*args,**kwargs):
            self.assertIs(c._gpu_viewport,retained)
            return dict(ok=True,engine='vtk-opengl',drawn=[])
        with (patch('native_viewport.paint_native',return_value=dict(ok=False,error='unavailable')),
             patch('gpu_viewport.paint_gpu',side_effect=existing_gpu) as paint,
             patch('gpu_viewport.close_gpu') as close,
             patch('viewport_scene.paint_background',return_value={'spacing':32}),
             patch('viewport_scene.paint_overlay'),patch('viewport_annotations.paint')):
            App.draw(app);app.camera.yaw+=10;App.draw(app)
            self.assertEqual(paint.call_count,2);close.assert_not_called()
    def test_overlay_retains_editor_geometry_and_excludes_preexisting_background(self):
        c=Canvas();c._native_background_items={1}
        c.items={1:dict(type='rectangle',coords=[0,0,800,600],fill='#ffffff'),
                 2:dict(type='line',coords=[10,20,30,40],fill='#ff1478',width='3',dash='2 3',arrow='both'),
                 3:dict(type='oval',coords=[15,15,25,25],fill='',outline='#ffad00',width='3',dash='')}
        commands,storage=overlay_commands(c)
        self.assertEqual(len(commands),2);self.assertEqual(commands[0].flags,7);self.assertEqual(commands[0].count,4)
        self.assertEqual(commands[0].outline,0x7814ff);self.assertEqual(commands[1].fill,0xffffffff)
        self.assertEqual(list(commands[0].coordinates[:4]),[10,20,30,40])
    def test_overlay_commit_and_close_cancel_delayed_jobs_exactly_once(self):
        c=Canvas();v=viewport(c);c._native_viewport=v;c._native_pending=True;c._native_commit_job=c.after_idle(lambda:None)
        flush_native(c);self.assertEqual(v.api.overlay_counts,[0]);self.assertFalse(c.jobs);self.assertFalse(c._native_pending)
        close_native(c);close_native(c);self.assertEqual(v.api.closed,1)
    def test_overlay_failure_caches_backend_until_explicit_preferences_retry(self):
        c=Canvas();v=viewport(c);c._native_viewport=v;c._native_pending=True;c._native_requested_backend='auto'
        with patch.object(v,'call',side_effect=NativeUnavailable('layered window unavailable')):
            with self.assertRaises(NativeUnavailable):flush_native(c)
        self.assertEqual(v.api.closed,1);self.assertEqual(c._native_failed_backend,'auto')
        with patch('native_viewport.NativeViewport',side_effect=NativeUnavailable('no adapter')) as create:
            result=paint_native(c,self.project(),backend='auto',camera=Camera(),size=(800,600));create.assert_not_called()
            self.assertEqual(result['error'],'layered window unavailable')
            reset_native_failure(c)
            paint_native(c,self.project(),backend='auto',camera=Camera(),size=(800,600));self.assertEqual(create.call_count,1)

if __name__=='__main__':unittest.main()
