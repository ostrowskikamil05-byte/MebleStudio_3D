import copy
import tkinter as tk
import unittest

from core import Project
from room_objects import add_object, scene_of
from room_scene_ui import RoomScenePanel


class TestApp:
    def __init__(self):
        self.project=Project();self.selected_scene=None;self.selected=None
        self.history=[];self.draws=0;self.panel=None
    def snapshot(self):self.history.append(copy.deepcopy(self.project))
    def draw(self):self.draws+=1
    def refresh(self):self.panel.refresh()
    def undo(self):self.project=self.history.pop();self.refresh()


class RoomScenePanelTests(unittest.TestCase):
    def setUp(self):
        self.root=tk.Tk();self.root.withdraw()
        self.app=TestApp();self.panel=RoomScenePanel(self.root,self.app)
        self.app.panel=self.panel;self.panel.pack();self.root.update_idletasks()
    def tearDown(self):self.root.destroy()

    def test_add_edit_rotate_delete_are_snapshots_with_no_parts(self):
        ident=self.panel.add_selected('washer')
        self.assertEqual(self.app.selected_scene,ident)
        self.panel.values['width'].set('720')
        self.panel.values['price'].set('1499,99')
        self.assertTrue(self.panel.apply_selected())
        obj=scene_of(self.app.project)['objects'][0]
        self.assertEqual(obj['width'],720);self.assertEqual(obj['price'],1499.99)
        self.assertTrue(self.panel.rotate_selected(90))
        self.assertEqual(scene_of(self.app.project)['objects'][0]['yaw'],90)
        self.assertTrue(self.panel.delete_selected())
        self.assertEqual(scene_of(self.app.project)['objects'],[])
        self.assertEqual(len(self.app.history),4);self.assertEqual(self.app.project.parts,[])

    def test_refresh_tracks_undo_loaded_selection_and_scene_controls(self):
        ident=self.panel.add_selected('lamp')
        self.panel.room.set('Łazienka');self.panel.brightness.set('0,35');self.panel.lights.set(False)
        self.assertTrue(self.panel.apply_scene())
        self.assertEqual(scene_of(self.app.project)['room'],'bathroom')
        self.assertFalse(scene_of(self.app.project)['lights_enabled'])
        self.app.undo()
        self.assertEqual(self.panel.room.get(),'Neutralne');self.assertEqual(self.panel.brightness.get(),'1')
        self.assertTrue(self.panel.lights.get())
        self.app.selected_scene='scene:'+ident;self.panel.refresh()
        self.assertEqual(self.panel.selected,ident)
        self.app.project=Project();self.panel.refresh()
        self.assertIsNone(self.panel.selected);self.assertEqual(self.panel.values['name'].get(),'')

    def test_bad_dimension_does_not_commit_or_spawn_modal(self):
        self.panel.add_selected('fridge');before=self.app.project.data();count=len(self.app.history)
        self.panel.values['height'].set('0')
        self.assertFalse(self.panel.apply_selected())
        self.assertEqual(self.app.project.data(),before);self.assertEqual(len(self.app.history),count)
        self.assertTrue(self.panel.note.get())

    def test_tree_selection_does_not_queue_endless_refresh(self):
        one=self.panel.add_selected('washer');two=self.panel.add_selected('dryer')
        self.assertTrue(self.panel.select_object(one))
        for _ in range(4):self.root.update()
        self.assertEqual(self.panel.selected,one)
        self.assertEqual(self.app.selected_scene,one)
        self.assertLess(self.app.draws,20)


if __name__=='__main__':unittest.main()
