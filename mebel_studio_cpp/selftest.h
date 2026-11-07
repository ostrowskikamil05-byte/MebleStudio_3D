// Pełny test programu: przechodzi po wszystkich funkcjach (meble z biblioteki, konfigurator, rozciąganie, układy L/U, narożniki,
// układ i magnes, okucia, biblioteka, eksporty, zapis/odczyt, cofanie, scena pokoju, widok, zakładki, okna dialogowe) i zapisuje
// pełny raport (Markdown + JSON + log) wraz z audytem plików źródłowych względem oryginału w Pythonie.
// Test nie zapisuje ustawień programu (preferencje trafiają do katalogu tymczasowego) i nie dotyka żadnego projektu użytkownika.
#pragma once
#include <string>

class App;

namespace selftest {

// Tryb wiersza poleceń: MebelStudio3D_cpp.exe --full-test [katalog_raportu] [--pause]. Zwraca 0, gdy brak błędów (ostrzeżenia nie liczą się jako błąd).
int run_cli(const std::string& report_dir, bool pause_at_end = false);

// Przycisk w Logach: uruchamia pełny test jako osobny proces programu (osobny katalog raportu), żeby nie ruszać otwartego projektu.
void launch_in_new_process(App* app);

}  // namespace selftest
