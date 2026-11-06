# Budowanie Mebel Studio 3D (C++20) w MSYS2 UCRT64

Cały program (71 modułów z `app/*.py` + warstwa Tcl/Tk) jest w tym katalogu. Docelowy
toolchain: **MSYS2 UCRT64, g++ + Ninja + windres**.

## 1. Pakiety (powłoka „MSYS2 UCRT64”)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-tk mingw-w64-ucrt-x86_64-opencascade mingw-w64-ucrt-x86_64-nlohmann-json
# opcjonalnie rendering GPU przez VTK (bez niego program rysuje na CPU / przez MebelNative3D.dll):
pacman -S --needed mingw-w64-ucrt-x86_64-vtk
```

## 2. Budowa

```bash
cd mebel_studio_cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

* Wynik: `build/bin/MebelStudio3D_cpp.exe` (domyślnie z oknem konsoli — widać komunikaty błędów).
  `-DMEBEL_GUI_SUBSYSTEM=ON` wyłącza konsolę.
* `MebelModel.dll` i `MebelNative3D.dll` CMake kopiuje obok programu z katalogu nadrzędnego
  (korzeń repozytorium). Inny katalog: `-DMEBEL_NATIVE_DIR=C:/ścieżka`.
* Po zbudowaniu CMake kopiuje obok programu wymagane biblioteki DLL (g++, Tcl/Tk, OpenCASCADE…)
  i katalogi `lib/tcl8.6`, `lib/tk8.6`. Dzięki temu `build/bin` można uruchomić poza powłoką MSYS2.
  Wyłączenie: `-DMEBEL_BUNDLE_RUNTIME=OFF` (wtedy uruchamiaj z powłoki UCRT64).
* Kompilacja całości trwa kilka–kilkanaście minut (nlohmann::json + OpenCASCADE). Pierwsza próba
  kompilacji pojedynczego pliku: `cmake --build build --target MebelStudio3D_cpp -- -k 0`
  zbiera wszystkie błędy zamiast zatrzymać się na pierwszym.

## 3. Pierwsze uruchomienie

```bash
build/bin/MebelStudio3D_cpp.exe --self-test raport.json   # diagnostyka offline, bez GUI
build/bin/MebelStudio3D_cpp.exe                           # aplikacja
```

Kod interfejsu (okna, menu, obsługa myszy) został przetłumaczony 1:1, ale **nigdy nie był
uruchomiony** — pierwsze uruchomienie może pokazać błędy czasu działania. Prześlij komunikat
z konsoli (albo `MebelStudio3D-blad.txt` z katalogu domowego) i poprawiamy w kolejnych krokach.

## 4. Warstwa Tcl/Tk

| plik | rola |
|------|------|
| `tk.h`, `ttk.h` | interfejs (odpowiednik `tkinter` / `tkinter.ttk`) używany przez moduły UI |
| `tk.cpp`, `ttk.cpp` | implementacja: budowanie poleceń Tcl, rejestr widgetów, zdarzenia, zmienne, dialogi |
| `tkcore.h`, `tkcore.cpp` | **jedyny** plik z `<tcl.h>`/`<tk.h>` (nagłówki X11 z Tk definiują makra `None`, `Bool`, `Status`) |
| `tests/tk_mock/` | test warstwy na atrapie interpretera (`-DMEBEL_BUILD_TK_TESTS=ON`, bez Tcl/Tk) |

Zasady: ścieżki widgetów jak w tkinter (`.!frame.!button`), opcje `{"nazwa", wartość}` (`from_` →
`-from`, pusta `Value()` pomijana), `std::function` rejestrowane jako polecenia Tcl, wyjątki z wywołań
zwrotnych trafiają do `Tk::report_callback_exception`. Katalog projektu **nie może** być w `-I`
(`<tk.h>` trafiłby do naszego `tk.h`) — CMake tego pilnuje.

Skrypty Tcl/Tk program szuka kolejno w: `TCL_LIBRARY`/`TK_LIBRARY`, `_internal/_tcl_data` i
`_internal/_tk_data` (układ PyInstallera z repozytorium), `lib/tcl8.6`, `lib/tk8.6` obok programu.
