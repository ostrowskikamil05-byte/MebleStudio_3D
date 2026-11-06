// Presentation-only opening transforms: production model remains closed.
#include "mechanisms.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "assembly.h"
#include "pyutil.h"

namespace mechanisms {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::local_world;
using core::Point3;
using core::rotate;
using py::StopIteration;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> MODES = {
    {"Bez mechanizmu", "none"}, {"Zawias lewy", "hinge_left"}, {"Zawias prawy", "hinge_right"},
    {"Szuflada — wysuw", "drawer"}, {"Drzwi przesuwne w lewo", "slide_left"}, {"Drzwi przesuwne w prawo", "slide_right"},
    {"Klapa do góry", "lift"}, {"Klapa w dół", "flap_down"}, {"Cargo — wysuw", "cargo"}, {"Pokrywa skrzyni", "lid"}};

namespace {

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

// part.assembly.get('config', {})
json config_of(const Part& p) { return py::get(p.assembly, "config", json::object()); }

double num(const json& v) { return v.is_number() ? v.get<double>() : py::to_float(v); }

Part& find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return p;
    throw StopIteration();
}

}  // namespace

json effective_motion(const Part& part) {
    if (py::truthy(part.motion)) return part.motion;
    std::string role = assembly::inferred_role(part);
    if (role == "door") {
        json side = py::get(config_of(part), "hinge_side", ends_with(part.name, "2") ? "right" : "left");
        return json{{"mode", "hinge_" + side.get<std::string>()}, {"angle", 100}, {"travel", part.u}};
    }
    if (role == "drawer") return json{{"mode", "drawer"}, {"travel", py::get(config_of(part), "drawer_depth", 400)}, {"angle", 0}};
    return json{{"mode", "none"}};
}

ProjectPtr posed_project(const ProjectPtr& project, const std::map<std::string, double>& amounts) {
    auto result = std::make_shared<Project>(*project);
    for (const auto& original : project->parts) {
        auto found = amounts.find(original.id);
        double amount = std::max(0.0, std::min(1.0, found == amounts.end() ? 0.0 : found->second));
        if (!amount) continue;
        json motion = effective_motion(original);
        std::string mode = motion["mode"].get<std::string>();
        Part& p = find_part(*result, original.id);
        if (mode == "hinge_left" || mode == "hinge_right" || mode == "lift" || mode == "flap_down" || mode == "lid") {
            if (starts_with(mode, "hinge")) {
                if ((p.plane != "XZ" && p.plane != "YZ") || std::abs(p.rx) > 1e-6 || std::abs(p.ry) > 1e-6) continue;
            } else if (std::abs(p.rx) > 1e-6 || std::abs(p.ry) > 1e-6 || std::abs(p.rz) > 1e-6 || p.plane != (mode == "lid" ? "XY" : "XZ")) {
                continue;
            }
            double angle = num(py::get(motion, "angle", 100)) * amount;
            char axis;
            Point3 pivot;
            if (mode == "lid") {
                axis = 'x'; angle = -angle; pivot = local_world(p, 0, p.v, 0);
            } else if (starts_with(mode, "hinge")) {
                axis = 'z';
                angle = mode == "hinge_left" ? -angle : angle;
                pivot = local_world(p, mode == "hinge_left" ? 0 : p.u, 0, p.t);
                if (p.plane == "YZ") angle = -angle;
                json bay = py::get(config_of(p), "corner_bay", json::object());
                angle *= num(py::get(bay, "opening_sign", 1));
                if (py::get(bay, "cup_face") == "bottom") pivot = local_world(p, mode == "hinge_left" ? 0 : p.u, 0, 0);
            } else {
                axis = 'x';
                angle = mode == "lift" ? -angle : angle;
                pivot = local_world(p, 0, mode == "lift" ? p.v : 0, p.t);
            }
            Point3 r = axis == 'x' ? Point3{angle, 0, 0} : Point3{0, 0, angle};
            Point3 delta = rotate(Point3{p.x - pivot[0], p.y - pivot[1], p.z - pivot[2]}, r[0], r[1], r[2]);
            p.x = pivot[0] + delta[0]; p.y = pivot[1] + delta[1]; p.z = pivot[2] + delta[2];
            p.rx += r[0]; p.rz += r[2];
        } else if (mode == "drawer" || mode == "cargo" || mode == "slide_left" || mode == "slide_right") {
            double travel = num(py::get(motion, "travel", 400)) * amount;
            bool x_axis = starts_with(mode, "slide");
            double sign = mode == "slide_right" ? 1 : -1;
            (x_axis ? p.x : p.y) += sign * travel;
            for (auto& child : result->parts)
                if (py::get(child.assembly, "generated_by") == p.id) (x_axis ? child.x : child.y) += sign * travel;
        }
    }
    return result;
}

