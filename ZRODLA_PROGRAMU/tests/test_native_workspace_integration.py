"""The native view must retain settings, secondary documents and model state.

No Tk window or graphics context is created here. The renderer boundary is
controlled so the tests exercise switching/failure behavior on every machine.
"""
import copy
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from core import Camera, Part, Project
from dock_workspace import VisualizationDocument
from responsive_ui import UILayout
from ui_preferences import DEFAULTS, ENGINES, ENGINE_LABELS, load_preferences, save_preferences


class Value:
    def __init__(self,value):self.value=value
    def get(self):return self.value
    def set(self,value):self.value=value


class Canvas:
    def __init__(self):self.commands=[];self._active_renderer='cpu'
    def delete(self,*args):self.commands=[]
    def winfo_width(self):return 900
    def winfo_height(self):return 600
    def create_line(self,*args,**kwargs):self.commands.append(('line',args,kwargs))
    def create_text(self,*args,**kwargs):self.commands.append(('text',args,kwargs))
    def create_polygon(self,*args,**kwargs):self.commands.append(('polygon',args,kwargs))


def document(engine='d3d11',quality='balanced',dimensions=True):
    project=Project();project.parts=[Part(u=600,v=400,t=18,material=project.materials[0].id)]
    part=project.parts[0];camera=Camera();camera.fit(project,900,600)
    app=SimpleNamespace(viewport_engine=engine,viewport_quality=quality,selected=part.id,
                        xray_scope=Value('Cały zestaw'),log=Mock())
    view=SimpleNamespace(app=app,canvas=Canvas(),camera=camera,need_fit=False,selected=part.id,
        snapshot_project=project,project=lambda:project,explode=Value(False),xray=Value(True),
        dimensions=Value(dimensions),_interactive=False)
    view.draw=lambda **kwargs:VisualizationDocument.draw(view,**kwargs)
    return view,project


class NativeSettingsTests(unittest.TestCase):
    def test_all_engines_roundtrip_and_old_preferences_are_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'ui.json'
            self.assertEqual(set(ENGINE_LABELS.values()),set(ENGINES))
            for engine in ENGINES:
                with self.subTest(engine=engine):
                    prefs=dict(DEFAULTS,engine=engine,quality='high',ui_scale=1.2,inspector_width=405)
                    save_preferences(prefs,path)
                    self.assertEqual(load_preferences(path),prefs)
                    self.assertEqual(load_preferences(path)['schema'],1)

    def test_preference_apply_updates_documents_without_resetting_their_cameras(self):
        project={'geometry':['unchanged']};calls=[]
        camera=Camera();camera.yaw=73;camera.pan=(40,50)
        secondary=SimpleNamespace(camera=camera,viewport_preferences_changed=lambda:calls.append('secondary'))
        sketch=SimpleNamespace(viewport_preferences_changed=lambda:calls.append('sketch'))
        app=SimpleNamespace(project=project,workspace=SimpleNamespace(entries={
            'secondary':{'page':secondary},'sketch':{'page':sketch},'production':{'page':SimpleNamespace()}}),
            apply_viewport_preferences=lambda prefs:calls.append(prefs['engine']))
        controller=SimpleNamespace(app=app,suspended=False,prefs=None,set_scale=lambda value:None,
            show_inspector=lambda *a,**k:None,show_ribbon=lambda *a,**k:None,
            show_toolbars=lambda *a,**k:None,schedule=lambda:None,schedule_save=lambda:None)
        UILayout.apply(controller,dict(DEFAULTS,engine='vulkan'),geometry=False)
        self.assertEqual(calls,['vulkan','secondary','sketch'])
        self.assertIs(secondary.camera,camera)
        self.assertEqual((camera.yaw,camera.pan),(73,(40,50)))
        self.assertEqual(project,{'geometry':['unchanged']})


