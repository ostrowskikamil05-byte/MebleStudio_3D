import copy
from types import SimpleNamespace
import tkinter as tk
import unittest
from unittest.mock import patch
from core import Project,Part,Camera
from drag_hardware import HardwareDragController
from hardware_placement_ui import HardwarePlacementPanel


class Variable:
    def __init__(self,value=False):self.value=value
    def get(self):return self.value
    def set(self,value):self.value=value


class Canvas:
    def __init__(self):self.items=[]
    def winfo_width(self):return 800
    def winfo_height(self):return 600
    def winfo_rootx(self):return 100
    def winfo_rooty(self):return 150
    def delete(self,tag):self.items=[]
    def create_polygon(self,*a,**kw):self.items.append(kw);return len(self.items)
    def create_text(self,*a,**kw):self.items.append(kw);return len(self.items)
    def create_rectangle(self,*a,**kw):self.items.append(kw);return len(self.items)
    def bbox(self,item):return (5,5,70,40)
    def tag_lower(self,*a):pass


class App:
    def __init__(self):
        self.project=Project();self.part=Part(name='Panel',u=400,v=300,t=18,material=self.project.materials[0].id)
        self.project.parts=[self.part];self.canvas=Canvas();self.camera=Camera(scale=1)
        self.status=Variable();self.history=[];self.draws=0;self.refreshes=0;self.selected=None
        self.open_amounts={};self.explode=Variable();self.project_generation=0;self.invalid=False
        self.edit_mode=Variable('camera');self.selected_scene=None
    def snapshot(self):self.history.append(copy.deepcopy(self.project))
    def draw(self):self.draws+=1
    def refresh(self):self.refreshes+=1
    def pick_attachment_target(self,x,y):
        return None if self.invalid else dict(target_id=self.part.id,local_hit=dict(u=200,v=100,w=18,face='top'))


def event(x=250,y=280):return SimpleNamespace(x_root=x,y_root=y)


class ControllerTests(unittest.TestCase):
    def setUp(self):self.app=App();self.engine=HardwareDragController(self.app)

    def test_click_tool_then_target_is_one_snapshot_with_green_preview(self):
        self.engine.begin('led',dict(width=12));before=self.app.project.data()
        self.engine.move(event());self.assertTrue(self.engine.proposal['valid'])
        self.assertEqual(self.app.project.data(),before);self.assertTrue(any(i.get('outline')=='#23986a' for i in self.app.canvas.items))
        self.assertEqual(self.engine.target_click(event()),'break');self.assertFalse(self.engine.active)
        self.assertEqual(len(self.app.history),1);self.assertEqual(len(self.app.project.hardware),3)

    def test_physical_press_motion_release_and_invalid_drop(self):
        self.engine.begin('led',event=event(20,30));self.app.invalid=True
        self.engine.move(event());self.assertFalse(self.engine.proposal['valid']);self.engine.end(event())
        self.assertEqual(len(self.app.history),0);self.assertTrue(self.engine.active)
        self.app.invalid=False;self.engine.move(event());self.engine.end(event())
        self.assertFalse(self.engine.active);self.assertEqual(len(self.app.history),1)

    def test_tile_click_release_stays_armed_and_escape_clears_ghost(self):
        self.engine.begin('led',event=event(10,10));self.engine.end(event(10,10))
        self.assertTrue(self.engine.active);self.assertFalse(self.app.history)
        self.engine.move(event());self.engine.cancel();self.assertFalse(self.engine.active);self.assertEqual(self.app.canvas.items,[])

    def test_explosion_open_pose_and_new_document_block_commit(self):
        for exploded,open_value in ((True,0),(False,.25)):
            self.app.explode.set(exploded);self.app.open_amounts={'front':open_value}
            self.engine.begin('led');self.engine.target_click(event())
            self.assertFalse(self.app.history);self.assertIn('Zamknij',self.engine.proposal['message'])
        self.app.explode.set(False);self.app.open_amounts={};self.engine.begin('led')
        self.app.project_generation+=1;self.engine.target_click(event())
        self.assertFalse(self.app.history);self.assertFalse(self.engine.active)

    def test_cache_retains_real_project_and_replaces_same_ids_in_new_project(self):
        from drag_hardware import preview_attachment
        self.engine.begin('led')
        with patch('drag_hardware.preview_attachment',wraps=preview_attachment) as compute:
            self.engine.move(event());self.engine._last_move=0;self.engine.move(event())
            self.assertEqual(compute.call_count,1);old=self.app.project
            self.assertIs(self.engine._cache_project,old)
            self.app.project=copy.deepcopy(old);self.app.project.parts[0].u=800
            self.engine._last_move=0;self.engine.move(event())
            self.assertEqual(compute.call_count,2);self.assertEqual(self.engine.proposal['length'],740)
            self.assertIs(self.engine._cache_project,self.app.project)

    def test_new_bulb_is_selected_in_layout_mode_with_single_undo(self):
        self.engine.begin('bulb');self.engine.target_click(event())
        self.assertIsNone(self.app.selected);self.assertEqual(self.app.edit_mode.get(),'layout')
        self.assertEqual(self.app.selected_scene,self.app.project.scene['objects'][-1]['id'])
        self.assertEqual(len(self.app.history),1)


class PanelTests(unittest.TestCase):
    def setUp(self):
        self.root=tk.Tk();self.root.withdraw();self.app=App();self.panel=HardwarePlacementPanel(self.root,self.app)
        self.panel.pack();self.root.update_idletasks()
    def tearDown(self):self.root.destroy()

    def test_tray_options_and_accessible_arm_without_extra_window(self):
        self.assertIs(self.app.hardware_drag,self.panel.controller);self.assertEqual(len(self.panel.tiles),6)
        self.panel.face.set('Krawędź V=0');self.panel.width.set('8,5');self.panel.price.set('12,50')
        self.panel.axis.set('Oś V powierzchni')
        self.panel.begin('led');self.assertTrue(self.panel.controller.active)
        self.assertEqual(self.panel.controller.options['face'],'v0');self.assertEqual(self.panel.controller.options['width'],8.5)
        self.assertEqual(self.panel.controller.options['price_per_m'],12.5);self.assertFalse(self.app.history)
        self.assertEqual(self.panel.controller.options['axis'],'v')
        self.assertEqual([c for c in self.root.winfo_children() if isinstance(c,tk.Toplevel)],[])
        self.assertEqual(self.panel.details.winfo_manager(),'')
        self.panel.toggle_details();self.assertEqual(self.panel.details.winfo_manager(),'pack')
        self.panel.toggle_details();self.assertEqual(self.panel.details.winfo_manager(),'')

    def test_bad_number_does_not_arm_or_commit(self):
        self.panel.width.set('x');self.panel.begin('led')
        self.assertFalse(self.panel.controller.active);self.assertFalse(self.app.history);self.assertTrue(self.panel.note.get())


if __name__=='__main__':unittest.main()
