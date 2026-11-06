"""Native Tk regression tests for document state and mouse docking gestures."""
import tkinter as tk
from tkinter import ttk
import unittest
import sys
from unittest.mock import patch, Mock

from core import Project
from dock_workspace import DockWorkspace, VisualizationDocument
from sketch import Sketch


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        try:
            self.app = tk.Tk()
        except tk.TclError as exc:
            self.skipTest(str(exc))
        self.app.geometry('1180x720+10+10')
        self.app.project = Project()
        self.app.selected_part = lambda: None
        self.app.tabs = ttk.Notebook(self.app)
        self.app.tabs.pack(fill='both', expand=True)
        self.workspace = DockWorkspace(self.app, self.app.tabs)
        self.main = tk.Frame(self.app)
        self.workspace.add_existing(self.main, 'Projekt 3D')
        self.app.update()

    def tearDown(self):
        if hasattr(self, 'app'):
            self.app.destroy()

    def test_unsaved_sketch_survives_detach_redock_and_shortcut(self):
        sketch = Sketch(self.app)
        self.app.update()
        sketch.mode.set('Wielokąt')
        sketch.add_point((25, 40))
        sketch.add_point((275, 40))
        sketch.fields['name'].set('Niezapisany bok')
        sketch.u.set('77.5')
        old_canvas, old_points, old_history = sketch.canvas, sketch.points[:], sketch.history[:]
        self.workspace.detach(sketch)
        self.app.update()
        self.assertEqual(sketch.winfo_toplevel(), sketch)
        self.assertIsNone(self.app.grab_current())
        self.workspace.dock(sketch)
        self.app.update()
        self.assertEqual(sketch.winfo_toplevel(), self.app)
        self.assertIs(sketch.canvas, old_canvas)
        self.assertEqual(sketch.points, old_points)
        self.assertEqual(sketch.history, old_history)
        self.assertEqual(sketch.fields['name'].get(), 'Niezapisany bok')
        self.assertEqual(sketch.u.get(), '77.5')
        root_undo = []
        self.app.bind('<Control-z>', lambda event: root_undo.append(True))
        sketch.canvas.focus_force()
        self.app.update()
        sketch.canvas.event_generate('<Control-z>')
        self.app.update()
        self.assertEqual(len(sketch.points), 1)
        self.assertEqual(root_undo, [])

    def test_synthetic_mouse_drag_from_tab_and_header_back(self):
        sketch = Sketch(self.app)
        self.app.update()
        n = self.app.tabs
        tab_x = next(x for x in range(10, n.winfo_width(), 5)
                     if n.identify(x, 16) and n.index('@%d,16' % x) == 1)
        n.event_generate('<ButtonPress-1>', x=tab_x, y=16)
        n.event_generate('<B1-Motion>', x=tab_x+25, y=110)
        n.event_generate('<ButtonRelease-1>', x=tab_x+25, y=110)
        self.app.update()
        self.assertTrue(self.workspace.detached(sketch))
        self.assertEqual(len(n.tabs()), 1)
        self.assertIsNone(self.app.grab_current())
        grip = self.workspace.entries[str(sketch)]['grip']
        grip.event_generate('<ButtonPress-1>', x=200, y=10)
        gx = n.winfo_rootx()+30-grip.winfo_rootx()
        gy = n.winfo_rooty()+15-grip.winfo_rooty()
        target = dict(rootx=n.winfo_rootx()+30, rooty=n.winfo_rooty()+15)
        grip.event_generate('<B1-Motion>', x=gx, y=gy, **target)
        grip.event_generate('<ButtonRelease-1>', x=gx, y=gy, **target)
        self.app.update()
        self.assertFalse(self.workspace.detached(sketch))
        self.assertEqual(len(n.tabs()), 2)
        self.assertEqual(n.select(), str(sketch))
        self.assertIsNone(self.app.grab_current())

    def test_form_only_changes_are_not_lost_on_close_or_new_element(self):
        sketch = Sketch(self.app)
        self.app.update()
        self.assertFalse(sketch.has_changes())
        sketch.fields['name'].set('Nie zapisuj bez potwierdzenia')
        self.assertEqual(sketch.history, [])
        self.assertTrue(sketch.has_changes())
        with patch('sketch.messagebox.askyesno', return_value=False) as question:
            self.assertFalse(sketch.cancel())
            self.assertFalse(sketch.new_element())
            self.assertEqual(question.call_count, 2)
        self.assertTrue(sketch.winfo_exists())
        with patch('sketch.messagebox.askyesno', return_value=True):
            self.assertTrue(sketch.new_element())
        self.assertFalse(sketch.has_changes())
        sketch.config['hinge_count'] = 4
        self.assertTrue(sketch.has_changes())

    def test_undo_to_original_state_is_clean(self):
        sketch = Sketch(self.app)
        self.app.update()
        sketch.add_point((25, 40))
        self.assertTrue(sketch.has_changes())
        sketch.undo()
        self.assertFalse(sketch.has_changes())
    def test_numeric_sketch_resize_preserves_exact_profile_and_undo(self):
        from core import Part
        from shaped_furniture import apply_shape
        from sketch_live import apply_sizes
        from sketch import normalized_part
        q=Part(u=600,v=300,t=28,material=self.app.project.materials[0].id)
        self.app.project.parts=[q]
        self.app.project=apply_shape(self.app.project,q.id,'rounded',radii=(20,30,40,50))
        q=self.app.project.parts[0];q.fillets=[dict(edge='top:0',radius=2)];q.validate()
        sketch=Sketch(self.app,q);self.app.update();original=sketch.points[:]
        sketch.size_vars['u'].set('800');sketch.size_vars['v'].set('400');apply_sizes(sketch)
        resized=normalized_part(sketch.part,sketch.points)
        self.assertEqual((resized.u,resized.v),(800,400))
        self.assertEqual(resized.profile['radii'],[20,30,40,50])
        self.assertTrue(resized.fillets)
        sketch.undo();self.assertEqual(sketch.points,original)
        self.assertEqual((sketch.part.u,sketch.part.v),(600,300))
        self.assertFalse(sketch.has_changes())

    def test_detached_main_view_has_application_shortcuts_once(self):
        calls = []
        entry = ttk.Entry(self.main)
        entry.pack()
        self.workspace._install_document_tag(self.main)
        self.workspace.bind_app_shortcut('<Control-s>', lambda: calls.append('save'))
        self.workspace.detach(self.main)
        self.app.update()
        entry.focus_force()
        self.app.update()
        entry.event_generate('<Control-s>')
        self.app.update()
        self.assertEqual(calls, ['save'])
        self.workspace.dock(self.main)
        self.app.update()
        entry.focus_force()
        entry.event_generate('<Control-s>')
        self.app.update()
        self.assertEqual(calls, ['save', 'save'])

    @unittest.skipUnless(sys.platform == 'win32', 'Native Windows title bar')
    def test_native_move_events_queue_redocking_and_clean_hooks(self):
        view = VisualizationDocument(self.app)
        self.app.update()
        self.workspace.detach(view)
        self.app.update()
        native, key = self.workspace.native, str(view)
        self.assertTrue(self.workspace.entries[key]['native_title_drag'])
        self.assertIn(key, native.hooks)
        hwnd = native.hooks[key]['hwnd']
        api = native.api
        api.NotifyWinEvent.argtypes = (native.w.DWORD, native.w.HWND, native.w.LONG, native.w.LONG)
        api.NotifyWinEvent.restype = None
        point = (self.app.tabs.winfo_rootx()+40, self.app.tabs.winfo_rooty()+15)
        old = native.rect(hwnd)
        with patch.object(native, 'cursor', return_value=point):
            self.app.after_cancel(self.workspace._poll_job)
            self.workspace._poll_job = None
            api.NotifyWinEvent(0x000A, hwnd, 0, 0)
            self.app.update()
            self.assertIsNotNone(native.hooks[key]['start'])
            native.move(view, old[0]+35, old[1]+50)
            self.app.update()
            api.NotifyWinEvent(0x000B, hwnd, 0, 0)
            self.app.update()
        # Native callback has only queued work. Tcl reparenting has not run yet.
        self.assertTrue(self.workspace.detached(view))
        self.assertTrue(native.pending)
        self.workspace._poll_native()
        self.app.update()
        self.assertFalse(self.workspace.detached(view))
        self.assertNotIn(key, native.hooks)
        self.assertEqual(self.app.tabs.select(), key)
        for _ in range(4):
            self.workspace.detach(view)
            self.app.update()
            self.workspace.dock(view)
            self.app.update()
        self.assertEqual(native.hooks, {})
        self.assertEqual(len(view._doc_after_jobs), 1)  # Only live preview timer.

    @unittest.skipUnless(sys.platform == 'win32', 'Native Windows sizing')
    def test_native_resize_does_not_dock_when_corner_reaches_tabs(self):
        view = VisualizationDocument(self.app)
        self.app.update()
        self.workspace.detach(view)
        self.app.update()
        native, key = self.workspace.native, str(view)
        hwnd, api = native.hooks[key]['hwnd'], native.api
        api.NotifyWinEvent.argtypes = (native.w.DWORD, native.w.HWND, native.w.LONG, native.w.LONG)
        api.NotifyWinEvent.restype = None
        old = native.rect(hwnd)
        point = (self.app.tabs.winfo_rootx()+40, self.app.tabs.winfo_rooty()+15)
        with patch.object(native, 'cursor', return_value=point):
            api.NotifyWinEvent(0x000A, hwnd, 0, 0)
            self.app.update()
            view.tk.call('wm', 'geometry', str(view), '%dx%d+%d+%d' % (view.winfo_width()-20,view.winfo_height()-20,old[0]+20,old[1]+20))
            self.app.update()
            api.NotifyWinEvent(0x000B, hwnd, 0, 0)
            self.app.update()
        self.assertEqual(native.pending, [])
        self.assertTrue(self.workspace.detached(view))

    @unittest.skipUnless(sys.platform == 'win32', 'Win32 monitor work area')
    def test_absolute_negative_window_coordinates_and_monitor_clamp(self):
        view = VisualizationDocument(self.app)
        self.app.update()
        self.workspace.detach(view)
        self.app.update()
        native = self.workspace.native
        native.move(view, -120, 110)
        self.app.update()
        self.assertEqual(native.rect(native.hwnd(view))[:2], (-120, 110))
        native.move(view, -99999, -99999, fit=True)
        self.app.update()
        r = native.rect(native.hwnd(view))
        work = native.workarea(-99999, -99999)
        self.assertGreaterEqual(r[0], work[0])
        self.assertGreaterEqual(r[1], work[1])
        self.assertLessEqual(r[2], work[2])
        self.assertLessEqual(r[3], work[3])


    def test_document_closure_and_permanent_view_native_close(self):
        view = VisualizationDocument(self.app)
        self.app.update()
        self.workspace.detach(self.main)
        self.app.update()
        self.workspace.close(self.main)
        self.app.update()
        self.assertFalse(self.workspace.detached(self.main))
        self.assertTrue(self.main.winfo_exists())
        self.workspace.detach(view)
        self.app.update()
        key = str(view)
        self.workspace.close(view)
        self.app.update()
        self.assertNotIn(key, self.workspace.entries)
        self.assertEqual(len(self.app.tabs.tabs()), 1)

    def _press_main_tab(self):
        n = self.app.tabs
        # Separate synthetic clicks in Tk's double-click clock without sleeps.
        self._mouse_time = getattr(self, '_mouse_time', 0) + 1000
        n.event_generate('<ButtonPress-1>', x=30, y=16, time=self._mouse_time)
        self.assertIsNotNone(self.workspace.drag)

    def test_detach_with_missing_mouse_release_does_not_block_window(self):
        # wm manage changes the Toplevel. A grab on Notebook must not remain
        # behind when the release is lost during that native transition.
        self._press_main_tab()
        self.workspace.detach(self.main)
        self.app.update()
        self.assertIsNone(self.app.grab_current())
        self.assertIsNone(self.workspace.drag)
        self.workspace.entries[str(self.main)]['toggle'].invoke()
        self.app.update()
        self.assertFalse(self.workspace.detached(self.main))

    def test_destroy_document_during_tab_press_releases_notebook_grab(self):
        self._press_main_tab()
        self.main.destroy()
        self.app.update()
        self.assertIsNone(self.app.grab_current())
        self.assertIsNone(self.workspace.drag)

    def test_plain_tab_press_does_not_make_other_windows_modal(self):
        self._press_main_tab()
        self.assertIsNone(self.app.grab_current())

    def _drag_main_out(self):
        self._press_main_tab()
        self.app.tabs.event_generate('<B1-Motion>', x=180, y=130)
        self.assertTrue(self.workspace.detached(self.main))
        grip = self.workspace.entries[str(self.main)]['grip']
        self.assertIs(self.app.grab_current(), grip)
        self.assertIs(grip.winfo_toplevel(), self.main)

    @unittest.skipUnless(sys.platform == 'win32', 'Native mouse release watchdog')
    def test_lost_release_watchdog_restores_button_clicks(self):
        counter = []
        button = ttk.Button(self.main, text='Działający przycisk', command=lambda: counter.append(True))
        button.pack()
        self.app.update()
        with patch.object(self.workspace.native, 'left_button_down', return_value=True):
            self._drag_main_out()
            self.app.update()
        self.app.after_cancel(self.workspace._poll_job)
        with patch.object(self.workspace.native, 'left_button_down', return_value=False):
            self.workspace._poll_native()
        self.app.update()
        self.assertIsNone(self.app.grab_current())
        self.assertIsNone(self.workspace.drag)
        button.event_generate('<ButtonPress-1>', x=8, y=8)
        button.event_generate('<ButtonRelease-1>', x=8, y=8)
        self.app.update()
        self.assertEqual(counter, [True])
        # Exercise the actual registered WM_DELETE_WINDOW callback, rather
        # than invoking workspace.close directly.
        callback = self.main.tk.call('wm', 'protocol', str(self.main), 'WM_DELETE_WINDOW')
        self.main.tk.call(callback)
        self.app.update()
        self.assertFalse(self.workspace.detached(self.main))

    def test_close_or_escape_during_drag_releases_capture(self):
        self._drag_main_out()
        self.workspace._cancel_drag()
        self.assertIsNone(self.app.grab_current())
        self.workspace.dock(self.main)
        self.app.update()
        self._drag_main_out()
        self.workspace.close(self.main)
        self.app.update()
        self.assertIsNone(self.app.grab_current())
        self.assertIsNone(self.workspace.drag)
        self.assertFalse(self.workspace.detached(self.main))

    def test_existing_modal_dialog_grab_is_not_stolen_or_released(self):
        dialog = tk.Toplevel(self.app)
        self.app.update()
        dialog.grab_set()
        event = Mock(widget=self.app.tabs, x_root=10, y_root=10)
        self.assertFalse(self.workspace._start(self.main, event))
        self.assertIsNone(self.workspace.drag)
        self.assertIs(self.app.grab_current(), dialog)
        self.workspace.detach(self.main)
        self.workspace.dock(self.main)
        self.assertIs(self.app.grab_current(), dialog)
        dialog.destroy()

    def test_additional_3d_close_during_drag_cleans_capture_and_timers(self):
        view = VisualizationDocument(self.app)
        self.app.update()
        self.workspace.detach(view)
        self.app.update()
        grip = self.workspace.entries[str(view)]['grip']
        grip.event_generate('<ButtonPress-1>', x=80, y=10)
        grip.event_generate('<B1-Motion>', x=105, y=10)
        self.assertIs(self.app.grab_current(), grip)
        timers = set(view._doc_after_jobs)
        self.workspace.close(view)
        self.app.update()
        self.assertIsNone(self.app.grab_current())
        self.assertIsNone(self.workspace.drag)
        self.assertNotIn(str(view), self.workspace.entries)
        if self.workspace.native:
            self.assertNotIn(str(view), self.workspace.native.hooks)
        pending = set(self.app.tk.splitlist(self.app.tk.call('after', 'info')))
        self.assertFalse(timers & pending)

    def test_repeated_detach_click_and_native_close_do_not_accumulate_grabs(self):
        errors = []
        self.app.report_callback_exception = lambda *args: errors.append(args)
        for _ in range(12):
            self._drag_main_out()
            # Mimic releasing the mouse in another application: no Tk
            # ButtonRelease arrives before a direct close/dock action.
            callback = self.main.tk.call('wm', 'protocol', str(self.main), 'WM_DELETE_WINDOW')
            self.main.tk.call(callback)
            self.app.update()
            self.assertFalse(self.workspace.detached(self.main))
            self.assertIsNone(self.app.grab_current())
            self.assertIsNone(self.workspace.drag)
        self.assertEqual(errors, [])
        if self.workspace.native:
            self.assertEqual(self.workspace.native.hooks, {})


