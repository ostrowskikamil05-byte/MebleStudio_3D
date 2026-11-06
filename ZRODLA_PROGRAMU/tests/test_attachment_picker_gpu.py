"""GPU attachment hits use the exact depth surface, not cheap overlay faces."""
import copy
from types import SimpleNamespace
from unittest.mock import patch
import unittest
import numpy as np
from core import Project,Part,Camera,world
from attachment_picker import pick


class AttachmentGPUHits(unittest.TestCase):
    def setUp(self):
        self.project=Project()
        self.part=Part(u=200,v=120,t=18,plane='XZ',x=200,y=-40,z=100,
                       rx=13,ry=7,rz=23,layout=dict(x=20,y=10,z=0,yaw=35),material=self.project.materials[0].id)
        self.project.parts=[self.part]
        self.canvas=SimpleNamespace(_active_renderer='gpu',_id_buffer=np.zeros((600,900),dtype=np.int32),
                                   _depth_ids=[self.part.id],winfo_width=lambda:900,winfo_height=lambda:600)
        self.app=SimpleNamespace(canvas=self.canvas,project=self.project,camera=Camera(),drawn=[],
                                 display_project=lambda:self.project)

    def test_rotated_hit_is_inverted_exactly_without_overlay_triangles(self):
        before=copy.deepcopy(self.project.data());point=world(self.part,75,40,0)
        with patch('gpu_viewport.pick_world',return_value=dict(id=self.part.id,kind='part',world=point,precise_surface=True)):
            hit=pick(self.app,400,300)
        self.assertEqual(hit['target_id'],self.part.id);self.assertEqual(hit['local_hit']['face'],'bottom')
        for axis,value in zip(('u','v','w'),(75,40,0)):self.assertAlmostEqual(hit['local_hit'][axis],value,places=5)
        self.assertEqual(self.project.data(),before)

    def test_fitting_owner_does_not_masquerade_as_board_surface(self):
        with patch('gpu_viewport.pick_world',return_value=dict(id=self.part.id,kind='fitting',world=world(self.part,75,40,25),precise_surface=False)):
            hit=pick(self.app,400,300)
        self.assertIsNone(hit['target_id']);self.assertIn('world',hit['local_hit'])


if __name__=='__main__':unittest.main()
