"""Small real-Tk regressions for clipped, non-modal form documents."""
import tkinter as tk
from tkinter import ttk
import unittest
from core import Project
from dock_workspace import DockWorkspace
from form_dialog import FormDocument

class FormTests(unittest.TestCase):
    def setUp(self):
        self.app=tk.Tk();self.app.geometry('820x440+20+20');self.app.project=Project();self.app.project_generation=0
        self.app.tabs=ttk.Notebook(self.app);self.app.tabs.pack(fill='both',expand=True)
        DockWorkspace(self.app,self.app.tabs)
    def tearDown(self):self.app.destroy()
    def test_content_is_clipped_by_canvas_and_submit_stays_in_footer(self):
        d=FormDocument(self.app,'QA','700x600');calls=[]
        for i in range(24):ttk.Entry(d.form).pack(fill='x',pady=4)
        original=ttk.Button(d.form,text='Zapisz',command=lambda:calls.append(True));original.pack(fill='x')
        self.app.update()
        self.assertIs(d.form.master,d.viewport)
        self.assertIsNone(self.app.grab_current())
        self.assertFalse(original.winfo_ismapped())
        button=d.footer.winfo_children()[0];self.assertTrue(button.winfo_ismapped())
        self.assertLessEqual(button.winfo_rooty()+button.winfo_height(),d.winfo_rooty()+d.winfo_height())
        d.viewport.yview_moveto(1);self.app.update();button.invoke();self.assertEqual(calls,[True])
    def test_content_close_destroys_entire_document_without_recursion(self):
        d=FormDocument(self.app,'QA','700x600');self.app.update();path=str(d)
        d.form.destroy();self.app.update();self.assertNotIn(path,self.app.workspace.entries)
        self.assertEqual(self.app.tabs.tabs(),())
    def test_state_and_callback_survive_detaching(self):
        d=FormDocument(self.app,'QA','700x600');value=tk.StringVar(value='Szkic')
        ttk.Entry(d.form,textvariable=value).pack();self.app.update()
        value.set('Nowe');self.assertTrue(d.has_changes())
        self.app.workspace.detach(d);self.app.update();self.app.workspace.dock(d);self.app.update()
        self.assertEqual(value.get(),'Nowe');self.assertTrue(d.has_changes());self.assertIsNone(self.app.grab_current())
