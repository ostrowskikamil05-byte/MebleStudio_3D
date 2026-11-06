# Wbudowane środowisko programu 0.14

`MebelStudio3D.exe` jest natywnym programem C++ dla Windows. Uruchamia zachowany
interfejs i logikę meblarską we własnym procesie, korzystając z `python313.dll`
dostarczonego w folderze `_internal`. Nie uruchamia drugiego programu Python,
nie pobiera pakietów i nie wymaga instalowania pip ani systemowego Pythona.

Ten host zapewnia ciągłość działania dotychczasowych funkcji podczas przenoszenia
silnika renderowania do C++. Interfejs Tk i logika aplikacji pozostają modułami
Python w folderze `app`; sam host nie oznacza przepisania tych modułów na C++.
Dokładna geometria CAD wykorzystuje dostarczone biblioteki natywne OpenCascade.

## Układ paczki

- `MebelStudio3D.exe`: host C++ z manifestem System DPI i zgodności Windows 8/10.
- `app`: źródła funkcji programu, wczytywane przed modułami zależności.
- `_internal`: prywatne biblioteki Python 3.13, Tk, CAD, NumPy i VTK.
- `runtime`: prywatny oficjalny loader Vulkan.
- Pliki przykładów i dokumentacji znajdują się obok EXE, zgodnie z wcześniejszym
  sposobem wyznaczania katalogu zasobów przez aplikację.

Uruchamianie z dowolnego bieżącego folderu jest obsługiwane. Host ignoruje
`PYTHONHOME`, `PYTHONPATH`, pakiety użytkownika oraz moduły w bieżącym folderze.
Zapis plików bytecode jest wyłączony. Pełną paczkę należy rozpakować razem;
samo przeniesienie EXE nie przenosi bibliotek aplikacji.

## Diagnostyka

Log startowy i błędy Pythona są zapisywane w `%LOCALAPPDATA%\MebelStudio3D`:
`native-host.log` i `native-host-stderr.log`. Gdy ten katalog jest niedostępny,
host korzysta z `%TEMP%\MebelStudio3D`. Logi są ograniczone do około 1 MB na plik
i jednej poprzedniej kopii. Diagnostyka logów w interfejsie pozostaje dostępna.

Tryb `MebelStudio3D.exe --host-test C:\sciezka\raport.json` sprawdza załadowanie
modułów core, Tk, NumPy, VTK i CadQuery bez otwierania okna. Tryb `--self-test`
uruchamia kontrolę funkcji aplikacji i jej interfejsu. Pozostałe argumenty oraz
ścieżka projektu `.mebel` są przekazywane dotychczasowemu modułowi uruchomienia.

## Budowanie hosta

`build_host.py --compiler C:\llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe
--output C:\paczka\MebelStudio3D.exe` kompiluje kod C++ oraz manifest. Wymagane
jest LLVM-MinGW z programem windres. Nagłówek konfiguracji CPython 3.13 znajduje
się w źródłach i jest objęty dołączoną licencją Python; instalacja SDK Python
nie jest wymagana do kompilacji samego hosta. Bibliotekę DLL Python należy
dostarczyć razem z kompletnym, zgodnym środowiskiem 3.13.

`extract_runtime.py C:\paczka\_internal --release C:\rozpakowana-wersja-0.13`
przygotowuje nową kopię bibliotek i wydobywa moduły bytecode z archiwum EXE.
Skrypt budowania paczki wymaga Python 3.13 z PyInstaller; użytkownik programu
tego narzędzia nie potrzebuje. Ekstraktor nie nadpisuje różniących się plików
i odmawia zapisu do pierwotnego katalogu `_internal` wersji 0.13.

Opcja `--hardlinks` jest przeznaczona wyłącznie do odczytowego przygotowania
środowiska testowego na tym samym dysku. Końcowa paczka 0.14 zawiera niezależne
kopie plików bibliotek.
