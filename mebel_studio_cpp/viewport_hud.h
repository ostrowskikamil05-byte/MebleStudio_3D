// Nakładka w widoku 3D w stylu "Forma": pływająca paleta narzędzi, przełączniki u góry
// i przełącznik widoków u dołu. Rysowana na płótnie (a więc także w silnikach natywnych).
#pragma once
#include "core.h"
#include "tk.h"

class App;

namespace viewport_hud {

// Rysuje nakładkę na końcu rysowania widoku; zapamiętuje obszary klikalne.
void paint(App* app, double width, double height, const core::json& grid_spacing);

// Kliknięcie w element nakładki: wykonuje akcję i zwraca true (zdarzenie obsłużone).
bool press(App* app, const tk::Event& event);

// true, dopóki trwa przeciąganie rozpoczęte na nakładce (kamera ma wtedy nie reagować).
bool captured();

// Puszczenie przycisku; true, gdy zdarzenie należało do nakładki.
bool release();

}  // namespace viewport_hud
