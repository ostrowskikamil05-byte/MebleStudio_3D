#include "edge_editor.h"

#include "graphics_mode_ui.h"

#include <algorithm>
#include <format>
#include <set>

#include "app.h"
#include "assembly.h"
#include "pyutil.h"

namespace edge_editor {

using core::Camera;
using core::Part;
using core::Point3;

namespace {
// getattr(spec[2], 'chain', (spec[0],))
std::vector<std::string> chain_of(const cad_geometry::EdgeSpec& spec) {
    if (spec.geometry.chain) return *spec.geometry.chain;
    return {spec.edge};
}
bool contains(const std::vector<std::string>& items, const std::string& key) {
    return std::find(items.begin(), items.end(), key) != items.end();
}
}  // namespace

EdgeEditor::EdgeEditor(App* app_, const Part& part_, std::optional<cad_geometry::Edge> edge, const std::string& mode)
    : dock_workspace::DockDocument(app_), app(app_), part(part_) {
    attribute = mode == "chamfer" ? "chamfers" : "fillets"; parameter = mode == "chamfer" ? "distance" : "radius";
    for (auto& item : part.get_attr(attribute)) values[item["edge"].get<std::string>()] = item[parameter];
    specs = cad_geometry::edge_specs(part.points(), part.t, part.profile, {part.u, part.v});
    for (auto& spec : specs) {
        if (values.contains(spec.edge)) {
            for (auto& key : chain_of(spec)) values[key] = values[spec.edge];
        }
    }
    title((mode == "chamfer" ? "Fazy 45° — " : "Zaokrąglenia krawędzi — ") + part.name); geometry("1050x650");
    transient(app); grab_set();
    (new ttk::Label(this, {{"text", "Każdy ostry kant ma własną wartość w mm. 0 usuwa obróbkę.\nŁuki i odcinki połączone stycznie są obrabiane wspólnie — zaznaczenie jednego ustawia cały ciąg.\nFaza i zaokrąglenie nie mogą dotyczyć tej samej krawędzi lub ciągu."}, {"padding", 12}}))->pack({{"fill", "x"}});
    auto* footer = new ttk::Frame(this, {{"padding", 8}}); footer->pack({{"side", "bottom"}, {"fill", "x"}});
    (new ttk::Button(footer, {{"text", "Zapisz obróbkę krawędzi"}, {"command", std::function<void()>([this] { save(); })}, {"style", "Accent.TButton"}}))->pack({{"side", "right"}});
    summary = tk::StringVar(); (new ttk::Label(footer, {{"textvariable", summary}}))->pack({{"side", "left"}});
    auto* body = new ttk::Frame(this); body->pack({{"fill", "both"}, {"expand", true}});
    auto* side = new ttk::Frame(body, {{"padding", 10}}); side->pack({{"side", "left"}, {"fill", "y"}});
    auto* controls = new ttk::Frame(side); controls->pack({{"side", "bottom"}, {"fill", "x"}});
    graphics_mode_ui::add_compact(controls, app_, "edytor krawędzi");
    radius = tk::StringVar("3");
    (new ttk::Label(controls, {{"text", "Odległość fazy / promień [mm]"}}))->pack({{"anchor", "w"}, {"pady", "8 0"}});
    (new ttk::Entry(controls, {{"textvariable", radius}}))->pack({{"fill", "x"}});
    (new ttk::Button(controls, {{"text", "Ustaw wartość / podgląd"}, {"command", std::function<void()>([this] { set_radius(); })}}))->pack({{"fill", "x"}, {"pady", 6}});
    (new ttk::Button(controls, {{"text", "Usuń wszystkie z tej operacji"}, {"command", std::function<void()>([this] { reset(); })}}))->pack({{"fill", "x"}});
    table = new ttk::Treeview(side, {{"columns", "edge radius"}, {"show", "headings"}, {"height", 15}, {"selectmode", "extended"}});
    table->heading("edge", {{"text", "Wybrana krawędź"}}); table->heading("radius", {{"text", mode == "chamfer" ? "C mm" : "R mm"}});
    table->column("edge", {{"width", 240}}); table->column("radius", {{"width", 65}});
    table->pack({{"fill", "both"}, {"expand", true}});
    auto* scroll = new ttk::Scrollbar(side, {{"orient", "vertical"}, {"command", table->path() + " yview"}}); scroll->pack({{"side", "right"}, {"fill", "y"}}); table->configure({{"yscrollcommand", scroll->path() + " set"}});
    canvas = new tk::Canvas(body, {{"bg", "#f1eee8"}, {"highlightthickness", 0}}); canvas->pack({{"fill", "both"}, {"expand", true}});
    canvas->bind("<Configure>", [this](tk::Event&) { draw(); return std::string(); });
    table->bind("<<TreeviewSelect>>", [this](tk::Event& e) { select(&e); return std::string(); });
    candidate = part; refresh();
    if (edge && !edge->empty()) { table->selection_set(*edge); table->see(*edge); }
    after(80, [this] { draw(); });
}

void EdgeEditor::refresh() {
    auto selection = table->selection(); table->delete_(table->get_children());
    for (auto& spec : specs) {
        double value = values.contains(spec.edge) ? py::to_float(values[spec.edge]) : 0;
        table->insert("", "end", {{"iid", spec.edge}, {"values", std::vector<std::string>{spec.name, std::format("{:g}", value)}}});
    }
    for (auto& key : selection) {
        if (table->exists(key)) table->selection_add(key);
    }
    summary.set(std::format("{} kantów z parametrem", values.size()));
}

void EdgeEditor::select(tk::Event*) {
    auto keys = table->selection();
    if (keys.size() == 1) radius.set(values.contains(keys[0]) ? py::str(values[keys[0]]) : "0");
    draw();
}

void EdgeEditor::set_radius() {
    try {
        double radius_ = core::finite(py::replace_all(radius.get(), ",", "."), 0, 10000);
        auto keys = table->selection();
        if (keys.empty()) throw py::ValueError("Wybierz jeden lub kilka kantów (Ctrl / Shift).");
        std::set<std::string> selected(keys.begin(), keys.end());
        for (auto& spec : specs) {
            if (contains(keys, spec.edge)) for (auto& key : chain_of(spec)) selected.insert(key);
        }
        std::vector<std::string> sorted_keys(selected.begin(), selected.end());  // sorted(selected)
        json values_ = values;
        for (auto& key : sorted_keys) {
            if (radius_) values_[key] = radius_;
            else values_.erase(key);
        }
        Part candidate_ = part;
        json items = json::array();
        for (auto& [key, value] : values_.items()) items.push_back(json{{"edge", key}, {parameter, value}});
        candidate_.set_attr(attribute, items);
        candidate_.validate(); values = values_; candidate = candidate_; refresh(); draw();
    } catch (const py::ValueError& e) {
        tk::messagebox::showerror("Zaokrąglenia", e.what(), this);
    }
}

void EdgeEditor::reset() {
    values = json::object(); candidate = part; candidate.set_attr(attribute, json::array()); refresh(); draw();
}

void EdgeEditor::draw() {
    canvas->delete_("all"); int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10) return;
    core::Project project = *app->project; project.parts = {candidate};
    Camera camera; camera.fit(project, w, h);
    for (auto& face : core::render_faces(project, camera, w, h)) {
        std::vector<double> coords;
        for (auto& p : face["points"]) for (auto& v : p) coords.push_back(v.get<double>());
        bool mesh = py::truthy(py::get(face, "mesh"));
        std::string color = face["color"].get<std::string>();
        canvas->create_polygon(coords, {{"fill", color}, {"outline", mesh ? color : "#6b7266"}, {"width", mesh ? 0 : 1}});
    }
    auto selection = table->selection();
    for (auto& spec_ : specs) {
        if (!contains(selection, spec_.edge)) continue;
        const cad_geometry::EdgeSpec* spec = nullptr;
        for (auto& item : specs) if (item.edge == spec_.edge) { spec = &item; break; }
        std::vector<double> path;
        for (auto& point : cad_geometry::edge_path(*spec)) {
            Point3 q = camera.project(core::world(candidate, point[0], point[1], point[2]), w, h);
            path.push_back(q[0]); path.push_back(q[1]);
        }
        canvas->create_line(path, {{"fill", "#dc6b24"}, {"width", 3}, {"dash", "4 3"}});
    }
}

bool EdgeEditor::save() {
    if (!ensure_current_project()) return false;
    try {
        core::Project project = *app->project;
        auto found = std::find_if(project.parts.begin(), project.parts.end(), [&](const Part& p) { return p.id == part.id; });
        if (found == project.parts.end()) throw py::StopIteration();
        *found = candidate;
        core::ProjectPtr result = assembly::rebuild_dependents(project, part.id); result->validate();
        app->snapshot(); app->project = result; app->refresh(); app->fit(); destroy();
    } catch (const py::ValueError& e) {
        tk::messagebox::showerror("Zaokrąglenia", e.what(), this);
    }
    return true;
}

json EdgeEditor::_document_model() const {
    return json{{"part", core::asdict(part)}, {"candidate", core::asdict(candidate)}, {"values", values}};
}

}  // namespace edge_editor
