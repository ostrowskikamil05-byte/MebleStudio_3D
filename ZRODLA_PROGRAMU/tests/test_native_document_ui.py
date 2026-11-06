"""Real editor transaction boundaries backed by the C++ document implementation."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from core import Project, Part
import native_model
from native_document import NativeHistoryView, serialize


@unittest.skipUnless(native_model.available(), 'C++ document library is required')
class NativeDocumentEditorTests(unittest.TestCase):
    def setUp(self):
        from app import App
        self.app = App()
        self.errors = []
        self.app.report_callback_exception = lambda *args: self.errors.append(args)
        self.app.update()
        project = Project()
        project.parts = [Part(material=project.materials[0].id)]
        self.reset(project)

    def reset(self, project):
        self.app._native_document.reset(project)
        self.app.project = project
        self.app.selected = project.parts[0].id if project.parts else None
        self.app._native_pending = False
        self.app.refresh()
        self.app.layout_magnet.set(False)

    def tearDown(self):
        self.app.destroy()
        self.assertEqual(self.errors, [])

    @property
    def document(self):
        return self.app._native_document

    def test_legacy_in_place_scene_edit_is_one_native_history_step(self):
        before = serialize(self.app.project)
        revision = self.document.revision
        self.assertIsInstance(self.app.undo_stack, NativeHistoryView)
        self.app.snapshot()
        self.assertEqual(self.document.revision, revision)
        self.app.project.scene['brightness'] = .35
        self.app.draw()
        self.assertEqual(len(self.app.undo_stack), 1)
        self.assertTrue(self.app.dirty)
        self.assertEqual(json.loads(self.document.read())['scene']['brightness'], .35)
        self.app.undo()
        self.assertEqual(serialize(self.app.project), before)
        self.assertFalse(self.app.dirty)
        self.assertEqual(len(self.app.redo_stack), 1)
        self.app.redo()
        self.assertEqual(self.app.project.scene['brightness'], .35)
        self.assertTrue(self.app.dirty)

    def test_explicit_commit_preserves_projection_identity_and_branches_history(self):
        candidate = copy.deepcopy(self.app.project)
        candidate.labor = 30
        self.assertIs(self.app.commit_project(candidate), candidate)
        self.assertIs(self.app.project, candidate)
        self.app.undo()
        branch = copy.deepcopy(self.app.project)
        branch.markup = 45
        self.app.commit_project(branch)
        self.assertFalse(self.app.redo_stack)
        self.assertEqual(self.document.info()['undo_count'], 1)

    def test_late_invalid_cad_restores_projection_without_changing_history(self):
        before = serialize(self.app.project)
        info = self.document.info()
        self.app.snapshot()
        self.app.project.parts[0].fillets = [dict(edge='top:0', radius=10000)]
        with self.assertRaises(ValueError):
            self.app.draw()
        self.assertEqual(serialize(self.app.project), before)
        after = self.document.info()
        for key in ('revision', 'undo_count', 'redo_count'):
            self.assertEqual(after[key], info[key])
        self.assertFalse(self.app._native_pending)
        self.assertFalse(self.app.dirty)

    def test_explicit_invalid_candidate_does_not_replace_held_projection(self):
        original = self.app.project
        candidate = copy.deepcopy(original)
        candidate.parts[0].material = 'missing material'
        with self.assertRaises(ValueError):
            self.app.commit_project(candidate)
        self.assertIs(self.app.project, original)
        self.assertFalse(self.app.dirty)
        self.assertFalse(self.app.undo_stack)

    def test_stale_revision_rejects_edit_and_recovers_authoritative_state(self):
        external = copy.deepcopy(self.app.project)
        external.labor = 22
        result = native_model.call('document', 'commit', self.document.handle,
                                   self.document.revision, serialize(external), True)
        self.app.snapshot()
        self.app.project.labor = 44
        with self.assertRaises(ValueError):
            self.app.draw()
        self.assertEqual(self.app.project.labor, 22)
        self.assertEqual(self.document.info()['revision'], result['revision'])
        self.assertEqual(self.document.info()['undo_count'], result['undo_count'])
        self.assertEqual(self.document.info()['redo_count'], result['redo_count'])
        self.assertFalse(self.app._native_pending)
        self.assertTrue(self.app.dirty)

    def test_camera_draws_do_not_repeat_document_validation_or_commit(self):
        info = self.document.info()
        with patch.object(self.app.project, 'validate', side_effect=AssertionError('unchanged document validated')):
            with patch.object(self.document, 'commit', side_effect=AssertionError('unchanged document committed')):
                for _ in range(3):
                    self.app.camera.yaw += 10
                    self.app.draw()
        self.assertEqual(self.document.info()['revision'], info['revision'])

    def begin_resize(self):
        base = copy.deepcopy(self.app.project)
        self.app.start = (0, 0)
        self.app.drag_edit = dict(base=base, candidate=None, id=base.parts[0].id, last=0,
                                  handle=dict(face='u1', vector=(1, 0)))

    def test_resize_preview_cancel_and_release_use_one_commit_only(self):
        before = serialize(self.app.project)
        revision = self.document.revision
        self.begin_resize()
        self.app.direct_drag(SimpleNamespace(x=40, y=0))
        self.assertEqual(self.document.revision, revision)
        self.assertNotEqual(serialize(self.app.project), before)
        self.assertTrue(self.app.cancel_active_edits())
        self.assertEqual(serialize(self.app.project), before)
        self.assertEqual(self.document.revision, revision)
        self.begin_resize()
        self.app.direct_drag(SimpleNamespace(x=40, y=0))
        self.app.mouse_up(SimpleNamespace(x=60, y=0))
        self.assertEqual(self.document.revision, revision + 1)
        self.assertEqual(len(self.app.undo_stack), 1)
        self.assertEqual(self.app.project.parts[0].u, 660)
        self.app.undo()
        self.assertEqual(serialize(self.app.project), before)

    def begin_layout(self):
        self.app.layout_drag = dict(base=copy.deepcopy(self.app.project), group=self.app.project.parts[0].group,
                                    start=(0, 0), candidate=None, last=0)

    def test_layout_preview_cancel_and_release_use_one_commit_only(self):
        from furniture_layout_ui import drag, cancel_drag, end_drag
        before = serialize(self.app.project)
        revision = self.document.revision
        event = SimpleNamespace(x=40, y=20, state=0)
        self.begin_layout()
        drag(self.app, event, force=True)
        self.assertEqual(self.document.revision, revision)
        cancel_drag(self.app)
        self.assertEqual(serialize(self.app.project), before)
        self.assertEqual(self.document.revision, revision)
        self.begin_layout()
        drag(self.app, event, force=True)
        end_drag(self.app, event)
        self.assertEqual(self.document.revision, revision + 1)
        self.assertEqual(len(self.app.undo_stack), 1)
        self.app.undo()
        self.assertEqual(serialize(self.app.project), before)

    def test_room_object_preview_cancel_and_release_use_one_commit_only(self):
        from room_objects import add_object
        from room_scene_drag import begin, move, cancel, end
        self.reset(add_object(self.app.project, 'washer', x=900))
        before = serialize(self.app.project)
        revision = self.document.revision
        identity = 'scene:' + self.app.project.scene['objects'][0]['id']
        origin = SimpleNamespace(x=0, y=0, state=0)
        target = SimpleNamespace(x=40, y=20, state=0)
        begin(self.app, origin, identity)
        move(self.app, target, force=True)
        self.assertEqual(self.document.revision, revision)
        cancel(self.app)
        self.assertEqual(serialize(self.app.project), before)
        self.assertEqual(self.document.revision, revision)
        begin(self.app, origin, identity)
        move(self.app, target, force=True)
        end(self.app, target)
        self.assertEqual(self.document.revision, revision + 1)
        self.assertEqual(len(self.app.undo_stack), 1)
        self.app.undo()
        self.assertEqual(serialize(self.app.project), before)

    def test_saved_name_dirty_undo_and_failed_save_preserve_state(self):
        candidate = copy.deepcopy(self.app.project)
        candidate.labor = 20
        self.app.commit_project(candidate)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Żółć — projekt.mebel'
            with patch('app.filedialog.asksaveasfilename', return_value=str(path)):
                self.assertTrue(self.app.save(True))
            self.assertEqual(self.app.project.name, path.stem)
            self.assertFalse(self.app.dirty)
            self.assertEqual(json.loads(path.read_text(encoding='utf-8')), json.loads(self.document.read()))
            self.app.undo()
            self.assertTrue(self.app.dirty)
            self.app.redo()
            self.assertFalse(self.app.dirty)
            before = serialize(self.app.project)
            info = self.document.info()
            saved_path = self.app.path
            with patch('app.filedialog.asksaveasfilename', return_value=directory), patch('app.messagebox.showerror'):
                self.assertFalse(self.app.save(True))
            self.assertEqual(serialize(self.app.project), before)
            self.assertEqual(self.app.path, saved_path)
            self.assertFalse(self.app.dirty)
            self.assertEqual(self.document.info()['revision'], info['revision'])

    def test_stale_save_cannot_touch_disk_and_recovers_native_projection(self):
        external = copy.deepcopy(self.app.project)
        external.labor = 25
        result = native_model.call('document', 'commit', self.document.handle,
                                   self.document.revision, serialize(external), True)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Istniejący projekt.mebel'
            path.write_bytes(b'preserve existing file')
            with patch('app.filedialog.asksaveasfilename', return_value=str(path)), patch('app.messagebox.showerror'):
                self.assertFalse(self.app.save(True))
            self.assertEqual(path.read_bytes(), b'preserve existing file')
        self.assertEqual(self.app.project.labor, 25)
        self.assertEqual(self.document.info()['revision'], result['revision'])
        self.assertTrue(self.app.dirty)
        self.assertIsNone(self.app.path)

    def test_save_as_after_undo_keeps_redo_name_and_geometry(self):
        candidate = copy.deepcopy(self.app.project)
        candidate.parts[0].u = 650
        self.app.commit_project(candidate)
        expected = serialize(self.app.project)
        original_name = self.app.project.name
        self.app.undo()
        self.assertEqual(len(self.app.redo_stack), 1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Nowa nazwa po cofnięciu.mebel'
            with patch('app.filedialog.asksaveasfilename', return_value=str(path)):
                self.assertTrue(self.app.save(True))
            self.assertEqual(self.app.project.name, path.stem)
            self.assertFalse(self.app.dirty)
            self.assertEqual(len(self.app.redo_stack), 1)
            self.app.redo()
            self.assertEqual(self.app.project.name, original_name)
            self.assertEqual(self.app.project.parts[0].u, 650)
            self.assertEqual(serialize(self.app.project), expected)

    def test_new_and_load_replace_native_handle_clear_history_and_keep_generation_guard(self):
        candidate = copy.deepcopy(self.app.project)
        candidate.labor = 20
        self.app.commit_project(candidate)
        old_handle = self.document.handle
        with patch.object(self.app, 'confirm_project_transition', return_value=True):
            self.assertTrue(self.app.new())
        self.assertNotEqual(self.document.handle, old_handle)
        with self.assertRaises(ValueError):
            native_model.call('document', 'info', old_handle)
        self.assertFalse(self.app.undo_stack)
        self.assertFalse(self.app.redo_stack)
        self.assertEqual(self.app.project_generation, 1)
        fresh = Project(name='Otwarty')
        with patch('app.Project.load', return_value=fresh), patch.object(self.app, 'confirm_project_transition', return_value=True):
            self.assertTrue(self.app.load_path('loaded.mebel'))
        self.assertIs(self.app.project, fresh)
        self.assertEqual(self.app.project_generation, 2)
        self.assertFalse(self.app.dirty)


if __name__ == '__main__':
    unittest.main()
