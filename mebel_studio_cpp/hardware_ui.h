// Dockable sourced planning calculators and handle alternatives.
//
// Zależności jeszcze nieprzetłumaczone (założone interfejsy): studio_ui
// (selected, safe, commit), runners_catalog (PROFILES, get_profile,
// calculate_runner), runner_installation (drawer_depth_context, apply_runner),
// opening_options (OPTIONS, install z pełną listą argumentów).
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace hardware_ui {

using json = core::json;

// (holder, form, viewport, bind_children)
struct ScrollForm {
    ttk::Frame* holder = nullptr;
    ttk::Frame* form = nullptr;
    tk::Canvas* viewport = nullptr;
    std::function<void()> bind_children;
};

ScrollForm _scroll_form(tk::Widget* parent, int width = 330);

class RunnerDialog : public dock_workspace::DockDocument {
public:
    explicit RunnerDialog(App* app);

    std::optional<std::string> part_id;
    json profiles;
    tk::StringVar choice, nominal, confirmed, summary, note;
    std::vector<std::pair<std::string, tk::StringVar>> vars;  // dict z kolejnością wstawiania
    ttk::Combobox* length_combo = nullptr;
    ttk::Entry* confirmed_entry = nullptr;
    ttk::Notebook* result_tabs = nullptr;
    tk::Canvas* form_viewport = nullptr;
    json last_plan = nullptr;

    json profile();
    void choose();
    void calculate();
    json confirmed_depth();
    void source();
    void apply();

private:
    tk::StringVar& var(const std::string& key);
};

class OpeningDialog : public dock_workspace::DockDocument {
public:
    explicit OpeningDialog(App* app);

    std::string part_id;
    json options;
    tk::StringVar choice, note, spring, edge;
    std::vector<std::pair<std::string, tk::StringVar>> fields;
    std::vector<std::pair<std::string, std::string>> spring_labels, edge_labels;

    std::string key();
    void describe();
    void source();
    void apply();
};

dock_workspace::DockDocument* material_dialog(App* app);

void export_hardware(App* app);

void install(App* app);

}  // namespace hardware_ui
