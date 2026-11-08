"""Build the independent C++ Windows host with LLVM-MinGW (no Python SDK needed)."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess

SOURCE = Path(__file__).resolve().parent

def workspace():
    return next((p for p in SOURCE.parents if (p / 'work/native-tools').is_dir()), SOURCE)

def build(compiler, output):
    compiler, output = Path(compiler).resolve(), Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    windres = compiler.with_name('x86_64-w64-mingw32-windres.exe')
    resource = output.with_suffix('.host.res.o')
    inputs = [SOURCE / p for p in ('host.cpp', 'host.rc', 'host.manifest', 'pyconfig313.h')]
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    commands = [
        [str(windres), '--codepage=65001', 'host.rc', '-O', 'coff', '-o', str(resource)],
        [str(compiler), '-std=c++20', '-O2', '-Wall', '-Wextra', '-static', '-static-libstdc++',
         '-static-libgcc', '-municode', '-mwindows', '-DUNICODE', '-D_UNICODE',
         str(SOURCE / 'host.cpp'), str(resource), '-lshell32', '-o', str(output)],
    ]
    log = []
    for command in commands:
        result = subprocess.run(command, cwd=SOURCE, text=True, encoding='utf-8', errors='replace', capture_output=True)
        log.append(result.stdout + result.stderr)
        if result.returncode:
            output.with_suffix('.build.log').write_text('\n'.join(log), encoding='utf-8')
            raise RuntimeError('Host build failed: ' + log[-1])
    current = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    if current != hashes:
        raise RuntimeError('Host sources changed during build; rebuild')
    resource.unlink(missing_ok=True)
    report = dict(ok=True, compiler=str(compiler), executable=str(output), bytes=output.stat().st_size,
                  sha256=hashlib.sha256(output.read_bytes()).hexdigest(), source_sha256=hashes,
                  native_windows_executable=True, external_python_required=False)
    output.with_suffix('.build.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    output.with_suffix('.build.log').write_text('\n'.join(log), encoding='utf-8')
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    default_compiler = workspace() / 'work/native-tools/llvm-mingw-20260922-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe'
    parser.add_argument('--compiler', default=str(default_compiler))
    parser.add_argument('--output', default=str(workspace() / 'work/native-host/MebelStudio3D_host.exe'))
    args = parser.parse_args()
    print(json.dumps(build(args.compiler, args.output)), flush=True)
