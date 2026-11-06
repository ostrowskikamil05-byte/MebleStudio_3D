import copy
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from core import Project, Part, local_world
from cad_geometry import local_shape
from fitting_visuals import fitting_faces
from configurator_accessories import (apply_front_style, install_accessories,
                                     accessory_vertices, normalize_front_style,
                                     normalize_accessories)


def front_project():
    project = Project()
    project.parts = [Part(name='Front', group='A', plane='XZ', u=600, v=720,
                          t=18, role='door', material=project.materials[0].id)]
    return project, project.parts[0].id


def cabinet():
    project = Project()
    mat = project.materials[0].id
    project.parts = [
        Part(name='Bok lewy', group='A', role='left', plane='YZ', u=500, v=700, t=18,
             x=0, y=0, z=100, material=mat),
        Part(name='Bok prawy', group='A', role='right', plane='YZ', u=500, v=700, t=18,
             x=582, y=0, z=100, material=mat),
        Part(name='Dno', group='A', role='bottom', plane='XY', u=564, v=500, t=18,
             x=18, y=0, z=100, material=mat),
        Part(name='Wieniec', group='A', role='top', plane='XY', u=564, v=500, t=18,
             x=18, y=0, z=782, material=mat),
        Part(name='Półka', group='A', role='shelf', plane='XY', u=564, v=470, t=18,
             x=18, y=8, z=440, material=mat)]
    return project


class FrontMillingTests(unittest.TestCase):
    def test_rounded_frame_is_real_solid_with_exact_removed_volume(self):
        base, part_id = front_project()
        before = base.data()
        result = apply_front_style(base, part_id, 'frame')
        part = result.parts[0]
        shape = local_shape(part)
        pocket_area = 480 * 600 - (4 - math.pi) * 6 ** 2
        self.assertTrue(shape.isValid())
        self.assertEqual(len(shape.Solids()), 1)
        self.assertAlmostEqual(shape.Volume(), 600 * 720 * 18 - pocket_area * 3, places=4)
        self.assertEqual(len(result.parts), 1)
        self.assertEqual(base.data(), before)
        self.assertTrue(all(c['face'] == 'bottom' for c in part.machining))

    def test_flutes_are_separate_exact_slots_and_requested_count(self):
        base, part_id = front_project()
        result = apply_front_style(base, part_id, 'fluted', {'flute_count': 12, 'flute_spacing': 24})
        part = result.parts[0]
        self.assertEqual(len(part.machining), 12)
        groove_area = (600 - 8) * 8 + math.pi * 4 ** 2
        self.assertAlmostEqual(local_shape(part).Volume(), 600 * 720 * 18 - 12 * groove_area * 3, places=4)
        us = [c['u'] for c in part.machining]
        self.assertEqual(us, sorted(us))
        self.assertAlmostEqual(us[1] - us[0], 24)

    def test_style_replacement_preserves_manual_milling_and_drilling(self):
        base, part_id = front_project()
        manual = dict(type='pocket', shape='circle', face='top', u=20, v=20,
                      length=6, width=6, depth=2, purpose='Ręczna kieszeń')
        base.parts[0].machining = [manual]
        base.parts[0].operations = [dict(type='drill', face='top', u=35, v=35, diameter=5, depth=4)]
        result = apply_front_style(base, part_id, 'frame')
        result = apply_front_style(result, part_id, 'smooth')
        self.assertEqual(result.parts[0].machining, [manual])
        self.assertEqual(result.parts[0].operations, base.parts[0].operations)

    def test_small_drawer_limits_border_and_depth_without_invalid_default(self):
        base, part_id = front_project()
        base.parts[0].v = 80
        base.parts[0].role = 'drawer'
        result = apply_front_style(base, part_id, 'frame', {'front_border': 60, 'front_depth': 30})
        info = result.parts[0].configurator_source['front_style']
        self.assertEqual(info['front_border'], 20)
        self.assertAlmostEqual(info['front_depth'], 8.1)
        self.assertTrue(local_shape(result.parts[0]).isValid())

    def test_finger_is_machining_not_a_bought_handle_and_cleans_up(self):
        base, part_id = front_project()
        base.hardware.append(dict(kind='handle', owner_id=part_id, name='Poprzedni uchwyt', qty=1, price=20))
        result = apply_front_style(base, part_id, 'finger')
        self.assertEqual([h['kind'] for h in result.hardware], ['opening'])
        self.assertEqual(result.hardware[0]['qty'], 0)
        self.assertEqual(len(result.parts[0].machining), 1)
        self.assertLess(local_shape(result.parts[0]).Volume(), 600 * 720 * 18)
        result = apply_front_style(result, part_id, 'smooth')
        self.assertEqual(result.hardware, [])
        self.assertEqual(result.parts[0].machining, [])

    def test_impossible_count_or_thin_board_fails_transactionally(self):
        base, part_id = front_project()
        before = copy.deepcopy(base.data())
        with self.assertRaises(ValueError):
            apply_front_style(base, part_id, 'fluted', {'flute_count': 32, 'flute_spacing': 100})
        self.assertEqual(base.data(), before)
        base.parts[0].t = 6
        with self.assertRaises(ValueError):
            apply_front_style(base, part_id, 'finger')
        with self.assertRaises(ValueError):
            normalize_front_style('fluted', {'flute_count': 1.5})

    def test_deep_recess_keeps_web_under_opposite_handle_drilling(self):
        base, part_id = front_project()
        base.parts[0].operations = [dict(type='drill', face='top', u=300, v=360, diameter=5, depth=10)]
        result = apply_front_style(base, part_id, 'frame', {'front_depth': 30})
        part = result.parts[0]
        self.assertEqual(part.configurator_source['front_style']['front_depth'], 7)
        self.assertTrue(all(c['depth'] == 7 for c in part.machining))
        self.assertEqual(part.operations, base.parts[0].operations)


