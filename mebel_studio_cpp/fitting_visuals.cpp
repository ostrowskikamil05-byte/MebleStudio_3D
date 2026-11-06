// Simple fitting geometry for visualisation; purchased items, not manufactured panels.
#include "fitting_visuals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "configurator_accessories.h"
#include "opening_options.h"

namespace fitting_visuals {

using core::Part;
using core::Point3;
using core::Project;
using core::layout_point;
using core::local_world;
using core::world;
using json = core::json;

namespace {

constexpr double TAU = 2 * std::numbers::pi;

double num(const json& value) { return value.get<double>(); }

void extend(Faces& target, const Faces& more) { target.insert(target.end(), more.begin(), more.end()); }

std::vector<FittingFaceMeta> collect(const Project& project, const std::optional<std::set<std::string>>& kinds) {
    std::vector<FittingFaceMeta> out;
    std::map<std::string, const Part*> owners;
    for (const Part& p : project.parts) owners[p.id] = &p;

    auto append = [&](const std::string& owner, const Faces& faces_, const std::string& color, bool construction = false, std::optional<int> sign = std::nullopt) {
        Faces faces = faces_;
        if (construction) {
            for (auto& face : faces) for (auto& point : face) point = layout_point(*owners.at(owner), point);
        }
        int s;
        if (sign) s = *sign;
        else s = construction || owners.at(owner)->plane != "XZ" ? 1 : -1;
        for (auto& face : faces) out.push_back({owner, face, color, s});
    };
    // [[world(p,*point) for point in face] for face in local]
    auto to_world = [](const Part& p, const Faces& local) {
        Faces faces;
        for (auto& face : local) {
            std::vector<Point3> converted;
            for (auto& point : face) converted.push_back(world(p, point[0], point[1], point[2]));
            faces.push_back(std::move(converted));
        }
        return faces;
    };
    // [[(u,v,p.t-w) for u,v,w in reversed(face)] for face in local]
    auto mirrored = [](const Part& p, const Faces& local) {
        Faces faces;
        for (auto& face : local) {
            std::vector<Point3> converted;
            for (auto it = face.rbegin(); it != face.rend(); ++it) converted.push_back({(*it)[0], (*it)[1], p.t - (*it)[2]});
            faces.push_back(std::move(converted));
        }
        return faces;
    };

    for (const json& h : project.hardware) {
        json kind_value = py::get(h, "kind");
        if (kinds && !(kind_value.is_string() && kinds->contains(kind_value.get<std::string>()))) continue;
        json owner_id = py::get(h, "owner_id");
        const Part* p = nullptr;
        if (owner_id.is_string()) { auto found = owners.find(owner_id.get<std::string>()); if (found != owners.end()) p = found->second; }
        if (!p || !p->visible) continue;
        std::string kind = kind_value.is_string() ? kind_value.get<std::string>() : "";
        json color_value = py::get(h, "color", "#aab0b6");
        std::string color = color_value.is_string() ? color_value.get<std::string>() : py::str(color_value);

        if (kind == "handle") {
            double u = num(h.at("u")), v = num(h.at("v"));
            json spacing_value = h.at("spacing"); double spacing = num(spacing_value); double projection = num(h.at("projection"));
            bool vertical = py::truthy(h.at("vertical"));
            Faces local;
            if (py::truthy(spacing_value)) {
                for (double k : {-spacing / 2, spacing / 2}) extend(local, box(u - 4 + (vertical ? 0 : k), v - 4 + (vertical ? k : 0), -projection, 8, 8, projection));
                extend(local, box(vertical ? u - 5 : u - spacing / 2 - 5, vertical ? v - spacing / 2 - 5 : v - 5, -projection - 6, vertical ? 10 : spacing + 10, vertical ? spacing + 10 : 10, 8));
            } else {
                local = box(u - 10, v - 10, -projection, 20, 20, projection);
            }
            if (py::get(h, "mount_face", "bottom") == "top") local = mirrored(*p, local);
            append(p->id, to_world(*p, local), color);
        } else if (kind == "opening") {
            append(p->id, opening_options::opening_faces(project, *p, h), color, false, 0);
        } else if (kind == "leg") {
            double height = num(py::get(h, "height", 0)), diameter = num(py::get(h, "diameter", 40));
            if (height <= 0 || diameter <= 0) continue;
            double foot_height = std::min(6.0, height / 4);
            json positions = py::get(h, "positions", json::array());
            for (const json& position : positions) {
                double x = num(position.at("x")), y = num(position.at("y")), z = num(position.at("z"));
                Faces geometry = cylinder(x, y, z, num(py::get(h, "foot_diameter", diameter * 1.15)) / 2, foot_height);
                extend(geometry, cylinder(x, y, z + foot_height, diameter / 2, height - foot_height));
                append(p->id, geometry, color, true);
            }
        } else if (kind == "led_strip") {
            for (auto& [face, finish] : configurator_accessories::led_local_geometry(*p, h)) {
                append(p->id, to_world(*p, Faces{face}), finish);
            }
        } else if (kind == "hinge") {
            json cfg = py::get(p->assembly, "config", json::object());
            json side_value = py::get(cfg, "hinge_side", "left"); bool left_side = side_value == "left";
            double cup = num(py::get(cfg, "cup_edge", 5)) + 17.5; double u = left_side ? cup : p->u - cup;
            json levels = py::get(h, "levels", json::array());
            for (const json& level : levels) {
                double v = num(level);
                Faces local = box(u - 14, v - 16, p->t, 28, 32, 3);
                extend(local, box(std::max(0.0, left_side ? u - 15 : u), v - 6, p->t, 15, 12, 12));
                if (py::get(h, "mount_face", "top") == "bottom") local = mirrored(*p, local);
                append(p->id, to_world(*p, local), color);
            }
        } else if (kind == "runner") {
            json positions = py::get(h, "positions", json::array());
            for (const json& position : positions) {
                double px = num(position.at("x")), py_ = num(position.at("y")), pz = num(position.at("z"));
                append(p->id, box(px - 5, py_, pz - 15, 10, num(py::get(h, "length", 400)), 30), color, true);
                double shift = p->y - num(py::get(h, "closed_front_y", p->y));
                append(p->id, box(px - 3, py_ + shift, pz - 8, 6, num(py::get(h, "length", 400)), 16), "#6c747d", true);
            }
        } else if (kind == "sliding") {
            json origin = h.at("origin"); double x = num(origin.at(0)), y = num(origin.at(1)), z = num(origin.at(2)); double w = num(h.at("width"));
            for (double top : {z, z + num(h.at("height")) + 90}) {
                for (double lane : {0.0, 22.0}) append(p->id, box(x, y - 45 - lane, top, w, 6, 8), color, true);
            }
        } else if (kind == "cargo") {
            json origin = h.at("origin"); double x = num(origin.at(0)), y = num(origin.at(1)), z = num(origin.at(2));
            double w = num(h.at("width")), d = num(h.at("depth")), height = num(h.at("height"));
            y += p->y - num(py::get(h, "closed_y", p->y));
            if (std::min({w, d, height}) <= 0) continue;
            long long baskets = (long long)num(py::get(h, "baskets", 3));
            for (long long i = 0; i < baskets; ++i) {
                double level = z + i * height / std::max<long long>(1, baskets - 1);
                Faces geometry = box(x, y, level, w, d, 4);
                for (double a : {x, x + w - 4}) extend(geometry, box(a, y, level, 4, d, 70));
                for (double b : {y, y + d - 4}) extend(geometry, box(x, b, level, w, 4, 70));
                append(p->id, geometry, color, true);
            }
        } else if (kind == "lift") {
            json origin = h.at("origin"); double x = num(origin.at(0)), y = num(origin.at(1)), z = num(origin.at(2));
            for (double a : {x, x + num(h.at("width")) - 15}) {
                append(p->id, box(a, y, z + num(h.at("height")) - 80, 15, 100, 80), color, true);
                Point3 target = local_world(*p, a == x ? 20 : p->u - 20, p->v - 60, p->t);
                append(p->id, rod({a, y, z + num(h.at("height")) - 40}, target, 4), color, true);
            }
        }
    }
    return out;
}

}  // namespace

Faces box(double x, double y, double z, double u, double v, double t) {
    static const int corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    static const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    Point3 points[8];
    for (int i = 0; i < 8; ++i) points[i] = {x + corners[i][0] * u, y + corners[i][1] * v, z + corners[i][2] * t};
    Faces out;
    for (auto& indices : faces) out.push_back({points[indices[0]], points[indices[1]], points[indices[2]], points[indices[3]]});
    return out;
}

std::vector<FittingFace> fitting_faces(const Project& project, const std::optional<std::set<std::string>>& kinds) {
    std::vector<FittingFace> out;
    for (auto& item : collect(project, kinds)) out.push_back({item.owner, item.face, item.color});
    return out;
}

std::vector<FittingFaceMeta> fitting_faces_with_metadata(const Project& project, const std::optional<std::set<std::string>>& kinds) {
    return collect(project, kinds);
}

Faces cylinder(double x, double y, double z, double r, double height, int segments) {
    if (r <= 0 || height <= 0) return {};
    std::vector<Point3> rings[2];
    int index = 0;
    for (double level : {0.0, height}) {
        for (int i = 0; i < segments; ++i) rings[index].push_back({x + r * std::cos(i * TAU / segments), y + r * std::sin(i * TAU / segments), z + level});
        ++index;
    }
    Faces out;
    out.push_back(std::vector<Point3>(rings[0].rbegin(), rings[0].rend()));
    out.push_back(rings[1]);
    for (int i = 0; i < segments; ++i) out.push_back({rings[0][i], rings[0][(i + 1) % segments], rings[1][(i + 1) % segments], rings[1][i]});
    return out;
}

Faces slot_prism(double u, double v, double z, double length, double width, double height, bool vertical, int segments) {
    double radius = width / 2, half = (length - width) / 2;
    if (radius <= 0 || height <= 0 || half < 0) return {};
    std::vector<std::array<double, 2>> outline;
    for (auto [center, start] : {std::pair{half, -std::numbers::pi / 2}, std::pair{-half, std::numbers::pi / 2}}) {
        for (int i = 0; i <= segments; ++i) {
            double angle = start + i * std::numbers::pi / segments;
            double a = center + radius * std::cos(angle), b = radius * std::sin(angle);
            outline.push_back(vertical ? std::array<double, 2>{u - b, v + a} : std::array<double, 2>{u + a, v + b});
        }
    }
    std::vector<Point3> rings[2];
    int index = 0;
    for (double level : {0.0, height}) {
        for (auto& [a, b] : outline) rings[index].push_back({a, b, z + level});
        ++index;
    }
    int n = (int)outline.size();
    Faces out;
    out.push_back(std::vector<Point3>(rings[0].rbegin(), rings[0].rend()));
    out.push_back(rings[1]);
    for (int i = 0; i < n; ++i) out.push_back({rings[0][i], rings[0][(i + 1) % n], rings[1][(i + 1) % n], rings[1][i]});
    return out;
}

Faces rod(const Point3& a, const Point3& b, double r) {
    double delta[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double length = std::sqrt(py::sum({delta[0] * delta[0], delta[1] * delta[1], delta[2] * delta[2]}));
    if (length < 1e-6) return {};
    double direction[3] = {delta[0] / length, delta[1] / length, delta[2] / length};
    double up[3] = {0, 0, 1};
    if (!(std::abs(direction[2]) < .9)) { up[0] = 1; up[1] = 0; up[2] = 0; }
    double u[3] = {direction[1] * up[2] - direction[2] * up[1], direction[2] * up[0] - direction[0] * up[2], direction[0] * up[1] - direction[1] * up[0]};
    double scale = std::sqrt(py::sum({u[0] * u[0], u[1] * u[1], u[2] * u[2]}));
    for (double& x : u) x = x / scale;
    double v[3] = {direction[1] * u[2] - direction[2] * u[1], direction[2] * u[0] - direction[0] * u[2], direction[0] * u[1] - direction[1] * u[0]};
    static const int signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    std::vector<Point3> rings[2];
    int index = 0;
    for (const Point3* point : {&a, &b}) {
        for (auto& s : signs) {
            Point3 corner;
            for (int i = 0; i < 3; ++i) corner[i] = (*point)[i] + r * (u[i] * s[0] + v[i] * s[1]);
            rings[index].push_back(corner);
        }
        ++index;
    }
    Faces out;
    out.push_back(std::vector<Point3>(rings[0].rbegin(), rings[0].rend()));
    out.push_back(rings[1]);
    for (int i = 0; i < 4; ++i) out.push_back({rings[0][i], rings[0][(i + 1) % 4], rings[1][(i + 1) % 4], rings[1][i]});
    return out;
}

}  // namespace fitting_visuals
