// Biblioteka w lewym panelu (jak na makiecie): wyszukiwarka, kategorie z miniaturami
// i przycisk "Zobacz wszystkie". Wybór karty od razu wykonuje akcję katalogu
// (ta sama logika co okno "Katalog graficzny"), bez otwierania nowej zakładki.
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include "core.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace quick_studio_ui { class ScrollPanel; }

namespace library_panel {

using json = core::json;

class LibraryPanel : public ttk::Frame {
public:
    LibraryPanel(tk::Widget* parent, App* app);

    // Przełącza rodzaj katalogu (furniture, material, hinge, runner, handle, leg, led, appliance) i pokazuje panel.
    void show_kind(const std::string& kind);
    // Przebudowuje listę dla bieżącego rodzaju (np. po zmianie projektu).
    void refresh();

private:
    void load_items();
    void render();
    void schedule();
    void render_later();
    void activate(const json& item);

    App* app_;
    std::string kind_ = "furniture";
    json items_ = json::array();
    int generation_ = -1;
    tk::StringVar query_, note_;
    std::map<std::string, ttk::Button*> chips_;
    ttk::Entry* entry_ = nullptr;
    quick_studio_ui::ScrollPanel* scroll_ = nullptr;
    std::set<std::string> expanded_;
    std::optional<std::string> job_;
    std::string selected_id_;
};

}  // namespace library_panel
