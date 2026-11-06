"""Real compact inspector widgets tested with the root window withdrawn."""
import tkinter as tk
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from core import Project
from corner_furniture import default_corner_config,create_corner
from furniture_configurator import default_config,configure
from quick_studio_ui import QuickInspector


class QuickInspectorDisplayTests(unittest.TestCase):
    def setUp(self):
        self.root=tk.Tk();self.root.withdraw()
        self.app=SimpleNamespace(project=Project(),project_generation=0,selected=None)
        self.app.selected_part=lambda:next((p for p in self.app.project.parts if p.id==self.app.selected),None)
        with patch('configurator_presets.load_presets',return_value={}):self.panel=QuickInspector(self.root,self.app)
        self.panel.pack(fill='both',expand=True)

    def tearDown(self):
        self.panel.cancel_pending();self.root.destroy()

    def select(self,project,group):
        self.app.project=project;self.app.selected=next(p.id for p in project.parts if p.group==group)
        self.panel.refresh();self.root.update_idletasks()

    def assert_dimensions_and_materials(self,config):
        for key in ('width','depth','height','thickness','front_thickness','leg_height'):
            self.assertEqual(float(self.panel.fields[key][0].get()),config[key],key)
            self.assertEqual(self.panel.config[key],config[key],key)
        for key in ('material','front_material'):
            displayed=self.panel.fields[key][0].get();self.assertEqual(self.panel.combos[key][1][displayed],config[key]);self.assertEqual(self.panel.config[key],config[key])

    def test_corner_shows_actual_dimensions_materials_and_routes_editing_to_corner_tab(self):
        c=default_corner_config(self.app.project);c.update(width=1000,depth=1100,height=970,thickness=22,front_thickness=21,leg_height=120,material=self.app.project.materials[-1].id,front_material=self.app.project.materials[0].id)
        q,g=create_corner(self.app.project,c);self.select(q,g);before=q.data()
        self.assert_dimensions_and_materials(c)
        self.assertIn('zakładce Narożnik',self.panel.note.get());self.assertIn('Narożnik',str(self.panel.apply_button.cget('text')));self.assertEqual(str(self.panel.apply_button.cget('state')),'disabled')
        self.assertTrue(self.panel.flush_draft());self.assertFalse(self.panel.apply(False));self.assertEqual(self.app.project.data(),before)
        c.update(width=1200,depth=950,height=1050,material=q.materials[0].id,front_material=q.materials[-1].id)
        r,g=create_corner(q,c,g);self.select(r,g);self.assert_dimensions_and_materials(c)

    def test_switch_back_to_regular_configured_cabinet_restores_real_fields_and_editing(self):
        c=default_corner_config(self.app.project);c.update(width=1000,depth=1000);q,g=create_corner(self.app.project,c);self.select(q,g)
        regular=default_config(0,q);regular.update(name='Zwykła szafka',width=750,depth=460,height=890,thickness=20)
        r,name=configure(q,regular);self.select(r,name)
        self.assert_dimensions_and_materials(regular);self.assertEqual(str(self.panel.apply_button.cget('state')),'normal');self.assertEqual(self.panel.apply_button.cget('text'),'Zastosuj do mebla');self.assertNotIn('zakładce Narożnik',self.panel.note.get())

    def test_shared_worktop_shows_actual_format_dimensions_material_and_its_type(self):
        from workspace_worktops import install_shared_worktop
        cfg=default_config(0,self.app.project);cfg.update(width=750,depth=460,height=890,thickness=20)
        q,g=configure(self.app.project,cfg)
        r,name=install_shared_worktop(q,[g],thickness=30,overhang=25,material=q.materials[-1].id)
        self.select(r,name);top=next(p for p in r.parts if p.group==name)
        self.assertEqual(self.panel.templatevar.get(),'Wspólny blat');self.assertEqual(str(self.panel.apply_button.cget('state')),'disabled')
        for key,value in [('width',top.u),('depth',top.v),('height',top.t),('thickness',top.t)]:self.assertEqual(float(self.panel.fields[key][0].get()),value,key)
        self.assertEqual(self.panel.combos['material'][1][self.panel.fields['material'][0].get()],top.material)
        self.assertIn('wysokość oznacza grubość',self.panel.note.get());self.assertIn(f'{top.z:g} mm',self.panel.note.get())


if __name__=='__main__':unittest.main()
