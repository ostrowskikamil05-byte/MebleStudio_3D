// Edycja obróbki części: kieszenie, rowki, wycięcia i szyki wierceń.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace machining_editor {

using json = core::json;

class MachiningEditor : public dock_workspace::DockDocument {
public:
    MachiningEditor(App* app, const core::Part& part);

    core::Part part;
    std::optional<int> editing_cut;
    ttk::Notebook* controls = nullptr;
    ttk::Frame* cut_tab = nullptr;
    tk::StringVar kind, shape, cut_face, drill_face, status;
    std::vector<std::pair<std::string, tk::StringVar>> cut_vars, drill_vars;  // dict z kolejnością wstawiania
    ttk::Treeview* table = nullptr;
    tk::Canvas* canvas = nullptr;

    void edit_cut();
    std::vector<std::pair<std::string, double>> values(const std::vector<std::pair<std::string, tk::StringVar>>& variables);
    void add_cut();
    void add_drills();
    void refresh();
    void delete_selected();
    void draw();
    void save();
};

}  // namespace machining_editor
