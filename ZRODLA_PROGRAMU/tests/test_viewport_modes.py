import copy
import unittest
from types import SimpleNamespace
from core import Project,Part,Camera
from viewport_modes import xray_part_ids
from viewport_annotations import paint_drills


class Variable:
    def __init__(self,value):self.value=value
    def get(self):return self.value


class Canvas:
    def __init__(self):self.items=[]
    def winfo_width(self):return 1000
    def winfo_height(self):return 700
    def create_line(self,*args,**kw):self.items.append(('line',args,kw))
    def create_oval(self,*args,**kw):self.items.append(('oval',args,kw))
    def create_text(self,*args,**kw):self.items.append(('text',args,kw))


class ViewModeTests(unittest.TestCase):
    def setUp(self):
        self.project=Project()
        self.a=Part(group='A',u=200,v=150,t=18,material=self.project.materials[0].id,role='side')
        self.d=Part(group='A',u=200,v=150,t=18,material=self.project.materials[0].id,role='door')
        self.b=Part(group='B',u=200,v=150,t=18,material=self.project.materials[0].id)
        self.hidden=Part(group='A',visible=False,material=self.project.materials[0].id)
        self.project.parts=[self.a,self.d,self.b,self.hidden]

    def test_xray_ranges_include_carcass_without_mutating_visibility(self):
        before=copy.deepcopy(self.project.data())
        self.assertEqual(xray_part_ids(self.project,True),{self.a.id,self.d.id,self.b.id})
        self.assertEqual(xray_part_ids(self.project,True,'Wybrany mebel',self.a.id),{self.a.id,self.d.id})
        self.assertEqual(xray_part_ids(self.project,True,'Wybrany element',self.a.id),{self.a.id})
        self.assertEqual(xray_part_ids(self.project,True,'Fronty'),{self.d.id})
        self.assertEqual(xray_part_ids(self.project,False),set())
        self.assertEqual(xray_part_ids(self.project,True,'Wybrany mebel',None),set())
        self.assertEqual(self.project.data(),before)

    def test_mounting_holes_from_rear_are_explicit_hidden_marks_with_exact_hover_data(self):
        operation=dict(type='drill',face='top',u=100,v=75,diameter=5,depth=8,purpose='Wiercenie testowe')
        self.a.operations=[operation];self.project.parts=[self.a]
        canvas=Canvas();camera=Camera(pitch=-24,scale=1,center=(100,75,9))
        app=SimpleNamespace(canvas=canvas,camera=camera,show_drills=Variable(True),drill_mode=Variable('Także zasłonięte'),explode=Variable(False))
        before=copy.deepcopy(self.project.data());paint_drills(app,self.project)
        self.assertEqual(len(app.visible_drills),1)
        self.assertTrue(app.visible_drills[0]['hidden'])
        self.assertIs(app.visible_drills[0]['operation'],operation)
        self.assertTrue(any(item[2].get('outline')=='#4487bc' and item[2].get('dash') for item in canvas.items))
        self.assertEqual(self.project.data(),before)
        app.drill_mode=Variable('Widoczne powierzchnie');paint_drills(app,self.project)
        self.assertFalse(app.visible_drills)

    def test_forward_hole_has_high_contrast_halo_and_highlight(self):
        self.a.operations=[dict(type='drill',face='top',u=100,v=75,diameter=5,depth=8,purpose='Wiercenie testowe')]
        self.project.parts=[self.a];canvas=Canvas()
        app=SimpleNamespace(canvas=canvas,camera=Camera(pitch=24,scale=1,center=(100,75,9)),show_drills=Variable(True),drill_mode=Variable('Także zasłonięte'),explode=Variable(False))
        paint_drills(app,self.project)
        self.assertEqual(len(app.visible_drills),1);self.assertFalse(app.visible_drills[0]['hidden'])
        self.assertTrue(any(item[2].get('outline')=='white' for item in canvas.items))
        self.assertTrue(any(item[2].get('outline')=='#287fc2' for item in canvas.items))
