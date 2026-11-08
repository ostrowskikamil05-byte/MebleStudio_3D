// Tryb „2D CAD” Pracowni: rysunek na płaszczyźnie (podłoga, przód, bok albo góra wybranej płyty) z prawdziwymi łukami, okręgami,
// zaokrągleniami i wymiarami. Pracuje na tym samym projekcie co widok 3D — płyty z rysunku od razu są w 3D, a obrys płyty z 3D można
// wczytać i poprawić tutaj. Geometria (łuki, zaokrąglenia, grupowanie otworów) jest w cad2d, a przeniesienie na płyty w modeler_geometry.
#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

#include "app.h"
#include "features.h"
#include "journal.h"
#include "modeler_ui.h"

namespace modeler_ui {

namespace mg = modeler_geometry;
namespace c2 = cad2d;
using P = c2::Point;
using Vec3 = mg::Vec3;
using Cmd = std::function<void()>;

namespace {

std::string num(double x) { return std::format("{:g}", std::round(x * 100) / 100); }

double number_field(const std::string& text, const std::string& what) {
    std::string t = text;
    std::replace(t.begin(), t.end(), ',', '.');
    size_t used = 0;
    double value = 0;
    try {
        value = std::stod(t, &used);
    } catch (const std::exception&) {
        throw std::invalid_argument(what + " musi być liczbą (podano „" + text + "”).");
    }
    if (used != t.size() || !std::isfinite(value)) throw std::invalid_argument(what + " musi być liczbą (podano „" + text + "”).");
    return value;
}

double dist(const P& a, const P& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
P add(const P& a, const P& b) { return {a[0] + b[0], a[1] + b[1]}; }
P sub(const P& a, const P& b) { return {a[0] - b[0], a[1] - b[1]}; }
P mul(const P& a, double k) { return {a[0] * k, a[1] * k}; }

bool inside_polygon(const std::vector<P>& poly, const P& p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i][1] > p[1]) != (poly[j][1] > p[1]) && p[0] < (poly[j][0] - poly[i][0]) * (p[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]) in = !in;
    return in;
}

bool inside_path(const c2::Path& path, const P& p) {
    if (!path.closed || path.v.size() < 2) return false;
    try {
        auto pts = c2::tessellate(path, .5, 4096);
        return pts.size() >= 3 && inside_polygon(pts, p);
    } catch (const std::exception&) {
        return false;
    }
}

// „250” → {250}, „250<45” → {250, 45} (długość i kąt względem osi poziomej), „600x400” / „100;200” → {600, 400}.
struct Typed {
    std::vector<double> values;
    bool polar = false;
};
Typed parse_typed(const std::string& text) {
    Typed out;
    auto lt = text.find('<');
    if (lt != std::string::npos) {
        out.polar = true;
        out.values = {number_field(text.substr(0, lt), "Długość"), number_field(text.substr(lt + 1), "Kąt")};
        return out;
    }
    out.values = mg::parse_numbers(text);
    return out;
}

std::string segment_label(const c2::Segment& s) {
    if (std::fabs(s.bulge) < 1e-12) {
        double angle = std::atan2(s.b[1] - s.a[1], s.b[0] - s.a[0]) * 180 / c2::PI;
        return std::format("{} mm ∠{}°", num(dist(s.a, s.b)), num(angle));
    }
    c2::Arc arc = c2::arc_of(s.a, s.b, s.bulge);
    return std::format("R{} ∠{}° ({} mm)", num(arc.radius), num(std::fabs(arc.sweep) * 180 / c2::PI), num(c2::segment_length(s)));
}

}  // namespace

const std::vector<Tool2dInfo>& tools2d() {
    static const std::vector<Tool2dInfo> list = {
        {"select2", "Wybierz", "V", "Kliknij kształt szkicu albo płytę. Delete usuwa; Enter wyciąga zamknięte kształty do płyt."},
        {"line", "Linia", "L", "Klikaj punkty. A = następny odcinek jako łuk 3-punktowy, T = łuk styczny, L = prosty; C albo klik w pierwszy punkt zamyka, Enter kończy linię. Wpisz „250” lub „250<45”."},
        {"arc", "Łuk", "A", "Kliknij początek, punkt pośredni i koniec łuku. T = łuk styczny do poprzedniego odcinka, L = prosty odcinek."},
        {"rect", "Prostokąt", "R", "Kliknij dwa narożniki albo wpisz „600x400” i Enter."},
        {"rrect", "Prostokąt R", "U", "Jak prostokąt; promień narożników jest w panelu (Zaokrąglenie)."},
        {"circle", "Okrąg", "K", "Kliknij środek i punkt na okręgu albo wpisz promień i Enter. Wewnątrz płyty = otwór."},
        {"polygon", "Wielobok", "W", "Kliknij środek i wierzchołek (liczba boków w panelu) albo wpisz promień i Enter."},
        {"slot", "Szczelina", "S", "Kliknij środki obu końców szczeliny (szerokość w panelu) albo wpisz długość osi i Enter."},
        {"fillet", "Zaokrąglenie", "Z", "Kliknij narożnik kształtu — dostanie łuk styczny o promieniu z panelu. Wpisz promień i Enter, aby go zmienić."},
        {"chamfer", "Ścięcie", "H", "Kliknij narożnik kształtu — zostanie ścięty na odległość z panelu."},
        {"movev", "Przesuń", "M", "Kliknij wierzchołek (przesuwa jego) albo kształt (przesuwa cały), potem miejsce docelowe."},
        {"delv", "Usuń punkt", "X", "Kliknij wierzchołek kształtu, aby go usunąć."},
        {"dim", "Wymiar", "D", "Kliknij odcinek, aby zobaczyć jego wymiar; wpisz nową długość i Enter, aby go zmienić."},
    };
    return list;
}

namespace {
const Tool2dInfo& info2d(const std::string& id) {
    for (const auto& t : tools2d())
        if (id == t.id) return t;
    throw std::invalid_argument("Nieznane narzędzie rysunku: " + id);
}
}  // namespace

// ------------------------------------------------------------------ tryb i panel

void Modeler::set_mode(const std::string& name) {
    if (name != "2d" && name != "3d") throw std::invalid_argument("Nieznany tryb pracowni: " + name);
    mode_var.set(name);
    if (name == mode) return;
    reset_draft_2d();
    draft = Draft();
    typed.set("");
    if (name == "2d") {
        saved_camera3d = camera;
        mode = "2d";
        tools3d_frame->pack_forget();
        panel3d_frame->pack_forget();
        tools2d_frame->pack({{"side", "left"}});
        panel2d_frame->pack({{"fill", "x"}});
        auto angles = mg::camera_angles_for_plane(plane2d);
        camera.yaw = angles.first;
        camera.pitch = angles.second;
        status.set(info2d(tool2).hint);
        ref_key.clear();
        note("cad2d", std::format("przełączono na 2D CAD (płaszczyzna „{}”, szkic: {} kształtów)", plane2d.label, shapes.size()));
    } else {
        camera = saved_camera3d;
        mode = "3d";
        tools2d_frame->pack_forget();
        panel2d_frame->pack_forget();
        tools3d_frame->pack({{"side", "left"}});
        panel3d_frame->pack({{"fill", "x"}});
        status.set(std::string("Widok 3D. ") + (tool == "select" ? "Kliknij płytę, aby ją zaznaczyć." : ""));
        note("cad2d", std::format("przełączono na 3D (płyty w projekcie: {})", app->project->parts.size()));
    }
    cursor = Cursor();
    need_fit = name == "2d";
    refresh_panel();
    if (canvas != nullptr) canvas->focus_set();
    request_scene();
}

void Modeler::set_tool2(const std::string& name) {
    const Tool2dInfo& info = info2d(name);
    if (tool2 != name) note("cad2d", std::format("narzędzie 2D: {} → {}", info2d(tool2).label, info.label));
    tool2 = name;
    tool2_var.set(name);
    reset_draft_2d();
    status.set(info.hint);
    draw_overlay_2d();
}

void Modeler::reset_draft_2d() {
    d2 = Draft2D();
    if (d2.stage == 0) typed.set("");
}

void Modeler::reject_2d(const std::string& what, const std::string& reason) {
    note("cad2d", "odrzucono: " + what + " — " + reason);
    status.set("Nie zrobiono: " + what + " — " + reason);
}

