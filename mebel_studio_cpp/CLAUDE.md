# Zasady pracy nad Mebel Studio 3D (C++)

- Odpowiedzi dla użytkownika po polsku. Gałąź robocza: `migracja-cpp` (do `main` dopiero na polecenie).
- **Każda zmiana zachowania programu musi mieć (1) wpis w dzienniku i (2) test.**
  - Dziennik: `journal::event(app, "kategoria", "opis")` w miejscu akcji użytkownika (kliknięcie, zatwierdzenie, odrzucenie z powodem).
    Zmiany projektu opisuje automatycznie `[operacja:zmiana]` (hak w `App::refresh`, źródło = plik:linia ostatniego `App::snapshot`).
  - Test: przypadek w `selftest.cpp` — dla dziennika w `test_journal_actions()` (znacznik `mark()` / `since()` czyta fragment logu dopisany w trakcie przypadku);
    nowe kategorie dopisz też do listy w `test_log_coverage()`.
- Nic nie może się psuć po cichu: gdy akcja jest blokowana albo odrzucana, w dzienniku ma być powód (por. `allow_press`, brak uchwytów rozciągania).
- Komendy: `./dev.sh fulltest` (`PULL=0 ./dev.sh fulltest`), raport w `raporty_testu/OSTATNI_RAPORT.md`.
- Commity kończą się stopkami `Co-Authored-By` i `Claude-Session`.
