// Nakładka w widoku 3D w stylu "Forma": pływająca paleta narzędzi, przełączniki u góry
// i przełącznik widoków u dołu. Rysowana na płótnie (a więc także w silnikach natywnych).
#pragma once
#include "core.h"
#include "tk.h"

class App;

namespace viewport_hud {

// Rysuje nakładkę na końcu rysowania widoku; zapamiętuje obszary klikalne.
void paint(App* app, double width, double height, const core::json& grid_spacing);

// Wybór narzędzia z palety (tryb: camera, layout, furniture, resize, configure, tray, rotate, dimensions…) — to samo, co kliknięcie w kafelek;
// zapisuje w dzienniku kliknięcie i stan interakcji po przełączeniu.
void activate_tool(App* app, const std::string& mode, const std::string& label);

// Kliknięcie w element nakładki: wykonuje akcję i zwraca true (zdarzenie obsłużone).
bool press(App* app, const tk::Event& event);

// true, dopóki trwa przeciąganie rozpoczęte na nakładce (kamera ma wtedy nie reagować).
bool captured();

// Puszczenie przycisku; true, gdy zdarzenie należało do nakładki.
bool release();

}  // namespace viewport_hud
