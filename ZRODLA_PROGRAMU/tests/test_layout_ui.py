"""Pure interaction/coordinate regressions; no desktop windows required."""
import copy
import math
from types import SimpleNamespace
import tkinter as tk
import unittest
from unittest.mock import patch

from core import Camera,Part,Project,world
from furniture_layout_ui import screen_move,drag,end_drag,cancel_drag,LayoutDocument
from sketch import normalized_part


class Value:
    def __init__(self,value):self.value=value
    def get(self):return self.value
    def set(self,value):self.value=value


class LayoutInteractionTests(unittest.TestCase):
    def app(self):
        project=Project(parts=[])
        project.parts.append(Part(group='A',u=600,v=400,t=18,material=project.materials[0].id))
        app=SimpleNamespace(project=project,camera=Camera(),layout_magnet=Value(False),layout_tolerance=Value(25),
                            status=Value(''),dragged=False,undo=[],draw=lambda:None,refresh=lambda:None)
        app.snapshot=lambda:app.undo.append(copy.deepcopy(app.project.data()))
        app.layout_drag=dict(base=copy.deepcopy(project),group='A',start=(100,100),candidate=None,last=0)
        return app

    def assertPoint(self,a,b):
        for x,y in zip(a,b):self.assertAlmostEqual(x,y,places=6)

    def test_screen_drag_reconstructs_room_plane_for_rotated_cameras(self):
        for yaw in (-155,-90,-38,0,77,180):
            for pitch in (-50,24,90):
                with self.subTest(yaw=yaw,pitch=pitch):
                    camera=Camera(yaw=yaw,pitch=pitch,scale=.73,center=(1000,-800,350),pan=(24,-19))
                    origin=camera.project((0,0,0),1100,700)
                    target=camera.project((170,-95,0),1100,700)
                    self.assertPoint(screen_move(camera,target[0]-origin[0],target[1]-origin[1]),(170,-95,0))

    def test_vertical_drag_and_degenerate_views(self):
        for pitch in (0,24,-35):
            camera=Camera(yaw=37,pitch=pitch,scale=1.2)
            a,b=camera.project((0,0,0),400,300),camera.project((0,0,180),400,300)
            self.assertPoint(screen_move(camera,b[0]-a[0],b[1]-a[1],True),(0,0,180))
        with self.assertRaisesRegex(ValueError,'Góra'):screen_move(Camera(pitch=0),10,10)
        with self.assertRaisesRegex(ValueError,'Przód'):screen_move(Camera(pitch=90),0,10,True)

    def test_drag_preview_skips_CAD_validation_and_release_is_one_undo(self):
        app=self.app();before=app.project.data();event=SimpleNamespace(x=160,y=125,state=0)
        with patch.object(Project,'validate',autospec=True) as validate:
            drag(app,event,force=True)
            self.assertEqual(validate.call_count,0);self.assertEqual(len(app.undo),0)
            self.assertNotEqual(before,app.project.data())
            self.assertTrue(end_drag(app,event));self.assertEqual(validate.call_count,1)
        self.assertEqual(app.undo,[before]);self.assertIsNone(app.layout_drag)
        self.assertNotEqual(before,app.project.data())

    def test_cancel_drag_restores_uncommitted_project(self):
        app=self.app();before=app.project.data()
        drag(app,SimpleNamespace(x=155,y=115,state=0),force=True)
        cancel_drag(app)
        self.assertEqual(app.project.data(),before);self.assertEqual(app.undo,[]);self.assertIsNone(app.layout_drag)

    def test_layout_form_applies_absolute_bounds_and_angle_without_moving_part_geometry(self):
        from furniture_layout import group_bounds,group_layout
        app=self.app();app.number=lambda value:float(value.get());before=app.project.data()
        form=SimpleNamespace(app=app,current=lambda:'A',ensure_current_project=lambda **kwargs:True,read=lambda:None,
                             vars={key:Value(value) for key,value in [('x',1000),('y',-500),('z',120),('yaw',90)]})
        LayoutDocument.apply(form)
        self.assertPoint(group_bounds(app.project,'A')[0],(1000,-500,120))
        self.assertEqual(group_layout(app.project,'A')['yaw'],90)
        self.assertEqual((app.project.parts[0].x,app.project.parts[0].y,app.project.parts[0].z),(0,0,0))
        self.assertEqual(app.undo,[before])

    def test_invalid_absolute_angle_form_keeps_current_project_and_undo(self):
        app=self.app();app.number=lambda value:float(value.get());before=app.project.data()
        form=SimpleNamespace(app=app,current=lambda:'A',ensure_current_project=lambda **kwargs:True,read=lambda:None,
                             vars={key:Value(value) for key,value in [('x',1000),('y',-500),('z',120),('yaw','nan')]})
        with self.assertRaises(ValueError):LayoutDocument.apply(form)
        self.assertEqual(app.project.data(),before);self.assertEqual(app.undo,[])

    def test_invalid_tolerance_does_not_raise_callback_error(self):
        class InvalidTolerance:
            def get(self):raise tk.TclError('expected floating-point number')
        app=self.app();before=app.project.data();app.layout_magnet=Value(True);app.layout_tolerance=InvalidTolerance()
        drag(app,SimpleNamespace(x=155,y=115,state=0),force=True)
        self.assertEqual(app.project.data(),before);self.assertIsNone(app.layout_drag['candidate']);self.assertTrue(app.status.get())

    def test_sketch_normalization_after_furniture_rotation_preserves_room_geometry(self):
        for plane in ('XY','XZ','YZ'):
            part=Part(plane=plane,x=40,y=70,z=90,rx=12,ry=23,rz=34,
                      layout=dict(x=1300,y=-600,z=220,yaw=90),holes=[[70,80,4,5]])
            outline=[(-30,20),(130,20),(130,160),(-30,160)]
            result=normalized_part(part,outline)
            self.assertEqual(result.layout,part.layout);self.assertEqual(result.holes,[[100,60,4,5]])
            for old,local in zip(outline,result.points()):self.assertPoint(world(part,*old,0),world(result,*local,0))

    def test_manual_profile_edge_reindexing_remains_guarded(self):
        part=Part(profile={'kind':'circle'},contour=[(0,200),(200,0),(400,200),(200,400)],u=400,v=400,
                  fillets=[dict(edge='top:0',radius=3)],layout=dict(yaw=90,x=1000))
        before=copy.deepcopy(part)
        altered=[(0,200),(180,0),(400,200),(200,400)]
        with self.assertRaisesRegex(ValueError,'numeracja'):normalized_part(part,altered)
        self.assertEqual(part,before)


if __name__=='__main__':unittest.main()
