// Sparse, view-dependent dimensions and depth-tested manufacturing marks.
#include "viewport_annotations.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <set>

#include "app.h"
#include "configurator_accessories.h"
#include "corner_furniture.h"
#include "depth_raster.h"
#include "features.h"
#include "furniture_configurator.h"
#include "pyutil.h"

namespace viewport_annotations {

using core::Point3;
using json = core::json;

namespace {

double hypot2(double dx, double dy) { return std::hypot(dx, dy); }

// text[:count] w punktach kodowych UTF-8
std::string utf8_prefix(const std::string& text, size_t count) {
    size_t i = 0, n = 0;
    while (i < text.size() && n < count) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        ++n;
    }
    return text.substr(0, std::min(i, text.size()));
}

size_t utf8_length(const std::string& text) {
    size_t n = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

long long floor_div(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// One exposed edge per measurable axis, avoiding end-on dimensions.
std::vector<DimensionSpec> box_edges(const Point3& lo, const Point3& hi, const std::function<Point3(const Point3&)>& transform, const core::Camera& camera,
                                     std::pair<int, int> size, tk::Canvas* canvas) {
    std::vector<DimensionSpec> result;
    for (int axis = 0; axis < 3; ++axis) {
        struct Candidate { int visible; double depth; Point3 qa, qb; };
        std::vector<Candidate> candidates;
        std::array<int, 2> other{};
        int k = 0;
        for (int i = 0; i < 3; ++i)
            if (i != axis) other[k++] = i;
        for (int e0 = 0; e0 < 2; ++e0)
            for (int e1 = 0; e1 < 2; ++e1) {
                std::array<int, 2> ends{e0, e1};
                Point3 a = lo, b = lo;
                b[axis] = hi[axis];
                for (int j = 0; j < 2; ++j) {
                    int i = other[j];
                    a[i] = b[i] = ends[j] ? hi[i] : lo[i];
                }
                Point3 qa = camera.project(transform(a), size.first, size.second);
                Point3 qb = camera.project(transform(b), size.first, size.second);
                double length = hypot2(qb[0] - qa[0], qb[1] - qa[1]);
                if (length < 24) continue;
                int visible = 0;
                for (double t : {.03, .25, .5, .75, .97})
                    visible += depth_raster::screen_visibility(canvas, qa[0] + (qb[0] - qa[0]) * t, qa[1] + (qb[1] - qa[1]) * t, qa[2] + (qb[2] - qa[2]) * t, std::nullopt, 2);
                if (visible >= 2) candidates.push_back({visible, (qa[2] + qb[2]) / 2, qa, qb});
            }
        if (!candidates.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < candidates.size(); ++i)
                if (std::pair(candidates[i].visible, candidates[i].depth) > std::pair(candidates[best].visible, candidates[best].depth)) best = i;
            DimensionSpec spec;
            spec.axis = axis;
            spec.a = candidates[best].qa;
            spec.b = candidates[best].qb;
            spec.value = hi[axis] - lo[axis];
            result.push_back(spec);
        }
    }
    return result;
}

}  // namespace

Point3 project_local(App* app, const core::Part& part, const Point3& point) {
    Point3 shift = core::explosion_shift(part, app->camera, app->explode.get() ? .22 : 0);
    Point3 xyz = core::world(part, point[0], point[1], point[2]);
    return app->camera.project({xyz[0] + shift[0], xyz[1] + shift[1], xyz[2] + shift[2]}, app->canvas->winfo_width(), app->canvas->winfo_height());
}

std::vector<DimensionSpec> dimension_specs(App* app, const core::Project& display) {
    const core::Part* selected = nullptr;
    for (auto& p : display.parts)
        if (app->selected && p.id == *app->selected && p.visible) { selected = &p; break; }
    std::string mode = app->dimension_mode.get();
    std::pair<int, int> size{app->canvas->winfo_width(), app->canvas->winfo_height()};
    if (app->explode.get() || mode == "Wybrany element") {
        if (!selected) return {};
        Point3 shift = core::explosion_shift(*selected, app->camera, app->explode.get() ? .22 : 0);
        auto transform = [&](const Point3& point) {
            Point3 w = core::world(*selected, point[0], point[1], point[2]);
            return Point3{w[0] + shift[0], w[1] + shift[1], w[2] + shift[2]};
        };
        std::vector<DimensionSpec> specs = box_edges({0, 0, 0}, {selected->u, selected->v, selected->t}, transform, app->camera, size, app->canvas);
        for (auto& spec : specs) spec.label = std::string(1, "UVT"[spec.axis]);
        return specs;
    }
    std::vector<const core::Part*> parts;
    for (auto& p : display.parts)
        if (p.visible && (mode == "Cały zestaw" || (selected && p.group == selected->group))) parts.push_back(&p);
    if (parts.empty()) return {};
    std::vector<Point3> points;
    std::function<Point3(const Point3&)> transform;
    if (mode == "Cały zestaw") {
        for (auto* p : parts)
            for (auto& [u, v] : p->points())
                for (double w : {0.0, p->t}) points.push_back(core::world(*p, u, v, w));
        for (auto& q : configurator_accessories::accessory_vertices(display)) points.push_back(q);
        transform = [](const Point3& point) { return point; };
    } else {
        json cfg = furniture_configurator::read_config(*app->project, selected->group);
        if (!py::truthy(cfg)) cfg = corner_furniture::read_corner_config(*app->project, selected->group);
        if (py::truthy(cfg)) {
            double ox = cfg.at("origin").at(0).get<double>(), oy = cfg.at("origin").at(1).get<double>(), oz = cfg.at("origin").at(2).get<double>();
            double width = cfg.at("width").get<double>(), depth = cfg.at("depth").get<double>(), height = cfg.at("height").get<double>();
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z) points.push_back({ox + x * width, oy + y * depth, oz + z * height});
        } else {
            for (auto* p : parts)
                for (auto& [u, v] : p->points())
                    for (double w : {0.0, p->t}) points.push_back(core::local_world(*p, u, v, w));
        }
        transform = [selected](const Point3& point) { return core::layout_point(*selected, point); };
    }
    Point3 lo = points[0], hi = points[0];
    for (auto& q : points)
        for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], q[i]); hi[i] = std::max(hi[i], q[i]); }
    std::vector<DimensionSpec> specs = box_edges(lo, hi, transform, app->camera, size, app->canvas);
    static const char* const WHOLE[] = {"X", "Y", "Z"};
    static const char* const FURNITURE[] = {"S", "G", "H"};
    for (auto& spec : specs) spec.label = mode == "Cały zestaw" ? WHOLE[spec.axis] : FURNITURE[spec.axis];
    return specs;
}

