// Interactive local U/V sketching; shared Part remains the production source.
#include "sketch.h"
#include "journal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>

#include "app.h"
#include "assembly.h"
#include "features.h"
#include "fittings.h"
#include "native_model.h"
#include "opening_options.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "dxf_import.h"
#include "sketch_shapes.h"
#include "sketch_live.h"

namespace sketch {

using core::ValueError;
using Cmd = std::function<void()>;

namespace {

std::string g(double x) { return std::format("{:g}", x); }

double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

double degrees(double radians) { return radians * (180.0 / std::numbers::pi); }

json points_json(const std::vector<Point>& points) {
    json out = json::array();
    for (auto& p : points) out.push_back(json::array({p[0], p[1]}));
    return out;
}

std::vector<Point> points_from_json(const json& value) {
    std::vector<Point> out;
    for (auto& p : value) out.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return out;
}

// ROLES[label] — KeyError, gdy brak etykiety.
std::string role_value(const std::string& label) {
    for (auto& [name, value] : assembly::ROLES)
        if (name == label) return value;
    throw std::out_of_range("'" + label + "'");
}

bool is_role_label(const std::string& label) {
    for (auto& [name, value] : assembly::ROLES)
        if (name == label) return true;
    return false;
}

core::Part initial_part(App* app, const core::Part* part) {
    if (part) return *part;
    core::Part q;
    q.name = "Własny element";
    q.material = app->project->materials.at(0).id;
    return q;
}

double orientation(const std::vector<Point>& pts) {
    std::vector<double> terms;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % pts.size()];
        terms.push_back(a[0] * b[1] - b[0] * a[1]);
    }
    return py::sum(terms);
}

// cadquery: Workplane('XY').polyline(points).close().offset2D(distance, kind='intersection').vals()
// i wire.Edges() -> edge.startPoint(). Zwraca punkty początkowe krawędzi jedynego konturu.
std::vector<Point> offset_contour(const std::vector<Point>& pts, double distance) {
    BRepBuilderAPI_MakePolygon polygon;
    for (auto& p : pts) polygon.Add(gp_Pnt(p[0], p[1], 0));
    polygon.Close();
    if (!polygon.IsDone()) throw ValueError("Nie można zbudować konturu.");
    BRepOffsetAPI_MakeOffset offset;
    offset.Init(GeomAbs_Intersection);
    offset.AddWire(polygon.Wire());
    offset.Perform(distance);
    TopoDS_Shape result = offset.Shape();
    std::vector<TopoDS_Wire> wires;
    for (TopExp_Explorer explorer(result, TopAbs_WIRE); explorer.More(); explorer.Next()) wires.push_back(TopoDS::Wire(explorer.Current()));
    if (wires.size() != 1) throw ValueError("Odsunięcie daje kilka konturów. Zmniejsz odległość.");
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(wires[0], TopAbs_EDGE, edges);
    std::vector<Point> out;
    for (int i = 1; i <= edges.Extent(); ++i) {
        BRepAdaptor_Curve curve(TopoDS::Edge(edges.FindKey(i)));
        gp_Pnt point = curve.Value(curve.FirstParameter());
        out.push_back({point.X(), point.Y()});
    }
    return out;
}

}  // namespace

core::Part& find_part(core::Project& project, const std::string& id) {
    for (auto& p : project.parts)
        if (p.id == id) return p;
    throw py::StopIteration();
}

core::Part normalized_part(const core::Part& part, const std::vector<Point>& points_in) {
    if (points_in.size() < 3) throw ValueError("Kontur wymaga przynajmniej trzech punktów.");
    std::vector<Point> points;
    for (auto& [u, v] : points_in) points.push_back({core::finite(u), core::finite(v)});
    double u0 = points[0][0], v0 = points[0][1];
    for (auto& p : points) { u0 = std::min(u0, p[0]); v0 = std::min(v0, p[1]); }
    core::Part q = part;
    std::vector<Point> old = part.points();
    if (points != old) {
        if ((py::truthy(part.fillets) || py::truthy(part.chamfers)) &&
            (py::truthy(part.profile) || points.size() != old.size() || orientation(points) * orientation(old) <= 0))
            throw ValueError("Przed edycją punktów obrysu usuń fazy i zaokrąglenia kantów w edytorze CAD; zmieni się numeracja krawędzi.");
        // Manual point editing replaces the analytic generator definition.
        // Otherwise Part.validate would silently restore the previous outline.
        q.profile = json::object();
    }
    std::vector<std::vector<double>> shifted;
    for (auto& [u, v] : points) shifted.push_back({u - u0, v - v0});
    std::vector<Point> contour = core::validate_polygon(shifted);
    q.contour.clear();
    for (auto& p : contour) q.contour.push_back({p[0], p[1]});
    core::Point3 origin = core::local_world(part, u0, v0, 0);
    q.x = origin[0]; q.y = origin[1]; q.z = origin[2];
    q.holes.clear();
    for (auto& hole : part.holes) {
        if (hole.size() != 4) throw ValueError(hole.size() < 4 ? "not enough values to unpack (expected 4, got " + std::to_string(hole.size()) + ")"
                                                              : "too many values to unpack (expected 4)");
        q.holes.push_back({hole[0] - u0, hole[1] - v0, hole[2], hole[3]});
    }
    features::shift_features(q, u0, v0);
    double umax = q.contour.at(0).at(0), vmax = q.contour.at(0).at(1);
    for (auto& p : q.contour) { umax = std::max(umax, p[0]); vmax = std::max(vmax, p[1]); }
    std::vector<std::vector<double>> canonical{{0, 0}, {umax, 0}, {umax, vmax}, {0, vmax}};
    bool any_edge = std::any_of(part.edges.begin(), part.edges.end(), [](bool e) { return e; });
    if (any_edge && q.contour == canonical) {
        q.u = canonical[2][0];
        q.v = canonical[2][1];
        q.contour.clear();
    } else {
        q.edges = {false, false, false, false};
    }
    q.validate();
    return q;
}

// ---------------------------------------------------------------- Sketch

