// Atomic direct edits and furniture resizing with thickness preservation.
#include "direct_edit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include "assembly.h"
#include "corner_furniture.h"
#include "drag_hardware.h"
#include "features.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "opening_options.h"
#include "shaped_furniture.h"

namespace direct_edit {

using core::Part;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::local_world;
using assembly::attach;
using assembly::rebuild_dependents;
using assembly::frame;
using assembly::DEFAULTS;
using assembly::clear_generated;
using py::ValueError;

namespace {

double num(const json& value) { return value.get<double>(); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    throw py::StopIteration();
}


// Rozciągnięcie szafki w bok tworzy rząd OSOBNYCH szafek: do pewnego progu szafka po prostu rośnie, a powyżej progu obok niej
// powstają kolejne szafki (każda z własnymi bokami i własnym kreatorem), które można dalej rozciągać, podnosić albo zmieniać w narożnik.
struct RowPlan {
    double first_width;            // szerokość szafki, którą ciągniemy
    std::vector<double> extra;     // szerokości nowych szafek, od najbliższej do najdalszej od pierwszej
};

RowPlan plan_row(double old_width, double new_width) {
    RowPlan plan{new_width, {}};
    double delta = new_width - old_width;
    if (delta <= 0) return plan;
    double typical = std::clamp(old_width, 400.0, 800.0);
    if (delta < 0.5 * typical) return plan;
    long long k = std::min<long long>(std::max<long long>(1, std::llround(delta / typical)), 7);
    plan.first_width = old_width;
    plan.extra.assign((size_t)k, delta / (double)k);
    return plan;
}

}  // namespace

ProjectPtr resize_part(const Project& project, const std::string& part_id, const std::string& face, const json& distance_) {
    auto temp = std::make_shared<Project>(project); Part* p = find_part(*temp, part_id);
    if (py::truthy(py::get(p->assembly, "generated_by"))) throw ValueError("Edytuj front szuflady; skrzynka jest generowana z jej parametrów.");
    double distance = finite(distance_); char axis = face.at(0); bool minimum = face.ends_with("0");
    std::string key = std::map<char, std::string>{{'u', "u"}, {'v', "v"}, {'t', "t"}}.at(axis); double old = p->get_attr(key).get<double>(); double updated = finite(old + (minimum ? -distance : distance), .1, 10000);
    if ((axis == 'u' || axis == 'v') && !p->contour.empty()) {
        shaped_furniture::resize_profile(*p, axis == 'u' ? updated : p->u, axis == 'v' ? updated : p->v);
    }
    if (minimum) {
        Point3 local = std::map<char, Point3>{{'u', {distance, 0, 0}}, {'v', {0, distance, 0}}, {'t', {0, 0, distance}}}.at(axis);
        auto [x, y, z] = local_world(*p, local[0], local[1], local[2]); p->x = x; p->y = y; p->z = z;
        if (axis == 'u' || axis == 'v') {
            size_t index = axis == 'u' ? 0 : 1;
            for (auto& hole : p->holes) if (index < hole.size()) hole[index] -= distance;
        }
        features::shift_features(*p, local[0], local[1], local[2]);
    }
    p->set_attr(key, updated);
    if (py::truthy(p->assembly) && !py::truthy(py::get(p->assembly, "corner_source"))) {
        json operations = json::array(); for (auto& op : p->operations) if (py::get(op, "owner_id") != p->id) operations.push_back(op);
        p->operations = operations;
        temp = attach(*temp, *p, p->group, p->role, py::get(p->assembly, "config")).first;
    } else {
        fittings::refresh_handles(*temp, part_id); opening_options::refresh_openings(*temp, part_id); find_part(*temp, part_id)->validate();
    }
    temp = rebuild_dependents(*temp, part_id);
    drag_hardware::refresh_manual_attachments(*temp);
    temp->validate(); return temp;
}

ProjectPtr resize_furniture(const Project& project, const std::string& group, double width, double height, double depth, const json& origin) {
    ProjectPtr corner = corner_furniture::resize_corner_furniture(project, group, width, height, depth, origin);
    if (corner != nullptr) return corner;
    json config = furniture_configurator::read_config(project, group);
    if (py::truthy(config)) {
        config["width"] = finite(width, .1, 10000); config["depth"] = finite(depth, .1, 10000);
        config["height"] = finite(height, .1, 10000) + num(config.at("leg_height")) + (py::truthy(py::get(config, "worktop")) ? num(config.at("worktop_thickness")) : 0);
        return furniture_configurator::configure(project, config, group, origin).first;
    }
    ProjectPtr shaped = shaped_furniture::resize_shaped_furniture(project, group, width, height, depth);
    if (shaped != nullptr) return shaped;
    assembly::Frame original = frame(project, group); Point3 lo = original.lower, hi = original.upper;
    std::array<double, 3> sizes{finite(width, .1, 10000), finite(depth, .1, 10000), finite(height, .1, 10000)};
    std::array<double, 3> ratios; for (int i = 0; i < 3; ++i) ratios[i] = sizes[i] / (hi[i] - lo[i]);
    auto temp = std::make_shared<Project>(project);
    std::map<std::string, json> retained_hardware;
    for (auto& p : temp->parts) {
        json items = json::array(); for (auto& h : temp->hardware) if (py::get(h, "owner_id") == p.id && py::truthy(py::get(h, "manual"))) items.push_back(h);
        retained_hardware[p.id] = items;
    }
    std::string tag = "connector:" + group;
    json connections = json::array(); for (auto& h : temp->hardware) if (py::get(h, "kind") == "connector" && py::get(h, "connection_tag") == tag) connections.push_back(h);
    if (!connections.empty()) {
        for (auto& p : temp->parts) { json ops = json::array(); for (auto& op : p.operations) if (py::get(op, "connection_tag") != tag) ops.push_back(op); p.operations = ops; }
        json kept = json::array(); for (auto& h : temp->hardware) if (py::get(h, "connection_tag") != tag) kept.push_back(h);
        temp->hardware = kept;
    }
    std::vector<Part> managed;
    for (auto& p : temp->parts) {
        json role = py::get(p.assembly, "role");
        if (p.group == group && (role == "door" || role == "shelf" || role == "drawer" || role == "back" || role == "worktop")) managed.push_back(p);
    }
    for (auto& p : managed) clear_generated(*temp, p.id);
    std::set<std::string> managed_ids; for (auto& p : managed) managed_ids.insert(p.id);
    std::erase_if(temp->parts, [&](const Part& p) { return managed_ids.contains(p.id); });
    for (auto& p : temp->parts) {
        if (p.group != group) continue;
        for (double angle : {p.rx, p.ry, p.rz}) if (std::abs(angle) > 1e-8) throw ValueError("Zmiana całego mebla wymaga elementów bez obrotów.");
        std::array<int, 3> axes = std::map<std::string, std::array<int, 3>>{{"XY", {0, 1, 2}}, {"XZ", {0, 2, 1}}, {"YZ", {1, 2, 0}}}.at(p.plane);
        std::array<double, 3> position{p.x, p.y, p.z}; std::array<double, 3> new_position;
        for (int i = 0; i < 3; ++i) new_position[i] = lo[i] + (position[i] - lo[i]) * ratios[i];
        int thick_axis = axes[2];
        if (std::abs(position[thick_axis] + p.t - hi[thick_axis]) < .01) new_position[thick_axis] = lo[thick_axis] + sizes[thick_axis] - p.t;
        std::vector<double> dimensions;
        int index = 0;
        for (std::string key : {"u", "v"}) {
            int axis = axes[index++]; double start = position[axis]; double end = start + p.get_attr(key).get<double>();
            auto anchored = [&](double value) {
                if (std::abs(value - lo[axis]) < 50) return lo[axis] + value - lo[axis];
                if (std::abs(hi[axis] - value) < 50) return lo[axis] + sizes[axis] - (hi[axis] - value);
                return lo[axis] + (value - lo[axis]) * ratios[axis];
            };
            new_position[axis] = anchored(start); dimensions.push_back(anchored(end) - new_position[axis]);
        }
        if (std::min(dimensions[0], dimensions[1]) <= 0) throw ValueError("Mebel jest za mały dla grubości elementów i zachowanych luzów.");
        if (!p.contour.empty()) {
            shaped_furniture::resize_profile(p, dimensions[0], dimensions[1]);
        }
        p.u = dimensions[0]; p.v = dimensions[1]; p.x = new_position[0]; p.y = new_position[1]; p.z = new_position[2];
        json ops = json::array(); for (auto& op : p.operations) { json owner = py::get(op, "owner_id"); if (!(owner.is_string() && managed_ids.contains(owner.get<std::string>()))) ops.push_back(op); }
        p.operations = ops;
        if (py::truthy(py::get(p.assembly, "role"))) {
            json& config_ = p.assembly.at("config");
            config_["level"] = num(config_.at("level")) * ratios[2];
            config_["offset"] = num(config_.at("offset")) * ratios[0];
        }
    }
    for (auto p : managed) {
        json cfg = DEFAULTS; json own_config = py::get(p.assembly, "config", json::object()); for (auto& [k, value] : own_config.items()) cfg[k] = value;
        cfg["level"] = num(cfg["level"]) * ratios[2];
        std::string role = p.role; double new_u, new_v; double gap = num(cfg["gap"]);
        if (py::truthy(py::get(cfg, "bay"))) {
            assembly::Frame current_frame = frame(*temp, group, std::nullopt, cfg["bay"]); Point3 a = current_frame.lower, b = current_frame.upper; double bay_w = b[0] - a[0]; double bay_h = b[2] - a[2];
            if (role == "door") { double doors = num(py::get(cfg, "bay_doors", 1)); new_u = (bay_w - (doors + 1) * gap) / doors; new_v = bay_h - 2 * gap; }
            else if (role == "drawer") {
                double count = num(py::get(cfg, "bay_drawers", 1)); new_u = bay_w - 2 * gap; new_v = (bay_h - (count + 1) * gap) / count;
                cfg["level"] = gap + num(py::get(cfg, "bay_drawer_index", 0)) * (new_v + gap);
                cfg["drawer_raise"] = std::max(0.0, current_frame.inner_bottom - (a[2] + num(cfg["level"]) + gap)) + 6;
                cfg["drawer_height"] = std::min(num(cfg["drawer_height"]), new_v - num(cfg["drawer_bottom"]) - 2 * gap - num(cfg["drawer_raise"]));
            } else {
                std::tie(new_u, new_v) = assembly::suggested_size(*temp, group, role, p.t, cfg);
                if (role == "shelf" && py::truthy(py::get(cfg, "bay_shelves")))
                    cfg["level"] = current_frame.inner_bottom - a[2] + (current_frame.inner_top - current_frame.inner_bottom - p.t) * num(py::get(cfg, "bay_shelf_index", 1)) / (num(cfg["bay_shelves"]) + 1);
            }
        } else if (role == "shelf" || role == "top" || role == "bottom") {
            assembly::Frame new_frame = frame(*temp, group);
            new_u = new_frame.inner_right - new_frame.inner_left - (role == "shelf" ? 2 * gap : 0);
            new_v = depth - (role == "shelf" ? 2 * gap : 0);
        } else if (role == "door") {
            // Preserve full-width vs split-door proportions and fixed edge gaps.
            double old_inner_width = hi[0] - lo[0] - 2 * gap;
            new_u = p.u * (width - 2 * gap) / old_inner_width;
            new_v = p.v * (height - 2 * gap) / (hi[2] - lo[2] - 2 * gap);
        } else if (role == "drawer") { new_u = width - 2 * gap; new_v = p.v; }
        else if (role == "left" || role == "right") { new_u = depth; new_v = height; }
        else if (role == "back") { new_u = width; new_v = height; }
        else if (role == "worktop") { new_u = width; new_v = depth; }
        else { new_u = p.u * ratios[1]; new_v = p.v * ratios[2]; }
        if (!p.contour.empty()) {
            shaped_furniture::resize_profile(p, new_u, new_v);
        }
        p.u = new_u; p.v = new_v; p.operations = json::array();
        if (retained_hardware.contains(p.id)) for (auto& h : retained_hardware[p.id]) temp->hardware.push_back(h);
        temp = attach(*temp, p, group, role, cfg).first;
    }
    if (!connections.empty()) {
        json& h = connections[0]; temp = fittings::cabinet_connectors(*temp, group, py::get(h, "recipe_id", "confirmat7"), py::get(h, "front", 50), py::get(h, "back", 50));
    }
    temp->validate(); return temp;
}

ProjectPtr resize_furniture_face(const Project& project, const std::string& group, const std::string& face, const json& distance_) {
    auto styled = shaped_furniture::styled_bounds(project, group);
    assembly::Frame f = styled ? *styled : frame(project, group); Point3 lo = f.lower, hi = f.upper;
    size_t axis = std::string("xyz").find(face.at(0));
    if (axis == std::string::npos) throw ValueError("substring not found");
    bool minimum = face.ends_with("0"); double distance = finite(distance_);
    std::array<double, 3> size; for (int i = 0; i < 3; ++i) size[i] = hi[i] - lo[i];
    size[axis] += minimum ? -distance : distance;
    json corner = corner_furniture::read_corner_config(project, group);
    if (py::truthy(corner)) {
        json origin = corner.at("origin");
        if (minimum) origin[axis] = num(origin[axis]) + distance;
        return resize_furniture(project, group, size[0], size[2], size[1], origin);
    }
    json config = furniture_configurator::read_config(project, group);
    if (py::truthy(config)) {
        json origin = config.at("origin");
        if (minimum) origin[axis] = num(origin[axis]) + distance;
        if (axis == 0 && size[0] > hi[0] - lo[0]) {
            double old_width = hi[0] - lo[0];
            RowPlan plan = plan_row(old_width, size[0]);
            json first_origin = config.at("origin");
            if (minimum) first_origin[0] = num(config.at("origin")[0]) - (plan.first_width - old_width);
            json first = config;
            first["width"] = finite(plan.first_width, .1, 10000); first["depth"] = finite(size[1], .1, 10000);
            first["height"] = finite(size[2], .1, 10000) + num(first.at("leg_height")) + (py::truthy(py::get(first, "worktop")) ? num(first.at("worktop_thickness")) : 0);
            ProjectPtr result = furniture_configurator::configure(project, first, group, first_origin).first;
            if (plan.extra.empty()) return result;
            json layout = furniture_layout::group_layout(*result, group);
            double offset = 0;
            for (double width : plan.extra) {
                json module = config;
                module["width"] = finite(width, .1, 10000);
                if (!minimum) module["origin"][0] = num(config.at("origin")[0]) + old_width + offset;
                else module["origin"][0] = num(config.at("origin")[0]) - offset - width;
                for (auto& section : module["sections"]) section.erase("column_widths");
                auto created = furniture_configurator::configure(*result, module, std::nullopt, module.at("origin"));
                result = furniture_layout::placed(*created.first, created.second, layout, false);
                offset += width;
            }
            return result;
        }
        return resize_furniture(project, group, size[0], size[2], size[1], origin);
    }
    ProjectPtr result = resize_furniture(project, group, size[0], size[2], size[1]);
    if (minimum) {
        std::set<std::string> ids; for (auto& p : result->parts) if (p.group == group) ids.insert(p.id);
        std::string key(1, face[0]);
        for (auto& p : result->parts) {
            if (ids.contains(p.id)) {
                p.set_attr(key, p.get_attr(key).get<double>() + distance);
                if (py::truthy(p.style_source)) {
                    p.style_source.at("origin")[axis] = num(p.style_source["origin"][axis]) + distance;
                    p.style_source["signature"] = shaped_furniture::source_signature(p, p.style_source["origin"]);
                }
            }
        }
        for (auto& h : result->hardware) {
            json owner = py::get(h, "owner_id");
            if (!(owner.is_string() && ids.contains(owner.get<std::string>()))) continue;
            if (h.contains("positions")) for (auto& position : h["positions"]) position[key] = num(position.at(key)) + distance;
            if (h.contains("origin")) h["origin"][axis] = num(h["origin"][axis]) + distance;
            if (axis == 1) {
                for (std::string k : {"closed_y", "closed_front_y"}) {
                    if (h.contains(k)) h[k] = num(h[k]) + distance;
                }
            }
        }
    }
    result->validate(); return result;
}

}  // namespace direct_edit
