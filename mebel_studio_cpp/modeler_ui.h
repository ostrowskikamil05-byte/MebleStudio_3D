// „Pracownia 3D” — własny modeler mebli w stylu bezpośredniego modelowania: rysujesz prostokąt lub kontur na podłodze albo na ścianie
// istniejącej płyty, wyciągasz go, przesuwasz, kopiujesz; każda bryła od razu jest płytą projektu (z listą formatek, okuciami
// i eksportami). Nic się nie „zapisuje po konturze” — każda czynność jest jednym krokiem cofania.
//
// Frezy: dowolnie wiele operacji na jednej płycie, każda z własnym narzędziem (prosty, kulowy, V, zaokrąglający, fazujący).
//
// Logika geometryczna jest w modeler_geometry (czysta); tu jest okno, narzędzia i nakładki. Metody on_* są publiczne, żeby testy
// mogły symulować mysz i klawiaturę bez prawdziwych zdarzeń Tk.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cad2d.h"
#include "cutters.h"
#include "dock_workspace.h"
#include "modeler_geometry.h"
#include "quick_studio_ui.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace modeler_ui {

struct Tool2dInfo {
    const char* id;
    const char* label;
    const char* key;
    const char* hint;
};
// Narzędzia trybu 2D CAD (do paska i testów).
const std::vector<Tool2dInfo>& tools2d();

class Modeler : public dock_workspace::DockDocument {
public:
    using Vec3 = modeler_geometry::Vec3;

    explicit Modeler(App* app);

    // ---- stan
    core::Camera camera;
    quick_studio_ui::StudioCanvas* canvas = nullptr;
    tk::StringVar tool_var, typed, status, thickness, group_name, material_name, work_plane, copies;
    tk::StringVar cutter_var, cutter_diameter, cutter_angle, cutter_depth, cutter_kind, cutter_name, pocket_size;
    tk::StringVar edge_cutter_var, edge_size, edge_scope, pattern_var, selection_text;
    tk::BooleanVar copy_mode;
    std::string tool = "select";
    std::set<std::string> selection;  // identyfikatory zaznaczonych płyt
    tk::Listbox* ops_list = nullptr;
    size_t ops_listed = 0;  // ile pozycji ma lista operacji zaznaczonej płyty
    tk::Canvas* cutter_preview = nullptr;
    tk::Canvas* pattern_preview = nullptr;
    tk::Canvas* edge_preview = nullptr;
    ttk::Combobox* material_box = nullptr;

    struct Draft {
        int stage = 0;
        std::optional<modeler_geometry::Plane> plane;
        std::vector<Vec3> points;  // prostokąt: [P0, P1]; kontur: wierzchołki
        double depth = 0;
        std::pair<double, double> anchor{0, 0};
        std::optional<modeler_geometry::FaceHit> hit;
        core::Part original;
        Vec3 base{0, 0, 0}, delta{0, 0, 0};
        std::vector<std::string> ids;
        int axis_lock = -1;  // -1 brak, 0 X, 1 Y, 2 Z
        std::string label;
        std::string mill_face = "top";
        std::optional<size_t> part;
    } draft;

    // ---- narzędzia
    void set_tool(const std::string& name);
    void cancel();
    // Mysz i klawiatura (współrzędne płótna w pikselach). `state`: bit 1 = Shift, bit 4 = Ctrl.
    void on_motion(double sx, double sy);
    void on_click(double sx, double sy, int state = 0);
    void on_key(const std::string& keysym, int state = 0);
    // Wpisanie wymiaru (jak pole wymiarów w SketchUp): „600x400”, „18”, „560”; zwraca false i zapisuje powód, gdy odrzucono.
    bool apply_typed(const std::string& text);

    // ---- operacje na projekcie (każda = jeden krok cofania, wpis w dzienniku)
    bool add_board(const modeler_geometry::Box& box, const std::string& origin);
    bool add_polygon_board(const modeler_geometry::Plane& plane, const std::vector<Vec3>& points, double depth);
    bool commit_pull(size_t part_index, const std::string& face, double distance);
    bool commit_move(const std::vector<std::string>& ids, const Vec3& delta, bool copy, int count);
    bool erase_selected();
    bool add_mill_op(size_t part_index, cutters::Op op);
    bool remove_mill_op(size_t part_index, const std::string& op_id);
    bool apply_edge_cutter_ui();
    bool add_pattern_ui();
    bool clear_patterns_ui();

