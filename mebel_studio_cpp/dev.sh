#!/usr/bin/env bash
# Szybki cykl pracy dla Mebel Studio 3D (MSYS2 UCRT64).
#   ./dev.sh            pobierz zmiany + zbuduj tylko to, co się zmieniło + self-test
#   ./dev.sh run        jw. i od razu uruchom aplikację
#   ./dev.sh fast       jak wyżej, ale kompilacja bez optymalizacji (-O0) w osobnym katalogu build-dev
#   ./dev.sh test       sam self-test (bez budowania)
#   ./dev.sh clean      usuń katalog build
# Zmienna PULL=0 wyłącza git pull.
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:-build}"
dir=build
type=Release
if [ "$mode" = "fast" ]; then dir=build-dev; type=Debug; fi
if [ "$mode" = "clean" ]; then rm -rf build build-dev; echo "Usunięto katalogi build."; exit 0; fi

exe() { ls "$dir"/bin/MebelStudio3D_cpp.exe "$dir"/MebelStudio3D_cpp.exe 2>/dev/null | head -1 || true; }

selftest() {
  local e; e="$(exe)"
  [ -n "$e" ] || { echo "Brak pliku exe w $dir"; exit 1; }
  rm -f raport.json
  "$e" --self-test raport.json >/dev/null 2>&1 || true
  if [ -f raport.json ] && grep -q '"ok": true' raport.json; then
    echo "SELF-TEST: OK"
  else
    echo "SELF-TEST: BŁĄD (patrz raport.json)"; tail -n 20 raport.json 2>/dev/null || true; exit 1
  fi
}

if [ "$mode" = "test" ]; then selftest; exit 0; fi

if [ "${PULL:-1}" = "1" ] && command -v git >/dev/null && [ -d ../.git -o -d .git ]; then
  echo "== git pull"; git pull --ff-only || echo "(pominięto pull)"
fi

[ -f "$dir/build.ninja" ] || cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$type"
echo "== budowanie ($dir, tylko zmienione pliki)"
cmake --build "$dir" -- -k 0
selftest
[ "$mode" = "run" ] && exec "$(exe)"
echo "Gotowe. Uruchom: $(exe)"
