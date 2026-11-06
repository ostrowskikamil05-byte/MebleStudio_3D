import copy
import tkinter as tk
import unittest
from core import Project
from form_dialog import FormDocument
from visual_catalogue import show,install


class CataloguePanelTests(unittest.TestCase):
    def setUp(self):
        self.app=tk.Tk();self.app.withdraw();self.app.project=Project();self.app.project_generation=0;self.app.status=tk.StringVar();self.app.selected=None
        self.app.dialog=lambda title,size:FormDocument(self.app,title,size).form
        self.app.flush_parameter_drafts=lambda:True;self.received=[]

    def tearDown(self):
        self.app.update_idletasks()
        for ident in self.app.tk.splitlist(self.app.tk.call('after','info')):self.app.tk.call('after','cancel',ident)
        self.app.destroy()

    def test_reuses_one_persistent_tab_and_picker_passes_correct_payload(self):
        install(self.app);panel=self.app.visual_catalogue('legs',on_select=self.received.append);self.app.update_idletasks()
        self.assertEqual(str(panel.kindcombo.cget('state')),'disabled');self.assertEqual(len(panel.cards),8)
        item=panel.items[0];self.assertTrue(panel.activate(item));self.assertEqual(self.received[-1],item)
        self.received[-1]['payload']['leg_height']=999;self.assertNotEqual(item['payload']['leg_height'],999)
        same=show(self.app,'runner',on_select=self.received.append);self.assertIs(same,panel);self.assertEqual(len(self.app.workspace.entries),1)
        self.assertIsNone(self.app.grab_current());self.assertFalse(panel.form.document.has_changes())

    def test_filter_reflows_grid_and_clears_pending_timer_on_destroy(self):
        panel=show(self.app,'furniture',on_select=self.received.append);self.app.update_idletasks()
        panel.query.set('piekarnik');panel.render();self.assertGreater(len(panel.cards),0);self.assertLess(len(panel.cards),52)
        for ident in panel.cards:self.assertIn('piekarnik',next(i['title'] for i in panel.items if i['id']==ident).lower())
        panel.resize(type('Event',(),dict(width=700))());panel.render();self.assertEqual(panel.columns,3)
        panel.query.set('szafa');self.assertIsNotNone(panel.job);panel.form.destroy();self.app.update_idletasks();self.assertIsNone(panel.job)

    def test_stale_project_guard_blocks_callbacks_and_invalid_draft_does_not_select(self):
        panel=show(self.app,'hinge',on_select=self.received.append);item=panel.items[0];self.app.project_generation+=1
        self.assertFalse(panel.activate(item));self.assertFalse(self.received);self.assertIn('Projekt zmienił',panel.note.get())
        fresh=show(self.app,'hinge',on_select=self.received.append);self.assertIsNot(fresh,panel)
        self.app.flush_parameter_drafts=lambda:False;self.assertFalse(fresh.activate(fresh.items[0]));self.assertFalse(self.received)

    def test_install_adds_clear_ribbon_entrypoints_once(self):
        from tkinter import ttk
        self.app.ribbon_pages={'Mebel':ttk.Frame(self.app),'Scena':ttk.Frame(self.app)};install(self.app)
        self.assertEqual(set(self.app.catalogue_buttons),{'furniture','hinge','material','appliance'})
        self.assertEqual(self.app.catalogue_buttons['furniture'].cget('text'),'Katalog mebli')
        count=len(self.app.ribbon_pages['Mebel'].winfo_children());install(self.app);self.assertEqual(len(self.app.ribbon_pages['Mebel'].winfo_children()),count)
        self.app.catalogue_buttons['appliance'].invoke();self.assertEqual(self.app._visual_catalogue_panel.kind,'appliance')


if __name__=='__main__':unittest.main()
