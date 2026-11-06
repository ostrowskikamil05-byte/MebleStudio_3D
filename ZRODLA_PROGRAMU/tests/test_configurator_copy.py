"""Copying a configured furniture group must preserve independent editability."""
import copy
import tempfile
import unittest
from pathlib import Path

from core import Project, world
from configurator_accessories import accessory_vertices
from furniture_configurator import (configuration_status, configure, default_config,
                                    read_config)
from furniture_layout import group_layout, move_group, rotate_group
from productivity import extract_group, insert_template


def cabinet(complex=False):
    project = Project()
    config = default_config(0, project)
    config.update(name='Oryginał', width=1200, height=1800, depth=600,
                  origin=[200, 80, 25])
    if complex:
        config.update(front_style='frame', led=True, plinth=True,
                      runner_profile='blum_movento_760h', runner_length=400)
        by_key = {s['key']: s for s in config['sections']}
        by_key['bottom'].update(enabled=True, height=450, shelves=0, front='drawers', drawers=2)
        by_key['main'].update(columns=2, shelves=1, front='doors', doors=2)
        by_key['upper'].update(enabled=True, height=300, shelves=1, front='open')
    return configure(project, config)


class ConfiguratorCopyTests(unittest.TestCase):
    def test_copy_bays_children_and_manifest_refer_only_to_new_group(self):
        original, group = cabinet(complex=True)
        before = copy.deepcopy(original.data())
        result = insert_template(original, extract_group(original, group), 'Kopia', (1800, 0, 0))
        members = [p for p in result.parts if p.group == 'Kopia']
        ids = {p.id for p in members}
        self.assertFalse(ids & {p.id for p in original.parts})
        self.assertTrue(configuration_status(result, 'Kopia')['editable'])
        self.assertEqual(read_config(result, 'Kopia')['name'], 'Kopia')
        for p in members:
            self.assertTrue(set(p.assembly.get('anchors', [])) <= ids)
            self.assertTrue(set(p.assembly.get('children', [])) <= ids)
            if p.assembly.get('generated_by'):
                self.assertIn(p.assembly['generated_by'], ids)
            bay = p.assembly.get('config', {}).get('bay')
            if bay:
                self.assertTrue({bay[k] for k in ('left', 'right', 'top', 'bottom')} <= ids)
            for entry in p.configurator_source.get('manifest', {}).values():
                self.assertIn(entry['id'], ids)
            for op in p.operations + p.machining:
                if op.get('owner_id'):
                    self.assertIn(op['owner_id'], ids)
                if op.get('opening_owner'):
                    self.assertIn(op['opening_owner'], ids)
        self.assertEqual(original.data(), before)

    def test_resize_and_change_cloned_configuration_leave_original_unchanged(self):
        original, group = cabinet(complex=True)
        result = insert_template(original, extract_group(original, group), 'Kopia', (1800, 0, 0))
        before = extract_group(result, group).data()
        cfg = read_config(result, 'Kopia')
        cfg.update(width=1400, height=2000, front_style='smooth')
        resized, _ = configure(result, cfg, 'Kopia')
        self.assertEqual(extract_group(resized, group).data(), before)
        self.assertTrue(configuration_status(resized, 'Kopia')['editable'])
        self.assertEqual(read_config(resized, 'Kopia')['width'], 1400)
        self.assertEqual(read_config(resized, 'Kopia')['height'], 2000)
        resized.validate()
        source = next(p for p in resized.parts if p.group == 'Kopia' and p.role == 'left')
        self.assertEqual(source.v, 1900)  # Height includes the 100 mm feet.
        drawers = [p for p in resized.parts if p.group == 'Kopia' and p.role == 'drawer']
        self.assertEqual(len(drawers), 2)
        for drawer in drawers:
            runner = next(h for h in resized.hardware if h.get('owner_id') == drawer.id and h.get('kind') == 'runner')
            self.assertEqual(runner['runner_profile'], 'blum_movento_760h')

    def test_copy_placed_rotated_group_keeps_world_translation_and_accessories(self):
        original, group = cabinet()
        original = rotate_group(move_group(original, group, 450, -200, 30), group, 90)
        offset = (1700, 230, 40)
        result = insert_template(original, extract_group(original, group), 'Kopia', offset)
        old_by_key = {p.configurator_source.get('key'): p for p in original.parts}
        for p in result.parts:
            if p.group != 'Kopia':
                continue
            source = old_by_key[p.configurator_source['key']]
            for a, b, delta in zip(world(source, source.u / 2, source.v / 2, source.t),
                                   world(p, p.u / 2, p.v / 2, p.t), offset):
                self.assertAlmostEqual(b - a, delta, places=6)
        a = accessory_vertices(original, group)
        b = accessory_vertices(result, 'Kopia')
        self.assertEqual(len(a), len(b))
        for source, clone in zip(a, b):
            for old, new, delta in zip(source, clone, offset):
                self.assertAlmostEqual(new - old, delta, places=6)
        cfg = read_config(result, 'Kopia');cfg['width'] += 100
        resized, _ = configure(result, cfg, 'Kopia')
        self.assertEqual(group_layout(resized, 'Kopia')['yaw'], 90)
        self.assertEqual(group_layout(resized, 'Kopia'), group_layout(result, 'Kopia'))

    def test_hardware_group_names_are_remapped_for_independent_delete(self):
        original, group = cabinet(complex=True)
        result = insert_template(original, extract_group(original, group), 'Kopia', (1800, 0, 0))
        ids = {p.id for p in result.parts if p.group == 'Kopia'}
        hardware = [h for h in result.hardware if h.get('owner_id') in ids]
        self.assertTrue(any(h.get('kind') == 'leg' for h in hardware))
        self.assertTrue(any(h.get('kind') == 'led_strip' for h in hardware))
        self.assertTrue(all(h.get('group', 'Kopia') == 'Kopia' for h in hardware))
        # Same predicate used by the inspector when deleting the original.
        original_ids = {p.id for p in result.parts if p.group == group}
        retained = [h for h in result.hardware if h.get('owner_id') not in original_ids and h.get('group') != group]
        self.assertEqual(len(retained), len(hardware))

    def test_import_maps_material_ids_and_survives_save_load_then_rebuild(self):
        original, group = cabinet()
        destination = Project()
        result = insert_template(destination, extract_group(original, group), 'Import', (900, -150, 0))
        cfg = read_config(result, 'Import')
        self.assertNotEqual(cfg['material'], read_config(original, group)['material'])
        self.assertIn(cfg['material'], {m.id for m in result.materials})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'kopia.mebel'
            result.save(path)
            restored = Project.load(path)
        self.assertTrue(configuration_status(restored, 'Import')['editable'])
        cfg = read_config(restored, 'Import');cfg['depth'] = 650
        resized, _ = configure(restored, cfg, 'Import')
        self.assertEqual(read_config(resized, 'Import')['depth'], 650)
        self.assertTrue(configuration_status(resized, 'Import')['editable'])


if __name__ == '__main__':
    unittest.main()
