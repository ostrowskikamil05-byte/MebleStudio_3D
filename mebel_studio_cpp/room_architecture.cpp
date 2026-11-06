// Presentation room enclosure; never creates manufactured furniture parts.
#include "room_architecture.h"

#include <cmath>
#include <numbers>

namespace room_architecture {

using core::Point3;
using core::finite;
using py::ValueError;

namespace {
// math.radians
double radians(double degrees) { return degrees * (std::numbers::pi / 180.0); }

bool is_number(const json& v) { return v.is_number_integer() || v.is_number_float(); }  // type(x) in (int, float); bool jest osobnym typem

json point(const Point3& p) { return json::array({p[0], p[1], p[2]}); }
}  // namespace

json defaults() {
    return json{{"enabled", false}, {"width", 6000}, {"depth", 4500}, {"height", 2800}, {"x", -600}, {"y", -600}, {"z", 0},
                {"wall_color", "#c8d8dd"}, {"floor_color", "#bc9567"}, {"cutaway", true}, {"ceiling", false}};
}

json validate(const json& value) {
    json base = defaults();
    if (!value.is_object()) throw ValueError("Nieprawidłowe parametry ścian pomieszczenia.");
    for (auto& [key, item] : value.items()) if (!base.contains(key)) throw ValueError("Nieprawidłowe parametry ścian pomieszczenia.");
    json result = base;
    for (auto& [key, item] : value.items()) result[key] = item;
    for (const char* key : {"enabled", "cutaway", "ceiling"})
        if (!result[key].is_boolean()) throw ValueError("Widoczność ścian i sufitu wymaga TAK / NIE.");
    for (const char* key : {"width", "depth", "height"}) {
        if (!is_number(result[key])) throw ValueError("Podaj liczbowy wymiar pomieszczenia.");
        result[key] = finite(result[key], 100, 50000);
    }
    for (const char* key : {"x", "y", "z"}) {
        if (!is_number(result[key])) throw ValueError("Podaj liczbową pozycję pomieszczenia.");
        result[key] = finite(result[key], -100000, 100000);
    }
    for (const char* key : {"wall_color", "floor_color"}) {
        const json& c = result[key];
        if (!c.is_string() || py::len(c.get<std::string>()) != 7 || c.get<std::string>()[0] != '#') throw ValueError("Kolor pomieszczenia: #RRGGBB.");
        try { py::int16(c.get<std::string>().substr(1)); }
        catch (const std::invalid_argument&) { throw ValueError("Kolor pomieszczenia: #RRGGBB."); }
    }
    return result;
}

json world_faces(const core::Project& project, const core::Camera* camera) {
    json cfg = validate(py::get(project.scene, "architecture", json::object()));
    if (!cfg["enabled"].get<bool>()) return json::array();
    double x = cfg["x"], y = cfg["y"], z = cfg["z"], w = cfg["width"], d = cfg["depth"], h = cfg["height"];
    struct Surface { std::string name; std::vector<Point3> points; Point3 normal; std::string color; };
    std::string wall = cfg["wall_color"], floor_color = cfg["floor_color"];
    std::vector<Surface> surfaces{
        {"floor", {{x, y, z}, {x + w, y, z}, {x + w, y + d, z}, {x, y + d, z}}, {0, 0, 1}, floor_color},
        {"west", {{x, y + d, z}, {x, y, z}, {x, y, z + h}, {x, y + d, z + h}}, {1, 0, 0}, wall},
        {"east", {{x + w, y, z}, {x + w, y + d, z}, {x + w, y + d, z + h}, {x + w, y, z + h}}, {-1, 0, 0}, wall},
        {"north", {{x + w, y + d, z}, {x, y + d, z}, {x, y + d, z + h}, {x + w, y + d, z + h}}, {0, -1, 0}, wall},
        {"south", {{x, y, z}, {x + w, y, z}, {x + w, y, z + h}, {x, y, z + h}}, {0, 1, 0}, wall}};
    if (cfg["ceiling"].get<bool>())
        surfaces.push_back({"ceiling", {{x, y + d, z + h}, {x + w, y + d, z + h}, {x + w, y, z + h}, {x, y, z + h}}, {0, 0, -1}, wall});
    std::optional<Point3> near_vector;
    if (camera != nullptr) {
        double yaw = radians(camera->yaw), pitch = radians(camera->pitch);
        near_vector = Point3{-std::sin(yaw) * std::cos(pitch), -std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
    }
    json result = json::array();
    for (const auto& s : surfaces) {
        if (cfg["cutaway"].get<bool>() && near_vector) {
            double dot = py::sum({s.normal[0] * (*near_vector)[0], s.normal[1] * (*near_vector)[1], s.normal[2] * (*near_vector)[2]});
            if (dot <= .001) continue;
        }
        json points = json::array();
        for (const auto& p : s.points) points.push_back(point(p));
        Point3 center{};
        for (int i = 0; i < 3; ++i) center[i] = py::sum({s.points[0][i], s.points[1][i], s.points[2][i], s.points[3][i]}) / 4;
        result.push_back(json{{"id", "scene:room:" + s.name}, {"world", points}, {"color", s.color},
                              {"normal", json::array({(int)s.normal[0], (int)s.normal[1], (int)s.normal[2]})}, {"center", point(center)},
                              {"scene_object", true}, {"architecture", true}, {"emissive", false}});
    }
    return result;
}

json world_faces(const core::Project& project, const core::Camera& camera) { return world_faces(project, &camera); }

std::vector<Point3> vertices(const core::Project& project) {
    json cfg = validate(py::get(project.scene, "architecture", json::object()));
    if (!cfg["enabled"].get<bool>()) return {};
    double x = cfg["x"], y = cfg["y"], z = cfg["z"], w = cfg["width"], d = cfg["depth"], h = cfg["height"];
    std::vector<Point3> out;
    for (int i : {0, 1}) for (int j : {0, 1}) for (int k : {0, 1}) out.push_back({x + i * w, y + j * d, z + k * h});
    return out;
}

}  // namespace room_architecture
