// Persistent room/equipment panel; root mounts it beside the 3D inspector.
#include "room_scene_ui.h"

#include <algorithm>
#include <format>

#include "app.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "room_architecture.h"
#include "room_objects.h"

namespace room_scene_ui {

using core::Point3;

namespace {

// f'{value:g}' if isinstance(value,(float,int)) else value
std::string display(const json& value) {
    if (value.is_number() || value.is_boolean()) return std::format("{:g}", value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>());
    return value.is_string() ? value.get<std::string>() : py::str(value);
}

double parse_number(const std::string& text) { return py::to_float(py::replace_all(text, ",", ".")); }

struct SyncGuard {
    bool& flag;
    explicit SyncGuard(bool& f) : flag(f) { flag = true; }
    ~SyncGuard() { flag = false; }
};

}  // namespace

RoomScenePanel::RoomScenePanel(tk::Widget* parent, App* app_)
    : ttk::Frame(parent), app(app_), note("Zakupowe wyposażenie: własne wymiary, bez rozkroju płyt."), room(""), brightness("1"), led(true), lights(true), walls(false),
      cutaway(true), ceiling(false), catalogue(room_objects::CATALOG.at("washer").at("name").get<std::string>()), enabled(true), light_enabled(true), total("") {
    auto* top = new ttk::Frame(this, {{"padding", "10 6"}});
    top->pack({{"fill", "x"}});
    (new ttk::Label(top, {{"text", "POMIESZCZENIE I WYPOSAŻENIE"}, {"foreground", "#176a78"}, {"font", "{Segoe UI} 10 bold"}}))->pack({{"anchor", "w"}});
    std::vector<std::string> room_labels;
    for (auto& [key, label] : room_objects::ROOMS.items()) room_labels.push_back(label.get<std::string>());
    roomcombo = new ttk::Combobox(top, {{"textvariable", room}, {"values", room_labels}, {"state", "readonly"}});
    roomcombo->pack({{"fill", "x"}, {"pady", 4}});
    roomcombo->bind("<<ComboboxSelected>>", [this](tk::Event&) { apply_scene(); return std::string(); });
    auto* row = new ttk::Frame(top);
    row->pack({{"fill", "x"}});
    (new ttk::Label(row, {{"text", "Jasność (0,1–2)"}}))->pack({{"side", "left"}});
    auto* entry = new ttk::Entry(row, {{"textvariable", brightness}, {"width", 8}});
    entry->pack({{"side", "right"}});
    entry->bind("<Return>", [this](tk::Event&) { apply_scene(); return std::string(); });
    entry->bind("<FocusOut>", [this](tk::Event&) { apply_scene(); return std::string(); });
    row = new ttk::Frame(top);
    row->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Checkbutton(row, {{"text", "LED mebli"}, {"variable", led}, {"command", std::function<void()>([this] { apply_scene(); })}}))->pack({{"side", "left"}});
    (new ttk::Checkbutton(row, {{"text", "Lampy / żarówki"}, {"variable", lights}, {"command", std::function<void()>([this] { apply_scene(); })}}))->pack({{"side", "left"}, {"padx", 6}});
    (new ttk::Checkbutton(top, {{"text", "Pokaż podłogę i ściany"}, {"variable", walls}, {"command", std::function<void()>([this] { apply_room(); })}}))->pack({{"anchor", "w"}});
    auto* footer = new ttk::Frame(this, {{"padding", "10 6"}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    apply_button = new ttk::Button(footer, {{"text", "Zastosuj do obiektu"}, {"command", std::function<void()>([this] { apply_selected(); })}, {"style", "Accent.TButton"}});
    apply_button->pack({{"fill", "x"}});
    row = new ttk::Frame(footer);
    row->pack({{"fill", "x"}, {"pady", 4}});
    (new ttk::Button(row, {{"text", "−90°"}, {"command", std::function<void()>([this] { rotate_selected(-90); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Button(row, {{"text", "+90°"}, {"command", std::function<void()>([this] { rotate_selected(90); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Button(row, {{"text", "Usuń obiekt"}, {"command", std::function<void()>([this] { delete_selected(); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Label(footer, {{"textvariable", total}, {"font", "{Segoe UI} 9"}}))->pack({{"fill", "x"}});
    (new ttk::Label(footer, {{"textvariable", note}, {"wraplength", 295}, {"font", "{Segoe UI} 9"}}))->pack({{"fill", "x"}, {"pady", "3 0"}});
    canvas = new tk::Canvas(this, {{"bg", "#f6f8fa"}, {"highlightthickness", 0}, {"width", 320}});
    auto* scrollbar = new ttk::Scrollbar(this, {{"orient", "vertical"}, {"command", canvas->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    canvas->pack({{"fill", "both"}, {"expand", true}});
    canvas->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    auto* body = new ttk::Frame(canvas, {{"padding", "10 3"}});
    int window = canvas->create_window(0, 0, {{"window", body->path()}, {"anchor", "nw"}});
    body->bind("<Configure>", [this](tk::Event&) { responsive_ui::update_scrollregion(canvas); return std::string(); });
    canvas->bind("<Configure>", [this, window](tk::Event& e) { canvas->itemconfigure(window, {{"width", e.width}}); return std::string(); });
    auto* architecture = new ttk::LabelFrame(body, {{"text", "Wymiary pomieszczenia"}, {"padding", 5}});
    architecture->pack({{"fill", "x"}, {"pady", 6}});
    json defaults = room_architecture::defaults();
    for (auto [key, label] : std::vector<std::pair<std::string, std::string>>{{"width", "Szerokość X"}, {"depth", "Głębokość Y"}, {"height", "Wysokość Z"}, {"x", "Początek X"},
                                                                              {"y", "Początek Y"}, {"z", "Podłoga Z"}, {"wall_color", "Kolor ścian"}, {"floor_color", "Kolor podłogi"}}) {
        row = new ttk::Frame(architecture);
        row->pack({{"fill", "x"}, {"pady", 2}});
        (new ttk::Label(row, {{"text", label}}))->pack({{"side", "left"}});
        room_values.insert_or_assign(key, tk::StringVar(py::str(defaults.at(key))));
        entry = new ttk::Entry(row, {{"textvariable", room_values.at(key)}, {"width", 11}});
        entry->pack({{"side", "right"}});
        entry->bind("<Return>", [this](tk::Event&) { apply_room(); return std::string(); });
    }
    (new ttk::Checkbutton(architecture, {{"text", "Odsłaniaj ściany od strony kamery"}, {"variable", cutaway}, {"command", std::function<void()>([this] { apply_room(); })}}))->pack({{"anchor", "w"}});
    (new ttk::Checkbutton(architecture, {{"text", "Sufit"}, {"variable", ceiling}, {"command", std::function<void()>([this] { apply_room(); })}}))->pack({{"anchor", "w"}});
    row = new ttk::Frame(architecture);
    row->pack({{"fill", "x"}});
    (new ttk::Button(row, {{"text", "Zastosuj"}, {"command", std::function<void()>([this] { apply_room(); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Button(row, {{"text", "Pokaż całe pomieszczenie"}, {"command", std::function<void()>([this] { fit_room(); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Label(body, {{"text", "Wyposażenie przeciągniesz w 3D w trybie Układaj. Shift: wysokość. Środkowy przycisk: obrót widoku."}, {"wraplength", 285}, {"foreground", "#176a78"}}))->pack({{"fill", "x"}, {"pady", 5}});
    (new ttk::Label(body, {{"text", "Dodaj z katalogu"}, {"font", "{Segoe UI} 9 bold"}}))->pack({{"anchor", "w"}});
    std::vector<std::string> catalogue_names;
    for (auto& [key, spec] : room_objects::CATALOG.items()) catalogue_names.push_back(spec.at("name").get<std::string>());
    cataloguecombo = new ttk::Combobox(body, {{"textvariable", catalogue}, {"values", catalogue_names}, {"state", "readonly"}});
    cataloguecombo->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Button(body, {{"text", "+ Dodaj wyposażenie"}, {"command", std::function<void()>([this] { add_selected(); })}}))->pack({{"fill", "x"}, {"pady", 3}});
    auto* treeframe = new ttk::Frame(body);
    treeframe->pack({{"fill", "x"}, {"pady", 5}});
    tree = new ttk::Treeview(treeframe, {{"show", "tree"}, {"selectmode", "browse"}, {"height", 5}});
    tree->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    auto* sb = new ttk::Scrollbar(treeframe, {{"orient", "vertical"}, {"command", tree->path() + " yview"}});
    sb->pack({{"side", "right"}, {"fill", "y"}});
    tree->configure({{"yscrollcommand", sb->path() + " set"}});
    tree->bind("<<TreeviewSelect>>", [this](tk::Event&) { tree_select(); return std::string(); });
    tree->bind("<Delete>", [this](tk::Event&) { delete_selected(); return std::string(); });
    for (auto [key, label] : std::vector<std::pair<std::string, std::string>>{{"name", "Nazwa"}, {"width", "Szerokość [mm]"}, {"depth", "Głębokość [mm]"}, {"height", "Wysokość [mm]"},
                                                                              {"x", "Pozycja X [mm]"}, {"y", "Pozycja Y [mm]"}, {"z", "Pozycja Z [mm]"}, {"yaw", "Obrót Z [°]"},
                                                                              {"color", "Kolor #RRGGBB"}, {"price", "Cena zakupu [zł]"}}) {
        row = new ttk::Frame(body);
        row->pack({{"fill", "x"}, {"pady", 2}});
        (new ttk::Label(row, {{"text", label}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "left"}});
        values.insert_or_assign(key, tk::StringVar());
        entry = new ttk::Entry(row, {{"textvariable", values.at(key)}, {"width", key == "name" ? 20 : 11}});
        entry->pack({{"side", "right"}});
        entry->bind("<Return>", [this](tk::Event&) { apply_selected(); return std::string(); });
    }
    (new ttk::Checkbutton(body, {{"text", "Widoczny w scenie"}, {"variable", enabled}}))->pack({{"anchor", "w"}, {"pady", 4}});
    (new ttk::Checkbutton(body, {{"text", "Światło obiektu (lampa / żarówka)"}, {"variable", light_enabled}}))->pack({{"anchor", "w"}});
    (new ttk::Label(body, {{"text", "Gabaryty są ustawieniem własnym. Nie tworzą otworów w meblach; dobierz artykuł i miejsce montażu."}, {"wraplength", 285},
                           {"font", "{Segoe UI} 9"}, {"foreground", "#68788a"}}))->pack({{"fill", "x"}, {"pady", 5}});
    auto wheel = [this](tk::Event& event) { canvas->yview_scroll(event.delta > 0 ? -1 : 1, "units"); return std::string("break"); };
    std::function<void(tk::Widget*)> bind_wheel = [&](tk::Widget* widget) {
        widget->bind("<MouseWheel>", wheel, true);
        for (auto* child : widget->winfo_children()) bind_wheel(child);
    };
    bind_wheel(body);
    refresh();
}

bool RoomScenePanel::_error(const std::string& message) {
    note.set(message);
    app->status.set("Wyposażenie: " + message);
    return false;
}

bool RoomScenePanel::_commit(core::ProjectPtr candidate, const std::optional<std::string>& selection) {
    if (candidate->data() == app->project->data()) return true;
    candidate->validate();
    app->snapshot();
    app->project = candidate;
    if (selection) {
        app->selected_scene = *selection;
        app->selected.reset();
    }
    app->refresh();
    refresh();
    app->draw();
    return true;
}

void RoomScenePanel::refresh() {
    SyncGuard guard(syncing);
    json scene = room_objects::scene_of(*app->project);
    room.set(room_objects::ROOMS.at(scene.at("room").get<std::string>()).get<std::string>());
    brightness.set(display(scene.at("brightness")));
    led.set(py::truthy(scene.at("led_enabled")));
    lights.set(py::truthy(scene.at("lights_enabled")));
    const json& architecture = scene.at("architecture");
    walls.set(py::truthy(architecture.at("enabled")));
    cutaway.set(py::truthy(architecture.at("cutaway")));
    ceiling.set(py::truthy(architecture.at("ceiling")));
    for (auto& [key, var] : room_values) var.set(display(architecture.at(key)));
    tree->delete_(tree->get_children());
    for (const auto& obj : scene.at("objects"))
        tree->insert("", "end", {{"iid", obj.at("id").get<std::string>()}, {"text", std::string(py::truthy(obj.at("enabled")) ? "◉ " : "○ ") + obj.at("name").get<std::string>()}});
    std::optional<std::string> current = app->selected_scene;
    if (current && current->starts_with("scene:")) current = current->substr(6);
    selected.reset();
    const json* found = nullptr;
    for (const auto& obj : scene.at("objects")) if (current && obj.at("id") == *current) { found = &obj; break; }
    if (found != nullptr) selected = current;
    app->selected_scene = selected;
    for (auto& [key, var] : values) var.set(found ? display(found->at(key)) : std::string());
    enabled.set(found ? py::truthy(found->at("enabled")) : true);
    light_enabled.set(found ? py::truthy(found->at("light_enabled")) : true);
    if (found != nullptr) {
        std::string id = found->at("id");
        tree->selection_set(id);
        tree->see(id);
    }
    apply_button->configure({{"state", found != nullptr ? "normal" : "disabled"}});
    total.set(std::format("Zakupowe obiekty: {} · {:.2f} zł", scene.at("objects").size(), room_objects::purchased_total(*app->project)));
}

bool RoomScenePanel::select_object(const std::string& object_id_in) {
    std::string object_id = object_id_in.starts_with("scene:") ? object_id_in.substr(6) : object_id_in;
    bool exists = false;
    for (const auto& obj : room_objects::scene_of(*app->project).at("objects")) if (obj.at("id") == object_id) { exists = true; break; }
    if (!exists) return _error("Wybierz istniejący obiekt wyposażenia.");
    app->selected_scene = object_id;
    app->selected.reset();
    refresh();
    app->draw();
    return true;
}

void RoomScenePanel::tree_select() {
    if (syncing) return;
    auto chosen = tree->selection();
    if (!chosen.empty() && (!selected || chosen[0] != *selected)) select_object(chosen[0]);
}

bool RoomScenePanel::apply_scene() {
    if (syncing) return false;
    try {
        std::string mode;
        bool found = false;
        for (auto& [key, label] : room_objects::ROOMS.items())
            if (label == room.get()) { mode = key; found = true; break; }
        if (!found) throw py::StopIteration();
        double value = parse_number(brightness.get());
        auto result = room_objects::set_scene(*app->project, json{{"room", mode}, {"brightness", value}, {"led_enabled", led.get()}, {"lights_enabled", lights.get()}});
        return _commit(result);
    } catch (const py::StopIteration&) {
        return _error("");
    } catch (const py::ValueError& error) {
        return _error(error.what());
    } catch (const json::exception& error) {
        return _error(error.what());
    } catch (const std::out_of_range& error) {
        return _error(error.what());
    }
}

std::optional<std::string> RoomScenePanel::add_selected(const std::optional<std::string>& kind_in) {
    try {
        std::string kind;
        if (kind_in && !kind_in->empty()) kind = *kind_in;
        else {
            bool found = false;
            for (auto& [key, spec] : room_objects::CATALOG.items())
                if (spec.at("name") == catalogue.get()) { kind = key; found = true; break; }
            if (!found) throw py::StopIteration();
        }
        // Keep newly inserted standalone equipment beside the current scene.
        std::vector<Point3> points = room_objects::object_vertices(*app->project);
        for (const auto& p : app->project->parts)
            for (const auto& uv : p.points())
                for (double w : {0.0, p.t}) points.push_back(core::world(p, uv[0], uv[1], w));
        double x = -50;
        if (!points.empty()) {
            x = points[0][0];
            for (const auto& p : points) x = std::max(x, p[0]);
        }
        x += 50;
        auto result = room_objects::add_object(*app->project, kind, json{{"x", x}});
        std::string ident = result->scene.at("objects").back().at("id");
        _commit(result, ident);
        app->edit_mode.set("layout");
        note.set("Dodano wyposażenie. Przeciągnij obiekt w 3D; Shift zmienia wysokość.");
        return ident;
    } catch (const py::StopIteration&) {
        _error("");
    } catch (const py::ValueError& error) {
        _error(error.what());
    } catch (const json::exception& error) {
        _error(error.what());
    } catch (const std::out_of_range& error) {
        _error(error.what());
    }
    return std::nullopt;
}

bool RoomScenePanel::apply_selected() {
    if (syncing || !selected) return false;
    try {
        json changes = json::object();
        for (auto& [key, var] : values) changes[key] = py::strip(var.get());
        for (const char* key : {"width", "depth", "height", "x", "y", "z", "yaw", "price"}) changes[key] = parse_number(changes.at(key).get<std::string>());
        changes["enabled"] = enabled.get();
        changes["light_enabled"] = light_enabled.get();
        auto result = room_objects::update_object(*app->project, *selected, changes);
        _commit(result);
        note.set("Zaktualizowano zakupowy obiekt. Formatki mebli pozostały zachowane.");
        return true;
    } catch (const py::ValueError& error) {
        return _error(error.what());
    } catch (const json::exception& error) {
        return _error(error.what());
    } catch (const std::out_of_range& error) {
        return _error(error.what());
    }
}

bool RoomScenePanel::rotate_selected(double angle) {
    if (!selected) return false;
    try {
        json scene = room_objects::scene_of(*app->project);
        const json* obj = nullptr;
        for (const auto& o : scene.at("objects")) if (o.at("id") == *selected) { obj = &o; break; }
        if (obj == nullptr) throw py::StopIteration();
        return _commit(room_objects::update_object(*app->project, *selected, json{{"yaw", obj->at("yaw").get<double>() + angle}}));
    } catch (const py::StopIteration&) {
        return _error("");
    } catch (const py::ValueError& error) {
        return _error(error.what());
    } catch (const json::exception& error) {
        return _error(error.what());
    } catch (const std::out_of_range& error) {
        return _error(error.what());
    }
}

bool RoomScenePanel::delete_selected() {
    if (!selected) return false;
    try {
        auto result = room_objects::delete_object(*app->project, *selected);
        app->selected_scene.reset();
        return _commit(result);
    } catch (const py::ValueError& error) {
        return _error(error.what());
    } catch (const json::exception& error) {
        return _error(error.what());
    } catch (const std::out_of_range& error) {
        return _error(error.what());
    }
}

bool RoomScenePanel::apply_room() {
    if (syncing) return false;
    try {
        json architecture = json::object();
        for (auto& [key, var] : room_values) architecture[key] = py::strip(var.get());
        for (const char* key : {"width", "depth", "height", "x", "y", "z"}) architecture[key] = parse_number(architecture.at(key).get<std::string>());
        architecture["enabled"] = walls.get();
        architecture["cutaway"] = cutaway.get();
        architecture["ceiling"] = ceiling.get();
        return _commit(room_objects::set_scene(*app->project, json{{"architecture", architecture}}));
    } catch (const py::ValueError& error) {
        return _error(error.what());
    } catch (const json::exception& error) {
        return _error(error.what());
    } catch (const std::out_of_range& error) {
        return _error(error.what());
    }
}

void RoomScenePanel::fit_room() {
    std::vector<Point3> points = room_architecture::vertices(*app->project);
    if (points.empty()) { app->fit(); return; }
    Point3 lo = points[0], hi = points[0];
    for (const auto& p : points)
        for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    app->camera.center = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    app->camera.pan = {0, 0};
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    bool first = true;
    for (const auto& p : points) {
        Point3 q = app->camera.view(p);
        if (first) { minx = maxx = q[0]; miny = maxy = q[1]; first = false; }
        else { minx = std::min(minx, q[0]); maxx = std::max(maxx, q[0]); miny = std::min(miny, q[1]); maxy = std::max(maxy, q[1]); }
    }
    double width = maxx - minx, height = maxy - miny;
    app->camera.scale = std::min((app->canvas->winfo_width() - 80) / std::max(width, 1.0), (app->canvas->winfo_height() - 80) / std::max(height, 1.0));
    app->draw();
}

}  // namespace room_scene_ui
