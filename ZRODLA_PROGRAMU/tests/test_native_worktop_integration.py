"""Real shared-worktop lifecycle around the C++ geometry solver."""
import copy
import inspect
import json
import unittest
from unittest.mock import patch

from core import Project, polygon_area
from furniture_configurator import configure, default_config, read_config
from furniture_layout import move_group
import native_model
import native_worktops
import workspace_worktops as worktops


@unittest.skipUnless(native_model.available(), 'C++ worktop library is required')
class NativeWorktopIntegrationTests(unittest.TestCase):
    def pair(self):
        project = Project()
        names = []
        for name, x in (('A', 0), ('B', 600)):
            config = default_config(0, project)
            config.update(name=name, width=600, top_mode='rails', origin=[x, 0, 0])
            project, group = configure(project, config)
            names.append(group)
        return project, names

    def top(self, project, group):
        return next(part for part in project.parts if part.group == group)

    def test_install_extracts_each_body_once_and_executes_one_native_solver(self):
        project, names = self.pair()
        before = copy.deepcopy(project.data())
        bridge, original_body = native_model.call, worktops._body
        calls = []

        def traced(module, function, *args, **kwargs):
            if module == 'workspace_worktop':
                calls.append(function)
            return bridge(module, function, *args, **kwargs)

        with (patch('native_model.call', side_effect=traced),
              patch('workspace_worktops._body', wraps=original_body) as bodies,
              patch('workspace_worktops._distance', side_effect=AssertionError('Python distance loop executed')),
              patch('workspace_worktops._offset', side_effect=AssertionError('Python offset loop executed')),
              patch('workspace_worktops._union', side_effect=AssertionError('Python union loop executed'))):
            result, group = worktops.install_shared_worktop(project, names)
        self.assertEqual(calls, ['geometry'])
        self.assertEqual(bodies.call_count, len(names))
        self.assertEqual(project.data(), before)
        top = self.top(result, group)
        self.assertEqual((top.x, top.y, top.z, top.u, top.v, top.t), (-20, -20, 820, 1240, 600, 38))
        self.assertIsInstance(top.contour, list)
        self.assertTrue(all(isinstance(point, tuple) for point in top.contour))

    def test_refit_preserves_ids_anchors_manual_cad_features_and_metadata(self):
        project, names = self.pair()
        project, group = worktops.install_shared_worktop(project, names, overhang=0)
        top = self.top(project, group)
        top.holes = [[100, 100, 4, 5]]
        top.operations = [dict(type='drill', face='top', u=200, v=100, diameter=5, depth=6, purpose='Ręczne wiercenie')]
        top.machining = [dict(type='pocket', shape='rect', face='top', u=400, v=300,
                              length=20, width=20, depth=3, purpose='Ręczna kieszeń')]
        top.configurator_source['custom_note'] = dict(label='Żółć — dane użytkownika', nested=[1, 2.0, True])
        top.assembly['custom_data'] = dict(order=['b', 'a'])
        project.hardware.append(dict(name='Ręczne okucie blatu', qty=1, price=123, owner_id=top.id,
                                     custom=dict(article='ABC-Ż')))
        project.validate()
        before = copy.deepcopy(project.data())
        preserved = {key: copy.deepcopy(getattr(top, key)) for key in
                     ('id', 'holes', 'operations', 'machining', 'material', 'configurator_source')}
        old_anchors = worktops.read_shared_worktop(project, group)['anchors']
        config = read_config(project, names[1])
        config.update(width=700, name='B po zmianie')
        result, unused_name = configure(project, config, names[1])
        fitted = next(part for part in result.parts if part.id == top.id)
        self.assertEqual(fitted.u, 1300)
        for key, value in preserved.items():
            self.assertEqual(getattr(fitted, key), value, key)
        self.assertEqual(fitted.assembly['custom_data'], top.assembly['custom_data'])
        self.assertEqual(worktops.read_shared_worktop(result, group)['anchors'], old_anchors)
        self.assertEqual(worktops.read_shared_worktop(result, group)['status'], 'ready')
        self.assertEqual(next(item for item in result.hardware if item['name'] == 'Ręczne okucie blatu'),
                         next(item for item in project.hardware if item['name'] == 'Ręczne okucie blatu'))
        self.assertEqual(project.data(), before)

    def test_failed_refit_keeps_last_valid_top_and_manual_features(self):
        project, names = self.pair()
        project, group = worktops.install_shared_worktop(project, names, overhang=0)
        top = self.top(project, group)
        top.holes = [[1150, 100, 4, 5]]
        project.validate()
        geometry = (top.u, top.v, top.t, top.x, top.y, top.z, copy.deepcopy(top.contour))
        config = read_config(project, names[1])
        config['width'] = 300
        result, unused_name = configure(project, config, names[1])
        retained = next(part for part in result.parts if part.id == top.id)
        self.assertEqual((retained.u, retained.v, retained.t, retained.x, retained.y, retained.z, retained.contour), geometry)
        self.assertEqual(retained.holes, top.holes)
        self.assertEqual(worktops.read_shared_worktop(result, group)['status'], 'needs_fit')
        before = copy.deepcopy(result.data())
        with self.assertRaises(ValueError):
            worktops.fit_shared_worktop(result, group)
        self.assertEqual(result.data(), before)

    def test_manual_geometry_guard_runs_before_the_native_solver(self):
        project, names = self.pair()
        project, group = worktops.install_shared_worktop(project, names)
        top = self.top(project, group)
        top.x += 1
        before = copy.deepcopy(project.data())
        bridge = native_model.call

        def forbid_solver(module, function, *args, **kwargs):
            if module == 'workspace_worktop':
                raise AssertionError('Manually changed top reached geometry solver')
            return bridge(module, function, *args, **kwargs)

        with patch('native_model.call', side_effect=forbid_solver):
            result = worktops.refresh_shared_worktops(project, validate=False)
            with self.assertRaisesRegex(ValueError, 'ręcznie'):
                worktops.fit_shared_worktop(project, group)
        self.assertEqual(self.top(result, group).x, top.x)
        self.assertEqual(worktops.read_shared_worktop(result, group)['status'], 'needs_fit')
        self.assertEqual(project.data(), before)

    def test_corner_bodies_keep_true_contour_and_top_height(self):
        from corner_furniture import create_corner, default_corner_config
        for shape in ('L', 'chamfer', 'rounded'):
            with self.subTest(shape=shape):
                project = Project()
                config = default_corner_config(project)
                config.update(shape=shape, side='right', top_mode='rails', origin=[1200, 700, 50])
                project, support = create_corner(project, config)
                polygon, height, anchor = worktops._body(project, support)
                result, group = worktops.install_shared_worktop(project, [support], overhang=0)
                top = self.top(result, group)
                self.assertAlmostEqual(polygon_area(top.contour), polygon_area(polygon), places=4)
                self.assertEqual(top.z, height)
                self.assertEqual(worktops.read_shared_worktop(result, group)['anchors'], [anchor])

    def test_helper_types_numeric_arguments_and_zero_offset_shallow_copy(self):
        from decimal import Decimal
        from fractions import Fraction
        polygon = [[Decimal(0), Decimal(0)], [Decimal(10), Decimal(0)],
                   [Decimal(10), Decimal(10)], [Decimal(0), Decimal(10)]]
        with patch('native_model.call', side_effect=AssertionError('zero offset crossed native bridge')):
            copied = worktops._offset(polygon, Decimal(0))
        self.assertIsNot(copied, polygon)
        self.assertTrue(all(first is second for first, second in zip(copied, polygon)))
        a = [(Fraction(x), Fraction(y)) for x, y in ((0, 0), (10, 0), (10, 10), (0, 10))]
        b = [(Fraction(x), Fraction(y)) for x, y in ((10, 0), (20, 0), (20, 10), (10, 10))]
        self.assertEqual(worktops._offset(a, Fraction(1, 2)), [(-.5, -.5), (10.5, -.5), (10.5, 10.5), (-.5, 10.5)])
        self.assertEqual(worktops._union(poly for poly in (a, b)), [(0., 0.), (20., 0.), (20., 10.), (0., 10.)])
        touching = worktops._distance(a, b)
        self.assertEqual(touching, 0.0)
        self.assertIsInstance(touching, float)
        self.assertIs(type(worktops._distance([(1, 1), (2, 1), (2, 2), (1, 2)],
                                             [(0, 0), (10, 0), (10, 10), (0, 10)])), int)

    def test_native_zero_edge_keeps_zero_division_exception(self):
        with self.assertRaises(ZeroDivisionError):
            worktops._offset([(0, 0), (0, 0), (10, 0)], 1)

    def test_empty_inputs_use_interpreter_builtin_errors_without_solver(self):
        polygon = [(0, 0), (10, 0), (10, 10), (0, 10)]
        cases = ((worktops._distance, ([], polygon)), (worktops._distance, (polygon, [])),
                 (worktops._geometry, (Project(), [], {})))
        for function, arguments in cases:
            with self.subTest(function=function.__name__, arguments=arguments):
                with self.assertRaises(ValueError) as expected:
                    function.__wrapped__(*arguments)
                with patch('native_model.call', side_effect=AssertionError('empty input reached solver')):
                    with self.assertRaises(type(expected.exception)) as actual:
                        function(*arguments)
                self.assertEqual(str(actual.exception), str(expected.exception))

    def test_original_callable_signatures_and_fallbacks_remain_available(self):
        for name in native_worktops.OPERATIONS:
            function = getattr(worktops, name)
            self.assertTrue(function._native_cpp, name)
            self.assertTrue(function._native_worktop_solver, name)
            self.assertEqual(inspect.signature(function), inspect.signature(function.__wrapped__))


