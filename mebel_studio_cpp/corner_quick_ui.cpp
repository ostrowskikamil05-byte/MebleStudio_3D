// Compact persistent corner inspector; no extra window or modal grab.
#include "corner_quick_ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "app.h"
#include "corner_furniture.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "pyutil.h"
#include "quick_studio_ui.h"

namespace corner_quick_ui {

using namespace corner_furniture;

namespace {
// dict[key] — KeyError z komunikatem jak str(KeyError(key)).
const json& at(const json& d, const std::string& key) {
    if (!d.is_object() || !d.contains(key)) throw py::KeyError("'" + key + "'");
    return d[key];
}
}  // namespace

CornerPanel::CornerPanel(tk::Widget* parent, App* app_) : ttk::Frame(parent, {{"padding", 8}}), app(app_) {
    group.reset(); config = json::object(); syncing = false; job.reset(); generation = app->project_generation; fields.clear(); mapping = json::object(); signature.reset(); has_draft = false;
    (new ttk::Label(this, {{"text", "NAROŻNIK"}, {"font", "{Segoe UI} 10 bold"}, {"foreground", "#4e6b3f"}}))->pack({{"anchor", "w"}});
    note = tk::StringVar("Wybierz szafkę do konwersji lub dodaj nowy narożnik.");
    auto* footer = new ttk::Frame(this); footer->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* actions = new ttk::Frame(footer); actions->pack({{"fill", "x"}, {"pady", 5}});
    (new ttk::Button(actions, {{"text", "Zamień wybrany"}, {"command", std::function<void()>([this] { convert_selected(); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    (new ttk::Button(actions, {{"text", "+ Nowy"}, {"command", std::function<void()>([this] { new_corner(); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    apply_button = new ttk::Button(footer, {{"text", "Zastosuj narożnik"}, {"command", std::function<void()>([this] { apply(); })}}); apply_button->pack({{"fill", "x"}});
    (new ttk::Label(footer, {{"textvariable", note}, {"wraplength", 280}, {"font", "{Segoe UI} 9"}}))->pack({{"fill", "x"}, {"pady", 4}});
    scroll = new quick_studio_ui::ScrollPanel(this); scroll->pack({{"fill", "both"}, {"expand", true}}); body = scroll->body;
    preview = new tk::Canvas(body, {{"height", 104}, {"bg", "#edf1f4"}, {"highlightthickness", 0}}); preview->pack({{"fill", "x"}, {"pady", 4}});
    preview->bind("<Configure>", [this](tk::Event&) { draw_preview(); return std::string(); });
    auto* row = new ttk::Frame(body); row->pack({{"fill", "x"}});
    for (auto& [label, shape] : std::vector<std::pair<std::string, std::string>>{{"L", "L"}, {"Ścięty", "chamfer"}, {"Okrągły", "rounded"}}) {
        std::string s = shape;
        (new ttk::Button(row, {{"text", label}, {"command", std::function<void()>([this, s] { quick_shape(s); })}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    }
    combo("furniture_type", "Typ", json{{"Szafka", "cabinet"}, {"Szafa", "wardrobe"}});
    combo("side", "Kierunek", json{{"Lewy", "left"}, {"Prawy", "right"}});
    combo("fronts", "Fronty", json{{"Otwarte", "open"}, {"Dwa niezależne", "orthogonal"}, {"Skośny bez okucia", "diagonal"}});
    for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"width", "Szerokość X"}, {"depth", "Głębokość Y"}, {"height", "Wysokość całkowita"}, {"arm_depth", "Ramię X — głębokość"}, {"arm_depth_y", "Ramię Y — szerokość"}, {"radius", "Promień / długość ścięcia"}, {"diagonal_angle", "Kąt ścięcia [°]"}, {"shelves", "Liczba półek"}, {"thickness", "Grubość korpusu"}, {"front_thickness", "Grubość frontu"}, {"leg_height", "Wysokość nóg"}}) field(key, label);
    combo("top_mode", "Góra", json{{"Pełna płyta", "panel"}, {"Listwy pod wspólny blat", "rails"}});
    combo("front_style", "Styl frontu", json{{"Gładki", "smooth"}, {"Ramka", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}});
    bind("<Destroy>", [this](tk::Event& e) { destroyed(e); return std::string(); }, true); set_config(default_corner_config(app->project.get())); refresh();
}

tk::StringVar& CornerPanel::var(const std::string& key) {
    for (auto& [name, value] : fields) if (name == key) return value;
    throw py::KeyError("'" + key + "'");
}

std::string CornerPanel::label_for(const std::string& key, const json& value) {
    // next(k for k, v in self.mapping[key].items() if v == value)
    for (auto& [k, v] : mapping[key].items()) if (v == value) return k;
    throw py::StopIteration();
}

void CornerPanel::field(const std::string& key, const std::string& label) {
    auto* row = new ttk::Frame(body); row->pack({{"fill", "x"}, {"pady", 1}}); (new ttk::Label(row, {{"text", label}}))->pack({{"side", "left"}});
    tk::StringVar var_; auto* entry = new ttk::Entry(row, {{"textvariable", var_}, {"width", 9}}); entry->pack({{"side", "right"}}); fields.emplace_back(key, var_);
    var_.trace_add("write", [this] { changed(); }); entry->bind("<Return>", [this](tk::Event&) { apply(); return std::string(); }); entry->bind("<FocusOut>", [this](tk::Event&) { changed(); return std::string(); });
}

void CornerPanel::combo(const std::string& key, const std::string& label, const json& choices) {
    auto* row = new ttk::Frame(body); row->pack({{"fill", "x"}, {"pady", 2}}); (new ttk::Label(row, {{"text", label}}))->pack({{"side", "left"}});
    std::vector<std::string> labels; for (auto& [k, v] : choices.items()) labels.push_back(k);
    tk::StringVar var_; auto* box = new ttk::Combobox(row, {{"textvariable", var_}, {"values", labels}, {"state", "readonly"}, {"width", 19}}); box->pack({{"side", "right"}}); fields.emplace_back(key, var_); mapping[key] = choices;
    box->bind("<<ComboboxSelected>>", [this](tk::Event&) { changed(); return std::string(); });
}

void CornerPanel::set_config(const json& config_) {
    syncing = true; config = config_; has_draft = false;
    {
    struct Finally { bool& flag; ~Finally() { flag = false; } } finally{syncing};
    for (auto& [key, var_] : fields) {
        json value = at(config_, key);
        std::string text;
        if (mapping.contains(key)) text = label_for(key, value);
        else if (value.is_number() || value.is_boolean()) text = std::format("{:g}", py::to_float(value));
        else text = py::str(value);
        var_.set(text);
    }
    }  // finally: self.syncing = False
    draw_preview();
}

json CornerPanel::draft() {
    json config_ = config;
    for (auto& [key, var_] : fields) {
        std::string text = py::strip(var_.get());
        if (mapping.contains(key)) config_[key] = at(mapping[key], text);
        else {
            try {
                config_[key] = py::to_float(py::replace_all(text, ",", "."));
            } catch (const py::ValueError&) {
                throw py::ValueError("Wpisz liczbę: " + key);
            }
        }
    }
    return config_;
}

void CornerPanel::cancel_pending() {
    if (job) {
        try {
            after_cancel(*job);
        } catch (const tk::TclError&) {
        }
    }
    job.reset();
}

void CornerPanel::destroyed(tk::Event& event) {
    if (event.widget == this) cancel_pending();
}

void CornerPanel::changed() {
    if (syncing) return;
    has_draft = (bool)group;
    cancel_pending(); draw_preview();
    if (group) job = after(450, [this] { apply(); });
}

void CornerPanel::quick_shape(const std::string& shape) {
    config["shape"] = shape;
    std::string fronts = shape == "L" ? "orthogonal" : shape == "chamfer" ? "diagonal" : "open";
    var("fronts").set(label_for("fronts", fronts)); changed();
}

void CornerPanel::draw_preview() {
    if (preview == nullptr) return;
    preview->delete_("all");
    try {
        json c = draft();
        auto points = corner_outline(py::to_float(at(c, "width")), py::to_float(at(c, "depth")), py::to_float(at(c, "arm_depth")), at(c, "shape").get<std::string>(),
                                     py::to_float(at(c, "radius")), at(c, "side").get<std::string>(), 12, py::to_float(at(c, "arm_depth_y")), py::to_float(at(c, "diagonal_angle")));
        double width = py::to_float(c["width"]), depth = py::to_float(c["depth"]);
        if (width == 0 || depth == 0) return;  // ZeroDivisionError
        double w = std::max(100, preview->winfo_width()); double scale = std::min((w - 18) / width, 86 / depth);
        std::vector<double> coords; for (auto& [x, y] : points) { coords.push_back(9 + x * scale); coords.push_back(9 + y * scale); }
        preview->create_polygon(coords, {{"fill", "#a9be96"}, {"outline", "#4e6b3f"}, {"width", 2}});
    } catch (const py::ValueError&) {
    } catch (const py::KeyError&) {
    }
}

void CornerPanel::refresh() {
    core::Part* part = app->selected_part(); std::optional<std::string> group_ = part ? std::optional<std::string>(part->group) : std::nullopt;
    bool has_group = group_ && !group_->empty();
    json cfg = has_group ? read_corner_config(*app->project, group_) : json(nullptr);
    json regular = has_group && !py::truthy(cfg) ? furniture_configurator::read_config(*app->project, *group_) : json(nullptr);
    std::string marker = py::dumps(json::array({app->project_generation, group_ ? json(*group_) : json(nullptr), cfg, regular}));
    if (marker == signature) return;
    cancel_pending(); signature = marker; generation = app->project_generation; group = py::truthy(cfg) ? group_ : std::nullopt;
    if (py::truthy(cfg)) { set_config(cfg); note.set(py::str(corner_status(*app->project, *group_)["reason"])); }
    else {
        if (py::truthy(regular)) {
            double arm = py::to_float(at(regular, "depth")); json suggested = default_corner_config(app->project.get(), py::to_float(at(regular, "height")) >= 1500 ? "wardrobe" : "cabinet");
            suggested["width"] = std::max(py::to_float(at(regular, "width")), arm + 340); suggested["depth"] = arm + 340;
            suggested["height"] = py::to_float(at(regular, "height")) - (py::truthy(py::get(regular, "worktop")) ? py::to_float(at(regular, "worktop_thickness")) : 0);
            suggested["arm_depth"] = arm; suggested["arm_depth_y"] = arm; suggested["fronts"] = "orthogonal";
            for (auto key : {"leg_height", "front_thickness", "thickness", "material", "front_material"}) suggested[key] = at(regular, key);
            set_config(suggested);
        }
        note.set("Konwersja użyje widocznych wymiarów narożnika. Ręczne obróbki i okucia są chronione.");
    }
    apply_button->configure({{"state", py::truthy(cfg) && py::truthy(corner_status(*app->project, *group_)["editable"]) ? "normal" : "disabled"}});
}

bool CornerPanel::commit(core::ProjectPtr result, const std::string& group_) {
    if (result->data() == app->project->data()) return true;
    std::optional<std::string> oldselected = app->selected; app->snapshot(); app->project = result;
    bool kept = false; for (auto& p : result->parts) if (oldselected && p.id == *oldselected) kept = true;
    if (kept) app->selected = oldselected;
    else {
        auto found = std::find_if(result->parts.begin(), result->parts.end(), [&](const core::Part& p) { return p.group == group_; });
        if (found == result->parts.end()) throw py::StopIteration();
        app->selected = found->id;
    }
    app->open_amounts = {};
    app->motion_value.set(0);
    app->refresh(); refresh(); app->status.set("Przeliczono narożnik: " + group_); return true;
}

bool CornerPanel::apply() {
    cancel_pending();
    if (syncing || !group || group->empty()) return false;
    try {
        if (generation != app->project_generation) throw py::ValueError("Projekt zmienił się. Wybierz narożnik ponownie.");
        json c = normalize_corner_config(draft(), app->project.get()); json previous = read_corner_config(*app->project, group);
        if (c == normalize_corner_config(previous, app->project.get())) { has_draft = false; return true; }
        auto [result, group_] = create_corner(*app->project, c, group); return commit(result, group_);
    } catch (const py::ValueError& e) { note.set(e.what()); return false; }
    catch (const py::KeyError& e) { note.set(e.what()); return false; }
    catch (const json::out_of_range& e) { note.set(e.what()); return false; }
    catch (const py::StopIteration&) { note.set(""); return false; }  // str(StopIteration()) == ''
}

bool CornerPanel::flush_draft() {
    cancel_pending();
    return has_draft && group && !group->empty() ? apply() : true;
}

bool CornerPanel::convert_selected() {
    cancel_pending(); core::Part* part = app->selected_part();
    if (!part) { note.set("Wybierz szafkę albo szafę."); return false; }
    try {
        json c = draft();
        json options = json::object();
        for (auto& [key, _] : fields) {
            if (key != "side" && key != "fronts" && key != "width" && key != "depth" && key != "arm_depth") options[key] = at(c, key);
        }
        options["shape"] = at(c, "shape"); options["material"] = at(c, "material"); options["front_material"] = at(c, "front_material");
        std::string shape = options["shape"].get<std::string>(); options.erase("shape");
        auto [result, group_] = convert_to_corner(*app->project, part->group, shape, at(c, "side").get<std::string>(), at(c, "arm_depth"), at(c, "width"), at(c, "depth"),
                                                  at(c, "fronts").get<std::string>(), options);
        return commit(result, group_);
    } catch (const py::ValueError& e) { note.set(e.what()); return false; }
    catch (const py::KeyError& e) { note.set(e.what()); return false; }
    catch (const json::out_of_range& e) { note.set(e.what()); return false; }
    catch (const py::StopIteration&) { note.set(""); return false; }
}

bool CornerPanel::new_corner() {
    cancel_pending();
    try {
        json c = draft(); c["name"] = at(c, "furniture_type") == "wardrobe" ? "Szafa narożna" : "Korpus narożny"; c["origin"] = json::array({0, 0, 0});
        auto [result, group_] = create_corner(*app->project, c);
        if (!app->project->parts.empty()) {
            std::set<std::string> groups; for (auto& p : app->project->parts) groups.insert(p.group);
            double right = -INFINITY;
            for (auto& g : groups) right = std::max(right, furniture_layout::group_bounds(*app->project, g).second[0]);
            auto [lo, _] = furniture_layout::group_bounds(*result, group_); result = furniture_layout::move_group(*result, group_, right + 50 - lo[0]);
        }
        return commit(result, group_);
    } catch (const py::ValueError& e) { note.set(e.what()); return false; }
    catch (const py::KeyError& e) { note.set(e.what()); return false; }
    catch (const json::out_of_range& e) { note.set(e.what()); return false; }
    catch (const py::StopIteration&) { note.set(""); return false; }
}

}  // namespace corner_quick_ui
