"""Diagnostic startup failures must still produce a useful JSON report."""
import builtins
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import launcher


class LauncherDiagnosticsTests(unittest.TestCase):
    def test_import_failure_is_recorded_before_cad_or_gui_start(self):
        original_import = builtins.__import__

        def import_without_core(name, *args, **kwargs):
            if name == 'core':
                raise ImportError('diagnostic dependency unavailable')
            return original_import(name, *args, **kwargs)

        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / 'nested' / 'self_test.json'
            with patch('builtins.__import__', side_effect=import_without_core):
                self.assertEqual(launcher.self_test(report), 1)
            result = json.loads(report.read_text(encoding='utf-8'))
            self.assertFalse(result['ok'])
            self.assertEqual(result['checks'], [])
            self.assertIn('diagnostic dependency unavailable', result['error'])

    def test_gui_startup_failure_restores_preferences_and_removes_private_files(self):
        import ui_preferences
        original_path = ui_preferences.preference_path
        original_temporary_directory = tempfile.TemporaryDirectory
        created = []

        def track_temporary_directory(*args, **kwargs):
            temporary = original_temporary_directory(*args, **kwargs)
            created.append(Path(temporary.name))
            return temporary

        with original_temporary_directory() as directory:
            report = Path(directory) / 'self_test.json'
            with patch('app.App', side_effect=RuntimeError('diagnostic GUI startup failure')):
                with patch('tempfile.TemporaryDirectory', side_effect=track_temporary_directory):
                    self.assertEqual(launcher.self_test(report), 1)
            result = json.loads(report.read_text(encoding='utf-8'))
            self.assertFalse(result['ok'])
            self.assertIn('diagnostic GUI startup failure', result['error'])
            self.assertIs(ui_preferences.preference_path, original_path)
            self.assertTrue(created)
            self.assertTrue(all(not path.exists() for path in created))


if __name__ == '__main__':
    unittest.main()
