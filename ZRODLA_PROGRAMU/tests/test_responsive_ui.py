import copy
from pathlib import Path
import tempfile
import time
import tkinter as tk
from tkinter import ttk
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from responsive_ui import WrapToolbar, FieldRow, install, UILayout
from ui_preferences import DEFAULTS, load_preferences


class WindowFrameTests(unittest.TestCase):
    """No Tk instance: exercise the settings-to-window contract with native metrics."""
    def test_apply_reserves_native_caption_menu_and_borders_inside_workarea(self):
        for workarea,frame,requested,expected in [
            ((0,0,1366,728),(16,64),(1366,768),(1350,664)),
            ((-1920,0,0,1040),(24,88),(1920,1080),(1896,952)),
        ]:
            with self.subTest(workarea=workarea,frame=frame):
                calls={}
                app=SimpleNamespace(minsize=lambda w,h:calls.update(minimum=(w,h)),
                                    geometry=lambda geometry:calls.update(geometry=geometry))
                controller=SimpleNamespace(app=app,suspended=False,prefs=None,set_scale=lambda scale:None,
                    show_inspector=lambda *a,**k:None,show_ribbon=lambda *a,**k:None,
                    show_toolbars=lambda *a,**k:None,schedule=lambda:None,schedule_save=lambda:None)
                prefs=dict(DEFAULTS,window_mode='custom',window_width=requested[0],window_height=requested[1],
                           window_x=workarea[0]+10,window_y=10)
                with patch('responsive_ui.monitor_info',return_value=dict(workarea=workarea,dpi=144,frame=frame)):
                    UILayout.apply(controller,prefs,geometry=True)
                # These are client dimensions; the complete native window must
                # fit above the taskbar, including its caption and menu bar.
                self.assertEqual(calls['geometry'],f'{expected[0]}x{expected[1]}+{workarea[0]}+0')
                self.assertEqual(expected[0]+frame[0],workarea[2]-workarea[0])
                self.assertEqual(expected[1]+frame[1],workarea[3]-workarea[1])
                self.assertFalse(controller.suspended)


class ResponsiveTkTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory();self.path=Path(self.directory.name)/'ui.json'
        self.app=tk.Tk();self.app.attributes('-alpha',0)
        self.app.geometry('900x600+30+30');self.app.project={'unchanged':['model']};self.app.dirty=False
        self.app.status=tk.StringVar();self.app.workspace_pane=ttk.Panedwindow(self.app,orient='vertical');self.app.workspace_pane.pack(fill='both',expand=True)
        self.app.ribbon=ttk.Notebook(self.app.workspace_pane);self.app.workspace_pane.add(self.app.ribbon,weight=0)
        page=ttk.Frame(self.app.ribbon);self.app.ribbon.add(page,text='Mebel')
        self.app.main_pane=ttk.Panedwindow(self.app.workspace_pane,orient='horizontal');self.app.workspace_pane.add(self.app.main_pane,weight=1)
        self.app.inspector_frame=ttk.Frame(self.app.main_pane,width=330);self.app.main_pane.add(self.app.inspector_frame,weight=0)
        self.app.inspector_tabs=ttk.Notebook(self.app.inspector_frame);self.app.inspector_tabs.pack(fill='both',expand=True)
        for name in ('Mebel','CAD','Scena','Narożnik'):
            self.app.inspector_tabs.add(ttk.Frame(self.app.inspector_tabs),text=name)
        self.app.view_frame=ttk.Frame(self.app.main_pane);self.app.main_pane.add(self.app.view_frame,weight=1)
        self.app.view_toolbar=WrapToolbar(self.app.view_frame);self.app.view_toolbar.pack(fill='x')
        self.app.detail_toolbar=WrapToolbar(self.app.view_frame);self.app.detail_toolbar.pack(fill='x')
        self.app.canvas=tk.Canvas(self.app.view_frame);self.app.canvas.pack(fill='both',expand=True)
        self.app.responsive_toolbars=[self.app.view_toolbar,self.app.detail_toolbar]
        self.app.update();self.before=copy.deepcopy(self.app.project)
        self.layout=install(self.app,dict(DEFAULTS,window_mode='custom',window_width=900,window_height=600),self.path)
        self.pump()
    def pump(self,duration=.1):
        deadline=time.monotonic()+duration
        while time.monotonic()<deadline:self.app.update();time.sleep(.005)
    def tearDown(self):
        for job in (self.layout.job,self.layout.save_job):
            if job is not None:
                try:self.app.after_cancel(job)
                except tk.TclError:pass
        self.app.destroy();self.directory.cleanup()
    def test_wrap_reaches_last_action_and_adopts_later_plugin_control(self):
        toolbar=self.app.view_toolbar
        for i in range(10):ttk.Button(toolbar,text='Akcja '+str(i)+' długa').pack(side='left')
        toolbar.schedule();self.pump()
        last=toolbar.winfo_children()[-1]
        self.assertEqual(last.winfo_manager(),'place')
        self.assertLessEqual(last.winfo_x()+last.winfo_width(),toolbar.winfo_width())
        self.assertLessEqual(last.winfo_y()+last.winfo_height(),toolbar.winfo_height())
        added=ttk.Button(toolbar,text='Dopisane okucie');added.pack(side='left');toolbar.schedule();self.pump()
        self.assertEqual(added.winfo_manager(),'place')
    def test_hide_show_and_sash_roundtrip_preserve_project(self):
        self.layout.show_inspector(False);self.layout.show_ribbon(False);self.layout.show_toolbars(False);self.pump()
        self.assertEqual(len(self.app.main_pane.panes()),1);self.assertEqual(len(self.app.workspace_pane.panes()),1)
        self.layout.show_inspector(True);self.layout.show_ribbon(True);self.layout.show_toolbars(True);self.pump()
        self.app.main_pane.sashpos(0,405);self.app.workspace_pane.sashpos(0,135)
        self.layout.sash_released();self.pump();self.layout.persist()
        prefs=load_preferences(self.path)
        self.assertEqual(prefs['inspector_width'],405);self.assertEqual(prefs['ribbon_height'],135)
        self.assertEqual(self.app.project,self.before);self.assertFalse(self.app.dirty)
        self.assertEqual(len(self.app.main_pane.panes()),2)
    def test_scale_does_not_compound_and_field_stacks(self):
        row=FieldRow(self.app.inspector_frame,'Bardzo długa nazwa pola',width=22);row.pack(fill='x')
        p=self.layout.preferences();p['inspector_width']=240
        self.layout.apply(p,geometry=False);self.pump();self.assertTrue(row._stacked)
        p=self.layout.preferences();p['ui_scale']=1.5
        self.layout.apply(p,geometry=False);self.pump();first=row.label.cget('font')
        from tkinter import font
        size=font.Font(root=self.app,font=first).actual()['size']
        self.layout.apply(p,geometry=False);self.pump()
        self.assertEqual(font.Font(root=self.app,font=row.label.cget('font')).actual()['size'],size)
        self.assertEqual(self.app.project,self.before)
    def test_absolute_negative_monitor_geometry_is_accepted_by_tk(self):
        p=self.layout.preferences();p.update(window_x=-1700,window_y=40)
        with patch('responsive_ui.monitor_info',return_value=dict(workarea=(-1920,0,0,1080),dpi=144)):
            self.layout.apply(p,geometry=True);self.pump()
        self.assertEqual(self.app.winfo_x(),-1700)
        self.assertEqual(self.app.winfo_y(),40)
    def test_settings_accept_continuous_scale_and_do_not_change_project(self):
        p=self.layout.preferences();p['ui_scale']=1.2;self.layout.apply(p,geometry=False)
        def dialog(title,size):
            form=ttk.Frame(self.app);form.document=form;form.mark_clean=lambda:None
            return form
        self.app.dialog=dialog;self.app.workspace=SimpleNamespace(select=lambda page:None)
        from ui_settings import open_settings
        form=open_settings(self.app)
        self.assertIn('120%',form.interface_vars['ui_scale'].get())
        form.interface_vars['quality'].set('Wysoka');form.apply_interface_preferences()
        self.assertEqual(self.layout.preferences()['quality'],'high')
        self.assertEqual(self.app.project,self.before);self.assertFalse(self.app.dirty)
    def test_narrow_inspector_selector_reaches_last_panel(self):
        p=self.layout.preferences();p.update(inspector_width=240,ui_scale=2.0)
        self.layout.apply(p,geometry=False);self.pump()
        self.assertEqual(self.layout.panel_selector.winfo_manager(),'pack')
        self.assertEqual(tuple(self.layout.panel_selector.cget('values')),('Mebel','CAD','Scena','Narożnik'))
        self.layout.panel_selector.current(3);self.layout.select_panel();self.pump()
        self.assertEqual(self.app.inspector_tabs.tab(self.app.inspector_tabs.select(),'text'),'Narożnik')

    def test_large_sash_values_remain_persistable(self):
        self.app.geometry('1920x1080+0+0');self.pump()
        self.app.main_pane.sashpos(0,1500);self.app.workspace_pane.sashpos(0,600)
        self.layout.sash_released();self.pump()
        self.assertLessEqual(self.layout.preferences()['inspector_width'],1200)
        self.assertLessEqual(self.layout.preferences()['ribbon_height'],500)
        self.assertTrue(self.layout.persist())
        self.assertLessEqual(load_preferences(self.path)['inspector_width'],1200)

    def test_already_stacked_label_wraps_after_further_narrowing(self):
        row=FieldRow(self.app.inspector_frame,'Bardzo długa nazwa pola ze szczegółami',width=22)
        row.pack(fill='x');self.pump()
        p=self.layout.preferences();p['inspector_width']=330
        self.layout.apply(p,geometry=False);self.pump();self.assertTrue(row._stacked)
        old=int(row.label.cget('wraplength'))
        p['inspector_width']=240;self.layout.apply(p,geometry=False);self.pump()
        self.assertTrue(row._stacked);self.assertLess(int(row.label.cget('wraplength')),old)
        self.assertLessEqual(int(row.label.cget('wraplength')),row.winfo_width())


if __name__=='__main__':unittest.main()
