// Furniture, fittings, palette and motion dialogs.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "dock_workspace.h"
#include "form_dialog.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace studio_ui {

using json = core::json;

// app.snapshot(); app.project = project; app.refresh(); app.fit()
void commit(App* app, const core::ProjectPtr& project);

// Zaznaczony element projektu (ValueError, gdy nic nie wybrano).
core::Part& selected(App* app);

// Błędy walidacji trafiają do okna „Sprawdź ustawienia”.
std::function<void()> safe(App* app, std::function<void()> fn);

form_dialog::Form* library_dialog(App* app);
form_dialog::Form* palette_dialog(App* app);
form_dialog::Form* motion_dialog(App* app);

class FittingsDialog : public dock_workspace::DockDocument {
public:
    explicit FittingsDialog(App* app);

    App* app;
    core::Part part;
    std::vector<std::pair<std::string, ttk::Frame*>> pages;  // słownik w kolejności tworzenia
    tk::StringVar choice, info, face, hinge, side, sys_face, handle, finish, connector;
    tk::StringVar hinge_count, handle_u, handle_v, connector_front, connector_back;
    tk::BooleanVar both, vertical;
    std::vector<std::pair<std::string, tk::StringVar>> fields;
    std::vector<std::pair<std::string, tk::StringVar>> sys_fields;
    bool fields_ready = false;  // hasattr(self, 'fields')

    // Keep actions visible while tall fitting forms scroll in a tab.
    void scroll_pages();
    void describe();
    void source();
    void update_project(const core::ProjectPtr& result);
    void drill();
    void hinges();
    void system();
    void handles();
    void connectors();
    void runners();
    void cargo();
};

void install(App* app);

}  // namespace studio_ui