void paint_dimensions(App* app, const core::Project& display) {
    app->visible_dimensions = json::array();
    if (!app->dimensions.get()) return;
    tk::Canvas* c = app->canvas;
    std::pair<int, int> size{c->winfo_width(), c->winfo_height()};
    std::vector<DimensionSpec> specs = dimension_specs(app, display);
    if (specs.empty()) return;
    core::Part* current = app->selected_part();
    std::set<std::string> selected_ids;
    if (current)
        for (auto& p : display.parts)
            if (p.group == current->group) selected_ids.insert(p.id);
    std::vector<double> xs, ys;
    for (auto& f : app->drawn) {
        const json& id = f.at("id");
        bool take = (app->selected && id == json(*app->selected)) || (id.is_string() && selected_ids.count(id.get<std::string>()));
        if (!take) continue;
        for (auto& q : f.at("points")) { xs.push_back(q.at(0).get<double>()); ys.push_back(q.at(1).get<double>()); }
    }
    std::pair<double, double> center = !xs.empty() ? std::pair<double, double>{py::sum(xs) / xs.size(), py::sum(ys) / ys.size()}
                                                   : std::pair<double, double>{size.first / 2.0, size.second / 2.0};
    struct Placed { double x, y, half; };
    std::vector<Placed> placed;
    for (auto& spec : specs) {
        const Point3 &a = spec.a, &b = spec.b;
        double mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
        double dx = b[0] - a[0], dy = b[1] - a[1];
        double length = hypot2(dx, dy);
        if (length == 0) throw std::domain_error("float division by zero");
        double nx = -dy / length, ny = dx / length;
        if (nx * (mx - center.first) + ny * (my - center.second) < 0) { nx = -nx; ny = -ny; }
        double offset = 32;
        double x = mx + nx * offset, y = my + ny * offset;
        std::string text = std::format("{} {:g} mm", spec.label, spec.value);
        double half = std::max(40.0, utf8_length(text) * 3.4);
        for (int attempt = 0; attempt < 6; ++attempt) {
            bool collides = std::any_of(placed.begin(), placed.end(), [&](const Placed& q) { return std::abs(x - q.x) < half + q.half + 8 && std::abs(y - q.y) < 24; });
            if (!collides) break;
            offset += 23;
            x = mx + nx * offset;
            y = my + ny * offset;
        }
        x = std::max(half + 5, std::min(size.first - half - 5, x));
        y = std::max(45.0, std::min(size.second - 22.0, y));
        placed.push_back({x, y, half});
        double ax = a[0] + nx * offset, ay = a[1] + ny * offset, bx = b[0] + nx * offset, by = b[1] + ny * offset;
        c->create_line({a[0], a[1], ax, ay}, {{"fill", "#23889c"}, {"dash", "2 3"}, {"tags", "dimensions"}});
        c->create_line({b[0], b[1], bx, by}, {{"fill", "#23889c"}, {"dash", "2 3"}, {"tags", "dimensions"}});
        c->create_line({ax, ay, bx, by}, {{"fill", "#137b94"}, {"width", 1}, {"arrow", "both"}, {"tags", "dimensions"}});
        c->create_rectangle(x - half, y - 11, x + half, y + 11, {{"fill", "#eefbff"}, {"outline", "#91c6d2"}, {"tags", "dimensions"}});
        c->create_text(x, y, {{"text", text}, {"fill", "#123c49"}, {"font", "{Segoe UI} 9 bold"}, {"tags", "dimensions"}});
        json record = json::object();
        record["axis"] = spec.axis;
        record["a"] = json::array({a[0], a[1], a[2]});
        record["b"] = json::array({b[0], b[1], b[2]});
        record["value"] = spec.value;
        record["label"] = spec.label;
        record["x"] = x;
        record["y"] = y;
        record["text"] = text;
        app->visible_dimensions.push_back(record);
    }
    if (app->explode.get() && current && app->dimension_mode.get() != "Wybrany element") {
        std::string group = current->group;
        std::vector<const core::Part*> parts;
        for (auto& p : display.parts)
            if (p.group == group && p.visible) parts.push_back(&p);
        long long rows = std::max(1LL, std::min(12LL, floor_div(size.second - 135, 23)));
        std::vector<std::pair<const core::Part*, Point3>> visible;
        for (auto* part : parts) {
            Point3 q = project_local(app, *part, {part->u / 2, part->v / 2, part->t / 2});
            // A label table is outside the model. No text is drawn over every
            // shelf of every cabinet, so exploded assemblies remain legible.
            visible.push_back({part, q});
        }
        double x = size.first - 260, y = 55;
        double shown = (double)std::min<long long>(rows, (long long)visible.size());
        c->create_rectangle(x, y, size.first - 8, y + 46 + shown * 23, {{"fill", "#f6fbff"}, {"outline", "#a7c5d5"}, {"tags", "dimensions"}});
        c->create_text(x + 10, y + 13, {{"text", "Formatki wybranego mebla [mm]"}, {"anchor", "w"}, {"fill", "#123c49"}, {"font", "{Segoe UI} 9 bold"}, {"tags", "dimensions"}});
        for (long long n = 1; n <= std::min<long long>(rows, (long long)visible.size()); ++n) {
            auto& [part, q] = visible[(size_t)(n - 1)];
            c->create_text(x + 10, y + 16 + n * 23, {{"text", std::format("{}. {} · {:g}×{:g}×{:g}", n, utf8_prefix(part->name, 18), part->u, part->v, part->t)},
                                                     {"anchor", "w"}, {"fill", "#25394c"}, {"font", "{Segoe UI} 9"}, {"tags", "dimensions"}});
            if (depth_raster::screen_visibility(c, q[0], q[1], q[2], std::nullopt, part->t)) {
                c->create_oval(q[0] - 8, q[1] - 8, q[0] + 8, q[1] + 8, {{"fill", "#eefbff"}, {"outline", "#137b94"}, {"tags", "dimensions"}});
                c->create_text(q[0], q[1], {{"text", std::to_string(n)}, {"fill", "#123c49"}, {"font", "{Segoe UI} 8 bold"}, {"tags", "dimensions"}});
            }
        }
        std::string footer = (long long)visible.size() > rows ? "Wszystkie formatki: zakładka Formatki / Drukuj" : "Wymiary nominalne U × V × grubość";
        c->create_text(x + 10, y + 34 + shown * 23, {{"text", footer}, {"anchor", "w"}, {"fill", "#56717f"}, {"font", "{Segoe UI} 8"}, {"tags", "dimensions"}});
    }
}

