"""Extract immutable frozen pure modules into a new, independent runtime.

Run with matching Python 3.13 and PyInstaller installed. No released files change.
The resulting normal .pyc modules no longer require the PyInstaller importer.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, marshal, os, shutil, struct
from PyInstaller.archive.readers import CArchiveReader

ROOT = next((p for p in Path(__file__).resolve().parents if (p / 'outputs/MebelStudio3D_0_13_Windows').is_dir()), Path.cwd())

def extract(destination, *, hardlinks=False, release=None):
    release = Path(release).resolve() if release is not None else ROOT / 'outputs/MebelStudio3D_0_13_Windows'
    runtime = release / '_internal'
    destination = Path(destination).resolve()
    if destination == runtime or runtime in destination.parents:
        raise ValueError('Do not write into released runtime')
    destination.mkdir(parents=True, exist_ok=True)
    copied = 0
    for source in runtime.rglob('*'):
        relative = source.relative_to(runtime)
        if '__pycache__' in relative.parts:
            continue
        target = destination / relative
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            if hardlinks:
                os.link(source, target)
            else:
                shutil.copy2(source, target)
            copied += 1
    executable = release / 'MebelStudio3D.exe'
    outer = CArchiveReader(str(executable))
    archive = outer.open_embedded_archive('PYZ.pyz')
    app_modules = {p.stem for p in (release / 'ZRODLA_PROGRAMU').glob('*.py')}
    if not app_modules:
        raise ValueError('Released package must contain ZRODLA_PROGRAMU Python sources')
    count = 0
    for name, (kind, _, _) in archive.toc.items():
        if name in app_modules:
            continue  # New app source must be authoritative.
        components = name.split('.')
        if any(p in ('', '..', '.') for p in components):
            raise ValueError('Invalid archive module name')
        if kind == 3:  # Namespace package; no code payload.
            (destination / Path(*components)).mkdir(parents=True, exist_ok=True)
            continue
        code = archive.extract(name)
        if not hasattr(code, 'co_code'):
            raise ValueError(f'Expected Python code for {name}')
        relative = Path(*components, '__init__.pyc') if kind == 1 else Path(*components[:-1], components[-1] + '.pyc')
        target = destination / relative
        payload = importlib.util.MAGIC_NUMBER + struct.pack('<III', 0, 0, 0) + marshal.dumps(code)
        if target.exists():
            if target.read_bytes() != payload:
                raise ValueError(f'Extraction refuses to replace existing file: {target}')
            count += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        # Unchecked timestamp pyc is valid for the standard sourceless loader.
        target.write_bytes(payload)
        count += 1
    # struct is bundled in the bootstrap CArchive rather than the main PYZ.
    target = destination / 'struct.pyc'
    payload = importlib.util.MAGIC_NUMBER + struct.pack('<III', 0, 0, 0) + outer.extract('struct')
    if target.exists() and target.read_bytes() != payload:
        raise ValueError('Refusing to replace existing struct.pyc')
    if not target.exists():
        target.write_bytes(payload)
    report = dict(ok=True, dependency_modules=count, runtime_files=copied,
                  source_exe_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  destination=str(destination), hardlinks=hardlinks)
    (destination / 'runtime_manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination')
    parser.add_argument('--hardlinks', action='store_true')
    parser.add_argument('--release', help='Path to unpacked immutable 0.13 Windows package')
    args = parser.parse_args()
    print(json.dumps(extract(args.destination, hardlinks=args.hardlinks, release=args.release)), flush=True)