@unittest.skipUnless(native_model.available(), 'C++ document/worktop libraries are required')
class NativeWorktopDocumentTests(unittest.TestCase):
    def test_needs_fit_and_anchors_roundtrip_through_native_undo_redo(self):
        from app import App
        project, names = NativeWorktopIntegrationTests.pair(self)
        project, group = worktops.install_shared_worktop(project, names, overhang=0)
        before = copy.deepcopy(project.data())
        app = App()
        try:
            app._native_document.reset(project)
            app.project = project
            app.selected = project.parts[0].id
            app.refresh()
            app.commit_project(move_group(project, names[1], 300, validate=False))
            self.assertTrue(app.dirty)
            self.assertEqual(len(app.undo_stack), 1)
            self.assertEqual(worktops.read_shared_worktop(app.project, group)['status'], 'needs_fit')
            native = json.loads(app._native_document.read())
            native_top = next(part for part in native['parts'] if part['group'] == group)
            self.assertEqual(native_top['assembly']['shared_worktop']['anchors'],
                             worktops.read_shared_worktop(project, group)['anchors'])
            app.undo()
            self.assertEqual(app.project.data(), before)
            self.assertFalse(app.dirty)
            app.redo()
            self.assertTrue(app.dirty)
            self.assertEqual(worktops.read_shared_worktop(app.project, group)['status'], 'needs_fit')
        finally:
            app.destroy()


class NativeWorktopAvailabilityTests(unittest.TestCase):
    def test_source_fallback_and_missing_native_capability_are_explicit(self):
        namespace = {name: object() for name in native_worktops.OPERATIONS}
        before = namespace.copy()
        with patch('native_model.available', return_value=False), patch('native_model.call') as bridge:
            native_worktops.install(namespace)
            bridge.assert_not_called()
        self.assertEqual(namespace, before)
        with patch('native_model.available', return_value=True), patch('native_model.call', return_value={}):
            with self.assertRaisesRegex(RuntimeError, 'Etap 3'):
                native_worktops.install(namespace)
        self.assertEqual(namespace, before)


if __name__ == '__main__':
    unittest.main()