class CabinetAccessoryTests(unittest.TestCase):
    def test_four_or_six_legs_do_not_change_cutting_parts_and_qty_cost(self):
        base = cabinet()
        before = base.data()
        result = install_accessories(base, 'A', {'leg_height': 100, 'leg_count': 6, 'leg_price': 12})
        h = result.hardware[0]
        self.assertEqual(h['kind'], 'leg')
        self.assertEqual(h['qty'], 6)
        self.assertEqual(len(h['positions']), 6)
        self.assertEqual(len(set((p['x'], p['y']) for p in h['positions'])), 6)
        self.assertEqual(len(result.parts), len(base.parts))
        self.assertEqual(result.costs()['hardware'], 72)
        vertices = accessory_vertices(result)
        self.assertAlmostEqual(min(v[2] for v in vertices), 0)
        self.assertAlmostEqual(max(v[2] for v in vertices), 100)
        self.assertEqual(base.data(), before)
        result = install_accessories(result, 'A', {'leg_height': 100, 'leg_count': 4})
        self.assertEqual(len(result.hardware), 1)
        self.assertEqual(result.hardware[0]['qty'], 4)

    def test_room_transform_and_visibility_apply_to_leg_envelope(self):
        base = cabinet()
        for p in base.parts:
            p.layout = dict(x=1200, y=-400, z=25, yaw=90)
        result = install_accessories(base, 'A', {'leg_height': 100})
        local = accessory_vertices(result, 'A', local=True)
        room = accessory_vertices(result, 'A')
        self.assertAlmostEqual(min(v[2] for v in room), 25)
        self.assertAlmostEqual(min(v[0] for v in room), 1200 - max(v[1] for v in local))
        self.assertAlmostEqual(min(v[1] for v in room), -400 + min(v[0] for v in local))
        self.assertEqual(accessory_vertices(result, 'Elsewhere'), [])
        owner = next(p for p in result.parts if p.id == result.hardware[0]['owner_id'])
        owner.visible = False
        self.assertEqual(accessory_vertices(result), [])

    def test_support_envelope_default_does_not_add_existing_handle_projection(self):
        base, part_id = front_project()
        base.hardware = [dict(kind='handle', owner_id=part_id, name='Uchwyt', qty=1, price=20,
                             u=300, v=360, spacing=128, vertical=False, projection=50)]
        self.assertEqual(accessory_vertices(base), [])
        self.assertTrue(accessory_vertices(base, include_all=True))

    def test_accessory_reapply_is_deterministic_and_preserves_manual_hardware(self):
        base = cabinet()
        base.hardware.append(dict(kind='custom', owner_id=base.parts[0].id,
                                  name='Ręczne akcesorium', qty=2, price=5))
        cfg = dict(leg_height=100, led=True, led_source='24 V własny')
        once = install_accessories(base, 'A', cfg)
        twice = install_accessories(once, 'A', cfg)
        self.assertEqual(once.data(), twice.data())
        cleared = install_accessories(twice, 'A', {})
        self.assertEqual(cleared.hardware, base.hardware)
        self.assertTrue(all(not p.machining for p in cleared.parts))

    def test_led_groove_is_real_and_lengths_bom_and_cost_match(self):
        base = cabinet()
        result = install_accessories(base, 'A', dict(led=True, led_price_per_m=20,
            led_profile_price_per_m=10, led_diffuser_price_per_m=5, led_source='24 V 30 W', led_source_price=40))
        strip = next(h for h in result.hardware if h['kind'] == 'led_strip')
        p = next(p for p in result.parts if p.id == strip['owner_id'])
        self.assertEqual(p.role, 'shelf')
        self.assertEqual(strip['length'], 504)
        self.assertAlmostEqual(strip['qty'], .504)
        self.assertEqual(p.machining[0]['face'], 'bottom')
        self.assertLess(local_shape(p).Volume(), p.u * p.v * p.t)
        self.assertAlmostEqual(result.costs()['hardware'], .504 * 35 + 40)
        self.assertTrue(any(color == '#fff3bf' for owner, face, color in fitting_faces(result)))
        self.assertEqual(len(result.parts), len(base.parts))

    def test_led_side_uses_inner_face_and_world_geometry_matches_recess(self):
        result = install_accessories(cabinet(), 'A', dict(led=True, led_mount='side', led_side='left'))
        strip = next(h for h in result.hardware if h['kind'] == 'led_strip')
        p = next(p for p in result.parts if p.id == strip['owner_id'])
        self.assertEqual(p.role, 'left')
        self.assertEqual(p.machining[0]['face'], 'top')
        self.assertEqual(p.machining[0]['angle'], 90)
        vertices = accessory_vertices(result, local=True)
        self.assertAlmostEqual(max(v[0] for v in vertices), p.x + p.t + .3)
        self.assertTrue(local_shape(p).isValid())

    def test_led_target_manual_milling_preserved_and_no_partial_failure(self):
        base = cabinet()
        shelf = next(p for p in base.parts if p.role == 'shelf')
        manual = dict(type='pocket', shape='circle', face='top', u=10, v=10, length=4, width=4, depth=2)
        shelf.machining = [manual]
        result = install_accessories(base, 'A', dict(led=True, led_targets=[shelf.id]))
        self.assertIn(manual, next(p for p in result.parts if p.id == shelf.id).machining)
        before = base.data()
        with self.assertRaises(ValueError):
            install_accessories(base, 'A', {'leg_height': 100}, bounds={'lower': (0,0,100), 'upper': (600,500,800), 'floor': 50})
        with self.assertRaises(ValueError):
            install_accessories(base, 'A', dict(led=True, led_targets=['missing']))
        self.assertEqual(base.data(), before)
        with self.assertRaises(ValueError):
            normalize_accessories({'leg_count': 5})


if __name__ == '__main__':
    unittest.main()
