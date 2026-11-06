"""Assemble a complete C++/Python Windows package without downloading dependencies."""
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys


BINARIES = ('MebelStudio3D.exe', 'MebelModel.dll', 'MebelNative3D.dll')
APP_REQUIRED = ('launcher.py', 'app.py', 'core.py')
BUILD_HELPERS = {'zbuduj_paczke.py', 'sprawdz_regresje.py'}
SOURCE_IGNORED_DIRS = {'build', 'dist', '__pycache__', '.git', '.venv', 'venv',
                       'node_modules', 'sprawdzenie', 'sprawdzenie_regresji'}
DERIVED_EXTENSIONS = {'.dll', '.exe', '.pyd', '.pyc', '.pyo', '.obj', '.o', '.lib', '.a', '.res'}
RESOURCE_EXTENSIONS = {'.mebel', '.step', '.svg', '.png', '.jpg', '.jpeg', '.html', '.md', '.txt'}
MANIFEST = 'MANIFEST_SHA256.json'
PROVENANCE = 'POCHODZENIE_PACZKI.json'
INCOMPLETE = 'NIEUKONCZONA_PACZKA.json'


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def contained(path, directory):
    return path == directory or directory in path.parents


def is_link(path):
    return path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction())


def require_file(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f'Brak wymaganego pliku albo plik jest pusty: {path}')


def inventory_tree(folder, *, ignored_dirs=(), ignored_extensions=(), excluded=None, preserved_files=()):
    """Enumerate before creating output; never follow a junction or symlink."""
    files = []
    if is_link(folder):
        raise ValueError(f'Paczka nie może zależeć od dowiązania: {folder}')
    for current, directories, names in os.walk(folder, followlinks=False):
        current = Path(current)
        kept = []
        for name in sorted(directories):
            entry = current / name
            ignored_name = name.casefold() in ignored_dirs
            if ignored_dirs and name.casefold().startswith(('build', 'naprawy')):
                ignored_name = True
            if ignored_name or (excluded is not None and contained(entry.resolve(), excluded)):
                continue
            if is_link(entry):
                raise ValueError(f'Paczka nie może zależeć od dowiązania: {entry}')
            kept.append(name)
        directories[:] = kept
        for name in sorted(names):
            entry = current / name
            if (entry.suffix.lower() in ignored_extensions
                    and entry.relative_to(folder).as_posix().casefold() not in preserved_files):
                continue
            if ignored_dirs and entry.suffix.lower() in ('.json', '.log') and name.upper().startswith(
                    ('SPRAWDZENIE', 'VERIFY_', 'BENCHMARK_', 'HOST_BUILD', 'MODEL_BUILD')):
                continue
            if is_link(entry):
                raise ValueError(f'Paczka nie może zależeć od dowiązania: {entry}')
            files.append((entry, entry.relative_to(folder)))
    return files


