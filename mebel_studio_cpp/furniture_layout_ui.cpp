// Whole-furniture arrangement controls and screen-plane dragging.
#include "furniture_layout_ui.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <set>

#include "app.h"
#include "furniture_layout.h"
#include "project_print.h"
#include "pyutil.h"
#include "studio_interaction.h"

namespace furniture_layout_ui {

namespace fs = std::filesystem;
using core::ProjectPtr;
using json = core::json;

const std::vector<std::pair<std::string, std::string>> SIDES = {
    {"Po prawej (+X)", "x+"}, {"Po lewej (−X)", "x-"}, {"Z tyłu (+Y)", "y+"}, {"Z przodu (−Y)", "y-"}, {"Nad meblem (+Z)", "z+"}, {"Pod meblem (−Z)", "z-"}};
const std::vector<std::pair<std::string, std::string>> ALIGN = {{"Początek", "start"}, {"Środek", "center"}, {"Koniec", "end"}};

namespace {
// SIDES[key] / ALIGN[key] — KeyError z komunikatem jak str(KeyError(key)).
const std::string& lookup(const std::vector<std::pair<std::string, std::string>>& table, const std::string& key) {
    for (auto& [k, v] : table) if (k == key) return v;
    throw py::KeyError("'" + key + "'");
}
std::vector<std::string> keys_of(const std::vector<std::pair<std::string, std::string>>& table) {
    std::vector<std::string> out; for (auto& [k, v] : table) out.push_back(k);
    return out;
}
// list(dict.fromkeys(p.group for p in project.parts))
std::vector<std::string> unique_groups(const core::Project& project) {
    std::vector<std::string> names;
    for (auto& p : project.parts) if (std::find(names.begin(), names.end(), p.group) == names.end()) names.push_back(p.group);
    return names;
}
// f'{value:.3f}'.rstrip('0').rstrip('.')
std::string trimmed(double value) {
    std::string text = std::format("{:.3f}", value);
    while (!text.empty() && text.back() == '0') text.pop_back();
    while (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}
double layout_yaw(const json& layout) {
    if (layout.is_object() && layout.contains("yaw")) return layout["yaw"].get<double>();
    return 0;
}
double monotonic() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

std::array<double, 3> screen_move(const core::Camera& camera, double dx, double dy, bool vertical) {
    core::Point3 origin = camera.project({0, 0, 0}, 1, 1);
    std::array<std::array<double, 2>, 3> vectors{};
    std::array<core::Point3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    for (int a = 0; a < 3; ++a) {
        core::Point3 q = camera.project(axes[a], 1, 1);
        for (int i : {0, 1}) vectors[a][i] = q[i] - origin[i];
    }
    if (vertical) {
        double vx = vectors[2][0], vy = vectors[2][1]; double length = vx * vx + vy * vy;
        if (length < 1e-10) throw py::ValueError("Aby przesuwać w pionie, wybierz widok 3D lub Przód.");
        return {0, 0, (dx * vx + dy * vy) / length};
    }
    double ax = vectors[0][0], ay = vectors[0][1], bx = vectors[1][0], by = vectors[1][1]; double det = ax * by - ay * bx;
    if (std::abs(det) < 1e-8) throw py::ValueError("Aby układać na podłodze, wybierz widok 3D lub Góra.");
    return {(dx * by - dy * bx) / det, (ax * dy - ay * dx) / det, 0};
}

std::string selected_group(App* app) {
    core::Part* p = app->selected_part();
    if (p) return p->group;
    auto selection = app->tree->selection();
    if (!selection.empty()) {
        std::string name = app->tree->item(selection[0], "text");
        for (auto& q : app->project->parts) if (q.group == name) return name;
    }
    throw py::ValueError("Wybierz mebel lub jego element w drzewie projektu.");
}

void commit(App* app, ProjectPtr result, const std::string& note) {
    app->snapshot(); app->project = result; app->refresh(); app->status.set(note);
}

std::function<void()> guarded(App* app, std::function<void()> fn) {
    return [app, fn = std::move(fn)] {
        auto fail = [&](const char* what) { tk::messagebox::showerror("Układ mebli", what, app->canvas->winfo_toplevel()); };
        try { fn(); }
        catch (const py::ValueError& exc) { fail(exc.what()); }
        catch (const std::out_of_range& exc) { fail(exc.what()); }  // KeyError
        catch (const py::StopIteration& exc) { fail(exc.what()); }
        catch (const py::OSError& exc) { fail(exc.what()); }
    };
}

std::function<std::string(tk::Event&)> guarded(App* app, std::function<void(tk::Event&)> fn) {
    return [app, fn = std::move(fn)](tk::Event& event) {
        auto fail = [&](const char* what) { tk::messagebox::showerror("Układ mebli", what, app->canvas->winfo_toplevel()); };
        try { fn(event); }
        catch (const py::ValueError& exc) { fail(exc.what()); }
        catch (const std::out_of_range& exc) { fail(exc.what()); }
        catch (const py::StopIteration& exc) { fail(exc.what()); }
        catch (const py::OSError& exc) { fail(exc.what()); }
        return std::string();
    };
}

LayoutDocument::LayoutDocument(App* app_) : form_dialog::FormDocument(app_, "Układ kompletu · obrót i magnes", "940x660") {
    minsize(720, 510);
    groups = new ttk::Combobox(form, {{"state", "readonly"}, {"width", 55}});
    (new ttk::Label(form, {{"text", "Wybierz cały mebel"}, {"padding", "14 8"}}))->pack({{"anchor", "w"}});
    groups->pack({{"fill", "x"}, {"padx", 14}}); groups->bind("<<ComboboxSelected>>", guarded(app, std::function<void(tk::Event&)>([this](tk::Event&) { read(); })));
    info = tk::StringVar(); (new ttk::Label(form, {{"textvariable", info}, {"padding", 14}, {"wraplength", 880}}))->pack({{"fill", "x"}});
    footer->configure({{"padding", 12}});
    (new ttk::Button(footer, {{"text", "Wróć do widoku 3D"}, {"command", guarded(app, std::function<void()>([this] { show_design(); }))}}))->pack({{"side", "right"}});
    auto* body_ = new ttk::Frame(form, {{"padding", "14 0"}}); body_->pack({{"fill", "both"}, {"expand", true}});
    auto* position = new ttk::LabelFrame(body_, {{"text", "Pozycja mebla w komplecie"}, {"padding", 12}}); position->pack({{"fill", "x"}, {"pady", 6}});
    vars.clear();
    int col = 0;
    for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"x", "Minimum X [mm]"}, {"y", "Minimum Y [mm]"}, {"z", "Minimum Z [mm]"}, {"yaw", "Obrót [°]"}}) {
        (new ttk::Label(position, {{"text", label}}))->grid({{"row", 0}, {"column", col}, {"sticky", "w"}, {"padx", 5}});
        tk::StringVar var_; vars.emplace_back(key, var_);
        (new ttk::Entry(position, {{"textvariable", var_}, {"width", 15}}))->grid({{"row", 1}, {"column", col}, {"sticky", "ew"}, {"padx", 5}, {"pady", 5}});
        position->call({"grid", "columnconfigure", position->path(), std::to_string(col), "-weight", "1"});
        ++col;
    }
    auto* row = new ttk::Frame(position); row->grid({{"row", 2}, {"column", 0}, {"columnspan", 4}, {"sticky", "ew"}});
    (new ttk::Button(row, {{"text", "Zastosuj pozycję i kąt"}, {"command", guarded(app, std::function<void()>([this] { apply(); }))}, {"style", "Accent.TButton"}}))->pack({{"side", "left"}});
    (new ttk::Button(row, {{"text", "↶ −90°"}, {"command", guarded(app, std::function<void()>([this] { turn(-90); }))}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Button(row, {{"text", "↷ +90°"}, {"command", guarded(app, std::function<void()>([this] { turn(90); }))}}))->pack({{"side", "left"}});
    auto* snap = new ttk::LabelFrame(body_, {{"text", "Połącz bokami / ułóż kolejny poziom"}, {"padding", 12}}); snap->pack({{"fill", "x"}, {"pady", 10}});
    target = new ttk::Combobox(snap, {{"state", "readonly"}}); target->pack({{"fill", "x"}});
    row = new ttk::Frame(snap); row->pack({{"fill", "x"}, {"pady", 8}});
    side = tk::StringVar(SIDES.front().first); align = tk::StringVar("Początek"); gap = tk::StringVar("0");
    (new ttk::Combobox(row, {{"textvariable", side}, {"values", keys_of(SIDES)}, {"state", "readonly"}, {"width", 26}}))->pack({{"side", "left"}});
    (new ttk::Combobox(row, {{"textvariable", align}, {"values", keys_of(ALIGN)}, {"state", "readonly"}, {"width", 12}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Label(row, {{"text", "Odstęp [mm]"}}))->pack({{"side", "left"}}); (new ttk::Entry(row, {{"textvariable", gap}, {"width", 9}}))->pack({{"side", "left"}, {"padx", 6}});
    (new ttk::Button(snap, {{"text", "Dosuń do wybranego mebla"}, {"command", guarded(app, std::function<void()>([this] { join(); }))}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}});
    contact = tk::StringVar(); (new ttk::Label(body_, {{"textvariable", contact}, {"padding", "0 4"}, {"wraplength", 880}}))->pack({{"fill", "x"}});
    (new ttk::Label(body_, {{"text", "W 3D wybierz „Układaj meble” i przeciągnij mebel. Shift przesuwa w pionie. Magnes dosuwa bliskie powierzchnie przy obrotach co 90°. Dowolny kąt ustawisz ręcznie. Obrót ±90° i dosuwanie tworzą układy L i U. Każdy mebel można ponownie przesunąć lub obrócić."}, {"wraplength", 880}, {"padding", "0 8"}}))->pack({{"fill", "x"}});
    row = new ttk::Frame(body_); row->pack({{"fill", "x"}});
    (new ttk::Checkbutton(row, {{"text", "Magnes w widoku 3D"}, {"variable", app->layout_magnet}}))->pack({{"side", "left"}});
    (new ttk::Label(row, {{"text", "Zasięg [mm]"}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Spinbox(row, {{"textvariable", app->layout_tolerance}, {"from", 1}, {"to", 200}, {"width", 8}}))->pack({{"side", "left"}});
    std::vector<std::string> names = unique_groups(*app->project); groups->configure({{"values", names}});
    std::string chosen;
    try { chosen = selected_group(app); } catch (const py::ValueError&) { chosen = names.empty() ? std::string() : names[0]; }
    groups->set(chosen); read();
}

std::string LayoutDocument::current() {
    ensure_current_project(true);
    std::string name = groups->get();
    bool found = false; for (auto& p : app->project->parts) if (p.group == name) { found = true; break; }
    if (!found) throw py::ValueError("Ten mebel został usunięty. Otwórz układ ponownie.");
    return name;
}

void LayoutDocument::read() {
    using namespace furniture_layout;
    std::vector<std::string> names = unique_groups(*app->project); groups->configure({{"values", names}});
    std::string name = groups->get();
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        name = names.empty() ? std::string() : names[0]; groups->set(name);
    }
    if (name.empty()) {
        info.set("Dodaj mebel, aby ustawić komplet."); target->set(""); target->configure({{"values", std::vector<std::string>{}}}); return;
    }
    Box bounds = group_bounds(*app->project, name); json layout = group_layout(*app->project, name);
    const core::Point3& lo = bounds.first; const core::Point3& hi = bounds.second;
    for (int i = 0; i < 3; ++i) for (auto& [key, var_] : vars) if (key == std::string(1, "xyz"[i])) var_.set(trimmed(lo[i]));
    for (auto& [key, var_] : vars) if (key == "yaw") var_.set(std::format("{:g}", layout_yaw(layout)));
    std::string dims;
    for (int i = 0; i < 3; ++i) dims += (i ? " × " : "") + std::format("{:g}", hi[i] - lo[i]);
    info.set(name + " · gabaryt w komplecie X × Y × Z: " + dims + " mm");
    std::vector<std::string> targets; for (auto& g : unique_groups(*app->project)) if (g != name) targets.push_back(g);
    target->configure({{"values", targets}});
    if (std::find(targets.begin(), targets.end(), target->get()) == targets.end()) target->set(targets.empty() ? std::string() : targets[0]);
    json links = contacts(*app->project, name);
    contact.set(std::format("Stykające się powierzchnie: {}. Dosunięcie ustala pozycję; nie generuje łączników między korpusami.", links.size()));
    mark_clean();
}

void LayoutDocument::apply() {
    using namespace furniture_layout;
    std::string group = current(); std::map<std::string, double> values;
    for (auto& [k, v] : vars) values[k] = app->number(v);
    ProjectPtr result = rotate_group(*app->project, group, values["yaw"] - layout_yaw(group_layout(*app->project, group)));
    Box lo_hi = group_bounds(*result, group); const core::Point3& lo = lo_hi.first;
    result = move_group(*result, group, values["x"] - lo[0], values["y"] - lo[1], values["z"] - lo[2]);
    commit(app, result); read();
}

void LayoutDocument::turn(double angle) {
    commit(app, furniture_layout::rotate_group(*app->project, current(), angle)); read();
}

void LayoutDocument::join() {
    std::string group = current(); std::string target_group = target->get();
    std::string direction = lookup(SIDES, side.get()); std::string alignment = lookup(ALIGN, align.get());
    ProjectPtr result = furniture_layout::snap_group(*app->project, group, target_group, direction, alignment, app->number(gap));
    commit(app, result, "Dosunięto mebel · pozycje i wymiary kompletu przeliczone"); read();
}

void LayoutDocument::show_design() {
    std::string group = current();
    std::optional<std::string> found;
    for (auto& p : app->project->parts) if (p.group == group) { found = p.id; break; }
    if (!found) throw py::StopIteration();
    app->selected = *found;
    app->edit_mode.set("layout"); app->refresh(); app->fit();
    tk::Widget* page = app->design_page;
    if (app->workspace->detached(page)) page->lift();
    else app->workspace->select(page);
}

void begin_drag(App* app, tk::Event& event) {
    bool open = false; for (auto& [k, v] : app->open_amounts) if (v != 0) { open = true; break; }
    if (app->explode.get() || open) {
        app->status.set("Wyłącz rozstrzelenie i zamknij mechanizmy przed układaniem mebli."); return;
    }
    std::optional<std::string> selected = studio_interaction::pick(app, event.x, event.y);
    if (!selected) return;
    bool exists = false; for (auto& p : app->project->parts) if (p.id == *selected) { exists = true; break; }
    if (!exists) return;
    app->selected = selected; core::Part* p = app->selected_part(); app->tree->selection_set(*selected); app->load_form();
    auto data = std::make_shared<LayoutDrag>();
    data->base = std::make_shared<core::Project>(*app->project); data->group = p->group; data->start = {event.x, event.y}; data->candidate = nullptr; data->last = 0;
    app->layout_drag = data;
    app->draw();
}

void drag(App* app, tk::Event& event, bool force) {
    std::shared_ptr<LayoutDrag> data = app->layout_drag;
    if (!data) return;
    double dx = event.x - data->start.first, dy = event.y - data->start.second;
    if (std::hypot(dx, dy) < 3 && data->candidate == nullptr) return;
    if (!force && monotonic() - data->last < .05) return;
    data->last = monotonic(); app->dragged = true;
    try {
        std::array<double, 3> movement = screen_move(app->camera, dx, dy, (event.state & 1) != 0);
        ProjectPtr result = furniture_layout::move_group(*data->base, data->group, movement[0], movement[1], movement[2], false); core::json info = nullptr;
        if (app->layout_magnet.get()) {
            auto snapped = furniture_layout::nearest_snap(*result, data->group, app->layout_tolerance.get(), true, false);
            result = snapped.first; info = snapped.second;
        }
        data->candidate = result; app->project = result; app->draw();
        app->status.set(py::truthy(info) ? std::string("Magnes: dosunięto powierzchnie")
                                         : "Przesunięcie X / Y / Z: " + std::format("{:+.1f}", movement[0]) + " / " + std::format("{:+.1f}", movement[1]) + " / " + std::format("{:+.1f}", movement[2]) + " mm · puść, aby zapisać");
    } catch (const py::ValueError& exc) {
        data->candidate = nullptr; app->project = data->base; app->draw(); app->status.set(exc.what());
    } catch (const tk::TclError& exc) {
        data->candidate = nullptr; app->project = data->base; app->draw(); app->status.set(exc.what());
    }
}

bool end_drag(App* app, tk::Event& event) {
    std::shared_ptr<LayoutDrag> data = app->layout_drag;
    if (!data) return false;
    drag(app, event, true); app->layout_drag = nullptr; app->project = data->base;
    if (data->candidate != nullptr) {
        try { data->candidate->validate(); commit(app, data->candidate, "Zapisano układ mebli · Ctrl+Z cofa cały ruch"); }
        catch (const py::ValueError& exc) { app->draw(); app->status.set(exc.what()); }
    } else {
        app->draw();
    }
    return true;
}

void cancel_drag(App* app) {
    if (app->layout_drag) {
        app->project = app->layout_drag->base; app->layout_drag = nullptr; app->refresh(); app->status.set("Anulowano przesuwanie mebla");
    }
}

void print_project(App* app) {
    if (!app->flush_parameter_drafts()) return;
    if (app->project->parts.empty()) throw py::ValueError("Dodaj meble do projektu przed wydrukiem.");
    std::string path = tk::filedialog::asksaveasfilename({{"parent", app->canvas->winfo_toplevel()}, {"defaultextension", ".html"}, {"initialfile", app->project->name + " — dokumentacja.html"},
                                                          {"filetypes", tk::FileTypes{{"Projekt do druku / PDF", "*.html"}}}});
    if (path.empty()) return;
    project_print::export_project_print(app->project, path);
    py::webbrowser_open(py::as_uri(fs::weakly_canonical(py::to_path(path)))); app->status.set("Dokumentacja gotowa · w przeglądarce wybierz Drukuj lub Zapisz jako PDF");
}

void install(App* app) {
    app->layout_drag = nullptr;
    app->layout_dialog = guarded(app, std::function<void()>([app] { new LayoutDocument(app); }));
    app->print_project = guarded(app, std::function<void()>([app] { print_project(app); }));
    auto turn = [app](double angle) {
        commit(app, furniture_layout::rotate_group(*app->project, selected_group(app), angle));
    };
    // app.rotate_furniture=guarded(app,turn) — wywołanie przyjmuje kąt.
    app->rotate_furniture = [app, turn](double angle) {
        guarded(app, std::function<void()>([turn, angle] { turn(angle); }))();
    };
    tk::Widget* bar = app->nametowidget(app->cget("menu")); auto* menu = new tk::Menu(app, {{"tearoff", false}});
    bar->call({bar->path(), "add", "cascade", "-label", "Układ i wydruk", "-menu", menu->path()});
    for (auto& [label, fn] : std::vector<std::pair<std::string, std::function<void()>>>{
             {"Ustawianie kompletu — pozycja, obrót, magnes…", app->layout_dialog},
             {"Obróć mebel −90°", [app] { app->rotate_furniture(-90); }},
             {"Obróć mebel +90°", [app] { app->rotate_furniture(90); }},
             {"Drukuj komplet i szafki z wymiarami…", app->print_project}}) menu->add_command({{"label", label}, {"command", fn}});
    app->canvas->bind("<Escape>", [app](tk::Event&) { cancel_drag(app); return std::string(); });
}

}  // namespace furniture_layout_ui