    // ---- tryb 2D CAD (ten sam projekt co w 3D: płyty dodane tu są widoczne w 3D i odwrotnie)
    // Szkic to lista konturów z prawdziwymi łukami we współrzędnych płaszczyzny rysunku. „Wyciągnij” zamienia zamknięte kontury w płyty
    // (kształty wewnątrz są otworami przelotowymi), „Wczytaj obrys” zabiera do edycji kontur istniejącej płyty.
    std::string mode = "3d";                       // „3d” albo „2d”
    std::string tool2 = "line";
    tk::StringVar mode_var, tool2_var, plane2_var, level2_var, grid_var, radius_var, sides_var, slot_width_var, shapes_text;
    tk::BooleanVar snap_grid_var, ortho_var;
    std::vector<cad2d::Path> shapes;
    std::optional<size_t> shape_sel;
    std::optional<std::string> editing_part;       // identyfikator płyty, której obrys jest w szkicu
    modeler_geometry::Plane plane2d = modeler_geometry::ground_plane(0);
    tk::Listbox* shapes_list = nullptr;
    size_t shapes_listed = 0;
    struct Draft2D {
        int stage = 0;
        std::vector<cad2d::Vertex> verts;          // kontur w trakcie rysowania (łuki jako wybrzuszenia)
        std::string seg = "line";                  // rodzaj kolejnego odcinka: line, arc3, tangent
        std::optional<cad2d::Point> arc_through;   // łuk 3 punktowe: zaznaczony punkt pośredni
        std::optional<cad2d::Point> first;         // pierwszy punkt narzędzi dwupunktowych
        std::optional<size_t> index;               // wskazany kształt
        std::optional<size_t> vertex;              // wskazany wierzchołek albo odcinek kształtu
        cad2d::Point base{0, 0};
    } d2;
    struct SketchStep { std::vector<cad2d::Path> shapes; bool project_step = false; };
    std::vector<SketchStep> sketch_undo, sketch_redo;
    void set_mode(const std::string& name);
    void set_tool2(const std::string& name);
    void on_motion_2d(double sx, double sy);
    void on_click_2d(double sx, double sy, int state = 0);
    void on_key_2d(const std::string& keysym, int state = 0);
    bool apply_typed_2d(const std::string& text);
    bool set_work_plane(const std::string& name, double level);
    bool add_shape(const cad2d::Path& path, const std::string& what);
    bool replace_shape(size_t index, const cad2d::Path& path, const std::string& what);
    bool remove_shape(size_t index);
    void clear_sketch();
    bool extrude_shapes();
    bool load_part_outline();
    bool apply_part_outline();
    bool undo_2d();
    bool redo_2d();
    void draw_scene_2d();
    void draw_overlay_2d();
    cad2d::Point cursor_2d() const { return cursor2; }
    double grid_step() const;
    void handle_point_2d(const cad2d::Point& p, bool shift = false);   // kliknięcie albo wpisany punkt (rzuca std::invalid_argument z powodem)
    void reject_2d(const std::string& what, const std::string& reason);
    void reset_draft_2d();
    std::optional<std::string> pick_part_2d(const cad2d::Point& p) const;

    // ---- widok
    void fit();
    void view(double yaw, double pitch);
    void request_scene();
    void draw_scene();
    void draw_overlay();
    void refresh_panel();
    void draw_cutter_preview();
    void draw_pattern_preview();
    void draw_edge_preview();
    core::ProjectPtr project() const;
    const core::Part* selected_part() const;
    void destroy() override;

    // ---- pomocnicze (publiczne dla testów)
    modeler_geometry::Plane default_plane() const;
    cutters::Cutter panel_cutter() const;
    double panel_thickness() const;
    void note(const std::string& category, const std::string& text);
    void reject(const std::string& what, const std::string& reason);

private:
    struct Cursor {
        Vec3 point{0, 0, 0};
        std::string label;
        std::optional<modeler_geometry::FaceHit> hit;
        std::optional<modeler_geometry::Plane> plane;
        bool valid = false;
    };
    Cursor resolve_cursor(double sx, double sy);
    void finish_draft_with_depth(double depth);
    std::pair<double, double> mouse{0, 0};
    Cursor cursor;
    bool interactive = false;
    bool need_fit = true;
    std::optional<std::string> scene_job, live_job;
    std::string live_key;
    std::optional<std::string> last_warning;
    std::pair<int, int> anchor_px{0, 0};
    void live_refresh();
    void build_panel(tk::Widget* parent);
    void orbit(tk::Event& e);
    void pan(tk::Event& e);
    void zoom(tk::Event& e);
    void finish_view();
    void select_at(double sx, double sy, bool add);
    cad2d::Point cursor2{0, 0};
    core::Camera saved_camera3d{-38, 24, .35, {300, 280, 300}};
    std::string cursor2_label;
    std::vector<cad2d::Path> ref_paths;            // obrysy płyt rzutowane na płaszczyznę rysunku (cel przyciągania)
    std::string ref_key;
    void rebuild_refs();
    cad2d::Point resolve_2d(double sx, double sy);
    void build_panel_2d(tk::Widget* parent);
    void push_sketch_step(bool project_step = false);
    void finish_polyline(bool close);
    void add_draft_segment(const cad2d::Point& end, const cad2d::Point& through, bool tangent);
    ttk::Frame* tools3d_frame = nullptr;
    ttk::Frame* tools2d_frame = nullptr;
    ttk::Frame* panel3d_frame = nullptr;
    ttk::Frame* panel2d_frame = nullptr;
    void update_op_list();
    bool replace_part(const core::Part& part, const std::string& what, const std::string& category);
};

// Otwiera okno pracowni (jedno na projekt).
Modeler* open(App* app);
// Aktualnie otwarte okno albo nullptr.
Modeler* current();

}  // namespace modeler_ui
