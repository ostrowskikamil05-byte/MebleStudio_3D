// Profile editor and curved furniture library using the common project model.
//
// Różnica względem Pythona: atrybuty dopisywane do DockDocument (d.shape_vars,
// d.shape_apply, d.style_table, ...) są składowymi klas ShapeDialog / StyleLibrary.
// Opcje nazwane (radii, segments, arch_rise, taper) są przekazywane jako json.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace style_ui {

class ShapeDialog : public dock_workspace::DockDocument {
public:
    ShapeDialog(App* app, const core::Part& part);
    core::Part p;
    // d.shape_vars — kolejność wstawiania ma znaczenie przy walidacji
    std::vector<std::pair<std::string, tk::StringVar>> shape_vars;
    tk::StringVar shape_choice, shape_flute_face;
    tk::BooleanVar shape_edge, shape_flute;
    std::function<void()> shape_apply;
    tk::Canvas* shape_canvas = nullptr;
    ttk::Button* shape_submit_button = nullptr;
    tk::StringVar& shape_var(const std::string& key);
};

class StyleLibrary : public dock_workspace::DockDocument {
public:
    explicit StyleLibrary(App* app);
    std::map<std::string, tk::StringVar> style_vars;
    ttk::Treeview* style_table = nullptr;
    std::function<void()> style_add;
    ttk::Button* style_create_button = nullptr;
};

ShapeDialog* shape_dialog(App* app);
StyleLibrary* style_library(App* app);
void install_menu(App* app);

}  // namespace style_ui
