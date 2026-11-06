import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from ui_preferences import DEFAULTS, load_preferences, save_preferences, validate_preferences
from responsive_ui import flow_layout, fitted_geometry, clamp_inspector


class PreferencesTests(unittest.TestCase):
    def test_local_roundtrip_and_atomic_failure_preserve_previous_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'nested'/'ui.json'
            prefs=dict(DEFAULTS,ui_scale=1.25,inspector_width=405,engine='cpu')
            save_preferences(prefs,path);before=path.read_bytes()
            self.assertEqual(load_preferences(path),prefs)
            with patch('ui_preferences.os.replace',side_effect=OSError('locked')):
                with self.assertRaises(OSError):save_preferences(dict(prefs,quality='high'),path)
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual(list(path.parent.glob('*.tmp')),[])

    def test_corrupt_future_and_oversized_preferences_fall_back(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'ui.json'
            for raw in ('{bad',json.dumps(dict(DEFAULTS,schema=99)),json.dumps(dict(DEFAULTS,ui_scale=float('nan'))),' '*70000):
                path.write_text(raw,encoding='utf-8');self.assertEqual(load_preferences(path),DEFAULTS)

    def test_validation_rejects_bool_numbers_unknown_modes_and_outside_ranges(self):
        for key,value in [('window_width',True),('window_width',899),('window_height',599),('ui_scale',2.1),
                          ('show_inspector',1),('engine','magical'),('ribbon_height',63),('window_x',1.5)]:
            with self.subTest(key=key,value=value):
                with self.assertRaises(ValueError):validate_preferences(dict(DEFAULTS,**{key:value}))
        self.assertEqual(validate_preferences({'schema':1,'extra':'ignored'}),DEFAULTS)

    def test_geometry_presets_fit_workarea_with_taskbar_and_negative_monitor(self):
        for size in ((900,600),(1280,720),(1366,768),(1920,1080)):
            work=(-1920,0,0,1040)
            p=dict(DEFAULTS,window_mode='custom',window_width=size[0],window_height=size[1],window_x=4000,window_y=3000)
            w,h,x,y=fitted_geometry(p,work)
            self.assertGreaterEqual(x,work[0]);self.assertGreaterEqual(y,work[1])
            self.assertLessEqual(x+w+16,work[2]);self.assertLessEqual(y+h+40,work[3])
        w,h,x,y=fitted_geometry(DEFAULTS,(0,0,900,600))
        self.assertEqual((w,h),(884,560))

    def test_wrapping_keeps_each_action_and_inspector_preserves_viewport(self):
        sizes=[(210,35),(180,35),(190,35),(80,28),(270,40)]
        for width in (300,550,900,1300):
            placements,height=flow_layout(width,sizes)
            self.assertEqual(len(placements),len(sizes))
            for x,y,w,h in placements:
                self.assertLessEqual(x+w,width);self.assertLessEqual(y+h,height)
            self.assertGreaterEqual(height,40)
        for total in (884,1264,1350,1904):
            width=clamp_inspector(1100,total,1.5)
            self.assertGreaterEqual(width,240);self.assertGreaterEqual(total-width-10,400)


if __name__=='__main__':unittest.main()