ProjectPtr set_motion(const Project& project, const std::string& part_id, const std::string& mode, const json& angle_in, const json& travel_in) {
    bool known = false;
    for (const auto& [_, value] : MODES) if (value == mode) known = true;
    if (!known) throw ValueError("Nieznany mechanizm.");
    auto result = std::make_shared<Project>(project);
    Part* p = &find_part(*result, part_id);
    if (py::truthy(py::get(p->assembly, "generated_by"))) throw ValueError("Wybierz front zamiast części skrzynki.");
    if (py::truthy(py::get(config_of(*p), "corner_bay"))) {
        std::string side = py::get(p->assembly.at("config"), "hinge_side", "left").get<std::string>();
        if (mode != "none" && mode != "hinge_" + side)
            throw ValueError("Front narożnika otwiera się przy boku z wykonanym wzorcem zawiasów. Zmień kierunek całego narożnika w kreatorze.");
        p->motion = json{{"mode", mode}, {"angle", finite(angle_in, 0, 180)}, {"travel", 0}};
        result->validate();
        return result;
    }
    if (py::truthy(py::get(config_of(*p), "corner_diagonal"))) {
        if (mode != "none") throw ValueError("Front skośny wymaga osobno dobranego rzeczywistego mechanizmu. Automatyczne otwieranie jest wyłączone.");
        p->motion = json{{"mode", "none"}};
        result->validate();
        return result;
    }
    if (p->plane != (mode == "lid" ? "XY" : "XZ") || std::abs(p->rx) > 1e-6 || std::abs(p->ry) > 1e-6 || std::abs(p->rz) > 1e-6)
        throw ValueError("Mechanizm frontu wymaga płaszczyzny XZ i zerowych obrotów.");
    double angle = finite(angle_in, 0, 180);
    double travel = finite(travel_in, 0, 3000);
    {
        json kept = json::array();
        for (const auto& h : result->hardware) {
            std::string kind = py::get(h, "kind").is_string() ? py::get(h, "kind").get<std::string>() : "";
            if (!(py::get(h, "owner_id") == p->id && (kind == "lift" || kind == "sliding" || kind == "cargo"))) kept.push_back(h);
        }
        result->hardware = kept;
    }
    if (starts_with(mode, "hinge")) {
        json cfg = py::merged(config_of(*p), json{{"hinge_side", mode == "hinge_left" ? "left" : "right"}, {"auto_hardware", true}});
        std::string group = p->group;
        auto attached = assembly::attach(*result, *p, group, "door", cfg);
        result = attached.first;
        p = attached.second;
    } else if (mode == "lift" || mode == "flap_down" || mode == "slide_left" || mode == "slide_right" || mode == "cargo") {
        if (assembly::inferred_role(*p) == "drawer") throw ValueError("Dla skrzynki szuflady wybierz wysuw. Do cargo użyj osobnego frontu.");
        for (auto& q : result->parts) {
            json kept = json::array();
            for (const auto& o : q.operations) if (py::get(o, "owner_id") != p->id) kept.push_back(o);
            q.operations = kept;
        }
        {
            json kept = json::array();
            for (const auto& h : result->hardware) if (py::get(h, "owner_id") != p->id || py::truthy(py::get(h, "manual"))) kept.push_back(h);
            result->hardware = kept;
        }
        if (py::truthy(p->assembly)) {
            if (!p->assembly.contains("config")) throw std::out_of_range("config");
            p->assembly["config"]["auto_hardware"] = false;
        }
        assembly::Frame f = assembly::frame(*result, p->group);
        std::string kind = mode == "cargo" ? "cargo" : (mode == "lift" || mode == "flap_down") ? "lift" : "sliding";
        std::string name = kind == "cargo" ? "Cargo wysuwane — model koncepcyjny"
                         : kind == "lift" ? "Mechanizm klapy — model koncepcyjny"
                                          : "Prowadzenie drzwi przesuwnych — model koncepcyjny";
        std::string source = kind == "cargo" ? "https://www.kesseboehmer.com/en/storage-solutions/kitchen/larder-units/dispensa"
                           : kind == "lift" ? "https://www.blum.com/no/en/products/liftsystems/aventos-hk-top/downloads-videos/"
                                            : "https://hta.hettich.com/en-de/sliding-door-systems/topline-l";
        assembly::accessory(*result, *p, name, 1, 0, {}, json::array());
        json& h = result->hardware.back();
        h["kind"] = kind;
        h["manual"] = true;
        h["model_only"] = true;
        h["width"] = f.inner_right - f.inner_left - 30;
        h["depth"] = f.upper[1] - f.lower[1] - 45;
        h["height"] = f.inner_top - f.inner_bottom - 110;
        h["origin"] = json::array({f.inner_left + 15, f.lower[1] + 20, f.inner_bottom + 20});
        h["baskets"] = 3;
        h["closed_y"] = p->y;
        h["color"] = "#a5abb3";
        h["source"] = source;
    }
    p->motion = json{{"mode", mode}, {"angle", angle}, {"travel", travel}};
    result->validate();
    return result;
}

void refresh_mechanism_hardware(Project& project, const std::string& part_id) {
    Part& p = find_part(project, part_id);
    std::vector<json*> items;
    for (auto& h : project.hardware) {
        json kind = py::get(h, "kind");
        if (py::get(h, "owner_id") == p.id && kind.is_string() && (kind == "cargo" || kind == "lift" || kind == "sliding")) items.push_back(&h);
    }
    if (items.empty()) return;
    assembly::Frame f = assembly::frame(project, p.group);
    for (json* h : items) {
        double width = f.inner_right - f.inner_left - 30;
        double depth = f.upper[1] - f.lower[1] - 45;
        double height = f.inner_top - f.inner_bottom - 110;
        (*h)["width"] = width;
        (*h)["depth"] = depth;
        (*h)["height"] = height;
        (*h)["origin"] = json::array({f.inner_left + 15, f.lower[1] + 20, f.inner_bottom + 20});
        (*h)["closed_y"] = p.y;
        if ((*h)["kind"] == "cargo" && std::min({width, depth, height}) <= 0) throw ValueError("Korpus jest za mały dla modelu cargo.");
    }
}

}  // namespace mechanisms
