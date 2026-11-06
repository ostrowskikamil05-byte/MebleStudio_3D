"""Package copies, hashes and destination safety with small real fixture files."""
import json
from pathlib import Path
import tempfile
import unittest

from zbuduj_paczke import assemble, BINARIES, fingerprint, INCOMPLETE, MANIFEST, PROVENANCE


class PackageAssemblyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name) / 'Łódź — próba paczki'
        self.resources = self.base / 'paczka wzorcowa'
        self.source = self.resources / 'ZRODLA_PROGRAMU'
        self.runtime = self.resources / '_internal'
        self.build = self.base / 'nowa kompilacja' / 'bin'
        self.output = self.base / 'gotowy program'
        for name in BINARIES:
            self.write(self.build / name, b'new native artifact ' + name.encode())
            self.write(self.resources / name, b'old artifact must never be reused')
        for name in ('launcher.py', 'core.py', 'app.py', 'funkcja_żółć.py', 'zbuduj_paczke.py', 'sprawdz_regresje.py'):
            self.write(self.source / name, b'# source module\n')
        for name in ('python313.dll', 'base_library.zip', '_tcl_data/init.tcl', '_tk_data/tk.tcl'):
            self.write(self.runtime / name, b'bundled runtime')
        for name in ('numpy', 'PIL', 'cadquery', 'OCP', 'vtkmodules', 'tkinter'):
            self.write(self.runtime / name / '__init__.py', b'# dependency\n')
        for name in ('runtime/vulkan-1.dll', 'runtime/VulkanRT-License.txt', 'LICENCJE/Python.txt',
                     'PRZYKLADY/Żółta szafka.mebel', 'INSTRUKCJA.html', 'MIGRACJA_CPP.json',
                     'WIDOK_PROGRAMU.png', 'SPRAWDZENIE_0_15.json'):
            self.write(self.resources / name, b'fixture resource')
        self.write(self.source / 'native/tests/fixtures/reference.mebel', b'source fixture')
        self.write(self.source / 'native/host/Python-LICENSE.txt', b'source license')
        self.write(self.source / 'runtime/vulkan-1.dll', b'vendored Vulkan loader')
        self.write(self.source / 'tests/test_real.py', b'# source test\n')

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def assemble(self, **kwargs):
        return assemble(self.build, self.output, source=self.source, resources=self.resources, **kwargs)

    def test_independent_complete_copy_uses_new_binaries_and_manifest(self):
        result = self.assemble()
        self.assertTrue(result['ok'])
        for name in BINARIES:
            self.assertEqual((self.output / name).read_bytes(), (self.build / name).read_bytes())
        self.assertTrue((self.output / 'app/funkcja_żółć.py').is_file())
        self.assertFalse((self.output / 'app/zbuduj_paczke.py').exists())
        self.assertFalse((self.output / 'app/sprawdz_regresje.py').exists())
        self.assertTrue((self.output / '_internal/cadquery/__init__.py').is_file())
        self.assertTrue((self.output / 'PRZYKLADY/Żółta szafka.mebel').is_file())
        self.assertFalse((self.output / 'SPRAWDZENIE_0_15.json').exists())
        self.assertFalse((self.output / INCOMPLETE).exists())
        manifest = json.loads((self.output / MANIFEST).read_text(encoding='utf-8'))
        actual = {path.relative_to(self.output).as_posix() for path in self.output.rglob('*')
                  if path.is_file() and path.name != MANIFEST}
        self.assertEqual(set(manifest), actual)
        for relative, expected in manifest.items():
            self.assertEqual(fingerprint(self.output / relative), expected)
        provenance = json.loads((self.output / PROVENANCE).read_text(encoding='utf-8'))
        self.assertFalse(provenance['validation']['executed_during_assembly'])
        original = (self.source / 'core.py').read_bytes()
        (self.output / 'app/core.py').write_bytes(b'changed staged source')
        self.assertEqual((self.source / 'core.py').read_bytes(), original)

    def test_output_in_source_dist_does_not_recurse_or_copy_derived_files(self):
        self.output = self.source / 'dist' / 'gotowa paczka'
        for name in ('build/obj.o', 'build-debug/notes.txt', 'dist/stara_paczka/file.txt',
                     '__pycache__/app.pyc', '.venv/settings.json', 'SPRAWDZENIE/old.json',
                     'NAPRAWY_2026/old.json', 'SPRAWDZENIE_WYDAJNOSCI_0_11.json'):
            self.write(self.source / name, b'derived or historical output')
        self.assemble(include_sources=True)
        copied = self.output / 'ZRODLA_PROGRAMU'
        self.assertTrue((copied / 'native/tests/fixtures/reference.mebel').is_file())
        self.assertTrue((copied / 'native/host/Python-LICENSE.txt').is_file())
        self.assertEqual((copied / 'runtime/vulkan-1.dll').read_bytes(), b'vendored Vulkan loader')
        self.assertTrue((copied / 'tests/test_real.py').is_file())
        for name in ('build', 'build-debug', 'dist', '__pycache__', '.venv', 'SPRAWDZENIE',
                     'NAPRAWY_2026', 'SPRAWDZENIE_WYDAJNOSCI_0_11.json'):
            self.assertFalse((copied / name).exists(), name)

    def test_nonempty_and_input_destinations_are_rejected_without_writes(self):
        self.write(self.output / 'user_project.mebel', b'preserve this project')
        with self.assertRaises(ValueError):
            self.assemble()
        self.assertEqual(list(self.output.iterdir()), [self.output / 'user_project.mebel'])
        self.assertEqual((self.output / 'user_project.mebel').read_bytes(), b'preserve this project')
        for forbidden in (self.runtime / 'new', self.build / 'new', self.resources / 'PRZYKLADY/new', self.base):
            with self.subTest(path=str(forbidden)):
                self.output = forbidden
                with self.assertRaises(ValueError):
                    self.assemble()

    def test_missing_prerequisite_fails_before_output_is_created(self):
        (self.build / 'MebelNative3D.dll').unlink()
        with self.assertRaises(ValueError):
            self.assemble()
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