void Modeler::build_panel_2d(tk::Widget* parent_in) {
    panel2d_frame = new ttk::Frame(parent_in);
    tk::Widget* parent = panel2d_frame;
    auto section = [&](const std::string& text) { (new ttk::Label(parent, {{"text", text}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "10 2"}}); };
    auto row = [&](const std::string& label, tk::StringVar& var, int width = 8) {
        auto* frame = new ttk::Frame(parent);
        frame->pack({{"fill", "x"}, {"pady", 1}});
        (new ttk::Label(frame, {{"text", label}}))->pack({{"side", "left"}});
        auto* entry = new ttk::Entry(frame, {{"textvariable", var}, {"width", width}});
        entry->pack({{"side", "right"}});
        return entry;
    };
    auto combo = [&](const std::string& label, tk::StringVar& var, const std::vector<std::string>& values) {
        auto* frame = new ttk::Frame(parent);
        frame->pack({{"fill", "x"}, {"pady", 1}});
        (new ttk::Label(frame, {{"text", label}}))->pack({{"side", "left"}});
        auto* box = new ttk::Combobox(frame, {{"textvariable", var}, {"values", values}, {"state", "readonly"}, {"width", 22}});
        box->pack({{"side", "right"}});
        return box;
    };

    section("Płaszczyzna rysunku");
    auto apply_plane = [this] {
        try {
            set_work_plane(plane2_var.get(), number_field(level2_var.get(), "Poziom płaszczyzny"));
        } catch (const std::invalid_argument& e) {
            reject_2d("zmiana płaszczyzny", e.what());
        }
    };
    combo("Rysuj na", plane2_var, {"Podłoga", "Przód", "Bok", "Góra zaznaczonej płyty"})->bind("<<ComboboxSelected>>", [apply_plane](tk::Event&) { apply_plane(); return std::string(); });
    row("Poziom [mm]", level2_var)->bind("<Return>", [apply_plane](tk::Event&) { apply_plane(); return std::string(); });

    section("Siatka i przyciąganie");
    row("Krok siatki [mm] (0 = auto)", grid_var);
    (new ttk::Checkbutton(parent, {{"text", "Przyciągaj do siatki"}, {"variable", snap_grid_var}}))->pack({{"anchor", "w"}});
    (new ttk::Checkbutton(parent, {{"text", "Wymuszaj osie poziomo/pionowo (O)"}, {"variable", ortho_var}}))->pack({{"anchor", "w"}});
    (new ttk::Label(parent, {{"text", "Kursor przyciąga się do końców, środków, środków łuków i kwadrantów okręgów — także na płytach projektu."}, {"wraplength", 280}}))->pack({{"anchor", "w"}, {"pady", 2}});

    section("Parametry narzędzi");
    row("Promień zaokrąglenia / ścięcie [mm]", radius_var);
    row("Liczba boków wieloboku", sides_var, 5);
    row("Szerokość szczeliny [mm]", slot_width_var);

    section("Nowe płyty");
    row("Grubość płyty [mm]", thickness);
    row("Mebel (grupa)", group_name, 14);
    std::vector<std::string> materials;
    for (const auto& m : app->project->materials) materials.push_back(m.name);
    combo("Materiał", material_name, materials);

    section("Szkic");
    (new ttk::Label(parent, {{"textvariable", shapes_text}, {"wraplength", 280}}))->pack({{"anchor", "w"}});
    shapes_list = new tk::Listbox(parent, {{"height", 7}, {"exportselection", 0}});
    shapes_list->pack({{"fill", "x"}, {"pady", 3}});
    shapes_list->bind("<<ListboxSelect>>", [this](tk::Event&) {
        auto sel = shapes_list->curselection();
        if (!sel.empty() && (size_t)sel[0] < shapes.size()) {
            shape_sel = (size_t)sel[0];
            note("cad2d", "wybrano z listy: " + c2::describe(shapes[*shape_sel]));
            request_scene();
        }
        return std::string();
    });
    auto button = [&](const std::string& text, std::function<void()> fn) {
        (new ttk::Button(parent, {{"text", text}, {"command", Cmd(fn)}}))->pack({{"fill", "x"}, {"pady", 1}});
    };
    button("Wyciągnij zamknięte kształty do płyt", [this] { extrude_shapes(); });
    button("Wczytaj obrys zaznaczonej płyty", [this] { load_part_outline(); });
    button("Zastosuj obrys do wczytanej płyty", [this] { apply_part_outline(); });
    button("Usuń wybrany kształt", [this] {
        if (!shape_sel) reject_2d("usunięcie kształtu", "nie wybrano kształtu (narzędzie Wybierz albo lista)");
        else remove_shape(*shape_sel);
    });
    button("Wyczyść szkic", [this] { clear_sketch(); });
    (new ttk::Label(parent, {{"text", "Kształty wewnątrz konturu stają się otworami przelotowymi (okrąg, prostokąt, szczelina). Obrys z łukami zapisuje się w płycie jako kontur z drobnych odcinków (do 128 punktów)."}, {"wraplength", 280}}))->pack({{"anchor", "w"}, {"pady", 4}});
}

