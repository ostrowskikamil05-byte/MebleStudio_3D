// Małe widgety w stylu makiety: przełącznik on/off, segmenty, próbki kolorów, wiersz pola z jednostką.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "tk.h"
#include "ttk.h"

namespace forma_widgets {

using Command = std::function<void()>;

// Wiersz: etykieta po lewej, przełącznik po prawej. Zmienna jest źródłem prawdy (zmiana z kodu odświeża rysunek).
tk::Frame* switch_row(tk::Widget* parent, const std::string& text, tk::BooleanVar var, Command on_change = nullptr);
// Segmenty (np. Niska | Średnia | Wysoka); zmienna trzyma etykietę wybranego segmentu.
tk::Frame* segmented(tk::Widget* parent, const std::vector<std::string>& labels, tk::StringVar var, Command on_change = nullptr);
// Próbki kolorów w rzędzie; zmienna trzyma indeks (jako tekst) wybranej próbki.
tk::Frame* swatches(tk::Widget* parent, const std::vector<std::string>& colors, tk::StringVar var, Command on_change = nullptr);
// Pogrubiony podpis sekcji w panelu.
ttk::Label* caption(tk::Widget* parent, const std::string& text);
// Wiersz: etykieta, pole, jednostka ("mm"). Zwraca pole.
ttk::Entry* field_row(tk::Widget* parent, const std::string& label, tk::StringVar var, const std::string& unit = "mm", bool readonly = false);

}  // namespace forma_widgets
