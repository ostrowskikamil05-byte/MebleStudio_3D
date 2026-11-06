// Visible face hit to exact local U,V,W; no CAD work during pointer movement.
#include "attachment_picker.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "depth_raster.h"
#include "gpu_viewport.h"

namespace attachment_picker {

using core::inverse_layout_point;
using core::rotate;
using core::explosion_shift;
using core::Part;
using core::Point3;
using depth_raster::pick_visible;
using depth_raster::triangulate;

Point3 inverse_world(const Part& part, const Point3& point) {
    Point3 p = inverse_layout_point(part, point);
    p = {p[0] - part.x, p[1] - part.y, p[2] - part.z};
    // Construction transform is Rx then Ry then Rz, invert in reverse order.
    p = rotate(p, 0, 0, -part.rz); p = rotate(p, 0, -part.ry, 0); p = rotate(p, -part.rx, 0, 0);
    return part.plane == "XY" ? p : part.plane == "XZ" ? Point3{p[0], p[2], p[1]} : Point3{p[1], p[2], p[0]};
}

std::optional<std::array<double, 3>> _barycentric(const std::array<double, 2>& point, const std::array<std::array<double, 2>, 3>& triangle) {
    auto [ax, ay] = triangle[0]; auto [bx, by] = triangle[1]; auto [cx, cy] = triangle[2]; auto [x, y] = point;
    double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::abs(den) < 1e-9) return std::nullopt;
    double a = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den;
    double b = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den; double c = 1 - a - b;
    return std::min({a, b, c}) >= -1e-6 ? std::optional<std::array<double, 3>>({a, b, c}) : std::nullopt;
}

std::optional<json> pick(App* app, int x, int y) {
    std::optional<std::string> identity = pick_visible(app->canvas, x, y);
    core::ProjectPtr display = app->display_project();
    const Part* part = nullptr;
    for (auto& p : display->parts) if (identity && p.id == *identity) { part = &p; break; }
    std::optional<Point3> surface;
    auto& native = app->canvas->_native_world;
    if (native) {
        json hit = native(x, y);
        if (py::truthy(hit) && py::get(hit, "kind") == "part") {
            display = app->display_project();
            part = nullptr;
            for (auto& p : display->parts) if (json(p.id) == hit["id"]) { part = &p; break; }
            if (part != nullptr) {
                Point3 shift = explosion_shift(*part, app->camera, app->explode.get() ? .22 : 0);
                surface = Point3{hit["world"][0].get<double>() - shift[0], hit["world"][1].get<double>() - shift[1], hit["world"][2].get<double>() - shift[2]};
            }
        }
    }
    if (app->canvas->_active_renderer == "gpu") {
        json hit = gpu_viewport::pick_world(app->canvas, x, y);
        if (py::truthy(hit) && py::get(hit, "kind") == "part" && part != nullptr) {
            surface = Point3{hit["world"][0].get<double>(), hit["world"][1].get<double>(), hit["world"][2].get<double>()};
        }
    }
    if (part) {
        std::optional<std::pair<double, Point3>> best;
        const std::string& renderer = app->canvas->_active_renderer;
        if (renderer != "gpu" && renderer != "native") {
            json wanted = identity ? json(*identity) : json(nullptr);
            for (auto& face : app->drawn) {
                if (face.at("id") != wanted || py::truthy(py::get(face, "fitting")) || !py::truthy(py::get(face, "world"))) continue;
                for (auto& indices : triangulate(face["points"])) {
                    std::array<std::array<double, 2>, 3> triangle;
                    for (int j = 0; j < 3; ++j) triangle[j] = {face["points"][indices[j]][0].get<double>(), face["points"][indices[j]][1].get<double>()};
                    auto weights = _barycentric({(double)x, (double)y}, triangle);
                    if (!weights) continue;
                    Point3 xyz{0, 0, 0};
                    for (int k = 0; k < 3; ++k)
                        for (int j = 0; j < 3; ++j) xyz[k] += (*weights)[j] * face["world"][indices[j]][k].get<double>();
                    double depth = app->camera.view(xyz)[2];
                    if (!best || depth > best->first) best = std::make_pair(depth, xyz);
                }
            }
        }
        if (best) surface = best->second;
        if (surface) {
            Point3 local = inverse_world(*part, *surface);
            std::vector<std::pair<double, std::string>> distances{{std::abs(local[2] - part->t), "top"}, {std::abs(local[2]), "bottom"}, {std::abs(local[0]), "u0"}, {std::abs(local[0] - part->u), "u1"}, {std::abs(local[1]), "v0"}, {std::abs(local[1] - part->v), "v1"}};
            return json{{"target_id", part->id}, {"local_hit", {{"u", local[0]}, {"v", local[1]}, {"w", local[2]}, {"face", std::min_element(distances.begin(), distances.end())->second}}}};
        }
    }
    // Lamp placement can use the room floor even without a Part target.
    auto [ca, sa, cb, sb] = app->camera._basis(); double scale = app->camera.scale;
    double u = (x - app->canvas->winfo_width() / 2.0 - app->camera.pan[0]) / scale;
    double v = -(y - app->canvas->winfo_height() / 2.0 - app->camera.pan[1]) / scale;
    json floor = py::get(py::get(app->project->scene, "architecture", json::object()), "z", 0); double z = floor.get<double>() - app->camera.center[2];
    if (std::abs(sb) < .01) return std::nullopt;
    double d = (v - z * cb) / sb;
    return json{{"target_id", nullptr}, {"local_hit", {{"world", {app->camera.center[0] + u * ca + d * sa, app->camera.center[1] - u * sa + d * ca, floor}}}}};
}

}  // namespace attachment_picker