double Modeler::grid_step() const {
    double step = 0;
    try { step = number_field(grid_var.get(), "Krok siatki"); } catch (const std::invalid_argument&) {}
    const double scale = std::max(camera.scale, 1e-6);
    if (step > 0) return std::max(step, 4 / scale);   // zbyt gęsta siatka nie jest rysowana ani przyciągana
    static const double nice[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
    for (double s : nice)
        if (s * scale >= 12) return s;
    return 10000;
}

// ------------------------------------------------------------------ płaszczyzna rysunku

bool Modeler::set_work_plane(const std::string& name, double level) {
    try {
        mg::Plane plane;
        if (name == "Podłoga") plane = mg::ground_plane(level);
        else if (name == "Przód") plane = mg::front_plane(level);
        else if (name == "Bok") plane = mg::side_plane(level);
        else if (name == "Góra zaznaczonej płyty") {
            const core::Part* part = selected_part();
            if (!part) throw std::invalid_argument("zaznacz płytę (narzędzie Wybierz), na której górze chcesz rysować");
            plane = mg::face_plane(*part, "top");
            plane.label = "Góra: " + part->name;
        } else {
            throw std::invalid_argument("nieznana płaszczyzna „" + name + "”");
        }
        plane2d = plane;
        plane2_var.set(name);
        level2_var.set(num(level));
        if (editing_part) {
            note("cad2d", "zmiana płaszczyzny zakończyła edycję obrysu płyty (szkic zostaje, ale nie jest już powiązany z płytą)");
            editing_part.reset();
        }
        reset_draft_2d();
        auto angles = mg::camera_angles_for_plane(plane2d);
        camera.yaw = angles.first;
        camera.pitch = angles.second;
        ref_key.clear();
        need_fit = true;
        note("cad2d", std::format("płaszczyzna rysunku: „{}” (poziom {} mm)", plane2d.label, num(level)));
        status.set(std::format("Rysujesz na płaszczyźnie „{}”.", plane2d.label));
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject_2d("zmiana płaszczyzny rysunku", e.what());
    }
    return false;
}

void Modeler::rebuild_refs() {
    std::string key = std::format("{}|{}|{}|{}", (const void*)app->project.get(), app->viewport_revision, app->project->parts.size(), plane2d.label + num(plane2d.origin[0]) + num(plane2d.origin[1]) + num(plane2d.origin[2]));
    if (key == ref_key) return;
    ref_key = key;
    ref_paths.clear();
    for (const auto& part : app->project->parts) {
        if (!part.visible) continue;
        for (const auto& face : mg::part_faces(part)) {
            if (std::fabs(mg::dot(face.plane.n, plane2d.n)) < .99) continue;
            c2::Path path;
            path.closed = true;
            path.kind = "płyta";
            for (const auto& p : face.polygon) {
                mg::Point ab = mg::to_plane(plane2d, p);
                path.v.push_back({{ab[0], ab[1]}, 0});
            }
            ref_paths.push_back(std::move(path));
        }
    }
}

std::optional<std::string> Modeler::pick_part_2d(const P& p) const {
    std::optional<std::string> best;
    double best_area = 1e300;
    for (const auto& part : app->project->parts) {
        if (!part.visible) continue;
        for (const auto& face : mg::part_faces(part)) {
            if (std::fabs(mg::dot(face.plane.n, plane2d.n)) < .99) continue;
            std::vector<P> poly;
            for (const auto& q : face.polygon) {
                mg::Point ab = mg::to_plane(plane2d, q);
                poly.push_back({ab[0], ab[1]});
            }
            if (poly.size() < 3 || !inside_polygon(poly, p)) continue;
            double area = 0;
            for (size_t i = 0; i < poly.size(); ++i) area += poly[i][0] * poly[(i + 1) % poly.size()][1] - poly[(i + 1) % poly.size()][0] * poly[i][1];
            area = std::fabs(area) / 2;
            if (area < best_area) { best_area = area; best = part.id; }
        }
    }
    return best;
}

// ------------------------------------------------------------------ szkic (z cofaniem)

void Modeler::push_sketch_step(bool project_step) {
    sketch_undo.push_back({shapes, project_step});
    if (sketch_undo.size() > 200) sketch_undo.erase(sketch_undo.begin());
    sketch_redo.clear();
}

bool Modeler::add_shape(const c2::Path& path, const std::string& what) {
    push_sketch_step();
    shapes.push_back(path);
    shape_sel = shapes.size() - 1;
    note("cad2d", std::format("dodano {}: {}", what, c2::describe(path)));
    status.set(std::format("Dodano {}: {}.", what, c2::describe(path)));
    refresh_panel();
    request_scene();
    return true;
}

bool Modeler::replace_shape(size_t index, const c2::Path& path, const std::string& what) {
    if (index >= shapes.size()) { reject_2d(what, "kształt nie istnieje"); return false; }
    push_sketch_step();
    std::string before = c2::describe(shapes[index]);
    shapes[index] = path;
    shape_sel = index;
    note("cad2d", std::format("{}: {} → {}", what, before, c2::describe(path)));
    status.set(std::format("{}: {}.", what, c2::describe(path)));
    refresh_panel();
    request_scene();
    return true;
}

bool Modeler::remove_shape(size_t index) {
    if (index >= shapes.size()) { reject_2d("usunięcie kształtu", "kształt nie istnieje"); return false; }
    push_sketch_step();
    note("cad2d", "usunięto kształt: " + c2::describe(shapes[index]));
    shapes.erase(shapes.begin() + (long)index);
    shape_sel.reset();
    reset_draft_2d();
    status.set("Usunięto kształt.");
    refresh_panel();
    request_scene();
    return true;
}

void Modeler::clear_sketch() {
    if (shapes.empty()) { reject_2d("czyszczenie szkicu", "szkic jest już pusty"); return; }
    push_sketch_step();
    note("cad2d", std::format("wyczyszczono szkic ({} kształtów)", shapes.size()));
    shapes.clear();
    shape_sel.reset();
    editing_part.reset();
    reset_draft_2d();
    status.set("Szkic wyczyszczony.");
    refresh_panel();
    request_scene();
}

bool Modeler::undo_2d() {
    if (!sketch_undo.empty()) {
        SketchStep step = sketch_undo.back();
        sketch_undo.pop_back();
        sketch_redo.push_back({shapes, step.project_step});
        shapes = step.shapes;
        if (step.project_step) app->undo();
        shape_sel.reset();
        reset_draft_2d();
        note("cad2d", std::format("cofnięto krok szkicu{} (kształtów: {})", step.project_step ? " razem ze zmianą w projekcie" : "", shapes.size()));
        status.set("Cofnięto.");
    } else {
        app->undo();
        note("cad2d", "cofnięto zmianę w projekcie (szkic bez zmian)");
    }
    refresh_panel();
    request_scene();
    return true;
}

bool Modeler::redo_2d() {
    if (!sketch_redo.empty()) {
        SketchStep step = sketch_redo.back();
        sketch_redo.pop_back();
        sketch_undo.push_back({shapes, step.project_step});
        shapes = step.shapes;
        if (step.project_step) app->redo();
        shape_sel.reset();
        reset_draft_2d();
        note("cad2d", std::format("ponowiono krok szkicu{} (kształtów: {})", step.project_step ? " razem ze zmianą w projekcie" : "", shapes.size()));
        status.set("Ponowiono.");
    } else {
        app->redo();
        note("cad2d", "ponowiono zmianę w projekcie (szkic bez zmian)");
    }
    refresh_panel();
    request_scene();
    return true;
}

// ------------------------------------------------------------------ szkic → płyty

namespace {

// Otwór przelotowy z kształtu wewnątrz konturu: tylko okrąg, prostokąt równoległy do osi i szczelina mają odpowiedniki w obróbce płyty.
mg::Cutout cutout_of(const c2::Path& path) {
    mg::Cutout cutout;
    if (auto c = c2::as_circle(path)) {
        cutout.shape = "circle";
        cutout.center = {c->center[0], c->center[1]};
        cutout.width = cutout.length = c->radius * 2;
    } else if (auto r = c2::as_rectangle(path)) {
        cutout.shape = "rect";
        cutout.center = {((*r)[0][0] + (*r)[1][0]) / 2, ((*r)[0][1] + (*r)[1][1]) / 2};
        cutout.length = (*r)[1][0] - (*r)[0][0];
        cutout.width = (*r)[1][1] - (*r)[0][1];
    } else if (auto s = c2::as_slot(path)) {
        cutout.shape = "slot";
        cutout.center = {(s->a[0] + s->b[0]) / 2, (s->a[1] + s->b[1]) / 2};
        cutout.width = s->width;
        cutout.length = dist(s->a, s->b) + s->width;
        cutout.angle = std::atan2(s->b[1] - s->a[1], s->b[0] - s->a[0]) * 180 / c2::PI;
    } else {
        throw std::invalid_argument("kształt wewnątrz płyty (" + c2::describe(path) + ") nie jest okręgiem, prostokątem ani szczeliną — otwór o dowolnym kształcie narysuj jako jedną z tych figur albo wyciągnij ten kształt jako osobną płytę");
    }
    return cutout;
}

std::vector<Vec3> world_points(const mg::Plane& plane, const std::vector<P>& pts) {
    std::vector<Vec3> out;
    for (const auto& p : pts) out.push_back(mg::from_plane(plane, p[0], p[1]));
    return out;
}

}  // namespace

bool Modeler::extrude_shapes() {
    try {
        if (shapes.empty()) throw std::invalid_argument("szkic jest pusty — narysuj zamknięty kontur");
        std::vector<c2::Path> closed, kept;
        for (const auto& s : shapes) (s.closed ? closed : kept).push_back(s);
        if (closed.empty()) throw std::invalid_argument("w szkicu nie ma zamkniętego konturu — zamknij linię (klawisz C albo klik w pierwszy punkt)");
        if (editing_part) throw std::invalid_argument("szkic jest obrysem istniejącej płyty — użyj „Zastosuj obrys do wczytanej płyty” albo wyczyść szkic");
        double thickness = panel_thickness();
        std::vector<c2::Group> groups = c2::group_shapes(closed);   // błąd z powodem dla przecinających się i zagnieżdżonych dwa razy kształtów
        std::string material_id = app->project->materials.empty() ? std::string() : app->project->materials[0].id;
        for (const auto& m : app->project->materials) if (m.name == material_name.get()) material_id = m.id;
        auto temp = std::make_shared<core::Project>(*app->project);
        std::vector<std::string> names;
        for (const auto& p : temp->parts) names.push_back(p.name);
        std::vector<core::Part> added;
        for (const auto& group : groups) {
            const c2::Path& outer = closed[group.outer];
            c2::Contour contour = c2::to_contour(outer);   // przybliżenie łuków odcinkami (limit konturu)
            std::vector<P> pts;
            for (const auto& p : contour.points) pts.push_back({p[0] + contour.offset[0], p[1] + contour.offset[1]});
            std::vector<mg::Cutout> cutouts;
            for (size_t k : group.inner) cutouts.push_back(cutout_of(closed[k]));
            core::Part part = mg::board_from_profile(plane2d, world_points(plane2d, pts), cutouts, thickness, "", group_name.get().empty() ? "Mebel" : group_name.get(), material_id);
            part.name = mg::suggest_name(part, names);
            names.push_back(part.name);
            temp->parts.push_back(part);
            added.push_back(part);
        }
        temp->validate();
        std::vector<c2::Path> before = shapes;
        app->snapshot();
        app->project = temp;
        app->refresh();
        sketch_undo.push_back({before, true});
        sketch_redo.clear();
        shapes = kept;
        shape_sel.reset();
        selection.clear();
        std::string summary;
        for (const auto& p : added) {
            selection.insert(p.id);
            summary += (summary.empty() ? "" : "; ") + p.name + " (" + mg::describe(p) + ")";
        }
        note("cad2d", std::format("wyciągnięto {} kształtów do {} płyt o grubości {} mm: {}{}", closed.size(), added.size(), num(thickness), summary,
                                   kept.empty() ? std::string() : std::format(" · otwartych linii zostało w szkicu: {}", kept.size())));
        status.set(std::format("Dodano {} płyt do projektu (widoczne też w 3D).", added.size()));
        ref_key.clear();
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject_2d("wyciągnięcie kształtów do płyt", e.what());
    } catch (const std::out_of_range& e) {
        reject_2d("wyciągnięcie kształtów do płyt", e.what());
    } catch (const std::exception& e) {
        reject_2d("wyciągnięcie kształtów do płyt", e.what());
    }
    return false;
}

bool Modeler::load_part_outline() {
    try {
        const core::Part* part = selected_part();
        if (!part) throw std::invalid_argument("zaznacz płytę (narzędzie Wybierz) — jej obrys trafi do szkicu");
        if (!mg::axis_aligned(*part)) throw std::invalid_argument("płyta „" + part->name + "” jest obrócona — ustaw obrót w inspektorze, żeby edytować jej obrys");
        if (part->profile.is_object() && !part->profile.empty()) throw std::invalid_argument("płyta „" + part->name + "” ma kształt parametryczny — zmień go w oknie kształtów");
        mg::Plane top = mg::face_plane(*part, "top");
        std::vector<c2::Path> loaded;
        std::vector<P> outline;
        mg::Vec3 origin = core::world(*part, 0, 0, 0);
        for (const auto& q : part->points()) {
            mg::Point ab = mg::to_plane(top, core::world(*part, q[0], q[1], 0));
            outline.push_back({ab[0], ab[1]});
        }
        (void)origin;
        c2::Path path = c2::polyline(outline, true);
        if (part->contour.empty()) {
            auto b = c2::bounds(path);
            path = c2::rectangle(b[0], b[1]);
        }
        loaded.push_back(path);
        size_t skipped = 0;
        if (part->machining.is_array())
            for (const auto& cut : part->machining) if (cut.value("type", "") == "cutout") ++skipped;
        for (const auto& cutout : mg::cutouts_of(*part, top)) {
            --skipped;
            P center{cutout.center[0], cutout.center[1]};
            if (cutout.shape == "circle") loaded.push_back(c2::circle(center, cutout.width / 2));
            else if (cutout.shape == "rect") loaded.push_back(c2::rectangle({center[0] - cutout.length / 2, center[1] - cutout.width / 2}, {center[0] + cutout.length / 2, center[1] + cutout.width / 2}));
            else {
                double a = cutout.angle * c2::PI / 180, half = (cutout.length - cutout.width) / 2;
                loaded.push_back(c2::slot({center[0] - std::cos(a) * half, center[1] - std::sin(a) * half}, {center[0] + std::cos(a) * half, center[1] + std::sin(a) * half}, cutout.width));
            }
        }
        push_sketch_step();
        plane2d = top;
        plane2d.label = "Góra: " + part->name;
        plane2_var.set("Góra zaznaczonej płyty");
        level2_var.set("0");
        shapes = loaded;
        shape_sel = 0;
        editing_part = part->id;
        reset_draft_2d();
        auto angles = mg::camera_angles_for_plane(plane2d);
        camera.yaw = angles.first;
        camera.pitch = angles.second;
        ref_key.clear();
        need_fit = true;
        note("cad2d", std::format("wczytano obrys płyty „{}” do edycji: {} kształtów{}", part->name, shapes.size(),
                                   skipped > 0 ? std::format(" (pominięto {} obróconych wycięć — zostają w obróbce płyty)", skipped) : std::string()));
        status.set("Edytujesz obrys płyty „" + part->name + "”. Łuki zapisane w płycie są odcinkami — nowe łuki rysuj narzędziami. Potem „Zastosuj obrys do wczytanej płyty”.");
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject_2d("wczytanie obrysu płyty", e.what());
    } catch (const std::exception& e) {
        reject_2d("wczytanie obrysu płyty", e.what());
    }
    return false;
}

bool Modeler::apply_part_outline() {
    try {
        if (!editing_part) throw std::invalid_argument("najpierw wczytaj obrys płyty („Wczytaj obrys zaznaczonej płyty”)");
        auto found = std::find_if(app->project->parts.begin(), app->project->parts.end(), [&](const core::Part& p) { return p.id == *editing_part; });
        if (found == app->project->parts.end()) throw std::invalid_argument("wczytana płyta nie istnieje już w projekcie");
        std::vector<c2::Path> closed;
        for (const auto& s : shapes) if (s.closed) closed.push_back(s);
        if (closed.empty()) throw std::invalid_argument("w szkicu nie ma zamkniętego konturu");
        std::vector<c2::Group> groups = c2::group_shapes(closed);
        if (groups.size() != 1) throw std::invalid_argument(std::format("obrys jednej płyty musi być jednym konturem z ewentualnymi otworami, a w szkicu jest {} osobnych konturów — usuń lub połącz nadmiarowe", groups.size()));
        c2::Contour contour = c2::to_contour(closed[groups[0].outer]);
        std::vector<P> pts;
        for (const auto& p : contour.points) pts.push_back({p[0] + contour.offset[0], p[1] + contour.offset[1]});
        std::vector<mg::Cutout> cutouts;
        for (size_t k : groups[0].inner) cutouts.push_back(cutout_of(closed[k]));
        core::Part changed = mg::reshape_part(*found, plane2d, world_points(plane2d, pts), cutouts);
        std::vector<c2::Path> before = shapes;
        bool had_edges = !found->fillets.empty() || !found->chamfers.empty();
        std::string name = found->name, id = *editing_part;
        if (!replace_part(changed, std::format("zmieniono obrys płyty „{}” w 2D CAD: {} → {} × {} mm, otworów: {}{}", name, mg::describe(*found), num(changed.u), num(changed.v), cutouts.size(),
                                              had_edges ? " · zerowane zaokrąglenia/fazy krawędzi (zmieniły się numery krawędzi)" : ""), "cad2d")) return false;
        sketch_undo.push_back({before, true});
        sketch_redo.clear();
        shapes.clear();
        shape_sel.reset();
        editing_part.reset();
        selection = {id};
        ref_key.clear();
        status.set("Obrys płyty „" + name + "” zmieniony.");
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject_2d("zastosowanie obrysu do płyty", e.what());
    } catch (const std::exception& e) {
        reject_2d("zastosowanie obrysu do płyty", e.what());
    }
    return false;
}

// ------------------------------------------------------------------ kursor, kliknięcia, klawisze

P Modeler::resolve_2d(double sx, double sy) {
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    cursor2_label.clear();
    if (w < 10 || h < 10) return cursor2;
    rebuild_refs();
    mg::Ray ray = mg::unproject(camera, sx, sy, w, h);
    auto t = mg::ray_plane(ray, plane2d);
    if (!t) return cursor2;
    mg::Point ab = mg::to_plane(plane2d, mg::at(ray, *t));
    P p{ab[0], ab[1]};
    const double tol = 10 / std::max(camera.scale, 1e-6);
    std::optional<c2::Snap> a = c2::nearest_snap(shapes, p, tol), b = c2::nearest_snap(ref_paths, p, tol);
    std::optional<c2::Snap> best = a;
    if (b && (!a || dist(b->p, p) < dist(a->p, p))) best = b;
    if (best) {
        p = best->p;
        cursor2_label = best->label;
    } else {
        std::optional<P> anchor;
        if ((tool2 == "line" || tool2 == "arc") && d2.stage >= 1 && !d2.verts.empty()) anchor = d2.verts.back().p;
        else if (d2.first && d2.stage >= 1) anchor = d2.first;
        else if (tool2 == "movev" && d2.stage == 1) anchor = d2.base;
        bool axis = false;
        if (anchor && ortho_var.get()) {
            P q = c2::snap_to_axis(*anchor, p, 3);
            if (q != p) { p = q; axis = true; cursor2_label = "oś"; }
        }
        if (!axis && snap_grid_var.get()) p = c2::snap_to_grid(p, grid_step());
    }
    return p;
}

void Modeler::on_motion_2d(double sx, double sy) {
    mouse = {sx, sy};
    if (canvas == nullptr) return;
    cursor2 = resolve_2d(sx, sy);
    draw_overlay_2d();
}

void Modeler::on_click_2d(double sx, double sy, int state) {
    mouse = {sx, sy};
    if (canvas == nullptr) return;
    cursor2 = resolve_2d(sx, sy);
    try {
        handle_point_2d(cursor2, (state & 1) != 0);
    } catch (const std::invalid_argument& e) {
        reject_2d(std::string("kliknięcie (") + info2d(tool2).label + ")", e.what());
        if (tool2 != "line" && tool2 != "arc") reset_draft_2d();
    } catch (const std::out_of_range& e) {
        reject_2d(std::string("kliknięcie (") + info2d(tool2).label + ")", e.what());
        reset_draft_2d();
    } catch (const std::exception& e) {
        reject_2d(std::string("kliknięcie (") + info2d(tool2).label + ")", e.what());
        reset_draft_2d();
    }
    draw_overlay_2d();
}

void Modeler::add_draft_segment(const P& end, const P& through, bool tangent) {
    if (d2.verts.empty()) throw std::invalid_argument("najpierw kliknij początek");
    c2::Vertex& last = d2.verts.back();
    if (dist(last.p, end) < .5) throw std::invalid_argument("punkt końcowy pokrywa się z poprzednim (odległość < 0,5 mm)");
    if (tangent) {
        if (d2.verts.size() < 2) throw std::invalid_argument("łuk styczny potrzebuje poprzedniego odcinka — narysuj najpierw linię albo łuk");
        c2::Path tmp;
        tmp.v = d2.verts;
        P direction = c2::segment_direction(c2::segment(tmp, d2.verts.size() - 2), true);
        last.bulge = c2::bulge_tangent(last.p, direction, end);
    } else if (std::fabs(through[0] - end[0]) < 1e-9 && std::fabs(through[1] - end[1]) < 1e-9) {
        last.bulge = 0;
    } else {
        last.bulge = c2::bulge_three_points(last.p, through, end);
    }
    d2.verts.push_back({end, 0});
}

void Modeler::finish_polyline(bool close) {
    try {
        if (d2.verts.size() < 2) throw std::invalid_argument("kontur wymaga co najmniej dwóch punktów");
        c2::Path path;
        path.v = d2.verts;
        path.closed = close;
        path.kind = close ? "kontur" : "linia";
        if (close) c2::validate_profile(path, "Kontur");
        else path.v.back().bulge = 0;
        add_shape(path, close ? "kontur zamknięty" : "linię");
        reset_draft_2d();
        status.set(info2d(tool2).hint);
    } catch (const std::exception& e) {
        reject_2d("zakończenie konturu", e.what());
    }
    draw_overlay_2d();
}

void Modeler::handle_point_2d(const P& p, bool shift) {
    const std::string t = tool2;
    const double tol = 10 / std::max(camera.scale, 1e-6);
    auto pick_shape = [&]() -> std::optional<size_t> {
        std::optional<size_t> best;
        double best_d = tol;
        for (size_t i = 0; i < shapes.size(); ++i) {
            double d = c2::distance_to_path(shapes[i], p);
            if (d <= best_d) { best = i; best_d = d; }
        }
        if (best) return best;
        double best_area = 1e300;
        for (size_t i = 0; i < shapes.size(); ++i)
            if (inside_path(shapes[i], p)) {
                double a = c2::area(shapes[i]);
                if (a < best_area) { best_area = a; best = i; }
            }
        return best;
    };
    auto pick_vertex = [&]() -> std::optional<std::pair<size_t, size_t>> {
        std::optional<std::pair<size_t, size_t>> best;
        double best_d = tol;
        for (size_t i = 0; i < shapes.size(); ++i)
            if (auto v = c2::nearest_vertex(shapes[i], p, tol)) {
                double d = dist(shapes[i].v[*v].p, p);
                if (d <= best_d) { best = std::make_pair(i, *v); best_d = d; }
            }
        return best;
    };

    if (t == "select2") {
        if (auto i = pick_shape()) {
            shape_sel = i;
            note("cad2d", "wybrano kształt: " + c2::describe(shapes[*i]));
            status.set(c2::describe(shapes[*i]) + (shapes[*i].closed ? std::format(" · pole {} cm²", num(c2::area(shapes[*i]) / 100)) : std::string()));
        } else if (auto id = pick_part_2d(p)) {
            if (!shift) selection.clear();
            selection.insert(*id);
            shape_sel.reset();
            auto found = std::find_if(app->project->parts.begin(), app->project->parts.end(), [&](const core::Part& q) { return q.id == *id; });
            note("cad2d", "wybrano płytę z projektu: " + (found != app->project->parts.end() ? found->name : *id));
            status.set("Wybrano płytę „" + (found != app->project->parts.end() ? found->name : *id) + "” — „Wczytaj obrys zaznaczonej płyty” otworzy jej kształt do edycji.");
        } else {
            shape_sel.reset();
            if (!shift) selection.clear();
            status.set(info2d(t).hint);
        }
        refresh_panel();
        request_scene();
        return;
    }

    if (t == "line" || t == "arc") {
        if (d2.stage == 0) {
            d2.verts = {{p, 0}};
            d2.stage = 1;
            d2.seg = t == "arc" ? "arc3" : "line";
            note("cad2d", std::format("początek {} w ({}, {}) mm", t == "arc" ? "łuku" : "konturu", num(p[0]), num(p[1])));
            status.set(t == "arc" ? "Kliknij punkt pośredni łuku." : "Klikaj kolejne punkty; A = łuk, T = łuk styczny, C = zamknij, Enter = koniec linii.");
            return;
        }
        const bool near_first = d2.verts.size() >= 2 && dist(p, d2.verts[0].p) < std::max(tol, .5);
        if (d2.seg == "line") {
            if (near_first && d2.verts.size() >= 3) { finish_polyline(true); return; }
            if (near_first) throw std::invalid_argument("kontur zamknięty z prostych odcinków potrzebuje co najmniej trzech punktów (albo użyj łuków: klawisz A)");
            if (dist(p, d2.verts.back().p) < .5) throw std::invalid_argument("punkt pokrywa się z poprzednim (odległość < 0,5 mm)");
            d2.verts.push_back({p, 0});
            return;
        }
        if (d2.seg == "arc3") {
            if (!d2.arc_through) {
                if (dist(p, d2.verts.back().p) < .5) throw std::invalid_argument("punkt pośredni pokrywa się z początkiem łuku");
                d2.arc_through = p;
                status.set("Kliknij koniec łuku.");
                return;
            }
            P through = *d2.arc_through;
            if (near_first && d2.verts.size() >= 2) {
                add_draft_segment(d2.verts[0].p, through, false);
                d2.verts.pop_back();
                d2.arc_through.reset();
                finish_polyline(true);
                return;
            }
            add_draft_segment(p, through, false);
            d2.arc_through.reset();
            status.set("Kolejny odcinek: kliknij punkt (A = łuk, T = styczny, L = prosty). C zamyka, Enter kończy.");
            return;
        }
        // łuk styczny do poprzedniego odcinka
        if (near_first && d2.verts.size() >= 2) {
            add_draft_segment(d2.verts[0].p, p, true);
            d2.verts.pop_back();
            finish_polyline(true);
            return;
        }
        add_draft_segment(p, p, true);
        return;
    }

    if (t == "rect" || t == "rrect") {
        if (d2.stage == 0) {
            d2.first = p;
            d2.stage = 1;
            status.set("Kliknij przeciwległy narożnik albo wpisz „600x400” i Enter.");
            return;
        }
        P a = *d2.first;
        if (std::fabs(a[0] - p[0]) < .5 || std::fabs(a[1] - p[1]) < .5) throw std::invalid_argument("prostokąt ma wymiar mniejszy niż 0,5 mm");
        c2::Path path = t == "rect" ? c2::rectangle(a, p) : c2::rounded_rectangle(a, p, number_field(radius_var.get(), "Promień zaokrąglenia"));
        add_shape(path, t == "rect" ? "prostokąt" : "prostokąt z zaokrągleniem");
        reset_draft_2d();
        status.set(info2d(t).hint);
        return;
    }

    if (t == "circle") {
        if (d2.stage == 0) { d2.first = p; d2.stage = 1; status.set("Kliknij punkt na okręgu albo wpisz promień i Enter."); return; }
        double r = dist(*d2.first, p);
        if (r < .25) throw std::invalid_argument("promień okręgu jest mniejszy niż 0,25 mm");
        add_shape(c2::circle(*d2.first, r), "okrąg");
        reset_draft_2d();
        status.set(info2d(t).hint);
        return;
    }

    if (t == "polygon") {
        if (d2.stage == 0) { d2.first = p; d2.stage = 1; status.set("Kliknij wierzchołek albo wpisz promień i Enter."); return; }
        double r = dist(*d2.first, p);
        int sides = (int)number_field(sides_var.get(), "Liczba boków");
        double start = std::atan2(p[1] - (*d2.first)[1], p[0] - (*d2.first)[0]) * 180 / c2::PI;
        add_shape(c2::regular_polygon(*d2.first, r, sides, start), std::format("{}-kąt foremny", sides));
        reset_draft_2d();
        status.set(info2d(t).hint);
        return;
    }

    if (t == "slot") {
        if (d2.stage == 0) { d2.first = p; d2.stage = 1; status.set("Kliknij środek drugiego końca albo wpisz długość osi i Enter."); return; }
        add_shape(c2::slot(*d2.first, p, number_field(slot_width_var.get(), "Szerokość szczeliny")), "szczelinę");
        reset_draft_2d();
        status.set(info2d(t).hint);
        return;
    }

    if (t == "fillet" || t == "chamfer") {
        auto v = pick_vertex();
        if (!v) throw std::invalid_argument("kliknij narożnik (wierzchołek) kształtu");
        double value = number_field(radius_var.get(), t == "fillet" ? "Promień zaokrąglenia" : "Odległość ścięcia");
        c2::Path changed = t == "fillet" ? c2::fillet_vertex(shapes[v->first], v->second, value) : c2::chamfer_vertex(shapes[v->first], v->second, value);
        replace_shape(v->first, changed, t == "fillet" ? std::format("zaokrąglono narożnik R{} mm", num(value)) : std::format("ścięto narożnik o {} mm", num(value)));
        return;
    }

    if (t == "movev") {
        if (d2.stage == 0) {
            if (auto v = pick_vertex()) {
                d2.index = v->first;
                d2.vertex = v->second;
                d2.base = shapes[v->first].v[v->second].p;
                status.set("Kliknij nowe położenie wierzchołka.");
            } else if (auto i = pick_shape()) {
                d2.index = i;
                d2.vertex.reset();
                d2.base = p;
                status.set("Kliknij miejsce, do którego przesunąć cały kształt.");
            } else {
                throw std::invalid_argument("kliknij wierzchołek albo kształt do przesunięcia");
            }
            d2.stage = 1;
            return;
        }
        size_t i = *d2.index;
        if (i >= shapes.size()) throw std::invalid_argument("kształt już nie istnieje");
        c2::Path changed = d2.vertex ? c2::move_vertex(shapes[i], *d2.vertex, p) : c2::translate(shapes[i], sub(p, d2.base));
        replace_shape(i, changed, d2.vertex ? "przesunięto wierzchołek" : std::format("przesunięto kształt o ({}, {}) mm", num(p[0] - d2.base[0]), num(p[1] - d2.base[1])));
        reset_draft_2d();
        status.set(info2d(t).hint);
        return;
    }

    if (t == "delv") {
        auto v = pick_vertex();
        if (!v) throw std::invalid_argument("kliknij wierzchołek kształtu");
        replace_shape(v->first, c2::remove_vertex(shapes[v->first], v->second), "usunięto wierzchołek");
        return;
    }

    if (t == "dim") {
        std::optional<std::pair<size_t, size_t>> best;
        double best_d = tol;
        for (size_t i = 0; i < shapes.size(); ++i)
            if (auto s = c2::nearest_segment(shapes[i], p, tol)) {
                double d = c2::distance_to_path(shapes[i], p);
                if (d <= best_d) { best = std::make_pair(i, *s); best_d = d; }
            }
        if (!best) throw std::invalid_argument("kliknij odcinek albo łuk kształtu");
        d2.index = best->first;
        d2.vertex = best->second;
        d2.stage = 1;
        shape_sel = best->first;
        c2::Segment seg = c2::segment(shapes[best->first], best->second);
        note("cad2d", std::format("wymiar odcinka {} kształtu: {}", best->second + 1, segment_label(seg)));
        status.set(std::format("Odcinek {}: {}.{}", best->second + 1, segment_label(seg), std::fabs(seg.bulge) < 1e-12 ? " Wpisz nową długość i Enter." : " Łuk zmienisz, przesuwając jego wierzchołki."));
        request_scene();
        return;
    }
    throw std::invalid_argument("narzędzie nie obsługuje kliknięcia");
}

bool Modeler::apply_typed_2d(const std::string& text) {
    try {
        const std::string t = tool2;
        if (text.empty()) {
            if (t == "line" || t == "arc") {
                if (d2.stage >= 1 && d2.verts.size() >= 2) { finish_polyline(false); return true; }
                throw std::invalid_argument("narysuj co najmniej dwa punkty albo wpisz wartość");
            }
            if (t == "select2") return extrude_shapes();
            throw std::invalid_argument("nic nie wpisano");
        }
        Typed in = parse_typed(text);
        if (in.values.empty()) throw std::invalid_argument("wpisz liczbę");
        // etap 0: dwie liczby = współrzędne punktu początkowego (jak „100;200” w programach CAD)
        const bool needs_point = t == "line" || t == "arc" || t == "rect" || t == "rrect" || t == "circle" || t == "polygon" || t == "slot";
        if (needs_point && d2.stage == 0) {
            if (in.values.size() != 2 || in.polar) throw std::invalid_argument("najpierw podaj punkt początkowy: kliknij albo wpisz współrzędne „x;y”");
            handle_point_2d({in.values[0], in.values[1]});
            typed.set("");
            draw_overlay_2d();
            return true;
        }
        auto direction_to_cursor = [&](const P& from) {
            P d = sub(cursor2, from);
            double l = std::hypot(d[0], d[1]);
            if (l < 1e-9) throw std::invalid_argument("poruszaj kursorem, aby wskazać kierunek");
            return mul(d, 1 / l);
        };
        auto direction = [&](const P& from) {
            if (in.polar) {
                double a = in.values[1] * c2::PI / 180;
                return P{std::cos(a), std::sin(a)};
            }
            return direction_to_cursor(from);
        };
        if (t == "line" || t == "arc") {
            if (in.values.size() != (in.polar ? 2u : 1u)) throw std::invalid_argument("wpisz długość („250”) albo długość i kąt („250<45”)");
            if (d2.seg == "arc3") throw std::invalid_argument("łuk 3-punktowy zadaj myszą (punkt pośredni i koniec) — długość można wpisać dla odcinków prostych i łuków stycznych");
            P last = d2.verts.back().p;
            P end = add(last, mul(direction(last), in.values[0]));
            if (d2.seg == "tangent") add_draft_segment(end, end, true);
            else {
                if (in.values[0] < .5) throw std::invalid_argument("długość odcinka musi wynosić co najmniej 0,5 mm");
                d2.verts.push_back({end, 0});
            }
        } else if (t == "rect" || t == "rrect") {
            if (in.values.size() != 2 || in.polar) throw std::invalid_argument("prostokąt wymaga dwóch wymiarów, np. 600x400");
            P a = *d2.first;
            double su = cursor2[0] >= a[0] ? 1 : -1, sv = cursor2[1] >= a[1] ? 1 : -1;
            P b{a[0] + su * in.values[0], a[1] + sv * in.values[1]};
            if (in.values[0] < .5 || in.values[1] < .5) throw std::invalid_argument("wymiary prostokąta muszą wynosić co najmniej 0,5 mm");
            add_shape(t == "rect" ? c2::rectangle(a, b) : c2::rounded_rectangle(a, b, number_field(radius_var.get(), "Promień zaokrąglenia")), t == "rect" ? "prostokąt" : "prostokąt z zaokrągleniem");
            reset_draft_2d();
        } else if (t == "circle") {
            if (in.values.size() != 1) throw std::invalid_argument("wpisz promień okręgu");
            if (in.values[0] < .25) throw std::invalid_argument("promień okręgu musi wynosić co najmniej 0,25 mm");
            add_shape(c2::circle(*d2.first, in.values[0]), "okrąg");
            reset_draft_2d();
        } else if (t == "polygon") {
            if (in.values.size() != 1) throw std::invalid_argument("wpisz promień wieloboku");
            add_shape(c2::regular_polygon(*d2.first, in.values[0], (int)number_field(sides_var.get(), "Liczba boków")), "wielokąt foremny");
            reset_draft_2d();
        } else if (t == "slot") {
            if (in.values.size() != (in.polar ? 2u : 1u)) throw std::invalid_argument("wpisz długość osi szczeliny („200” lub „200<30”)");
            P end = add(*d2.first, mul(direction(*d2.first), in.values[0]));
            add_shape(c2::slot(*d2.first, end, number_field(slot_width_var.get(), "Szerokość szczeliny")), "szczelinę");
            reset_draft_2d();
        } else if (t == "fillet" || t == "chamfer") {
            if (in.values.size() != 1) throw std::invalid_argument("wpisz jedną liczbę — promień albo odległość [mm]");
            if (in.values[0] <= 0) throw std::invalid_argument("wartość musi być dodatnia");
            radius_var.set(num(in.values[0]));
            note("cad2d", std::format("{} ustawiono na {} mm", t == "fillet" ? "promień zaokrąglenia" : "odległość ścięcia", num(in.values[0])));
            status.set(std::format("Ustawiono {} mm — kliknij narożnik.", num(in.values[0])));
        } else if (t == "dim") {
            if (!d2.index || !d2.vertex || *d2.index >= shapes.size()) throw std::invalid_argument("najpierw kliknij odcinek, którego wymiar chcesz zmienić");
            if (in.values.size() != 1) throw std::invalid_argument("wpisz jedną liczbę — nową długość [mm]");
            replace_shape(*d2.index, c2::set_segment_length(shapes[*d2.index], *d2.vertex, in.values[0]), std::format("zmieniono długość odcinka na {} mm", num(in.values[0])));
        } else {
            throw std::invalid_argument("w tym narzędziu nie ma czego wpisywać");
        }
        typed.set("");
        draw_overlay_2d();
        return true;
    } catch (const std::invalid_argument& e) {
        reject_2d("wpisana wartość „" + text + "”", e.what());
    } catch (const std::out_of_range& e) {
        reject_2d("wpisana wartość „" + text + "”", e.what());
    } catch (const std::exception& e) {
        reject_2d("wpisana wartość „" + text + "”", e.what());
    }
    return false;
}

void Modeler::on_key_2d(const std::string& keysym, int state) {
    const bool ctrl = (state & 4) != 0;
    static const std::vector<std::pair<std::string, char>> chars = {{"period", '.'}, {"comma", ','}, {"semicolon", ';'}, {"asterisk", '*'}, {"minus", '-'}, {"KP_Decimal", '.'}, {"less", '<'}};
    if (keysym == "Escape") {
        if (d2.stage != 0 || !typed.get().empty()) note("cad2d", std::format("anulowano rysowanie ({}, etap {})", info2d(tool2).label, d2.stage));
        reset_draft_2d();
        typed.set("");
        shape_sel.reset();
        status.set(info2d(tool2).hint);
        draw_overlay_2d();
        request_scene();
        return;
    }
    if (keysym == "Return" || keysym == "KP_Enter") { apply_typed_2d(typed.get()); return; }
    if (keysym == "BackSpace") {
        std::string s = typed.get();
        if (!s.empty()) { s.pop_back(); typed.set(s); }
        else if ((tool2 == "line" || tool2 == "arc") && d2.stage == 1) {
            if (d2.arc_through) d2.arc_through.reset();
            else if (d2.verts.size() > 1) { d2.verts.pop_back(); d2.verts.back().bulge = 0; }
            else reset_draft_2d();
        }
        draw_overlay_2d();
        return;
    }
    if (keysym == "Delete") {
        if (shape_sel) remove_shape(*shape_sel);
        else if (!selection.empty()) erase_selected();
        else reject_2d("usunięcie", "nic nie wybrano — kliknij kształt albo płytę narzędziem Wybierz");
        return;
    }
    if (ctrl && (keysym == "z" || keysym == "Z")) { undo_2d(); return; }
    if (ctrl && (keysym == "y" || keysym == "Y")) { redo_2d(); return; }
    std::string name = keysym;
    if (name.rfind("KP_", 0) == 0 && name.size() == 4) name = name.substr(3);
    if (name.size() == 1 && std::string("0123456789").find(name[0]) != std::string::npos) { typed.set(typed.get() + name); draw_overlay_2d(); return; }
    for (const auto& [sym, ch] : chars)
        if (keysym == sym) { typed.set(typed.get() + ch); draw_overlay_2d(); return; }
    if ((keysym == "x" || keysym == "X") && !typed.get().empty()) { typed.set(typed.get() + "x"); draw_overlay_2d(); return; }
    if (keysym.size() != 1) return;
    char c = (char)std::tolower((unsigned char)keysym[0]);
    if ((tool2 == "line" || tool2 == "arc") && d2.stage == 1) {
        if (c == 'c') {
            if (d2.verts.size() < 2) { reject_2d("zamknięcie konturu", "kontur ma za mało punktów"); return; }
            finish_polyline(true);
            return;
        }
        if (c == 'a' || c == 't' || c == 'l') {
            d2.seg = c == 'a' ? "arc3" : c == 't' ? "tangent" : "line";
            d2.arc_through.reset();
            note("cad2d", std::string("następny odcinek: ") + (c == 'a' ? "łuk 3-punktowy" : c == 't' ? "łuk styczny" : "prosty"));
            status.set(c == 'a' ? "Kliknij punkt pośredni łuku." : c == 't' ? "Kliknij koniec łuku stycznego (albo wpisz długość cięciwy)." : "Kliknij koniec odcinka.");
            draw_overlay_2d();
            return;
        }
    }
    if (c == 'o') {
        ortho_var.set(!ortho_var.get());
        note("cad2d", ortho_var.get() ? "wymuszanie osi: włączone" : "wymuszanie osi: wyłączone");
        return;
    }
    for (const auto& tool_def : tools2d())
        if (std::tolower((unsigned char)tool_def.key[0]) == c) { set_tool2(tool_def.id); return; }
}

// ------------------------------------------------------------------ rysowanie

namespace {

struct Mapper {
    const core::Camera& camera;
    const mg::Plane& plane;
    double w, h;
    std::pair<double, double> origin, ux, vy;
    Mapper(const core::Camera& c, const mg::Plane& pl, double width, double height) : camera(c), plane(pl), w(width), h(height) {
        auto s = [&](double a, double b) {
            Vec3 q = camera.project(mg::from_plane(plane, a, b), w, h);
            return std::pair<double, double>{q[0], q[1]};
        };
        origin = s(0, 0);
        auto x = s(1, 0), y = s(0, 1);
        ux = {x.first - origin.first, x.second - origin.second};
        vy = {y.first - origin.first, y.second - origin.second};
    }
    std::pair<double, double> operator()(const P& p) const { return {origin.first + p[0] * ux.first + p[1] * vy.first, origin.second + p[0] * ux.second + p[1] * vy.second}; }
};

std::vector<double> path_xy(const Mapper& map, const c2::Path& path, double scale) {
    std::vector<double> xy;
    std::vector<P> pts = c2::tessellate(path, std::max(.03, .6 / std::max(scale, 1e-6)), 6000);
    for (const auto& p : pts) { auto s = map(p); xy.push_back(s.first); xy.push_back(s.second); }
    if (path.closed && pts.size() >= 2) { xy.push_back(xy[0]); xy.push_back(xy[1]); }
    return xy;
}

}  // namespace

void Modeler::draw_scene_2d() {
    if (canvas == nullptr || !winfo_exists()) return;
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10 || h < 10) return;
    if (need_fit) {
        double lo_x = 1e18, lo_y = 1e18, hi_x = -1e18, hi_y = -1e18;
        auto take = [&](const P& p) { lo_x = std::min(lo_x, p[0]); lo_y = std::min(lo_y, p[1]); hi_x = std::max(hi_x, p[0]); hi_y = std::max(hi_y, p[1]); };
        for (const auto& part : app->project->parts) {
            if (!part.visible) continue;
            for (const auto& face : mg::part_faces(part))
                for (const auto& q : face.polygon) { mg::Point ab = mg::to_plane(plane2d, q); take({ab[0], ab[1]}); }
        }
        for (const auto& s : shapes)
            if (s.v.size() >= 2) { auto b = c2::bounds(s); take(b[0]); take(b[1]); }
            else if (!s.v.empty()) take(s.v[0].p);
        if (lo_x > hi_x) { lo_x = 0; lo_y = 0; hi_x = 1200; hi_y = 800; }
        double dx = std::max(100.0, hi_x - lo_x), dy = std::max(100.0, hi_y - lo_y);
        auto angles = mg::camera_angles_for_plane(plane2d);
        camera.yaw = angles.first;
        camera.pitch = angles.second;
        camera.center = mg::from_plane(plane2d, (lo_x + hi_x) / 2, (lo_y + hi_y) / 2);
        camera.pan = {0, 0};
        camera.scale = std::min(40.0, std::max(.01, std::min(w / dx, h / dy) * .8));
        need_fit = false;
    }
    canvas->delete_("all");
    rebuild_refs();
    Mapper map(camera, plane2d, w, h);
    const double scale = std::max(camera.scale, 1e-6);

    // siatka: zakres płaszczyzny widoczny w oknie
    {
        double lo_x = 1e18, lo_y = 1e18, hi_x = -1e18, hi_y = -1e18;
        for (auto [sx, sy] : std::vector<std::pair<double, double>>{{0, 0}, {(double)w, 0}, {0, (double)h}, {(double)w, (double)h}}) {
            mg::Ray ray = mg::unproject(camera, sx, sy, w, h);
            if (auto t = mg::ray_plane(ray, plane2d)) {
                mg::Point ab = mg::to_plane(plane2d, mg::at(ray, *t));
                lo_x = std::min(lo_x, ab[0]); hi_x = std::max(hi_x, ab[0]); lo_y = std::min(lo_y, ab[1]); hi_y = std::max(hi_y, ab[1]);
            }
        }
        double step = grid_step();
        if (lo_x <= hi_x && (hi_x - lo_x) / step < 400 && (hi_y - lo_y) / step < 400) {
            for (double x = std::ceil(lo_x / step) * step; x <= hi_x; x += step) {
                bool major = std::fabs(std::fmod(std::round(x / step), 5)) < 1e-9;
                auto a = map({x, lo_y}), b = map({x, hi_y});
                canvas->create_line({a.first, a.second, b.first, b.second}, {{"fill", major ? "#c9d4dc" : "#dfe7ec"}, {"width", 1}, {"tags", "grid"}});
            }
            for (double y = std::ceil(lo_y / step) * step; y <= hi_y; y += step) {
                bool major = std::fabs(std::fmod(std::round(y / step), 5)) < 1e-9;
                auto a = map({lo_x, y}), b = map({hi_x, y});
                canvas->create_line({a.first, a.second, b.first, b.second}, {{"fill", major ? "#c9d4dc" : "#dfe7ec"}, {"width", 1}, {"tags", "grid"}});
            }
        }
        if (lo_x <= hi_x) {
            auto a = map({lo_x, 0}), b = map({hi_x, 0});
            canvas->create_line({a.first, a.second, b.first, b.second}, {{"fill", "#d46a6a"}, {"width", 1}, {"tags", "grid"}});
            auto c = map({0, lo_y}), d = map({0, hi_y});
            canvas->create_line({c.first, c.second, d.first, d.second}, {{"fill", "#5fa86a"}, {"width", 1}, {"tags", "grid"}});
        }
    }

    // płyty projektu (te same co w 3D): ściany równoległe do płaszczyzny jako obrysy, leżące w płaszczyźnie wypełnione
    for (const auto& part : app->project->parts) {
        if (!part.visible) continue;
        const bool selected = selection.contains(part.id);
        for (const auto& face : mg::part_faces(part)) {
            std::vector<double> xy;
            for (const auto& q : face.polygon) {
                mg::Point ab = mg::to_plane(plane2d, q);
                auto s = map({ab[0], ab[1]});
                xy.push_back(s.first); xy.push_back(s.second);
            }
            if (xy.size() < 4) continue;
            const bool parallel = std::fabs(mg::dot(face.plane.n, plane2d.n)) > .99;
            const double offset = std::fabs(mg::dot(mg::sub(face.plane.origin, plane2d.origin), plane2d.n));
            const bool on_plane = parallel && offset < .5;
            const std::string color = selected ? "#ed8a32" : on_plane ? "#2c6e91" : parallel ? "#9fb3c2" : "#b6c3cd";
            if (parallel) canvas->create_polygon(xy, {{"fill", on_plane ? (selected ? "#fbe3c8" : "#d7e8f3") : ""}, {"outline", color}, {"width", on_plane || selected ? 2 : 1}, {"tags", "plates"}});
            else {
                xy.push_back(xy[0]); xy.push_back(xy[1]);
                canvas->create_line(xy, {{"fill", color}, {"width", 1}, {"tags", "plates"}});
            }
        }
    }

    // szkic
    for (size_t i = 0; i < shapes.size(); ++i) {
        try {
            const bool sel = shape_sel && *shape_sel == i;
            auto xy = path_xy(map, shapes[i], scale);
            if (xy.size() >= 4) canvas->create_line(xy, {{"fill", sel ? "#ed8a32" : shapes[i].closed ? "#12507a" : "#4d6578"}, {"width", sel ? 3 : 2}, {"tags", "sketch"}});
            if (sel) {
                for (const auto& v : shapes[i].v) {
                    auto s = map(v.p);
                    canvas->create_rectangle(s.first - 3, s.second - 3, s.first + 3, s.second + 3, {{"fill", "#ffffff"}, {"outline", "#ed8a32"}, {"width", 1}, {"tags", "sketch"}});
                }
                for (size_t k = 0; k < c2::segment_count(shapes[i]); ++k) {
                    c2::Segment seg = c2::segment(shapes[i], k);
                    if (c2::segment_length(seg) * scale < 40) continue;   // za krótki, żeby opisać
                    auto s = map(c2::segment_point(seg, .5));
                    canvas->create_text(s.first + 6, s.second - 8, {{"text", segment_label(seg)}, {"fill", "#7a4a12"}, {"anchor", "w"}, {"font", "{Segoe UI} 8"}, {"tags", "sketch"}});
                }
            }
        } catch (const std::exception&) {
            // zdegenerowany kształt — jest wypisany na liście, ale nie da się go narysować
        }
    }
    draw_overlay_2d();
}

