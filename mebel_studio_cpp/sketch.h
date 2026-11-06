// Interactive local U/V sketching; shared Part remains the production source.
//
// Różnice względem Pythona:
//  * `from native_model import install_shapes` nie istnieje — normalized_part to
//    skompilowany C++; transform_points / constrain_points wołają natywną DLL tylko
//    wtedy, gdy native_model::available(), tak jak w oryginale.
//  * cadquery offset2D(kind='intersection') jest realizowane bezpośrednio przez
//    BRepOffsetAPI_MakeOffset (GeomAbs_Intersection) z OpenCascade.
//  * Atrybuty dopisywane dynamicznie przez sketch_live.install (size_vars, live_canvas,
//    live_note, auto_fit, preview_job, viewport_preferences_changed) są zwykłymi
//    składowymi; "hasattr" odpowiada sprawdzeniu pustości / nullptr.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core.h"
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;
namespace quick_studio_ui { class StudioCanvas; }

namespace sketch {

using json = core::json;
using Point = core::Point;

core::Part normalized_part(const core::Part& part, const std::vector<Point>& points);
// next(p for p in project.parts if p.id == id) — py::StopIteration, gdy brak.
core::Part& find_part(core::Project& project, const std::string& id);

class Sketch : public dock_workspace::DockDocument {
public:
    using Segment = std::pair<Point, Point>;
    struct Snapshot { std::vector<Point> points; bool closed; core::Part part; };

    std::optional<core::Part> original;
    core::Part part;
    std::vector<Point> points;
    bool closed = false;
    std::vector<Snapshot> history;
    json config;
    std::optional<Point> cursor;
    double scale = 0.7;
    std::array<double, 2> origin{65, 540};

    tk::StringVar mode;
    tk::BooleanVar snap, ortho, point_snap;
    std::string snap_reason;
    // (id(project), target, role, level, plane, t) -> odcinki prowadzące
    using GuideKey = std::tuple<const void*, std::string, std::string, std::string, std::string, std::string>;
    std::optional<std::pair<GuideKey, std::vector<Segment>>> guide_cache;
    tk::StringVar grid, target, role, hinge_side, level, plane, u, v, length, angle, status;
    tk::BooleanVar automatic, replace_existing;
    std::map<std::string, tk::StringVar> fields;  // name, group, t

    tk::Canvas* canvas = nullptr;
    tk::Listbox* listbox = nullptr;
    ttk::Combobox* role_combo = nullptr;
    ttk::Combobox* hinge_combo = nullptr;
    ttk::Combobox* plane_combo = nullptr;
    ttk::Combobox* material = nullptr;
    ttk::Entry* level_entry = nullptr;
    std::pair<int, int> pan_anchor{0, 0};

    // --- dopisywane przez sketch_live::install
    std::optional<std::string> preview_job;
    std::map<std::string, tk::StringVar> size_vars;
    std::optional<tk::BooleanVar> auto_fit;
    quick_studio_ui::StudioCanvas* live_canvas = nullptr;
    tk::StringVar live_note;
    std::function<void()> viewport_preferences_changed;

    Sketch(App* app, const core::Part* part = nullptr);

    json _document_model() const override;
    void role_changed();
    void update_controls();
    json current_config();
    void recipe_settings();
    void fit_to_furniture();
    // Zwraca (projekt kandydujący, id elementu w tym projekcie).
    std::pair<core::ProjectPtr, std::string> candidate();
    dock_workspace::VisualizationDocument* preview();
    double number(const tk::StringVar& var);
    void checkpoint();
    bool cancel();
    void change_mode();
    std::pair<double, double> screen(const Point& p) const;
    std::vector<Segment> guide_geometry();
    Point local(const tk::Event& event);
    void motion(const tk::Event& event);
    void click(const tk::Event& event);
    void add_point(const Point& p);
    void numeric_point();
    void segment();
    void select_point();
    void edit_point();
    void close_contour();
    void undo();
    void clear();
    void update_list();
    void fit();
    void zoom(const tk::Event& event);
    void pan_start(const tk::Event& event);
    void pan(const tk::Event& event);
    void label(double x, double y, const std::string& text, const std::string& color = "#24586c");
    void draw();
    bool new_element(bool discard_confirmed = false);
    void set_sketch_points(const std::vector<Point>& points);
    void transform_sketch(const std::string& action);
    void offset_sketch();
    void constrain_edge(const std::string& kind);
    bool save(bool keep_open = false);
};

}  // namespace sketch