class SecondaryViewTests(unittest.TestCase):
    def native_result(self,canvas,project,**kwargs):
        canvas._active_renderer='native'
        return dict(ok=True,engine='native',drawn=['retained_geometry'])

    def test_all_native_engines_keep_dimensions_and_same_project_camera(self):
        for backend in ('auto','d3d11','d3d12','vulkan'):
            with self.subTest(backend=backend):
                view,project=document(backend);before=copy.deepcopy(project.data());camera=view.camera
                flushed=[]
                with patch('viewport_scene.paint_background'),patch('core.render_faces') as raster,\
                    patch('native_viewport.paint_native',side_effect=self.native_result) as native,\
                    patch('native_viewport.flush_native',side_effect=lambda c:flushed.append(tuple(c.commands))),\
                    patch('gpu_viewport.close_gpu'):
                    view.draw()
                self.assertIs(native.call_args.args[1],project)
                self.assertIs(native.call_args.kwargs['camera'],camera)
                self.assertEqual(native.call_args.kwargs['backend'],backend)
                self.assertEqual(native.call_args.kwargs['xray_ids'],{project.parts[0].id})
                self.assertTrue(any(command[0]=='text' and command[2].get('tags')=='dimensions' for command in flushed[0]))
                self.assertEqual(view.drawn,['retained_geometry']);raster.assert_not_called()
                self.assertEqual(project.data(),before)

    def test_native_failure_keeps_cpu_geometry_dimensions_and_source_model(self):
        view,project=document('vulkan');before=project.data()
        with patch('viewport_scene.paint_background'),\
            patch('native_viewport.paint_native',return_value=dict(ok=False,error='missing driver')),\
            patch('native_viewport.close_native') as close,\
            patch('gpu_viewport.close_gpu'),\
            patch('core.render_faces',return_value=['cpu_geometry']) as raster,\
            patch('viewport_scene.paint_faces') as paint:
            view.draw()
        close.assert_called();raster.assert_called_once();paint.assert_called_once()
        self.assertEqual(view.canvas._active_renderer,'cpu')
        self.assertEqual(view.drawn,['cpu_geometry'])
        self.assertTrue(view.canvas.commands);self.assertEqual(project.data(),before)

    def test_failed_overlay_redraws_cpu_without_changing_selected_engine(self):
        view,project=document('d3d12');before=project.data()
        with patch('viewport_scene.paint_background'),\
            patch('native_viewport.paint_native',side_effect=self.native_result),\
            patch('native_viewport.flush_native',side_effect=RuntimeError('overlay failure')),\
            patch('native_viewport.close_native'),patch('gpu_viewport.close_gpu'),\
            patch('core.render_faces',return_value=['cpu_geometry']),patch('viewport_scene.paint_faces'):
            view.draw()
        self.assertEqual(view.canvas._active_renderer,'cpu')
        self.assertEqual(view.app.viewport_engine,'d3d12');self.assertEqual(project.data(),before)

    def test_opengl_high_quality_remains_selectable_in_secondary_view(self):
        view,project=document('gpu','high');before=project.data()
        with patch('viewport_scene.paint_background'),patch('native_viewport.close_native') as close,\
            patch('native_viewport.paint_native') as native,\
            patch('gpu_viewport.paint_gpu',return_value=dict(ok=True,engine='vtk-opengl',drawn=['opengl_geometry'])) as gpu,\
            patch('core.render_faces') as raster:
            view.draw()
        close.assert_called_once();native.assert_not_called();raster.assert_not_called()
        self.assertEqual(gpu.call_args.kwargs['quality'],'high')
        self.assertEqual(view.canvas._active_renderer,'gpu');self.assertEqual(project.data(),before)

    def test_auto_tries_opengl_after_unavailable_native_renderer(self):
        view,_=document('auto',dimensions=False)
        with patch('viewport_scene.paint_background'),\
            patch('native_viewport.paint_native',return_value=dict(ok=False,error='native unavailable')),\
            patch('gpu_viewport.paint_gpu',return_value=dict(ok=True,engine='vtk-opengl',drawn=['opengl_geometry'])) as gpu:
            view.draw()
        gpu.assert_called_once();self.assertEqual(view.canvas._active_renderer,'gpu')


class SketchPreviewTests(unittest.TestCase):
    def sketch(self):
        view,project=document('d3d11',dimensions=False)
        return SimpleNamespace(app=view.app,live_canvas=view.canvas,closed=True,
            live_note=Value(''),preview_job='pending',candidate=lambda:(project,project.parts[0])),project

    def test_validated_candidate_native_preview_leaves_original_parts_unchanged(self):
        from sketch_live import preview
        sketch,project=self.sketch();before=project.data()
        def native(canvas,project,**kwargs):
            canvas._active_renderer='native'
            return dict(ok=True,engine='native')
        with patch('native_viewport.paint_native',side_effect=native) as render,\
            patch('native_viewport.flush_native') as flush,patch('gpu_viewport.close_gpu'):
            preview(sketch)
        self.assertIs(render.call_args.args[1],project)
        self.assertEqual(render.call_args.kwargs['selected'],project.parts[0].id)
        flush.assert_called_once_with(sketch.live_canvas)
        self.assertIsNone(sketch.preview_job);self.assertIn('600 × 400 × 18 mm',sketch.live_note.get())
        self.assertEqual(project.data(),before)

    def test_open_contour_hides_previous_native_and_opengl_preview(self):
        from sketch_live import preview
        sketch,project=self.sketch();sketch.closed=False
        with patch('native_viewport.close_native') as native,patch('gpu_viewport.close_gpu') as gpu:
            preview(sketch)
        native.assert_called_once_with(sketch.live_canvas);gpu.assert_called_once_with(sketch.live_canvas)
        self.assertIn('zamknij wielokąt',sketch.live_note.get())

    def test_invalid_candidate_hides_old_native_geometry_and_shows_editing_error(self):
        from sketch_live import preview
        sketch,project=self.sketch();sketch.candidate=Mock(side_effect=ValueError('Invalid dimensions'))
        with patch('native_viewport.close_native') as native,patch('gpu_viewport.close_gpu') as gpu:
            preview(sketch)
        native.assert_called_once_with(sketch.live_canvas);gpu.assert_called_once_with(sketch.live_canvas)
        self.assertEqual(sketch.live_note.get(),'Invalid dimensions')
        self.assertTrue(any(command[0]=='text' for command in sketch.live_canvas.commands))


if __name__=='__main__':unittest.main()