Sketch::Sketch(App* app_, const core::Part* part_)
    : dock_workspace::DockDocument(app_), part(initial_part(app_, part_)) {
    if (part_) original = *part_;
    if (part_) points = part.points();
    closed = part_ != nullptr;
    journal::event(app_, "szkic", part_ ? "otwarto szkic istniejącego elementu „" + part_->name + "”" : std::string("nowy szkic"));
    config = assembly::DEFAULTS;
    if (part.assembly.is_object() && part.assembly.contains("config") && part.assembly["config"].is_object())
        for (auto& [key, value] : part.assembly["config"].items()) config[key] = value;
    if (!part_) config["hinge_pattern"] = "hinge45";
    if (part_ && assembly::inferred_role(part) == "door" && !py::truthy(part.assembly)) {
        try {
            assembly::Frame f = assembly::frame(*app->project, part.group);
            if (std::abs(part.x + part.u - f.upper[0] + config.at("gap").get<double>()) < 1 &&
                part.x > f.lower[0] + config.at("gap").get<double>() + 1)
                config["hinge_side"] = "right";
        } catch (const ValueError&) {
        }
    }
    title("Szkic CAD 2D → element 3D");
    geometry(std::format("{}x{}+15+15", std::min(1200, winfo_screenwidth() - 60), std::min(850, winfo_screenheight() - 110)));
    minsize(1180, 600);
    transient(app);
    grab_set();

    auto* toolbar = new ttk::Frame(this, {{"padding", 8}});
    toolbar->pack({{"fill", "x"}});
    mode = tk::StringVar(!part_ ? "Prostokąt" : "Wielokąt");
    for (auto& [name, hint] : std::vector<std::pair<std::string, std::string>>{{"Wielokąt", "L"}, {"Prostokąt", "R"}, {"Okrąg", "C"}, {"Wielobok", "P"}, {"Łuk", "A"}, {"Przesuń", "M"}, {"Miarka", "T"}})
        (new ttk::Radiobutton(toolbar, {{"text", name + " (" + hint + ")"}, {"variable", mode}, {"value", name}, {"command", Cmd([this] { change_mode(); })}}))
            ->pack({{"side", "left"}, {"padx", 4}});
    sides = tk::StringVar("6");
    (new ttk::Label(toolbar, {{"text", "boki"}}))->pack({{"side", "left"}});
    (new ttk::Entry(toolbar, {{"textvariable", sides}, {"width", 3}}))->pack({{"side", "left"}, {"padx", 2}});
    snap = tk::BooleanVar(true);
    ortho = tk::BooleanVar(false);
    point_snap = tk::BooleanVar(true);
    snap_reason = "";
    guide_cache.reset();
    (new ttk::Checkbutton(toolbar, {{"text", "Siatka"}, {"variable", snap}}))->pack({{"side", "left"}});
    grid = tk::StringVar("10");
    (new ttk::Entry(toolbar, {{"textvariable", grid}, {"width", 5}}))->pack({{"side", "left"}});
    (new ttk::Label(toolbar, {{"text", "mm"}}))->pack({{"side", "left"}});
    (new ttk::Checkbutton(toolbar, {{"text", "Orto / Shift"}, {"variable", ortho}}))->pack({{"side", "left"}, {"padx", 6}});
    (new ttk::Checkbutton(toolbar, {{"text", "Punkty / środki"}, {"variable", point_snap}}))->pack({{"side", "left"}});
    for (auto& [label_, command] : std::vector<std::pair<std::string, Cmd>>{{"Cofnij", [this] { undo(); }}, {"Wyczyść", [this] { clear(); }},
                                                                           {"Zamknij kontur", [this] { close_contour(); }}, {"Dopasuj", [this] { fit(); }}})
        (new ttk::Button(toolbar, {{"text", label_}, {"command", command}}))->pack({{"side", "left"}, {"padx", 3}});

    auto* body = new ttk::Frame(this);
    body->pack({{"fill", "both"}, {"expand", true}});
    auto* sidebar = new ttk::Frame(body);
    sidebar->pack({{"side", "right"}, {"fill", "y"}});
    auto* viewport = new tk::Canvas(sidebar, {{"width", 305}, {"highlightthickness", 0}, {"bg", "#f6f3ee"}});
    auto* scrollbar = new ttk::Scrollbar(sidebar, {{"orient", "vertical"}, {"command", viewport->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    viewport->pack({{"side", "left"}, {"fill", "y"}, {"expand", true}});
    viewport->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    auto* side = new ttk::Frame(viewport, {{"padding", 10}});
    int side_window = viewport->create_window(0, 0, {{"window", side->path()}, {"anchor", "nw"}});
    side->bind("<Configure>", [viewport](tk::Event&) { responsive_ui::update_scrollregion(viewport); return std::string(); });
    viewport->bind("<Configure>", [viewport, side_window](tk::Event& e) { viewport->itemconfigure(side_window, {{"width", e.width}}); return std::string(); });
    canvas = new tk::Canvas(body, {{"bg", "#fbfaf7"}, {"highlightthickness", 0}, {"takefocus", true}});
    canvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* sketch_tools = new ttk::Frame(this, {{"padding", "8 2"}});
    sketch_tools->pack({{"fill", "x"}, {"before", body->path()}});
    for (auto& [label_, action] : std::vector<std::pair<std::string, Cmd>>{
             {"Odbij U", [this] { transform_sketch("mirror_u"); }}, {"Odbij V", [this] { transform_sketch("mirror_v"); }},
             {"Obróć 90°", [this] { transform_sketch("rotate"); }}, {"Odsuń kontur…", [this] { offset_sketch(); }},
             {"Odcinek poziomy", [this] { constrain_edge("horizontal"); }}, {"Odcinek pionowy", [this] { constrain_edge("vertical"); }},
             {"Długość odcinka…", [this] { constrain_edge("length"); }}})
        (new ttk::Button(sketch_tools, {{"text", label_}, {"command", action}}))->pack({{"side", "left"}, {"padx", 2}});
    auto* sketch_tools2 = new ttk::Frame(this, {{"padding", "8 2"}});
    sketch_tools2->pack({{"fill", "x"}, {"before", body->path()}});
    for (auto& [label_, action] : std::vector<std::pair<std::string, Cmd>>{
             {"Zaokrąglij narożnik…", [this] { fillet_selected(false); }}, {"Sfazuj narożnik…", [this] { fillet_selected(true); }},
             {"Usuń punkt (Del)", [this] { remove_selected_point(); }}, {"Odsuń kontur (F)", [this] { offset_sketch(); }},
             {"Importuj DXF…", [this] { import_dxf(); }}, {"Usuń linie pomocnicze", [this] { clear_guides(); }}})
        (new ttk::Button(sketch_tools2, {{"text", label_}, {"command", action}}))->pack({{"side", "left"}, {"padx", 2}});

    (new ttk::Label(side, {{"text", "1. Wybierz mebel i funkcję"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "0 8"}});
    std::vector<std::string> groups;
    for (auto& p : app->project->parts)
        if (!py::truthy(py::get(p.assembly, "generated_by")) && std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
    core::Part* selected_part = app->selected_part();
    std::string initial_group = part_ ? part.group : selected_part ? selected_part->group : !groups.empty() ? groups[0] : "Mebel";
    target = tk::StringVar(initial_group);
    (new ttk::Combobox(side, {{"textvariable", target}, {"values", groups}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"pady", 3}});
    std::string role_label = "Inny / swobodny element";
    for (auto& [label_, value] : assembly::ROLES)
        if (value == assembly::inferred_role(part)) { role_label = label_; break; }
    role = tk::StringVar(role_label);
    std::vector<std::string> role_labels;
    for (auto& [label_, value] : assembly::ROLES) role_labels.push_back(label_);
    role_combo = new ttk::Combobox(side, {{"textvariable", role}, {"values", role_labels}, {"state", "readonly"}});
    role_combo->pack({{"fill", "x"}, {"pady", 3}});
    role_combo->bind("<<ComboboxSelected>>", [this](tk::Event&) { role_changed(); return std::string(); });
    automatic = tk::BooleanVar(py::truthy(config.at("auto_hardware")));
    (new ttk::Checkbutton(side, {{"text", "Generuj okucia i wiercenia"}, {"variable", automatic}}))->pack({{"anchor", "w"}});
    hinge_side = tk::StringVar(config.at("hinge_side") == "left" ? "Lewy" : "Prawy");
    (new ttk::Label(side, {{"text", "Zawiasy na boku"}}))->pack({{"anchor", "w"}});
    hinge_combo = new ttk::Combobox(side, {{"textvariable", hinge_side}, {"values", std::vector<std::string>{"Lewy", "Prawy"}}, {"state", "readonly"}});
    hinge_combo->pack({{"fill", "x"}});
    level = tk::StringVar(py::str(config.at("level")));
    (new ttk::Label(side, {{"text", "Poziom półki / szuflady od dołu [mm]"}}))->pack({{"anchor", "w"}});
    level_entry = new ttk::Entry(side, {{"textvariable", level}});
    level_entry->pack({{"fill", "x"}});
    replace_existing = tk::BooleanVar(false);
    (new ttk::Checkbutton(side, {{"text", "Zastąp istniejący bok / górę / dół"}, {"variable", replace_existing}}))->pack({{"anchor", "w"}, {"pady", 3}});
    (new ttk::Button(side, {{"text", "Parametry okuć i szuflady…"}, {"command", Cmd([this] { recipe_settings(); })}}))->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Button(side, {{"text", "Dopasuj prostokąt do mebla"}, {"command", Cmd([this] { fit_to_furniture(); })}}))->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Label(side, {{"text", "Szybkie profile"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "10 4"}});
    {
        std::vector<std::string> profile_names;
        for (const auto& info : sketch_shapes::profile_catalog()) profile_names.push_back(info.name);
        profile_name = tk::StringVar("Kształt L");
        profile_width = tk::StringVar("600");
        profile_height = tk::StringVar("400");
        profile_param = tk::StringVar("100");
        profile_param_label = tk::StringVar("Grubość ramienia [mm]");
        auto* profile_combo = new ttk::Combobox(side, {{"textvariable", profile_name}, {"values", profile_names}, {"state", "readonly"}});
        profile_combo->pack({{"fill", "x"}, {"pady", 2}});
        profile_preview = new tk::Canvas(side, {{"width", 150}, {"height", 90}, {"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER}});
        profile_preview->pack({{"anchor", "w"}, {"pady", "2 4"}});
        profile_combo->bind("<<ComboboxSelected>>", [this](tk::Event&) {
            for (const auto& info : sketch_shapes::profile_catalog())
                if (info.name == profile_name.get()) {
                    profile_param_label.set(info.param_label.empty() ? "(bez dodatkowego parametru)" : info.param_label);
                    profile_param.set(g(info.default_param));
                }
            draw_profile_preview();
            return std::string();
        });
        auto* profile_row = new ttk::Frame(side);
        profile_row->pack({{"fill", "x"}});
        for (auto& [label_, var] : std::vector<std::pair<std::string, tk::StringVar>>{{"Szer.", profile_width}, {"Wys.", profile_height}}) {
            (new ttk::Label(profile_row, {{"text", label_}}))->pack({{"side", "left"}});
            (new ttk::Entry(profile_row, {{"textvariable", var}, {"width", 7}}))->pack({{"side", "left"}, {"padx", 3}});
        }
        (new ttk::Label(side, {{"textvariable", profile_param_label}, {"wraplength", 270}}))->pack({{"anchor", "w"}});
        auto* param_entry = new ttk::Entry(side, {{"textvariable", profile_param}});
        param_entry->pack({{"fill", "x"}});
        param_entry->bind("<KeyRelease>", [this](tk::Event&) { draw_profile_preview(); return std::string(); });
        draw_profile_preview();
        (new ttk::Button(side, {{"text", "Wstaw profil"}, {"command", Cmd([this] { insert_profile(); })}}))->pack({{"fill", "x"}, {"pady", 3}});
    }
    (new ttk::Label(side, {{"text", "2. Narysuj lub dopasuj kontur"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "10 6"}});
    for (auto& [key, label_] : std::vector<std::pair<std::string, std::string>>{{"name", "Nazwa"}, {"group", "Mebel / grupa"}, {"t", "Grubość [mm]"}}) {
        (new ttk::Label(side, {{"text", label_}}))->pack({{"anchor", "w"}});
        std::string initial = key == "name" ? part.name : key == "group" ? part.group : py::str(json(part.t));
        tk::StringVar var(initial);
        fields.insert_or_assign(key, var);
        (new ttk::Entry(side, {{"textvariable", var}}))->pack({{"fill", "x"}, {"pady", "0 6"}});
    }
    (new ttk::Label(side, {{"text", "Płaszczyzna (element swobodny)"}}))->pack({{"anchor", "w"}});
    plane = tk::StringVar(part.plane);
    plane_combo = new ttk::Combobox(side, {{"textvariable", plane}, {"values", std::vector<std::string>{"XY", "XZ", "YZ"}}, {"state", "readonly"}});
    plane_combo->pack({{"fill", "x"}});
    (new ttk::Label(side, {{"text", "Materiał"}}))->pack({{"anchor", "w"}, {"pady", "8 0"}});
    std::vector<std::string> material_names;
    for (auto& m : app->project->materials) material_names.push_back(m.name);
    material = new ttk::Combobox(side, {{"values", material_names}, {"state", "readonly"}});
    material->pack({{"fill", "x"}});
    {
        int index = -1;
        for (size_t i = 0; i < app->project->materials.size(); ++i)
            if (app->project->materials[i].id == part.material) { index = (int)i; break; }
        if (index < 0) throw py::StopIteration();
        material->current(index);
    }
    (new ttk::Label(side, {{"text", "Punkty U, V [mm]"}}))->pack({{"anchor", "w"}, {"pady", "12 0"}});
    listbox = new tk::Listbox(side, {{"height", 7}, {"exportselection", false}});
    listbox->pack({{"fill", "x"}});
    listbox->bind("<<ListboxSelect>>", [this](tk::Event&) { select_point(); return std::string(); });
    auto* row = new ttk::Frame(side);
    row->pack({{"fill", "x"}, {"pady", 4}});
    u = tk::StringVar("0");
    v = tk::StringVar("0");
    for (auto& [label_, var] : std::vector<std::pair<std::string, tk::StringVar>>{{"U", u}, {"V", v}}) {
        (new ttk::Label(row, {{"text", label_}}))->pack({{"side", "left"}});
        (new ttk::Entry(row, {{"textvariable", var}, {"width", 10}}))->pack({{"side", "left"}, {"padx", 3}});
    }
    row = new ttk::Frame(side);
    row->pack({{"fill", "x"}});
    (new ttk::Button(row, {{"text", "Dodaj punkt"}, {"command", Cmd([this] { numeric_point(); })}}))->pack({{"side", "left"}});
    (new ttk::Button(row, {{"text", "Zmień wybrany"}, {"command", Cmd([this] { edit_point(); })}}))->pack({{"side", "left"}});
    (new ttk::Label(side, {{"text", "Odcinek od ostatniego punktu"}}))->pack({{"anchor", "w"}, {"pady", "12 0"}});
    length = tk::StringVar("100");
    angle = tk::StringVar("0");
    row = new ttk::Frame(side);
    row->pack({{"fill", "x"}});
    for (auto& [label_, var] : std::vector<std::pair<std::string, tk::StringVar>>{{"mm", length}, {"°", angle}}) {
        (new ttk::Entry(row, {{"textvariable", var}, {"width", 10}}))->pack({{"side", "left"}});
        (new ttk::Label(row, {{"text", label_}}))->pack({{"side", "left"}, {"padx", 3}});
    }
    (new ttk::Button(side, {{"text", "Dodaj odcinek: długość + kąt"}, {"command", Cmd([this] { segment(); })}}))->pack({{"fill", "x"}, {"pady", 5}});
    (new ttk::Label(side, {{"text", "0° = +U, 90° = +V.\nRolka: zoom · PPM: przesuwanie\nEnter: zamknij · Backspace: cofnij\nKliknij pierwszy punkt, aby zamknąć.\nEdycja punktów: wybierz na liście.\nObrzeża konturu zostaną wyłączone.\n\nSkróty: L linia · R prostokąt · C okrąg · P wielobok · A łuk · F odsuń · M przesuń punkt · T miarka (G = linia pomocnicza) · Del usuń punkt.\nStrzałki blokują oś U/V, Shift blokuje wykryty kierunek, Esc anuluje.\nPo zamknięciu konturu kliknij napis wymiaru, aby zmienić długość odcinka."},
                                  {"wraplength", 270}}))->pack({{"anchor", "w"}, {"pady", 8}});
    auto* footer = new ttk::Frame(this, {{"padding", 6}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}, {"before", body->path()}});
    (new ttk::Button(footer, {{"text", "Podgląd montażu"}, {"command", Cmd([this] { preview(); })}}))->pack({{"side", "left"}, {"padx", 4}});
    (new ttk::Button(footer, {{"text", "Zapisz i rysuj następny"}, {"command", Cmd([this] { save(true); })}}))->pack({{"side", "left"}, {"padx", 4}});
    (new ttk::Button(footer, {{"text", "Nowy element"}, {"command", Cmd([this] { new_element(); })}}))->pack({{"side", "left"}, {"padx", 4}});
    (new ttk::Button(footer, {{"text", "3. Dodaj do mebla / zapisz zmiany"}, {"command", Cmd([this] { save(); })}, {"style", "Accent.TButton"}}))
        ->pack({{"side", "right"}, {"padx", 4}});
    value_text = tk::StringVar("");
    value_prompt = tk::StringVar("Długość");
    value_live = tk::StringVar("");
    auto* value_row = new ttk::Frame(this, {{"padding", "8 2"}});
    value_row->pack({{"side", "bottom"}, {"fill", "x"}, {"before", body->path()}});
    (new ttk::Label(value_row, {{"textvariable", value_prompt}, {"width", 11}, {"style", "Title.TLabel"}}))->pack({{"side", "left"}});
    auto* value_entry = new ttk::Entry(value_row, {{"textvariable", value_text}, {"width", 18}});
    value_entry->pack({{"side", "left"}, {"padx", 4}});
    value_entry->bind("<Return>", [this](tk::Event&) { value_apply(); canvas->focus_set(); return std::string("break"); });
    value_entry->bind("<Escape>", [this](tk::Event&) { value_text.set(""); canvas->focus_set(); return std::string("break"); });
    (new ttk::Label(value_row, {{"textvariable", value_live}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Label(value_row, {{"text", "Wpisz wartość i Enter, bez klikania w pole: 250 · 250<45 · 600x400 · promień"}}))->pack({{"side", "right"}});
    status = tk::StringVar("Kliknij pierwszy punkt lub wpisz U i V. Wszystkie wymiary w mm.");
    (new ttk::Label(this, {{"textvariable", status}, {"padding", 8}}))->pack({{"side", "bottom"}, {"fill", "x"}, {"before", body->path()}});
    canvas->bind("<Motion>", [this](tk::Event& e) { motion(e); return std::string(); });
    canvas->bind("<B1-Motion>", [this](tk::Event& e) { drag(e); return std::string(); });
    canvas->bind("<ButtonRelease-1>", [this](tk::Event& e) { release(e); return std::string(); });
    canvas->bind("<Button-1>", [this](tk::Event& e) { click(e); return std::string(); });
    canvas->bind("<MouseWheel>", [this](tk::Event& e) { zoom(e); return std::string(); });
    canvas->bind("<ButtonPress-3>", [this](tk::Event& e) { pan_start(e); return std::string(); });
    canvas->bind("<B3-Motion>", [this](tk::Event& e) { pan(e); return std::string(); });
    canvas->bind("<Configure>", [this](tk::Event&) { draw(); return std::string(); });
    canvas->bind("<KeyPress>", [this](tk::Event& e) { return std::string(key(e.keysym, e.state) ? "break" : ""); });
    bind("<Control-z>", [this](tk::Event&) { undo(); return std::string("break"); });
    protocol("WM_DELETE_WINDOW", [this] { cancel(); });
    target.trace_add("write", [this] { fields.at("group").set(target.get()); });
    update_controls();
    auto scroll_sidebar = [viewport](tk::Event& event) { viewport->yview_scroll(event.delta > 0 ? -1 : 1, "units"); return std::string("break"); };
    std::function<void(tk::Widget*)> bind_scroll = [&](tk::Widget* widget) {
        widget->bind("<MouseWheel>", scroll_sidebar);
        for (tk::Widget* child : widget->winfo_children()) bind_scroll(child);
    };
    bind_scroll(side);
    viewport->bind("<MouseWheel>", scroll_sidebar);
    sketch_live::install(this);
    mark_clean();
    after(80, [this] { fit(); });
}

json Sketch::_document_model() const {
    json model = json::object();
    model["part"] = core::asdict(part);
    model["config"] = config;
    model["points"] = points_json(points);
    model["closed"] = closed;
    return model;
}

void Sketch::role_changed() {
    if (fields.at("name").get() == "Własny element" || is_role_label(fields.at("name").get())) fields.at("name").set(role.get());
    fields.at("group").set(target.get());
    update_controls();
    if (auto_fit && auto_fit->get()) fit_to_furniture();
    else status.set("Narysuj kontur lub użyj Dopasuj prostokąt do mebla. Wymiary U/V zmieniają jednocześnie szkic i podgląd 3D.");
}

void Sketch::update_controls() {
    const std::string role_name = role_value(role.get());
    hinge_combo->configure({{"state", role_name == "door" ? "readonly" : "disabled"}});
    level_entry->configure({{"state", role_name == "shelf" || role_name == "drawer" ? "normal" : "disabled"}});
    plane_combo->configure({{"state", role_name == "other" ? "readonly" : "disabled"}});
}

json Sketch::current_config() {
    json c = config;
    c["level"] = number(level);
    c["hinge_side"] = hinge_side.get() == "Lewy" ? "left" : "right";
    c["auto_hardware"] = automatic.get();
    return c;
}

void Sketch::recipe_settings() {
    auto* dialog = new tk::Toplevel(this);
    dialog->title("Wzorce robocze — edytowalne parametry");
    dialog->geometry("690x650");
    dialog->transient(winfo_toplevel());
    dialog->grab_set();
    (new ttk::Label(dialog, {{"text", "Parametry przykładowe. Dobierz je do konkretnego okucia.\nLiczba zawiasów nie jest automatycznym doborem nośności."}, {"padding", 12}}))
        ->pack({{"fill", "x"}});
    auto* holder = new ttk::Frame(dialog);
    holder->pack({{"fill", "both"}, {"expand", true}});
    auto* cv = new tk::Canvas(holder, {{"highlightthickness", 0}});
    auto* scroll = new ttk::Scrollbar(holder, {{"orient", "vertical"}, {"command", cv->path() + " yview"}});
    scroll->pack({{"side", "right"}, {"fill", "y"}});
    cv->pack({{"fill", "both"}, {"expand", true}});
    cv->configure({{"yscrollcommand", scroll->path() + " set"}});
    auto* form = new ttk::Frame(cv, {{"padding", 10}});
    int item = cv->create_window(0, 0, {{"window", form->path()}, {"anchor", "nw"}});
    form->bind("<Configure>", [cv](tk::Event&) { responsive_ui::update_scrollregion(cv); return std::string(); });
    cv->bind("<Configure>", [cv, item](tk::Event& e) { cv->itemconfigure(item, {{"width", e.width}}); return std::string(); });
    std::vector<std::pair<std::string, tk::StringVar>> variables;
    std::vector<std::pair<std::string, std::string>> specs{{"gap", "Luz / szczelina [mm]"}, {"offset", "Przegroda od lewego wnętrza [mm]"}};
    specs.insert(specs.end(), assembly::PARAMETERS.begin(), assembly::PARAMETERS.end());
    for (auto& [key, label_] : specs) {
        auto* line = new ttk::Frame(form);
        line->pack({{"fill", "x"}, {"pady", 3}});
        (new ttk::Label(line, {{"text", label_}}))->pack({{"side", "left"}});
        tk::StringVar var(py::str(config.at(key)));
        variables.push_back({key, var});
        (new ttk::Entry(line, {{"textvariable", var}, {"width", 12}}))->pack({{"side", "right"}});
    }
    auto close = [this, dialog] { dialog->destroy(); grab_set(); };
    auto commit = [this, dialog, variables, close] {
        try {
            json values = config;
            for (auto& [key, var] : variables) values[key] = number(var);
            config = assembly::recipe(values);
            close();
        } catch (const ValueError& e) {
            tk::messagebox::showerror("Parametry", e.what(), dialog);
        }
    };
    (new ttk::Button(dialog, {{"text", "Zapisz parametry"}, {"command", Cmd(commit)}}))->pack({{"fill", "x"}, {"padx", 12}, {"pady", 8}});
    dialog->protocol("WM_DELETE_WINDOW", close);
}

void Sketch::fit_to_furniture() {
    journal::event(app, "szkic", "dopasowanie szkicu do mebla");
    try {
        const std::string role_name = role_value(role.get());
        auto [uu, vv] = assembly::suggested_size(*app->project, target.get(), role_name, number(fields.at("t")), current_config());
        if (std::min(uu, vv) <= 0) throw ValueError("Brak miejsca na element.");
        checkpoint();
        points = {{0, 0}, {uu, 0}, {uu, vv}, {0, vv}};
        closed = true;
        cursor.reset();
        mode.set("Prostokąt");
        update_list();
        fit();
        status.set(std::format("Dopasowano {} × {} mm. Sprawdź podgląd montażu i zapisz.", g(uu), g(vv)));
    } catch (const std::invalid_argument& e) {
        tk::messagebox::showerror("Dopasowanie", e.what(), this);
    } catch (const std::out_of_range& e) {
        tk::messagebox::showerror("Dopasowanie", e.what(), this);
    }
}

std::pair<core::ProjectPtr, std::string> Sketch::candidate() {
    ensure_current_project(true);
    if (original) {
        bool exists = std::any_of(app->project->parts.begin(), app->project->parts.end(), [&](const core::Part& p) { return p.id == original->id; });
        if (!exists) throw ValueError("Element został usunięty z projektu. Otwórz szkic ponownie lub rozpocznij nowy element.");
    }
    if (!closed) throw ValueError("Najpierw zamknij kontur.");
    core::Part base = part;
    const std::string role_name = role_value(role.get());
    // Generated drilling is recomputed from the new shape; manual drilling remains.
    auto without = [](json& operations, auto&& drop) {
        json kept = json::array();
        for (auto& op : operations)
            if (!drop(op)) kept.push_back(op);
        operations = kept;
    };
    if (role_name != "other") without(base.operations, [](const json& op) { return py::truthy(py::get(op, "owner_id")); });
    for (auto& [key, var] : fields) {
        if (key == "t") base.t = number(var);
        else if (key == "name") base.name = var.get();
        else if (key == "group") base.group = var.get();
    }
    if (imported_holes)
        for (auto& hole : base.holes) hole[3] = base.t;  // otwory z DXF są przelotowe: głębokość = grubość
    without(base.operations, [](const json& op) { return py::get(op, "recipe_id") == "handle"; });
    base.plane = plane.get();
    base.material = app->project->materials.at(material->current()).id;
    core::Part q = normalized_part(base, points);
    std::string group = role_name != "other" ? target.get() : fields.at("group").get();
    if (!original && replace_existing.get() && (role_name == "left" || role_name == "right" || role_name == "top" || role_name == "bottom")) {
        std::vector<const core::Part*> targets;
        for (auto& p : app->project->parts)
            if (p.group == group && assembly::inferred_role(p) == role_name) targets.push_back(&p);
        if (targets.size() != 1) throw ValueError("Zastępowanie wymaga dokładnie jednego elementu danej funkcji w meblu.");
        q.id = targets[0]->id;
        if (!targets[0]->holes.empty())
            throw ValueError("Element ma ręczne wiercenia. Wybierz go i użyj „Edytuj szkic”, aby je zachować.");
    }
    if (role_name == "other") {
        auto temp = std::make_shared<core::Project>(*app->project);
        std::vector<json> retained;
        for (auto& h : temp->hardware)
            if (py::get(h, "owner_id") == q.id && py::truthy(py::get(h, "manual"))) retained.push_back(h);
        assembly::clear_generated(*temp, q.id);
        std::erase_if(temp->parts, [&](const core::Part& p) { return p.id == q.id; });
        q.assembly = json::object();
        q.role = "other";
        without(q.operations, [](const json& op) { return py::truthy(py::get(op, "owner_id")); });
        auto peer = std::find_if(temp->parts.begin(), temp->parts.end(), [&](const core::Part& p) { return p.group == q.group; });
        if (peer != temp->parts.end()) q.layout = peer->layout;
        temp->parts.push_back(q);
        for (auto& h : retained) temp->hardware.push_back(h);
        fittings::refresh_handles(*temp, q.id);
        opening_options::refresh_openings(*temp, q.id);
        temp = assembly::rebuild_dependents(*temp, q.id);
        find_part(*temp, q.id);
        return {temp, q.id};
    }
    auto [temp, attached] = assembly::attach(*app->project, q, group, role_name, current_config());
    std::string id = attached->id;
    temp = assembly::rebuild_dependents(*temp, id);
    find_part(*temp, id);
    return {temp, id};
}

dock_workspace::VisualizationDocument* Sketch::preview() {
    try {
        auto [temp, id] = candidate();
        const core::Part& q = find_part(*temp, id);
        long long count = 0;
        for (auto& p : temp->parts) count += (long long)p.operations.size();
        for (auto& p : app->project->parts) count -= (long long)p.operations.size();
        std::vector<std::string> hardware;
        for (auto& h : temp->hardware)
            if (py::get(h, "owner_id") == q.id) hardware.push_back(g(h.at("qty").get<double>()) + " × " + py::str(h.at("name")));
        std::string joined;
        for (size_t i = 0; i < hardware.size(); ++i) joined += (i ? ", " : "") + hardware[i];
        std::string text = std::format("{} · {} · {} × {} × {} mm\n", q.group, role.get(), g(q.u), g(q.v), g(q.t));
        text += std::format("Pozycja X,Y,Z: {}, {}, {} · płaszczyzna {}\n", g(q.x), g(q.y), g(q.z), q.plane);
        text += "Okucia: " + (joined.empty() ? std::string("bez okuć") : joined);
        text += std::format("\nNowe formatki: {:+d} · zmiana liczby wierceń: {:+d}", (long long)temp->parts.size() - (long long)app->project->parts.size(), count);
        auto* dialog = new dock_workspace::VisualizationDocument(app, temp, q.id, "Podgląd montażu — " + q.name, text);
        auto close = [this, dialog] {
            dialog->destroy();
            if (winfo_exists()) workspace->select(this);
        };
        (new ttk::Button(dialog, {{"text", "Wróć do szkicu"}, {"command", Cmd(close)}}))->pack({{"pady", 8}});
        dialog->protocol("WM_DELETE_WINDOW", close);
        return dialog;
    } catch (const std::invalid_argument& e) {
        tk::messagebox::showerror("Montaż", e.what(), this);
    } catch (const std::out_of_range& e) {
        tk::messagebox::showerror("Montaż", e.what(), this);
    }
    return nullptr;
}

double Sketch::number(const tk::StringVar& var) { return core::finite(json(py::replace_all(var.get(), ",", "."))); }

void Sketch::checkpoint() {
    history.push_back({points, closed, part});
    if (history.size() > 60) history.erase(history.begin(), history.end() - 60);
}

bool Sketch::cancel() {
    journal::event(app, "szkic", std::format("anulowanie bieżącej operacji szkicu ({} punktów)", points.size()));
    if (has_changes() && !tk::messagebox::askyesno("Zamknij szkic", "Odrzucić niezapisane zmiany szkicu i jego parametrów?", winfo_toplevel())) return false;
    destroy();
    return true;
}

void Sketch::change_mode() {
    const std::string tool = mode.get();
    journal::event(app, "szkic", "narzędzie szkicu: " + tool + (pending.empty() ? "" : " (porzucono rozpoczęty kształt)"));
    pending.clear();
    arrow_lock.reset();
    value_text.set("");
    value_prompt.set(value_prompt_text());
    value_live.set("");
    if (tool == "Prostokąt") status.set(!points.empty() && !closed ? "Kliknij przeciwległy narożnik prostokąta albo wpisz szer;wys i Enter." : "Kliknij pierwszy narożnik prostokąta (potem drugi albo szer;wys + Enter).");
    else if (tool == "Okrąg") status.set("Kliknij środek okręgu, potem promień (albo wpisz promień i Enter).");
    else if (tool == "Wielobok") status.set("Kliknij środek wieloboku, potem promień (liczba boków w polu „boki”; można wpisać promień;boki + Enter).");
    else if (tool == "Przesuń") status.set(points.empty() ? "Najpierw narysuj kontur. Potem przeciągaj jego punkty myszą (przyciągają się do wskazówek)." : "Przeciągnij punkt konturu myszą. Zwolnij przycisk, aby zatwierdzić; kontur musi pozostać poprawny.");
    else if (tool == "Miarka") status.set("Miarka: kliknij dwa punkty, aby zmierzyć odległość. Klawisz G zamienia pomiar w linię pomocniczą (łapaną przez wskazówki).");
    else if (tool == "Łuk") status.set("Łuk przez trzy punkty: początek, punkt pośredni, koniec (początek = ostatni punkt konturu, jeśli jest).");
    else status.set(!points.empty() && !closed ? "Klikaj kolejne punkty; wpisz długość (np. 250 albo 250<45) i Enter." : "Klikaj kolejne punkty wielokąta.");
    draw();
}

void Sketch::set_tool(const std::string& tool) {
    static const std::set<std::string> known{"Wielokąt", "Prostokąt", "Okrąg", "Wielobok", "Łuk", "Przesuń", "Miarka"};
    if (!known.contains(tool)) throw ValueError("Nieznane narzędzie szkicu: " + tool);
    mode.set(tool);
    change_mode();
}

std::string Sketch::value_prompt_text() const {
    const std::string tool = mode.get();
    return tool == "Prostokąt" ? "Wymiary" : tool == "Okrąg" || tool == "Wielobok" ? "Promień" : tool == "Łuk" ? "Łuk" : tool == "Przesuń" ? "Punkt" : tool == "Miarka" ? "Odległość" : "Długość";
}

void Sketch::cancel_pending() {
    bool had = !pending.empty() || arrow_lock || !value_text.get().empty();
    pending.clear();
    arrow_lock.reset();
    value_text.set("");
    value_live.set("");
    cursor.reset();
    if (had) journal::event(app, "szkic", "Esc: anulowano rozpoczętą operację (narzędzie " + mode.get() + ")");
    draw();
}

std::pair<double, double> Sketch::screen(const Point& p) const { return {origin[0] + p[0] * scale, origin[1] - p[1] * scale}; }

std::vector<Sketch::Segment> Sketch::guide_geometry() {
    GuideKey key{app->project.get(), target.get(), role.get(), level.get(), plane.get(), fields.at("t").get()};
    if (guide_cache && guide_cache->first == key) return guide_cache->second;
    std::vector<Segment> segments;
    try {
        const std::string role_name = role_value(role.get());
        core::Part reference = part;
        reference.plane = plane.get();
        if (role_name != "other") {
            reference.role = role_name;
            std::tie(reference.u, reference.v) = assembly::suggested_size(*app->project, target.get(), role_name, reference.t, current_config());
            assembly::set_placement(reference, assembly::frame(*app->project, target.get()), current_config());
            auto peer = std::find_if(app->project->parts.begin(), app->project->parts.end(), [&](const core::Part& p) { return p.group == target.get(); });
            if (peer != app->project->parts.end()) reference.layout = peer->layout;
        }
        core::Point3 base = core::world(reference, 0, 0, 0);
        std::array<core::Point3, 2> axes;
        {
            int k = 0;
            for (core::Point3 point : {core::Point3{1, 0, 0}, core::Point3{0, 1, 0}}) {
                core::Point3 w = core::world(reference, point[0], point[1], point[2]);
                axes[k++] = {w[0] - base[0], w[1] - base[1], w[2] - base[2]};
            }
        }
        auto local_point = [&](const core::Point3& point) {
            core::Point3 difference{point[0] - base[0], point[1] - base[1], point[2] - base[2]};
            Point out{};
            for (int k = 0; k < 2; ++k)
                out[k] = py::sum({difference[0] * axes[k][0], difference[1] * axes[k][1], difference[2] * axes[k][2]});
            return out;
        };
        for (auto& p : app->project->parts) {
            if (p.group != target.get() || (original && p.id == original->id)) continue;
            std::vector<Point> pts = p.points();
            for (double thickness : {0.0, p.t}) {
                std::vector<Point> mapped;
                for (auto& [uu, vv] : pts) mapped.push_back(local_point(core::world(p, uu, vv, thickness)));
                for (size_t i = 0; i < mapped.size(); ++i) {
                    const Point& a = mapped[i];
                    const Point& b = mapped[(i + 1) % mapped.size()];
                    if (dist(a, b) > .01) segments.push_back({a, b});
                }
            }
        }
    } catch (const ValueError&) {
    }
    guide_cache = std::make_pair(key, segments);
    return segments;
}

Point Sketch::local(const tk::Event& event) {
    const std::string tool = mode.get();
    sketch_inference::Input in;
    in.raw = {(event.x - origin[0]) / scale, (origin[1] - event.y) / scale};
    in.tolerance = 9 / scale;
    in.points = points;
    in.closed = closed;
    if (drag_index && *drag_index < in.points.size()) in.points.erase(in.points.begin() + (long)*drag_index);
    for (auto& s : guide_geometry()) in.reference.push_back({s.first, s.second});
    for (auto& g : guides) in.reference.push_back({g.first, g.second});
    for (auto& q : pending) in.reference.push_back({q, q});
    in.centers = centers;
    if (tool == "Wielokąt" && !points.empty() && !closed) in.anchor = points.back();
    else if (tool == "Prostokąt" && !points.empty() && !closed) in.anchor = points.front();
    else if (tool == "Miarka" && !pending.empty()) in.anchor = pending.front();
    else if (tool == "Przesuń" && drag_index && points.size() > 1) in.anchor = points[(*drag_index + points.size() - 1) % points.size()];
    else if ((tool == "Okrąg" || tool == "Wielobok") && !pending.empty()) in.anchor = pending.front();
    else if (tool == "Łuk") in.anchor = !pending.empty() ? std::optional<Point>(pending.back()) : (!points.empty() && !closed ? std::optional<Point>(points.back()) : std::nullopt);
    in.snap_points = point_snap.get();
    if (snap.get()) {
        double step = number(grid);
        if (step <= 0) throw ValueError("Krok siatki musi być dodatni.");
        in.grid = true;
        in.grid_step = step;
    }
    const bool shift = (event.state & 1) != 0;
    if (arrow_lock && in.anchor) in.lock_direction = arrow_lock;
    else if (shift && in.anchor && inference.direction && sketch_inference::is_direction(inference.kind)) in.lock_direction = inference.direction;
    else in.ortho = (ortho.get() || shift) && (tool == "Wielokąt" || tool == "Przesuń") && in.anchor;
    inference = sketch_inference::infer(in);
    snap_reason = inference.kind == sketch_inference::Kind::None || inference.kind == sketch_inference::Kind::Grid ? std::string() : inference.tip;
    return inference.point;
}

void Sketch::motion(const tk::Event& event) {
    try {
        cursor = local(event);
        auto [uu, vv] = *cursor;
        std::string text = std::format("U {:.2f} · V {:.2f} mm", uu, vv);
        if (!snap_reason.empty()) text += " · przyciąganie: " + snap_reason;
        if (!points.empty() && !closed) {
            Point a = points.back();
            double du = uu - a[0], dv = vv - a[1];
            text += std::format("   ΔU {:.2f} · ΔV {:.2f} · długość {:.2f} mm · kąt {:.1f}°", du, dv, std::hypot(du, dv), degrees(std::atan2(dv, du)));
        }
        status.set(text);
        const std::string tool = mode.get();
        if (tool == "Wielokąt" && !points.empty() && !closed) value_live.set(std::format("{:.2f} mm", dist(points.back(), *cursor)));
        else if (tool == "Prostokąt" && !points.empty() && !closed) value_live.set(std::format("{:.2f} × {:.2f} mm", std::abs(uu - points[0][0]), std::abs(vv - points[0][1])));
        else if ((tool == "Okrąg" || tool == "Wielobok") && !pending.empty()) value_live.set(std::format("R {:.2f} mm", dist(pending[0], *cursor)));
        else value_live.set("");
        draw();
    } catch (const ValueError& e) {
        status.set(e.what());
    }
}

void Sketch::click(const tk::Event& event) {
    canvas->focus_set();
    if (mode.get() == "Przesuń") { press(event); return; }
    try {
        if (closed) {
            int edge = dimension_at(event.x, event.y);
            if (edge >= 0) { edit_dimension((size_t)edge); return; }
        }
        if (!closed && points.size() >= 3 && mode.get() == "Wielokąt") {
            auto s = screen(points[0]);
            if (std::hypot(event.x - s.first, event.y - s.second) < 12) {
                close_contour();
                return;
            }
        }
        add_point(local(event));
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Szkic", e.what(), this);
    }
}

void Sketch::add_point(const Point& p_in) {
    journal::event(app, "szkic", std::format("punkt #{}: u={:.1f} v={:.1f} (kontur {})", points.size() + 1, p_in[0], p_in[1], closed ? "zamknięty" : "otwarty"));
    if (closed) {
        if (tk::messagebox::askyesno("Następny element", "Kontur jest gotowy. Zapisać go i rozpocząć następny element?", this)) {
            if (!save(true)) return;
        } else return;
    }
    Point p{core::finite(p_in[0]), core::finite(p_in[1])};
    const std::string tool = mode.get();
    if (tool == "Przesuń") return;  // w tym narzędziu klik obsługuje press()/release()
    if (tool == "Miarka") {
        if (pending.empty()) {
            pending.push_back(p);
            status.set("Początek pomiaru ustawiony. Kliknij drugi punkt.");
            draw();
            return;
        }
        Point a = pending[0];
        pending.clear();
        measured = Segment{a, p};
        double du = p[0] - a[0], dv = p[1] - a[1];
        std::string text = std::format("{:.2f} mm (ΔU {:.2f}, ΔV {:.2f}, kąt {:.1f}°)", std::hypot(du, dv), du, dv, degrees(std::atan2(dv, du)));
        journal::event(app, "szkic", "pomiar miarką: " + text);
        status.set("Pomiar: " + text + " · G = zamień w linię pomocniczą.");
        value_live.set(std::format("{:.2f} mm", std::hypot(du, dv)));
        draw();
        return;
    }
    if (tool == "Okrąg" || tool == "Wielobok") {
        if (pending.empty()) {
            pending.push_back(p);
            arrow_lock.reset();
            status.set("Środek ustawiony. Kliknij punkt na okręgu albo wpisz promień i Enter.");
            draw();
            return;
        }
        double radius = dist(pending[0], p);
        if (radius < 0.5) throw ValueError("Promień jest za mały.");
        Point center = pending[0];
        if (tool == "Okrąg") finish_shape(sketch_shapes::circle(center, radius, 48), std::format("okrąg R={:.2f} w ({:.1f}; {:.1f})", radius, center[0], center[1]), center);
        else {
            long long n = (long long)std::lround(number(sides));
            finish_shape(sketch_shapes::regular_polygon(center, radius, (int)n, degrees(std::atan2(p[1] - center[1], p[0] - center[0]))),
                         std::format("wielobok {} boków R={:.2f} w ({:.1f}; {:.1f})", n, radius, center[0], center[1]), center);
        }
        return;
    }
    if (tool == "Łuk") {
        std::vector<Point> chain = pending;
        if (chain.empty() && !points.empty()) chain.push_back(points.back());
        chain.push_back(p);
        if (chain.size() < 3) {
            if (pending.empty() && !points.empty()) pending.push_back(points.back());
            pending.push_back(p);
            status.set(pending.size() == 1 ? "Początek łuku ustawiony. Kliknij punkt pośredni." : "Punkt pośredni ustawiony. Kliknij koniec łuku.");
            draw();
            return;
        }
        std::vector<Point> arc_points = sketch_shapes::arc_three_points(chain[0], chain[1], chain[2]);
        bool continues = !points.empty() && dist(points.back(), arc_points.front()) < 0.01;
        if (points.size() + arc_points.size() - (continues ? 1 : 0) > 128) throw ValueError("Maksymalnie 128 punktów — łuk nie mieści się w konturze.");
        checkpoint();
        for (size_t i = continues ? 1 : 0; i < arc_points.size(); ++i) points.push_back(arc_points[i]);
        pending.clear();
        arrow_lock.reset();
        cursor.reset();
        journal::event(app, "szkic", std::format("łuk przez 3 punkty: +{} punktów (kontur ma teraz {})", arc_points.size() - (continues ? 1 : 0), points.size()));
        update_list();
        draw();
        return;
    }
    if (points.size() >= 128) throw ValueError("Maksymalnie 128 punktów.");
    if (!points.empty() && dist(points.back(), p) < 0.01) throw ValueError("Punkt pokrywa się z poprzednim.");
    if (mode.get() == "Prostokąt" && !points.empty()) {
        Point a = points[0];
        if (a[0] == p[0] || a[1] == p[1]) throw ValueError("Prostokąt musi mieć dodatnią szerokość i wysokość.");
        checkpoint();
        points = {a, {p[0], a[1]}, p, {a[0], p[1]}};
        closed = true;
    } else {
        checkpoint();
        points.push_back(p);
    }
    arrow_lock.reset();
    cursor.reset();
    update_list();
    draw();
}

void Sketch::numeric_point() {
    journal::event(app, "szkic", "punkt z wartości liczbowych");
    try {
        add_point({number(u), number(v)});
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Punkt", e.what(), this);
    }
}

void Sketch::segment() {
    journal::event(app, "szkic", "odcinek o zadanej długości i kącie");
    try {
        if (points.empty()) throw ValueError("Najpierw dodaj punkt początkowy.");
        double len = number(length);
        double radians = number(angle) * (std::numbers::pi / 180.0);
        if (len <= 0) throw ValueError("Długość musi być dodatnia.");
        Point a = points.back();
        add_point({a[0] + len * std::cos(radians), a[1] + len * std::sin(radians)});
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Odcinek", e.what(), this);
    }
}

void Sketch::select_point() {
    auto selection = listbox->curselection();
    if (!selection.empty()) {
        auto [uu, vv] = points.at(selection[0]);
        u.set(std::format("{:.4f}", uu));
        v.set(std::format("{:.4f}", vv));
        draw();
    }
}

void Sketch::edit_point() {
    journal::event(app, "szkic", "edycja punktu");
    try {
        auto selection = listbox->curselection();
        if (selection.empty()) throw ValueError("Wybierz punkt na liście.");
        std::vector<Point> edited = points;
        edited.at(selection[0]) = {number(u), number(v)};
        if (closed) {
            core::Part base = part;
            json kept = json::array();
            for (auto& op : base.operations)
                if (!py::truthy(py::get(op, "owner_id"))) kept.push_back(op);
            base.operations = kept;
            normalized_part(base, edited);
        }
        checkpoint();
        points = edited;
        update_list();
        draw();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Punkt", e.what(), this);
    }
}

void Sketch::close_contour() {
    journal::event(app, "szkic", std::format("zamknięcie konturu ({} punktów)", points.size()));
    try {
        core::Part base = part;
        json kept = json::array();
        for (auto& op : base.operations)
            if (!py::truthy(py::get(op, "owner_id"))) kept.push_back(op);
        base.operations = kept;
        normalized_part(base, points);
        if (!closed) {
            checkpoint();
            closed = true;
        }
        cursor.reset();
        draw();
        status.set("Kontur zamknięty. Nadaj grubość i zapisz element do projektu.");
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Kontur", e.what(), this);
    }
}

void Sketch::undo() {
    journal::event(app, "szkic", std::format("cofnij w szkicu (historia: {})", history.size()));
    if (!history.empty()) {
        Snapshot last = std::move(history.back());
        history.pop_back();
        points = std::move(last.points);
        closed = last.closed;
        part = std::move(last.part);
        pending.clear();
        centers.clear();
        cursor.reset();
        update_list();
        draw();
    }
}

void Sketch::clear() {
    journal::event(app, "szkic", std::format("wyczyszczono kontur ({} punktów)", points.size()));
    checkpoint();
    points.clear();
    closed = false;
    cursor.reset();
    pending.clear();
    centers.clear();
    arrow_lock.reset();
    guides.clear();
    measured.reset();
    if (imported_holes) { part.holes.clear(); imported_holes = false; }
    update_list();
    draw();
}

void Sketch::update_list() {
    sketch_live::sync(this);
    listbox->delete_("0", "end");
    for (size_t i = 0; i < points.size(); ++i) listbox->insert("end", std::format("{}.   {:.2f} ; {:.2f}", i + 1, points[i][0], points[i][1]));
}

void Sketch::fit() {
    int w = std::max(canvas->winfo_width(), 100), h = std::max(canvas->winfo_height(), 100);
    std::vector<Point> pts = points.empty() ? std::vector<Point>{{0, 0}, {800, 600}} : points;
    double xmin = pts[0][0], xmax = pts[0][0], ymin = pts[0][1], ymax = pts[0][1];
    for (auto& p : pts) {
        xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
        ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
    }
    scale = std::min(std::max(w - 120, 30) / std::max(xmax - xmin, 100.0), std::max(h - 96, 30) / std::max(ymax - ymin, 100.0));
    origin = {w / 2.0 - (xmin + xmax) * scale / 2, h / 2.0 + (ymin + ymax) * scale / 2};
    update_list();
    draw();
}

void Sketch::zoom(const tk::Event& event) {
    double uu = (event.x - origin[0]) / scale, vv = (origin[1] - event.y) / scale;
    scale = std::min(30.0, std::max(.015, scale * (event.delta > 0 ? 1.15 : 1 / 1.15)));
    origin = {event.x - uu * scale, event.y + vv * scale};
    draw();
}

void Sketch::pan_start(const tk::Event& event) { pan_anchor = {event.x, event.y}; }

void Sketch::pan(const tk::Event& event) {
    origin[0] += event.x - pan_anchor.first;
    origin[1] += event.y - pan_anchor.second;
    pan_anchor = {event.x, event.y};
    draw();
}

void Sketch::label(double x, double y, const std::string& text, const std::string& color) {
    int item = canvas->create_text(x, y, {{"text", text}, {"fill", color}, {"font", "{Segoe UI} 9 bold"}});
    auto box = canvas->bbox(item);
    if (box && box->size() == 4) {
        int rect = canvas->create_rectangle((*box)[0], (*box)[1], (*box)[2], (*box)[3], {{"fill", "#fbfaf7"}, {"outline", ""}});
        canvas->tag_lower(rect, item);
    }
}

void Sketch::draw() {
    tk::Canvas* c = canvas;
    c->delete_("all");
    int w = c->winfo_width(), h = c->winfo_height();
    double step_grid;
    try {
        step_grid = std::max(.1, number(grid));
    } catch (const ValueError&) {
        step_grid = 10;
    }
    while (step_grid * scale < 24) step_grid *= 2;
    for (auto [screen_axis, limit, axis_origin] : std::vector<std::tuple<int, int, double>>{{0, w, origin[0]}, {1, h, origin[1]}}) {
        double step = step_grid * scale;
        long long count = std::max(0LL, (long long)(limit / step) + 2);
        for (long long i = 0; i < count; ++i) {
            double p = py::mod(axis_origin, step) + i * step;
            if (screen_axis == 0) c->create_line({p, 0, p, (double)h}, {{"fill", "#e8e4db"}});
            else c->create_line({0, p, (double)w, p}, {{"fill", "#e8e4db"}});
        }
    }
    c->create_line({0, origin[1], (double)w, origin[1]}, {{"fill", "#a93f3f"}});
    c->create_line({origin[0], 0, origin[0], (double)h}, {{"fill", "#36805b"}});
    for (auto& [a, b] : guide_geometry()) {
        auto sa = screen(a), sb = screen(b);
        c->create_line({sa.first, sa.second, sb.first, sb.second}, {{"fill", "#b4bec9"}, {"dash", "3 4"}});
    }
    for (auto& [ga_point, gb_point] : guides) {
        auto ga = screen(ga_point), gb = screen(gb_point);
        c->create_line({ga.first, ga.second, gb.first, gb.second}, {{"fill", "#c2399c"}, {"dash", "6 3"}});
    }
    if (measured) {
        auto ma = screen(measured->first), mb = screen(measured->second);
        c->create_line({ma.first, ma.second, mb.first, mb.second}, {{"fill", "#b06d27"}, {"width", 2}, {"arrow", "both"}});
        label((ma.first + mb.first) / 2, (ma.second + mb.second) / 2 - 12, std::format("{:.2f} mm", dist(measured->first, measured->second)), "#986022");
    }
    if (cursor) {
        for (const auto& guide : inference.guides) {
            auto ga = screen(guide.from), gb = screen(guide.to);
            c->create_line({ga.first, ga.second, gb.first, gb.second}, {{"fill", guide.color}, {"dash", "2 4"}, {"width", 1}});
        }
    }
    const std::string tool_now = mode.get();
    if (cursor && (tool_now == "Okrąg" || tool_now == "Wielobok" || tool_now == "Łuk")) {
        std::vector<Point> preview = shape_preview(*cursor);
        if (preview.size() >= 2) {
            std::vector<double> flat;
            for (auto& q : preview) { auto sq = screen(q); flat.push_back(sq.first); flat.push_back(sq.second); }
            if (tool_now == "Łuk" || preview.size() == 2) c->create_line(flat, {{"fill", "#b06d27"}, {"width", 2}});
            else c->create_polygon(flat, {{"fill", "#eef3e6"}, {"outline", "#b06d27"}, {"width", 2}, {"dash", "4 3"}});
        }
        if (!pending.empty() && tool_now != "Łuk") {
            auto sc = screen(pending[0]), sm = screen(*cursor);
            c->create_line({sc.first, sc.second, sm.first, sm.second}, {{"fill", "#b06d27"}, {"dash", "3 3"}});
            label((sc.first + sm.first) / 2, (sc.second + sm.second) / 2 - 12, std::format("R {:.2f} mm", dist(pending[0], *cursor)), "#986022");
        }
    }
    for (auto& q : pending) {
        auto sq = screen(q);
        c->create_oval(sq.first - 4, sq.second - 4, sq.first + 4, sq.second + 4, {{"fill", "#b06d27"}, {"outline", "white"}});
    }
    for (auto& q : centers) {
        auto sq = screen(q);
        c->create_line({sq.first - 5, sq.second, sq.first + 5, sq.second}, {{"fill", "#2e9e4f"}});
        c->create_line({sq.first, sq.second - 5, sq.first, sq.second + 5}, {{"fill", "#2e9e4f"}});
    }
    std::vector<Point> pts = points;
    if (cursor && !pts.empty() && !closed) {
        if (mode.get() == "Prostokąt") {
            Point a = pts[0], b = *cursor;
            pts = {a, {b[0], a[1]}, b, {a[0], b[1]}};
        } else pts.push_back(*cursor);
    }
    std::vector<std::pair<double, double>> coords;
    for (auto& p : pts) coords.push_back(screen(p));
    bool filled = closed || mode.get() == "Prostokąt";
    if (coords.size() >= 3 && filled) {
        std::vector<double> flat;
        for (auto& [x, y] : coords) { flat.push_back(x); flat.push_back(y); }
        c->create_polygon(flat, {{"fill", "#dbe6cf"}, {"outline", "#4e6b3f"}, {"width", 2}});
    }
    using ScreenPair = std::pair<std::pair<double, double>, std::pair<double, double>>;
    std::vector<ScreenPair> pairs;
    std::vector<Segment> local_pairs;
    for (size_t i = 0; i + 1 < coords.size(); ++i) {
        pairs.push_back({coords[i], coords[i + 1]});
        local_pairs.push_back({pts[i], pts[i + 1]});
    }
    if (filled && coords.size() >= 3) {
        pairs.push_back({coords.back(), coords.front()});
        local_pairs.push_back({pts.back(), pts.front()});
    }
    const bool rubber_band = cursor && !points.empty() && !closed && tool_now == "Wielokąt";
    for (size_t i = 0; i < pairs.size(); ++i) {
        auto& [a, b] = pairs[i];
        auto& [pa, pb] = local_pairs[i];
        const bool is_rubber = rubber_band && i + 1 == pairs.size();
        c->create_line({a.first, a.second, b.first, b.second}, {{"fill", is_rubber && sketch_inference::is_direction(inference.kind) ? inference.color : std::string("#4e6b3f")}, {"width", is_rubber ? 3 : 2}});
        if (!py::truthy(part.profile) && pts.size() <= 20 && std::hypot(a.first - b.first, a.second - b.second) >= 45)
            label((a.first + b.first) / 2, (a.second + b.second) / 2 - 12, std::format("{:.2f} mm", dist(pa, pb)));
    }
    std::set<int> selected_points;
    for (int index : listbox->curselection()) selected_points.insert(index);
    for (size_t i = 0; i < points.size(); ++i) {
        auto [x, y] = screen(points[i]);
        c->create_oval(x - 4, y - 4, x + 4, y + 4, {{"fill", "#4e6b3f"}, {"outline", "white"}});
        if ((!py::truthy(part.profile) && points.size() <= 20) || selected_points.count((int)i))
            c->create_text(x + 9, y + 10, {{"text", std::to_string(i + 1)}, {"anchor", "w"}, {"fill", "#345"}});
    }
    if (pts.size() >= 2) {
        double xmin = pts[0][0], xmax = pts[0][0], ymin = pts[0][1], ymax = pts[0][1];
        for (auto& p : pts) {
            xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
            ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
        }
        auto a = screen({xmin, ymin});
        auto b = screen({xmax, ymax});
        c->create_line({a.first, a.second + 20, b.first, a.second + 20}, {{"fill", "#b06d27"}, {"arrow", "both"}});
        label((a.first + b.first) / 2, a.second + 33, std::format("U {:.2f} mm", xmax - xmin), "#986022");
        c->create_line({b.first + 22, a.second, b.first + 22, b.second}, {{"fill", "#b06d27"}, {"arrow", "both"}});
        label(b.first + 52, (a.second + b.second) / 2, std::format("V {:.2f} mm", ymax - ymin), "#986022");
    }
    if (cursor && inference.kind != sketch_inference::Kind::None && inference.kind != sketch_inference::Kind::Grid) {
        auto m = screen(inference.point);
        if (sketch_inference::is_point_inference(inference.kind))
            c->create_rectangle(m.first - 5, m.second - 5, m.first + 5, m.second + 5, {{"fill", inference.color}, {"outline", "white"}});
        else
            c->create_oval(m.first - 4, m.second - 4, m.first + 4, m.second + 4, {{"fill", inference.color}, {"outline", "white"}});
        label(m.first + 16 + inference.tip.size() * 3, m.second - 16, inference.tip, inference.color);
    }
}

bool Sketch::new_element(bool discard_confirmed) {
    journal::event(app, "szkic", "nowy element w szkicu");
    if (has_changes() && !discard_confirmed &&
        !tk::messagebox::askyesno("Nowy element", "Odrzucić niezapisany szkic i rozpocząć nowy? Użyj „Zapisz i rysuj następny”, aby zachować go.", winfo_toplevel()))
        return false;
    std::string group = target.get();
    std::string material_id = app->project->materials.at(material->current()).id;
    original.reset();
    part = core::Part{};
    part.name = "Własny element";
    part.group = group;
    part.material = material_id;
    part.t = number(fields.at("t"));
    points.clear();
    closed = false;
    pending.clear();
    centers.clear();
    guides.clear();
    measured.reset();
    if (imported_holes) { part.holes.clear(); imported_holes = false; }
    arrow_lock.reset();
    value_text.set("");
    cursor.reset();
    history.clear();
    replace_existing.set(false);
    fields.at("name").set("Własny element");
    fields.at("group").set(group);
    listbox->delete_("0", "end");
    status.set("Nowy element: wybierz funkcję i rysuj. Poprzedni zapis pozostaje w meblu.");
    draw();
    mark_clean();
    return true;
}

void Sketch::set_sketch_points(const std::vector<Point>& new_points) {
    centers.clear();
    journal::event(app, "szkic", std::format("ustawiono {} punktów konturu", new_points.size()));
    if (!part.holes.empty() || py::truthy(part.operations) || py::truthy(part.fillets) || py::truthy(part.chamfers) ||
        py::truthy(part.machining))
        throw ValueError("Ten szkic ma obróbkę. Zastosuj odbicie części z menu CAD, aby przeliczyć także otwory i profile.");
    if (closed) normalized_part(part, new_points);
    checkpoint();
    points = new_points;
    cursor.reset();
    update_list();
    fit();
}

void Sketch::transform_sketch(const std::string& action) {
    journal::event(app, "szkic", "przekształcenie: " + action);
    try {
        if (points.empty()) throw ValueError("Najpierw narysuj kontur.");
        std::vector<Point> result;
        if (native_model::available()) {
            result = points_from_json(native_model::call("sketch", "transform_points", json::array({points_json(points), action})));
        } else {
            double xmin = points[0][0], xmax = xmin, ymin = points[0][1], ymax = ymin;
            for (auto& p : points) {
                xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
                ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
            }
            double cx = (xmin + xmax) / 2, cy = (ymin + ymax) / 2;
            for (auto& [uu, vv] : points)
                result.push_back(action == "mirror_u" ? Point{2 * cx - uu, vv} : action == "mirror_v" ? Point{uu, 2 * cy - vv} : Point{cx - (vv - cy), cy + (uu - cx)});
        }
        set_sketch_points(result);
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Szkic", e.what(), this);
    }
}

void Sketch::offset_sketch() {
    journal::event(app, "szkic", "odsunięcie konturu");
    auto distance = tk::simpledialog::askfloat("Odsunięcie konturu", "Odległość w mm: dodatnia na zewnątrz, ujemna do środka.", {{"parent", this}});
    if (!distance) return;
    try {
        if (!closed) throw ValueError("Najpierw zamknij kontur.");
        set_sketch_points(offset_contour(points, *distance));
    } catch (const Standard_Failure& e) {
        const char* message = e.GetMessageString();
        tk::messagebox::showerror("Odsunięcie", message && *message ? message : e.DynamicType()->Name(), this);
    } catch (const std::exception& e) {
        tk::messagebox::showerror("Odsunięcie", e.what(), this);
    }
}

void Sketch::constrain_edge(const std::string& kind) {
    journal::event(app, "szkic", "więz krawędzi: " + kind);
    try {
        auto selection = listbox->curselection();
        if (selection.empty() || points.size() < 2) throw ValueError("Wybierz punkt początkowy odcinka na liście.");
        std::optional<double> new_length;
        size_t i = (size_t)selection[0];
        if (kind == "length") {
            if (points.size() < 2 || i >= points.size()) throw ValueError("Wybierz punkt początkowy odcinka na liście.");
            Point a = points.at(i), b = points.at((i + 1) % points.size());
            new_length = tk::simpledialog::askfloat("Długość odcinka", "Nowa długość [mm]", {{"initialvalue", dist(a, b)}, {"parent", this}, {"minvalue", .1}});
            if (!new_length) return;
        }
        constrain_at(i, kind, new_length);
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("więz odrzucony: ") + e.what());
        tk::messagebox::showerror("Odcinek", e.what(), this);
    }
}

void Sketch::constrain_at(size_t i, const std::string& kind, std::optional<double> new_length) {
    if (points.size() < 2 || i >= points.size()) throw ValueError("Nie ma takiego odcinka.");
    size_t j = (i + 1) % points.size();
    if (!closed && j == 0) throw ValueError("Ostatni punkt otwartego konturu nie ma kolejnego odcinka.");
    Point a = points.at(i), b = points.at(j);
    std::vector<Point> edited = points;
    if (kind == "horizontal") edited[j] = {b[0], a[1]};
    else if (kind == "vertical") edited[j] = {a[0], b[1]};
    else {
        if (!new_length || *new_length <= 0) throw ValueError("Długość odcinka musi być dodatnia.");
        double old = dist(a, b);
        if (old == 0) throw ValueError("Punkty się pokrywają.");
        edited[j] = {a[0] + (b[0] - a[0]) * *new_length / old, a[1] + (b[1] - a[1]) * *new_length / old};
    }
    if (native_model::available()) {
        json result = native_model::call("sketch", "constrain_points",
                                         json::array({points_json(points), (long long)i, closed, kind, new_length ? json(*new_length) : json(nullptr)}));
        if (result.is_null()) return;
        edited = points_from_json(result);
    }
    journal::event(app, "szkic", std::format("odcinek {}: {}{}", i + 1, kind == "length" ? "długość " + std::format("{:.2f}", dist(a, b)) + " → " + std::format("{:.2f}", *new_length) + " mm" : kind, ""));
    set_sketch_points(edited);
}

void Sketch::apply_edge_length(size_t index, double new_length) { constrain_at(index, "length", new_length); }

void Sketch::select_row(size_t index) {
    listbox->call({listbox->path(), "selection", "clear", "0", "end"});
    listbox->call({listbox->path(), "selection", "set", std::to_string(index)});
    select_point();
}

std::optional<size_t> Sketch::selected_index() const {
    auto selection = listbox->curselection();
    if (selection.empty() || selection[0] < 0) return std::nullopt;
    return (size_t)selection[0];
}

int Sketch::dimension_at(int x, int y) const {
    if (!closed || py::truthy(part.profile) || points.size() > 20 || points.size() < 3) return -1;
    for (size_t i = 0; i < points.size(); ++i) {
        auto a = screen(points[i]);
        auto b = screen(points[(i + 1) % points.size()]);
        if (std::hypot(a.first - b.first, a.second - b.second) < 45) continue;
        double mx = (a.first + b.first) / 2, my = (a.second + b.second) / 2 - 12;
        if (std::abs(x - mx) <= 40 && std::abs(y - my) <= 10) return (int)i;
    }
    return -1;
}

void Sketch::edit_dimension(size_t edge) {
    journal::event(app, "szkic", std::format("kliknięto wymiar odcinka {}", edge + 1));
    try {
        if (edge >= points.size()) throw ValueError("Nie ma takiego odcinka.");
        Point a = points[edge], b = points[(edge + 1) % points.size()];
        auto length = tk::simpledialog::askfloat("Długość odcinka", std::format("Nowa długość odcinka {} [mm]", edge + 1), {{"initialvalue", dist(a, b)}, {"parent", this}, {"minvalue", .1}});
        if (!length) return;
        apply_edge_length(edge, *length);
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("zmiana wymiaru odrzucona: ") + e.what());
        tk::messagebox::showerror("Odcinek", e.what(), this);
    }
}

void Sketch::finish_shape(const std::vector<Point>& shape, const std::string& what, std::optional<Point> center) {
    journal::event(app, "szkic", "kształt: " + what + (points.empty() ? "" : closed ? " (zastępuje zamknięty kontur)" : " (zastępuje rozpoczęty kontur)"));
    try {
        pending.clear();
        arrow_lock.reset();
        set_sketch_points(shape);
        if (!closed) close_contour();
        if (center) centers.push_back(*center);
        status.set("Kształt gotowy: " + what + ". Nadaj grubość i zapisz element do projektu.");
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("kształt odrzucony: ") + e.what());
        tk::messagebox::showerror("Kształt", e.what(), this);
    }
}

std::vector<Point> Sketch::shape_preview(const Point& at) const {
    const std::string tool = mode.get();
    try {
        if ((tool == "Okrąg" || tool == "Wielobok") && !pending.empty()) {
            double radius = dist(pending[0], at);
            if (radius < 0.5) return {};
            if (tool == "Okrąg") return sketch_shapes::circle(pending[0], radius, 48);
            int n = (int)std::lround(std::stod(sides.get()));
            return sketch_shapes::regular_polygon(pending[0], radius, n, degrees(std::atan2(at[1] - pending[0][1], at[0] - pending[0][0])));
        }
        if (tool == "Łuk") {
            std::vector<Point> chain = pending;
            if (chain.empty() && !points.empty() && !closed) chain.push_back(points.back());
            if (chain.size() == 2) return sketch_shapes::arc_three_points(chain[0], chain[1], at);
            if (chain.size() == 1) return {chain[0], at};
        }
    } catch (const std::exception&) {
    }
    return {};
}

bool Sketch::key(const std::string& keysym, int state) {
    if (state & (4 | 8 | 0x20000)) return false;  // Ctrl / Alt: zostaw skróty okna (Ctrl+Z itd.)
    static const std::map<std::string, std::string> typed{{"comma", ","}, {"period", "."}, {"KP_Decimal", "."}, {"minus", "-"}, {"KP_Subtract", "-"}, {"semicolon", ";"},
                                                         {"less", "<"}, {"space", " "}, {"x", "x"}, {"X", "x"}};
    std::string digit;
    if (keysym.size() == 1 && keysym[0] >= '0' && keysym[0] <= '9') digit = keysym;
    else if (keysym.size() == 4 && keysym.rfind("KP_", 0) == 0 && keysym[3] >= '0' && keysym[3] <= '9') digit = keysym.substr(3);
    const bool typing = !value_text.get().empty();
    const std::string tool = mode.get();
    // wartość do pola wartości: cyfry zawsze, litery/znaki tylko, gdy już coś wpisano (skróty narzędzi używają liter)
    if (!digit.empty()) { value_text.set(value_text.get() + digit); return true; }
    if (typing && typed.contains(keysym)) { value_text.set(value_text.get() + typed.at(keysym)); return true; }
    if (!typing && (keysym == "minus" || keysym == "KP_Subtract" || keysym == "less")) { value_text.set(value_text.get() + typed.at(keysym)); return true; }
    if (keysym == "Return" || keysym == "KP_Enter") {
        if (typing) { value_apply(); return true; }
        journal::event(app, "szkic", "Enter: zamknięcie konturu");
        close_contour();
        return true;
    }
    if (keysym == "BackSpace") {
        if (typing) { std::string v = value_text.get(); v.pop_back(); value_text.set(v); return true; }
        undo();
        return true;
    }
    if (keysym == "Escape") { cancel_pending(); return true; }
    if (keysym == "Delete") { remove_selected_point(); return true; }
    if (keysym == "Right" || keysym == "Left" || keysym == "Up" || keysym == "Down") {
        Point dir = keysym == "Right" || keysym == "Left" ? Point{1, 0} : Point{0, 1};
        if (arrow_lock && (*arrow_lock)[0] == dir[0] && (*arrow_lock)[1] == dir[1]) arrow_lock.reset();
        else arrow_lock = dir;
        journal::event(app, "szkic", std::string("blokada kierunku strzałką: ") + (arrow_lock ? (dir[0] == 1 ? "oś U" : "oś V") : "zdjęta"));
        status.set(arrow_lock ? (dir[0] == 1 ? "Kierunek zablokowany na osi U (czerwonej). Strzałka ponownie zdejmuje blokadę." : "Kierunek zablokowany na osi V (zielonej). Strzałka ponownie zdejmuje blokadę.")
                              : "Blokada kierunku zdjęta.");
        draw();
        return true;
    }
    if (!typing) {
        static const std::map<std::string, std::string> tools{{"l", "Wielokąt"}, {"L", "Wielokąt"}, {"r", "Prostokąt"}, {"R", "Prostokąt"}, {"c", "Okrąg"}, {"C", "Okrąg"},
                                                              {"p", "Wielobok"}, {"P", "Wielobok"}, {"a", "Łuk"}, {"A", "Łuk"},
                                                              {"m", "Przesuń"}, {"M", "Przesuń"}, {"t", "Miarka"}, {"T", "Miarka"}};
        auto it = tools.find(keysym);
        if (it != tools.end()) { set_tool(it->second); return true; }
        if (keysym == "f" || keysym == "F") { offset_sketch(); return true; }
        if (keysym == "g" || keysym == "G") {
            if (!measured) {
                journal::event(app, "szkic", "G: brak pomiaru do zamiany w linię pomocniczą");
                status.set("Najpierw zmierz odległość narzędziem Miarka (T), potem naciśnij G.");
                return true;
            }
            try { add_guide(measured->first, measured->second); }
            catch (const ValueError& e) { journal::event(app, "szkic", std::string("linia pomocnicza odrzucona: ") + e.what()); status.set(e.what()); }
            return true;
        }
    }
    return false;
}

bool Sketch::value_apply() {
    std::string text = value_text.get();
    if (text.find_first_not_of(' ') == std::string::npos) return false;
    const std::string tool = mode.get();
    journal::event(app, "szkic", "wartość z klawiatury „" + text + "” (narzędzie " + tool + ")");
    auto fail = [&](const std::string& why) {
        journal::event(app, "szkic", "wartość odrzucona: " + why);
        status.set("Wartość odrzucona: " + why);
        return true;
    };
    try {
        sketch_shapes::ValueInput values = sketch_shapes::parse_value_input(text, tool == "Prostokąt");
        auto cursor_point = [&]() -> std::optional<Point> { return cursor; };
        if (tool == "Wielokąt") {
            if (closed) return fail("kontur jest zamknięty — wyczyść go albo zapisz element");
            if (points.empty()) {
                if (values.numbers.size() != 2) return fail("najpierw kliknij punkt początkowy albo wpisz jego współrzędne „U;V”");
                add_point({values.numbers[0], values.numbers[1]});
            } else {
                if (values.numbers.empty() || values.numbers[0] <= 0) return fail("długość musi być dodatnia");
                Point a = points.back();
                Point dir{1, 0};
                if (values.has_angle) dir = {std::cos(values.angle * std::numbers::pi / 180), std::sin(values.angle * std::numbers::pi / 180)};
                else if (arrow_lock) dir = *arrow_lock;
                else if (cursor_point() && dist(*cursor_point(), a) > 1e-6) dir = {((*cursor_point())[0] - a[0]) / dist(*cursor_point(), a), ((*cursor_point())[1] - a[1]) / dist(*cursor_point(), a)};
                if (arrow_lock && !values.has_angle && cursor_point()) {
                    double along = ((*cursor_point())[0] - a[0]) * (*arrow_lock)[0] + ((*cursor_point())[1] - a[1]) * (*arrow_lock)[1];
                    if (along < 0) dir = {-(*arrow_lock)[0], -(*arrow_lock)[1]};
                }
                add_point({a[0] + dir[0] * values.numbers[0], a[1] + dir[1] * values.numbers[0]});
            }
        } else if (tool == "Prostokąt") {
            if (closed) return fail("kontur jest zamknięty — wyczyść go albo zapisz element");
            if (points.empty()) return fail("najpierw kliknij pierwszy narożnik prostokąta");
            if (values.numbers.empty()) return fail("wpisz szerokość i wysokość, np. 600;400");
            double w = values.numbers[0], h = values.numbers.size() > 1 ? values.numbers[1] : values.numbers[0];
            if (w == 0 || h == 0) return fail("prostokąt musi mieć dodatnią szerokość i wysokość");
            double sx = 1, sy = 1;
            if (cursor_point()) { sx = (*cursor_point())[0] < points[0][0] ? -1 : 1; sy = (*cursor_point())[1] < points[0][1] ? -1 : 1; }
            add_point({points[0][0] + sx * w, points[0][1] + sy * h});
        } else if (tool == "Okrąg" || tool == "Wielobok") {
            if (pending.empty()) return fail("najpierw kliknij środek");
            if (values.numbers.empty() || values.numbers[0] <= 0) return fail("promień musi być dodatni");
            double radius = values.numbers[0];
            Point center = pending[0];
            if (tool == "Okrąg") {
                int n = values.numbers.size() > 1 ? (int)std::lround(values.numbers[1]) : 48;
                finish_shape(sketch_shapes::circle(center, radius, n), std::format("okrąg R={:.2f} ({} segmentów, z klawiatury)", radius, n), center);
            } else {
                int n = values.numbers.size() > 1 ? (int)std::lround(values.numbers[1]) : (int)std::lround(number(sides));
                double start = cursor_point() ? degrees(std::atan2((*cursor_point())[1] - center[1], (*cursor_point())[0] - center[0])) : 90;
                finish_shape(sketch_shapes::regular_polygon(center, radius, n, start), std::format("wielobok {} boków R={:.2f} (z klawiatury)", n, radius), center);
            }
        } else {
            return fail("narzędzie Łuk nie przyjmuje wartości z klawiatury — kliknij trzy punkty");
        }
    } catch (const ValueError& e) {
        return fail(e.what());
    }
    value_text.set("");
    return true;
}

void Sketch::add_guide(const Point& a, const Point& b) {
    double length = dist(a, b);
    if (length < 0.01) throw ValueError("Linia pomocnicza wymaga dwóch różnych punktów.");
    if (guides.size() >= 40) throw ValueError("Maksymalnie 40 linii pomocniczych — usuń je przyciskiem „Wyczyść”.");
    Point d{(b[0] - a[0]) / length, (b[1] - a[1]) / length};
    constexpr double reach = 5000;
    guides.push_back({{a[0] - d[0] * reach, a[1] - d[1] * reach}, {a[0] + d[0] * reach, a[1] + d[1] * reach}});
    journal::event(app, "szkic", std::format("linia pomocnicza przez ({:.1f}; {:.1f}) i ({:.1f}; {:.1f}) · razem {}", a[0], a[1], b[0], b[1], guides.size()));
    status.set(std::format("Dodano linię pomocniczą ({}). Wskazówki łapią ją jak krawędź.", guides.size()));
    draw();
}

void Sketch::clear_guides() {
    journal::event(app, "szkic", std::format("usunięto linie pomocnicze ({})", guides.size()));
    guides.clear();
    measured.reset();
    draw();
}

void Sketch::move_point(size_t index, const Point& to) {
    if (index >= points.size()) throw ValueError("Nie ma takiego punktu.");
    Point from = points[index];
    std::vector<Point> edited = points;
    edited[index] = {core::finite(to[0]), core::finite(to[1])};
    if (closed) {
        core::Part base = part;
        json kept = json::array();
        for (auto& op : base.operations)
            if (!py::truthy(py::get(op, "owner_id"))) kept.push_back(op);
        base.operations = kept;
        normalized_part(base, edited);
    }
    checkpoint();
    points = edited;
    journal::event(app, "szkic", std::format("przesunięto punkt {}: ({:.2f}; {:.2f}) → ({:.2f}; {:.2f})", index + 1, from[0], from[1], edited[index][0], edited[index][1]));
    update_list();
    draw();
}

void Sketch::press(const tk::Event& event) {
    int best = -1;
    double best_distance = 11;
    for (size_t i = 0; i < points.size(); ++i) {
        auto s = screen(points[i]);
        double d = std::hypot(event.x - s.first, event.y - s.second);
        if (d < best_distance) { best_distance = d; best = (int)i; }
    }
    if (best < 0) {
        journal::event(app, "szkic", points.empty() ? "Przesuń: brak punktów do przesunięcia" : "Przesuń: kliknięto poza punktami konturu");
        status.set(points.empty() ? "Najpierw narysuj kontur." : "Kliknij blisko punktu konturu, aby go przesunąć.");
        return;
    }
    drag_index = (size_t)best;
    drag_origin = points[best];
    drag_moved = false;
    status.set(std::format("Przeciągasz punkt {}. Zwolnij przycisk, aby zatwierdzić.", best + 1));
}

void Sketch::drag(const tk::Event& event) {
    if (!drag_index || *drag_index >= points.size()) return;
    try {
        Point p = local(event);
        if (dist(p, points[*drag_index]) < 1e-9) return;
        points[*drag_index] = p;
        drag_moved = true;
        cursor = p;
        status.set(std::format("Punkt {}: U {:.2f} · V {:.2f} mm{}", *drag_index + 1, p[0], p[1], snap_reason.empty() ? "" : " · przyciąganie: " + snap_reason));
        draw();
    } catch (const ValueError& e) {
        status.set(e.what());
    }
}

void Sketch::release(const tk::Event&) {
    if (!drag_index) return;
    size_t index = *drag_index;
    Point from = drag_origin.value_or(points[index]);
    Point to = index < points.size() ? points[index] : from;
    drag_index.reset();
    drag_origin.reset();
    bool moved = drag_moved;
    drag_moved = false;
    if (index >= points.size()) return;
    points[index] = from;  // przywróć, move_point sprawdzi poprawność i zapisze historię
    if (!moved || dist(from, to) < 0.01) { draw(); return; }
    try {
        move_point(index, to);
        status.set(std::format("Przesunięto punkt {} do ({:.2f}; {:.2f}).", index + 1, to[0], to[1]));
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::format("przesunięcie punktu {} odrzucone: {}", index + 1, e.what()));
        status.set(std::string("Przesunięcie odrzucone: ") + e.what());
        update_list();
        draw();
    }
}

// Miniaturka profilu rysowana z tych samych funkcji, które wstawiają kontur (przy błędnych liczbach zostaje pusta ramka).
void Sketch::draw_profile_preview() {
    if (!profile_preview) return;
    profile_preview->delete_("profile_preview");
    try {
        std::vector<Point> shape = sketch_shapes::profile(profile_name.get(), number(profile_width), number(profile_height), number(profile_param));
        std::vector<Point> thumb = sketch_shapes::thumbnail_points(shape, 150, 90, 8);
        if (thumb.size() < 3) return;
        std::vector<double> coords;
        for (const auto& p : thumb) { coords.push_back(p[0]); coords.push_back(p[1]); }
        profile_preview->create_polygon(coords, {{"fill", "#e6ddd0"}, {"outline", "#6b5a45"}, {"width", 1.5}, {"tags", "profile_preview"}});
    } catch (const std::exception&) {
    }
}

void Sketch::insert_profile() {
    try {
        std::string name = profile_name.get();
        double w = number(profile_width), h = number(profile_height), param = number(profile_param);
        journal::event(app, "szkic", std::format("szybki profil „{}” {:g}×{:g} mm, parametr {:g}", name, w, h, param));
        std::vector<Point> shape = sketch_shapes::profile(name, w, h, param);
        if (sketch_shapes::self_intersects(shape)) throw ValueError("Profil przecina sam siebie.");
        pending.clear();
        set_sketch_points(shape);
        if (!closed) close_contour();
        status.set("Wstawiono profil „" + name + "”. Nadaj grubość i zapisz element do projektu.");
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("profil odrzucony: ") + e.what());
        tk::messagebox::showerror("Szybki profil", e.what(), this);
    }
}

void Sketch::fillet_point(size_t index, double value, bool chamfer) {
    journal::event(app, "szkic", std::format("{} narożnika {}: {:g} mm", chamfer ? "fazowanie" : "zaokrąglenie", index + 1, value));
    std::vector<Point> result = chamfer ? sketch_shapes::chamfer_corner(points, index, value, closed) : sketch_shapes::fillet_corner(points, index, value, closed);
    set_sketch_points(result);
}

void Sketch::fillet_selected(bool chamfer) {
    try {
        auto index = selected_index();
        if (!index) throw ValueError("Wybierz narożnik (punkt) na liście.");
        auto value = tk::simpledialog::askfloat(chamfer ? "Fazowanie narożnika" : "Zaokrąglenie narożnika", chamfer ? "Odległość fazy od wierzchołka [mm]" : "Promień zaokrąglenia [mm]",
                                                {{"initialvalue", 20.0}, {"parent", this}, {"minvalue", .1}});
        if (!value) return;
        fillet_point(*index, *value, chamfer);
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string(chamfer ? "fazowanie" : "zaokrąglenie") + " odrzucone: " + e.what());
        tk::messagebox::showerror("Narożnik", e.what(), this);
    }
}

void Sketch::remove_point_at(size_t index) {
    journal::event(app, "szkic", std::format("usunięto punkt {}", index + 1));
    std::vector<Point> result = sketch_shapes::remove_point(points, index, closed);
    if (closed) set_sketch_points(result);
    else {
        checkpoint();
        points = result;
        cursor.reset();
        update_list();
        draw();
    }
}

void Sketch::remove_selected_point() {
    try {
        auto index = selected_index();
        if (!index) throw ValueError("Wybierz punkt na liście, aby go usunąć.");
        remove_point_at(*index);
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("usunięcie punktu odrzucone: ") + e.what());
        status.set(e.what());
    }
}

bool Sketch::import_dxf(const std::string& path_in, bool confirm_replace) {
    std::string path = path_in;
    auto started = std::chrono::steady_clock::now();
    if (path.empty()) path = tk::filedialog::askopenfilename({{"parent", this}, {"filetypes", tk::FileTypes{{"Rysunek DXF", "*.dxf"}}}});
    if (path.empty()) return false;
    auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
    try {
        std::error_code size_error;
        auto file_bytes = std::filesystem::file_size(py::to_path(path), size_error);
        if (size_error) throw ValueError("Nie można otworzyć pliku: " + path);
        if (file_bytes > 30'000'000) throw ValueError("Plik DXF jest zbyt duży (powyżej 30 MB).");
        std::ifstream in(py::to_path(path), std::ios::binary);
        if (!in) throw ValueError("Nie można otworzyć pliku: " + path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        dxf_import::Result result = dxf_import::parse(buffer.str());
        dxf_import::Outline outline = dxf_import::pick_outline(result);
        if (confirm_replace && !points.empty() &&
            !tk::messagebox::askyesno("Import DXF", "Zastąpić bieżący kontur konturem z pliku DXF?", this)) {
            journal::file_event(app, "import DXF anulowany", path, elapsed(), "bez zmian");
            return false;
        }
        std::string summary = std::format("jednostki {} (×{:g}), kontur {} punktów{}, {:.1f}×{:.1f} mm, otwory/kontury wewnątrz: {} (okręgi {}), pominięte zamknięte: {}, linie otwarte: {}",
                                          result.unit_name, result.unit_scale, outline.points.size(), outline.simplified ? std::format(" (uproszczony z {})", outline.original_points) : "",
                                          outline.width, outline.height, outline.inner_contours, outline.inner_circles, outline.outer_candidates, outline.open_paths);
        for (const auto& w : result.warnings) summary += " · " + w;
        pending.clear();
        if (imported_holes) { part.holes.clear(); imported_holes = false; }
        set_sketch_points(outline.points);
        if (!closed) close_contour();
        for (const auto& hole : outline.holes) part.holes.push_back({hole.u, hole.v, hole.diameter, part.t});
        imported_holes = !outline.holes.empty();
        summary += std::format(" · otwory przelotowe z okręgów: {}{}", outline.holes.size(), outline.skipped_holes ? std::format(" (pominięto {} niemieszczących się)", outline.skipped_holes) : "");
        journal::event(app, "szkic", "import DXF: " + summary);
        journal::file_event(app, "import DXF", path, elapsed(), "wczytano obrys " + std::format("{:.1f}×{:.1f} mm", outline.width, outline.height));
        std::string note = std::format("Zaimportowano obrys {:.1f}×{:.1f} mm z DXF", outline.width, outline.height);
        if (!outline.holes.empty()) note += std::format(" · {} otworów przelotowych z okręgów", outline.holes.size());
        if (outline.inner_contours > outline.holes.size()) note += std::format(" · pominięto {} wewnętrznych konturów (inne niż okręgi lub niemieszczące się)", outline.inner_contours - outline.holes.size());
        if (outline.simplified) note += std::format(" · uproszczono z {} do {} punktów", outline.original_points, outline.points.size());
        if (!result.warnings.empty()) note += " · " + result.warnings.front();
        status.set(note + ".");
        return true;
    } catch (const ValueError& e) {
        journal::event(app, "szkic", std::string("import DXF odrzucony: ") + e.what());
        journal::file_event(app, "import DXF", path, elapsed(), std::string("błąd: ") + e.what());
        tk::messagebox::showerror("Import DXF", e.what(), this);
        return false;
    }
}

bool Sketch::save(bool keep_open) {
    if (!ensure_current_project()) return false;
    try {
        auto [temp, id] = candidate();
        temp->validate();
        app->snapshot();
        app->project = temp;
        app->selected = id;
        journal::event(app, "szkic", std::format("zapis szkicu jako element {} (zostaje otwarty: {})", id, keep_open ? "tak" : "nie"));
        app->refresh();
        app->fit();
        app->status.set("Zapisano szkic jako element 3D i przeliczono dane produkcyjne");
        if (keep_open) {
            history.clear();
            new_element(true);
        } else destroy();
        return true;
    } catch (const std::invalid_argument& e) {
        tk::messagebox::showerror("Zapis szkicu", e.what(), this);
        return false;
    } catch (const std::out_of_range& e) {
        tk::messagebox::showerror("Zapis szkicu", e.what(), this);
        return false;
    }
}

}  // namespace sketch
