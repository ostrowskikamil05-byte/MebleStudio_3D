// Okienko wyboru wysuwane po najechaniu na pozycję lewego menu (Kształty i style, Materiały, LED, Nóżki,
// Zawiasy, Produkcja…). Pojawia się tuż po prawej stronie lewego panelu — nad widokiem 3D — więc można do niego
// wjechać myszką; po zjechaniu poza pozycję i okienko samo się chowa.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core.h"
#include "tk.h"

class App;

namespace hover_flyout {

using json = core::json;
using Builder = std::function<void(tk::Widget* body)>;

struct Action {
    std::string label;
    std::string hint;
    std::function<void()> run;
};

// Po najechaniu na `trigger` pokazuje okienko z tytułem i zawartością zbudowaną przez `builder`.
void attach(App* app, tk::Widget* trigger, const std::string& title, Builder builder);
// Zawartość: karty katalogu podanych rodzajów (np. {"hinge","runner"}); kliknięcie karty dodaje ją do projektu.
Builder catalogue(App* app, const std::vector<std::string>& kinds);
// Zawartość: lista poleceń z krótkim opisem.
Builder actions(std::vector<Action> items);

// Pokazuje okienko od razu (bez myszy) — dla testów i skrótów. Chowa się, gdy wskaźnik jest poza nim.
void show_now(App* app, const std::string& title, Builder builder, tk::Widget* trigger = nullptr);
void hide(App* app);
bool visible();
// Liczba kart/poleceń w ostatnio zbudowanym okienku (testy).
int last_item_count();
// Klika n-tą kartę/polecenie ostatnio zbudowanego okienka (testy); zwraca false, gdy nie ma takiej pozycji.
bool click_item(int index);

}  // namespace hover_flyout
