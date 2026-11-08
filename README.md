# Mebel Studio 3D

Program do projektowania mebli (C++20, MSYS2 UCRT64, OpenCASCADE, własna warstwa Tk, interfejs „FORMA Studio”) dla Windows.
Gałąź robocza: `migracja-cpp`.

| Katalog | Po co jest |
|---|---|
| `mebel_studio_cpp/` | **Program** wraz z samotestami (`selftest.cpp`), `dev.sh` (budowanie i testy), `BUDOWANIE.md`, `CLAUDE.md` (zasady pracy) |
| `biblioteki_natywne/` | `MebelModel.dll` i `MebelNative3D.dll` (Git LFS), ich źródła (`zrodla/`) i `MIGRACJA_CPP.json` |
| `app/` | Oryginał w Pythonie — wzorzec, z którym test porównuje zgodność funkcji (nie jest uruchamiany) |
| `przyklady/` | Projekty `.mebel` i eksporty używane przez testy |
| `licencje/` | Noty licencyjne używanych bibliotek |

Stara paczka Pythona 0.15 (środowisko, exe, dokumentacja, zrzuty) jest tylko w historii — gałąź `archiwum-python-0.15`.

```bash
cd mebel_studio_cpp
./dev.sh run               # pobierz zmiany, zbuduj, samotest, uruchom program
PULL=0 ./dev.sh fulltest   # pełny test → raporty_testu/OSTATNI_RAPORT.md
```
