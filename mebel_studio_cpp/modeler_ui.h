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

#include "cutters.h"
#include "dock_workspace.h"
#include "modeler_geometry.h"
#include "quick_studio_ui.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace modeler_ui {

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
    void update_op_list();
    bool replace_part(const core::Part& part, const std::string& what, const std::string& category);
};

// Otwiera okno pracowni (jedno na projekt).
Modeler* open(App* app);
// Aktualnie otwarte okno albo nullptr.
Modeler* current();

}  // namespace modeler_ui
