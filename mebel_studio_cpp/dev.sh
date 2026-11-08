#!/usr/bin/env bash
# Szybki cykl pracy dla Mebel Studio 3D (MSYS2 UCRT64).
#   ./dev.sh            pobierz zmiany + zbuduj tylko to, co się zmieniło + self-test
#   ./dev.sh run        jw. i od razu uruchom aplikację
#   ./dev.sh fast       jak wyżej, ale kompilacja bez optymalizacji (-O0) w osobnym katalogu build-dev
#   ./dev.sh test       sam self-test (bez budowania)
#   ./dev.sh fulltest   jw. jak „run”, ale zamiast aplikacji uruchamia PEŁNY TEST wszystkich funkcji i zapisuje raport do raporty_testu/
#                       (ustawienia programu nie są zapisywane; ten sam test: MebelStudio3D_cpp.exe --full-test [katalog])
#   ./dev.sh addr 0x…   nazwa funkcji dla adresu z logu "Stos postoju" (exe+0x…)
#   ./dev.sh dll        przebuduj MebelNative3D.dll i MebelModel.dll ze źródeł (biblioteki_natywne/zrodla) do native-dll/
#   ./dev.sh clean      usuń katalog build
# Zmienna PULL=0 wyłącza git pull.
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:-build}"
dir=build
type=Release
if [ "$mode" = "fast" ]; then dir=build-dev; type=Debug; fi
if [ "$mode" = "clean" ]; then rm -rf build build-dev; echo "Usunięto katalogi build."; exit 0; fi

build_dlls() {
  local native="../biblioteki_natywne/zrodla" out="native-dll" cxx="${CXX:-g++}"
  command -v "$cxx" >/dev/null || { echo "Brak kompilatora $cxx (powłoka MSYS2 UCRT64?)"; exit 1; }
  mkdir -p "$out"
  local flags=(-std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -static-libstdc++ -static-libgcc -DUNICODE -D_UNICODE)
  echo "== kompilator: $cxx ($($cxx --version | head -1))"
  for f in MebelNative3D.dll MebelModel.dll; do
    [ -f "$out/$f" ] && cp -f "$out/$f" "$out/$f.bak"
  done
  echo "== MebelNative3D.dll (silnik D3D11/D3D12/Vulkan)"
  "$cxx" "${flags[@]}" -shared -I"$native/third_party/vulkan/include" \
    "$native/src/native_embed.cpp" "$native/src/scene.cpp" "$native/src/renderer_d3d11.cpp" \
    "$native/src/renderer_d3d12.cpp" "$native/src/renderer_vulkan.cpp" \
    -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -lgdi32 -lgdiplus -luser32 -lole32 -o "$out/MebelNative3D.dll"
  echo "== MebelModel.dll (model obliczeniowy)"
  local core="$native/core" srcs=()
  for n in model_api core_model shapes construction services document layout layout_geometry worktop; do srcs+=("$core/$n.cpp"); done
  "$cxx" "${flags[@]}" -shared "${srcs[@]}" -o "$out/MebelModel.dll"
  echo "== gotowe:"; ls -l "$out"/*.dll; sha256sum "$out"/*.dll
  for d in build build-dev; do [ -f "$d/build.ninja" ] && cmake -S . -B "$d" >/dev/null; done
  echo "Teraz: ./dev.sh run   (CMake skopiuje DLL z native-dll/ do build/bin)"
}

if [ "$mode" = "dll" ]; then build_dlls; exit 0; fi

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

if [ "$mode" = "addr" ]; then  # ./dev.sh addr 0x1234 0x5678 — nazwy funkcji dla adresów "MebelStudio3D_cpp.exe+0x…" z logu
  e="$(exe)"; [ -n "$e" ] || { echo "Brak pliku exe w $dir"; exit 1; }
  base="$(objdump -p "$e" | awk '/^ImageBase/{print $2}')"
  shift
  for a in "$@"; do
    printf '%s  ' "$a"; addr2line -f -C -e "$e" "$(printf '0x%x' $((0x$base + a)))" | paste -sd' '
  done
  exit 0
fi

if [ "${PULL:-1}" = "1" ] && command -v git >/dev/null && [ -d ../.git -o -d .git ]; then
  echo "== git pull"; git pull --ff-only || echo "(pominięto pull)"
fi

[ -f "$dir/build.ninja" ] || cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$type"
echo "== budowanie ($dir, tylko zmienione pliki)"
cmake --build "$dir" -- -k 0
selftest
[ "$mode" = "run" ] && exec "$(exe)"
if [ "$mode" = "fulltest" ]; then
  mkdir -p raporty_testu
  "$(exe)" --full-test raporty_testu || true
  echo "Raport: $(ls -t raporty_testu/raport_pelny_test_*.md 2>/dev/null | head -1)"
  exit 0
fi
echo "Gotowe. Uruchom: $(exe)"
