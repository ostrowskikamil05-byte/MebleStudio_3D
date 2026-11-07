#include "stretch_choice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "app.h"
#include "corner_furniture.h"
#include "forma_theme.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "journal.h"
#include "opening_options.h"
#include "tile_glyphs.h"

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
    // Zależności, które kreator inaczej odrzuciłby błędem: strefa szuflad nie ma półek; magnetyczny TIP-ON wymaga zawiasów bez sprężyny.
    if (cfg.contains("sections") && cfg["sections"].is_array())
        for (auto& section : cfg["sections"])
            if (section.value("front", std::string()) == "drawers" && section.value("shelves", 0) != 0) section["shelves"] = 0;
    if (values.contains("handle_mode") && !values.contains("hinge_spring") && cfg.value("handle_mode", json()).is_string()) {
        auto it = opening_options::OPTIONS.find(cfg["handle_mode"].get<std::string>());
        if (it != opening_options::OPTIONS.end() && it->contains("hinge_spring")) cfg["hinge_spring"] = (*it)["hinge_spring"];
    }
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
    std::function<void()> on_change;  // po każdej zmianie pola (podgląd na żywo w oknie Konfiguruj)

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

    // Po zastosowaniu zmian bieżące wartości pól stają się wartościami wyjściowymi.
    void rebase() {
        for (auto& row : choices) {
            std::string label = row.var.get();
            if (row.choices.contains(label)) row.initial = row.choices.at(label);
        }
        for (auto& row : checks) row.initial = row.var.get();
    }
};

std::vector<std::string> keys_of(const json& choices) {
    std::vector<std::string> out;
    for (auto& [k, v] : choices.items()) out.push_back(k);
    return out;
}

// Skraca tekst do `limit` znaków (UTF-8), dodając wielokropek.
std::string shorten(const std::string& text, size_t limit) {
    size_t count = 0, i = 0;
    while (i < text.size()) {
        if (count == limit) return text.substr(0, i) + "…";
        unsigned char ch = (unsigned char)text[i];
        i += ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
        ++count;
    }
    return text;
}

