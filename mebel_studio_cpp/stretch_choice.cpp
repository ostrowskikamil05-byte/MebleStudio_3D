#include "stretch_choice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "app.h"
#include "corner_furniture.h"
#include "forma_theme.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "opening_options.h"

namespace stretch_choice {

using core::json;

namespace {

constexpr double MIN_CHANGE = 150;  // mm; mniejsze rozciągnięcie to zwykła zmiana wymiaru, bez pytania
constexpr double TILE_W = 118, TILE_H = 104;

double num(const json& v, double fallback = 0) { return v.is_number() ? v.get<double>() : fallback; }

json section_of(json& config, const std::string& key) {
    for (auto& s : config["sections"]) if (s.value("key", std::string()) == key) return s;
    return json::object();
}

void set_section(json& config, const std::string& key, const json& changes) {
    for (auto& s : config["sections"]) {
        if (s.value("key", std::string()) != key) continue;
        for (auto& [k, v] : changes.items()) s[k] = v;
    }
}

// ---------------------------------------------------------------- ustawienia z okienka

// Wartości z dolnej części okienka (uchwyty, nóżki, zawiasy, materiały, dodatki). Przekazywane są tylko te, które zmieniono względem mebla.
struct Overrides {
    json values = json::object();
    bool apply_first = true;  // czy zmiany obejmują też rozciągniętą szafkę
};

void merge_overrides(json& cfg, const json& values) {
    for (auto& [k, v] : values.items()) {
        if (k == "leg_height") {
            double old_leg = cfg.value("leg_height", 0.0), new_leg = num(v);
            cfg["height"] = num(cfg["height"]) - old_leg + new_leg;
            cfg["leg_height"] = new_leg;
        } else {
            cfg[k] = v;
        }
    }
    if (cfg.value("leg_height", 0.0) <= 0) cfg["plinth"] = false;  // cokół wymaga nóżek
}

struct ChoiceRow {
    std::string key, label;
    json choices;  // etykieta -> wartość
    tk::StringVar var;
    json initial;
};

struct CheckRow {
    std::string key, label;
    tk::BooleanVar var;
    bool initial = false;
};

struct Settings {
    std::vector<ChoiceRow> choices;
    std::vector<CheckRow> checks;
    tk::BooleanVar apply_first;