class ProjectLifecycleTests(unittest.TestCase):
    def setUp(self):
        from app import App
        try:
            argv = ['MebelStudio3D.exe', 'discover'] if self._testMethodName == 'test_runner_arguments_are_not_loaded_as_projects' else ['MebelStudio3D.exe']
            with patch.object(sys, 'argv', argv):
                self.app = App()
        except tk.TclError as exc:
            self.skipTest(str(exc))
        self.errors = []
        self.app.report_callback_exception = lambda *args: self.errors.append(args)
        self.app.update()

    def tearDown(self):
        self.app.destroy()
        self.assertEqual(self.errors, [])

    def test_cancel_new_preserves_dirty_document_then_confirm_closes_it(self):
        sketch = Sketch(self.app)
        self.app.update()
        sketch.fields['t'].set('22')
        old = self.app.project
        with patch('dock_workspace.messagebox.askyesno', return_value=False):
            self.assertFalse(self.app.new())
        self.assertIs(self.app.project, old)
        self.assertTrue(sketch.winfo_exists())
        self.assertEqual(self.app.project_generation, 0)
        old_controls = (self.app.motion_value, self.app.motion_all, self.app.resize_step)
        self.app.workspace.detach(sketch)
        with patch('dock_workspace.messagebox.askyesno', return_value=True):
            self.assertTrue(self.app.new())
        self.assertEqual(self.app.project_generation, 1)
        self.assertEqual((self.app.motion_value, self.app.motion_all, self.app.resize_step), old_controls)
        self.assertFalse(sketch.winfo_exists())
        self.assertEqual(len(self.app.workspace.entries), 1)
        with patch('dock_workspace.messagebox.showerror') as error:
            self.assertFalse(sketch.save())
            error.assert_called_once()
        self.assertEqual(self.app.project.parts, [])

    def test_load_cancel_and_invalid_file_preserve_open_documents(self):
        sketch = Sketch(self.app)
        self.app.update()
        sketch.fields['name'].set('Niedokończony')
        old = self.app.project
        with patch('app.Project.load', return_value=Project()), patch('dock_workspace.messagebox.askyesno', return_value=False):
            self.assertFalse(self.app.load_path('fake.mebel'))
        self.assertIs(self.app.project, old)
        self.assertTrue(sketch.winfo_exists())
        with patch('app.Project.load', side_effect=ValueError('Uszkodzony plik')), patch('app.messagebox.showerror'):
            self.assertFalse(self.app.load_path('broken.mebel'))
        self.assertIs(self.app.project, old)
        self.assertTrue(sketch.winfo_exists())
        fresh = Project(name='Otwarty')
        with patch('app.Project.load', return_value=fresh), patch('dock_workspace.messagebox.askyesno', return_value=True):
            self.assertTrue(self.app.load_path('loaded.mebel'))
        self.assertIs(self.app.project, fresh)
        self.assertFalse(sketch.winfo_exists())
        self.assertEqual(self.app.project_generation, 1)

    def test_application_close_can_be_cancelled_for_form_only_draft(self):
        sketch = Sketch(self.app)
        self.app.update()
        sketch.hinge_side.set('Prawy')
        with patch('dock_workspace.messagebox.askyesno', return_value=False):
            self.assertFalse(self.app.close())
        self.assertTrue(self.app.winfo_exists())
        self.assertTrue(sketch.winfo_exists())

    def test_runner_arguments_are_not_loaded_as_projects(self):
        self.app.load_path = Mock()
        self.app.after(180, self.app.quit)
        self.app.mainloop()
        self.app.load_path.assert_not_called()



if __name__ == '__main__':
    unittest.main()
