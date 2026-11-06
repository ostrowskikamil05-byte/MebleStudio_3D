"""Inspector transactions tested without creating a competing native App."""
import unittest
from types import SimpleNamespace
from core import Project
from corner_quick_ui import CornerPanel
from corner_furniture import default_corner_config,create_corner,read_corner_config
from furniture_configurator import default_config,configure


class Var:
    def __init__(self,value=''):self.value=value
    def get(self):return self.value
    def set(self,value):self.value=value


class CornerPanelTests(unittest.TestCase):
    def panel(self,project,group):
        panel=object.__new__(CornerPanel);panel.group=None;panel.config={};panel.syncing=False;panel.job=None;panel.generation=0;panel.fields={};panel.mapping={};panel.signature=None;panel.has_draft=False
        panel.note=Var();panel.apply_button=SimpleNamespace(configure=lambda **kwargs:None);panel.draw_preview=lambda:None
        panel.after_cancel=lambda job:None;panel.after=lambda ms,fn:'job';panel.cancelled=[]
        cfg=default_corner_config(project)
        for key in ('width','depth','height','arm_depth','arm_depth_y','radius','diagonal_angle','shelves','thickness','front_thickness','leg_height'):panel.fields[key]=Var(str(cfg[key]))
        for key,values in [('side',{'Lewy':'left','Prawy':'right'}),('fronts',{'Otwarte':'open','Drzwi':'orthogonal','Skośny':'diagonal'}),('furniture_type',{'Szafka':'cabinet','Szafa':'wardrobe'}),('top_mode',{'Płyta':'panel','Listwy':'rails'}),('front_style',{'Gładki':'smooth','Ramka':'frame'})]:
            panel.mapping[key]=values;panel.fields[key]=Var(next(k for k,v in values.items() if v==cfg[key]))
        selected=next(p.id for p in project.parts if p.group==group)
        app=SimpleNamespace(project=project,selected=selected,project_generation=0,status=Var(),motion_value=Var(),snapshots=0,open_amounts={})
        app.selected_part=lambda:next((p for p in app.project.parts if p.id==app.selected),None)
        def snapshot():app.snapshots+=1
        app.snapshot=snapshot;app.refresh=panel.refresh;panel.app=app;panel.set_config(cfg);panel.refresh();return panel,app

    def test_pending_dimensions_flush_atomically_and_noop_has_no_undo(self):
        p=Project();q,g=create_corner(p,dict(default_corner_config(p),fronts='orthogonal'));panel,app=self.panel(q,g)
        panel.fields['width'].set('1000');panel.changed();self.assertTrue(panel.has_draft);self.assertEqual(panel.job,'job')
        self.assertTrue(panel.flush_draft());self.assertEqual(app.snapshots,1);self.assertEqual(read_corner_config(app.project,g)['width'],1000);self.assertFalse(panel.has_draft)
        self.assertTrue(panel.apply());self.assertEqual(app.snapshots,1)

    def test_invalid_draft_blocks_save_without_mutating_project(self):
        p=Project();q,g=create_corner(p,default_corner_config(p));panel,app=self.panel(q,g);before=q.data()
        panel.fields['arm_depth'].set('5000');panel.changed();self.assertFalse(panel.flush_draft());self.assertTrue(panel.has_draft);self.assertEqual(app.snapshots,0);self.assertEqual(app.project.data(),before)

    def test_regular_conversion_uses_visible_dimensions_and_one_snapshot(self):
        p=Project();c=default_config(0,p);c.update(width=1200,height=2100,leg_height=0);q,g=configure(p,c);panel,app=self.panel(q,g)
        self.assertEqual(float(panel.fields['height'].get()),2100);self.assertEqual(float(panel.fields['width'].get()),1200)
        panel.fields['width'].set('1400');panel.fields['arm_depth_y'].set('480');self.assertTrue(panel.convert_selected());cfg=read_corner_config(app.project,g)
        self.assertEqual((cfg['width'],cfg['height'],cfg['arm_depth_y']),(1400,2100,480));self.assertEqual(app.snapshots,1);self.assertEqual(panel.group,g)

    def test_destroy_cancels_scheduled_apply_and_project_switch_guard_is_explicit(self):
        p=Project();q,g=create_corner(p,default_corner_config(p));panel,app=self.panel(q,g)
        panel.fields['width'].set('1000');panel.changed();panel.destroyed(SimpleNamespace(widget=panel));self.assertIsNone(panel.job)
        app.project_generation=1;self.assertFalse(panel.apply());self.assertIn('zmienił',panel.note.get());self.assertEqual(app.snapshots,0)


if __name__=='__main__':unittest.main()
