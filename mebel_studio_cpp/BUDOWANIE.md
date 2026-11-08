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
  (`biblioteki_natywne/` w repozytorium). Inny katalog: `-DMEBEL_NATIVE_DIR=C:/ścieżka`.
* Po zbudowaniu CMake kopiuje obok programu wymagane biblioteki DLL (g++, Tcl/Tk, OpenCASCADE…)
  i katalogi `lib/tcl8.6`, `lib/tk8.6`. Dzięki temu `build/bin` można uruchomić poza powłoką MSYS2.
  Wyłączenie: `-DMEBEL_BUNDLE_RUNTIME=OFF` (wtedy uruchamiaj z powłoki UCRT64).
* Kompilacja całości trwa kilka–kilkanaście minut (nlohmann::json + OpenCASCADE). Pierwsza próba
  kompilacji pojedynczego pliku: `cmake --build build --target MebelStudio3D_cpp -- -k 0`
  zbiera wszystkie błędy zamiast zatrzymać się na pierwszym.

## 3. Pierwsze uruchomienie

```bash
build/bin/MebelStudio3D_cpp.exe --self-test raport.json   # diagnostyka offline, bez GUI
build/bin/MebelStudio3D_cpp.exe --full-test raporty_testu  # pełny test funkcji + audyt źródeł (Markdown, JSON i log); to samo robi przycisk „Pełny test” w Logach
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

Skrypty Tcl/Tk program szuka kolejno w: `TCL_LIBRARY`/`TK_LIBRARY`, `lib/tcl8.6` i `lib/tk8.6` obok programu
(kopiuje je CMake przy `MEBEL_BUNDLE_RUNTIME=ON`).


## Szybki cykl pracy (bez ciągłego budowania od zera)

W powłoce MSYS2 UCRT64, w folderze `mebel_studio_cpp`:

```bash
./dev.sh          # git pull + budowanie tylko zmienionych plików (ninja) + self-test
./dev.sh run      # jw. i od razu start aplikacji
./dev.sh fast     # kompilacja -O0 w build-dev: najszybsza przy poprawkach interfejsu
./dev.sh test     # sam self-test bez budowania
# Wersja deweloperska vs końcowa: narzędzia deweloperskie (panel Logi, dziennik operacji, pełny test, stan migracji)
# są wbudowane tylko przy -DMEBEL_DEV_TOOLS=ON (domyślnie). Wersja dla użytkowników:
#   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEBEL_DEV_TOOLS=OFF
# W wersji deweloperskiej MEBEL_DEV=0 chowa te elementy bez przebudowy (podgląd wyglądu dla użytkownika).
./dev.sh fulltest # budowanie + PEŁNY TEST wszystkich funkcji z raportem w raporty_testu/ (ustawienia programu nie są zapisywane)
```

Dodatkowo `pacman -S --needed mingw-w64-ucrt-x86_64-ccache` przyspiesza ponowne budowanie
(CMake sam wykrywa ccache). Zmiana jednego pliku `.cpp` oznacza ponowną kompilację tylko tego pliku i linkowanie.

## 5. Przebudowa bibliotek DLL (MebelNative3D.dll, MebelModel.dll)

DLL-e w `biblioteki_natywne/` są w Git LFS i mogą być **starsze niż źródła** w `biblioteki_natywne/zrodla`
(np. ciemne tło i brak podłogi/siatki w silniku natywnym). Świeże buduje się lokalnie, w powłoce UCRT64:

```bash
cd mebel_studio_cpp
./dev.sh dll        # g++ -> native-dll/MebelNative3D.dll + native-dll/MebelModel.dll (poprzednie jako *.bak)
./dev.sh run        # CMake kopiuje DLL z native-dll/ do build/bin (ma pierwszeństwo przed korzeniem repo)
```

Oficjalna ścieżka (LLVM-MinGW, razem z testami sceny), z PowerShella:

```powershell
biblioteki_natywne\zrodla\tools\build_embed.ps1 -Compiler 'C:\llvm-mingw\bin\clang++.exe' -RunTests
biblioteki_natywne\zrodla\tools\build_model.ps1 -Compiler 'C:\llvm-mingw\bin\clang++.exe'
```

Zmienna `CXX=clang++ ./dev.sh dll` wybiera inny kompilator. Plik DLL poniżej 4 KB to wskaźnik LFS —
program zgłasza to w logu zamiast ładować „pustą” bibliotekę.