def assemble(build_dir, output, *, runtime=None, resources=None, source=None, include_sources=False):
    source = Path(source or Path(__file__).resolve().parent).expanduser().resolve(strict=True)
    resources = Path(resources or source.parent).expanduser().resolve(strict=True)
    runtime = Path(runtime or resources / '_internal').expanduser().resolve(strict=True)
    build_dir = Path(build_dir).expanduser().resolve(strict=True)
    raw_output = Path(output).expanduser()
    if is_link(raw_output):
        raise ValueError('Katalog wyjściowy nie może być dowiązaniem.')
    output = raw_output.resolve()
    input_directories = [source, resources, runtime, build_dir,
                         resources / 'runtime', resources / 'LICENCJE', resources / 'PRZYKLADY']
    for directory in input_directories:
        if not directory.is_dir():
            raise ValueError(f'Brak wymaganego katalogu: {directory}')
        if contained(directory, output):
            raise ValueError(f'Katalog wyjściowy zawiera katalog wejściowy: {directory}')
    for directory in input_directories[2:]:
        if contained(output, directory):
            raise ValueError(f'Katalog wyjściowy znajduje się w kopiowanych danych: {directory}')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError(f'Katalog wyjściowy musi być nowy albo pusty: {output}')

    for name in BINARIES:
        require_file(build_dir / name)
    for name in APP_REQUIRED:
        require_file(source / name)
    for name in ('python313.dll', 'base_library.zip', '_tcl_data/init.tcl', '_tk_data/tk.tcl'):
        require_file(runtime / name)
    for name in ('numpy', 'PIL', 'cadquery', 'OCP', 'vtkmodules', 'tkinter'):
        if not (runtime / name).is_dir():
            raise ValueError(f'Niekompletne środowisko programu: {runtime / name}')
    require_file(resources / 'runtime' / 'vulkan-1.dll')
    require_file(resources / 'INSTRUKCJA.html')
    require_file(resources / 'MIGRACJA_CPP.json')

    plan = [(build_dir / name, Path(name)) for name in BINARIES]
    app_files = sorted(path for path in source.glob('*.py') if path.name not in BUILD_HELPERS)
    plan.extend((path, Path('app') / path.name) for path in app_files)
    app_before = {path.name: fingerprint(path) for path in app_files}
    native_before = {name: fingerprint(build_dir / name) for name in BINARIES}
    for folder, destination in ((runtime, '_internal'), (resources / 'runtime', 'runtime'),
                                (resources / 'LICENCJE', 'LICENCJE'),
                                (resources / 'PRZYKLADY', 'PRZYKLADY')):
        plan.extend((path, Path(destination) / relative) for path, relative in inventory_tree(folder))
    for path in sorted(resources.iterdir()):
        if path.is_file() and (path.suffix.lower() in RESOURCE_EXTENSIONS or path.name == 'MIGRACJA_CPP.json'):
            if path.name.upper().startswith(('SPRAWDZENIE', 'VERIFY_', 'TEST_', 'BENCHMARK_')):
                continue
            plan.append((path, Path(path.name)))
    if include_sources:
        plan.extend((path, Path('ZRODLA_PROGRAMU') / relative)
                    for path, relative in inventory_tree(source, ignored_dirs=SOURCE_IGNORED_DIRS,
                                                         ignored_extensions=DERIVED_EXTENSIONS,
                                                         excluded=output,
                                                         preserved_files=('runtime/vulkan-1.dll',)))
    destinations = set()
    for path, relative in plan:
        if is_link(path):
            raise ValueError(f'Paczka nie może zależeć od dowiązania: {path}')
        key = relative.as_posix().casefold()
        if key in destinations:
            raise ValueError(f'Powtórzony plik w paczce: {relative}')
        destinations.add(key)
    planned_bytes = sum(path.stat().st_size for path, _ in plan)
    required_bytes = planned_bytes + max(16 * 1024 * 1024, planned_bytes // 20)
    space_path = output
    while not space_path.exists():
        space_path = space_path.parent
    if shutil.disk_usage(space_path).free < required_bytes:
        raise ValueError(f'Za mało wolnego miejsca. Wymagane co najmniej {required_bytes} bajtów.')

    output.mkdir(parents=True, exist_ok=True)
    (output / INCOMPLETE).write_text(json.dumps({'status': 'copying', 'source': str(source)}, indent=2), encoding='utf-8')
    for path, relative in plan:
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    for name, before in native_before.items():
        if fingerprint(build_dir / name) != before or fingerprint(output / name) != before:
            raise RuntimeError(f'Biblioteka zmieniła się podczas pakowania: {name}')
    for name, before in app_before.items():
        if fingerprint(source / name) != before or fingerprint(output / 'app' / name) != before:
            raise RuntimeError(f'Źródło zmieniło się podczas pakowania: {name}')
    provenance = dict(schema=1, assembled_utc=datetime.now(timezone.utc).isoformat(),
                      package_type='hybrid_cpp_python', source=str(source), build_dir=str(build_dir),
                      runtime=str(runtime), resources=str(resources), include_sources=include_sources,
                      copied_files=len(plan), copied_bytes=planned_bytes,
                      copy_method='independent file copies; no hardlinks',
                      native_binaries=native_before, app_modules=app_before,
                      validation=dict(executed_during_assembly=False, status='requires_verification'),
                      historical_test_reports_included=False)
    (output / PROVENANCE).write_text(json.dumps(provenance, ensure_ascii=False, indent=2), encoding='utf-8')
    manifest = {relative.as_posix(): fingerprint(output / relative) for _, relative in plan}
    manifest[PROVENANCE] = fingerprint(output / PROVENANCE)
    (output / MANIFEST).write_text(json.dumps(dict(sorted(manifest.items())), ensure_ascii=False, indent=2), encoding='utf-8')
    (output / INCOMPLETE).unlink()
    return dict(ok=True, output=str(output), files=len(manifest), copied_bytes=planned_bytes,
                manifest=str(output / MANIFEST), provenance=str(output / PROVENANCE),
                validation='not run; verify the staged package before distribution')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True, help='Katalog bin świeżej kompilacji CMake.')
    parser.add_argument('--output', type=Path, required=True, help='Nowy albo pusty katalog końcowej paczki.')
    parser.add_argument('--runtime', type=Path, help='Kompletny katalog _internal; domyślnie obok źródeł.')
    parser.add_argument('--resources', type=Path, help='Paczka wzorcowa z dokumentacją, licencjami i przykładami.')
    parser.add_argument('--include-sources', action='store_true', help='Dołącz źródła bez build, dist i plików wynikowych.')
    args = parser.parse_args(argv)
    try:
        result = assemble(args.build_dir, args.output, runtime=args.runtime, resources=args.resources,
                          include_sources=args.include_sources)
    except (OSError, ValueError, RuntimeError) as error:
        print(json.dumps(dict(ok=False, error=str(error)), ensure_ascii=False), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
