// Persistent, searchable graphic catalogue and explicit backend action routing.
//
// Różnice względem Pythona:
//  * karty katalogu (dict) są wartościami json, tak jak w oryginale;
//  * callback on_select zwraca bool (False w Pythonie = false; None/True = true);
//  * funkcja show() nazywa się tak samo jak w Pythonie (launcher używa show);
//  * app.visual_catalogue(kind, on_select) to std::function w App.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "form_dialog.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace visual_catalogue {

using json = core::json;

extern const std::vector<std::pair<std::string, std::string>> KINDS;
extern const std::vector<std::pair<std::string, std::string>> ALIASES;
using OnSelect = std::function<bool(const json&)>;

std::string canonical_kind(const std::string& kind);

// Return independent card descriptors; dimensions are mm, no CAD work.
json catalogue_items(const std::string& kind = "furniture", const core::Project* project = nullptr);
inline json catalogue_items(const std::string& kind, const core::Project& project) { return catalogue_items(kind, &project); }

std::string search_text(const std::string& text);  // _search
json filter_items(const json& items, const std::string& query = "", const std::string& category = "Wszystkie");
std::string dimension_text(const json& item);

// Return to the existing main 3D page without closing the catalogue.
void select_design(App* app);
// Use existing transactional factory/controller commands for a card.
bool apply_item(App* app, const json& item);

class VisualCatalogue : public ttk::Frame {
public:
    App* app;
    form_dialog::Form* form;
    std::string kind;
    OnSelect on_select;
    std::optional<json> selected;
    std::optional<std::string> job;
    int columns = 0;
    std::map<std::string, tk::Widget*> cards;
    json items = json::array();
    int generation;
    tk::StringVar kindvar, query, category, note, count;
    ttk::Combobox* kindcombo = nullptr;
    ttk::Entry* search = nullptr;
    ttk::Combobox* categorycombo = nullptr;
    ttk::Button* choosebutton = nullptr;
    ttk::Button* sourcebutton = nullptr;
    tk::Canvas* canvas = nullptr;
    ttk::Frame* gridbody = nullptr;
    int window = 0;

    VisualCatalogue(form_dialog::Form* parent, App* app, const std::string& kind = "furniture", OnSelect on_select = nullptr);
    std::string wheel(tk::Event& event);
    void destroyed(tk::Event& event);
    void schedule();
    void resize(tk::Event& event);
    void set_kind(const std::string& kind, OnSelect on_select = nullptr);
    void render();
    bool activate_selected();
    void open_source();
    bool activate(const json& item);
};

// Reuse one live dock tab; callbacks receive a complete card descriptor.
VisualCatalogue* show(App* app, const std::string& kind = "furniture", OnSelect on_select = nullptr);

// Expose the catalogue through explicit discoverable ribbon commands.
void install(App* app);

}  // namespace visual_catalogue
