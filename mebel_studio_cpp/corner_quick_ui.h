// Compact persistent corner inspector; no extra window or modal grab.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "tk.h"
#include "ttk.h"

class App;
namespace quick_studio_ui { class ScrollPanel; }

namespace corner_quick_ui {

using json = core::json;

class CornerPanel : public ttk::Frame {
public:
    App* app;
    std::optional<std::string> group;
    json config = json::object();
    bool syncing = false;
    std::optional<std::string> job;
    int generation;
    std::vector<std::pair<std::string, tk::StringVar>> fields;  // dict: klucz -> zmienna, w kolejności wstawiania
    json mapping = json::object();                              // {klucz: {etykieta: wartość}}
    std::optional<std::string> signature;
    bool has_draft = false;
    tk::StringVar note;
    ttk::Button* apply_button = nullptr;
    quick_studio_ui::ScrollPanel* scroll = nullptr;
    tk::Widget* body = nullptr;
    tk::Canvas* preview = nullptr;

    CornerPanel(tk::Widget* parent, App* app);
    void field(const std::string& key, const std::string& label);
    void combo(const std::string& key, const std::string& label, const json& choices);
    void set_config(const json& config);
    json draft();
    void cancel_pending();
    void destroyed(tk::Event& event);
    void changed();
    void quick_shape(const std::string& shape);
    void draw_preview();
    void refresh();
    bool commit(core::ProjectPtr result, const std::string& group);
    bool apply();
    bool flush_draft();
    bool convert_selected();
    bool new_corner();

private:
    tk::StringVar& var(const std::string& key);
    std::string label_for(const std::string& key, const json& value);
};

}  // namespace corner_quick_ui
