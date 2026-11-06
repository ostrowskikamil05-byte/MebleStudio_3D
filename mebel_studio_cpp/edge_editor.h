#pragma once
#include <optional>
#include <string>
#include <vector>
#include "cad_geometry.h"
#include "core.h"
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace edge_editor {

using json = core::json;

class EdgeEditor : public dock_workspace::DockDocument {
public:
    App* app;
    core::Part part;              // obiekt części przekazany przy otwarciu
    std::string attribute, parameter;
    json values = json::object();  // {krawędź: wartość}, w kolejności wstawiania
    std::vector<cad_geometry::EdgeSpec> specs;
    tk::StringVar summary, radius;
    ttk::Treeview* table = nullptr;
    tk::Canvas* canvas = nullptr;
    core::Part candidate;

    EdgeEditor(App* app, const core::Part& part, std::optional<cad_geometry::Edge> edge = std::nullopt, const std::string& mode = "fillet");
    void refresh();
    void select(tk::Event* event = nullptr);
    void set_radius();
    void reset();
    void draw();
    // Chronione przez ensure_current_project() (DockDocument.__init_subclass__).
    bool save();
    json _document_model() const override;
};

}  // namespace edge_editor
