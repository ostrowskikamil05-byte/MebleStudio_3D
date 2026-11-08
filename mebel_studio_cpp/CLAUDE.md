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
- **Miniaturki:** każdy nowy element wybierany z listy lub karty (mebel, kształt, AGD, nóżki, zawiasy, prowadnice, uchwyty, LED, materiały, profile w szkicu, opcje w kafelkach) musi mieć miniaturkę — rysowaną z tych samych parametrów co element (`catalogue_previews`, `tile_glyphs`, `sketch_shapes::thumbnail_points`). Testy `miniaturki: …` sprawdzają, że żaden element nie ma pustej, zdegenerowanej ani nienarysowanej miniaturki; nowy rodzaj elementu = dopisz go do tych testów.
- **Porządek w repozytorium:** w korzeniu są tylko `mebel_studio_cpp/` (program + samotesty + narzędzia), `biblioteki_natywne/`, `app/` (wzorzec Python do testu zgodności), `przyklady/`, `licencje/` i `README.md`. Wszystko spoza programu (stara paczka Pythona, dokumentacja i zrzuty starych wersji, pliki demo) usunięte — kopia w gałęzi `archiwum-python-0.15`. Test „układ repozytorium” pilnuje, żeby to nie wróciło.
- **Porządek w repozytorium:** w korzeniu są tylko `mebel_studio_cpp/` (program + samotesty + narzędzia), `biblioteki_natywne/`, `app/` (wzorzec Python do testu zgodności), `przyklady/`, `licencje/` i `README.md`. Wszystko spoza programu (stara paczka Pythona, dokumentacja i zrzuty starych wersji, pliki demo) usunięte — kopia w gałęzi `archiwum-python-0.15`. Test „układ repozytorium” pilnuje, żeby to nie wróciło.
