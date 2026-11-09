#include "modeler_ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

#include "app.h"
#include "assembly.h"
#include "features.h"
#include "gpu_viewport.h"
#include "graphics_mode_ui.h"
#include "journal.h"
#include "native_viewport.h"
#include "sketch_design.h"
#include "view_style.h"
#include "viewport_render.h"
#include "viewport_scene.h"

namespace modeler_ui {

namespace mg = modeler_geometry;
using Cmd = std::function<void()>;
using Vec3 = mg::Vec3;

namespace {

Modeler* g_open = nullptr;

std::string g(double x) { return std::format("{:g}", std::round(x * 100) / 100); }

double parse_field(const std::string& text, const std::string& what) {
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

std::vector<double> flat(const std::vector<mg::Point>& points) {
    std::vector<double> out;
    for (const auto& p : points) { out.push_back(p[0]); out.push_back(p[1]); }
    return out;
}

struct ToolInfo {
    const char* id;
    const char* label;
    const char* key;
    const char* hint;
};
const std::vector<ToolInfo>& tools() {
    static const std::vector<ToolInfo> list = {
        {"select", "Wybierz", "S", "Kliknij płytę, aby ją zaznaczyć (Shift dodaje do zaznaczenia)."},
        {"rect", "Prostokąt", "R", "Kliknij pierwszy narożnik, drugi narożnik, potem wyciągnij (lub wpisz wymiar i Enter). Rysujesz na podłodze albo na ścianie płyty."},
        {"polygon", "Kontur", "G", "Klikaj kolejne wierzchołki; Enter zamyka kontur. Potem wyciągnij."},
        {"pencil", "Ołówek", "N", "Przeciągnij myszą na ścianie albo podłodze, jak ołówkiem: zamknięty obrys (też okrąg, prostokąt i kształt z łukami) zostanie wyprostowany do odcinków i łuków. Potem wyciągnij."},
        {"circle", "Okrąg", "K", "Kliknij środek i punkt na okręgu (albo wpisz promień i Enter). Powstaje okrągła płyta do wyciągnięcia — np. blat, półka, fronty okrągłe."},
        {"pull", "Wyciągnij", "P", "Kliknij ścianę płyty i przeciągnij — zmienia grubość, długość lub szerokość. Wpisz liczbę i Enter."},
        {"move", "Przesuń / kopiuj", "M", "Kliknij punkt bazowy, potem miejsce docelowe. Strzałki blokują oś; ‘*N’ po liczbie robi N kopii."},
        {"mill", "Frez", "F", "Kliknij dużą ścianę płyty: rowek (dwa punkty), kieszeń (dwa narożniki) lub kieszeń okrągła (środek)."},
        {"erase", "Usuń", "E", "Kliknij płytę, aby ją usunąć (Delete usuwa zaznaczone)."},
        {"orbit", "Obrót", "O", "Przeciągnij, aby obracać widok (środkowy przycisk myszy też obraca, prawy przesuwa)."},
    };
    return list;
}

const ToolInfo& tool_info(const std::string& id) {
    for (const auto& t : tools())
        if (id == t.id) return t;
    throw std::invalid_argument("Nieznane narzędzie pracowni: " + id);
}

}  // namespace

// ------------------------------------------------------------------ okno

Modeler::Modeler(App* app_) : dock_workspace::DockDocument(app_), camera(-38, 24, .35, {300, 280, 300}) {
    title("Pracownia 3D");
    geometry(std::format("{}x{}+20+20", std::min(1360, winfo_screenwidth() - 50), std::min(860, winfo_screenheight() - 100)));
    minsize(900, 560);
    tool_var = tk::StringVar("select");
    typed = tk::StringVar("");
    status = tk::StringVar(tool_info("select").hint);
    thickness = tk::StringVar("18");
    group_name = tk::StringVar("Mebel");
    material_name = tk::StringVar(app->project->materials.empty() ? "" : app->project->materials[0].name);
    work_plane = tk::StringVar("Podłoga");
    copies = tk::StringVar("1");
    copy_mode = tk::BooleanVar(false);
    selection_text = tk::StringVar("Nic nie zaznaczono.");
    cutter_var = tk::StringVar(cutters::catalog()[0].name);
    cutter_diameter = tk::StringVar("8");
    cutter_angle = tk::StringVar("90");
    cutter_depth = tk::StringVar("5");
    cutter_kind = tk::StringVar("Rowek");
    cutter_name = tk::StringVar("");
    pocket_size = tk::StringVar("");
    edge_cutter_var = tk::StringVar(cutters::catalog()[3].name);
    edge_size = tk::StringVar("3");
    edge_scope = tk::StringVar("Wszystkie krawędzie");
    pattern_var = tk::StringVar(sketch_design::pattern_catalog()[0].name);

    mode_var = tk::StringVar("3d");
    tool2_var = tk::StringVar("line");
    plane2_var = tk::StringVar("Podłoga");
    level2_var = tk::StringVar("0");
    grid_var = tk::StringVar("0");
    radius_var = tk::StringVar("20");
    sides_var = tk::StringVar("6");
    slot_width_var = tk::StringVar("20");
    shapes_text = tk::StringVar("Szkic jest pusty.");
    snap_grid_var = tk::BooleanVar(true);
    ortho_var = tk::BooleanVar(true);

    auto* toolbar = new ttk::Frame(this, {{"padding", 6}});
    toolbar->pack({{"fill", "x"}});
    for (auto& [label, value] : std::vector<std::pair<std::string, std::string>>{{"3D", "3d"}, {"2D CAD", "2d"}}) {
        std::string id = value;
        (new ttk::Radiobutton(toolbar, {{"text", label}, {"variable", mode_var}, {"value", id}, {"style", "Toolbutton"},
                                        {"command", Cmd([this] { set_mode(mode_var.get()); })}}))->pack({{"side", "left"}, {"padx", 2}});
    }
    (new ttk::Separator(toolbar, {{"orient", "vertical"}}))->pack({{"side", "left"}, {"fill", "y"}, {"padx", 6}});
    auto* tool_area = new ttk::Frame(toolbar);
    tool_area->pack({{"side", "left"}});
    tools3d_frame = new ttk::Frame(tool_area);
    tools3d_frame->pack({{"side", "left"}});
    tools2d_frame = new ttk::Frame(tool_area);
    for (const auto& t : tools()) {
        std::string id = t.id;
        (new ttk::Radiobutton(tools3d_frame, {{"text", std::string(t.label) + " (" + t.key + ")"}, {"variable", tool_var}, {"value", id}, {"style", "Toolbutton"},
                                              {"command", Cmd([this] { set_tool(tool_var.get()); })}}))->pack({{"side", "left"}, {"padx", 2}});
    }
    (new ttk::Separator(tools3d_frame, {{"orient", "vertical"}}))->pack({{"side", "left"}, {"fill", "y"}, {"padx", 6}});
    for (auto& [label, angles] : std::vector<std::pair<std::string, std::pair<double, double>>>{{"3D", {-38, 24}}, {"Przód", {0, 0}}, {"Bok", {-90, 0}}, {"Góra", {0, 90}}}) {
        auto a = angles;
        (new ttk::Button(tools3d_frame, {{"text", label}, {"command", Cmd([this, a] { view(a.first, a.second); })}}))->pack({{"side", "left"}, {"padx", 2}});
    }
    (new ttk::Button(tools3d_frame, {{"text", "Dopasuj"}, {"command", Cmd([this] { fit(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    // Styl wyświetlania jednym kliknięciem: rysujesz w szkielecie (X-ray), a potem przełączasz na ołówek albo realistyczny.
    (new ttk::Separator(tools3d_frame, {{"orient", "vertical"}}))->pack({{"side", "left"}, {"fill", "y"}, {"padx", 6}});
    for (auto& [label, style] : std::vector<std::pair<std::string, render_style::Mode>>{{"Szkielet", render_style::Mode::Wire}, {"Ołówek", render_style::Mode::Sketch}, {"Realistyczny", render_style::Mode::Realistic}}) {
        auto chosen = style;
        (new ttk::Button(tools3d_frame, {{"text", label}, {"command", Cmd([this, chosen] { graphics_mode_ui::set_mode(app, chosen, "pracownia 3D · pasek narzędzi"); })}}))->pack({{"side", "left"}, {"padx", 2}});
    }
    for (const auto& t : tools2d()) {
        std::string id = t.id;
        (new ttk::Radiobutton(tools2d_frame, {{"text", std::string(t.label) + " (" + t.key + ")"}, {"variable", tool2_var}, {"value", id}, {"style", "Toolbutton"},
                                              {"command", Cmd([this] { set_tool2(tool2_var.get()); })}}))->pack({{"side", "left"}, {"padx", 2}});
    }
    (new ttk::Button(tools2d_frame, {{"text", "Dopasuj"}, {"command", Cmd([this] { fit(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    (new ttk::Separator(toolbar, {{"orient", "vertical"}}))->pack({{"side", "left"}, {"fill", "y"}, {"padx", 6}});
    (new ttk::Button(toolbar, {{"text", "Cofnij"}, {"command", Cmd([this] { app->undo(); request_scene(); refresh_panel(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    (new ttk::Button(toolbar, {{"text", "Ponów"}, {"command", Cmd([this] { app->redo(); request_scene(); refresh_panel(); })}}))->pack({{"side", "left"}, {"padx", 2}});

    auto* bottom = new ttk::Frame(this, {{"padding", "8 4"}});
    bottom->pack({{"side", "bottom"}, {"fill", "x"}});
    (new ttk::Label(bottom, {{"text", "Wymiary:"}}))->pack({{"side", "left"}});
    auto* typed_entry = new ttk::Entry(bottom, {{"textvariable", typed}, {"width", 18}});
    typed_entry->pack({{"side", "left"}, {"padx", 6}});
    typed_entry->bind("<Return>", [this](tk::Event&) { apply_typed(typed.get()); return std::string(); });
    (new ttk::Label(bottom, {{"textvariable", status}, {"wraplength", 900}}))->pack({{"side", "left"}, {"padx", 10}, {"fill", "x"}, {"expand", true}});

    auto* scroll = new quick_studio_ui::ScrollPanel(this);
    scroll->pack({{"side", "right"}, {"fill", "y"}});
    build_panel(scroll->body);

    canvas = new quick_studio_ui::StudioCanvas(this, {{"background", "#eaf0f4"}, {"highlightthickness", 0}, {"takefocus", 1}});
    canvas->pack({{"fill", "both"}, {"expand", true}});
    auto handler = [](auto fn) { return [fn](tk::Event& e) { fn(e); return std::string(); }; };
    canvas->bind("<Configure>", handler([this](tk::Event&) { request_scene(); }));
    canvas->bind("<Motion>", handler([this](tk::Event& e) { on_motion(e.x, e.y); }));
    canvas->bind("<ButtonPress-1>", handler([this](tk::Event& e) {
        canvas->focus_set();
        anchor_px = {e.x, e.y};
        if (mode == "2d") {
            if (tool2 == "pencil") begin_stroke(e.x, e.y); else on_click_2d(e.x, e.y, e.state);
        } else if (tool == "pencil") begin_stroke(e.x, e.y);
        else if (tool != "orbit") on_click(e.x, e.y, e.state);
    }));
    canvas->bind("<B1-Motion>", handler([this](tk::Event& e) {
        if (mode == "3d" && tool == "orbit") orbit(e);
        else on_motion(e.x, e.y);
    }));
    canvas->bind("<ButtonRelease-1>", handler([this](tk::Event&) {
        if (stroking) end_stroke();
        else if (mode == "3d" && tool == "orbit") finish_view();
    }));
    canvas->bind("<ButtonPress-2>", handler([this](tk::Event& e) { anchor_px = {e.x, e.y}; }));
    canvas->bind("<B2-Motion>", handler([this](tk::Event& e) { if (mode == "2d") pan(e); else orbit(e); }));
    canvas->bind("<ButtonRelease-2>", handler([this](tk::Event&) { finish_view(); }));
    canvas->bind("<ButtonPress-3>", handler([this](tk::Event& e) { anchor_px = {e.x, e.y}; }));
    canvas->bind("<B3-Motion>", handler([this](tk::Event& e) { pan(e); }));
    canvas->bind("<ButtonRelease-3>", handler([this](tk::Event&) { finish_view(); }));
    canvas->bind("<MouseWheel>", handler([this](tk::Event& e) { zoom(e); }));
    canvas->bind("<Key>", handler([this](tk::Event& e) { on_key(e.keysym, e.state); }));
    canvas->bind("<Double-Button-1>", handler([this](tk::Event&) { if (mode == "2d" && d2.stage > 0 && d2.verts.size() >= 2) finish_polyline(false); }));

    graphics_mode_ui::add_listener([this] {
        try {
            if (!winfo_exists()) return false;
            request_scene();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
    note("modelowanie", "otwarto Pracownię 3D (projekt: " + std::to_string(app->project->parts.size()) + " płyt)");
    live_job = after(300, [this] { live_refresh(); });
    refresh_panel();
    request_scene();
}

void Modeler::build_panel(tk::Widget* parent_in) {
    panel3d_frame = new ttk::Frame(parent_in);
    panel3d_frame->pack({{"fill", "x"}});
    tk::Widget* parent = panel3d_frame;
    auto section = [&](const std::string& text) { (new ttk::Label(parent, {{"text", text}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "10 2"}}); };
    auto row = [&](const std::string& label, tk::StringVar& var, int width = 8) {
        auto* frame = new ttk::Frame(parent);
        frame->pack({{"fill", "x"}, {"pady", 1}});
        (new ttk::Label(frame, {{"text", label}}))->pack({{"side", "left"}});
        (new ttk::Entry(frame, {{"textvariable", var}, {"width", width}}))->pack({{"side", "right"}});
    };
    auto combo = [&](const std::string& label, tk::StringVar& var, const std::vector<std::string>& values) {
        auto* frame = new ttk::Frame(parent);
        frame->pack({{"fill", "x"}, {"pady", 1}});
        (new ttk::Label(frame, {{"text", label}}))->pack({{"side", "left"}});
        auto* box = new ttk::Combobox(frame, {{"textvariable", var}, {"values", values}, {"state", "readonly"}, {"width", 22}});
        box->pack({{"side", "right"}});
        return box;
    };

    section("Grafika");
    graphics_mode_ui::add_compact(parent, app, "pracownia 3D");

    section("Nowe płyty");
    row("Grubość / wyciągnięcie [mm]", thickness);
    row("Mebel (grupa)", group_name, 14);
    std::vector<std::string> materials;
    for (const auto& m : app->project->materials) materials.push_back(m.name);
    material_box = combo("Materiał", material_name, materials);
    combo("Płaszczyzna na pustym miejscu", work_plane, {"Podłoga", "Przód", "Bok"});

    section("Zaznaczenie");
    (new ttk::Label(parent, {{"textvariable", selection_text}, {"wraplength", 280}}))->pack({{"anchor", "w"}});
    auto* selection_row = new ttk::Frame(parent);
    selection_row->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Button(selection_row, {{"text", "Cały mebel"}, {"command", Cmd([this] {
        const core::Part* part = selected_part();
        if (!part) { reject("zaznaczenie całego mebla", "najpierw zaznacz jedną płytę tego mebla"); return; }
        selection.clear();
        for (const auto& p : app->project->parts) if (p.group == part->group) selection.insert(p.id);
        note("modelowanie", std::format("zaznaczono cały mebel „{}” ({} płyt)", part->group, selection.size()));
        refresh_panel(); request_scene();
    })}}))->pack({{"side", "left"}, {"padx", 2}});
    (new ttk::Button(selection_row, {{"text", "Usuń zaznaczone"}, {"command", Cmd([this] { erase_selected(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    row("Liczba kopii (Przesuń)", copies, 5);
    (new ttk::Checkbutton(parent, {{"text", "Przesuwaj jako kopię (C)"}, {"variable", copy_mode}}))->pack({{"anchor", "w"}});

    section("Frezy — nowa operacja");
    std::vector<std::string> cutter_names;
    for (const auto& c : cutters::catalog()) if (!c.for_edges) cutter_names.push_back(c.name);
    auto* cutter_box = combo("Frez", cutter_var, cutter_names);
    cutter_box->bind("<<ComboboxSelected>>", [this](tk::Event&) {
        const auto& info = cutters::info(cutter_var.get());
        cutter_diameter.set(g(info.default_diameter));
        cutter_angle.set(g(info.default_angle));
        draw_cutter_preview();
        return std::string();
    });
    cutter_preview = new tk::Canvas(parent, {{"width", 120}, {"height", 78}, {"bg", "#f7f4ee"}, {"highlightthickness", 0}});
    cutter_preview->pack({{"anchor", "w"}, {"pady", 3}});
    row("Średnica freza [mm]", cutter_diameter);
    row("Kąt V [°]", cutter_angle);
    row("Głębokość [mm]", cutter_depth);
    combo("Rodzaj operacji", cutter_kind, {"Rowek", "Kieszeń prostokątna", "Kieszeń okrągła"});
    row("Średnica kieszeni okrągłej", pocket_size);
    row("Nazwa operacji", cutter_name, 14);
    (new ttk::Label(parent, {{"text", "Wybierz narzędzie Frez (F) i klikaj na dużej ścianie zaznaczonej płyty. Operacji może być dowolnie wiele.", }, {"wraplength", 280}}))->pack({{"anchor", "w"}, {"pady", 2}});

    section("Operacje zaznaczonej płyty");
    ops_list = new tk::Listbox(parent, {{"height", 5}, {"exportselection", 0}});
    ops_list->pack({{"fill", "x"}});
    auto* ops_row = new ttk::Frame(parent);
    ops_row->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Button(ops_row, {{"text", "Usuń wybraną operację"}, {"command", Cmd([this] {
        const core::Part* part = selected_part();
        auto sel = ops_list->curselection();
        if (!part) { reject("usunięcie operacji", "nie zaznaczono płyty"); return; }
        auto ops = cutters::list_ops(part->machining);
        if (sel.empty() || (size_t)sel[0] >= ops.size()) { reject("usunięcie operacji", "wybierz operację na liście"); return; }
        size_t index = 0;
        for (size_t i = 0; i < app->project->parts.size(); ++i) if (app->project->parts[i].id == part->id) index = i;
        remove_mill_op(index, ops[(size_t)sel[0]].id);
    })}}))->pack({{"side", "left"}});

    section("Krawędzie (frez krawędziowy)");
    std::vector<std::string> edge_names;
    for (const auto& c : cutters::catalog()) if (c.for_edges) edge_names.push_back(c.name);
    combo("Frez", edge_cutter_var, edge_names)->bind("<<ComboboxSelected>>", [this](tk::Event&) { draw_edge_preview(); return std::string(); });
    row("Rozmiar [mm]", edge_size);
    combo("Zakres", edge_scope, {"Wszystkie krawędzie", "Tylko góra", "Tylko spód"});
    edge_preview = new tk::Canvas(parent, {{"width", 120}, {"height", 70}, {"bg", "#f7f4ee"}, {"highlightthickness", 0}});
    edge_preview->pack({{"anchor", "w"}, {"pady", 3}});
    (new ttk::Button(parent, {{"text", "Zastosuj na zaznaczonej płycie"}, {"command", Cmd([this] { apply_edge_cutter_ui(); })}}))->pack({{"anchor", "w"}});

    section("Wzorki frezowane");
    std::vector<std::string> pattern_names;
    for (const auto& p : sketch_design::pattern_catalog()) pattern_names.push_back(p.name);
    combo("Wzorek", pattern_var, pattern_names)->bind("<<ComboboxSelected>>", [this](tk::Event&) { draw_pattern_preview(); return std::string(); });
    pattern_preview = new tk::Canvas(parent, {{"width", 120}, {"height", 78}, {"bg", "#f7f4ee"}, {"highlightthickness", 0}});
    pattern_preview->pack({{"anchor", "w"}, {"pady", 3}});
    auto* pattern_row = new ttk::Frame(parent);
    pattern_row->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Button(pattern_row, {{"text", "Dodaj wzorek"}, {"command", Cmd([this] { add_pattern_ui(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    (new ttk::Button(pattern_row, {{"text", "Usuń wzorki"}, {"command", Cmd([this] { clear_patterns_ui(); })}}))->pack({{"side", "left"}, {"padx", 2}});
    draw_cutter_preview();
    draw_edge_preview();
    draw_pattern_preview();
    build_panel_2d(parent_in);
}

void Modeler::destroy() {
    for (auto* job : {&scene_job, &live_job}) {
        if (*job) {
            try { after_cancel(**job); } catch (const tk::TclError&) {}
            job->reset();
        }
    }
    note("modelowanie", "zamknięto Pracownię 3D");
    if (g_open == this) g_open = nullptr;
    dock_workspace::DockDocument::destroy();
}

core::ProjectPtr Modeler::project() const { return app->project; }

const core::Part* Modeler::selected_part() const {
    for (const auto& id : selection)
        for (const auto& part : app->project->parts)
            if (part.id == id) return &part;
    return nullptr;
}

void Modeler::note(const std::string& category, const std::string& text) { journal::event(app, category, text); }

void Modeler::reject(const std::string& what, const std::string& reason) {
    note("modelowanie", "odrzucono: " + what + " — " + reason);
    status.set("Nie zrobiono: " + what + " — " + reason);
}

mg::Plane Modeler::default_plane() const {
    const std::string name = work_plane.get();
    if (name == "Przód") return mg::front_plane(0);
    if (name == "Bok") return mg::side_plane(0);
    return mg::ground_plane(0);
}

double Modeler::panel_thickness() const {
    double t = parse_field(thickness.get(), "Grubość");
    if (t < .5 || t > 6000) throw std::invalid_argument(std::format("Grubość {} mm jest poza zakresem 0,5 … 6000 mm.", g(t)));
    return t;
}

cutters::Cutter Modeler::panel_cutter() const {
    cutters::Cutter cutter;
    cutter.type = cutters::info(cutter_var.get()).id;
    cutter.diameter = parse_field(cutter_diameter.get(), "Średnica freza");
    cutter.angle = cutters::info(cutter.type).has_angle ? parse_field(cutter_angle.get(), "Kąt freza") : 90;
    cutters::validate(cutter);
    return cutter;
}

// ------------------------------------------------------------------ widok

void Modeler::fit() {
    need_fit = true;
    request_scene();
    note("modelowanie", "dopasowanie widoku do mebla");
}

void Modeler::view(double yaw, double pitch) {
    camera.yaw = yaw;
    camera.pitch = pitch;
    note("modelowanie", std::format("widok pracowni: yaw {:.0f} pitch {:.0f}", yaw, pitch));
    need_fit = true;
    request_scene();
}

void Modeler::request_scene() {
    if (scene_job) return;
    scene_job = after(15, [this] {
        scene_job.reset();
        if (winfo_exists()) draw_scene();
    });
}

void Modeler::orbit(tk::Event& e) {
    interactive = true;
    camera.yaw += (e.x - anchor_px.first) * .5;
    camera.pitch = std::max(-89.0, std::min(89.0, camera.pitch + (e.y - anchor_px.second) * .4));
    anchor_px = {e.x, e.y};
    draw_scene();
}

void Modeler::pan(tk::Event& e) {
    interactive = true;
    camera.pan = {camera.pan[0] + e.x - anchor_px.first, camera.pan[1] + e.y - anchor_px.second};
    anchor_px = {e.x, e.y};
    draw_scene();
}

void Modeler::zoom(tk::Event& e) {
    if (mode == "2d" && canvas != nullptr) {
        // powiększenie wokół kursora: punkt rysunku pod myszą zostaje pod myszą
        int w = canvas->winfo_width(), h = canvas->winfo_height();
        mg::Ray ray = mg::unproject(camera, e.x, e.y, w, h);
        auto t = mg::ray_plane(ray, plane2d);
        camera.scale = std::min(40.0, std::max(.01, camera.scale * (e.delta > 0 ? 1.15 : 1 / 1.15)));
        if (t) {
            Vec3 s = camera.project(mg::at(ray, *t), w, h);
            camera.pan = {camera.pan[0] + e.x - s[0], camera.pan[1] + e.y - s[1]};
        }
        interactive = false;
        request_scene();
        return;
    }
    camera.scale = std::min(40.0, std::max(.01, camera.scale * (e.delta > 0 ? 1.12 : 1 / 1.12)));
    interactive = false;
    request_scene();
}

void Modeler::finish_view() {
    interactive = false;
    request_scene();
}

void Modeler::live_refresh() {
    if (!winfo_exists()) return;
    if (winfo_ismapped()) {
        std::string key = std::format("{}|{}", (const void*)app->project.get(), app->viewport_revision);
        if (key != live_key) {
            live_key = key;
            std::set<std::string> alive;
            for (const auto& p : app->project->parts) alive.insert(p.id);
            for (auto it = selection.begin(); it != selection.end();) it = alive.contains(*it) ? std::next(it) : selection.erase(it);
            refresh_panel();
            draw_scene();
        }
    }
    live_job = after(300, [this] { live_refresh(); });
}

void Modeler::draw_scene() {
    if (canvas == nullptr || !winfo_exists()) return;
    if (mode == "2d") { draw_scene_2d(); return; }
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10 || h < 10) return;
    canvas->delete_("all");
    core::ProjectPtr proj = project();
    if (need_fit) {
        if (!proj->parts.empty()) camera.fit(*proj, w, h);
        else { camera.center = {600, 400, 300}; camera.scale = std::min(w, h) / 2400.0; camera.pan = {0, 0}; }
        need_fit = false;
    }
    viewport_scene::paint_background(canvas, camera, proj, {w, h}, interactive);
    if (!proj->parts.empty()) {
        std::optional<std::string> first;
        if (!selection.empty()) first = *selection.begin();
        // Ten sam wybór silnika co w oknie wizualizacji: w trybie realistycznym biblioteka natywna jest zastępowana przez GPU/CPU.
        const std::string engine = view_style::effective_engine(app->viewport_engine);
        std::optional<native_viewport::ViewportResult> result;
        if (engine == "auto" || engine == "d3d11" || engine == "d3d12" || engine == "vulkan") {
            result = native_viewport::paint_native(canvas, {.project = proj, .backend = engine, .camera = camera, .size = {w, h}, .interactive = interactive,
                .selected_ids = selection, .selected = first, .explode = 0, .xray = false, .xray_ids = {}, .quality = app->viewport_quality});
            if (result->ok) gpu_viewport::close_gpu(canvas);
        } else {
            native_viewport::close_native(canvas);
        }
        if ((engine == "auto" || engine == "gpu") && !(result && result->ok)) {
            try {
                result = gpu_viewport::paint_gpu(canvas, {.project = proj, .camera = camera, .size = {w, h}, .interactive = interactive,
                    .selected_ids = selection, .selected = first, .explode = 0, .xray = false, .xray_ids = {}, .quality = app->viewport_quality});
            } catch (const std::runtime_error& error) {
                result = native_viewport::ViewportResult{};
                result->error = error.what();
            } catch (const py::ValueError& error) {
                result = native_viewport::ViewportResult{};
                result->error = error.what();
            }
        }
        if (result && result->ok) {
            if (result->engine != std::optional<std::string>("native")) canvas->_active_renderer = "gpu";
        } else {
            gpu_viewport::close_gpu(canvas);
            native_viewport::close_native(canvas);
            canvas->_active_renderer = "cpu";
            core::Drawn faces = core::render_faces(*proj, camera, w, h, first, 0, interactive);
            viewport_scene::paint_faces(canvas, faces, proj, {.selected_ids = selection, .camera = camera, .size = {w, h}, .xray = false, .explode = 0,
                                                              .interactive = interactive, .xray_ids = std::nullopt, .stats = viewport_render::last_stats()});
            std::optional<std::string> error = result ? result->error : std::nullopt;
            if (error && !error->empty() && error != last_warning) {
                last_warning = error;
                note("grafika", "Pracownia 3D: " + *error + "; podgląd CPU");
            }
        }
    }
    draw_overlay();
}

namespace {

// Rzut punktu świata na ekran.
std::pair<double, double> screen(const core::Camera& camera, const Vec3& p, double w, double h) {
    Vec3 s = camera.project(p, w, h);
    return {s[0], s[1]};
}

void wire_box(tk::Canvas* canvas, const core::Camera& camera, const mg::Box& box, double w, double h, const std::string& color, int width, const std::string& tag) {
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = {(i & 1) ? box.hi[0] : box.lo[0], (i & 2) ? box.hi[1] : box.lo[1], (i & 4) ? box.hi[2] : box.lo[2]};
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        auto a = screen(camera, c[e[0]], w, h), b = screen(camera, c[e[1]], w, h);
        canvas->create_line({a.first, a.second, b.first, b.second}, {{"fill", color}, {"width", width}, {"tags", tag}});
    }
}

void polygon_outline(tk::Canvas* canvas, const core::Camera& camera, const std::vector<Vec3>& points, bool closed, double w, double h, const std::string& color, int width,
                     const std::string& tag) {
    if (points.size() < 2) return;
    std::vector<double> xy;
    for (const auto& p : points) { auto s = screen(camera, p, w, h); xy.push_back(s.first); xy.push_back(s.second); }
    if (closed) { xy.push_back(xy[0]); xy.push_back(xy[1]); }
    canvas->create_line(xy, {{"fill", color}, {"width", width}, {"tags", tag}});
}

}  // namespace

void Modeler::draw_overlay() {
    if (canvas == nullptr || !winfo_exists()) return;
    if (mode == "2d") { draw_overlay_2d(); return; }
    canvas->delete_("overlay");
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10 || h < 10) return;
    const std::string tag = "overlay";
    core::ProjectPtr proj = project();
    // obrys zaznaczonych płyt
    for (const auto& part : proj->parts) {
        if (!selection.contains(part.id) || !part.visible) continue;
        for (const auto& face : mg::part_faces(part)) polygon_outline(canvas, camera, face.polygon, true, w, h, "#ed8a32", 2, tag);
    }
    auto label = [&](const Vec3& p, const std::string& text, int dx = 12, int dy = -12) {
        auto s = screen(camera, p, w, h);
        canvas->create_text(s.first + dx, s.second + dy, {{"text", text}, {"fill", "#123c49"}, {"anchor", "w"}, {"font", "{Segoe UI} 9 bold"}, {"tags", tag}});
    };
    auto text_corner = [&](const std::string& text) {
        canvas->create_text(12, h - 12, {{"text", text}, {"fill", "#123c49"}, {"anchor", "sw"}, {"font", "{Segoe UI} 10 bold"}, {"tags", tag}});
    };
    // podświetlenie ściany pod kursorem
    if (cursor.valid && cursor.hit && draft.stage == 0) {
        for (const auto& face : mg::part_faces(proj->parts[cursor.hit->part]))
            if (tool == "select" || tool == "erase" || tool == "move" || face.id == cursor.hit->face)
                polygon_outline(canvas, camera, face.polygon, true, w, h, tool == "erase" ? "#c0392b" : "#2f80c8", tool == "select" || tool == "erase" || tool == "move" ? 1 : 3, tag);
    }
    // znacznik przyciągnięcia
    if (cursor.valid && !cursor.label.empty()) {
        auto s = screen(camera, cursor.point, w, h);
        canvas->create_rectangle(s.first - 4, s.second - 4, s.first + 4, s.second + 4, {{"outline", "#0b7a4b"}, {"width", 2}, {"tags", tag}});
        canvas->create_text(s.first + 10, s.second - 10, {{"text", cursor.label}, {"fill", "#0b7a4b"}, {"anchor", "w"}, {"font", "{Segoe UI} 9"}, {"tags", tag}});
    }
    if (tool == "pencil" && stroking && draft.plane && stroke.size() >= 2) {
        std::vector<Vec3> line;
        for (const auto& q : stroke) line.push_back(mg::from_plane(*draft.plane, q[0], q[1]));
        polygon_outline(canvas, camera, line, false, w, h, "#8d8d90", 4, tag);
        polygon_outline(canvas, camera, line, false, w, h, "#3d3d40", 1, tag);
    }
    if ((tool == "rect" || tool == "polygon" || tool == "pencil" || tool == "circle") && draft.plane) {
        const mg::Plane& plane = *draft.plane;
        if (tool == "rect" && draft.stage == 1 && !draft.points.empty() && cursor.valid) {
            mg::Point a = mg::to_plane(plane, draft.points[0]), b = mg::to_plane(plane, cursor.point);
            std::vector<Vec3> rectangle = {draft.points[0], mg::from_plane(plane, b[0], a[1]), cursor.point, mg::from_plane(plane, a[0], b[1])};
            polygon_outline(canvas, camera, rectangle, true, w, h, "#2f80c8", 2, tag);
            label(cursor.point, std::format("{} × {} mm", g(std::fabs(b[0] - a[0])), g(std::fabs(b[1] - a[1]))));
        } else if (tool == "rect" && draft.stage == 2 && draft.points.size() == 2) {
            double depth = std::fabs(draft.depth) < .5 ? panel_thickness() : draft.depth;
            try {
                mg::Box box = mg::box_from_rectangle(plane, draft.points[0], draft.points[1], depth);
                wire_box(canvas, camera, box, w, h, "#2f80c8", 2, tag);
                label(draft.points[1], std::format("wyciągnięcie {} mm", g(depth)));
            } catch (const std::invalid_argument&) {}
        } else if (tool == "circle" && draft.stage == 1 && !draft.points.empty() && cursor.valid) {
            mg::Point a = mg::to_plane(plane, draft.points[0]), b = mg::to_plane(plane, cursor.point);
            const double radius = std::hypot(b[0] - a[0], b[1] - a[1]);
            if (radius >= .5) {
                std::vector<Vec3> ring;
                for (const auto& q : cad2d::tessellate(cad2d::circle({a[0], a[1]}, radius), 0.5)) ring.push_back(mg::from_plane(plane, q[0], q[1]));
                polygon_outline(canvas, camera, ring, true, w, h, "#2f80c8", 2, tag);
                label(cursor.point, std::format("R {} mm (Ø {})", g(radius), g(2 * radius)));
            }
        } else if ((tool == "polygon" || tool == "pencil" || tool == "circle") && !draft.points.empty() && !((tool == "pencil" || tool == "circle") && draft.stage == 1)) {
            std::vector<Vec3> line = draft.points;
            if (draft.stage == 1 && cursor.valid) line.push_back(cursor.point);
            polygon_outline(canvas, camera, line, draft.stage == 2, w, h, "#2f80c8", 2, tag);
            if (draft.stage == 2) {
                double depth = std::fabs(draft.depth) < .5 ? panel_thickness() : draft.depth;
                std::vector<Vec3> lifted;
                for (const auto& p : draft.points) lifted.push_back(mg::add(p, mg::scale(plane.n, depth)));
                polygon_outline(canvas, camera, lifted, true, w, h, "#2f80c8", 1, tag);
                label(draft.points[0], std::format("wyciągnięcie {} mm", g(depth)));
            } else if (cursor.valid) {
                label(cursor.point, std::format("{} pkt", draft.points.size()));
            }
        }
    }
    if (tool == "pull" && draft.stage == 1 && draft.hit && draft.part && *draft.part < proj->parts.size()) {
        try {
            core::Part pulled = mg::pull_face(draft.original, draft.hit->face, draft.depth);
            wire_box(canvas, camera, mg::part_box(pulled), w, h, "#2f80c8", 2, tag);
            label(draft.hit->point, std::format("{:+} mm", std::round(draft.depth * 10) / 10));
        } catch (const std::invalid_argument& e) {
            label(draft.hit->point, e.what());
        }
    }
    if (tool == "move" && draft.stage == 1) {
        for (const auto& part : proj->parts)
            if (std::find(draft.ids.begin(), draft.ids.end(), part.id) != draft.ids.end()) {
                mg::Box box = mg::part_box(part);
                box.lo = mg::add(box.lo, draft.delta);
                box.hi = mg::add(box.hi, draft.delta);
                wire_box(canvas, camera, box, w, h, copy_mode.get() ? "#0b7a4b" : "#2f80c8", 2, tag);
            }
        polygon_outline(canvas, camera, {draft.base, mg::add(draft.base, draft.delta)}, false, w, h, "#0b7a4b", 1, tag);
        label(mg::add(draft.base, draft.delta), std::format("Δ ({}, {}, {}) mm{}", g(draft.delta[0]), g(draft.delta[1]), g(draft.delta[2]), draft.axis_lock >= 0 ? std::string(" · oś ") + "XYZ"[draft.axis_lock] : std::string()));
    }
    if (tool == "mill" && draft.stage == 1 && draft.plane && !draft.points.empty() && cursor.valid) {
        const mg::Plane& plane = *draft.plane;
        if (cutter_kind.get() == "Rowek") {
            polygon_outline(canvas, camera, {draft.points[0], cursor.point}, false, w, h, "#c7882f", 3, tag);
            label(cursor.point, std::format("{} mm", g(mg::length(mg::sub(cursor.point, draft.points[0])))));
        } else {
            mg::Point a = mg::to_plane(plane, draft.points[0]), b = mg::to_plane(plane, cursor.point);
            polygon_outline(canvas, camera, {draft.points[0], mg::from_plane(plane, b[0], a[1]), cursor.point, mg::from_plane(plane, a[0], b[1])}, true, w, h, "#c7882f", 2, tag);
            label(cursor.point, std::format("{} × {} mm", g(std::fabs(b[0] - a[0])), g(std::fabs(b[1] - a[1]))));
        }
    }
    if (!typed.get().empty()) text_corner("Wpisano: " + typed.get());
}

// ------------------------------------------------------------------ kursor i narzędzia

Modeler::Cursor Modeler::resolve_cursor(double sx, double sy) {
    Cursor c;
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10 || h < 10) return c;
    core::ProjectPtr proj = project();
    mg::Ray ray = mg::unproject(camera, sx, sy, w, h);
    auto on_plane = [&](const mg::Plane& plane) -> bool {
        auto t = mg::ray_plane(ray, plane);
        if (!t) return false;
        Vec3 p = mg::at(ray, *t);
        std::string snap_label;
        if (auto s = mg::snap_to_points(*proj, camera, sx, sy, w, h)) {
            mg::Point ab = mg::to_plane(plane, s->point);
            p = mg::from_plane(plane, ab[0], ab[1]);  // punkt charakterystyczny rzutowany na płaszczyznę rysowania
            snap_label = s->label;
        } else {
            mg::Point ab = mg::to_plane(plane, p);
            p = mg::from_plane(plane, std::round(ab[0]), std::round(ab[1]));
        }
        if (snap_label.empty() && tool == "polygon" && draft.stage == 1 && !draft.points.empty()) {
            mg::Snap axis = mg::snap_to_axes(draft.points.back(), p, 1);
            p = axis.point;
            snap_label = axis.label;
        }
        c.point = p;
        c.label = snap_label;
        c.plane = plane;
        c.valid = true;
        return true;
    };
    if (draft.stage >= 1 && draft.plane && tool != "pull") {
        on_plane(*draft.plane);
        return c;
    }
    c.hit = mg::pick_face(*proj, camera, sx, sy, w, h);
    if (tool == "pull" || tool == "select" || tool == "erase") {
        if (c.hit) { c.point = c.hit->point; c.plane = c.hit->plane; c.valid = true; }
        return c;
    }
    mg::Plane plane = c.hit ? c.hit->plane : default_plane();
    if (!on_plane(plane)) c.valid = false;
    return c;
}

void Modeler::set_tool(const std::string& name) {
    const ToolInfo& info = tool_info(name);
    if (tool != name) note("modelowanie", std::format("narzędzie: {} → {}", tool_info(tool).label, info.label));
    cancel_stroke("zmieniono narzędzie");
    tool = name;
    tool_var.set(name);
    draft = Draft();
    typed.set("");
    status.set(info.hint);
    cursor = Cursor();
    draw_overlay();
}

void Modeler::cancel() {
    if (mode == "2d") { on_key_2d("Escape"); return; }
    if (draft.stage != 0 || !typed.get().empty()) note("modelowanie", std::format("anulowano rysowanie ({}, etap {})", tool_info(tool).label, draft.stage));
    cancel_stroke("Esc");
    draft = Draft();
    typed.set("");
    status.set(tool_info(tool).hint);
    draw_overlay();
}

bool Modeler::circle_outline(double radius) {
    try {
        if (!draft.plane || draft.points.empty()) throw std::invalid_argument("najpierw kliknij środek okręgu");
        if (!(radius >= .5)) throw std::invalid_argument(std::format("promień {} mm jest mniejszy niż 0,5 mm — przesuń kursor dalej od środka", g(radius)));
        if (radius > 20000) throw std::invalid_argument("promień większy niż 20 m");
        mg::Point center = mg::to_plane(*draft.plane, draft.points[0]);
        std::vector<Vec3> ring;
        for (const auto& q : cad2d::tessellate(cad2d::circle({center[0], center[1]}, radius), 0.3)) ring.push_back(mg::from_plane(*draft.plane, q[0], q[1]));
        draft.points = ring;
        draft.stage = 2;
        draft.anchor = mouse;
        draft.depth = 0;
        status.set(std::format("Okrąg R {} mm. Wyciągnij kursorem (klik zatwierdza) albo wpisz grubość i Enter.", g(radius)));
        note("modelowanie", std::format("okrąg R {} mm (Ø {}) → kontur z {} punktów", g(radius), g(2 * radius), ring.size()));
        return true;
    } catch (const std::exception& e) {
        reject("okrąg", e.what());
        draft = Draft();
        return false;
    }
}

void Modeler::on_motion(double sx, double sy) {
    if (mode == "2d") { on_motion_2d(sx, sy); return; }
    mouse = {sx, sy};
    if (canvas == nullptr) return;
    if (stroking) { add_stroke_point(sx, sy); return; }
    cursor = resolve_cursor(sx, sy);
    if ((tool == "rect" || tool == "polygon" || tool == "pencil" || tool == "circle") && draft.stage == 2 && draft.plane && draft.points.size() >= 2) {
        const mg::Plane& plane = *draft.plane;
        double raw = mg::pull_distance(camera, draft.points.back(), plane.n, sx - draft.anchor.first, sy - draft.anchor.second);
        int axis = 0;
        for (int k = 0; k < 3; ++k) if (std::fabs(plane.n[k]) > .5) axis = k;
        double tolerance = 6 / std::max(.05, std::fabs(camera.scale));
        std::string snap_label;
        double coordinate = mg::snap_coordinate(*project(), axis, draft.points.back()[axis] + raw * plane.n[axis], tolerance, std::nullopt, &snap_label);
        draft.depth = (coordinate - draft.points.back()[axis]) * plane.n[axis];
        cursor.label = snap_label;
        cursor.point = mg::add(draft.points.back(), mg::scale(plane.n, draft.depth));
        cursor.valid = true;
    } else if (tool == "pull" && draft.stage == 1 && draft.hit && draft.part) {
        double raw = mg::pull_distance(camera, draft.hit->point, draft.hit->plane.n, sx - draft.anchor.first, sy - draft.anchor.second);
        std::string snap_label;
        draft.depth = mg::snap_pull(*project(), *draft.part, draft.hit->face, raw, 6 / std::max(.05, std::fabs(camera.scale)), &snap_label);
        cursor.label = snap_label;
    } else if (tool == "move" && draft.stage == 1 && cursor.valid) {
        Vec3 delta = mg::sub(cursor.point, draft.base);
        if (draft.axis_lock >= 0) {
            Vec3 only{0, 0, 0};
            only[(size_t)draft.axis_lock] = delta[(size_t)draft.axis_lock];
            delta = only;
        } else {
            mg::Snap axis = mg::snap_to_axes({0, 0, 0}, delta, 1);
            delta = axis.point;
            if (cursor.label.empty()) cursor.label = axis.label;
        }
        draft.delta = delta;
    }
    draw_overlay();
}

void Modeler::select_at(double sx, double sy, bool add) {
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    auto hit = mg::pick_face(*project(), camera, sx, sy, w, h);
    if (!hit) {
        if (!add && !selection.empty()) note("modelowanie", "odznaczono wszystko");
        if (!add) selection.clear();
    } else {
        const core::Part& part = project()->parts[hit->part];
        if (add && selection.contains(part.id)) selection.erase(part.id);
        else {
            if (!add) selection.clear();
            selection.insert(part.id);
        }
        note("modelowanie", std::format("zaznaczono „{}” ({}), zaznaczonych: {}", part.name, mg::describe(part), selection.size()));
    }
    refresh_panel();
    request_scene();
}

void Modeler::on_click(double sx, double sy, int state) {
    if (mode == "2d") { on_click_2d(sx, sy, state); return; }
    mouse = {sx, sy};
    const bool shift = (state & 1) != 0, ctrl = (state & 4) != 0;
    try {
        cursor = resolve_cursor(sx, sy);
        core::ProjectPtr proj = project();
        if (tool == "select") { select_at(sx, sy, shift); return; }
        if (tool == "erase") {
            if (!cursor.hit) { reject("usunięcie płyty", "kliknij płytę"); return; }
            selection = {proj->parts[cursor.hit->part].id};
            erase_selected();
            return;
        }
        if (tool == "circle") {
            if (draft.stage == 2) { finish_draft_with_depth(std::fabs(draft.depth) < .5 ? panel_thickness() : draft.depth); draw_overlay(); return; }
            if (!cursor.valid || !cursor.plane) { reject("okrąg", "kursor nie trafia w płaszczyznę rysowania (patrzysz wzdłuż niej) — obróć widok"); return; }
            if (draft.stage == 0) {
                draft.plane = *cursor.plane;
                draft.points = {cursor.point};
                draft.stage = 1;
                status.set("Kliknij punkt na okręgu albo wpisz promień i Enter.");
                note("modelowanie", std::format("początek okręgu (środek) na płaszczyźnie „{}”", draft.plane->label));
            } else {
                mg::Point a = mg::to_plane(*draft.plane, draft.points[0]), b = mg::to_plane(*draft.plane, cursor.point);
                circle_outline(std::hypot(b[0] - a[0], b[1] - a[1]));
            }
            draw_overlay();
            return;
        }
        if (tool == "rect" || tool == "polygon") {
            if (!cursor.valid) { reject("rysowanie", "kursor nie trafia w płaszczyznę rysowania (patrzysz wzdłuż niej) — obróć widok"); return; }
            if (draft.stage == 0) {
                draft.plane = *cursor.plane;
                draft.points = {cursor.point};
                draft.stage = 1;
                status.set(tool == "rect" ? "Kliknij drugi narożnik albo wpisz wymiary (np. 600x400) i Enter." : "Klikaj kolejne wierzchołki; Enter zamyka kontur.");
                note("modelowanie", std::format("początek rysowania ({}) na płaszczyźnie „{}”", tool_info(tool).label, draft.plane->label));
            } else if (tool == "rect" && draft.stage == 1) {
                mg::Point a = mg::to_plane(*draft.plane, draft.points[0]), b = mg::to_plane(*draft.plane, cursor.point);
                if (std::fabs(a[0] - b[0]) < .5 || std::fabs(a[1] - b[1]) < .5) { reject("prostokąt", "ma wymiar mniejszy niż 0,5 mm — przesuń kursor"); return; }
                draft.points = {draft.points[0], cursor.point};
                draft.stage = 2;
                draft.anchor = {sx, sy};
                draft.depth = 0;
                status.set("Przesuń kursor, aby wyciągnąć (albo kliknij dla grubości z panelu); możesz wpisać wartość i Enter.");
            } else if (tool == "polygon" && draft.stage == 1) {
                auto first = screen(camera, draft.points[0], canvas->winfo_width(), canvas->winfo_height());
                if (draft.points.size() >= 3 && std::hypot(first.first - sx, first.second - sy) < 9) {
                    draft.stage = 2; draft.anchor = {sx, sy}; draft.depth = 0;
                    status.set("Kontur zamknięty. Wyciągnij kursorem lub wpisz grubość i Enter.");
                } else {
                    draft.points.push_back(cursor.point);
                }
            } else if (draft.stage == 2) {
                finish_draft_with_depth(std::fabs(draft.depth) < .5 ? panel_thickness() : draft.depth);
            }
            draw_overlay();
            return;
        }
        if (tool == "pull") {
            if (draft.stage == 0) {
                if (!cursor.hit) { reject("wyciąganie", "kliknij ścianę płyty"); return; }
                const core::Part& part = proj->parts[cursor.hit->part];
                (void)mg::pull_face(part, cursor.hit->face, 1);  // wczesne sprawdzenie: błąd z powodem zanim zaczniesz przeciągać
                draft.hit = cursor.hit;
                draft.part = cursor.hit->part;
                draft.original = part;
                draft.stage = 1;
                draft.anchor = {sx, sy};
                draft.depth = 0;
                selection = {part.id};
                status.set("Przeciągnij i kliknij albo wpisz odległość i Enter.");
                note("modelowanie", std::format("wyciąganie: ściana „{}” płyty „{}”", draft.hit->face, part.name));
            } else {
                commit_pull(*draft.part, draft.hit->face, draft.depth);
                draft = Draft();
            }
            draw_overlay();
            return;
        }
        if (tool == "move") {
            if (draft.stage == 0) {
                if (!cursor.hit || !cursor.valid) { reject("przesuwanie", "kliknij płytę, którą chcesz przesunąć"); return; }
                const core::Part& part = proj->parts[cursor.hit->part];
                if (!selection.contains(part.id)) selection = shift ? selection : std::set<std::string>{};
                selection.insert(part.id);
                draft.ids.assign(selection.begin(), selection.end());
                draft.plane = *cursor.plane;
                draft.base = cursor.point;
                draft.delta = {0, 0, 0};
                draft.stage = 1;
                refresh_panel();
                status.set("Kliknij miejsce docelowe; strzałki blokują oś (→ X, ← Y, ↑ Z); wpisz odległość (z ‘*N’ dla N kopii) i Enter.");
                note("modelowanie", std::format("przesuwanie: punkt bazowy ({}, {}, {}), płyt: {}", g(cursor.point[0]), g(cursor.point[1]), g(cursor.point[2]), draft.ids.size()));
            } else {
                int count = 1;
                try { count = std::max(1, (int)parse_field(copies.get(), "Liczba kopii")); } catch (const std::invalid_argument&) {}
                bool copy = copy_mode.get() || ctrl;
                commit_move(draft.ids, draft.delta, copy, copy ? count : 1);
                draft = Draft();
            }
            draw_overlay();
            return;
        }
        if (tool == "mill") {
            if (draft.stage == 0) {
                if (!cursor.hit) { reject("frezowanie", "kliknij dużą ścianę (górę lub spód) płyty"); return; }
                if (cursor.hit->face != "top" && cursor.hit->face != "bottom") { reject("frezowanie", "frezować można górę lub spód płyty, nie jej bok"); return; }
                draft.plane = cursor.hit->plane;
                draft.part = cursor.hit->part;
                draft.mill_face = cursor.hit->face;
                draft.points = {cursor.point};
                draft.stage = 1;
                selection = {proj->parts[cursor.hit->part].id};
                refresh_panel();
                if (cutter_kind.get() == "Kieszeń okrągła") {
                    cutters::Op op;
                    op.id = core::uid();
                    op.name = cutter_name.get().empty() ? "Kieszeń okrągła" : cutter_name.get();
                    op.kind = "kieszen_okragla";
                    op.cutter = panel_cutter();
                    op.face = draft.mill_face;
                    op.depth = parse_field(cutter_depth.get(), "Głębokość");
                    op.width = pocket_size.get().empty() ? 0 : parse_field(pocket_size.get(), "Średnica kieszeni");
                    const core::Part& part = proj->parts[*draft.part];
                    mg::Point ab = mg::to_plane(*draft.plane, cursor.point);
                    auto pts = part.points();
                    op.points = {{ab[0] + pts[0][0], ab[1] + pts[0][1]}};
                    add_mill_op(*draft.part, op);
                    draft = Draft();
                } else {
                    status.set(cutter_kind.get() == "Rowek" ? "Kliknij koniec rowka albo wpisz jego długość i Enter." : "Kliknij przeciwległy narożnik kieszeni albo wpisz wymiary i Enter.");
                }
            } else {
                apply_typed("");  // drugi punkt ze współrzędnych kursora
            }
            draw_overlay();
            return;
        }
    } catch (const std::invalid_argument& e) {
        reject(std::string("kliknięcie (") + tool_info(tool).label + ")", e.what());
        draft = Draft();
        draw_overlay();
    } catch (const std::out_of_range& e) {
        reject(std::string("kliknięcie (") + tool_info(tool).label + ")", e.what());
        draft = Draft();
        draw_overlay();
    }
}

void Modeler::finish_draft_with_depth(double depth) {
    if (!draft.plane) return;
    bool ok = false;
    if (tool == "rect" && draft.points.size() == 2) {
        try {
            mg::Box box = mg::box_from_rectangle(*draft.plane, draft.points[0], draft.points[1], depth);
            ok = add_board(box, "prostokąt");
        } catch (const std::invalid_argument& e) {
            reject("prostokąt", e.what());
        }
    } else if ((tool == "polygon" || tool == "pencil" || tool == "circle") && draft.points.size() >= 3) {
        ok = add_polygon_board(*draft.plane, draft.points, depth);
    }
    if (ok) {
        draft = Draft();
        typed.set("");
        status.set(tool_info(tool).hint);
    }
}

bool Modeler::apply_typed(const std::string& text_in) {
    if (mode == "2d") return apply_typed_2d(text_in);
    std::string text = text_in;
    try {
        if (tool == "rect" || tool == "polygon" || tool == "pencil" || tool == "circle") {
            if (draft.stage == 1 && tool == "circle") {
                if (text.empty()) throw std::invalid_argument("wpisz promień okręgu, np. 150");
                auto numbers = mg::parse_numbers(text);
                if (numbers.size() != 1) throw std::invalid_argument("okrąg wymaga jednej liczby — promienia");
                typed.set("");
                return circle_outline(numbers[0]);
            }
            if (draft.stage == 1 && tool == "rect") {
                if (text.empty()) throw std::invalid_argument("wpisz wymiary, np. 600x400");
                auto numbers = mg::parse_numbers(text);
                if (numbers.size() != 2) throw std::invalid_argument("prostokąt wymaga dwóch wymiarów, np. 600x400");
                mg::Point a = mg::to_plane(*draft.plane, draft.points[0]);
                mg::Point b = cursor.valid ? mg::to_plane(*draft.plane, cursor.point) : a;
                double su = b[0] >= a[0] ? 1 : -1, sv = b[1] >= a[1] ? 1 : -1;
                draft.points = {draft.points[0], mg::from_plane(*draft.plane, a[0] + su * numbers[0], a[1] + sv * numbers[1])};
                (void)mg::box_from_rectangle(*draft.plane, draft.points[0], draft.points[1], 1);
                draft.stage = 2;
                draft.anchor = mouse;
                draft.depth = 0;
                typed.set("");
                status.set("Wpisz grubość (np. 18) i Enter albo wyciągnij kursorem.");
                note("modelowanie", std::format("prostokąt wpisany: {} × {} mm", g(numbers[0]), g(numbers[1])));
                draw_overlay();
                return true;
            }
            if (draft.stage == 1 && tool == "polygon") {
                if (text.empty()) {
                    if (draft.points.size() < 3) throw std::invalid_argument("kontur ma mniej niż 3 wierzchołki");
                    draft.stage = 2; draft.anchor = mouse; draft.depth = 0;
                    status.set("Kontur zamknięty. Wpisz grubość i Enter albo wyciągnij kursorem.");
                    draw_overlay();
                    return true;
                }
                auto numbers = mg::parse_numbers(text);
                if (!cursor.valid) throw std::invalid_argument("kursor nie wskazuje płaszczyzny rysowania");
                Vec3 direction = mg::unit(mg::sub(cursor.point, draft.points.back()));
                draft.points.push_back(mg::add(draft.points.back(), mg::scale(direction, numbers[0])));
                typed.set("");
                draw_overlay();
                return true;
            }
            if (draft.stage == 2) {
                double depth = text.empty() ? (std::fabs(draft.depth) < .5 ? panel_thickness() : draft.depth) : mg::parse_numbers(text)[0];
                finish_draft_with_depth(depth);
                return true;
            }
            throw std::invalid_argument("najpierw kliknij pierwszy punkt");
        }
        if (tool == "pull" && draft.stage == 1 && draft.part) {
            double distance = text.empty() ? draft.depth : mg::parse_numbers(text)[0];
            bool ok = commit_pull(*draft.part, draft.hit->face, distance);
            if (ok) { draft = Draft(); typed.set(""); }
            return ok;
        }
        if (tool == "move" && draft.stage == 1) {
            int count = 1;
            std::string value = text;
            auto star = text.find('*');
            if (star != std::string::npos) {
                count = std::max(1, (int)parse_field(text.substr(star + 1), "Liczba kopii"));
                value = text.substr(0, star);
            }
            Vec3 delta = draft.delta;
            if (!value.empty()) {
                double distance = mg::parse_numbers(value)[0];
                Vec3 direction;
                if (draft.axis_lock >= 0) { direction = {0, 0, 0}; direction[(size_t)draft.axis_lock] = 1; }
                else direction = mg::unit(draft.delta);
                delta = mg::scale(direction, distance);
            }
            bool copy = copy_mode.get() || star != std::string::npos;
            bool ok = commit_move(draft.ids, delta, copy, copy ? count : 1);
            if (ok) { draft = Draft(); typed.set(""); }
            return ok;
        }
        if (tool == "mill" && draft.stage == 1 && draft.part && draft.plane) {
            core::ProjectPtr proj = project();
            const core::Part& part = proj->parts[*draft.part];
            auto pts = part.points();
            auto local = [&](const Vec3& p) {
                mg::Point ab = mg::to_plane(*draft.plane, p);
                return mg::Point{ab[0] + pts[0][0], ab[1] + pts[0][1]};
            };
            cutters::Op op;
            op.id = core::uid();
            const std::string kind = cutter_kind.get();
            op.kind = kind == "Rowek" ? "rowek" : "kieszen";
            op.name = cutter_name.get().empty() ? kind : cutter_name.get();
            op.cutter = panel_cutter();
            op.face = draft.mill_face;
            op.depth = parse_field(cutter_depth.get(), "Głębokość");
            Vec3 end = cursor.valid ? cursor.point : draft.points[0];
            if (!text.empty()) {
                auto numbers = mg::parse_numbers(text);
                mg::Point a = mg::to_plane(*draft.plane, draft.points[0]);
                mg::Point b = cursor.valid ? mg::to_plane(*draft.plane, cursor.point) : a;
                if (op.kind == "rowek") {
                    double dx = b[0] - a[0], dy = b[1] - a[1], len = std::hypot(dx, dy);
                    if (len < 1e-6) throw std::invalid_argument("przesuń kursor w stronę, w którą ma biec rowek");
                    end = mg::from_plane(*draft.plane, a[0] + dx / len * numbers[0], a[1] + dy / len * numbers[0]);
                } else {
                    if (numbers.size() != 2) throw std::invalid_argument("kieszeń wymaga dwóch wymiarów, np. 120x60");
                    end = mg::from_plane(*draft.plane, a[0] + (b[0] >= a[0] ? 1 : -1) * numbers[0], a[1] + (b[1] >= a[1] ? 1 : -1) * numbers[1]);
                }
            }
            op.points = {local(draft.points[0]), local(end)};
            bool ok = add_mill_op(*draft.part, op);
            if (ok) { draft = Draft(); typed.set(""); status.set(tool_info(tool).hint); }
            return ok;
        }
        throw std::invalid_argument("w tym narzędziu nie ma czego wpisywać — zacznij rysowanie lub wybierz element");
    } catch (const std::invalid_argument& e) {
        reject("wpisana wartość „" + text_in + "”", e.what());
        return false;
    } catch (const std::out_of_range& e) {
        reject("wpisana wartość „" + text_in + "”", e.what());
        return false;
    }
}

void Modeler::on_key(const std::string& keysym, int state) {
    if (mode == "2d") { on_key_2d(keysym, state); return; }
    const bool ctrl = (state & 4) != 0;
    static const std::vector<std::pair<std::string, char>> chars = {{"period", '.'}, {"comma", ','}, {"semicolon", ';'}, {"asterisk", '*'}, {"minus", '-'}, {"KP_Decimal", '.'}};
    if (keysym == "Escape") { cancel(); return; }
    if (keysym == "Return" || keysym == "KP_Enter") { apply_typed(typed.get()); return; }
    if (keysym == "BackSpace") {
        std::string t = typed.get();
        if (!t.empty()) { t.pop_back(); typed.set(t); }
        else if (tool == "polygon" && draft.stage == 1 && draft.points.size() > 1) { draft.points.pop_back(); }
        draw_overlay();
        return;
    }
    if (keysym == "Delete") { erase_selected(); return; }
    if (ctrl && (keysym == "z" || keysym == "Z")) { app->undo(); request_scene(); refresh_panel(); return; }
    if (ctrl && (keysym == "y" || keysym == "Y")) { app->redo(); request_scene(); refresh_panel(); return; }
    if (keysym == "Right" || keysym == "Left" || keysym == "Up") {
        int axis = keysym == "Right" ? 0 : keysym == "Left" ? 1 : 2;
        draft.axis_lock = draft.axis_lock == axis ? -1 : axis;
        status.set(draft.axis_lock < 0 ? "Blokada osi wyłączona." : std::string("Blokada osi ") + "XYZ"[draft.axis_lock] + ".");
        note("modelowanie", draft.axis_lock < 0 ? "blokada osi wyłączona" : std::string("blokada osi ") + "XYZ"[draft.axis_lock]);
        draw_overlay();
        return;
    }
    std::string name = keysym;
    if (name.rfind("KP_", 0) == 0 && name.size() == 4) name = name.substr(3);
    if (name.size() == 1 && std::string("0123456789").find(name[0]) != std::string::npos) { typed.set(typed.get() + name); draw_overlay(); return; }
    for (const auto& [sym, ch] : chars)
        if (keysym == sym) { typed.set(typed.get() + ch); draw_overlay(); return; }
    if ((keysym == "x" || keysym == "X") && !typed.get().empty()) { typed.set(typed.get() + "x"); draw_overlay(); return; }
    if (keysym.size() == 1) {
        char c = (char)std::tolower((unsigned char)keysym[0]);
        for (const auto& t : tools())
            if (std::tolower((unsigned char)t.key[0]) == c) { set_tool(t.id); return; }
        if (c == 'c') {
            copy_mode.set(!copy_mode.get());
            note("modelowanie", copy_mode.get() ? "przesuwanie jako kopia: włączone" : "przesuwanie jako kopia: wyłączone");
        }
    }
}

// ------------------------------------------------------------------ operacje na projekcie

bool Modeler::replace_part(const core::Part& part, const std::string& what, const std::string& category) {
    try {
        core::ProjectPtr temp = features::replace_part(*app->project, part);
        app->snapshot();
        app->project = temp;
        app->refresh();
        note(category, what);
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject(what, e.what());
    } catch (const std::out_of_range& e) {
        reject(what, e.what());
    } catch (const py::StopIteration&) {
        reject(what, "płyta nie istnieje już w projekcie");
    }
    return false;
}

bool Modeler::add_board(const mg::Box& box, const std::string& origin) {
    try {
        std::string material_id = app->project->materials.empty() ? std::string() : app->project->materials[0].id;
        for (const auto& m : app->project->materials) if (m.name == material_name.get()) material_id = m.id;
        core::Part part = mg::board_from_box(box, "", group_name.get().empty() ? "Mebel" : group_name.get(), material_id);
        std::vector<std::string> existing;
        for (const auto& p : app->project->parts) existing.push_back(p.name);
        part.name = mg::suggest_name(part, existing);
        auto temp = std::make_shared<core::Project>(*app->project);
        temp->parts.push_back(part);
        temp->validate();
        app->snapshot();
        app->project = temp;
        app->refresh();
        selection = {part.id};
        note("modelowanie", std::format("dodano płytę „{}” z bryły ({}): {}", part.name, origin, mg::describe(part)));
        status.set(std::format("Dodano „{}”: {} × {} × {} mm.", part.name, g(part.u), g(part.v), g(part.t)));
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject("dodanie płyty (" + origin + ")", e.what());
    } catch (const std::out_of_range& e) {
        reject("dodanie płyty (" + origin + ")", e.what());
    }
    return false;
}

bool Modeler::add_polygon_board(const mg::Plane& plane, const std::vector<Vec3>& points, double depth) {
    try {
        std::string material_id = app->project->materials.empty() ? std::string() : app->project->materials[0].id;
        for (const auto& m : app->project->materials) if (m.name == material_name.get()) material_id = m.id;
        core::Part part = mg::board_from_polygon(plane, points, depth, "", group_name.get().empty() ? "Mebel" : group_name.get(), material_id);
        std::vector<std::string> existing;
        for (const auto& p : app->project->parts) existing.push_back(p.name);
        part.name = mg::suggest_name(part, existing);
        auto temp = std::make_shared<core::Project>(*app->project);
        temp->parts.push_back(part);
        temp->validate();
        app->snapshot();
        app->project = temp;
        app->refresh();
        selection = {part.id};
        note("modelowanie", std::format("dodano płytę „{}” z konturu ({} wierzchołków): {}", part.name, points.size(), mg::describe(part)));
        status.set(std::format("Dodano „{}” z konturu.", part.name));
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject("dodanie płyty z konturu", e.what());
    } catch (const std::out_of_range& e) {
        reject("dodanie płyty z konturu", e.what());
    }
    return false;
}

bool Modeler::commit_pull(size_t part_index, const std::string& face, double distance) {
    if (part_index >= app->project->parts.size()) { reject("wyciąganie", "płyta nie istnieje"); return false; }
    try {
        const core::Part& part = app->project->parts[part_index];
        core::Part changed = mg::pull_face(part, face, distance);
        return replace_part(changed, std::format("wyciągnięto ścianę „{}” płyty „{}” o {} mm ({})", face, part.name, g(distance), mg::describe(changed)), "modelowanie");
    } catch (const std::invalid_argument& e) {
        reject("wyciąganie", e.what());
    }
    return false;
}

bool Modeler::commit_move(const std::vector<std::string>& ids, const Vec3& delta, bool copy, int count) {
    try {
        if (ids.empty()) throw std::invalid_argument("nie zaznaczono żadnej płyty");
        if (mg::length(delta) < 1e-9) throw std::invalid_argument("przesunięcie jest zerowe");
        auto temp = std::make_shared<core::Project>(*app->project);
        size_t done = 0;
        std::vector<core::Part> added;
        for (const auto& id : ids) {
            auto found = std::find_if(temp->parts.begin(), temp->parts.end(), [&](const core::Part& p) { return p.id == id; });
            if (found == temp->parts.end()) throw std::invalid_argument("zaznaczona płyta nie istnieje już w projekcie");
            if (!mg::axis_aligned(*found)) throw std::invalid_argument("płyta „" + found->name + "” jest obrócona — ustaw jej położenie w inspektorze");
            if (copy) {
                for (auto& c : mg::array_copies(*found, delta, count)) added.push_back(c);
            } else {
                *found = mg::moved(*found, delta);
            }
            ++done;
        }
        for (auto& c : added) temp->parts.push_back(c);
        for (const auto& id : ids) {
            if (!copy) temp = assembly::rebuild_dependents(*temp, id);
        }
        temp->validate();
        app->snapshot();
        app->project = temp;
        app->refresh();
        if (copy) { selection.clear(); for (const auto& c : added) selection.insert(c.id); }
        note("modelowanie", std::format("{} {} płyt o ({}, {}, {}) mm{}", copy ? "skopiowano" : "przesunięto", done, g(delta[0]), g(delta[1]), g(delta[2]),
                                        copy ? std::format(" · kopii na płytę: {}", count) : std::string()));
        status.set(std::format("{} {} płyt.", copy ? "Skopiowano" : "Przesunięto", done));
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject(copy ? "kopiowanie" : "przesuwanie", e.what());
    } catch (const std::out_of_range& e) {
        reject(copy ? "kopiowanie" : "przesuwanie", e.what());
    } catch (const py::StopIteration&) {
        reject(copy ? "kopiowanie" : "przesuwanie", "nie udało się przeliczyć powiązanych elementów");
    }
    return false;
}

bool Modeler::erase_selected() {
    try {
        if (selection.empty()) throw std::invalid_argument("nie zaznaczono żadnej płyty");
        auto temp = std::make_shared<core::Project>(*app->project);
        std::string names;
        for (const auto& id : selection) {
            auto found = std::find_if(temp->parts.begin(), temp->parts.end(), [&](const core::Part& p) { return p.id == id; });
            if (found == temp->parts.end()) continue;
            names += (names.empty() ? "" : ", ") + found->name;
            assembly::clear_generated(*temp, id);
            json kept = json::array();
            for (const auto& h : temp->hardware) if (py::get(h, "owner_id") != id) kept.push_back(h);
            temp->hardware = kept;
            temp->parts.erase(found);
        }
        if (names.empty()) throw std::invalid_argument("zaznaczone płyty nie istnieją już w projekcie");
        temp->validate();
        app->snapshot();
        app->project = temp;
        app->refresh();
        note("modelowanie", "usunięto płyty: " + names);
        status.set("Usunięto: " + names);
        selection.clear();
        refresh_panel();
        request_scene();
        return true;
    } catch (const std::invalid_argument& e) {
        reject("usunięcie płyt", e.what());
    } catch (const std::out_of_range& e) {
        reject("usunięcie płyt", e.what());
    }
    return false;
}

bool Modeler::add_mill_op(size_t part_index, cutters::Op op) {
    if (part_index >= app->project->parts.size()) { reject("frezowanie", "płyta nie istnieje"); return false; }
    try {
        const core::Part& part = app->project->parts[part_index];
        if (op.id.empty()) op.id = core::uid();
        std::vector<core::json> cuts = cutters::cuts_for(op, part);
        core::Part changed = part;
        changed.machining = cutters::add_cuts(part.machining, cuts);
        return replace_part(changed, "operacja frezowania na płycie „" + part.name + "”: " + cutters::describe(op, cuts.size()), "frezy");
    } catch (const std::invalid_argument& e) {
        reject("frezowanie", e.what());
        note("frezy", std::string("odrzucono operację frezowania: ") + e.what());
    }
    return false;
}

bool Modeler::remove_mill_op(size_t part_index, const std::string& op_id) {
    if (part_index >= app->project->parts.size()) { reject("usunięcie operacji", "płyta nie istnieje"); return false; }
    const core::Part& part = app->project->parts[part_index];
    core::Part changed = part;
    changed.machining = cutters::remove_op(part.machining, op_id);
    if (changed.machining.size() == part.machining.size()) { reject("usunięcie operacji", "nie ma takiej operacji na tej płycie"); return false; }
    return replace_part(changed, std::format("usunięto operację {} z płyty „{}” ({} wpisów mniej)", op_id, part.name, part.machining.size() - changed.machining.size()), "frezy");
}

bool Modeler::apply_edge_cutter_ui() {
    try {
        const core::Part* part = selected_part();
        if (!part) throw std::invalid_argument("zaznacz płytę, której krawędzie mają być obrobione");
        cutters::EdgeOp op;
        op.cutter = cutters::info(edge_cutter_var.get()).id;
        op.size = parse_field(edge_size.get(), "Rozmiar");
        std::vector<std::string> ids = sketch_design::edge_ids_all(part->points().size());
        const std::string scope = edge_scope.get();
        for (const auto& id : ids) {
            if (scope == "Tylko góra" && id.rfind("top:", 0) != 0) continue;
            if (scope == "Tylko spód" && id.rfind("bottom:", 0) != 0) continue;
            op.edges.push_back(id);
        }
        cutters::EdgeResult result = cutters::apply_edge_cutter(*part, op);
        core::Part changed = *part;
        changed.fillets = result.fillets;
        changed.chamfers = result.chamfers;
        return replace_part(changed, std::format("frez krawędziowy „{}” {} mm na płycie „{}”: {} krawędzi ({})", edge_cutter_var.get(), g(op.size), part->name, result.touched, scope), "frezy");
    } catch (const std::invalid_argument& e) {
        reject("frez krawędziowy", e.what());
        note("frezy", std::string("odrzucono frez krawędziowy: ") + e.what());
    }
    return false;
}

bool Modeler::add_pattern_ui() {
    try {
        const core::Part* part = selected_part();
        if (!part) throw std::invalid_argument("zaznacz płytę, na której ma być wzorek");
        const auto& info = sketch_design::pattern_info(pattern_var.get());
        std::vector<mg::Point> contour = part->contour.empty() ? std::vector<mg::Point>{} : part->points();
        auto result = sketch_design::generate_pattern(info.id, contour, part->u, part->v, part->t);
        core::Part changed = *part;
        changed.machining = sketch_design::merge_pattern(part->machining, info.id, result.cuts);
        return replace_part(changed, std::format("wzorek „{}” na płycie „{}”: {} frezów, pominięto {}", info.name, part->name, result.cuts.size(), result.skipped), "frezy");
    } catch (const std::invalid_argument& e) {
        reject("wzorek", e.what());
        note("frezy", std::string("odrzucono wzorek: ") + e.what());
    }
    return false;
}

bool Modeler::clear_patterns_ui() {
    const core::Part* part = selected_part();
    if (!part) { reject("usunięcie wzorków", "zaznacz płytę"); return false; }
    core::Part changed = *part;
    changed.machining = sketch_design::remove_patterns(part->machining);
    if (changed.machining.size() == part->machining.size()) { reject("usunięcie wzorków", "płyta „" + part->name + "” nie ma wzorków"); return false; }
    return replace_part(changed, std::format("usunięto wzorki z płyty „{}” ({} frezów)", part->name, part->machining.size() - changed.machining.size()), "frezy");
}

// ------------------------------------------------------------------ panel

void Modeler::update_op_list() {
    if (ops_list == nullptr) return;
    ops_list->delete_("0", "end");
    ops_listed = 0;
    const core::Part* part = selected_part();
    if (!part) return;
    for (const auto& op : cutters::list_ops(part->machining)) {
        ops_list->insert("end", std::format("{} — {} · {} mm · {} wpisów", op.name, op.cutter_name, g(op.depth), op.cuts));
        ++ops_listed;
    }
    size_t patterns = sketch_design::count_patterns(part->machining);
    if (patterns > 0) ops_list->insert("end", std::format("(wzorki: {} frezów — nie są operacjami)", patterns));
}

void Modeler::refresh_panel() {
    const core::Part* part = selected_part();
    if (!part) selection_text.set(selection.empty() ? "Nic nie zaznaczono." : std::format("Zaznaczono {} płyt (nie ma ich w projekcie).", selection.size()));
    else selection_text.set(std::format("{}{} — {} × {} × {} mm · mebel „{}”{}", part->name, selection.size() > 1 ? std::format(" (+{})", selection.size() - 1) : std::string(),
                                        g(part->u), g(part->v), g(part->t), part->group,
                                        part->machining.empty() && part->fillets.empty() && part->chamfers.empty() ? std::string() : " · z obróbką"));
    update_op_list();
    if (mode == "2d" || shapes_list != nullptr) {
        shapes_list->delete_("0", "end");
        shapes_listed = 0;
        for (const auto& s : shapes) {
            shapes_list->insert("end", cad2d::describe(s));
            ++shapes_listed;
        }
        shapes_text.set(shapes.empty() ? "Szkic jest pusty." : std::format("Kształtów w szkicu: {}{}", shapes.size(), editing_part ? " · edytujesz obrys płyty" : ""));
    }
}

namespace {
void draw_polys(tk::Canvas* canvas, const std::vector<std::vector<mg::Point>>& polys, const std::vector<std::string>& fills, const std::string& tag) {
    canvas->delete_(tag);
    for (size_t i = 0; i < polys.size(); ++i)
        canvas->create_polygon(flat(polys[i]), {{"fill", fills[std::min(i, fills.size() - 1)]}, {"outline", "#5f6b76"}, {"width", 1}, {"tags", tag}});
}
}  // namespace

void Modeler::draw_cutter_preview() {
    if (cutter_preview == nullptr) return;
    try {
        auto polys = cutters::cutter_thumbnail(cutters::info(cutter_var.get()).id, 120, 78);
        draw_polys(cutter_preview, polys, {"#f7f4ee", "#9aa3ad", "#c7882f", "#7a5a2b"}, "cutter_preview");
    } catch (const std::invalid_argument&) {}
}

void Modeler::draw_edge_preview() {
    if (edge_preview == nullptr) return;
    try {
        std::string style = cutters::info(edge_cutter_var.get()).id == "zaokraglajacy" ? "zaokraglenie" : "fazka";
        auto polygon = sketch_design::edge_section_thumbnail(style, 120, 70);
        edge_preview->delete_("edge_preview");
        edge_preview->create_polygon(flat(polygon), {{"fill", "#c9a26b"}, {"outline", "#5f6b76"}, {"width", 1}, {"tags", "edge_preview"}});
    } catch (const std::invalid_argument&) {}
}

void Modeler::draw_pattern_preview() {
    if (pattern_preview == nullptr) return;
    try {
        auto polys = sketch_design::pattern_thumbnail(sketch_design::pattern_info(pattern_var.get()).id, 120, 78);
        draw_polys(pattern_preview, polys, {"#e6d3b3", "#7a5a2b"}, "pattern_preview");
    } catch (const std::invalid_argument&) {}
}

Modeler* current() { return g_open; }

Modeler* open(App* app) {
    if (g_open != nullptr) {
        try {
            if (g_open->winfo_exists() && g_open->app == app) {
                g_open->workspace->select(g_open);
                journal::event(app, "modelowanie", "Pracownia 3D jest już otwarta — pokazano istniejące okno");
                return g_open;
            }
        } catch (const std::exception&) {}
        g_open = nullptr;
    }
    g_open = new Modeler(app);
    g_open->workspace->select(g_open);
    return g_open;
}

}  // namespace modeler_ui
