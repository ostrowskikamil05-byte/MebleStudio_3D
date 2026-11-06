// Direct purchased-object placement, one undo step and cheap move previews.
#include "room_scene_drag.h"

#include <chrono>
#include <cmath>
#include <set>

#include "app.h"
#include "furniture_layout.h"
#include "furniture_layout_ui.h"
#include "pyutil.h"
#include "room_objects.h"

namespace room_scene_drag {

using core::Point3;
using core::Project;
using core::ProjectPtr;

namespace {

double monotonic() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

std::pair<json, std::optional<std::map<int, double>>> snap_object(const Project& project, const json& obj, double tolerance, bool vertical) {
    auto [lo, hi] = room_objects::object_bounds(obj);
    std::map<int, double> best;
    std::vector<std::pair<Point3, Point3>> bounds;
    {
        std::set<std::string> seen;
        for (const auto& p : project.parts) {
            if (seen.insert(p.group).second) bounds.push_back(furniture_layout::group_bounds(project, p.group));
        }
    }
    for (const auto& o : py::get(project.scene, "objects", json::array()))
        if (o.at("id") != obj.at("id") && py::truthy(o.at("enabled"))) bounds.push_back(room_objects::object_bounds(o));
    for (const auto& [a, b] : bounds) {
        for (int axis : vertical ? std::vector<int>{2} : std::vector<int>{0, 1}) {
            // Only align surfaces whose other horizontal range overlaps. This
            // keeps distant cabinets from attracting an appliance across space.
            int other = axis < 2 ? 1 - axis : -1;
            if (other >= 0 && (hi[other] < a[other] - tolerance || lo[other] > b[other] + tolerance)) continue;
            for (auto [source, target] : std::vector<std::pair<double, double>>{{lo[axis], b[axis]}, {hi[axis], a[axis]}, {lo[axis], a[axis]}, {hi[axis], b[axis]}}) {
                double delta = target - source;
                if (std::abs(delta) <= tolerance && (!best.count(axis) || std::abs(delta) < std::abs(best[axis]))) best[axis] = delta;
            }
        }
    }
    json result = obj;
    const char* names[] = {"x", "y", "z"};
    for (const auto& [axis, delta] : best) result[names[axis]] = py::to_float(result.at(names[axis])) + delta;
    if (best.empty()) return {result, std::nullopt};
    return {result, best};
}

bool begin(App* app, tk::Event& event, const std::string& identity) {
    std::string objid = identity.starts_with("scene:") ? identity.substr(6) : identity;
    const json* found = nullptr;
    json objects = py::get(app->project->scene, "objects", json::array());
    for (const auto& o : objects) if (o.at("id") == objid) { found = &o; break; }
    if (found == nullptr) return false;
    auto data = std::make_shared<SceneDrag>();
    data->base = app->project;
    data->obj = *found;
    data->start = {event.x, event.y};
    data->candidate = nullptr;
    data->last = 0;
    app->scene_drag = data;
    app->start = app->last = {event.x, event.y};
    app->dragged = false;
    return true;
}

bool move(App* app, tk::Event& event, bool force) {
    std::shared_ptr<SceneDrag> data = app->scene_drag;
    if (!data) return false;
    double dx = event.x - data->start.first, dy = event.y - data->start.second;
    if (std::hypot(dx, dy) < 3 && data->candidate.is_null()) return true;
    if (!force && monotonic() - data->last < .033) return true;
    data->last = monotonic();
    try {
        auto movement = furniture_layout_ui::screen_move(app->camera, dx, dy, (event.state & 1) != 0);
        json obj = data->obj;
        const char* names[] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) obj[names[i]] = py::to_float(obj.at(names[i])) + movement[(size_t)i];
        bool contact = false;
        if (app->layout_magnet.get()) {
            auto snapped = snap_object(*data->base, obj, app->layout_tolerance.get(), (event.state & 1) != 0);
            obj = snapped.first;
            contact = snapped.second.has_value();
        }
        // Share all manufactured parts, copy only the moving object and scene.
        auto preview = std::make_shared<Project>(*data->base);
        json objects = json::array();
        for (const auto& o : data->base->scene.at("objects")) objects.push_back(o.at("id") == obj.at("id") ? obj : o);
        preview->scene["objects"] = objects;
        data->candidate = obj;
        app->project = preview;
        app->dragged = true;
        app->_view_interactive = true;
        app->draw();
        app->status.set(std::string(contact ? "Magnes: wyrównano krawędź · " : "") + "Przesuwanie wyposażenia · Shift: wysokość · puść, aby zapisać");
    } catch (const py::ValueError& error) {
        data->candidate = nullptr;
        app->project = data->base;
        app->status.set(error.what());
    }
    return true;
}

bool end(App* app, tk::Event& event) {
    std::shared_ptr<SceneDrag> data = app->scene_drag;
    if (!data) return false;
    move(app, event, true);
    app->scene_drag = nullptr;
    app->project = data->base;
    if (!data->candidate.is_null()) {
        ProjectPtr candidate = room_objects::update_object(*data->base, data->obj.at("id").get<std::string>(),
                                                           json{{"x", data->candidate.at("x")}, {"y", data->candidate.at("y")}, {"z", data->candidate.at("z")}});
        candidate->validate();
        if (candidate->data() != data->base->data()) {
            app->snapshot();
            app->project = candidate;
            app->refresh();
        }
    }
    app->status.set("Zapisano pozycję wyposażenia · Ctrl+Z cofa cały ruch");
    return true;
}

bool cancel(App* app) {
    std::shared_ptr<SceneDrag> data = app->scene_drag;
    if (!data) return false;
    app->project = data->base;
    app->scene_drag = nullptr;
    app->refresh();
    return true;
}

}  // namespace room_scene_drag
