// Atomic cabinet configurator with editable bays and provenance.
//
// UI API: default_config(index,project=None), normalize_config(config,project),
// read_config(project,group), infer_config(project,group),
// configuration_status(project,group), configure(project,config,group=None,origin=None).
// Height includes purchased legs and an optional worktop. Width/depth describe
// the carcass; front/back thickness and worktop overhang extend its envelope.
#include "furniture_configurator.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "assembly.h"
#include "configurator_accessories.h"
#include "dimension_model.h"
#include "drag_hardware.h"
#include "fittings.h"
#include "furniture_layout.h"
#include "furniture_library.h"
#include "opening_options.h"
#include "runner_installation.h"
#include "runners_catalog.h"
#include "workspace_worktops.h"

namespace furniture_configurator {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::add_template;
using core::finite;
using core::uid;
using furniture_library::TEMPLATES;
using py::ValueError;

const std::set<std::string> SUPPORTED{"cabinet", "shelf", "drawers", "island"};

namespace {

double num(const json& value) { return value.get<double>(); }

long long _integer(const json& value_, double lo, double hi, const std::string& label) {
    double value = finite(value_, lo, hi);
    if (value != std::trunc(value)) throw ValueError(label + " musi być liczbą całkowitą.");
    return (long long)value;
}

// a + b / a - b dla liczb Pythona: int (op) int pozostaje int
json add_numbers(const json& a, const json& b) {
    if (a.is_number_integer() && b.is_number_integer()) return a.get<long long>() + b.get<long long>();
    return num(a) + num(b);
}
json sub_numbers(const json& a, const json& b) {
    if (a.is_number_integer() && b.is_number_integer()) return a.get<long long>() - b.get<long long>();
    return num(a) - num(b);
}

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

Part& need(Project& project, const std::string& id) {
    Part* p = find_part(project, id);
    if (!p) throw py::StopIteration();  // next(...) bez wartości domyślnej
    return *p;
}

void _mark(Part& part, const std::string& key) { part.configurator_source = {{"schema", 1}, {"key", key}}; }

bool string_in(const json& value, std::initializer_list<const char*> options) {
    if (!value.is_string()) return false;
    for (const char* option : options) if (value == option) return true;
    return false;
}

// str.split(sep)[-1]
std::string last_piece(const std::string& text, const std::string& sep) {
    size_t at = text.rfind(sep);
    return at == std::string::npos ? text : text.substr(at + sep.size());
}

std::vector<json> default_sections(const json& template_index, const Project* project) {
    json config = default_config(template_index, project);  // nazwana zmienna: items()/iteracja po tymczasowym obiekcie wisiałaby
    std::vector<json> out; for (auto& s : config["sections"]) out.push_back(s);
    return out;
}

json section_defaults(const json& template_index, const Project* project, const std::string& key) {
    for (auto& s : default_sections(template_index, project)) if (s["key"] == key) return s;
    throw py::StopIteration();
}

}  // namespace

json default_config(const json& template_index_, const Project* project) {
    long long template_index = _integer(template_index_, 0, (double)TEMPLATES.size() - 1, "Wzorzec");
    const auto& tpl = furniture_library::template_at(template_index);
    const std::string& kind = tpl.kind;
    long long top = kind == "island" ? 38 : 0;
    json c = {{"schema", 1}, {"template_index", template_index}, {"name", tpl.name}, {"width", tpl.w}, {"height", tpl.h + 100 + top}, {"depth", tpl.d},
              {"thickness", 18}, {"front_thickness", 18}, {"gap", 2}, {"material", ""}, {"front_material", ""}, {"back", kind != "shelf"}, {"back_thickness", 3},
              {"worktop", top != 0}, {"worktop_thickness", top ? top : 28}, {"worktop_overhang", 20}, {"top_mode", "panel"}, {"rail_width", 80},
              {"leg_height", 100}, {"leg_count", 4}, {"leg_diameter", 40}, {"leg_setback", 60}, {"leg_color", "#242628"}, {"leg_price", 0},
              {"plinth", false}, {"plinth_setback", 40}, {"hinge_pattern", "hinge45"}, {"hinge_count", 2}, {"hinge_spring", "unknown"},
              {"handle_mode", "handle"}, {"handle_spacing", 128}, {"handle_color", "#242628"}, {"opening_angle", 100},
              {"runner_profile", ""}, {"runner_length", std::min<long long>(400, tpl.d - 30)}, {"drawer_side_thickness", 16}, {"drawer_bottom_thickness", 6},
              {"front_style", "smooth"}, {"front_border", 60}, {"front_depth", 3}, {"front_radius", 6},
              {"flute_width", 8}, {"flute_spacing", 20}, {"flute_count", 0}, {"finger_length", 120}, {"finger_width", 24}, {"finger_depth", 6},
              {"led", false}, {"led_mount", "under_shelf"}, {"led_width", 12}, {"led_depth", 3}, {"led_margin", 30},
              {"led_price_per_m", 0}, {"led_profile", true}, {"led_diffuser", true}, {"led_color", "#fff3bf"}, {"led_source", ""},
              {"origin", json::array({0, 0, 0})}, {"sections", json::array()}};
    for (const char* key : {"bottom", "main", "upper"}) {
        bool is_main = std::string(key) == "main";
        c["sections"].push_back({{"key", key}, {"enabled", is_main}, {"height", !is_main ? 200 : 0}, {"columns", 1},
                                 {"shelves", kind == "drawers" ? 0 : tpl.shelves}, {"front", kind == "drawers" ? "drawers" : !tpl.fronts ? "open" : "doors"},
                                 {"doors", std::min<long long>(2, std::max<long long>(1, tpl.fronts))}, {"drawers", kind == "drawers" ? tpl.fronts : 3}});
    }
    if (project) {
        c["material"] = project->materials.at(0).id;
        c["front_material"] = project->materials.at(std::min<size_t>(1, project->materials.size() - 1)).id;
    }
    json preset = furniture_library::template_options(template_index);
    json sections = preset.contains("sections") ? preset["sections"] : json::array();
    if (preset.contains("sections")) preset.erase("sections");
    json old_legs = c["leg_height"];
    for (auto& [k, v] : preset.items()) c[k] = v;
    c["height"] = add_numbers(c["height"], sub_numbers(c["leg_height"], old_legs));
    for (auto& options : sections) {
        for (auto& section : c["sections"]) {
            if (section["key"] == options.at("key")) for (auto& [k, v] : options.items()) section[k] = v;
        }
    }
    return c;
}

json default_config(const json& template_index, const Project& project) { return default_config(template_index, &project); }

json normalize_config(const json& config, const Project* project) {
    if (!config.is_object()) throw ValueError("Konfiguracja musi zawierać parametry mebla.");
    json c = default_config(py::get(config, "template_index", 0), project);
    for (auto& [k, v] : config.items()) c[k] = v;
    c["schema"] = 1;
    c["template_index"] = _integer(c["template_index"], 0, (double)TEMPLATES.size() - 1, "Wzorzec");
    if (!SUPPORTED.contains(TEMPLATES.at(c["template_index"].get<size_t>()).kind))
        throw ValueError("Szybki kreator obsługuje korpusy, regały, komody i wyspy. Ten wzorzec dodaj z pełnej biblioteki mebli.");
    if (!c["name"].is_string() || py::strip(c["name"].get<std::string>()).empty()) throw ValueError("Wpisz nazwę mebla.");
    c["name"] = py::strip(c["name"].get<std::string>());
    struct Range { const char* key; double lo, hi; };
    for (const Range& r : std::initializer_list<Range>{{"width", 100, 10000}, {"height", 100, 10000}, {"depth", 80, 10000}, {"thickness", 6, 60}, {"front_thickness", 6, 60},
                                                      {"gap", .1, 30}, {"back_thickness", .5, 40}, {"worktop_thickness", 1, 100}, {"worktop_overhang", 0, 300}, {"rail_width", 20, 300},
                                                      {"leg_height", 0, 1000}, {"leg_diameter", 10, 200}, {"leg_setback", 0, 500}, {"leg_price", 0, 100000},
                                                      {"plinth_setback", 0, 500}, {"handle_spacing", 0, 2000}, {"opening_angle", 0, 180},
                                                      {"runner_length", 50, 1500}, {"drawer_side_thickness", 6, 32}, {"drawer_bottom_thickness", 3, 20}})
        c[r.key] = finite(c[r.key], r.lo, r.hi);
    if (!string_in(c["top_mode"], {"panel", "rails"})) throw ValueError("Góra korpusu: pełna płyta albo listwy wzmacniające.");
    if (c["top_mode"] == "rails" && 2 * num(c["rail_width"]) > num(c["depth"])) throw ValueError("Dwie listwy nie mieszczą się w głębokości korpusu. Zmniejsz ich szerokość.");
    c["hinge_count"] = _integer(c["hinge_count"], 2, 8, "Liczba zawiasów"); c["leg_count"] = _integer(c["leg_count"], 4, 6, "Liczba nóg");
    if (c["leg_count"] != 4 && c["leg_count"] != 6) throw ValueError("Wybierz 4 albo 6 nóg.");
    for (const char* key : {"back", "worktop", "plinth", "led"})
        if (!c[key].is_boolean()) throw ValueError(std::string("Opcja ") + key + " wymaga TAK / NIE.");
    if (c["plinth"].get<bool>() && !py::truthy(c["leg_height"])) throw ValueError("Cokół wymaga dodatniej wysokości nóg.");
    for (const char* key : {"material", "front_material"}) {
        if (project) {
            bool known = false; for (auto& m : project->materials) if (c[key] == m.id) known = true;
            if (!known) throw ValueError("Wybierz istniejący materiał korpusu i frontów.");
        }
    }
    try { fittings::recipe(c["hinge_pattern"]); }
    catch (const py::StopIteration&) { throw ValueError("Wybierz obsługiwany wzorzec zawiasu."); }
    if (c["hinge_pattern"] == "hinge_sensys_tb55" || c["hinge_pattern"].get<std::string>().find("hinge") == std::string::npos) throw ValueError("Wybierz obsługiwany zawias nakładany.");
    if (!string_in(c["hinge_spring"], {"unknown", "sprung", "unsprung"})) throw ValueError("Stan zawiasu: unknown, sprung albo unsprung.");
    if (py::truthy(c["runner_profile"])) {
        if (!c["runner_profile"].is_string() || !runners_catalog::PROFILES.contains(c["runner_profile"].get<std::string>())) throw ValueError("Wybierz profil prowadnicy z katalogu.");
    }
    if (!(string_in(c["handle_mode"], {"handle", "none", "keep"}) || (c["handle_mode"].is_string() && opening_options::OPTIONS.contains(c["handle_mode"].get<std::string>()))))
        throw ValueError("Nieznany sposób chwytania / otwierania frontu.");
    if (!c["origin"].is_array() || c["origin"].size() != 3) throw ValueError("Pozycja korpusu wymaga X, Y i Z.");
    { json origin = json::array(); for (auto& v : c["origin"]) origin.push_back(finite(v)); c["origin"] = origin; }
    double body_height = num(c["height"]) - num(c["leg_height"]) - (c["worktop"].get<bool>() ? num(c["worktop_thickness"]) : 0);
    if (std::min({num(c["width"]), num(c["depth"]), body_height}) <= 2 * num(c["thickness"]) + 20) throw ValueError("Wymiary mebla są zbyt małe dla grubości płyt i nóg.");
    json sections = py::get(config, "sections", c["sections"]);
    if (!sections.is_array() || !(1 <= sections.size() && sections.size() <= 3)) throw ValueError("Kreator wymaga od jednej do trzech stref.");
    std::map<std::string, json> by_key;
    static const char* const ORDER[3] = {"bottom", "main", "upper"};
    for (size_t i = 0; i < sections.size(); ++i) {
        const json& section = sections[i];
        if (!section.is_object()) throw ValueError("Nieprawidłowa strefa mebla.");
        json key = py::get(section, "key", sections.size() == 1 ? json("main") : json(ORDER[i]));
        if (!string_in(key, {"bottom", "main", "upper"}) || by_key.contains(key.get<std::string>())) throw ValueError("Strefy: dół, główna i pawlacz, każda najwyżej raz.");
        std::string key_name = key.get<std::string>();
        json defaults = section_defaults(c["template_index"], project, key_name);
        json s = py::merged(defaults, section); s["key"] = key_name;
        if (!s["enabled"].is_boolean()) throw ValueError("Włączenie strefy wymaga TAK / NIE.");
        s["columns"] = _integer(s["columns"], 1, 8, "Liczba kolumn");
        {
            // Szerokości modułów w rzędzie [mm]: gdy liczba się nie zgadza lub są nieprawidłowe, moduły są równe; suma zawsze równa szerokości mebla.
            long long n = s["columns"].get<long long>();
            double total_width = num(c["width"]);
            std::vector<double> widths;
            const json cw = py::get(s, "column_widths");
            bool valid = cw.is_array() && (long long)cw.size() == n;
            double sum = 0;
            if (valid) for (const auto& v : cw) { if (!v.is_number() || v.get<double>() <= 0) { valid = false; break; } sum += v.get<double>(); }
            for (long long i = 0; i < n; ++i) widths.push_back(valid ? cw[(size_t)i].get<double>() * total_width / sum : total_width / n);
            json out = json::array();
            for (double v : widths) out.push_back(std::round(v * 10) / 10);
            s["column_widths"] = out;
        } s["shelves"] = _integer(s["shelves"], 0, 8, "Liczba półek");
        s["doors"] = _integer(s["doors"], 1, 2, "Liczba drzwi"); s["drawers"] = _integer(s["drawers"], 1, 8, "Liczba szuflad");
        if (!string_in(s["front"], {"open", "doors", "drawers"})) throw ValueError("Strefa: otwarta, drzwi albo szuflady.");
        s["height"] = finite(s["height"], 0, 10000);
        by_key[key_name] = s;
    }
    if (!by_key.contains("main") || !by_key["main"]["enabled"].get<bool>()) throw ValueError("Włącz główną strefę mebla.");
    for (const char* key : {"bottom", "upper"}) {
        if (!by_key.contains(key)) { json s = section_defaults(c["template_index"], project, key); s["enabled"] = false; by_key[key] = s; }
    }
    { std::vector<double> heights; for (const char* key : {"bottom", "upper"}) if (by_key[key]["enabled"].get<bool>()) heights.push_back(num(by_key[key]["height"]));
      by_key["main"]["height"] = body_height - py::sum(heights); }
    c["sections"] = json::array(); for (const char* key : ORDER) c["sections"].push_back(by_key[key]);
    for (auto& s : c["sections"]) {
        if (s["enabled"].get<bool>() && num(s["height"]) <= 2 * num(c["thickness"]) + 30) throw ValueError("Strefa " + s["key"].get<std::string>() + " jest zbyt niska po odjęciu pozostałych stref.");
    }
    c["front_style"] = configurator_accessories::normalize_front_style(c["front_style"], c)["style"];
    json accessories = configurator_accessories::normalize_accessories(c);
    for (auto& [k, v] : accessories.items()) c[k] = v;
    for (auto& s : c["sections"]) {
        if (py::truthy(py::get(s, "front_style"))) s["front_style"] = configurator_accessories::normalize_front_style(s["front_style"], c)["style"];
    }
    return c;
}

json normalize_config(const json& config, const Project& project) { return normalize_config(config, &project); }

json _geometry(const Part& part) {
    json data = json::object();
    for (const char* key : {"u", "v", "t", "plane", "x", "y", "z", "rx", "ry", "rz", "contour", "profile", "decor", "role"}) data[key] = part.get_attr(key);
    return py::sha256_hex(py::dumps(data, true, -1, true));
}

namespace {

// next((p.configurator_source for p in project.parts if p.group==group and p.configurator_source.get('config')), None)
const json* _source(const Project& project, const std::string& group) {
    for (auto& p : project.parts) if (p.group == group && py::truthy(py::get(p.configurator_source, "config"))) return &p.configurator_source;
    return nullptr;
}

ProjectPtr _build(const Project& project, const json& c, const std::string& group) {
    double t = num(c["thickness"]); double w = num(c["width"]), d = num(c["depth"]);
    double ox = num(c["origin"][0]), oy = num(c["origin"][1]), oz = num(c["origin"][2]); double floor = oz; oz += num(c["leg_height"]);
    double h = num(c["height"]) - num(c["leg_height"]) - (c["worktop"].get<bool>() ? num(c["worktop_thickness"]) : 0);
    ProjectPtr result = std::make_shared<Project>(project);
    std::vector<Part> carcass = add_template(*result, "shelf", w, h, d, t, 0, false, {ox, oy, oz}, group);
    static const std::map<std::string, std::pair<std::string, std::string>> mapping{{"Bok lewy", {"carcass.left", "left"}}, {"Bok prawy", {"carcass.right", "right"}},
                                                                                    {"Dno", {"carcass.bottom", "bottom"}}, {"Wieniec", {"carcass.top", "top"}}};
    std::map<std::string, std::string> core_ids;  // klucz -> id (wektor części może się realokować)
    for (const Part& created : carcass) {
        auto found = mapping.find(created.name);
        if (found == mapping.end()) throw py::KeyError("'" + created.name + "'");
        Part& p = need(*result, created.id);
        p.role = found->second.second; p.material = c["material"].get<std::string>(); _mark(p, found->second.first); core_ids[found->second.first] = p.id;
    }
    if (c["top_mode"] == "rails") {
        // The front rail remains the bay's upper support. Both real strips
        // reach the side panels and keep the carcass datum unchanged.
        Part& top = need(*result, core_ids["carcass.top"]);
        top.v = num(c["rail_width"]); top.name = "Listwa górna przednia";
        Part rear = top; rear.id = uid(); rear.name = "Listwa górna tylna"; rear.y = oy + d - num(c["rail_width"]);
        _mark(rear, "carcass.rail_back"); result->parts.push_back(rear);
    }
    auto add = [&](const std::string& key, const std::string& name, double u, double v, double thick, const std::string& plane, double x, double y, double z,
                   const std::string& role = "other", const std::string& material = "") -> std::string {
        Part p; p.name = name; p.group = group; p.u = u; p.v = v; p.t = thick; p.plane = plane; p.x = x; p.y = y; p.z = z; p.role = role;
        p.material = !material.empty() ? material : c["material"].get<std::string>();
        _mark(p, key); p.validate(); result->parts.push_back(p); return p.id;
    };
    if (c["back"].get<bool>()) {
        std::string back_material = c["material"].get<std::string>();
        for (auto& m : result->materials) if (m.kind == "hdf") { back_material = m.id; break; }
        add("carcass.back", "Plecy nakładane HDF", w, h, num(c["back_thickness"]), "XZ", ox, oy + d, oz, "back", back_material);
    }
    if (c["worktop"].get<bool>()) {
        double over = num(c["worktop_overhang"]);
        add("carcass.worktop", "Blat", w + 2 * over, d + 2 * over, num(c["worktop_thickness"]), "XY", ox - over, oy - over, oz + h, "worktop");
    }
    if (c["plinth"].get<bool>()) {
        if (num(c["plinth_setback"]) + t > d) throw ValueError("Cofnięcie cokołu wykracza poza korpus.");
        add("carcass.plinth", "Cokół", w, num(c["leg_height"]), t, "XZ", ox, oy + num(c["plinth_setback"]), floor, "accessory_panel");
    }
    std::vector<json> active; for (auto& s : c["sections"]) if (s["enabled"].get<bool>()) active.push_back(s);
    std::vector<std::string> boards{core_ids["carcass.bottom"]}; double boundary = oz;
    for (size_t i = 0; i + 1 < active.size(); ++i) {
        const json& section = active[i];
        boundary += num(section["height"]);
        boards.push_back(add("separator." + section["key"].get<std::string>(), "Przegroda pozioma — " + section["key"].get<std::string>(), w - 2 * t, d, t, "XY", ox + t, oy, boundary - t / 2, "partition"));
    }
    boards.push_back(core_ids["carcass.top"]);
    for (size_t row = 0; row < active.size(); ++row) {
        const json& section = active[row];
        const std::string lower_id = boards[row], upper_id = boards[row + 1]; std::string key = section["key"].get<std::string>();
        std::vector<std::string> column_parts{core_ids["carcass.left"]};
        long long columns = section["columns"].get<long long>();
        const json& module_widths = section["column_widths"];
        double module_edge = 0;
        for (long long col = 1; col < columns; ++col) {
            module_edge += module_widths[(size_t)(col - 1)].get<double>();
            const Part& lower = need(*result, lower_id); const Part& upper = need(*result, upper_id);
            double lower_z = lower.z, lower_t = lower.t, upper_z = upper.z;
            column_parts.push_back(add(key + std::format(".divider.{}", col), "Przegroda pionowa — " + key + std::format(" {}", col), d, upper_z - lower_z - lower_t, t, "YZ",
                                       ox + module_edge - t / 2, oy, lower_z + lower_t, "partition"));
        }
        column_parts.push_back(core_ids["carcass.right"]);
        for (long long col = 0; col < columns; ++col) {
            json bay = {{"left", column_parts[col]}, {"right", column_parts[col + 1]}, {"bottom", lower_id}, {"top", upper_id},
                        {"left_mid", col > 0}, {"right_mid", col < columns - 1}, {"bottom_mid", row > 0}, {"top_mid", row < active.size() - 1}};
            assembly::Frame f = assembly::frame(*result, group, std::nullopt, bay);
            core::Point3 lo = f.lower, hi = f.upper; double gap = num(c["gap"]); std::string bay_key = key + std::format(".bay.{}", col);
            long long shelves = section["shelves"].get<long long>();
            std::string front_kind = section["front"].get<std::string>();
            if (front_kind == "drawers" && shelves) throw ValueError("Strefa szuflad nie może jednocześnie zawierać półek. Wybierz inną strefę dla półek.");
            for (long long index = 1; index <= shelves; ++index) {
                double level = f.inner_bottom - lo[2] + (f.inner_top - f.inner_bottom - t) * index / (shelves + 1);
                Part shelf; shelf.name = "Półka — " + key + std::format(" {}.{}", col + 1, index); shelf.u = f.inner_right - f.inner_left - 2 * gap; shelf.v = d - 2 * gap; shelf.t = t;
                shelf.material = c["material"].get<std::string>();
                auto attached = assembly::attach(*result, shelf, group, "shelf", {{"bay", bay}, {"gap", gap}, {"level", level}, {"bay_shelves", shelves}, {"bay_shelf_index", index}});
                result = attached.first; _mark(*attached.second, bay_key + std::format(".shelf.{}", index));
            }
            if (front_kind == "open") continue;
            long long count = front_kind == "doors" ? section["doors"].get<long long>() : section["drawers"].get<long long>();
            for (long long index = 0; index < count; ++index) {
                bool is_door = front_kind == "doors"; std::string front_key = bay_key + (is_door ? ".door." : ".drawer.") + std::to_string(index);
                double width = is_door ? (hi[0] - lo[0] - (count + 1) * gap) / count : hi[0] - lo[0] - 2 * gap;
                double height = is_door ? hi[2] - lo[2] - 2 * gap : (hi[2] - lo[2] - (count + 1) * gap) / count;
                std::string hinge_side = index == 0 ? "left" : "right";
                json cfg = {{"bay", bay}, {"gap", gap}, {"hinge_side", hinge_side}, {"hinge_count", c["hinge_count"]}, {"hinge_pattern", c["hinge_pattern"]},
                            {"hinge_spring", py::get(c, "hinge_spring", "unknown")}};
                Part front; front.name = std::string(is_door ? "Drzwi" : "Front szuflady") + " — " + key + std::format(" {}.{}", col + 1, index + 1);
                front.u = width; front.v = height; front.t = num(c["front_thickness"]); front.material = c["front_material"].get<std::string>();
                std::string front_id;
                if (is_door) {
                    cfg["bay_doors"] = count;
                    auto attached = assembly::attach(*result, front, group, "door", cfg);
                    result = attached.first; front_id = attached.second->id;
                    result = dimension_model::regenerate_hinges(*result, front_id, c["hinge_pattern"], c["hinge_count"], hinge_side);
                } else {
                    double level = gap + index * (height + gap); double raise_by = std::max(0.0, f.inner_bottom - (lo[2] + level + gap)) + 6;
                    double drawer_height = std::min(120.0, height - num(c["drawer_bottom_thickness"]) - 2 * gap - raise_by);
                    if (drawer_height < 20) throw ValueError("Front szuflady jest za niski dla skrzynki. Zmniejsz liczbę szuflad lub zwiększ strefę.");
                    json drawer_values = {{"level", level}, {"drawer_raise", raise_by}, {"drawer_height", drawer_height}, {"drawer_t", c["drawer_side_thickness"]},
                                          {"drawer_bottom", c["drawer_bottom_thickness"]}, {"drawer_clearance", 12.7}, {"drawer_depth", c["runner_length"]},
                                          {"bay_drawers", count}, {"bay_drawer_index", index}};
                    for (auto& [k, v] : drawer_values.items()) cfg[k] = v;
                    if (py::truthy(c["runner_profile"])) {
                        json runner_values = {{"auto_hardware", false}, {"drawer_front_gap", 0}, {"runner_profile", c["runner_profile"]}, {"runner_nominal", c["runner_length"]}};
                        for (auto& [k, v] : runner_values.items()) cfg[k] = v;
                    }
                    auto attached = assembly::attach(*result, front, group, "drawer", cfg);
                    result = attached.first; front_id = attached.second->id;
                    if (py::truthy(c["runner_profile"]))
                        result = runner_installation::apply_runner(*result, front_id, c["runner_profile"], c["runner_length"], c["drawer_side_thickness"], c["drawer_bottom_thickness"]);
                }
                {
                    Part& placed = need(*result, front_id); _mark(placed, front_key);
                    for (auto& child : result->parts) {
                        if (py::get(child.assembly, "generated_by") == front_id) _mark(child, front_key + ".box." + last_piece(child.name, " / "));
                    }
                    if (is_door) placed.motion = {{"mode", "hinge_" + hinge_side}, {"angle", c["opening_angle"]}, {"travel", 0}};
                    else if (!py::truthy(placed.motion)) placed.motion = {{"mode", "drawer"}, {"angle", 0}, {"travel", num(c["runner_length"]) * .9}};
                }
                json effective_style = configurator_accessories::normalize_front_style(py::get(section, "front_style", c["front_style"]), c)["style"];
                if (c["handle_mode"] == "handle" && effective_style != "finger") {
                    double u = !is_door ? width / 2 : hinge_side == "left" ? width - 35 : 35;
                    json spacing = c["handle_spacing"]; bool vertical = is_door;
                    if (py::truthy(spacing) && num(spacing) > (vertical ? height : width) - 20) throw ValueError("Rozstaw uchwytu nie mieści się na froncie. Zmniejsz rozstaw lub wybierz gałkę.");
                    result = fittings::add_handle(*result, front_id, spacing, u, height / 2, vertical, c["handle_color"]);
                } else if (!string_in(c["handle_mode"], {"handle", "none", "keep"}) && effective_style != "finger") {
                    result = opening_options::install(*result, front_id, c["handle_mode"].get<std::string>(), c["finger_length"], c["finger_width"], c["finger_depth"], c["hinge_spring"]);
                }
                Part& finished = need(*result, front_id);
                configurator_accessories::apply_front_to_part(*result, finished, effective_style, c);
            }
        }
    }
    result = configurator_accessories::install_accessories(*result, group, c,
        {{"floor", floor}, {"body_x", ox}, {"body_y", oy}, {"body_z", oz}, {"width", w}, {"depth", d}, {"body_height", h}}, false);
    for (auto& p : result->parts) {
        if (p.group == group) for (auto& op : p.operations) op["config_auto"] = true;
    }
    std::set<std::string> ids; for (auto& p : result->parts) if (p.group == group) ids.insert(p.id);
    for (auto& item : result->hardware) {
        json owner = py::get(item, "owner_id");
        if (owner.is_string() && ids.contains(owner.get<std::string>())) item["config_auto"] = true;
    }
    return result;
}

}  // namespace

json read_config(const Project& project, const std::string& group) {
    const json* source = _source(project, group);
    return source ? json(source->at("config")) : json(nullptr);
}

json configuration_status(const Project& project, const std::string& group) {
    const json* source = _source(project, group);
    if (!source) return {{"configured", false}, {"editable", false}, {"reason", "Ten mebel nie pochodzi z szybkiego kreatora. Możesz rozpoznać wzorzec biblioteki przyciskiem Konwertuj."}};
    std::map<std::string, const Part*> by_id; for (auto& p : project.parts) if (p.group == group) by_id[p.id] = &p;
    json manifest = py::get(*source, "manifest", json::object());
    for (auto& [key, entry] : manifest.items()) {
        auto found = by_id.find(entry.at("id").get<std::string>());
        if (found == by_id.end()) return {{"configured", true}, {"editable", false}, {"reason", "Usunięto część kreatora: " + key + ". Utwórz nowy mebel lub użyj zwykłych edytorów."}};
        if (_geometry(*found->second) != entry.at("signature"))
            return {{"configured", true}, {"editable", false}, {"reason", "Geometrię części „" + found->second->name + "” zmieniono ręcznie. Kreator jej nie nadpisze; użyj zwykłych edytorów lub utwórz nowy mebel."}};
    }
    return {{"configured", true}, {"editable", true}, {"reason", "Konfiguracja dostępna; ręczne dodatki i obróbki zostaną zachowane."}};
}

void _remap(Project& project, const std::map<std::string, std::string>& mapping) {
    auto mapped = [&](const json& value) -> json {
        if (value.is_string()) { auto found = mapping.find(value.get<std::string>()); if (found != mapping.end()) return found->second; }
        return value;
    };
    for (auto& p : project.parts) {
        { auto found = mapping.find(p.id); if (found != mapping.end()) p.id = found->second; }
        for (const char* key : {"anchors", "children"}) {
            if (p.assembly.contains(key)) { json items = json::array(); for (auto& i : p.assembly[key]) items.push_back(mapped(i)); p.assembly[key] = items; }
        }
        if (py::truthy(py::get(p.assembly, "generated_by"))) p.assembly["generated_by"] = mapped(p.assembly["generated_by"]);
        json* config = p.assembly.contains("config") ? &p.assembly["config"] : nullptr;
        if (config && config->is_object() && py::truthy(py::get(*config, "bay"))) {
            json& bay = (*config)["bay"];
            for (const char* key : {"left", "right", "bottom", "top"}) bay[key] = mapped(bay.at(key));
        }
        for (json* collection : {&p.operations, &p.machining}) {
            for (auto& op : *collection) {
                for (const char* key : {"owner_id", "opening_owner"}) if (py::truthy(py::get(op, key))) op[key] = mapped(op[key]);
            }
        }
    }
    for (auto& item : project.hardware) {
        if (py::truthy(py::get(item, "owner_id"))) item["owner_id"] = mapped(item["owner_id"]);
        if (item.contains("anchors")) { json items = json::array(); for (auto& i : item["anchors"]) items.push_back(mapped(i)); item["anchors"] = items; }
    }
}

std::pair<ProjectPtr, std::string> configure(const Project& project, const json& config, const std::optional<std::string>& group_, const json& origin) {
    // Return a complete candidate and group. Input and Undo remain untouched.
    json c = normalize_config(config, project);
    std::optional<std::string> original_group = group_;
    bool has_group = group_ && !group_->empty();
    std::vector<const Part*> existing; if (has_group) for (auto& p : project.parts) if (p.group == *group_) existing.push_back(&p);
    json old_config = !existing.empty() ? read_config(project, *group_) : json(nullptr);
    bool legacy = false;
    json layout = json::object(); std::string group = group_ ? *group_ : "";
    if (!existing.empty()) {
        if (py::truthy(old_config)) {
            json status = configuration_status(project, group);
            if (!status["editable"].get<bool>()) throw ValueError(status["reason"].get<std::string>());
        } else {
            old_config = infer_config(project, group); legacy = true;
        }
        if (origin.is_null()) c["origin"] = old_config.at("origin");
        else { json o = json::array(); for (auto& v : origin) o.push_back(finite(v)); c["origin"] = o; }
        if (c["origin"].size() != 3) throw ValueError("Pozycja korpusu wymaga X, Y i Z.");
        layout = furniture_layout::group_layout(project, group);
        if (c["name"] != group) {
            std::string wanted = c["name"].get<std::string>();
            for (auto& p : project.parts) if (p.group == wanted) throw ValueError("Mebel o tej nazwie już istnieje. Wpisz inną nazwę.");
            group = wanted;
        }
    } else {
        std::string base = c["name"].get<std::string>(); group = base; int number = 2;
        std::set<std::string> occupied; for (auto& p : project.parts) occupied.insert(p.group);
        while (occupied.contains(group)) { group = base + std::format(" ({})", number); ++number; }
    }
    c["name"] = group;
    ProjectPtr baseline = std::make_shared<Project>(project);
    // old_by_key: dict z kolejnością wstawiania (klucz -> część projektu wejściowego)
    std::vector<std::pair<std::string, const Part*>> old_by_key;
    auto old_lookup = [&](const std::string& key) -> const Part* { for (auto& [k, p] : old_by_key) if (k == key) return p; return nullptr; };
    auto old_set = [&](const std::string& key, const Part* part) { for (auto& [k, p] : old_by_key) if (k == key) { p = part; return; } old_by_key.push_back({key, part}); };
    for (const Part* p : existing) {
        json key = py::get(p->configurator_source, "key");
        if (py::truthy(key)) old_set(key.get<std::string>(), p);
    }
    std::set<std::string> existing_ids; for (const Part* p : existing) existing_ids.insert(p->id);
    std::set<std::string> old_managed; for (auto& [k, p] : old_by_key) old_managed.insert(p->id);
    if (legacy) old_managed = existing_ids;
    std::vector<Part> manual; for (const Part* p : existing) if (!old_managed.contains(p->id)) manual.push_back(*p);
    std::vector<json> retained;
    for (auto& h : baseline->hardware) {
        json owner = py::get(h, "owner_id");
        if (owner.is_string() && existing_ids.contains(owner.get<std::string>()) && !py::truthy(py::get(h, "config_auto"))) retained.push_back(h);
    }
    if (legacy) retained.clear();
    {
        const std::string skipped = existing.empty() ? group : original_group.value_or("");
        std::erase_if(baseline->parts, [&](const Part& p) { return p.group == skipped; });
        json kept = json::array();
        for (auto& h : baseline->hardware) { json owner = py::get(h, "owner_id"); if (!(owner.is_string() && existing_ids.contains(owner.get<std::string>()))) kept.push_back(h); }
        baseline->hardware = kept;
    }
    ProjectPtr result = _build(*baseline, c, group);
    auto built_ids = [&]() { std::vector<std::string> ids; for (auto& p : result->parts) if (p.group == group) ids.push_back(p.id); return ids; };
    std::vector<std::string> built = built_ids();
    if (legacy) {
        // Explicit conversion retains IDs of matching structural roles and fronts.
        std::vector<const Part*> available = existing;
        for (const std::string& id : built) {
            const Part& p = need(*result, id);
            std::string part_key = p.configurator_source["key"].get<std::string>();
            auto take = [&](auto predicate) -> const Part* { for (const Part* q : available) if (predicate(*q)) return q; return nullptr; };
            const Part* match = take([&](const Part& q) { return q.name == p.name; });
            if (!match && part_key.find(".shelf.") != std::string::npos) {
                std::string label = "Półka " + part_key.substr(part_key.rfind(".shelf.") + 7);
                match = take([&](const Part& q) { return q.name == label; });
            }
            if (!match && part_key.find(".box.") != std::string::npos) {
                size_t at = part_key.find(".box."); std::string parent_key = part_key.substr(0, at), suffix = part_key.substr(at + 5);
                const Part* parent = old_lookup(parent_key);
                if (parent) match = take([&](const Part& q) { return py::get(q.assembly, "generated_by") == parent->id && last_piece(q.name, " / ") == suffix; });
            }
            if (!match) match = take([&](const Part& q) { return assembly::inferred_role(q) == p.role && q.plane == p.plane && std::abs(q.x - p.x) < 1e-6 && std::abs(q.z - p.z) < 1e-6; });
            if (match) { old_set(p.configurator_source["key"].get<std::string>(), match); available.erase(std::find(available.begin(), available.end(), match)); }
        }
    }
    std::set<std::string> new_keys; for (auto& id : built) new_keys.insert(py::get(need(*result, id).configurator_source, "key").is_string() ? need(*result, id).configurator_source["key"].get<std::string>() : "");
    for (auto& [key, p] : old_by_key) {
        if (new_keys.contains(key)) continue;
        bool has_manual = !p->holes.empty() || py::truthy(p->fillets) || py::truthy(p->chamfers);
        for (const json* collection : {&p->operations, &p->machining}) for (auto& o : *collection) if (!py::truthy(py::get(o, "config_auto"))) has_manual = true;
        for (auto& h : retained) if (py::get(h, "owner_id") == p->id) has_manual = true;
        if (has_manual) throw ValueError("Zmiana usuwa część „" + p->name + "” z ręczną obróbką lub okuciem. Zachowaj tę część albo usuń jej dodatki świadomie w zwykłym edytorze.");
    }
    std::map<std::string, std::string> mapping;
    for (auto& id : built) {
        const Part& p = need(*result, id); json key = py::get(p.configurator_source, "key");
        if (key.is_string()) { if (const Part* old = old_lookup(key.get<std::string>())) mapping[p.id] = old->id; }
    }
    _remap(*result, mapping); built = built_ids();
    for (auto& id : built) {
        Part& p = need(*result, id); p.layout = layout;
        json key = py::get(p.configurator_source, "key"); const Part* old = key.is_string() ? old_lookup(key.get<std::string>()) : nullptr;
        if (old) {
            if (key != "carcass.top" || !(old->name == "Wieniec" || old->name == "Listwa górna przednia")) p.name = old->name;
            p.visible = old->visible; p.grain = old->grain; p.edges = old->edges; p.contour_edge = old->contour_edge;
            const char* material_key = p.role == "door" || p.role == "drawer" ? "front_material" : "material";
            if (py::get(old_config, material_key) == c[material_key]) p.material = old->material;
            p.holes = old->holes; p.fillets = old->fillets; p.chamfers = old->chamfers;
            if (!legacy) {
                for (auto& o : old->operations) if (!py::truthy(py::get(o, "config_auto"))) p.operations.push_back(o);
                for (auto& o : old->machining) if (!py::truthy(py::get(o, "config_auto"))) p.machining.push_back(o);
            }
        }
    }
    for (Part& p : manual) {
        p.group = group;
        if (py::truthy(py::get(p.assembly, "group"))) p.assembly["group"] = group;
    }
    std::vector<std::string> manual_ids; for (auto& p : manual) manual_ids.push_back(p.id);
    for (auto& p : manual) result->parts.push_back(p);
    for (auto& h : retained) result->hardware.push_back(h);
    const std::string original_text = original_group ? *original_group : "None";
    const json original_json = original_group ? json(*original_group) : json(nullptr);
    for (auto& p : result->parts) {
        if (p.group != group) continue;
        if (py::truthy(py::get(p.assembly, "group"))) p.assembly["group"] = group;
        for (auto& op : p.operations) if (py::get(op, "connection_tag") == "connector:" + original_text) op["connection_tag"] = "connector:" + group;
    }
    for (auto& item : result->hardware) {
        json owner = py::get(item, "owner_id");
        if ((owner.is_string() && existing_ids.contains(owner.get<std::string>())) || py::get(item, "group") == original_json) {
            if (item.contains("group")) item["group"] = group;
            if (py::get(item, "connection_tag") == "connector:" + original_text) item["connection_tag"] = "connector:" + group;
        }
    }
    std::set<std::string> managed_ids(built.begin(), built.end());
    for (auto& item : result->hardware) {
        json owner = py::get(item, "owner_id");
        if (!(owner.is_string() && managed_ids.contains(owner.get<std::string>()))) continue;
        const json* old_item = nullptr;
        for (auto& h : project.hardware) {
            if (py::get(h, "owner_id") == py::get(item, "owner_id") && py::get(h, "kind") == py::get(item, "kind") && (py::truthy(py::get(h, "config_auto")) || legacy)) { old_item = &h; break; }
        }
        if (old_item && !old_item->empty()) {
            for (const char* key : {"price", "article", "manufacturer", "supplier", "note"}) {
                static const std::map<std::string, std::string> price_keys{{"leg", "leg_price"}, {"led_strip", "led_price_per_m"}, {"led_profile", "led_profile_price_per_m"},
                                                                          {"led_diffuser", "led_diffuser_price_per_m"}, {"led_power", "led_source_price"}};
                json kind = py::get(item, "kind"); std::string price_key;
                if (kind.is_string()) { auto found = price_keys.find(kind.get<std::string>()); if (found != price_keys.end()) price_key = found->second; }
                if (std::string(key) == "price" && !price_key.empty() && py::get(old_config, price_key) != py::get(c, price_key)) continue;
                if (std::string(key) == "article" || std::string(key) == "manufacturer") {
                    bool different = false;
                    for (const char* k : {"runner_profile", "source", "spacing", "recipe_id"}) if (py::get(*old_item, k) != py::get(item, k)) different = true;
                    if (different) continue;
                }
                if (old_item->contains(key)) item[key] = (*old_item)[key];
            }
        }
    }
    for (const std::string& id : manual_ids) {
        auto [lo, hi] = assembly::bounds(need(*result, id));
        for (const std::string& built_id : built) {
            auto [a, b] = assembly::bounds(need(*result, built_id));
            bool collides = true; for (int i = 0; i < 3; ++i) if (!(std::min(hi[i], b[i]) - std::max(lo[i], a[i]) > .01)) collides = false;
            if (collides) throw ValueError("Nowa konstrukcja koliduje z ręcznym elementem „" + need(*result, id).name + "”. Przesuń dodatek przed przebudową.");
        }
    }
    json manifest = json::object();
    for (auto& id : built) { Part& p = need(*result, id); manifest[p.configurator_source["key"].get<std::string>()] = {{"id", p.id}, {"signature", _geometry(p)}}; }
    Part* anchor = nullptr;
    for (auto& id : built) { Part& p = need(*result, id); if (py::get(p.configurator_source, "key") == "carcass.left") { anchor = &p; break; } }
    if (!anchor) throw py::StopIteration();
    anchor->configurator_source["config"] = c; anchor->configurator_source["manifest"] = manifest;
    std::set<std::string> changed{group}; if (original_group) changed.insert(*original_group);
    result = workspace_worktops::refresh_shared_worktops(result, changed, false);
    drag_hardware::refresh_manual_attachments(*result);
    result->validate();
    return {result, group};
}

json infer_config(const Project& project, const std::string& group) {
    // Recognise an unchanged library cabinet for an explicitly requested conversion.
    std::vector<const Part*> parts; for (auto& p : project.parts) if (p.group == group) parts.push_back(&p);
    if (parts.empty()) throw ValueError("Wybierz mebel do rozpoznania.");
    for (const Part* p : parts)
        if (py::truthy(p->style_source) || !p->contour.empty() || py::truthy(p->profile) || py::truthy(p->decor) || py::truthy(p->fillets) || py::truthy(p->chamfers) || py::truthy(p->machining))
            throw ValueError("Ten mebel zawiera własną geometrię lub obróbki. Konwertowanie mogłoby ją nadpisać; utwórz nowy korpus w kreatorze.");
    assembly::Frame f;
    try { f = assembly::frame(project, group); }
    catch (const ValueError&) { throw ValueError("Nie rozpoznano bibliotecznego korpusu. Utwórz nowy mebel w kreatorze."); }
    core::Point3 lo = f.lower, hi = f.upper; double w = hi[0] - lo[0], d = hi[1] - lo[1], h = hi[2] - lo[2];
    if (!f.left) throw ValueError("Nie rozpoznano boku bibliotecznego korpusu.");
    const Part* left_part = nullptr;
    for (auto& p : project.parts) if (p.id == *f.left) { left_part = &p; break; }
    if (!left_part) throw py::StopIteration();
    const Part& left = *left_part;
    std::string material = left.material; std::string front_material = material;
    for (const Part* p : parts) { std::string role = assembly::inferred_role(*p); if (role == "door" || role == "drawer") { front_material = p->material; break; } }
    for (size_t index = 0; index < TEMPLATES.size(); ++index) {
        const auto& tpl = TEMPLATES[index];
        if (!SUPPORTED.contains(tpl.kind) || !furniture_library::template_options((long long)index).empty()) continue;
        ProjectPtr expected;
        try {
            Project clean; clean.materials = project.materials;
            expected = furniture_library::create_furniture(clean, (long long)index, w, h, d, left.t, lo, group, material, front_material).first;
        } catch (const ValueError&) { continue; }
        std::map<std::string, const Part*> expected_parts; for (auto& p : expected->parts) expected_parts[p.name] = &p;
        std::set<std::string> actual_names; for (const Part* p : parts) actual_names.insert(p->name);
        std::set<std::string> expected_names; for (auto& [n, p] : expected_parts) expected_names.insert(n);
        if (expected_parts.size() != parts.size() || actual_names != expected_names) continue;
        // Legacy structural roles were inferred from names; geometry is compared
        // after normalising only that legacy role information.
        bool matches = true;
        auto operation_signature = [](const json& op) { json out = json::object(); for (auto& [k, v] : op.items()) if (k != "owner_id" && k != "opening_owner" && k != "batch") out[k] = v; return out; };
        for (const Part* original : parts) {
            Part q = *original, e = *expected_parts[original->name];
            q.role = e.role = "other"; q.decor = e.decor = json::object();
            if (_geometry(q) != _geometry(e)) { matches = false; break; }
            std::vector<json> a, b; for (auto& op : original->operations) a.push_back(operation_signature(op));
            for (auto& op : expected_parts[original->name]->operations) b.push_back(operation_signature(op));
            if (a != b) { matches = false; break; }
        }
        if (!matches) continue;
        std::map<std::string, std::string> expected_owners, actual_owners;
        for (auto& p : expected->parts) expected_owners[p.id] = p.name;
        for (const Part* p : parts) actual_owners[p->id] = p->name;
        auto hardware_signature = [](const json& items, const std::map<std::string, std::string>& owners) {
            std::vector<std::string> result;
            for (auto& item : items) {
                json owner = py::get(item, "owner_id");
                if (!owner.is_string() || !owners.contains(owner.get<std::string>())) continue;
                json data = json::object();
                for (auto& [k, v] : item.items())
                    if (k != "owner_id" && k != "anchors" && k != "price" && k != "article" && k != "manufacturer" && k != "supplier" && k != "note") data[k] = v;
                data["owner_name"] = owners.at(item.at("owner_id").get<std::string>()); result.push_back(py::dumps(data, true, -1, true));
            }
            std::sort(result.begin(), result.end());
            return result;
        };
        if (hardware_signature(project.hardware, actual_owners) != hardware_signature(expected->hardware, expected_owners)) continue;
        json c = default_config((long long)index, &project);
        double front_t = left.t;
        for (const Part* p : parts) { std::string role = assembly::inferred_role(*p); if (role == "door" || role == "drawer") { front_t = p->t; break; } }
        json updates = {{"name", group}, {"width", w}, {"height", h + (tpl.kind == "island" ? 38 : 0)}, {"depth", d}, {"thickness", left.t}, {"front_thickness", front_t}, {"leg_height", 0},
                        {"material", material}, {"front_material", front_material}, {"origin", json::array({lo[0], lo[1], lo[2]})}};
        for (auto& [k, v] : updates.items()) c[k] = v;
        return normalize_config(c, &project);
    }
    throw ValueError("Ten korpus różni się od niezmienionego wzorca biblioteki. Nie przebudowano ręcznych części; dodaj nowy mebel w kreatorze.");
}

}  // namespace furniture_configurator
