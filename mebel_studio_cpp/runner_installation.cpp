// Apply a sourced planning profile to a managed wooden drawer.
#include "runner_installation.h"

#include <algorithm>
#include <cmath>

#include "assembly.h"
#include "runners_catalog.h"

namespace runner_installation {

using core::Part;
using core::Project;
using core::ProjectPtr;
using py::ValueError;

namespace {
double num(const json& value) { return value.get<double>(); }

// {**DEFAULTS, **part.assembly.get('config', {}), **(config or {})}
json merged_config(const Part& part, const json& config) {
    json cfg = py::merged(assembly::DEFAULTS, py::get(part.assembly, "config", json::object()));
    return py::truthy(config) ? py::merged(cfg, config) : cfg;
}
}  // namespace

json drawer_depth_context(const Project& project, const Part& part, const json& config) {
    json cfg = merged_config(part, config);
    assembly::Frame f = assembly::frame_for_part(project, part);
    double start = f.lower[1] + num(cfg["drawer_front_gap"]);
    double rear = f.upper[1];
    double z0 = part.z + num(cfg["gap"]) + num(cfg["drawer_raise"]);
    double z1 = z0 + num(cfg["drawer_height"]) + num(cfg["drawer_bottom"]);
    json back_ids = json::array();
    for (const auto& candidate : project.parts) {
        if (candidate.group != part.group || assembly::inferred_role(candidate) != "back") continue;
        auto [lo, hi] = assembly::bounds(candidate);
        bool intersects = std::min(hi[0], f.inner_right) > std::max(lo[0], f.inner_left) + 1e-8 && std::min(hi[2], z1) > std::max(lo[2], z0) + 1e-8;
        if (intersects && lo[1] < rear && hi[1] > start) {
            if (std::abs(candidate.rx) > 1e-8 || std::abs(candidate.ry) > 1e-8 || std::abs(candidate.rz) > 1e-8)
                throw ValueError("Przed doborem prowadnic ustaw plecy bez obrotu; obrócona przeszkoda wymaga osobnego dopasowania.");
            rear = std::min(rear, lo[1]);
            back_ids.push_back(candidate.id);
        }
    }
    double end = rear - num(cfg["gap"]);
    if (end <= start) throw ValueError("Brak użytecznej głębokości szuflady po uwzględnieniu pleców i odstępów.");
    return json{{"start_y", start}, {"end_y", end}, {"usable_depth", end - start}, {"front_offset", cfg["drawer_front_gap"]},
                {"rear_gap", cfg["gap"]}, {"back_ids", back_ids}};
}

ProjectPtr apply_runner(const Project& project, const std::string& part_id, const json& profile_id, const json& nominal_length, const json& side_thickness,
                        const json& bottom_thickness, const json& recess, const json& confirmed_min_depth) {
    const Part* source = nullptr;
    for (const auto& q : project.parts) if (q.id == part_id) { source = &q; break; }
    if (source == nullptr) throw py::StopIteration();
    Part p = *source;  // copy.deepcopy
    if (p.role != "drawer") throw ValueError("Wybierz front szuflady utworzonej w szkicu lub bibliotece.");
    assembly::Frame f = assembly::frame_for_part(project, p);
    json cfg = py::merged(assembly::DEFAULTS, py::get(p.assembly, "config", json::object()));
    cfg["drawer_front_gap"] = 0;
    cfg["runner_confirmed_min_depth"] = confirmed_min_depth;
    json context = drawer_depth_context(project, p, cfg);
    json plan = runners_catalog::calculate_runner(profile_id, f.inner_right - f.inner_left, nominal_length, side_thickness, context["usable_depth"],
                                                  "four_sided", side_thickness, confirmed_min_depth);
    if (plan["min_cabinet_depth"].is_null())
        throw ValueError("Ta karta nie określa głębokości montażowej. Wpisz w kalkulatorze potwierdzoną minimalną głębokość konkretnego artykułu; podgląd samego obrysu nie wystarcza.");
    if (!plan["valid"].get<bool>()) {
        std::vector<std::string> errors;
        for (const auto& e : plan["errors"]) errors.push_back(e.get<std::string>());
        throw ValueError(py::join(errors, "\n"));
    }
    const json& spec = runners_catalog::PROFILES.at(profile_id.get<std::string>());
    json opening_gap = py::get(spec, "activation_gap_min");
    if (!py::truthy(opening_gap)) opening_gap = assembly::DEFAULTS["gap"];
    cfg["runner_profile"] = profile_id; cfg["runner_nominal"] = nominal_length;
    cfg["drawer_t"] = side_thickness; cfg["drawer_bottom"] = bottom_thickness;
    cfg["drawer_bottom_recess"] = recess; cfg["drawer_depth"] = plan["drawer_length"];
    cfg["drawer_clearance"] = plan["external_clearance_per_side"];
    // max(DEFAULTS['gap'], opening_gap): przy równości zwraca pierwszy argument
    cfg["opening_gap"] = num(opening_gap) > num(assembly::DEFAULTS["gap"]) ? opening_gap : assembly::DEFAULTS["gap"];
    cfg["auto_hardware"] = true;
    std::string group = p.group;
    auto [result, np] = assembly::attach(project, p, group, "drawer", cfg);
    json documented = py::get(plan, "documented_travel");
    json travel = py::truthy(documented) ? documented : (plan["extension"] == "partial" ? json(num(nominal_length) * .75) : nominal_length);
    np->motion = json{{"mode", "drawer"}, {"angle", 0}, {"travel", travel},
                      {"travel_status", py::truthy(documented) ? "Producent" : "Wysuw wizualizacyjny; sprawdź drogę konkretnego artykułu"}};
    result->validate();
    return result;
}

}  // namespace runner_installation
