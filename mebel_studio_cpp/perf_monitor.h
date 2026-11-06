// Profiler interfejsu: mierzy procedury obsługi zdarzeń, polecenia Tcl, blokady pętli zdarzeń i przełączanie zakładek.
// Wolne rzeczy trafiają do panelu "Logi developerskie" (WARNING), a pełny ranking daje "Raport wydajności".
#pragma once
#include <chrono>
#include <string>

#include "tk.h"
#include "ttk.h"

class App;

namespace perf {

// Instaluje hook w tkcore, pętlę kontrolną i zegar startu. Wołać po developer_logs::install.
void install(App* app);

// Czas bloku kodu (RAII). Bloki wolniejsze niż próg trafiają do logu, wszystkie do raportu.
class Scope {
public:
    explicit Scope(const char* name);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point started_;
};

// Mierzy czas od zmiany zakładki do chwili, gdy interfejs jest bezczynny (układ i rysowanie zakończone).
void watch_notebook(App* app, ttk::Notebook* notebook, const std::string& name);

// Wypisuje ranking do logu (najdłuższe sumarycznie czynności, liczba poleceń Tcl).
void report(App* app);
// Przełącza po kolei wszystkie zakładki i strony paneli, mierząc każde przełączenie; wynik trafia do logu.
void self_test(App* app);

// Przełącza na widok 3D i mierzy koszt odrysowania płótna (obraz z alfa vs bez), żeby wskazać źródło wielosekundowych postojów Tk.
void diagnose_redraw(App* app);

// Odrysowuje po kolei każdy widoczny widget (zdarzenie Expose) i mierzy czas: wskazuje klasy i konkretne widgety, które najwięcej kosztują.
void diagnose_widgets(App* app);
// Zeruje statystyki.
void reset();

}  // namespace perf
