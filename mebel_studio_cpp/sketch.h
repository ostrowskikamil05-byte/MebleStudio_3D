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
#include "sketch_design.h"
#include "sketch_inference.h"
#include "tk.h"
#include "ttk.h"

class App;
namespace quick_studio_ui { class StudioCanvas; }

namespace sketch {

using json = core::json;
using Point = core::Point;

core::Part normalized_part(const core::Part& part, const std::vector<Point>& points);
std::string design_role(const core::Part& part);  // funkcja elementu przy przekazywaniu kantów (lewy i prawy bok = „side”)
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
    // --- narzędzia rysowania: wskazówki pod kursorem, pole wartości, okrąg/wielobok/łuk, profile
    sketch_inference::Result inference;           // ostatnia wskazówka pod kursorem
    std::optional<Point> arrow_lock;              // kierunek zablokowany strzałką (wektor jednostkowy)
    std::vector<Point> pending;                   // punkty rozpoczętego okręgu / wieloboku / łuku
    tk::Canvas* profile_preview = nullptr;  // miniaturka wybranego profilu w panelu „Szybkie profile”
    void draw_profile_preview();
    tk::StringVar value_text, value_prompt, value_live, sides, profile_name, profile_width, profile_height, profile_param, profile_param_label;
    std::vector<Point> centers;                   // środki narysowanych okręgów (wskazówka „środek okręgu”)
    std::vector<Segment> guides;                  // linie pomocnicze (z miarki), widoczne jako przerywane i łapane przez wskazówki
    std::optional<Segment> measured;              // ostatni pomiar miarką
    std::optional<size_t> drag_index;             // przeciągany punkt (narzędzie Przesuń)
    std::optional<Point> drag_origin;
    bool drag_moved = false;
    bool imported_holes = false;                  // part.holes pochodzą z importu DXF (okręgi wewnątrz obrysu)
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

    // --- kanty, wzorki i zestaw elementów (projektowanie całego mebla w jednym oknie)
    struct EdgeAssign { std::string style; double size = 0; int segment = -1; };   // segment −1 = wszystkie boki konturu
    struct PatternAssign { std::string id, face; std::map<std::string, double> params; };
    struct Staged {
        std::string name, group, thickness, role, plane, level, hinge_side;
        std::vector<Point> points;
        core::Part part;
        int material_index = 0;
        bool automatic = false;
        std::vector<EdgeAssign> edges;
        std::vector<PatternAssign> patterns;
    };
    std::vector<EdgeAssign> edge_assigns;
    size_t design_skipped = 0;                    // frezy wzorków pominięte przy ostatnim nałożeniu (nie mieszczą się w konturze)
    std::vector<PatternAssign> pattern_assigns;
    std::vector<Staged> staged;                   // elementy odłożone do zestawu (zapisywane razem jednym krokiem)
    tk::StringVar edge_style_var, edge_size, edge_scope, pattern_var, pattern_face, design_note;
    tk::BooleanVar propagate;                     // przekaż kanty i wzorki pasującym elementom mebla
    std::vector<tk::StringVar> pattern_labels, pattern_values;
    std::vector<ttk::Frame*> pattern_rows;
    tk::Canvas* edge_preview = nullptr;
    tk::Canvas* pattern_preview = nullptr;
    tk::Listbox* staged_list = nullptr;
    ttk::Checkbutton* propagate_check = nullptr;
    void edge_style_selected();
    void pattern_selected();
    void draw_edge_preview();
    void draw_pattern_preview();
    void apply_edge_ui();
    void add_pattern_ui();
    void clear_patterns_ui();
    void refresh_design_note();
    // Nakłada kanty i wzorki na część (q.fillets / q.chamfers / q.machining); ValueError z powodem, gdy się nie da.
    void apply_design(core::Part& q);
    void draw_design_overlay();
    void propagate_design(core::ProjectPtr& project, const std::string& saved_id);
    bool stage_element();
    void unstage_selected();
    void load_staged(const Staged& item);
    bool save_all();
    void refresh_staged_list();

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
    // --- narzędzia rysowania
    void set_tool(const std::string& tool);
    bool key(const std::string& keysym, int state = 0);   // skróty i pole wartości; true = klawisz obsłużony
    bool value_apply();                                      // zatwierdza wpisaną wartość (długość, szer,wys, promień)
    std::string value_prompt_text() const;
    void cancel_pending();
    std::vector<Point> shape_preview(const Point& cursor) const;   // podgląd okręgu / wieloboku / łuku pod kursorem
    void finish_shape(const std::vector<Point>& shape, const std::string& what, std::optional<Point> center = std::nullopt);
    void insert_profile();
    // --- przesuwanie punktów, miarka i linie pomocnicze
    void press(const tk::Event& event);
    void drag(const tk::Event& event);
    void release(const tk::Event& event);
    void move_point(size_t index, const Point& to);
    void add_guide(const Point& a, const Point& b);
    void clear_guides();
    void fillet_point(size_t index, double value, bool chamfer);
    void fillet_selected(bool chamfer);
    void remove_point_at(size_t index);
    void remove_selected_point();
    void apply_edge_length(size_t index, double new_length);
    void constrain_at(size_t index, const std::string& kind, std::optional<double> new_length);
    void select_row(size_t index);                   // zaznacza punkt na liście (jak kliknięcie)
    std::optional<size_t> selected_index() const;
    void edit_dimension(size_t edge);
    // Kliknięcie w napis wymiaru zamkniętego konturu: indeks krawędzi albo -1.
    int dimension_at(int x, int y) const;
    bool import_dxf(const std::string& path = "", bool confirm_replace = true);
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
