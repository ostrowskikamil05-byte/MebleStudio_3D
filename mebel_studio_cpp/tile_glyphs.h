// Miniatury (kafelki) wyboru ustawień mebla: uchwyty, nóżki, zawiasy, fronty, LED itp. Wspólne dla panelu Właściwości
// oraz okien „Konfiguruj” i wyboru po rozciągnięciu.
#pragma once
#include <string>

namespace tk { class Canvas; }

namespace tile_glyphs {

// Rysuje kafelek (ramka, miniatura wg `key` i `choice`, podpis `label`) w `canvas`; `on` = zaznaczony.
// `color` (opcjonalnie): kolor próbki dla materiałów.
void draw(tk::Canvas* canvas, const std::string& key, const std::string& choice, const std::string& label, bool on, const std::string& color = "");

}  // namespace tile_glyphs