// Ustawienia mebla w zwijanych sekcjach (Korpus, Fronty, Uchwyty, Okucia, Nóżki, Dodatki), każda z ikoną, tytułem i podsumowaniem;
// wybory jako kafelki z miniaturami. `full` = pełny zestaw (okno Konfiguruj), inaczej zestaw skrócony (okno po rozciągnięciu).
std::shared_ptr<Settings> build_settings(App* app, tk::Widget* parent, const json& cfg, bool full = false,
                                         const std::string& title = "Ustawienia nowych szafek",
                                         const std::string& apply_text = "Zastosuj zmiany też do rozciągniętej szafki") {
    auto settings = std::make_shared<Settings>();
    json openings = {{"Uchwyt / gałka", "handle"}, {"Bez uchwytu", "none"}};
    for (auto& [k, v] : opening_options::OPTIONS.items()) if (k != "none") openings[v.at("label").get<std::string>()] = k;
    json materials = json::object();
    std::map<std::string, std::string> colors;  // etykieta materiału -> kolor próbki
    for (auto& m : app->project->materials) { materials[m.name] = m.id; colors[m.name] = m.color; }
    struct Spec { const char* key; const char* label; json choices; json shorts = json::object(); };
    struct Group { const char* title; const char* glyph; bool open; std::vector<Spec> specs; std::vector<std::pair<std::string, std::string>> checks; };
    std::vector<Group> groups;
    {
        Group korpus{"Korpus", "▣", false, {}, {}};
        korpus.specs.push_back({"material", "Materiał korpusu", materials});
        if (full) korpus.specs.push_back({"top_mode", "Góra korpusu", json{{"Pełna płyta", "panel"}, {"Listwy pod blat", "rails"}}});
        groups.push_back(korpus);
        Group fronty{"Fronty", "▭", true, {}, {}};
        fronty.specs.push_back({"front_material", "Materiał frontów", materials});
        fronty.specs.push_back({"front_style", "Styl frontu", json{{"Gładki", "smooth"}, {"Ramka frezowana", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}},
                                json{{"Gładki", "Gładki"}, {"Ramka frezowana", "Ramkowy"}, {"Ryflowany", "Ryflowany"}, {"Chwyt frezowany", "Frezowany"}}});
        fronty.specs.push_back({"door_hinge", "Strona zawiasów (pojedyncze drzwi)", json{{"Automatycznie", "auto"}, {"Po lewej", "left"}, {"Po prawej", "right"}},
                                json{{"Automatycznie", "Auto"}, {"Po lewej", "Lewa"}, {"Po prawej", "Prawa"}}});
        groups.push_back(fronty);
        Group uchwyty{"Uchwyty", "⌐", false, {}, {}};
        uchwyty.specs.push_back({"handle_mode", "Rodzaj uchwytu", openings});
        uchwyty.specs.push_back({"handle_color", "Kolor uchwytu", json{{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Złoty", "#bb9656"}, {"Biały", "#efefea"}}});
        groups.push_back(uchwyty);
        Group okucia{"Okucia", "⚙", false, {}, {}};
        okucia.specs.push_back({"hinge_pattern", "Zawiasy — wiercenie", json{{"Puszka 35 / rozstaw 45", "hinge45"}, {"Puszka 35 / rozstaw 52", "hinge52"}, {"Hettich Sensys", "hinge_sensys52"}},
                                json{{"Puszka 35 / rozstaw 45", "Ø35 · 45"}, {"Puszka 35 / rozstaw 52", "Ø35 · 52"}, {"Hettich Sensys", "Sensys"}}});
        if (full) okucia.specs.push_back({"hinge_spring", "Zawiasy — sprężyna", json{{"Nieokreślona", "unknown"}, {"Ze sprężyną", "sprung"}, {"Bez sprężyny", "unsprung"}},
                                          json{{"Nieokreślona", "Nieokreślona"}, {"Ze sprężyną", "Sprężyna"}, {"Bez sprężyny", "Bez"}}});
        groups.push_back(okucia);
        Group nozki{"Nóżki i cokół", "⊥", false, {}, {{"plinth", "Cokół"}}};
        nozki.specs.push_back({"leg_height", "Wysokość nóżek", json{{"Bez nóżek", 0}, {"60 mm", 60}, {"100 mm", 100}, {"150 mm", 150}}});
        if (full) {
            nozki.specs.push_back({"leg_count", "Liczba nóżek", json{{"4 sztuki", 4}, {"6 sztuk", 6}}, json{{"4 sztuki", "4 szt."}, {"6 sztuk", "6 szt."}}});
            nozki.specs.push_back({"leg_color", "Kolor nóżek", json{{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Biały", "#efefea"}}});
        }
        groups.push_back(nozki);
        Group dodatki{"Dodatki", "✦", false, {}, {{"worktop", "Blat"}, {"led", "LED"}}};
        if (full) dodatki.specs.push_back({"led_mount", "LED — miejsce", json{{"Pod półkami", "under_shelf"}, {"W boku korpusu", "side"}}, json{{"Pod półkami", "Pod półką"}, {"W boku korpusu", "W boku"}}});
        groups.push_back(dodatki);
    }
    auto* box = new ttk::LabelFrame(parent, {{"text", title}, {"padding", 8}});
    box->pack({{"fill", "x"}, {"pady", "10 0"}});
    std::weak_ptr<Settings> weak = settings;
    auto notify = [weak] { if (auto s = weak.lock()) if (s->on_change) s->on_change(); };
    auto register_row = [&](const Spec& spec) -> ChoiceRow& {
        json initial = cfg.contains(spec.key) ? cfg.at(spec.key) : json(nullptr);
        std::string initial_label = spec.choices.items().begin().key();
        for (auto& [label, value] : spec.choices.items()) if (value == initial) { initial_label = label; break; }
        json initial_value = spec.choices.at(initial_label);
        settings->choices.push_back({spec.key, spec.label, spec.choices, tk::StringVar(initial_label), initial_value});
        return settings->choices.back();
    };
    for (auto& g : groups) {
        if (g.specs.empty() && g.checks.empty()) continue;
        auto* section = new ttk::Frame(box);
        section->pack({{"fill", "x"}, {"pady", "2 0"}});
        auto* header = new ttk::Frame(section, {{"padding", "2 3"}});
        header->pack({{"fill", "x"}});
        auto* icon = new tk::Canvas(header, {{"width", 30}, {"height", 30}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
        icon->pack({{"side", "left"}, {"padx", "2 8"}});
        icon->create_oval(2, 2, 28, 28, {{"fill", forma_theme::ACCENT_SOFT}, {"outline", forma_theme::ACCENT}, {"width", 1}});
        icon->create_text(15, 15, {{"text", g.glyph}, {"fill", forma_theme::ACCENT}, {"font", "{Segoe UI Symbol} 12"}});
        auto* title_label = new ttk::Label(header, {{"text", g.title}, {"font", "{Segoe UI} 10 bold"}, {"cursor", "hand2"}});
        title_label->pack({{"side", "left"}});
        tk::StringVar arrow(g.open ? "▾" : "▸"), summary("");
        auto* arrow_label = new ttk::Label(header, {{"textvariable", arrow}, {"cursor", "hand2"}});
        arrow_label->pack({{"side", "right"}, {"padx", "6 2"}});
        auto* summary_label = new ttk::Label(header, {{"textvariable", summary}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}, {"cursor", "hand2"}});
        summary_label->pack({{"side", "right"}});
        auto* body = new ttk::Frame(section, {{"padding", "38 2 0 6"}});
        body->columnconfigure(0, {{"weight", 1}, {"uniform", "kol"}});
        body->columnconfigure(1, {{"weight", 1}, {"uniform", "kol"}});
        auto is_open = std::make_shared<bool>(g.open);
        auto show_state = [is_open, body, arrow]() mutable {
            arrow.set(*is_open ? "▾" : "▸");
            if (*is_open) body->pack({{"fill", "x"}});
            else body->pack_forget();
        };
        auto toggle = [is_open, show_state](tk::Event&) mutable { *is_open = !*is_open; show_state(); return std::string(); };
        for (tk::Widget* w : std::vector<tk::Widget*>{header, icon, title_label, arrow_label, summary_label}) w->bind("<Button-1>", toggle);
        show_state();
        // zmienne tej sekcji — do podsumowania w nagłówku
        auto vars = std::make_shared<std::vector<tk::StringVar>>();
        auto update_summary = [vars, summary]() mutable {
            std::string text;
            for (auto& v : *vars) text += (text.empty() ? "" : " · ") + shorten(v.get(), 14);
            summary.set(text);
        };
        int cell_index = 0;
        for (auto& spec : g.specs) {
            if (spec.choices.empty()) continue;
            ChoiceRow& row = register_row(spec);
            tk::StringVar var = row.var;
            vars->push_back(var);
            var.trace_add("write", notify);
            var.trace_add("write", update_summary);
            auto* cell = new ttk::Frame(body);
            cell->grid({{"row", cell_index / 2}, {"column", cell_index % 2}, {"sticky", "ew"}, {"padx", "0 10"}, {"pady", "0 6"}});
            ++cell_index;
            (new ttk::Label(cell, {{"text", row.label}, {"font", "{Segoe UI} 9 bold"}}))->pack({{"anchor", "w"}});
            auto* strip = new tk::Frame(cell, {{"bg", forma_theme::PANEL}});
            strip->pack({{"fill", "x"}, {"pady", "2 0"}});
            struct Tile { tk::Canvas* canvas; std::string label, short_label; };
            auto tiles = std::make_shared<std::vector<Tile>>();
            std::string key = spec.key;
            bool swatch = key == "material" || key == "front_material";
            auto redraw = [tiles, key, var, colors] {
                for (auto& tile : *tiles) {
                    try {
                        auto found = colors.find(tile.label);
                        tile_glyphs::draw(tile.canvas, key, tile.label, tile.short_label, var.get() == tile.label, found != colors.end() ? found->second : std::string());
                    } catch (const tk::TclError&) {
                    }
                }
            };
            int column = 0;
            for (auto& [label, value] : spec.choices.items()) {
                std::string short_label = spec.shorts.contains(label) ? spec.shorts.at(label).get<std::string>() : shorten(label, swatch ? 10 : 12);
                auto* canvas = new tk::Canvas(strip, {{"height", 62}, {"width", 74}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
                strip->columnconfigure(column % 4, {{"weight", 1}, {"uniform", "t" + key}});
                canvas->grid({{"row", column / 4}, {"column", column % 4}, {"padx", 2}, {"pady", "0 2"}, {"sticky", "ew"}});
                tiles->push_back({canvas, label, short_label});
                ++column;
                std::string chosen = label;
                canvas->bind("<Configure>", [redraw](tk::Event&) { redraw(); return std::string(); });
                canvas->bind("<Button-1>", [var, chosen](tk::Event&) mutable { var.set(chosen); return std::string(); });
            }
            var.trace_add("write", redraw);
        }
        if (!g.checks.empty()) {
            auto* extras = new ttk::Frame(body);
            extras->grid({{"row", (cell_index + 1) / 2}, {"column", 0}, {"columnspan", 2}, {"sticky", "w"}, {"pady", "2 0"}});
            for (auto& [key, label] : g.checks) {
                bool value = cfg.contains(key) && cfg.at(key).is_boolean() && cfg.at(key).get<bool>();
                settings->checks.push_back({key, label, tk::BooleanVar(value), value});
                tk::BooleanVar var = settings->checks.back().var;
                var.trace_add("write", notify);
                (new ttk::Checkbutton(extras, {{"text", label}, {"variable", var}}))->pack({{"side", "left"}, {"padx", "0 14"}});
            }
        }
        update_summary();
    }
    settings->apply_first = tk::BooleanVar(!full);
    (new ttk::Checkbutton(box, {{"text", apply_text}, {"variable", settings->apply_first}}))->pack({{"anchor", "w"}, {"pady", "8 0"}});
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

// Kierunek (oś pokoju), w którą patrzy lewy bok mebla (czyli „lewe" zawiasy drzwi), liczony ze środka mebla do środka jego lewego boku.
std::optional<Dir2> left_side_dir(const core::Project& project, const std::string& group) {
    auto box = furniture_layout::group_bounds(project, group);
    double cx = (box.first[0] + box.second[0]) / 2, cy = (box.first[1] + box.second[1]) / 2;
    for (const auto& p : project.parts) {
        if (p.group != group || p.role != "left") continue;
        auto w = core::world(p, p.u / 2, p.v / 2, p.t / 2);
        return axis_dir(w[0] - cx, w[1] - cy);
    }
    return std::nullopt;
}

// Pojedyncze drzwi mebla mają zawiasy od strony `want` (kierunek w pokoju); przebudowuje mebel tylko wtedy, gdy trzeba.
core::ProjectPtr hinge_toward(core::ProjectPtr project, const std::string& group, const Dir2& want) {
    json cfg = furniture_configurator::read_config(*project, group);
    auto left = left_side_dir(*project, group);
    if (cfg.is_null() || !left) return project;
    double dot = (*left)[0] * want[0] + (*left)[1] * want[1];
    if (dot == 0) return project;
    std::string wanted = dot > 0 ? "left" : "right";
    std::string current = cfg.value("door_hinge", std::string("auto")) == "right" ? "right" : "left";
    if (wanted == current) return project;
    cfg["door_hinge"] = wanted;
    return furniture_configurator::configure(*project, cfg, group, cfg.at("origin")).first;
}

// Nowa szafka z konfiguracji `cfg`, ustawiona jak `layout`; zwraca projekt i nazwę grupy.
std::pair<core::ProjectPtr, std::string> add_module(const core::Project& project, json cfg, const json& layout) {
    for (auto& section : cfg["sections"]) section.erase("column_widths");
    auto created = furniture_configurator::configure(project, cfg, std::nullopt, cfg.at("origin"));
    return {furniture_layout::placed(*created.first, created.second, layout, false), created.second};
}

// Szafka główna w układzie L/U: część frontu zasłonięta przez dostawione ramię dostaje zaślepkę zamiast drzwi (drzwi nie mogą przechodzić przez narożnik).
// Gdy zostaje wolny pas szerszy niż 150 mm, korpus dzieli się na odcinki: zaślepka · szafka z drzwiami · zaślepka. Zwraca nazwę odcinka z drzwiami (albo pusty napis).
core::ProjectPtr cover_corner(core::ProjectPtr result, const std::string& group, const json& main_cfg, char lateral, double covered_min, double covered_max, std::string& free_name) {
    free_name.clear();
    const int li = lateral == 'x' ? 0 : 1;
    const double width = num(main_cfg["width"]);
    covered_min = std::clamp(covered_min, 0.0, width);
    covered_max = std::clamp(covered_max, 0.0, width - covered_min);
    if (covered_min + covered_max <= 0) return result;
    struct Segment { double width; bool blind; };
    std::vector<Segment> segments;
    double free_width = width - covered_min - covered_max;
    if (free_width < 150) {
        segments.push_back({width, true});
    } else {
        if (covered_min > 0) segments.push_back({covered_min, true});
        segments.push_back({free_width, false});
        if (covered_max > 0) segments.push_back({covered_max, true});
    }
    auto config_for = [&](const Segment& segment) {
        json c = main_cfg;
        c["width"] = std::round(segment.width * 10) / 10;
        for (auto& s : c["sections"]) {
            s.erase("column_widths");
            if (segment.blind) { s["front"] = "blind"; s["shelves"] = 0; s["columns"] = 1; }
        }
        return c;
    };
    const double low = furniture_layout::group_bounds(*result, group).first[li];
    result = furniture_configurator::configure(*result, config_for(segments[0]), group, main_cfg.at("origin")).first;
    double shift = low - furniture_layout::group_bounds(*result, group).first[li];
    if (std::abs(shift) > 0.01) result = furniture_layout::move_group(*result, group, li == 0 ? shift : 0, li == 1 ? shift : 0, 0, false);
    if (!segments[0].blind) free_name = group;
    std::string previous = group;
    const std::string direction = lateral == 'x' ? "x+" : "y+";
    const std::string other = lateral == 'x' ? "y" : "x";
    for (size_t i = 1; i < segments.size(); ++i) {
        json layout = furniture_layout::group_layout(*result, group);
        auto [with_module, name] = add_module(*result, config_for(segments[i]), layout);
        result = furniture_layout::snap_group(*with_module, name, previous, direction, json{{other, "start"}, {"z", "start"}}, 0);
        if (!segments[i].blind) free_name = name;
        previous = name;
    }
    return result;
}

// Ramiona układu L / U: z rozciągniętej szafki robimy z powrotem zwykłą (o dawnej głębokości), a rozciągnięcie zamieniamy na szafki ustawione bokiem.
core::ProjectPtr build_arrangement(const core::Project& project, const std::string& group, double old_depth, const json& base_origin, bool min_side, bool max_side, const Overrides& ov) {
    core::ProjectPtr result = std::make_shared<core::Project>(project);
    json cfg = furniture_configurator::read_config(*result, group);
    if (cfg.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
    double delta = num(cfg["depth"]) - old_depth;
    json first = cfg;
    first["depth"] = old_depth;
    first["origin"] = base_origin;  // przywróć dawne położenie (rozciąganie przodu przesunęło początek)
    if (ov.apply_first) merge_overrides(first, ov.values);
    result = furniture_configurator::configure(*result, first, group, first.at("origin")).first;
    Dir2 f = front_dir(*result, group).value_or(Dir2{0.0, -1.0});
    const std::string dir = dir_name(f);
    const char lateral = f[0] != 0 ? 'y' : 'x';
    json layout = furniture_layout::group_layout(*result, group);
    long long count = std::max<long long>(1, std::llround(delta / 600.0));
    double width = delta / (double)count;
    double covered_min = 0, covered_max = 0;  // jak szeroko ramiona zasłaniają front szafki głównej
    const int lateral_index = lateral == 'x' ? 0 : 1;
    for (bool max_end : {false, true}) {
        if (max_end ? !max_side : !min_side) continue;
        Dir2 want = lateral == 'x' ? Dir2{max_end ? -1.0 : 1.0, 0.0} : Dir2{0.0, max_end ? -1.0 : 1.0};
        std::string previous = group;
        for (long long i = 0; i < count; ++i) {
            json module = cfg;
            module["depth"] = old_depth;
            module["width"] = width;
            module["origin"] = base_origin;
            merge_overrides(module, ov.values);
            auto [with_module, name] = add_module(*result, module, layout);
            result = with_module;
            auto box = furniture_layout::group_bounds(*result, name);
            result = furniture_layout::move_group(*result, name, free_x(*result, name) - box.first[0], 0, 0, false);
            result = orient(result, name, want);
            json align = {{std::string(1, lateral), max_end ? "end" : "start"}, {"z", "start"}};
            result = furniture_layout::snap_group(*result, name, previous, dir, align, 0);
            // Drzwi ramienia mają zawiasy po stronie wolnego końca (z dala od szafki głównej), żeby otwarte nie wchodziły w narożnik.
            if (!ov.values.contains("door_hinge")) result = hinge_toward(result, name, f);
            if (i == 0) {
                auto arm = furniture_layout::group_bounds(*result, name);
                (max_end ? covered_max : covered_min) = arm.second[lateral_index] - arm.first[lateral_index];
            }
            previous = name;
        }
    }
    // Front szafki głównej zasłonięty przez ramię nie może mieć drzwi: zaślepka (albo podział na zaślepkę i szafkę z drzwiami).
    std::string door_group;
    result = cover_corner(result, group, first, lateral, covered_min, covered_max, door_group);
    // Szafka z drzwiami przy jednym ramieniu: zawiasy od strony przeciwnej do ramienia (otwiera się w stronę pokoju, nie na ramię).
    if (!door_group.empty() && !ov.values.contains("door_hinge") && min_side != max_side) {
        Dir2 away = lateral == 'x' ? Dir2{min_side ? 1.0 : -1.0, 0.0} : Dir2{0.0, min_side ? 1.0 : -1.0};
        result = hinge_toward(result, door_group, away);
    }
    return result;
}

// Szafka dostawiona nad rozciągniętą (na niej albo wisząca) — rozciągnięta wraca do dawnej wysokości.
core::ProjectPtr build_stack(const core::Project& project, const std::string& group, double old_height, const json& base_origin, bool hanging, const Overrides& ov) {
    core::ProjectPtr result = std::make_shared<core::Project>(project);
    json cfg = furniture_configurator::read_config(*result, group);
    if (cfg.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
    double delta = num(cfg["height"]) - old_height;
    json first = cfg;
    first["height"] = old_height;
    first["origin"] = base_origin;
    if (ov.apply_first) merge_overrides(first, ov.values);
    result = furniture_configurator::configure(*result, first, group, first.at("origin")).first;
    json layout = furniture_layout::group_layout(*result, group);
    json module = cfg;
    module["leg_height"] = 0;
    module["plinth"] = false;
    module["worktop"] = false;
    module["height"] = std::max(300.0, std::round(delta));
    for (auto& section : module["sections"]) if (section.value("key", std::string()) != "main") section["enabled"] = false;
    json origin = base_origin;
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
                journal::event(app, "rozciąganie", std::format("wybór „{}” dla {} · zmienione ustawienia: {} · zastosuj też do pierwszej: {}", option.label + " / " + option.hint, group,
                                                              ov.values.empty() ? std::string("brak") : ov.values.dump(), ov.apply_first ? "tak" : "nie"));
                core::ProjectPtr result = build(*app->project, ov);
                journal::dump_groups(app, *result, "po: " + option.label);
                app->snapshot();
                app->project = result;
                for (auto& part : app->project->parts) if (part.group == group) { app->selected = part.id; break; }
                app->refresh();
                app->status.set(option.label + " · Ctrl+Z cofa zmianę");
            } catch (const std::exception& error) {
                journal::event(app, "rozciąganie", std::string("BŁĄD: ") + error.what());
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

namespace {
struct Prompt {
    std::string title, lead, group;
    std::vector<Option> options;
    json config;
};

// Wspólna część dla okna wyboru i testów: jakie warianty daje rozciągnięcie ściany `face` (puste = zmiana za mała albo mebel spoza kreatora).
std::optional<Prompt> make_prompt(const core::Project& base, const core::Project& stretched, const std::string& group, const std::string& face) {
    if (face.size() < 2) return std::nullopt;
    json before = furniture_configurator::read_config(base, group);
    json after = furniture_configurator::read_config(stretched, group);
    if (before.is_null() || after.is_null()) return std::nullopt;
    if (face[0] == 'y') {
        double old_depth = num(before["depth"]), new_depth = num(after["depth"]);
        if (new_depth - old_depth < MIN_CHANGE) return std::nullopt;
        double old_width = num(before["width"]);
        auto corner = [group, old_depth, old_width, new_depth](const std::string& side) {
            return [=](const core::Project& project, const Overrides&) {
                return corner_furniture::convert_to_corner(project, group, "L", side, json(old_depth), json(old_width), json(new_depth)).first;
            };
        };
        // Narożnik jednej szafki wymaga szerokości większej od głębokości ramienia — gdy się nie da, kafelek jest wyłączony zamiast błędu po kliknięciu.
        auto can_corner = [&](const std::string& side) {
            try { corner(side)(stretched, Overrides{}); return true; } catch (const std::exception&) { return false; }
        };
        json base_origin = before.at("origin");
        auto arrangement = [group, old_depth, base_origin](bool min_side, bool max_side) {
            return [=](const core::Project& project, const Overrides& ov) { return build_arrangement(project, group, old_depth, base_origin, min_side, max_side, ov); };
        };
        return Prompt{"Rozciągnięto front do siebie", std::format("Głębokość {:g} → {:g} mm. Co ma z tego powstać?", old_depth, new_depth), group,
                    {{"keep_depth", "Pogłębienie", "tylko głębszy mebel", true, nullptr},
                     {"corner_left", "Narożnik L", can_corner("left") ? "jedna szafka, w lewo" : "za wąska szafka", can_corner("left"), corner("left")},
                     {"corner_right", "Narożnik L", can_corner("right") ? "jedna szafka, w prawo" : "za wąska szafka", can_corner("right"), corner("right")},
                     {"arr_l_left", "Układ L", "szafki z lewej", true, arrangement(true, false)},
                     {"arr_l_right", "Układ L", "szafki z prawej", true, arrangement(false, true)},
                     {"arr_u", "Układ U", "szafki z obu stron", true, arrangement(true, true)}},
                    after};
    } else if (face[0] == 'z') {
        double old_height = num(before["height"]), new_height = num(after["height"]);
        double delta = new_height - old_height;
        if (delta < MIN_CHANGE) return std::nullopt;
        auto reconfigure = [group](std::function<void(json&)> change) {
            return [=](const core::Project& project, const Overrides& ov) {
                json config = furniture_configurator::read_config(project, group);
                if (config.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
                change(config);
                merge_overrides(config, ov.values);
                return furniture_configurator::configure(project, config, group, config["origin"]).first;
            };
        };
        json base_origin = before.at("origin");
        auto stacked = [group, old_height, base_origin](bool hanging) {
            return [=](const core::Project& project, const Overrides& ov) { return build_stack(project, group, old_height, base_origin, hanging, ov); };
        };
        return Prompt{"Rozciągnięto mebel do góry", std::format("Wysokość {:g} → {:g} mm. Jakim meblem ma być?", old_height, new_height), group,
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
                    after};
    }
    return std::nullopt;
}
}  // namespace

void offer(App* app, const core::Project& base, const std::string& group, const std::string& face) {
    auto prompt = make_prompt(base, *app->project, group, face);
    json before = furniture_configurator::read_config(base, group);
    json after = furniture_configurator::read_config(*app->project, group);
    if (before.is_null() || after.is_null()) return;
    journal::event(app, "rozciąganie", std::format("{}: ściana {} · wymiary {:g}×{:g}×{:g} → {:g}×{:g}×{:g} · początek {} → {}", group, face, num(before["width"]), num(before["depth"]), num(before["height"]),
                                                    num(after["width"]), num(after["depth"]), num(after["height"]), before.at("origin").dump(), after.at("origin").dump()));
    journal::dump_groups(app, *app->project, "po rozciągnięciu (przed wyborem)");
    if (prompt) show_dialog(app, prompt->title, prompt->lead, prompt->options, prompt->group, prompt->config);
}

std::vector<Variant> variants(const core::Project& base, const core::Project& stretched, const std::string& group, const std::string& face) {
    std::vector<Variant> out;
    auto prompt = make_prompt(base, stretched, group, face);
    if (!prompt) return out;
    for (const Option& option : prompt->options) {
        Variant v{option.id, option.label + " / " + option.hint, nullptr, option.enabled};
        if (option.build) v.build = [build = option.build](const core::Project& p) { return build(p, Overrides{}); };
        out.push_back(std::move(v));
    }
    return out;
}

namespace {
// Jedno okno Konfiguruj naraz: kolejne kliknięcie w mebel zastępuje poprzednie okno (to samo albo inne) zamiast otwierać drugie.
tk::Toplevel* g_config_dialog = nullptr;
std::string g_config_group;
}  // namespace

void apply_overrides(core::json& config, const core::json& values) { merge_overrides(config, values); }

tk::Widget* configuration_dialog() {
    try {
        if (g_config_dialog != nullptr && g_config_dialog->winfo_exists()) return g_config_dialog;
    } catch (const tk::TclError&) {}
    return nullptr;
}

void open_configuration(App* app, const std::string& group, const std::string& notice) {
    json cfg = furniture_configurator::read_config(*app->project, group);
    if (cfg.is_null()) {
        journal::event(app, "konfiguracja", group + ": mebel nie pochodzi z kreatora — brak konfiguracji");
        app->status.set("„" + group + "” nie pochodzi z kreatora mebli — okucia i korpus ustawisz w panelu Właściwości");
        return;
    }
    if (g_config_dialog != nullptr) {
        try {
            if (g_config_dialog->winfo_exists()) {
                journal::event(app, "konfiguracja", std::format("zamknięto poprzednie okno ({}) — zastąpione oknem dla {}", g_config_group, group));
                g_config_dialog->destroy();
            }
        } catch (const tk::TclError&) {}
        g_config_dialog = nullptr;
    }
    auto row_now = furniture_layout::row_groups(*app->project, group);
    journal::event(app, "konfiguracja", std::format("otwarto okno dla {} (rząd: {} szafek)", group, row_now.size()));
    auto* dialog = new tk::Toplevel(app);
    g_config_dialog = dialog;
    g_config_group = group;
    dialog->title("Konfiguracja mebla — " + group);
    dialog->transient(app);
    // Układ: stopka z przyciskiem i komunikatem na dole, nad nią przewijana treść (okno nigdy nie wychodzi poza ekran).
    auto* footer = new ttk::Frame(dialog, {{"padding", "14 8"}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* area = new ttk::Frame(dialog);
    area->pack({{"fill", "both"}, {"expand", true}});
    auto* viewport = new tk::Canvas(area, {{"highlightthickness", 0}, {"bg", forma_theme::PANEL}});
    auto* scrollbar = new ttk::Scrollbar(area, {{"command", viewport->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    viewport->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    viewport->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    auto* body = new ttk::Frame(viewport, {{"padding", 14}});
    int window = viewport->create_window(0, 0, {{"window", body->path()}, {"anchor", "nw"}});
    body->bind("<Configure>", [viewport](tk::Event&) {
        auto box = viewport->bbox("all");
        if (box) viewport->configure({{"scrollregion", std::format("{} {} {} {}", (long long)(*box)[0], (long long)(*box)[1], (long long)(*box)[2], (long long)(*box)[3])}});
        return std::string();
    });
    viewport->bind("<Configure>", [viewport, window](tk::Event& e) { viewport->itemconfigure(window, {{"width", e.width}}); return std::string(); });
    (new ttk::Label(body, {{"text", std::format("{}  ·  {:g} × {:g} × {:g} mm", group, num(cfg["width"]), num(cfg["depth"]), num(cfg["height"]))}, {"font", "{Segoe UI} 11 bold"}}))
        ->pack({{"anchor", "w"}, {"pady", "0 4"}});
    (new ttk::Label(body, {{"text", "Zmiany stosują się od razu po kliknięciu · Ctrl+Z cofa. Kliknij nagłówek sekcji, aby ją rozwinąć albo zwinąć. Kliknięcie innego mebla przełącza to okno na niego."}, {"wraplength", 520}}))
        ->pack({{"anchor", "w"}});
    std::string apply_text = row_now.size() > 1 ? std::format("Zmieniaj cały rząd szafek ({})", row_now.size()) : std::string("Zmieniaj cały rząd szafek");
    auto settings = build_settings(app, body, cfg, true, "Konfiguracja", apply_text);
    auto close = [dialog, app, keep = settings] {
        app->after(1, [dialog] {
            try { dialog->destroy(); } catch (const tk::TclError&) {}
            if (g_config_dialog == dialog) g_config_dialog = nullptr;
        });
    };
    auto* message = new ttk::Label(footer, {{"text", notice}, {"foreground", "#b3261e"}, {"wraplength", 420}, {"justify", "left"}});
    message->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Button(footer, {{"text", "Gotowe"}, {"style", "Accent.TButton"}, {"command", std::function<void()>(close)}}))->pack({{"side", "right"}});
    dialog->protocol("WM_DELETE_WINDOW", close);
    auto wheel = [viewport](tk::Event& event) { viewport->yview_scroll(event.delta > 0 ? -1 : 1, "units"); return std::string(); };
    std::function<void(tk::Widget*)> bind_wheel = [&](tk::Widget* widget) {
        for (auto* child : widget->winfo_children()) {
            std::string cls = child->winfo_class();
            if (cls != "TCombobox" && cls != "Treeview" && cls != "Text" && cls != "Listbox") child->bind("<MouseWheel>", wheel, true);
            bind_wheel(child);
        }
    };
    bind_wheel(body);
    // Rozmiar i położenie: przy prawej krawędzi okna programu, wysokość ograniczona do ekranu.
    dialog->update_idletasks();
    {
        int screen_w = app->winfo_screenwidth(), screen_h = app->winfo_screenheight();
        int want_w = std::clamp(body->winfo_reqwidth() + 24, 420, std::max(420, screen_w - 40));
        int want_h = std::min(body->winfo_reqheight() + footer->winfo_reqheight() + 8, std::max(320, screen_h - 120));
        int x = app->winfo_rootx() + app->winfo_width() - want_w - 12;
        int y = app->winfo_rooty() + 48;
        x = std::clamp(x, 0, std::max(0, screen_w - want_w - 8));
        y = std::clamp(y, 0, std::max(0, screen_h - want_h - 80));
        dialog->geometry(std::format("{}x{}+{}+{}", want_w, want_h, x, y));
        journal::event(app, "konfiguracja", std::format("okno {}×{} w ({}, {}); ekran {}×{}", want_w, want_h, x, y, screen_w, screen_h));
    }
    // Podgląd na żywo: każda zmiana pola od razu przebudowuje mebel (albo cały rząd), z osobnym krokiem cofania.
    std::weak_ptr<Settings> weak = settings;
    auto apply_now = [app, group, weak, dialog] {
        auto settings = weak.lock();
        if (!settings) return;
        Overrides ov = settings->collect();
        if (ov.values.empty()) return;
        try {
            std::vector<std::string> targets = ov.apply_first ? furniture_layout::row_groups(*app->project, group) : std::vector<std::string>{group};
            if (targets.empty()) targets = {group};
            journal::event(app, "konfiguracja", std::format("{}: zmiana {} · meble: {}", group, ov.values.dump(), targets.size()));
            core::ProjectPtr result = app->project;
            for (const auto& name : targets) {
                json config = furniture_configurator::read_config(*result, name);
                if (config.is_null()) continue;
                merge_overrides(config, ov.values);
                result = furniture_configurator::configure(*result, config, name, config.at("origin")).first;
            }
            app->snapshot();
            app->project = result;
            for (auto& part : app->project->parts) if (part.group == group) { app->selected = part.id; break; }
            app->refresh();
            settings->rebase();
            app->status.set(std::format("Skonfigurowano {} (szt. {}) · Ctrl+Z cofa zmianę", group, targets.size()));
        } catch (const std::exception& error) {
            std::string what = error.what();
            journal::event(app, "konfiguracja", "BŁĄD: " + what);
            if (what.find("Obróbka nie przecina") != std::string::npos)
                what = "Ta kombinacja ustawień nie jest możliwa do zbudowania (listwy usztywniające nie przecinają materiału) — zmiana cofnięta.";
            else
                what = "Nie udało się zastosować: " + what + " — zmiana cofnięta.";
            app->status.set(what);
            // Model się nie zmienił — odtwórz okno z aktualnym stanem mebla, żeby pola nie pokazywały nieudanej wartości.
            app->after(1, [app, group, what] { open_configuration(app, group, what); });
        }
    };
    auto pending = std::make_shared<bool>(false);
    settings->on_change = [app, pending, apply_now] {
        if (*pending) return;
        *pending = true;
        app->after(1, [pending, apply_now] { *pending = false; apply_now(); });
    };
}

}  // namespace stretch_choice
