"""Local presets preserve useful cabinet configuration and existing libraries."""
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from configurator_presets import delete_preset, load_presets, preset_path, save_preset
from core import Project
from furniture_configurator import configure, default_config


class PresetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.path = Path(self.temporary.name) / 'folder' / 'presety.json'
        self.project = Project()
        self.config = default_config(0, self.project)
        self.config.update(name='Kuchnia Łódź', width=1100, leg_height=120)
        self.config['_material_names'] = {m.id: m.name for m in self.project.materials}

    def tearDown(self):
        self.temporary.cleanup()

    def test_unicode_nested_settings_roundtrip_and_generate_same_configuration(self):
        before = copy.deepcopy(self.config)
        save_preset('  Kuchnia — dąb  ', self.config, self.path)
        loaded = load_presets(self.path)
        self.assertEqual(loaded['Kuchnia — dąb'], self.config)
        self.assertEqual(self.config, before)
        loaded['Kuchnia — dąb']['sections'][0]['drawers'] = 8
        self.assertEqual(load_presets(self.path)['Kuchnia — dąb'], before)
        result, group = configure(self.project, load_presets(self.path)['Kuchnia — dąb'])
        self.assertTrue(any(p.group == group and p.role == 'left' for p in result.parts))
        self.assertTrue(any(h.get('kind') == 'leg' and h['height'] == 120 for h in result.hardware))

    def test_overwrite_one_preset_keeps_other_and_delete_is_selective(self):
        save_preset('A', self.config, self.path)
        second = copy.deepcopy(self.config);second['width'] = 800
        save_preset('B', second, self.path)
        first = copy.deepcopy(self.config);first['depth'] = 650
        save_preset('A', first, self.path)
        self.assertEqual(load_presets(self.path), {'A': first, 'B': second})
        self.assertFalse(delete_preset('Brak', self.path))
        self.assertTrue(delete_preset('A', self.path))
        self.assertEqual(load_presets(self.path), {'B': second})

    def test_invalid_names_or_failed_replace_do_not_damage_existing_library(self):
        save_preset('Poprawny', self.config, self.path)
        before = self.path.read_bytes()
        for name in ('', '   ', 'x' * 81):
            with self.assertRaises(ValueError):
                save_preset(name, self.config, self.path)
            self.assertEqual(self.path.read_bytes(), before)
        with patch.object(Path, 'replace', side_effect=OSError('Brak dostępu')):
            with self.assertRaises(OSError):
                save_preset('Drugi', self.config, self.path)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(load_presets(self.path), {'Poprawny': self.config})

    def test_invalid_json_structure_is_reported_before_ui_load(self):
        self.path.parent.mkdir(parents=True)
        for data in ({'schema': 2, 'presets': {}}, {'schema': 1, 'presets': []},
                     {'schema': 1, 'presets': {'Uszkodzony': 'tekst'}},
                     {'schema': 1, 'presets': {'Uszkodzony': {'_material_names': []}}}):
            with self.subTest(data=data):
                self.path.write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(ValueError):
                    load_presets(self.path)
        self.path.write_text('{niedokończony', encoding='utf-8')
        with self.assertRaises(ValueError):
            load_presets(self.path)

    def test_missing_and_oversized_library_and_local_appdata_location(self):
        self.assertEqual(load_presets(self.path), {})
        with patch.dict('os.environ', {'LOCALAPPDATA': self.temporary.name}):
            self.assertEqual(preset_path(), Path(self.temporary.name) / 'MebelStudio3D' / 'kreator_presety.json')
        self.path.parent.mkdir(parents=True)
        self.path.write_bytes(b' ' * 2_000_001)
        with self.assertRaises(ValueError):
            load_presets(self.path)


if __name__ == '__main__':
    unittest.main()
