// Dziennik operacji: krótkie wpisy „co użytkownik zrobił i co z tego wyszło" (narzędzia, rozciąganie, układy L/U, konfiguracja,
// biblioteka, szkice, przejścia między zakładkami) oraz zrzut stanu mebli (położenie, obrót, odstępy między sąsiadami).
// Wpisy trafiają do zwykłego logu aplikacji (zakładka Logi), z przedrostkiem [operacja:kategoria].
#pragma once
#include <string>
#include "core.h"

class App;

namespace journal {

void event(App* app, const std::string& category, const std::string& message);

// Podsumowanie renderu: rysowania zbierane są w oknach co ok. 1 s (liczba, średni i najdłuższy czas) wraz z silnikiem, jakością, liczbą ścian,
// kamerą i trybem; wolne rysowania (> 150 ms) są dodatkowo zapisywane osobno.
void draw_done(App* app, double milliseconds);

// Operacja na pliku (nowy, otwarcie, zapis, eksport…): ścieżka, czas, wynik.
void file_event(App* app, const std::string& action, const std::string& path, double milliseconds, const std::string& result);

// Wypisuje każdą grupę (mebel): zakres X/Y/Z, obrót, kierunek frontu, a potem odstępy między sąsiadującymi meblami (≤ 250 mm).
void dump_groups(App* app, const core::Project& project, const std::string& label);

}  // namespace journal
