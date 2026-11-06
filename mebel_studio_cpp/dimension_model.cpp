// Shared dimension edits and fit operations for sketch, forms and 3D.
#include "dimension_model.h"

#include <cmath>

#include "assembly.h"
#include "corner_furniture.h"
#include "fittings.h"
#include "opening_options.h"
#include "shaped_furniture.h"

namespace dimension_model {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::finite;
using assembly::inferred_role;
using assembly::suggested_size;
using assembly::attach;
using assembly::rebuild_dependents;
using assembly::DEFAULTS;
using py::ValueError;

namespace {

double num(const json& value) { return value.get<double>(); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    throw py::StopIteration();
}

const Part* find_part(const Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    throw py::StopIteration();
}

void scale_contour(Part& p, double u, double v) {
    for (auto& point : p.contour) { point = {point.at(0) * u / p.u, point.at(1) * v / p.v}; }
}

json defaults_with(const json& config) {
    json cfg = DEFAULTS;
    for (auto& [k, value] : config.items()) cfg[k] = value;
    return cfg;
}

}  // namespace

ProjectPtr resize_dimensions(const Project& project, const std::string& part_id, const json& u_, const json& v_, const json& t_) {
    auto result = std::make_shared<Project>(project); Part* p = find_part(*result, part_id);
    if (py::truthy(py::get(p->assembly, "generated_by"))) throw ValueError("Wymiary skrzynki zmień przez jej front.");
    double u = finite(u_, .1, 10000), v = finite(v_, .1, 10000), t = finite(t_, .1, 10000);
    if (py::truthy(p->profile)) {
        shaped_furniture::resize_profile(*p, u, v);
    } else if (!p->contour.empty()) scale_contour(*p, u, v);
    p->u = u; p->v = v; p->t = t;
    for (auto& op : p->operations) {
        if (py::get(op, "recipe_id") == "handle") op["depth"] = t;
    }
    std::string id = p->id;
    if (py::truthy(p->assembly) && !py::truthy(py::get(p->assembly, "corner_source"))) { auto r = attach(*result, *p, p->group, p->role, py::get(p->assembly, "config")); result = r.first; p = r.second; }
    else {
        fittings::refresh_handles(*result, id); opening_options::refresh_openings(*result, id); p = find_part(*result, id); p->validate();
    }
    result = rebuild_dependents(*result, p->id); result->validate(); return result;
}

ProjectPtr fit_part(const Project& project, const std::string& part_id) {
    const Part* p = find_part(project, part_id); std::string role = inferred_role(*p);
    if (py::truthy(py::get(p->assembly, "corner_source"))) {
        return corner_furniture::fit_corner_part(project, part_id);
    }
    if (role == "other") throw ValueError("Nadaj elementowi funkcję w szkicu (bok, góra, dół, półka, front), aby dopasować go do korpusu.");
    json cfg = defaults_with(py::get(p->assembly, "config", json::object()));
    double u, v;
    if (role == "door") {
        assembly::Frame f = assembly::frame_for_part(project, *p); double full = f.upper[0] - f.lower[0] - 2 * num(cfg["gap"]);
        bool siblings = false;
        for (auto& q : project.parts)
            if (q.group == p->group && q.id != p->id && inferred_role(q) == "door" && py::get(py::get(q.assembly, "config", json::object()), "bay") == py::get(cfg, "bay")) siblings = true;
        std::tie(u, v) = suggested_size(project, p->group, role, p->t, cfg);
        if (siblings) {
            std::string side = p->x > f.lower[0] + full / 3 ? "right" : "left"; cfg["hinge_side"] = side; u = (full - num(cfg["gap"])) / 2;
        }
    } else std::tie(u, v) = suggested_size(project, p->group, role, p->t, cfg);
    auto result = std::make_shared<Project>(project); Part* q = find_part(*result, p->id);
    if (py::truthy(q->profile)) {
        shaped_furniture::resize_profile(*q, u, v);
    } else if (!q->contour.empty()) scale_contour(*q, u, v);
    q->u = u; q->v = v;
    auto [attached, qa] = attach(*result, *q, q->group, role, cfg); result = rebuild_dependents(*attached, qa->id); result->validate(); return result;
}

ProjectPtr regenerate_hinges(const Project& project, const std::string& part_id, const json& pattern, const json& count, const std::optional<std::string>& side_) {
    const Part* target = find_part(project, part_id);
    json target_config = py::get(target->assembly, "config", json::object());
    if (py::truthy(py::get(target_config, "corner_bay"))) {
        if (side_ && *side_ != "left") throw ValueError("Krawędź zawiasową narożnika zmień przez kierunek narożnika w kreatorze.");
        return corner_furniture::regenerate_corner_hinges(project, part_id, pattern, count);
    }
    if (py::truthy(py::get(target_config, "corner_diagonal"))) throw ValueError("Front skośny wymaga osobno dobranego zawiasu; automatyczne wiercenia są wyłączone.");
    Part p = *target; json r = fittings::recipe(pattern); assembly::Frame f = assembly::frame_for_part(project, p);
    if (pattern == "hinge_sensys_tb55") throw ValueError("Ten zawias jest wpuszczany. Wybierz wiercenie ręczne z katalogu; generator frontów obsługuje nakładane drzwiczki.");
    if (!(num(py::get(r, "min_thickness", 0)) <= p.t && p.t <= num(py::get(r, "max_thickness", 10000)))) throw ValueError("Grubość frontu nie mieści się w zakresie tego zawiasu.");
    json config = py::get(p.assembly, "config", json::object());
    json side = side_ && !side_->empty() ? json(*side_) : py::truthy(py::get(config, "hinge_side")) ? config["hinge_side"]
              : json(std::abs(p.x + p.u - f.upper[0] + 2) < 1 && p.x > f.lower[0] + 2 ? "right" : "left");
    json cfg = defaults_with(config);
    cfg["auto_hardware"] = true; cfg["hinge_side"] = side; cfg["hinge_count"] = count; cfg["cup_diameter"] = r.at("diameter"); cfg["cup_depth"] = r.at("depth"); cfg["hinge_pattern"] = pattern;
    if (py::truthy(py::get(r, "closing_spring"))) cfg["hinge_spring"] = r["closing_spring"];
    // Remove mounting-pilot operations of the previous catalog selection.
    json operations = json::array();
    for (auto& o : p.operations) if (py::get(o, "recipe_id") != "hinge45" && py::get(o, "recipe_id") != "hinge52") operations.push_back(o);
    p.operations = operations;
    auto [result, attached] = attach(project, p, p.group, "door", cfg);
    attached->motion = {{"mode", "hinge_" + side.get<std::string>()}, {"angle", 100}, {"travel", 0}};
    for (auto& h : result->hardware) {
        if (py::get(h, "owner_id") == attached->id && py::get(h, "kind") == "hinge") { h["name"] = r.at("name").get<std::string>() + " + prowadnik roboczy"; h["source"] = r.at("source"); }
    }
    attached->assembly["config"]["hinge_pattern"] = pattern;
    result->validate(); return result;
}

}  // namespace dimension_model
