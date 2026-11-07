# Zasady pracy nad Mebel Studio 3D (C++)

- Odpowiedzi dla użytkownika po polsku. Gałąź robocza: `migracja-cpp` (do `main` dopiero na polecenie).
- **Każda zmiana zachowania programu musi mieć (1) wpis w dzienniku i (2) test.**
  - Dziennik: `journal::event(app, "kategoria", "opis")` w miejscu akcji użytkownika (kliknięcie, zatwierdzenie, odrzucenie z powodem).
    Zmiany projektu opisuje automatycznie `[operacja:zmiana]` (hak w `App::refresh`, źródło = plik:linia ostatniego `App::snapshot`).
  - Test: przypadek w `selftest.cpp` — dla dziennika w `test_journal_actions()` (znacznik `mark()` / `since()` czyta fragment logu dopisany w trakcie przypadku);
    nowa kategoria dziennika = wpis w tabeli `JOURNAL_COVERAGE` (selftest.cpp) z nazwą testu; audyt źródeł zgłasza kategorię użytą w kodzie bez wpisu.
  - Nowy mebel / kształt / urządzenie w bibliotece jest testowany automatycznie przez `test_library_matrix()` (każdy element osobno + wpisy `[biblioteka]` i `[zmiana]`).
  - Zmiany nie wolno kończyć bez uruchomienia całości: stare testy (regresja) mają przechodzić razem z nowymi.
- Nic nie może się psuć po cichu: gdy akcja jest blokowana albo odrzucana, w dzienniku ma być powód (por. `allow_press`, brak uchwytów rozciągania).
- Komendy: `./dev.sh fulltest` (`PULL=0 ./dev.sh fulltest`), raport w `raporty_testu/OSTATNI_RAPORT.md`.
- Commity kończą się stopkami `Co-Authored-By` i `Claude-Session`.
- Logikę geometryczną (wskazówki, kształty, DXF) trzymamy w czystych modułach bez Tk/OCCT (`sketch_inference`, `sketch_shapes`, `dxf_import`) — da się je przetestować poza aplikacją; testy UI są w `test_sketch_tools()`.
- **Licencje i cudze pomysły:** czerpiemy tylko ogólne idee interfejsu; nie kopiujemy cudzego kodu, grafik, tekstów ani nazw produktów (por. `ZRODLA_I_LICENCJE.md`). Nowa zewnętrzna zależność lub zasób = wpis na tamtej liście.
