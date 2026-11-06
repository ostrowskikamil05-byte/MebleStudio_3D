import copy
import unittest
from types import SimpleNamespace
import numpy as np
from unittest.mock import patch
from core import Part,Project,Camera
from gpu_viewport import GPUViewport,GPUUnavailable,pick_world


class GPUViewportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:cls.view=GPUViewport()
        except GPUUnavailable as exc:raise unittest.SkipTest(str(exc))

    @classmethod
    def tearDownClass(cls):cls.view.close()

    def test_real_gpu_depth_owner_and_camera_pan_agree(self):
        project=Project();back=Part(u=600,v=450,t=18,z=0,material=project.materials[0].id)
        front=Part(u=300,v=250,t=18,x=150,y=100,z=200,material=project.materials[0].id)
        project.parts=[back,front];original=copy.deepcopy(project.data())
        camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0),pan=(13,-9))
        rgba=self.view.frame(project,camera,(360,260));canvas=SimpleNamespace(_gpu_viewport=self.view)
        hit=pick_world(canvas,193,121)
        self.assertEqual(hit['id'],front.id)
        self.assertAlmostEqual(hit['world'][2],218,places=2)
        self.assertEqual(tuple(rgba[121,193,3:]),(255,))
        self.assertEqual(project.data(),original)
        # VTK display coordinates start at the lower left; Canvas at top left.
        r=self.view.renderer;r.SetWorldPoint(300,225,218,1);r.WorldToDisplay()
        vtk=r.GetDisplayPoint();core=camera.project((300,225,218),360,260)
        self.assertAlmostEqual(vtk[0],core[0],places=5)
        self.assertAlmostEqual(260-vtk[1],core[1],places=5)

    def test_xray_exposes_interior_owner_and_matching_depth(self):
        project=Project();back=Part(u=600,v=450,t=18,z=0,material=project.materials[0].id)
        front=Part(u=600,v=450,t=18,z=200,material=project.materials[0].id)
        project.parts=[back,front];camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        self.view.frame(project,camera,(360,260),xray=True,xray_ids={front.id})
        hit=pick_world(SimpleNamespace(_gpu_viewport=self.view),180,130)
        self.assertEqual(hit['id'],back.id)
        self.assertAlmostEqual(hit['world'][2],18,places=2)

    def test_xray_whole_set_remains_selectable_without_opaque_parts(self):
        project=Project();part=Part(u=600,v=450,t=18,material=project.materials[0].id)
        project.parts=[part];camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        image=self.view.frame(project,camera,(360,260),xray=True,xray_ids={part.id})
        hit=pick_world(SimpleNamespace(_gpu_viewport=self.view),180,130)
        self.assertEqual(hit['id'],part.id);self.assertEqual(hit['kind'],'part')
        self.assertAlmostEqual(hit['world'][2],18,places=2)
        self.assertGreater(image[130,180,3],0)

    def test_same_owner_fitting_hit_is_not_a_board_surface(self):
        project=Project();part=Part(u=600,v=450,t=18,material=project.materials[0].id)
        project.parts=[part];camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        face=[(250,175,100),(350,175,100),(350,275,100),(250,275,100)]
        with patch('fitting_visuals.fitting_faces',return_value=[(part.id,face,'#808080',1)]):
            self.view.frame(project,camera,(360,260))
        hit=pick_world(SimpleNamespace(_gpu_viewport=self.view),180,130)
        self.assertEqual(hit['id'],part.id);self.assertEqual(hit['kind'],'fitting')
        self.assertFalse(hit['precise_surface'])

    def test_orbit_reuses_retained_actor_and_preview_buffers_match_image_pixels(self):
        project=Project();part=Part(u=600,v=450,t=18,material=project.materials[0].id)
        project.parts=[part];camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        rgba=self.view.frame(project,camera,(363,267),interactive=True)
        actor=self.view.current[0].actor
        owner=self.view.id_buffer>=0
        np.testing.assert_array_equal(rgba[:,:,3]>0,owner)
        for yaw in (-38,-20,0,15,35):
            camera.yaw=yaw;self.view.frame(project,camera,(363,267),interactive=True)
            self.assertIs(self.view.current[0].actor,actor)
            self.assertFalse(self.view.diagnostics['geometry_changed'])
        self.assertEqual(self.view.depth_buffer.shape,(267,363))

    def test_close_is_idempotent_and_disallows_render_on_released_context(self):
        view=GPUViewport()
        project=Project();project.parts=[Part(u=600,v=450,t=18,material=project.materials[0].id)]
        view.frame(project,Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0)),(360,260))
        self.assertGreater(view.renderer.GetViewProps().GetNumberOfItems(),0)
        self.assertIsNotNone(view.depth_buffer)
        view.close();view.close()
        self.assertEqual(view.renderer.GetViewProps().GetNumberOfItems(),0)
        self.assertEqual(view.renderer.GetLights().GetNumberOfItems(),0)
        self.assertEqual(view.groups,[]);self.assertEqual(view.current,[]);self.assertEqual(view.drawn,[])
        for field in ('context','depth_buffer','id_buffer','kind_buffer','rgba_array','z_array','_ssao','_steps'):
            self.assertIsNone(getattr(view,field))
        with self.assertRaises(GPUUnavailable):view.frame(Project(),Camera(),(20,20))

    def test_high_quality_has_valid_normals_lighting_grain_and_surface_ids(self):
        project=Project();part=Part(u=600,v=450,t=18,material=project.materials[0].id)
        project.parts=[part];camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        image=self.view.frame(project,camera,(360,260),quality='high')
        self.assertTrue(self.view._ssao)
        mesh=self.view.current[0]
        normals=self.view.api['vtk_to_numpy'](mesh.data.GetPointData().GetNormals())
        np.testing.assert_allclose(np.linalg.norm(normals,axis=1),1,atol=1e-6)
        self.assertTrue(mesh.actor.GetProperty().GetLighting())
        hit=pick_world(SimpleNamespace(_gpu_viewport=self.view),180,130)
        self.assertEqual(hit['id'],part.id)
        self.assertEqual(hit['kind'],'part')
        self.assertGreater(np.unique(image[100,100:250,0]).size,2)

    def test_opaque_cabinet_colors_do_not_change_when_floor_is_behind_it(self):
        from core import add_template
        from room_architecture import defaults
        project=Project();add_template(project,'cabinet',w=600,h=800,d=500)
        project.scene['architecture']=dict(defaults(),enabled=True,width=1600,depth=1400,height=1500)
        camera=Camera(yaw=-38,pitch=26,scale=.22,center=(300,250,400))
        image=self.view.frame(project,camera,(400,320),quality='balanced')
        ids=self.view.id_buffer.copy();owners=list(self.view.ids)
        self.assertFalse(any(group.visual_only or group.opacity<1 for group in self.view.groups))
        project=Project.from_data(project.data());project.scene['architecture']['enabled']=False
        bare=self.view.frame(project,camera,(400,320),quality='balanced')
        part_ids={part.id for part in project.parts}
        foreground=np.isin(ids,[i for i,owner in enumerate(owners) if owner in part_ids])
        foreground&=np.isin(self.view.id_buffer,[i for i,owner in enumerate(self.view.ids) if owner in part_ids])
        self.assertGreater(int(foreground.sum()),1000)
        np.testing.assert_allclose(image[foreground,:3],bare[foreground,:3],atol=1)

    def test_whole_xray_over_room_floor_prefers_ghost_board_to_background(self):
        from room_architecture import defaults
        project=Project();part=Part(u=600,v=450,t=18,z=150,material=project.materials[0].id)
        project.parts=[part];project.scene['architecture']=dict(defaults(),enabled=True,width=1200,depth=1000,height=1200)
        camera=Camera(yaw=0,pitch=90,scale=.4,center=(300,225,0))
        self.view.frame(project,camera,(360,260),xray=True,xray_ids={part.id})
        hit=pick_world(SimpleNamespace(_gpu_viewport=self.view),180,130)
        self.assertEqual(hit['id'],part.id)
        self.assertAlmostEqual(hit['world'][2],168,places=2)


if __name__=='__main__':unittest.main()
