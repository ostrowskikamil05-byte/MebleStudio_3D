"""Viewport styling never changes model geometry or requires a CAD renderer."""
import copy
import math
import unittest
from unittest.mock import patch

from core import Camera, Material, Part, Project, render_faces
from viewport_scene import (fit_scene, grain_segments, grid_spacing, paint_background,
                            paint_faces, paint_overlay, scene_bounds, scene_vertices)


class Canvas:
    def __init__(self):
        self.items = []

    def create(self, kind, *args, **kwargs):
        self.items.append((kind, args, kwargs))
        return len(self.items)

    def create_polygon(self, *args, **kwargs):
        return self.create('polygon', *args, **kwargs)

    def create_rectangle(self, *args, **kwargs):
        return self.create('rectangle', *args, **kwargs)

    def create_line(self, *args, **kwargs):
        return self.create('line', *args, **kwargs)

    def create_text(self, *args, **kwargs):
        return self.create('text', *args, **kwargs)


class ViewportTests(unittest.TestCase):
    def setUp(self):
        self.project = Project()
        self.part = Part(u=800, v=500, t=25, z=0, material=self.project.materials[0].id)
        self.project.parts = [self.part]
        self.camera = Camera()
        self.size = (1100, 750)

    def test_background_and_faces_are_readonly_and_retain_hittest_geometry(self):
        before = copy.deepcopy(self.project.data())
        fit_scene(self.camera, self.project, self.size)
        faces = render_faces(self.project, self.camera, *self.size, self.part.id)
        geometry = copy.deepcopy(faces)
        canvas = Canvas()
        scene = paint_background(canvas, self.camera, self.project, self.size)
        painted = paint_faces(canvas, faces, self.project, camera=self.camera, size=self.size)
        paint_overlay(canvas, self.camera, self.size, spacing=scene['spacing'])
        self.assertEqual(self.project.data(), before)
        self.assertEqual(faces, geometry)
        model = [item for item in canvas.items if item[2].get('tags', ('',))[0] == 'model']
        self.assertEqual(len(model), len(faces))
        for item, face in zip(model, faces):
            self.assertEqual(item[1], tuple(value for point in face['points'] for value in point))
            self.assertEqual(item[2]['width'], 2)
        self.assertGreater(painted['grain'], 0)

    def test_fit_includes_purchased_accessories_below_and_outside_panels(self):
        self.part.z = 100
        self.part.layout = dict(x=450, y=-100, z=0, yaw=90)
        # Purchased feet are not formatki. Their display geometry must still
        # fit, even when a room transform moves the panel assembly.
        accessory = [(self.part.id, [(100, -250, -80), (120, -250, -80),
                                    (120, -240, -60), (100, -240, -60)], '#808080',1)]
        with patch('fitting_visuals.fitting_faces', return_value=accessory):
            bounds = scene_bounds(self.project)
            self.assertEqual(bounds[0][2], -80)
            fit_scene(self.camera, self.project, self.size)
            for point in scene_vertices(self.project):
                x, y, _depth = self.camera.project(point, *self.size)
                self.assertTrue(49 <= x <= self.size[0] - 49)
                self.assertTrue(49 <= y <= self.size[1] - 49)
        self.assertEqual(self.part.layout['yaw'], 90)

    def test_hidden_panels_and_fittings_are_not_in_scene(self):
        self.part.visible = False
        self.assertIsNone(scene_bounds(self.project))
        fit_scene(self.camera, self.project, self.size)
        self.assertEqual(self.camera.center, (0, 0, 0))
        self.assertEqual(self.camera.scale, .4)

    def test_furniture_fit_keeps_scale_when_presentation_walls_are_enabled(self):
        from room_objects import add_object,set_scene
        from viewport_render import prepare_scene
        project=add_object(self.project,'washer',x=1000)
        fit_scene(self.camera,project,self.size)
        expected=(self.camera.center,self.camera.scale)
        project=set_scene(project,architecture=dict(enabled=True,width=9000,depth=8000,height=4000))
        fit_scene(self.camera,project,self.size)
        self.assertEqual((self.camera.center,self.camera.scale),expected)
        context=prepare_scene(project)
        self.assertTrue(context.architecture_vertices)
        self.assertTrue(context.architecture)
        self.assertTrue(any(f.get('architecture') for f in render_faces(project,self.camera,*self.size)))

    def test_grid_is_world_scaled_and_background_item_count_is_bounded(self):
        canvas = Canvas()
        self.camera.scale = .0001
        self.camera.pan = (100000, -100000)
        scene = paint_background(canvas, self.camera, self.project, self.size)
        self.assertLessEqual(scene['items'], 163)
        self.assertGreater(scene['spacing'], 100)
        self.assertAlmostEqual(grid_spacing(.4), 200)
        self.assertEqual(grid_spacing(0), 500000)
        self.assertTrue(all(math.isfinite(value) for _kind, args, _kwargs in canvas.items
                            for value in args))

    def test_grain_is_clipped_into_concave_profile_instead_of_across_notch(self):
        # A deep notch opening from the top. Horizontal strokes at y > 20
        # must split into two segments, one in each side of the U.
        points = [(0, 0), (100, 0), (100, 100), (75, 100),
                  (75, 20), (25, 20), (25, 100), (0, 100)]
        segments = grain_segments(points, (1, 0), count=5)
        self.assertTrue(segments)
        for a, b in segments:
            lo, hi = sorted((a[0], b[0]))
            if a[1] > 20:
                self.assertTrue(hi <= 25 or lo >= 75)
        self.assertEqual(grain_segments(points, (0, 0)), [])

    def test_no_grain_for_solid_color_meshes_or_purchased_hardware(self):
        fit_scene(self.camera, self.project, self.size)
        faces = render_faces(self.project, self.camera, *self.size)
        for face in faces:
            face['mesh'] = True
        self.assertEqual(paint_faces(Canvas(), faces, self.project)['grain'], 0)
        for face in faces:
            face['mesh'] = False
            face['fitting'] = True
        self.assertEqual(paint_faces(Canvas(), faces, self.project)['grain'], 0)
        for face in faces:
            face.pop('fitting')
        white = Material(name='Biel matowa', color='#f5f5f0', kind='mdf')
        self.project.materials = [white]
        self.part.material = white.id
        self.assertEqual(paint_faces(Canvas(), faces, self.project)['grain'], 0)

    def test_grain_budget_remains_bounded_with_many_large_faces(self):
        fit_scene(self.camera, self.project, self.size)
        faces = render_faces(self.project, self.camera, *self.size)
        faces *= 100
        scene = paint_faces(Canvas(), faces, self.project)
        self.assertLessEqual(scene['grain'], 120)

    def test_front_elevation_avoids_dense_edge_on_grid_and_tiny_canvas_is_safe(self):
        self.camera.pitch = 0
        canvas = Canvas()
        paint_background(canvas, self.camera, self.project, self.size)
        self.assertFalse(any(item[2].get('tags') == 'viewport.grid' for item in canvas.items))
        canvas = Canvas()
        self.assertEqual(paint_background(canvas, self.camera, self.project, (0, 0))['items'], 0)
        paint_overlay(canvas, self.camera, (0, 0))
        self.assertEqual(canvas.items, [])

    def test_xray_fronts_keep_mesh_geometry_and_fittings_remain_opaque(self):
        self.part.role = 'door'
        fit_scene(self.camera, self.project, self.size)
        faces = render_faces(self.project, self.camera, *self.size, self.part.id)
        for face in faces:
            face['mesh'] = True
        fitting = copy.deepcopy(faces[0])
        fitting.update(fitting=True, color='#aab0b6', mesh=False)
        faces.append(fitting)
        before = copy.deepcopy(faces)
        canvas = Canvas()
        paint_faces(canvas, faces, self.project, camera=self.camera, size=self.size, xray=True)
        polygons = [item for item in canvas.items if item[0] == 'polygon']
        # Ghost triangles remain available to picking, but create no invisible
        # Canvas items. Only the opaque bought hinge and one front contour paint.
        self.assertEqual(len(polygons), 1)
        self.assertNotIn('stipple', polygons[-1][2])
        self.assertEqual(polygons[-1][2]['fill'], '#aab0b6')
        self.assertEqual(faces, before)
        self.assertTrue(any(item[2].get('tags', ('',))[0] == 'viewport.ghost_outline'
                            for item in canvas.items))


if __name__ == '__main__':
    unittest.main()
