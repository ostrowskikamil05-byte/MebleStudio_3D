// Parametric furniture library: closed production geometry and motion metadata.
#include "furniture_library.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "assembly.h"
#include "core.h"
#include "dimension_model.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "mechanisms.h"

namespace furniture_library {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::add_template;
using core::finite;
using py::ValueError;

// Editable design defaults, appended to preserve indices in older projects.
const std::vector<Template> TEMPLATES{
    {"Kuchnia", "Szafka dolna", 600, 720, 560, "cabinet", 1, 1},  // 0
    {"Kuchnia", "Szafka wisząca", 800, 720, 320, "cabinet", 1, 2},  // 1
    {"Kuchnia", "Szafka pod zlew", 900, 720, 560, "cabinet", 0, 2},  // 2
    {"Kuchnia", "Szafka 3 szuflady", 600, 720, 560, "drawers", 0, 3},  // 3
    {"Kuchnia", "Słupek cargo", 300, 1800, 560, "cargo", 0, 1},  // 4
    {"Kuchnia", "Słupek spiżarniany", 600, 2100, 560, "cabinet", 5, 2},  // 5
    {"Kuchnia", "Moduł pod piekarnik", 600, 720, 560, "cabinet", 1, 0},  // 6
    {"Kuchnia", "Wyspa z blatem", 1200, 900, 700, "island", 2, 2},  // 7
    {"Sypialnia", "Szafa dwudrzwiowa", 1200, 2100, 600, "cabinet", 4, 2},  // 8
    {"Sypialnia", "Szafa przesuwna", 1800, 2200, 650, "sliding", 4, 2},  // 9
    {"Sypialnia", "Komoda 3 szuflady", 900, 800, 480, "drawers", 0, 3},  // 10
    {"Sypialnia", "Szafka nocna", 450, 500, 400, "cabinet", 1, 1},  // 11
    {"Sypialnia", "Łóżko 160 × 200", 1640, 450, 2040, "bed", 0, 0},  // 12
    {"Sypialnia", "Toaletka", 1000, 760, 450, "desk", 0, 0},  // 13
    {"Salon", "Szafka RTV", 1600, 550, 450, "cabinet", 1, 2},  // 14
    {"Salon", "Witryna", 800, 1800, 400, "cabinet", 4, 2},  // 15
    {"Salon", "Regał książkowy", 900, 1900, 320, "shelf", 5, 0},  // 16
    {"Salon", "Stolik kawowy", 1000, 450, 600, "table", 0, 0},  // 17
    {"Salon", "Komoda z drzwiami", 1200, 900, 450, "cabinet", 2, 2},  // 18
    {"Łazienka", "Szafka pod umywalkę", 800, 600, 450, "cabinet", 0, 2},  // 19
    {"Łazienka", "Słupek łazienkowy", 400, 1800, 350, "cabinet", 4, 1},  // 20
    {"Łazienka", "Szafka wisząca z klapą", 600, 400, 250, "lift", 0, 1},  // 21
    {"Łazienka", "Regał na ręczniki", 500, 1500, 300, "shelf", 4, 0},  // 22
    {"Garaż i warsztat", "Regał warsztatowy", 1200, 1800, 500, "shelf", 4, 0},  // 23
    {"Garaż i warsztat", "Stół warsztatowy", 1600, 900, 700, "table", 0, 0},  // 24
    {"Garaż i warsztat", "Szafka narzędziowa", 800, 1000, 500, "cabinet", 2, 2},  // 25
    {"Garaż i warsztat", "Szafka na części — szuflady", 800, 900, 500, "drawers", 0, 3},  // 26
    {"Ogród", "Stół ogrodowy", 1600, 760, 800, "table", 0, 0},  // 27
    {"Ogród", "Ławka ogrodowa", 1400, 850, 500, "bench", 0, 0},  // 28
    {"Ogród", "Skrzynia ogrodowa z pokrywą", 1000, 550, 500, "chest", 0, 0},  // 29
    {"Ogród", "Regał na donice", 800, 1400, 400, "shelf", 3, 0},  // 30
    {"Biuro i przedpokój", "Biurko", 1200, 760, 600, "desk", 0, 0},  // 31
    {"Biuro i przedpokój", "Szafka na buty", 800, 1000, 350, "cabinet", 4, 2},  // 32
    {"Biuro i przedpokój", "Regał biurowy", 800, 1800, 400, "shelf", 4, 0},  // 33
    {"Kuchnia", "Słupek pod piekarnik ze schowkami", 600, 2100, 580, "cabinet", 0, 0},  // 34
    {"Kuchnia", "Szafka zlewowa — listwy i otwarte plecy", 800, 720, 560, "cabinet", 0, 2},  // 35
    {"Kuchnia", "Szafka wisząca wąska", 400, 900, 320, "cabinet", 2, 1},  // 36
    {"Kuchnia", "Szafka wisząca pozioma", 1200, 360, 350, "cabinet", 1, 2},  // 37
    {"Kuchnia", "Otwarty moduł pod urządzenie", 600, 820, 580, "shelf", 0, 0},  // 38
    {"Sypialnia", "Szafa garderobiana trzykomorowa", 1800, 2200, 600, "cabinet", 4, 1},  // 39
    {"Biuro i przedpokój", "Niska szafka na obuwie", 1000, 800, 350, "cabinet", 3, 2},  // 40
    {"Biuro i przedpokój", "Wysoka szafka na obuwie", 600, 1800, 350, "cabinet", 5, 2},  // 41
    {"Sypialnia", "Komoda na bieliznę — 4 szuflady", 800, 1400, 450, "drawers", 0, 4},  // 42
    {"Salon", "Regał dwustronny z przegrodą", 1200, 1600, 350, "shelf", 4, 0},  // 43
    {"Biuro i przedpokój", "Kontenerek biurowy — 3 szuflady", 450, 600, 500, "drawers", 0, 3},  // 44
    {"Biuro i przedpokój", "Szafka na segregatory", 1200, 1200, 380, "cabinet", 3, 2},  // 45
    {"Garaż i warsztat", "Szafa warsztatowa trzykomorowa", 1200, 2000, 600, "cabinet", 4, 1},  // 46
    {"Garaż i warsztat", "Szafka górna garażowa", 1000, 600, 400, "cabinet", 1, 2},  // 47
    {"Garaż i warsztat", "Komoda warsztatowa — 4 szuflady", 800, 850, 500, "drawers", 0, 4},  // 48
    {"Łazienka", "Szafka pralnicza — listwy i otwarte plecy", 600, 900, 650, "cabinet", 1, 1},  // 49
    {"Łazienka", "Regał łazienkowy wąski", 300, 1800, 300, "shelf", 5, 0},  // 50
    {"Łazienka", "Zabudowa łazienkowa wisząca", 1200, 500, 450, "cabinet", 1, 2},  // 51
};

const std::map<long long, json> PRESET_OPTIONS{
    {34, {{"leg_height", 0}, {"back", false}, {"hinge_count", 3},
          {"sections", json::array({{{"key", "bottom"}, {"enabled", true}, {"height", 600}, {"front", "doors"}, {"doors", 1}, {"shelves", 0}},
                                    {{"key", "main"}, {"enabled", true}, {"front", "open"}, {"shelves", 0}},
                                    {{"key", "upper"}, {"enabled", true}, {"height", 600}, {"front", "doors"}, {"doors", 1}, {"shelves", 0}}})}}},
    {35, {{"top_mode", "rails"}, {"back", false}}}, {36, {{"leg_height", 0}}}, {37, {{"leg_height", 0}}},
    {38, {{"leg_height", 0}, {"top_mode", "rails"}, {"back", false}}},
    {39, {{"leg_height", 0}, {"hinge_count", 4}, {"sections", json::array({{{"key", "main"}, {"enabled", true}, {"columns", 3}, {"doors", 1}}})}}},
    {40, {{"leg_height", 80}}}, {41, {{"leg_height", 0}, {"hinge_count", 3}}}, {42, {{"hinge_count", 3}}},
    {43, {{"leg_height", 0}, {"back", false}, {"sections", json::array({{{"key", "main"}, {"enabled", true}, {"columns", 2}}})}}},
    {44, {{"leg_height", 0}}}, {45, {{"leg_height", 0}}},
    {46, {{"leg_height", 0}, {"hinge_count", 4}, {"sections", json::array({{{"key", "main"}, {"enabled", true}, {"columns", 3}, {"doors", 1}}})}}},
    {47, {{"leg_height", 0}}}, {48, {{"leg_height", 0}}}, {49, {{"top_mode", "rails"}, {"back", false}}},
    {50, {{"leg_height", 0}}}, {51, {{"leg_height", 0}}},
};

const Template& template_at(long long index) {
    long long n = (long long)TEMPLATES.size();
    if (index < 0) index += n;
    if (index < 0 || index >= n) throw py::IndexError("list index out of range");
    return TEMPLATES[(size_t)index];
}

json template_options(long long index) {
    auto found = PRESET_OPTIONS.find(index);
    return found != PRESET_OPTIONS.end() ? found->second : json::object();
}

namespace {

double num(const json& value) { return value.get<double>(); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

Part& require_part(Project& project, const std::string& id) {
    Part* p = find_part(project, id);
    if (!p) throw py::StopIteration();  // next(...) bez wartości domyślnej
    return *p;
}

// a + b dla liczb Pythona: int + int pozostaje int
json add_numbers(const json& a, const json& b) {
    if (a.is_number_integer() && b.is_number_integer()) return a.get<long long>() + b.get<long long>();
    return num(a) + num(b);
}

}  // namespace

std::pair<ProjectPtr, std::string> create_furniture(const Project& project, long long index, std::optional<double> width, std::optional<double> height, std::optional<double> depth,
                                                    double thickness, core::Point3 origin, const std::optional<std::string>& group_, const std::optional<std::string>& material_,
                                                    const std::optional<std::string>& front_material_) {
    const Template& tpl = template_at(index);
    const std::string& name = tpl.name; const std::string& kind = tpl.kind; long long shelves = tpl.shelves, fronts = tpl.fronts;
    if (PRESET_OPTIONS.contains(index)) {
        json c = furniture_configurator::default_config(index, project);
        auto truthy_or = [](const std::optional<std::string>& value, const json& fallback) { return value && !value->empty() ? json(*value) : fallback; };
        json updates = {{"name", group_ && !group_->empty() ? json(*group_) : json(name)},
                        {"width", width ? json(*width) : json(tpl.w)}, {"depth", depth ? json(*depth) : json(tpl.d)}, {"thickness", thickness},
                        {"origin", json::array({origin[0], origin[1], origin[2]})},
                        {"material", truthy_or(material_, c["material"])}, {"front_material", truthy_or(front_material_, c["front_material"])}};
        for (auto& [k, v] : updates.items()) c[k] = v;
        c["height"] = add_numbers(add_numbers(height ? json(*height) : json(tpl.h), c["leg_height"]), py::truthy(c["worktop"]) ? c["worktop_thickness"] : json(0));
        return furniture_configurator::configure(project, c);
    }
    double w = finite(width && *width != 0 ? json(*width) : json(tpl.w), 1, 10000), h = finite(height && *height != 0 ? json(*height) : json(tpl.h), 1, 10000);
    double d = finite(depth && *depth != 0 ? json(*depth) : json(tpl.d), 1, 10000), t = finite(thickness, 1, 10000);
    ProjectPtr result = std::make_shared<Project>(project);
    std::string group = group_ && !group_->empty() ? *group_ : name;
    std::set<std::string> occupied; for (auto& p : result->parts) occupied.insert(p.group);
    std::string base = group; int n = 2;
    while (occupied.contains(group)) { group = base + std::format(" ({})", n); ++n; }
    std::string material = material_ && !material_->empty() ? *material_ : result->materials.at(0).id;
    std::string front_material = front_material_ && !front_material_->empty() ? *front_material_ : result->materials.at(1).id;
    double ox = origin[0], oy = origin[1], oz = origin[2];
    std::vector<std::string> created;  // identyfikatory — wektor części może się realokować

    auto add = [&](const std::string& part_name, double u, double v, double thick, const std::string& plane, double x, double y, double z, const std::string& role = "other") -> std::string {
        Part p; p.name = part_name; p.group = group; p.u = u; p.v = v; p.t = thick; p.plane = plane; p.x = ox + x; p.y = oy + y; p.z = oz + z; p.role = role; p.material = material;
        p.validate(); result->parts.push_back(p); created.push_back(p.id); return p.id;
    };
    auto from_template = [&](const std::string& template_kind, long long count) {
        created.clear();
        for (auto& p : add_template(*result, template_kind, w, h, d, t, (int)count, false, origin, group)) created.push_back(p.id);
    };

    if (kind == "table" || kind == "desk") {
        from_template("table", 0);
    } else if (kind == "bed") {
        if (std::min(w, d) <= t * 4 || h <= 100) throw ValueError("Zbyt małe wymiary łóżka.");
        add("Bok łóżka lewy", d - 2 * t, 200, t, "YZ", 0, t, h - 200);
        add("Bok łóżka prawy", d - 2 * t, 200, t, "YZ", w - t, t, h - 200);
        add("Wezgłowie", w, h + 400, t, "XZ", 0, d - t, 0);
        add("Zanóżek", w, h, t, "XZ", 0, 0, 0);
        add("Belka środkowa", d - 2 * t, 100, t, "YZ", w / 2 - t / 2, t, h - 120);
        for (int i = 0; i < 12; ++i) add(std::format("Listwa stelaża {}", i + 1), w - 2 * t, 70, t, "XY", t, t + (d - 2 * t - 70) * i / 11, h - 30);
    } else if (kind == "bench") {
        double seat_h = std::min(450.0, h * .6), leg = 60;
        add("Siedzisko", w, d, t, "XY", 0, 0, seat_h);
        std::array<std::pair<double, double>, 4> legs{{{0, 0}, {w - leg, 0}, {0, d - leg}, {w - leg, d - leg}}};
        for (int i = 0; i < 4; ++i) add(std::format("Noga {}", i + 1), leg, seat_h, leg, "XZ", legs[i].first, legs[i].second, 0);
        add("Oparcie", w, h - seat_h, t, "XZ", 0, d - t, seat_h);
    } else if (kind == "chest") {
        from_template("shelf", 0);
        std::string top_id;
        for (auto& id : created) if (require_part(*result, id).name == "Wieniec") { top_id = id; break; }
        if (top_id.empty()) throw py::StopIteration();
        std::erase_if(result->parts, [&](const Part& p) { return p.id == top_id; });
        add("Ścianka przednia", w - 2 * t, h - t, t, "XZ", t, 0, t);
        add("Ścianka tylna", w - 2 * t, h - t, t, "XZ", t, d - t, t);
        std::string lid_id = add("Pokrywa", w, d, t, "XY", 0, 0, h, "other");
        require_part(*result, lid_id).motion = {{"mode", "lid"}, {"angle", 95}, {"travel", 0}};
    } else {
        from_template(kind == "shelf" ? "shelf" : "cabinet", shelves);
        for (auto& id : created) {
            Part& p = require_part(*result, id);
            if (p.name != "Plecy nakładane HDF") p.material = material;
            else {
                std::string hdf; bool found = false;
                for (auto& m : result->materials) if (m.kind == "hdf") { hdf = m.id; found = true; break; }
                if (!found) throw py::StopIteration();
                p.material = hdf;
            }
        }
        // Replace default shelf placement with linked shelf supports, maintaining their current level.
        for (auto& id : std::vector<std::string>(created)) {
            Part* found = find_part(*result, id);
            if (assembly::inferred_role(*found) == "shelf") {
                auto [su, sv] = assembly::suggested_size(*result, group, "shelf", t);
                found->u = su; found->v = sv;
                Part moved = *found;
                auto attached = assembly::attach(*result, moved, group, "shelf", {{"level", moved.z - oz}});
                result = attached.first;
            }
        }
        if (kind == "drawers") {
            json cfg = py::merged(assembly::DEFAULTS, {{"drawer_depth", std::min(400.0, d - 30)}, {"drawer_height", std::min(120.0, (h - 2 * t) / fronts - 40)},
                                                       {"drawer_clearance", 12.7}, {"drawer_raise", t + 6}});
            double front_h = (h - 2 - 2 - fronts * 2 + 2) / fronts;
            for (long long i = 0; i < fronts; ++i) {
                Part p; p.name = std::format("Front szuflady {}", i + 1); p.group = group; p.u = w - 4; p.v = front_h; p.t = t; p.material = front_material;
                auto attached = assembly::attach(*result, p, group, "drawer", py::merged(cfg, {{"level", 2 + i * (front_h + 2)}}));
                result = attached.first; Part* q = attached.second;
                q->motion = {{"mode", "drawer"}, {"travel", num(cfg["drawer_depth"]) * .9}, {"angle", 0}};
                result = fittings::add_handle(*result, q->id, 128, q->u / 2, q->v / 2, false);
            }
        } else if (fronts) {
            for (long long i = 0; i < fronts; ++i) {
                Part p; p.name = std::format("Front {}", i + 1); p.group = group; p.u = (w - 4 - 2 * (fronts - 1)) / fronts; p.v = h - 4; p.t = t; p.material = front_material;
                bool special = kind == "cargo" || kind == "sliding" || kind == "lift";
                auto attached = assembly::attach(*result, p, group, "door", {{"hinge_side", i == 0 ? "left" : "right"}, {"auto_hardware", !special}});
                result = attached.first; std::string id = attached.second->id; double front_u = attached.second->u;
                if (special) {
                    std::string mode = kind == "cargo" ? "cargo" : kind == "lift" ? "lift" : i == 0 ? "slide_right" : "slide_left";
                    result = mechanisms::set_motion(*result, id, mode, 100, kind == "cargo" ? std::min(d - 50, 450.0) : front_u * .85);
                    if (kind == "sliding") {
                        Part& q = require_part(*result, id); q.y -= i * (t + 3); q.assembly.at("config")["slide_lane"] = i * (t + 3);
                    }
                } else {
                    result = dimension_model::regenerate_hinges(*result, id, "hinge45", h > 1800 ? 4 : h > 1200 ? 3 : 2);
                }
                Part& current = require_part(*result, id);
                double cu = current.u, cv = current.v;
                result = fittings::add_handle(*result, id, cu < 140 ? 96 : 128, kind == "lift" ? cu / 2 : i == 0 ? cu - 35 : 35,
                                              kind == "lift" ? 40 : std::min(cv - 70, std::max(70.0, cv / 2)), kind != "lift");
            }
        }
        if (kind == "island") add("Blat wyspy", w + 40, d + 40, 38, "XY", -20, -20, h);
    }
    if (kind == "table" || kind == "desk") {
        for (auto& id : created) require_part(*result, id).material = material;
    }
    result->validate();
    return {result, group};
}

}  // namespace furniture_library
