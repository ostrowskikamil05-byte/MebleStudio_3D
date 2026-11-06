"""Sprawdza testy źródeł, korzystając również z bibliotek dołączonej paczki."""
from datetime import datetime, timezone
import argparse
import faulthandler
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import unittest


def signature(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-dir', type=Path,
                        default=Path(__file__).resolve().parent / 'SPRAWDZENIE_REGRESJI')
    parser.add_argument('--pattern', default='test*.py', help='Wzorzec nazw plików testowych.')
    parser.add_argument('--native-library', type=Path,
                        help='Biblioteka MebelModel.dll do sprawdzenia bez podmiany plików programu.')
    args = parser.parse_args(argv)
    source = Path(__file__).resolve().parent
    runtime = source.parent / '_internal'
    report_dir = args.report_dir.expanduser().resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    log_path = report_dir / 'python_regression.log'
    report_path = report_dir / 'python_regression.json'
    actual = Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'MebelStudio3D' / 'ui_preferences.json'
    before = signature(actual)
    private = report_dir / 'private'
    preference = private / 'MebelStudio3D' / 'ui_preferences.json'
    preference.parent.mkdir(parents=True, exist_ok=True)
    preference.write_text(json.dumps({'schema': 1, 'engine': 'cpu'}), encoding='utf-8')
    start = time.monotonic()
    report = dict(started_utc=datetime.now(timezone.utc).isoformat(), source=str(source),
                  interpreter=sys.executable, python=sys.version, pattern=args.pattern,
                  bundled_runtime=str(runtime) if runtime.is_dir() else None,
                  private_preferences=str(preference), actual_preferences=str(actual),
                  actual_preferences_before=before, status='running')
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    previous_environment = {key: os.environ.get(key) for key in ('LOCALAPPDATA', 'TCL_LIBRARY', 'TK_LIBRARY')}
    previous_path = sys.path[:]
    previous_stdout, previous_stderr = sys.stdout, sys.stderr
    previous_bytecode = sys.dont_write_bytecode
    previous_directory = Path.cwd()
    previous_faulthandler = faulthandler.is_enabled()
    dll_handles = []
    native_model = None
    previous_library_path = None
    with log_path.open('w', encoding='utf-8', buffering=1) as stream:
        try:
            sys.stdout = sys.stderr = stream
            faulthandler.enable(file=stream)
            os.environ['LOCALAPPDATA'] = str(private)
            os.chdir(source)
            sys.dont_write_bytecode = True
            sys.path.insert(0, str(source))
            if runtime.is_dir():
                if sys.version_info[:2] != (3, 13):
                    raise RuntimeError('Biblioteki paczki wymagają Python 3.13. Uruchom SPRAWDZ.bat albo py -3.13 sprawdz_regresje.py.')
                # Keep the complete interpreter standard library ahead of the
                # pruned packaged copy (e.g. html.parser is used only by tests).
                if str(runtime) not in sys.path:
                    sys.path.append(str(runtime))
                if hasattr(os, 'add_dll_directory'):
                    dll_handles.append(os.add_dll_directory(str(runtime)))
                for variable, folder in (('TCL_LIBRARY', '_tcl_data'), ('TK_LIBRARY', '_tk_data')):
                    if (runtime / folder).is_dir():
                        os.environ[variable] = str(runtime / folder)
            if args.native_library is not None:
                library = args.native_library.expanduser()
                if not library.is_absolute():
                    library = previous_directory / library
                library = library.resolve(strict=True)
                if not library.is_file():
                    raise ValueError('Wskaż plik biblioteki MebelModel.dll.')
                import native_model
                previous_library_path = native_model.library_path
                native_model.library_path = lambda: library
                report.update(native_library=str(library), native_library_sha256=signature(library))
            suite = unittest.defaultTestLoader.discover(str(source / 'tests'), pattern=args.pattern)
            result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
            report.update(tests_run=result.testsRun,
                          failures=[(str(test), trace) for test, trace in result.failures],
                          errors=[(str(test), trace) for test, trace in result.errors],
                          skipped=[(str(test), reason) for test, reason in result.skipped],
                          ok=result.wasSuccessful() and result.testsRun > 0)
        except Exception:
            import traceback
            report.update(ok=False, error=traceback.format_exc())
            stream.write(report['error'])
        finally:
            report.update(status='complete', finished_utc=datetime.now(timezone.utc).isoformat(),
                          elapsed_seconds=round(time.monotonic() - start, 3),
                          actual_preferences_after=signature(actual),
                          actual_preferences_preserved=before == signature(actual))
            report['ok'] = report.get('ok', False) and report['actual_preferences_preserved']
            report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            sys.stdout, sys.stderr = previous_stdout, previous_stderr
            faulthandler.disable()
            if previous_faulthandler:
                faulthandler.enable()
            sys.path[:] = previous_path
            sys.dont_write_bytecode = previous_bytecode
            os.chdir(previous_directory)
            for key, value in previous_environment.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
            for handle in dll_handles:
                handle.close()
            if previous_library_path is not None:
                native_model.library_path = previous_library_path
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
