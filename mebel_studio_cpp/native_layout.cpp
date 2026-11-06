// Batch placement geometry in C++; retain editable project/CAD publication.
#include "native_layout.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>

#include "fitting_visuals.h"
#include "native_model.h"
#include "pyutil.h"

namespace native_layout {

using core::Point3;
using core::Project;
using core::ProjectPtr;

const std::vector<std::string> OPERATIONS = {"groups", "group_layout", "group_bounds", "local_group_bounds", "move_group",
                                             "rotate_group", "snap_group", "nearest_snap", "contacts"};

json scalar(double value) {
    if (!std::isfinite(value)) return py::repr(value);  // str(float)
    return value;
}

json scalar(const json& value) {
    if (value.is_number_float() && !std::isfinite(value.get<double>())) return py::repr(value.get<double>());
    return value;
}

json scene(const Project& project, bool geometry, const std::optional<std::string>& group) {
    std::vector<const core::Part*> parts;
    for (const auto& part : project.parts) if (!group || part.group == *group) parts.push_back(&part);
    json rows = json::array();
    for (const core::Part* part : parts) {
        json row = {{"id", part->id}, {"group", part->group}, {"layout", part->layout}};
        if (geometry) {
            row["u"] = part->u; row["v"] = part->v; row["t"] = part->t; row["plane"] = part->plane;
            row["x"] = part->x; row["y"] = part->y; row["z"] = part->z;
            row["rx"] = part->rx; row["ry"] = part->ry; row["rz"] = part->rz;
            json points = json::array();
            for (const auto& p : part->points()) points.push_back(json::array({p[0], p[1]}));
            row["points"] = points;
            row["shared_worktop"] = py::truthy(py::get(part->assembly, "shared_worktop"));
            row["corner_bottom"] = part->role == "bottom" && py::truthy(py::get(part->assembly, "corner_source"));
        }
        rows.push_back(row);
    }
    json result = {{"parts", rows}};
    if (geometry) {
        std::map<std::string, const core::Part*> owners;
        for (const core::Part* part : parts) owners[part->id] = part;
        std::vector<std::pair<std::string, std::vector<Point3>>> vertices;
        for (const auto& item : fitting_visuals::fitting_faces(project, std::set<std::string>{"leg", "led_strip"})) {
            auto owner = owners.find(item.owner);
            if (owner == owners.end() || !owner->second->visible) continue;
            auto entry = vertices.begin();
            while (entry != vertices.end() && entry->first != item.owner) ++entry;
            if (entry == vertices.end()) { vertices.push_back({item.owner, {}}); entry = std::prev(vertices.end()); }
            entry->second.insert(entry->second.end(), item.face.begin(), item.face.end());
        }
        json accessories = json::array();
        for (const auto& [owner, points] : vertices) {
            json list = json::array();
            for (const auto& p : points) list.push_back(json::array({p[0], p[1], p[2]}));
            accessories.push_back({{"owner", owner}, {"vertices", list}});
        }
        result["accessories"] = accessories;
    }
    return result;
}

namespace {

json call(const std::string& function, const Project& project, json args, bool geometry = false, const std::optional<std::string>& group = std::nullopt) {
    json all = json::array({scene(project, geometry, group)});
    for (auto& a : args) all.push_back(a);
    return native_model::call("layout", function, all);
}

json layout_of(const json& value) {
    json out = json::object();
    for (const char* key : {"x", "y", "z", "yaw"}) out[key] = value.at(key);
    return out;
}

Point3 point_of(const json& v) { return {v.at(0).get<double>(), v.at(1).get<double>(), v.at(2).get<double>()}; }

furniture_layout::Box box_of(const json& v) { return {point_of(v.at(0)), point_of(v.at(1))}; }

}  // namespace

std::vector<std::string> groups(const Project& project) {
    json result = call("groups", project, json::array());
    std::vector<std::string> out;
    for (auto& g : result) out.push_back(g.get<std::string>());
    return out;
}

json group_layout(const Project& project, const std::string& group) {
    return layout_of(call("group_layout", project, json::array({group}), false, group));
}

furniture_layout::Box group_bounds(const Project& project, const std::string& group) {
    return box_of(call("group_bounds", project, json::array({group}), true, group));
}

furniture_layout::Box local_group_bounds(const Project& project, const std::string& group) {
    return box_of(call("local_group_bounds", project, json::array({group}), true, group));
}

ProjectPtr move_group(const Project& project, const std::string& group, double dx, double dy, double dz, bool validate) {
    json layout = call("move_group", project, json::array({group, scalar(dx), scalar(dy), scalar(dz)}), false, group);
    return furniture_layout::placed(project, group, layout_of(layout), validate);
}

ProjectPtr rotate_group(const Project& project, const std::string& group, double angle, const std::optional<std::vector<double>>& pivot) {
    json wire_pivot = nullptr;
    if (pivot) {
        wire_pivot = json::array();
        if (pivot->size() == 3) for (double v : *pivot) wire_pivot.push_back(scalar(v));
    }
    json layout = call("rotate_group", project, json::array({group, scalar(angle), wire_pivot}), true, group);
    return furniture_layout::placed(project, group, layout_of(layout));
}

ProjectPtr snap_group(const Project& project, const std::string& group, const std::string& target_group, const std::string& direction, const json& align,
                      const json& gap) {
    json layout = call("snap_group", project, json::array({group, target_group, direction, align, scalar(gap)}), true);
    return furniture_layout::placed(project, group, layout_of(layout));
}

std::pair<ProjectPtr, json> nearest_snap(const Project& project, const std::string& group, const json& tolerance, bool align, bool validate) {
    json result = call("nearest_snap", project, json::array({group, scalar(tolerance), align}), true);
    if (result.at("layout").is_null()) return {std::make_shared<Project>(project), nullptr};
    json info = result.at("info");
    if (!info.is_null()) {
        json picked = json::object();
        for (const char* key : {"group", "target_group", "direction", "delta", "distance"}) picked[key] = info.at(key);
        info = picked;
    }
    return {furniture_layout::placed(project, group, layout_of(result.at("layout")), validate), info};
}

json contacts(const Project& project, const std::optional<std::string>& group, const json& tolerance) {
    json args = json::array({group ? json(*group) : json(nullptr), scalar(tolerance)});
    json out = json::array();
    for (auto& item : call("contacts", project, args, true)) {
        json picked = json::object();
        for (const char* key : {"group", "target_group", "direction", "gap"}) picked[key] = item.at(key);
        out.push_back(picked);
    }
    return out;
}

}  // namespace native_layout
