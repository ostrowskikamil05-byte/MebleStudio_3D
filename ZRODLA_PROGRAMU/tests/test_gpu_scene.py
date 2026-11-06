import copy
import unittest
import numpy as np
from core import Camera,Part,Project
from gpu_scene import camera_spec,camera_basis,unproject,z_to_depth,mesh_groups,ghost_ids,cheap_drawn
from viewport_render import prepare_scene


class GPUSceneTests(unittest.TestCase):
    def test_camera_round_trip_all_axes_with_pan_and_orbit(self):
        for yaw in (-170,-38,0,91):
            for pitch in (-30,0,24,90):
                camera=Camera(yaw=yaw,pitch=pitch,scale=.32,center=(420,260,800),pan=(37,-22))
                right,up,near=camera_basis(camera)
                np.testing.assert_allclose(np.array((right,up,near))@np.array((right,up,near)).T,np.eye(3),atol=1e-12)
                for point in ((0,0,0),(400,700,1200),(-350,640,33)):
                    x,y,depth=camera.project(point,1300,720)
                    np.testing.assert_allclose(unproject(camera,(1300,720),x,y,depth),point,atol=1e-9)

    def test_opengl_depth_converts_to_existing_camera_units(self):
        camera=Camera();spec=camera_spec(camera,(1300,720),[(0,0,0),(2000,2000,1500)])
        near,far=spec['clipping'];expected=np.array([-450,0,750],dtype=np.float32)
        z=(spec['distance']-near-expected)/(far-near)
        np.testing.assert_allclose(z_to_depth(z,spec),expected,atol=.002)
        self.assertTrue(np.isneginf(z_to_depth(np.ones(3,dtype=np.float32),spec)).all())

    def test_retained_group_references_live_geometry_and_preserves_source(self):
        project=Project();project.parts=[Part(plane=plane,u=600,v=450,t=18,material=project.materials[0].id) for plane in ('XY','XZ','YZ')]
        original=copy.deepcopy(project.data());context=prepare_scene(project)
        groups=mesh_groups(context)
        self.assertEqual(len(groups),3)
        self.assertEqual([len(g.offsets)-1 for g in groups],[6]*3)
        self.assertTrue(all(g.records and g.vertices.shape==(8,3) for g in groups))
        # XZ is mirrored; final polygon winding still points outwards.
        for group in groups:
            center=group.vertices.mean(axis=0)
            for i in range(len(group.offsets)-1):
                face=group.vertices[group.connectivity[group.offsets[i]:group.offsets[i+1]]]
                normal=np.cross(face[1]-face[0],face[2]-face[0])
                self.assertGreater(float(normal@(face.mean(axis=0)-center)),0)
        self.assertEqual(project.data(),original)
        drawn=cheap_drawn(context,groups,Camera(),(900,650))
        self.assertEqual(len(drawn),6)
        self.assertTrue(all(len(f['points'])==len(f['world'])==len(f['depths']) for f in drawn))

    def test_explicit_xray_scope_respects_requested_parts(self):
        p=Project();p.parts=[Part(role='door'),Part(role='shelf')]
        self.assertEqual(ghost_ids(p,False,{p.parts[1].id}),set())
        self.assertEqual(ghost_ids(p,True,{p.parts[1].id}),{p.parts[1].id})
        self.assertEqual(ghost_ids(p,True),{p.parts[0].id})


if __name__=='__main__':unittest.main()
