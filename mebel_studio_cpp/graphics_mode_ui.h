// Przełącznik trybu grafiki (Klasyczna / Realistyczna) z miniaturami — używany w panelu „Silnik i jakość 3D”,
// w panelu CAD, w oknie szkicownika i w edytorze krawędzi. Wszystkie przełączniki pokazują ten sam, globalny tryb.
#pragma once
#include <functional>
#include <string>

#include "render_style.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace graphics_mode_ui {

// Ustawia tryb grafiki: zapis ustawień, wpis w dzienniku ([operacja:grafika]), odświeżenie widoku 3D i wszystkich przełączników.
// `source` mówi, skąd przyszła zmiana (np. „panel CAD”). Zwraca false, gdy tryb już był ustawiony.
bool set_mode(App* app, render_style::Mode mode, const std::string& source);

// Rysuje miniaturę trybu na płótnie (znacznik „mode_thumb”).
void draw_thumbnail(tk::Canvas* canvas, render_style::Mode mode, int width, int height);

// Wstawia do `parent` dwa kafelki z miniaturami i nazwami trybów; zwraca ramkę z kafelkami.
ttk::Frame* add_switch(tk::Widget* parent, App* app, const std::string& source);

// Wersja zwarta (jeden rząd segmentów „Klasyczna | Realistyczna”) do pasków narzędzi i zakładek.
tk::Frame* add_compact(tk::Widget* parent, App* app, const std::string& source);

// Wywoływane po każdej zmianie trybu (np. okno szkicownika odświeża własny podgląd 3D). Zwrócenie false wyrejestrowuje nasłuch.
void add_listener(std::function<bool()> listener);

}  // namespace graphics_mode_ui
