// One persistent inspector and a compact ribbon for cabinet configuration.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "depth_raster.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace quick_studio_ui {

using json = core::json;

// app.canvas: zwykłe tk.Canvas, do którego moduły renderujące dopisują atrybuty
// (canvas._depth_buffer, canvas._active_renderer, ...).
class StudioCanvas : public tk::Canvas, public depth_raster::CanvasState {
public:
    StudioCanvas() = default;
    StudioCanvas(tk::Widget* parent, const tk::Opts& options = {}) : tk::Canvas(parent, options) {}
    std::string _active_renderer = "cpu";
};

class ScrollPanel : public ttk::Frame {
public:
    explicit ScrollPanel(tk::Widget* parent);
    tk::Canvas* canvas = nullptr;
    ttk::Frame* body = nullptr;
    int window = 0;

    void install_wheel(tk::Widget* parent);
    void resize(tk::Event& event);
    std::string wheel(tk::Event& event);
};

class Section : public ttk::Frame {
public:
    Section(tk::Widget* parent, const std::string& title, bool opened = false);
    bool opened;
    ttk::Button* toggle = nullptr;
    ttk::Frame* body = nullptr;
    std::string title;

    void show();
    void flip();
};

class QuickInspector : public ttk::Frame {
public:
    // Pole formularza: (zmienna, rodzaj) — rodzaj: text | int | number | choice | bool.
    struct Field {
        std::string key;
        std::shared_ptr<tk::StringVar> text;   // text / int / number / choice
        std::shared_ptr<tk::BooleanVar> flag;  // bool
        std::string kind;
    };

    QuickInspector(tk::Widget* parent, App* app);

    App* app;
    bool syncing = false;
    std::optional<std::string> job;
    std::optional<std::string> group;
    int generation;
    json config = json::object();
    std::vector<Field> fields;  // dict w kolejności wstawiania
    // key -> (widget, {etykieta: wartość}); kolejność etykiet = kolejność wstawiania
    std::map<std::string, std::pair<ttk::Combobox*, json>> combos;
    std::map<std::string, Section*> sections;
    bool has_draft = false;
    std::vector<int> template_indices;
    json saved = json::object();
    std::optional<json> clipboard;
    tk::StringVar note, groupvar, templatevar, presetvar, runner_note;
    ttk::Combobox *groupcombo = nullptr, *templatecombo = nullptr, *presetcombo = nullptr;
    ttk::Button* apply_button = nullptr;
    ScrollPanel* scroll = nullptr;
    Section* type_section = nullptr;
    tk::Canvas* preview = nullptr;

    void draft_changed();
    void reset_fields();
    bool flush_draft();
    tk::StringVar* field(tk::Widget* parent, const std::string& key, const std::string& label, bool integer = false, bool text = false);
    ttk::Combobox* combo(tk::Widget* parent, const std::string& key, const std::string& label, const json& choices);
    void choice_changed(const std::string& key);
    void check(tk::Widget* parent, const std::string& key, const std::string& label);
    void section_fields(tk::Widget* parent, const std::string& key);
    void cancel_pending();
    void schedule();
    void choose_group();
    void change_template();
    json draft();
    void set_config(const json& cfg);
    void refresh();
    bool apply(bool explicit_ = false);
    std::optional<std::string> new_furniture();
    bool quick_front(const std::string& mode, int count);
    void duplicate_group();
    void delete_group();
    void copy_settings();
    void paste_settings();
    void reload_presets();
    void save_preset();
    void load_preset();
    void delete_preset();
    void draw_preview();

private:
    const Field& find(const std::string& key) const;
};

void build(App* app);
void focus_section(App* app, const std::string& key);
void build_production(App* app);
// quick_studio_ui._button: przycisk wstążki (używany też przez workspace_controls).
ttk::Button* _button(tk::Widget* parent, const std::string& label, std::function<void()> command, bool accent = false);
// Podpis grupy na wstążce (np. "WYPOSAŻENIE"); kolejne przyciski należą do tej grupy.
ttk::Label* _group(tk::Widget* parent, const std::string& title);

}  // namespace quick_studio_ui
