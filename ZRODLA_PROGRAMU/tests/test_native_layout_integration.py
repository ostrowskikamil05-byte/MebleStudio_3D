"""Native layout transaction boundaries, batching and unchanged editor contracts."""
import copy
import inspect
import unittest
from unittest.mock import patch

from core import Project, Part
import native_model
import furniture_layout as layout
import native_layout


@unittest.skipUnless(native_model.available(), 'C++ layout library is required')
class NativeLayoutIntegrationTests(unittest.TestCase):
    def boxes(self):
        project = Project()
        project.parts = [Part(group='A', u=600, v=400, t=800, material=project.materials[0].id),
                         Part(group='B', u=600, v=400, t=800, x=616, y=8, z=5,
                              material=project.materials[0].id)]
        return project

    def test_nearest_uses_one_scene_call_one_accessory_pass_and_no_python_geometry_loop(self):
        import fitting_visuals
        project = self.boxes()
        before = copy.deepcopy(project.data())
        calls = []
        bridge = native_model.call

        def traced(module, function, *args, **kwargs):
            if module == 'layout':
                calls.append(function)
            return bridge(module, function, *args, **kwargs)

        with (patch('native_model.call', side_effect=traced),
              patch('fitting_visuals.fitting_faces', wraps=fitting_visuals.fitting_faces) as accessories,
              patch('furniture_layout._bounds', side_effect=AssertionError('legacy bounds loop executed')),
              patch('furniture_layout._flat_faces', side_effect=AssertionError('legacy face loop executed')),
              patch('furniture_layout._check_collision', side_effect=AssertionError('legacy collision loop executed')),
              patch('workspace_worktops.polygons_overlap', side_effect=AssertionError('legacy overlap loop executed'))):
            candidate, information = layout.nearest_snap(project, 'B', 25, validate=False)
        self.assertEqual(calls, ['nearest_snap'])
        self.assertEqual(accessories.call_count, 1)
        self.assertIsNotNone(information)
        self.assertIsInstance(information['delta'], tuple)
        self.assertEqual(list(information), ['group', 'target_group', 'direction', 'delta', 'distance'])
        self.assertEqual(list(candidate.parts[1].layout), ['x', 'y', 'z', 'yaw'])
        self.assertEqual(list(layout.contacts(candidate)[0]), ['group', 'target_group', 'direction', 'gap'])
        self.assertEqual(layout.group_bounds(candidate, 'B')[0], (600, 0, 0))
        self.assertEqual(project.data(), before)

    def test_preview_skips_exact_cad_while_publication_rejects_invalid_solid_atomically(self):
        project = self.boxes()
        project.parts[0].fillets = [dict(edge='top:0', radius=10000)]
        before = copy.deepcopy(project.data())
        with patch('cad_geometry.local_shape', side_effect=AssertionError('CAD invoked during preview')):
            candidate = layout.move_group(project, 'A', 10, validate=False)
        self.assertEqual(layout.group_layout(candidate, 'A')['x'], 10)
        self.assertEqual(candidate.parts[0].fillets, project.parts[0].fillets)
        with self.assertRaises(ValueError):
            layout.move_group(project, 'A', 10)
        self.assertEqual(project.data(), before)

    def test_linked_worktop_refresh_retains_previous_top_when_support_moves_away(self):
        from furniture_configurator import configure, default_config
        from workspace_worktops import install_shared_worktop, read_shared_worktop
        project = Project()
        config = default_config(0, project)
        config.update(name='A', width=600, top_mode='rails', origin=[0, 0, 0])
        project, first = configure(project, config)
        config = default_config(0, project)
        config.update(name='B', width=600, top_mode='rails', origin=[600, 0, 0])
        project, second = configure(project, config)
        project, top_group = install_shared_worktop(project, [first, second])
        top = next(part for part in project.parts if part.group == top_group)
        shape = (top.u, top.v, top.t, top.contour[:])
        before = copy.deepcopy(project.data())
        moved = layout.move_group(project, second, 200, validate=False)
        retained = next(part for part in moved.parts if part.id == top.id)
        self.assertEqual((retained.u, retained.v, retained.t, retained.contour), shape)
        self.assertEqual(read_shared_worktop(moved, top_group)['status'], 'needs_fit')
        self.assertEqual(project.data(), before)

    def test_rotated_accessory_envelopes_keep_world_local_tuple_contract(self):
        from configurator_accessories import accessory_vertices
        from furniture_configurator import configure, default_config
        from core import world, local_world
        project = Project()
        config = default_config(0, project)
        config.update(name='Żółta szafka', leg_count=4)
        project, group = configure(project, config)
        project = layout.move_group(project, group, 700, -250, 80)
        project = layout.rotate_group(project, group, 35)
        for function, transform, local in ((layout.group_bounds, world, False),
                                           (layout.local_group_bounds, local_world, True)):
            vertices = [transform(part, u, v, t) for part in project.parts if part.group == group
                        for u, v in part.points() for t in (0, part.t)]
            vertices.extend(accessory_vertices(project, group, local=local))
            expected = tuple(tuple(aggregate(point[axis] for point in vertices) for axis in range(3))
                             for aggregate in (min, max))
            actual = function(project, group)
            self.assertIsInstance(actual, tuple)
            self.assertTrue(all(isinstance(point, tuple) for point in actual))
            for first, second in zip(actual, expected):
                for a, b in zip(first, second):
                    self.assertAlmostEqual(a, b, places=7)

    def test_all_public_ports_preserve_signatures_and_are_explicitly_native(self):
        for name in native_layout.OPERATIONS:
            function = getattr(layout, name)
            self.assertTrue(function._native_cpp, name)
            self.assertTrue(function._native_batch_layout, name)
            self.assertEqual(inspect.signature(function), inspect.signature(function.__wrapped__))

    def test_float_convertible_numeric_arguments_remain_accepted(self):
        from decimal import Decimal
        from fractions import Fraction
        project = self.boxes()
        moved = layout.move_group(project, 'A', Decimal('12.5'), Fraction(3, 2), False, validate=False)
        self.assertEqual(layout.group_layout(moved, 'A')['x'], 12.5)
        self.assertEqual(layout.group_layout(moved, 'A')['y'], 1.5)
        rotated = layout.rotate_group(project, 'A', Fraction(90), (Decimal(0), Fraction(0), 0))
        self.assertEqual(layout.group_layout(rotated, 'A')['yaw'], 90)
        snapped = layout.snap_group(project, 'B', 'A', gap=Decimal('10.5'))
        self.assertEqual(layout.group_bounds(snapped, 'B')[0][0], 610.5)
        unused_candidate, info = layout.nearest_snap(project, 'B', Fraction(25), validate=False)
        self.assertIsNotNone(info)
        self.assertIsInstance(layout.contacts(project, tolerance=Decimal('.1')), list)

    def test_invalid_pivot_conversion_follows_group_and_angle_validation(self):
        project = self.boxes()
        with self.assertRaisesRegex(ValueError, 'istniejący mebel'):
            layout.rotate_group(project, 'missing', 90, pivot=1)
        with self.assertRaisesRegex(ValueError, 'Wartość musi mieścić'):
            layout.rotate_group(project, 'A', float('nan'), pivot=1)
        with self.assertRaises(TypeError):
            layout.rotate_group(project, 'A', 90, pivot=1)
        with self.assertRaisesRegex(ValueError, 'Punkt obrotu'):
            layout.rotate_group(project, 'A', 90, pivot=(0, 0))


class NativeLayoutAvailabilityTests(unittest.TestCase):
    def test_missing_source_library_preserves_functions_and_wrong_packaged_api_fails_clearly(self):
        namespace = {name: object() for name in native_layout.OPERATIONS}
        original = namespace.copy()
        with patch('native_model.available', return_value=False), patch('native_model.call') as bridge:
            native_layout.install(namespace)
            bridge.assert_not_called()
        self.assertEqual(namespace, original)
        with patch('native_model.available', return_value=True), patch('native_model.call', return_value={}):
            with self.assertRaisesRegex(RuntimeError, 'Etap 2'):
                native_layout.install(namespace)
        self.assertEqual(namespace, original)


if __name__ == '__main__':
    unittest.main()