void Modeler::draw_overlay_2d() {
    if (canvas == nullptr || !winfo_exists()) return;
    canvas->delete_("overlay");
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10 || h < 10) return;
    Mapper map(camera, plane2d, w, h);
    const double scale = std::max(camera.scale, 1e-6);
    const std::string tag = "overlay";
    auto label = [&](const P& p, const std::string& text, int dx = 12, int dy = -12) {
        auto s = map(p);
        canvas->create_text(s.first + dx, s.second + dy, {{"text", text}, {"fill", "#123c49"}, {"anchor", "w"}, {"font", "{Segoe UI} 9 bold"}, {"tags", tag}});
    };
    auto draw_preview = [&](const c2::Path& path, const std::string& color = "#2f80c8") {
        try {
            auto xy = path_xy(map, path, scale);
            if (xy.size() >= 4) canvas->create_line(xy, {{"fill", color}, {"width", 2}, {"dash", "4 3"}, {"tags", tag}});
        } catch (const std::exception&) {}
    };
    const P cur = cursor2;
    const std::string t = tool2;
    try {
        if ((t == "line" || t == "arc") && d2.stage >= 1 && !d2.verts.empty()) {
            c2::Path done;
            done.v = d2.verts;
            if (done.v.size() >= 2) {
                done.v.pop_back();   // ostatni wierzchołek to początek bieżącego odcinka
                done.v.push_back(d2.verts.back());
                done.v.back().bulge = 0;
                try { auto xy = path_xy(map, done, scale); if (xy.size() >= 4) canvas->create_line(xy, {{"fill", "#2f80c8"}, {"width", 2}, {"tags", tag}}); } catch (const std::exception&) {}
            }
            // bieżący odcinek do kursora
            c2::Path rubber;
            rubber.v = {d2.verts.back(), {cur, 0}};
            rubber.v[0].bulge = 0;
            if (d2.seg == "arc3" && d2.arc_through && dist(*d2.arc_through, rubber.v[0].p) > .1 && dist(cur, rubber.v[0].p) > .1) {
                try { rubber.v[0].bulge = c2::bulge_three_points(rubber.v[0].p, *d2.arc_through, cur); } catch (const std::exception&) {}
            } else if (d2.seg == "tangent" && d2.verts.size() >= 2 && dist(cur, rubber.v[0].p) > .1) {
                try {
                    c2::Path tmp;
                    tmp.v = d2.verts;
                    rubber.v[0].bulge = c2::bulge_tangent(rubber.v[0].p, c2::segment_direction(c2::segment(tmp, d2.verts.size() - 2), true), cur);
                } catch (const std::exception&) {}
            }
            if (dist(cur, rubber.v[0].p) > .01) {
                draw_preview(rubber);
                label(cur, segment_label(c2::segment(rubber, 0)));
            }
            if (d2.arc_through) {
                auto s = map(*d2.arc_through);
                canvas->create_oval(s.first - 3, s.second - 3, s.first + 3, s.second + 3, {{"outline", "#0b7a4b"}, {"width", 2}, {"tags", tag}});
            }
            for (const auto& v : d2.verts) {
                auto s = map(v.p);
                canvas->create_rectangle(s.first - 3, s.second - 3, s.first + 3, s.second + 3, {{"fill", "#ffffff"}, {"outline", "#2f80c8"}, {"width", 1}, {"tags", tag}});
            }
        } else if ((t == "rect" || t == "rrect") && d2.stage == 1 && d2.first && (std::fabs((*d2.first)[0] - cur[0]) > .01 && std::fabs((*d2.first)[1] - cur[1]) > .01)) {
            double r = 0;
            if (t == "rrect") { try { r = number_field(radius_var.get(), "Promień"); } catch (const std::invalid_argument&) {} }
            draw_preview(t == "rect" ? c2::rectangle(*d2.first, cur) : c2::rounded_rectangle(*d2.first, cur, r));
            label(cur, std::format("{} × {} mm", num(std::fabs(cur[0] - (*d2.first)[0])), num(std::fabs(cur[1] - (*d2.first)[1]))));
        } else if (t == "circle" && d2.stage == 1 && d2.first && dist(*d2.first, cur) > .25) {
            draw_preview(c2::circle(*d2.first, dist(*d2.first, cur)));
            label(cur, std::format("R{} (Ø{}) mm", num(dist(*d2.first, cur)), num(2 * dist(*d2.first, cur))));
        } else if (t == "polygon" && d2.stage == 1 && d2.first && dist(*d2.first, cur) > .5) {
            int sides = 6;
            try { sides = (int)number_field(sides_var.get(), "Liczba boków"); } catch (const std::invalid_argument&) {}
            double start = std::atan2(cur[1] - (*d2.first)[1], cur[0] - (*d2.first)[0]) * 180 / c2::PI;
            draw_preview(c2::regular_polygon(*d2.first, dist(*d2.first, cur), sides, start));
            label(cur, std::format("R{} mm, {} boków", num(dist(*d2.first, cur)), sides));
        } else if (t == "slot" && d2.stage == 1 && d2.first && dist(*d2.first, cur) > .1) {
            double width = 20;
            try { width = number_field(slot_width_var.get(), "Szerokość"); } catch (const std::invalid_argument&) {}
            draw_preview(c2::slot(*d2.first, cur, width));
            label(cur, std::format("{} × {} mm", num(dist(*d2.first, cur) + width), num(width)));
        } else if (t == "movev" && d2.stage == 1 && d2.index && *d2.index < shapes.size()) {
            c2::Path moved = d2.vertex ? c2::move_vertex(shapes[*d2.index], *d2.vertex, cur) : c2::translate(shapes[*d2.index], sub(cur, d2.base));
            draw_preview(moved, "#0b7a4b");
            label(cur, std::format("Δ ({}, {}) mm", num(cur[0] - d2.base[0]), num(cur[1] - d2.base[1])));
        }
    } catch (const std::exception&) {
        // podgląd może być chwilowo niepoprawny (np. zbyt duży promień) — odrzucenie z powodem następuje dopiero przy zatwierdzeniu
    }
    if (!cursor2_label.empty()) {
        auto s = map(cur);
        canvas->create_rectangle(s.first - 4, s.second - 4, s.first + 4, s.second + 4, {{"outline", "#0b7a4b"}, {"width", 2}, {"tags", tag}});
        canvas->create_text(s.first + 10, s.second - 10, {{"text", cursor2_label}, {"fill", "#0b7a4b"}, {"anchor", "w"}, {"font", "{Segoe UI} 9"}, {"tags", tag}});
    }
    canvas->create_text(w - 12, h - 12, {{"text", std::format("x {}  y {} mm · {}", num(cur[0]), num(cur[1]), plane2d.label)}, {"fill", "#123c49"}, {"anchor", "se"}, {"font", "{Segoe UI} 9"}, {"tags", tag}});
    if (!typed.get().empty()) canvas->create_text(12, h - 12, {{"text", "Wpisano: " + typed.get()}, {"fill", "#123c49"}, {"anchor", "sw"}, {"font", "{Segoe UI} 10 bold"}, {"tags", tag}});
}

}  // namespace modeler_ui