    Overrides collect() const {
        Overrides out;
        out.apply_first = apply_first.get();
        for (const auto& row : choices) {
            std::string label = row.var.get();
            if (!row.choices.contains(label)) continue;
            const json& value = row.choices.at(label);
            if (value != row.initial) out.values[row.key] = value;
        }
        for (const auto& row : checks) {
            bool now = row.var.get();
            if (now != row.initial) out.values[row.key] = now;
        }
        return out;
    }
};

std::vector<std::string> keys_of(const json& choices) {
    std::vector<std::string> out;
    for (auto& [k, v] : choices.items()) out.push_back(k);
    return out;
}

std::shared_ptr<Settings> build_settings(App* app, tk::Widget* parent, const json& cfg) {
    auto settings = std::make_shared<Settings>();
    json openings = {{"Uchwyt / gałka", "handle"}, {"Bez uchwytu", "none"}};
    for (auto& [k, v] : opening_options::OPTIONS.items()) if (k != "none") openings[v.at("label").get<std::string>()] = k;
    json materials = json::object();
    for (auto& m : app->project->materials) materials[m.name] = m.id;
    struct Spec { const char* key; const char* label; json choices; };
    std::vector<Spec> specs = {
        {"material", "Korpus — materiał", materials},
        {"front_material", "Fronty — materiał", materials},
        {"front_style", "Fronty — styl", json{{"Gładki", "smooth"}, {"Ramka frezowana", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}}},
        {"handle_mode", "Uchwyty — rodzaj", openings},
        {"handle_color", "Uchwyty — kolor", json{{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Złoty", "#bb9656"}, {"Biały", "#efefea"}}},
        {"leg_height", "Nóżki", json{{"Bez nóżek", 0}, {"60 mm", 60}, {"100 mm", 100}, {"150 mm", 150}}},
        {"hinge_pattern", "Zawiasy", json{{"Puszka 35 / rozstaw 45", "hinge45"}, {"Puszka 35 / rozstaw 52", "hinge52"}, {"Hettich Sensys", "hinge_sensys52"}}},
    };
    auto* box = new ttk::LabelFrame(parent, {{"text", "Ustawienia nowych szafek"}, {"padding", 8}});
    box->pack({{"fill", "x"}, {"pady", "10 0"}});
    int index = 0;
    for (auto& spec : specs) {
        if (spec.choices.empty()) continue;
        json initial = cfg.contains(spec.key) ? cfg.at(spec.key) : json(nullptr);
        std::string initial_label = spec.choices.items().begin().key();
        for (auto& [label, value] : spec.choices.items()) if (value == initial) { initial_label = label; break; }
        json initial_value = spec.choices.at(initial_label);
        settings->choices.push_back({spec.key, spec.label, spec.choices, tk::StringVar(initial_label), initial_value});
        ChoiceRow& row = settings->choices.back();
        int r = index / 2, c = (index % 2) * 2;
        (new ttk::Label(box, {{"text", row.label}, {"font", "{Segoe UI} 9"}}))->grid({{"row", r}, {"column", c}, {"sticky", "w"}, {"padx", "0 6"}, {"pady", 2}});
        (new ttk::Combobox(box, {{"textvariable", row.var}, {"values", keys_of(row.choices)}, {"state", "readonly"}, {"width", 18}}))
            ->grid({{"row", r}, {"column", c + 1}, {"sticky", "ew"}, {"padx", "0 14"}, {"pady", 2}});
        ++index;
    }
    box->columnconfigure(1, {{"weight", 1}});
    box->columnconfigure(3, {{"weight", 1}});
    auto* extras = new ttk::Frame(box);
    extras->grid({{"row", (index + 1) / 2}, {"column", 0}, {"columnspan", 4}, {"sticky", "w"}, {"pady", "6 0"}});
    for (auto [key, label] : std::vector<std::pair<std::string, std::string>>{{"plinth", "Cokół"}, {"worktop", "Blat"}, {"led", "LED"}}) {
        settings->checks.push_back({key, label, tk::BooleanVar(cfg.contains(key) && cfg.at(key).is_boolean() && cfg.at(key).get<bool>()), cfg.contains(key) && cfg.at(key).is_boolean() && cfg.at(key).get<bool>()});
        (new ttk::Checkbutton(extras, {{"text", label}, {"variable", settings->checks.back().var}}))->pack({{"side", "left"}, {"padx", "0 14"}});
    }
    settings->apply_first = tk::BooleanVar(true);
    (new ttk::Checkbutton(box, {{"text", "Zastosuj zmiany też do rozciągniętej szafki"}, {"variable", settings->apply_first}}))
        ->grid({{"row", (index + 1) / 2 + 1}, {"column", 0}, {"columnspan", 4}, {"sticky", "w"}, {"pady", "4 0"}});
    return settings;
}

// ---------------------------------------------------------------- geometria układów

using Dir2 = std::array<double, 2>;

std::optional<Dir2> axis_dir(double dx, double dy) {
    if (std::hypot(dx, dy) < 1) return std::nullopt;
    if (std::abs(dx) > std::abs(dy)) return Dir2{dx > 0 ? 1.0 : -1.0, 0.0};
    return Dir2{0.0, dy > 0 ? 1.0 : -1.0};
}

// Kierunek, w którym patrzą fronty mebla (od pleców do środka albo od środka do drzwi), jako oś pokoju.
std::optional<Dir2> front_dir(const core::Project& project, const std::string& group) {
    auto box = furniture_layout::group_bounds(project, group);
    double cx = (box.first[0] + box.second[0]) / 2, cy = (box.first[1] + box.second[1]) / 2;
    for (const auto& p : project.parts) {
        if (p.group != group || p.role != "back") continue;
        auto w = core::world(p, p.u / 2, p.v / 2, p.t / 2);
        return axis_dir(cx - w[0], cy - w[1]);
    }
    for (const auto& p : project.parts) {
        if (p.group != group || (p.role != "door" && p.role != "drawer")) continue;
        auto w = core::world(p, p.u / 2, p.v / 2, p.t / 2);
        return axis_dir(w[0] - cx, w[1] - cy);
    }
    return std::nullopt;
}

std::string dir_name(const Dir2& d) { return d[0] != 0 ? (d[0] > 0 ? "x+" : "x-") : (d[1] > 0 ? "y+" : "y-"); }

core::ProjectPtr orient(core::ProjectPtr project, const std::string& group, const Dir2& want) {
    auto matches = [&] {
        auto f = front_dir(*project, group);
        return f && (*f)[0] == want[0] && (*f)[1] == want[1];
    };
    if (matches()) return project;
    for (double angle : {90.0, -180.0, 270.0}) {
        project = furniture_layout::rotate_group(*project, group, angle);
        if (matches()) return project;
    }
    return project;
}

double free_x(const core::Project& project, const std::string& except) {
    double right = 0;
    for (const auto& name : furniture_layout::groups(project)) {
        if (name == except) continue;
        right = std::max(right, furniture_layout::group_bounds(project, name).second[0]);
    }
    return right + 1500;
}

// Nowa szafka z konfiguracji `cfg`, ustawiona jak `layout`; zwraca projekt i nazwę grupy.
std::pair<core::ProjectPtr, std::string> add_module(const core::Project& project, json cfg, const json& layout) {
    for (auto& section : cfg["sections"]) section.erase("column_widths");
    auto created = furniture_configurator::configure(project, cfg, std::nullopt, cfg.at("origin"));
    return {furniture_layout::placed(*created.first, created.second, layout, false), created.second};
}

// Ramiona układu L / U: z rozciągniętej szafki robimy z powrotem zwykłą (o dawnej głębokości), a rozciągnięcie zamieniamy na szafki ustawione bokiem.
core::ProjectPtr build_arrangement(const core::Project& project, const std::string& group, double old_depth, bool min_side, bool max_side, const Overrides& ov) {
    core::ProjectPtr result = std::make_shared<core::Project>(project);
    json cfg = furniture_configurator::read_config(*result, group);
    if (cfg.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
    double delta = num(cfg["depth"]) - old_depth;
    json first = cfg;
    first["depth"] = old_depth;
    if (ov.apply_first) merge_overrides(first, ov.values);
    result = furniture_configurator::configure(*result, first, group, first.at("origin")).first;
    Dir2 f = front_dir(*result, group).value_or(Dir2{0.0, -1.0});
    const std::string dir = dir_name(f);
    const char lateral = f[0] != 0 ? 'y' : 'x';
    json layout = furniture_layout::group_layout(*result, group);
    long long count = std::max<long long>(1, std::llround(delta / 600.0));
    double width = delta / (double)count;
    for (bool max_end : {false, true}) {
        if (max_end ? !max_side : !min_side) continue;
        Dir2 want = lateral == 'x' ? Dir2{max_end ? -1.0 : 1.0, 0.0} : Dir2{0.0, max_end ? -1.0 : 1.0};
        std::string previous = group;
        for (long long i = 0; i < count; ++i) {
            json module = cfg;
            module["depth"] = old_depth;
            module["width"] = width;
            merge_overrides(module, ov.values);
            auto [with_module, name] = add_module(*result, module, layout);
            result = with_module;
            auto box = furniture_layout::group_bounds(*result, name);
            result = furniture_layout::move_group(*result, name, free_x(*result, name) - box.first[0], 0, 0, false);
            result = orient(result, name, want);
            json align = {{std::string(1, lateral), max_end ? "end" : "start"}, {"z", "start"}};
            result = furniture_layout::snap_group(*result, name, previous, dir, align, 0);
            previous = name;
        }
    }
    return result;
}

// Szafka dostawiona nad rozciągniętą (na niej albo wisząca) — rozciągnięta wraca do dawnej wysokości.
core::ProjectPtr build_stack(const core::Project& project, const std::string& group, double old_height, bool hanging, const Overrides& ov) {
    core::ProjectPtr result = std::make_shared<core::Project>(project);
    json cfg = furniture_configurator::read_config(*result, group);
    if (cfg.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
    double delta = num(cfg["height"]) - old_height;
    json first = cfg;
    first["height"] = old_height;
    if (ov.apply_first) merge_overrides(first, ov.values);
    result = furniture_configurator::configure(*result, first, group, first.at("origin")).first;
    json layout = furniture_layout::group_layout(*result, group);
    json module = cfg;
    module["leg_height"] = 0;
    module["plinth"] = false;
    module["worktop"] = false;
    module["height"] = std::max(300.0, std::round(delta));
    for (auto& section : module["sections"]) if (section.value("key", std::string()) != "main") section["enabled"] = false;
    json origin = cfg.at("origin");
    origin[2] = num(origin[2]) + old_height + (hanging ? 500 : 0);
    if (hanging) {
        double depth = std::min(num(cfg["depth"]), 320.0);
        origin[1] = num(origin[1]) + num(cfg["depth"]) - depth;  // tył przy ścianie
        module["depth"] = depth;
        module["height"] = std::clamp(std::round(delta), 300.0, 900.0);
    }
    module["origin"] = origin;
    merge_overrides(module, ov.values);
    module["leg_height"] = 0;
    module["plinth"] = false;
    return add_module(*result, module, layout).first;
}

// ---------------------------------------------------------------- okienko

struct Option {
    std::string id, label, hint;
    bool enabled = true;
    std::function<core::ProjectPtr(const core::Project&, const Overrides&)> build;  // nullptr = zostaw (z ewentualnymi ustawieniami)
};

void draw_glyph(tk::Canvas* c, const std::string& id, double cx, bool enabled) {
    const char* wood = enabled ? "#e8d2ab" : "#e6e3dc";
    const char* edge = enabled ? "#7a6544" : "#a9a69d";
    const char* dark = enabled ? "#8c795a" : "#b7b4ab";
    const char* alt = enabled ? "#cfe0bd" : "#e6e3dc";  // dodane szafki
    auto rect = [&](double x1, double y1, double x2, double y2, const char* fill) { c->create_rectangle(x1, y1, x2, y2, {{"fill", fill}, {"outline", edge}}); };
    auto poly = [&](std::vector<double> p) { c->create_polygon(p, {{"fill", wood}, {"outline", edge}, {"width", 1}}); };
    auto line = [&](double x1, double y1, double x2, double y2) { c->create_line({x1, y1, x2, y2}, {{"fill", dark}, {"width", 2}}); };
    const double L = cx - 26, R = cx + 26, T = 10, B = 66;
    if (id == "keep_depth") {
        rect(cx - 22, 30, cx + 22, 56, wood);
        c->create_rectangle(cx - 22, 56, cx + 22, 66, {{"fill", "#d9b98a"}, {"outline", edge}});
    } else if (id == "corner_left") {
        poly({L, T + 14, R, T + 14, R, T + 56, L + 18, T + 56, L + 18, T + 34, L, T + 34});
    } else if (id == "corner_right") {
        poly({L, T + 14, R, T + 14, R, T + 34, R - 18, T + 34, R - 18, T + 56, L, T + 56});
    } else if (id == "arr_l_left") {
        rect(L, T + 6, R, T + 22, wood);
        rect(L, T + 24, L + 16, T + 40, alt);
        rect(L, T + 42, L + 16, T + 58, alt);
    } else if (id == "arr_l_right") {
        rect(L, T + 6, R, T + 22, wood);
        rect(R - 16, T + 24, R, T + 40, alt);
        rect(R - 16, T + 42, R, T + 58, alt);
    } else if (id == "arr_u") {
        rect(L, T + 6, R, T + 22, wood);
        rect(L, T + 24, L + 16, T + 40, alt);
        rect(L, T + 42, L + 16, T + 58, alt);
        rect(R - 16, T + 24, R, T + 40, alt);
        rect(R - 16, T + 42, R, T + 58, alt);
    } else if (id == "keep_height") {
        rect(cx - 16, T, cx + 16, B, wood);
        rect(cx - 12, T + 4, cx + 12, T + 28, "#f4efe6");
    } else if (id == "with_upper") {
        rect(cx - 18, T, cx + 18, T + 18, wood);
        rect(cx - 18, T + 22, cx + 18, B, wood);
        line(cx - 18, T + 20, cx + 18, T + 20);
    } else if (id == "stack") {
        rect(cx - 20, T, cx + 20, T + 22, alt);
        rect(cx - 20, T + 24, cx + 20, B, wood);
    } else if (id == "wall") {
        rect(cx - 20, T, cx + 20, T + 20, alt);
        rect(cx - 20, T + 38, cx + 20, B, wood);
        line(cx - 20, T + 36, cx + 20, T + 36);
    } else if (id == "wardrobe") {
        rect(cx - 22, T, cx + 22, B, wood);
        line(cx, T + 2, cx, B - 2);
        c->create_oval(cx - 6, 36, cx - 3, 39, {{"fill", dark}, {"outline", ""}});
        c->create_oval(cx + 3, 36, cx + 6, 39, {{"fill", dark}, {"outline", ""}});
    } else if (id == "shelving") {
        rect(cx - 20, T, cx + 20, B, "#f4efe6");
        for (int i = 1; i < 5; ++i) line(cx - 20, T + i * 56.0 / 5, cx + 20, T + i * 56.0 / 5);
    }
}

void show_dialog(App* app, const std::string& title, const std::string& lead, std::vector<Option> options, const std::string& group, const json& config) {
    auto* dialog = new tk::Toplevel(app);
    dialog->title(title);
    dialog->transient(app);
    auto* body = new ttk::Frame(dialog, {{"padding", 14}});
    body->pack({{"fill", "both"}, {"expand", true}});
    (new ttk::Label(body, {{"text", lead}, {"font", "{Segoe UI} 11 bold"}, {"wraplength", (int)(options.size() * (TILE_W + 10))}}))->pack({{"anchor", "w"}, {"pady", "0 8"}});
    auto* row = new ttk::Frame(body);
    row->pack({{"fill", "x"}});
    auto settings = build_settings(app, body, config);
    auto close = [dialog, app] { app->after(1, [dialog] { try { dialog->destroy(); } catch (const tk::TclError&) {} }); };
    int column = 0;
    for (const Option& option : options) {
        auto* cell = new tk::Frame(row, {{"bg", forma_theme::PANEL}});
        cell->grid({{"row", 0}, {"column", column++}, {"padx", 4}, {"pady", 3}});
        auto* canvas = new tk::Canvas(cell, {{"width", (int)TILE_W}, {"height", (int)TILE_H}, {"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER},
                                             {"cursor", option.enabled ? "hand2" : "arrow"}});
        canvas->pack();
        draw_glyph(canvas, option.id, TILE_W / 2, option.enabled);
        canvas->create_text(TILE_W / 2, 82, {{"text", option.label}, {"fill", option.enabled ? forma_theme::TEXT : forma_theme::MUTED}, {"font", "{Segoe UI} 9 bold"}});
        canvas->create_text(TILE_W / 2, 96, {{"text", option.hint}, {"fill", forma_theme::MUTED}, {"font", "{Segoe UI} 8"}});
        if (!option.enabled) continue;
        auto pick = [app, group, option, close, settings](tk::Event&) {
            Overrides ov = settings->collect();
            close();
            try {
                auto build = option.build;
                if (!build && !ov.values.empty()) {
                    // „Zostaw", ale z zmienionymi ustawieniami: przebuduj samą rozciągniętą szafkę.
                    build = [group](const core::Project& project, const Overrides& o) {
                        json config = furniture_configurator::read_config(project, group);
                        merge_overrides(config, o.values);
                        return furniture_configurator::configure(project, config, group, config.at("origin")).first;
                    };
                }
                if (!build) { app->status.set("Zostawiono rozciągnięcie bez zmiany typu mebla"); return std::string(); }
                core::ProjectPtr result = build(*app->project, ov);
                app->snapshot();
                app->project = result;
                for (auto& part : app->project->parts) if (part.group == group) { app->selected = part.id; break; }
                app->refresh();
                app->status.set(option.label + " · Ctrl+Z cofa zmianę");
            } catch (const std::exception& error) {
                app->status.set(error.what());
            }
            return std::string();
        };
        canvas->bind("<Button-1>", pick);
        canvas->bind("<Enter>", [canvas](tk::Event&) { canvas->configure({{"highlightbackground", forma_theme::ACCENT}}); return std::string(); });
        canvas->bind("<Leave>", [canvas](tk::Event&) { canvas->configure({{"highlightbackground", forma_theme::BORDER}}); return std::string(); });
    }
    (new ttk::Button(body, {{"text", "Zostaw jak jest"}, {"command", std::function<void()>(close)}}))->pack({{"anchor", "e"}, {"pady", "10 0"}});
}

}  // namespace

void offer(App* app, const core::Project& base, const std::string& group, const std::string& face) {
    if (face.size() < 2) return;
    json before = furniture_configurator::read_config(base, group);
    json after = furniture_configurator::read_config(*app->project, group);
    if (before.is_null() || after.is_null()) return;
    if (face[0] == 'y') {
        double old_depth = num(before["depth"]), new_depth = num(after["depth"]);
        if (new_depth - old_depth < MIN_CHANGE) return;
        double old_width = num(before["width"]);
        auto corner = [group, old_depth, old_width, new_depth](const std::string& side) {
            return [=](const core::Project& project, const Overrides&) {
                return corner_furniture::convert_to_corner(project, group, "L", side, json(old_depth), json(old_width), json(new_depth)).first;
            };
        };
        auto arrangement = [group, old_depth](bool min_side, bool max_side) {
            return [=](const core::Project& project, const Overrides& ov) { return build_arrangement(project, group, old_depth, min_side, max_side, ov); };
        };
        show_dialog(app, "Rozciągnięto front do siebie", std::format("Głębokość {:g} → {:g} mm. Co ma z tego powstać?", old_depth, new_depth),
                    {{"keep_depth", "Pogłębienie", "tylko głębszy mebel", true, nullptr},
                     {"corner_left", "Narożnik L", "jedna szafka, w lewo", true, corner("left")},
                     {"corner_right", "Narożnik L", "jedna szafka, w prawo", true, corner("right")},
                     {"arr_l_left", "Układ L", "szafki z lewej", true, arrangement(true, false)},
                     {"arr_l_right", "Układ L", "szafki z prawej", true, arrangement(false, true)},
                     {"arr_u", "Układ U", "szafki z obu stron", true, arrangement(true, true)}},
                    group, after);
    } else if (face[0] == 'z') {
        double old_height = num(before["height"]), new_height = num(after["height"]);
        double delta = new_height - old_height;
        if (delta < MIN_CHANGE) return;
        auto reconfigure = [group](std::function<void(json&)> change) {
            return [=](const core::Project& project, const Overrides& ov) {
                json config = furniture_configurator::read_config(project, group);
                if (config.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
                change(config);
                merge_overrides(config, ov.values);
                return furniture_configurator::configure(project, config, group, config["origin"]).first;
            };
        };
        auto stacked = [group, old_height](bool hanging) {
            return [=](const core::Project& project, const Overrides& ov) { return build_stack(project, group, old_height, hanging, ov); };
        };
        show_dialog(app, "Rozciągnięto mebel do góry", std::format("Wysokość {:g} → {:g} mm. Jakim meblem ma być?", old_height, new_height),
                    {{"keep_height", "Wyższa szafka", "bez zmian typu", true, nullptr},
                     {"with_upper", "Z pawlaczem", "strefa górna", true, reconfigure([delta](json& config) {
                          json main = section_of(config, "main");
                          json upper = {{"enabled", true}, {"height", std::max(200.0, std::round(delta))}, {"columns", main.value("columns", 1)}, {"front", "doors"}, {"shelves", 0}};
                          if (main.contains("column_widths")) upper["column_widths"] = main["column_widths"];
                          set_section(config, "upper", upper);
                      })},
                     {"stack", "Szafka na szafce", "osobny moduł wyżej", true, stacked(false)},
                     {"wall", "Szafka wisząca", "nad blatem", true, stacked(true)},
                     {"wardrobe", "Szafa", "drzwi, półki", true, reconfigure([](json& config) {
                          double width = num(config["width"]);
                          set_section(config, "upper", {{"enabled", false}});
                          set_section(config, "main", {{"front", "doors"}, {"doors", width >= 800 ? 2 : 1}, {"shelves", 4}});
                      })},
                     {"shelving", "Regał", "otwarte półki", true, reconfigure([new_height](json& config) {
                          set_section(config, "upper", {{"enabled", false}});
                          set_section(config, "main", {{"front", "open"}, {"shelves", (long long)std::clamp(std::lround(new_height / 350.0), 2L, 8L)}});
                      })}},
                    group, after);
    }
}

}  // namespace stretch_choice