void paint_drills(App* app, const core::Project& display) {
    app->visible_drills.clear();
    if (!app->show_drills.get() || app->_view_interactive) return;
    tk::Canvas* c = app->canvas;
    bool through = app->drill_mode.get() == "Także zasłonięte";
    if (through)
        c->create_text(12, c->winfo_height() - 16.0, {{"text", "Otwory: wszystkie strony · niebieskie przerywane = zasłonięte"}, {"anchor", "sw"},
                                                      {"fill", "#3476b3"}, {"font", "{Segoe UI} 9 bold"}, {"tags", "mounting_holes"}});
    for (auto& part : display.parts) {
        if (!part.visible) continue;
        std::vector<json> operations(part.operations.begin(), part.operations.end());
        for (auto& hole : part.holes) {
            if (hole.size() != 4) throw py::ValueError(hole.size() < 4 ? "not enough values to unpack (expected 4, got " + std::to_string(hole.size()) + ")" : "too many values to unpack (expected 4)");
            operations.push_back(json{{"type", "drill"}, {"face", "top"}, {"u", hole[0]}, {"v", hole[1]}, {"diameter", hole[2]}, {"depth", hole[3]}, {"purpose", "otwór ręczny"}});
        }
        for (auto& operation : operations) {
            if (py::get(operation, "type") != json("drill")) continue;
            auto [origin, direction] = features::drill_location(part, operation);
            Point3 q = project_local(app, part, origin);
            if (!(0 <= q[0] && q[0] < c->winfo_width() && 0 <= q[1] && q[1] < c->winfo_height())) continue;
            Point3 outward{origin[0] - direction[0], origin[1] - direction[1], origin[2] - direction[2]};
            Point3 qn = project_local(app, part, outward);
            bool exposed = qn[2] - q[2] >= .01 && depth_raster::screen_visibility(c, q[0], q[1], q[2], std::nullopt, 1.5);
            if (!through && !exposed) continue;
            std::string color = exposed ? "#287fc2" : "#4487bc";
            double diameter = operation.at("diameter").get<double>();
            if (through) {
                // A minimum-size target is a legible annotation, not a change
                // to drill diameter. Hover retains the exact CNC dimensions.
                double radius = std::max(5.0, std::min(12.0, diameter * app->camera.scale / 2));
                std::string dash = exposed ? "" : "3 2";
                c->create_oval(q[0] - radius, q[1] - radius, q[0] + radius, q[1] + radius, {{"outline", "white"}, {"width", 5}, {"tags", "mounting_holes"}});
                c->create_oval(q[0] - radius, q[1] - radius, q[0] + radius, q[1] + radius, {{"outline", color}, {"width", 3}, {"dash", dash}, {"tags", "mounting_holes"}});
                c->create_line({q[0] - 4, q[1], q[0] + 4, q[1]}, {{"fill", color}, {"width", 2}, {"tags", "mounting_holes"}});
                c->create_line({q[0], q[1] - 4, q[0], q[1] + 4}, {{"fill", color}, {"width", 2}, {"tags", "mounting_holes"}});
                app->visible_drills.push_back({part.id, q[0], q[1], operation, !exposed});
                continue;
            }
            std::string face = operation.at("face").get<std::string>();
            std::array<int, 2> axes = (face == "top" || face == "bottom") ? std::array<int, 2>{0, 1} : face.starts_with("u") ? std::array<int, 2>{1, 2} : std::array<int, 2>{0, 2};
            std::vector<Point3> ring;
            double radius = diameter / 2;
            for (int i = 0; i < 25; ++i) {
                Point3 point = origin;
                double angle = i * (2 * std::numbers::pi) / 24;
                point[axes[0]] += radius * std::cos(angle);
                point[axes[1]] += radius * std::sin(angle);
                ring.push_back(project_local(app, part, point));
            }
            for (size_t i = 0; i + 1 < ring.size(); ++i) {
                const Point3 &a = ring[i], &b = ring[i + 1];
                Point3 mid{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2};
                if (depth_raster::screen_visibility(c, mid[0], mid[1], mid[2], std::nullopt, 1.5)) {
                    c->create_line({a[0], a[1], b[0], b[1]}, {{"fill", "white"}, {"width", 5}, {"tags", "mounting_holes"}});
                    c->create_line({a[0], a[1], b[0], b[1]}, {{"fill", color}, {"width", 3}, {"tags", "mounting_holes"}});
                }
            }
            radius = 5;
            c->create_oval(q[0] - radius, q[1] - radius, q[0] + radius, q[1] + radius, {{"outline", "white"}, {"width", 4}, {"tags", "mounting_holes"}});
            c->create_line({q[0] - 4, q[1], q[0] + 4, q[1]}, {{"fill", color}, {"width", 2}, {"tags", "mounting_holes"}});
            c->create_line({q[0], q[1] - 4, q[0], q[1] + 4}, {{"fill", color}, {"width", 2}, {"tags", "mounting_holes"}});
            app->visible_drills.push_back({part.id, q[0], q[1], operation, false});
        }
    }
}

void paint(App* app, const core::Project& display) {
    paint_dimensions(app, display);
    paint_drills(app, display);
}

}  // namespace viewport_annotations
