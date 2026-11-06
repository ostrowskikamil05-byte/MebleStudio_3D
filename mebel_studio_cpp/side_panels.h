// Prawy panel kontekstowy (jak na makiecie): zamiast otwierać okna, narzędzia pokazują swoje opcje
// w miejscu panelu "Właściwości" — np. "Zaokrąglenia" albo "Silnik i jakość 3D".
#pragma once
#include <functional>
#include <string>
#include "tk.h"
#include "ttk.h"

class App;

namespace side_panels {

// Pokazuje panel kontekstowy o kluczu `key`; builder wypełnia ciało panelu.
void show(App* app, const std::string& key, const std::string& title, std::function<void(tk::Widget* body)> builder);
// Wraca do panelu "Właściwości".
void hide(App* app);
bool is_open(const std::string& key);

void show_fillet(App* app);
void show_engine(App* app);

// Zaznaczanie krawędzi w widoku 3D, gdy panel "Zaokrąglenia" jest otwarty (tryb Kanty).
bool fillet_active();
void toggle_edge(App* app, const std::string& edge);

// Wiersze listy w panelu po lewej, które świecą się, gdy odpowiadający im panel jest otwarty.
void register_row(const std::string& key, ttk::Button* row);

}  // namespace side_panels
