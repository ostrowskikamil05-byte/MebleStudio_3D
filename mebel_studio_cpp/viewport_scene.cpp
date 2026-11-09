// Tłumaczenie 1:1 viewport_scene.py (patrz viewport_scene.h).
#include "viewport_scene.h"
#include "render_style.h"
#include "view_style.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numbers>

#include "assembly.h"
#include "depth_raster.h"
#include "pyutil.h"
#include "pencil_filter.h"
#include "wood_grain.h"

namespace viewport_scene {

using core::Part;
using core::Project;

namespace {

double num(const json& value) { return py::to_float(value); }

bool flag(const json& face, const char* key) { return py::truthy(py::get(face, key)); }

std::vector<double> flat(const std::vector<Point3>& points) {
    std::vector<double> out;
    for (const auto& p : points) { out.push_back(p[0]); out.push_back(p[1]); }
    return out;
}

std::vector<double> flat2(const std::vector<Point2>& points) {
    std::vector<double> out;
    for (const auto& p : points) { out.push_back(p[0]); out.push_back(p[1]); }
    return out;
}

// _convex_hull(points): punkty (x, y) bez duplikatów, łamana Andrew
std::vector<Point2> convex_hull(const std::vector<Point3>& source) {
    std::set<std::pair<double, double>> unique;
    for (const auto& p : source) unique.insert({p[0], p[1]});
    std::vector<Point2> points;
    for (const auto& p : unique) points.push_back({p.first, p.second});
    if (points.size() < 3) return points;
    auto cross = [](const Point2& a, const Point2& b, const Point2& c) {
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    };
    auto half = [&](const std::vector<Point2>& ordered) {
        std::vector<Point2> out;
        for (const auto& point : ordered) {
            while (out.size() > 1 && cross(out[out.size() - 2], out.back(), point) <= 0) out.pop_back();
            out.push_back(point);
        }
        return out;
    };
    std::vector<Point2> lower = half(points);
    std::vector<Point2> reversed(points.rbegin(), points.rend());
    std::vector<Point2> upper = half(reversed);
    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

viewport_render::OrderedGroups group_points(const Project& project, bool include_fittings, viewport_render::Context* context) {
    if (include_fittings) return context->groups;
    viewport_render::OrderedGroups groups;
    for (const auto& p : project.parts) {
        if (!p.visible) continue;
        auto& group = groups[p.group];
        for (const auto& [u, v] : p.points())
            for (double t : {0.0, p.t}) group.push_back(core::world(p, u, v, t));
    }
    return groups;
}

// str.casefold() dla UTF-8: litery łacińskie, w tym polskie znaki diakrytyczne.
std::string casefold(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); ++i; continue; }
        size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        char32_t cp = length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k < length && i + k < text.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += length;
        if (cp == 0xDF) { out += "ss"; continue; }
        if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) cp += 32;
        else if (cp >= 0x100 && cp <= 0x137) { if (cp % 2 == 0) cp += 1; }
        else if (cp >= 0x139 && cp <= 0x148) { if (cp % 2 == 1) cp += 1; }
        else if (cp >= 0x14A && cp <= 0x177) { if (cp % 2 == 0) cp += 1; }
        else if (cp >= 0x179 && cp <= 0x17E) { if (cp % 2 == 1) cp += 1; }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}

bool wood(const core::Material& material) {
    if (material.kind == "drewno" || material.kind == "sklejka") return true;
    std::string name = casefold(material.name);
    for (const char* word : {"dąb", "oak", "orzech", "walnut", "jesion", "ash", "buk", "beech", "sosna", "pine", "brzoza", "birch",
                             "wenge", "mahoń", "teak", "bambus"})
        if (name.find(word) != std::string::npos) return true;
    return false;
}

std::vector<Point2> points_of(const json& face) {
    std::vector<Point2> out;
    for (const auto& p : face.at("points")) out.push_back({num(p.at(0)), num(p.at(1))});
    return out;
}

// canvas._viewport_photo: zdjęcie musi żyć tak długo jak element obrazu na płótnie
std::map<tk::Canvas*, std::unique_ptr<tk::PhotoImage>>& photos() {
    static std::map<tk::Canvas*, std::unique_ptr<tk::PhotoImage>> state;
    return state;
}
std::set<tk::Canvas*>& destroy_bound() { static std::set<tk::Canvas*> state; return state; }

double distance(const Point2& a, const Point2& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

std::vector<Point2> with_first(std::vector<Point2> points) {
    if (!points.empty()) points.push_back(points.front());
    return points;
}

struct Solid {
    const json* face;
    std::vector<double> depths;
};

json paint_depth_faces(tk::Canvas* canvas, const json& faces, const Project& project, const std::set<std::string>& selected_ids_in,
                       bool use_wood, const std::optional<core::Camera>& camera, std::pair<double, double> size, bool xray,
                       double explode, bool interactive, const std::optional<std::set<std::string>>& xray_ids_in,
                       const json& stats) {
    std::map<std::string, const Part*> owners;
    for (const auto& p : project.parts) owners[p.id] = &p;
    std::map<std::string, const core::Material*> materials;
    for (const auto& m : project.materials) materials[m.id] = &m;
    std::set<std::string> ghost_ids;
    if (view_style::wire()) {
        for (const auto& p : project.parts) ghost_ids.insert(p.id);  // szkielet: wszystkie płyty jako przezroczyste, same krawędzie
    } else if (xray) {
        if (xray_ids_in) ghost_ids = *xray_ids_in;
        else
            for (const auto& p : project.parts) {
                std::string role = assembly::inferred_role(p);
                if (role == "door" || role == "drawer") ghost_ids.insert(p.id);
            }
    }
    const std::set<std::string>& selected_ids = selected_ids_in;
    double scale = interactive ? (faces.size() > 400 ? .35 : .6) : 1;
    std::set<std::string> face_ids;
    for (const auto& f : faces) face_ids.insert(f.at("id").get<std::string>());
    depth_raster::DepthRaster raster(size, std::vector<std::string>(face_ids.begin(), face_ids.end()), scale);
    std::vector<Solid> solid;
    for (auto it = faces.rbegin(); it != faces.rend(); ++it) {
        const json& face = *it;
        const std::string id = face.at("id").get<std::string>();
        if (ghost_ids.contains(id) && !flag(face, "fitting")) continue;
        std::vector<Point2> points = points_of(face);
        std::vector<double> depths;
        json given = py::get(face, "depths");
        if (given.is_null()) depths.assign(points.size(), num(py::get(face, "depth", 0)));
        else for (const auto& d : given) depths.push_back(num(d));
        raster.polygon(points, depths, face.at("color").get<std::string>(), id);
        solid.push_back({&face, depths});
    }
    // Draw edges only where the shared z-buffer says their surface is visible.
    if (interactive) {
        // Hundreds of NumPy line allocations cost more than the low-resolution
        // fill itself. Visible ownership gives crisp motion silhouettes in one
        // array pass, and cannot reveal an edge through an occluding board.
        std::set<std::string> outlined = selected_ids;
        for (const auto& f : faces) if (flag(f, "selected")) outlined.insert(f.at("id").get<std::string>());
        raster.outline_visible(std::vector<std::string>(outlined.begin(), outlined.end()));
    } else {
        for (const auto& item : solid) {
            const json& face = *item.face;
            if (flag(face, "mesh") || flag(face, "fitting")) continue;
            const std::string id = face.at("id").get<std::string>();
            bool selected = flag(face, "selected") || selected_ids.contains(id);
            if (view_style::sketch() && !selected) continue;  // obrys robi filtr ołówka
            std::string border = selected ? SELECTION : mix_color(face.at("color").get<std::string>(), "#514b43", .3);
            std::vector<double> depths = item.depths;
            if (!depths.empty()) depths.push_back(depths.front());
            raster.line(with_first(points_of(face)), depths, border, id, selected ? 2 : 1);
        }
    }
    int grain = 0;
    // Linie słojów: w trybie szkicu rysowane po filtrze ołówka jako jasne kreski grafitu (przed filtrem dawałyby szum kreskowania).
    auto draw_grain = [&]() {
        struct Candidate { double area; const Solid* item; };
        std::vector<Candidate> candidates;
        for (const auto& item : solid) {
            const json& face = *item.face;
            auto part = owners.find(face.at("id").get<std::string>());
            const core::Material* material = nullptr;
            if (part != owners.end()) {
                auto found = materials.find(part->second->material);
                if (found != materials.end()) material = found->second;
            }
            if (!flag(face, "mesh") && !flag(face, "fitting") && material && wood(*material)) {
                double area = polygon_area(points_of(face));
                if (area > 1500) candidates.push_back({area, &item});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.area > b.area; });
        for (size_t c = 0; c < candidates.size() && c < 24; ++c) {
            const json& face = *candidates[c].item->face;
            const std::vector<double>& depths = candidates[c].item->depths;
            const Part& part = *owners.at(face.at("id").get<std::string>());
            std::vector<Point2> points = points_of(face);
            Point2 direction;
            if (camera) {
                Point3 a = camera->project(core::world(part, 0, 0, 0), size.first, size.second);
                Point3 b = camera->project(core::world(part, part.grain && !part.grain_v ? part.u : 0, part.grain && !part.grain_v ? 0 : part.v, 0), size.first, size.second);
                direction = {b[0] - a[0], b[1] - a[1]};
            } else {
                // max(zip(points, points[1:] + points[:1]), key=dist): pierwszy najdłuższy bok
                size_t best = 0;
                double best_length = -1;
                for (size_t k = 0; k < points.size(); ++k) {
                    double length = distance(points[k], points[(k + 1) % points.size()]);
                    if (length > best_length) { best_length = length; best = k; }
                }
                const Point2 &a = points[best], &b = points[(best + 1) % points.size()];
                direction = {b[0] - a[0], b[1] - a[1]};
            }
            auto triangles = depth_raster::triangulate(points);
            if (triangles.empty()) continue;
            auto [i, j, k] = triangles[0];
            const Point2 &a2 = points[(size_t)i], &b2 = points[(size_t)j], &c2 = points[(size_t)k];
            double area = (b2[0] - a2[0]) * (c2[1] - a2[1]) - (b2[1] - a2[1]) * (c2[0] - a2[0]);
            if (std::fabs(area) < 1e-8) continue;
            auto z_at = [&](const Point2& point) {
                double x = point[0] - a2[0], y = point[1] - a2[1];
                double u = ((c2[1] - a2[1]) * x - (c2[0] - a2[0]) * y) / area;
                double v = (-(b2[1] - a2[1]) * x + (b2[0] - a2[0]) * y) / area;
                return depths[(size_t)i] + u * (depths[(size_t)j] - depths[(size_t)i]) + v * (depths[(size_t)k] - depths[(size_t)i]);
            };
            const std::string id = face.at("id").get<std::string>();
            for (const auto& [start, end] : grain_segments(points, direction, wood_grain::line_count(id), id)) {
                const std::string line_color = view_style::sketch() ? mix_color(face.at("color").get<std::string>(), "#4a4640", .12 + 1.6 * wood_grain::line_mix(id))
                                                                    : mix_color(face.at("color").get<std::string>(), "#5b442f", wood_grain::line_mix(id));
                raster.line({start, end}, {z_at(start), z_at(end)}, line_color, id);
                ++grain;
                if (grain >= 220) break;
            }
            if (grain >= 220) break;
        }
    };
    if (use_wood && !interactive && !view_style::sketch()) draw_grain();
    if (!ghost_ids.empty() && camera) {
        std::set<std::string> face_selected, present;
        for (const auto& f : faces) {
            present.insert(f.at("id").get<std::string>());
            if (flag(f, "selected")) face_selected.insert(f.at("id").get<std::string>());
        }
        for (const auto& owner : ghost_ids) {
            if (!present.contains(owner)) continue;
            const Part& part = *owners.at(owner);
            Point3 shift = core::explosion_shift(part, *camera, explode);
            std::vector<std::pair<std::vector<Point2>, std::vector<double>>> layers;
            for (double thickness : {0.0, part.t}) {
                std::vector<Point2> outline;
                std::vector<double> depths;
                for (const auto& [u, v] : part.points()) {
                    Point3 point = core::world(part, u, v, thickness);
                    Point3 q = camera->project({point[0] + shift[0], point[1] + shift[1], point[2] + shift[2]}, size.first, size.second);
                    outline.push_back({q[0], q[1]});
                    depths.push_back(q[2]);
                }
                layers.push_back({outline, depths});
            }
            bool selected = selected_ids.contains(owner) || face_selected.contains(owner);
            std::string color = selected ? "#259ec4" : view_style::wire() ? "#4d6a86" : "#819bb3";
            for (const auto& [outline, depths] : layers) {
                std::vector<double> closed = depths;
                if (!closed.empty()) closed.push_back(closed.front());
                raster.line(with_first(outline), closed, color, owner, selected ? 2 : 1, true);
            }
            for (size_t i = 0; i < layers[0].first.size(); ++i)
                raster.line({layers[0].first[i], layers[1].first[i]}, {layers[0].second[i], layers[1].second[i]}, color, owner, 1, true);
        }
    }
    json pencil = json::object();
    if (view_style::sketch()) {
        // Szkic ołówkiem: filtr obrazu liczony z tych samych buforów (elementy + głębia), co wskazywanie myszą.
        pencil_filter::Settings pencil_settings;
        pencil_settings.hatching = !interactive;
        pencil_settings.spacing = std::max(3.0, 6.0 * scale);
        pencil_settings.wobble = std::max(0.4, scale);
        pencil_filter::Stats pencil_stats = pencil_filter::apply(raster.width, raster.height, raster.rgba, raster.owner.data, raster.depth.data, pencil_settings);
        pencil = json::object({{"covered", pencil_stats.covered}, {"edge_pixels", pencil_stats.edge_pixels}, {"hatch_pixels", pencil_stats.hatch_pixels},
                               {"threads", pencil_stats.threads_used}, {"pattern_cached", pencil_stats.pattern_cached}, {"ms", pencil_stats.milliseconds}});
        if (use_wood && !interactive) draw_grain();
    }
    raster.attach(canvas);
    if (auto* state = dynamic_cast<depth_raster::CanvasState*>(canvas)) {
        state->_render_diagnostics = stats.is_object() ? stats : json::object();
        if (!pencil.empty()) state->_render_diagnostics["pencil"] = pencil;
    }
    depth_raster::Image image = raster.image();
    photos()[canvas] = std::make_unique<tk::PhotoImage>(canvas, image.width, image.height, image.rgba);
    if (!destroy_bound().contains(canvas)) {
        destroy_bound().insert(canvas);
        canvas->bind("<Destroy>", [canvas](tk::Event& event) {
            if (event.widget == canvas) { photos().erase(canvas); destroy_bound().erase(canvas); }
            return std::string();
        }, true);
    }
    canvas->create_image(0, 0, {{"anchor", "nw"}, {"image", *photos()[canvas]}, {"tags", "viewport.raster_model"}});
    return json::object({{"items", 1}, {"grain", grain}, {"raster", true}, {"depth", true}, {"triangles", raster.triangles}});
}

}  // namespace

std::string mix_color(const std::string& first, const std::string& second, double amount) {
    amount = std::max(0.0, std::min(1.0, amount));
    auto channel = [](const std::string& color, size_t start) { return (int)py::int16(start >= color.size() ? std::string() : color.substr(start, 2)); };
    std::string out = "#";
    for (size_t start : {1, 3, 5}) {
        int x = channel(first, start), y = channel(second, start);
        out += std::format("{:02x}", (int)py::round_half_even(x + (y - x) * amount));
    }
    return out;
}

std::vector<Point3> scene_vertices(const Project& project, bool include_fittings, bool include_room, viewport_render::Context* context) {
    std::shared_ptr<viewport_render::Context> own;
    if (context == nullptr) { own = viewport_render::prepare_scene(project); context = own.get(); }
    std::vector<Point3> points = context->stock;
    if (include_fittings)
        for (const auto& record : context->fittings) points.insert(points.end(), record->vertices.begin(), record->vertices.end());
    if (include_room) points.insert(points.end(), context->room_vertices.begin(), context->room_vertices.end());
    return points;
}

std::optional<std::pair<Point3, Point3>> scene_bounds(const Project& project, bool include_fittings) {
    auto points = scene_vertices(project, include_fittings);
    if (points.empty()) return std::nullopt;
    Point3 lo = points[0], hi = points[0];
    for (const auto& p : points)
        for (size_t i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    return std::make_pair(lo, hi);
}

void fit_scene(core::Camera& camera, const Project& project, std::pair<double, double> size, double padding, bool include_fittings) {
    auto [width, height] = size;
    auto points = scene_vertices(project, include_fittings);
    if (points.empty()) {
        camera.center = {0, 0, 0};
        camera.scale = .4;
        camera.pan = {0, 0};
        return;
    }
    Point3 lo = points[0], hi = points[0];
    for (const auto& p : points)
        for (size_t i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    camera.center = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    double min_u = 0, max_u = 0, min_v = 0, max_v = 0;
    bool first = true;
    for (const auto& p : points) {
        Point3 viewed = camera.view(p);
        if (first) { min_u = max_u = viewed[0]; min_v = max_v = viewed[1]; first = false; }
        else { min_u = std::min(min_u, viewed[0]); max_u = std::max(max_u, viewed[0]); min_v = std::min(min_v, viewed[1]); max_v = std::max(max_v, viewed[1]); }
    }
    double extent_u = max_u - min_u, extent_v = max_v - min_v;
    // The centre of an XYZ bounding box need not centre a projected L/U set.
    // Shift in the camera's orthonormal screen basis, preserving its depth.
    double yaw = camera.yaw * (std::numbers::pi / 180), pitch = camera.pitch * (std::numbers::pi / 180);
    Point3 screen_u{std::cos(yaw), -std::sin(yaw), 0};
    Point3 screen_v{std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch)};
    double middle_u = (min_u + max_u) / 2, middle_v = (min_v + max_v) / 2;
    Point3 center = camera.center;
    for (size_t i = 0; i < 3; ++i) center[i] = camera.center[i] + middle_u * screen_u[i] + middle_v * screen_v[i];
    camera.center = center;
    camera.scale = std::min(std::max(width - padding, 50.0) / std::max(extent_u, 100.0), std::max(height - padding, 50.0) / std::max(extent_v, 100.0));
    camera.pan = {0, 0};
}

double grid_spacing(double scale) {
    double target = 44 / std::max(std::fabs(scale), .0001);
    double power = std::pow(10.0, std::floor(std::log10(target)));
    for (double multiplier : {1.0, 2.0, 5.0, 10.0})
        if (multiplier * power >= target) return multiplier * power;
    throw py::StopIteration();
}

json paint_background(tk::Canvas* canvas, const core::Camera& camera, core::ProjectPtr project_ptr, std::pair<double, double> size,
                      const BackgroundOptions& options) {
    const Project& project = *project_ptr;
    auto [width, height] = size;
    if (width < 10 || height < 10) return json::object({{"items", 0}, {"spacing", 0}, {"floor", 0}});
    std::shared_ptr<viewport_render::Context> own;
    viewport_render::Context* context = options.context;
    if (context == nullptr) { own = viewport_render::prepare_scene(project); context = own.get(); }
    double brightness = num(py::get(project.scene, "brightness", 1));
    camera._scene_brightness = brightness;
    std::string sky_top, sky_bottom, floor, grid_color;
    std::array<std::string, 2> shadow_colors;
    if (brightness < .65) {
        sky_top = "#242c36"; sky_bottom = "#45474b"; floor = "#393a3b"; grid_color = "#515150";
        shadow_colors = {"#2f3031", "#27282a"};
    } else if (brightness > 1.1) {
        sky_top = "#f3f7fa"; sky_bottom = "#fffdf8"; floor = "#f1ede5"; grid_color = "#dfdcd5";
        shadow_colors = {"#e3ded3", "#d6cdbd"};
    } else {
        sky_top = SKY_TOP; sky_bottom = SKY_BOTTOM; floor = FLOOR; grid_color = GRID;
        shadow_colors = {"#dcd6ca", "#cfc6b7"};
    }
    if (view_style::realistic() && brightness >= .65) {
        // studio: chłodniejsze światło z góry i ciepły horyzont, jasna ciepła podłoga — kolory mebli nie toną w szarozielonym tle
        sky_top = brightness > 1.1 ? "#e6edf3" : "#dde4ea"; sky_bottom = "#f6f1e7"; floor = "#e9e2d4"; grid_color = "#d5cab8";
        shadow_colors = {"#d9d0c0", "#cbbfac"};
    }
    if (view_style::sketch()) {  // papier: jednolite, ciepłe tło i podłoga, siatka jak miękki ołówek
        sky_top = sky_bottom = render_style::paper_color(); floor = "#efeadc"; grid_color = "#cfc9ba";
        shadow_colors = {"#e4dfd2", "#d3ccbb"};
    } else if (const auto& palette = view_style::palette(); palette.sky_top != nullptr) {  // tło wybrane w panelu "Silnik i jakość 3D"
        sky_top = palette.sky_top; sky_bottom = palette.sky_bottom; floor = palette.floor; grid_color = palette.grid;
    }
    int count = 0;
    int bands = options.interactive ? 10 : 20;
    for (int i = 0; i < bands; ++i) {
        double y0 = height * i / bands, y1 = height * (i + 1) / bands + 1;
        canvas->create_rectangle(0, y0, width, y1, {{"outline", ""}, {"fill", mix_color(sky_top, sky_bottom, (double)i / (bands - 1))}, {"tags", "viewport.background"}});
        ++count;
    }
    viewport_render::OrderedGroups groups = group_points(project, options.include_fittings, context);
    double floor_z = 0;
    for (const auto& group : groups) for (const auto& p : group.second) floor_z = std::min(floor_z, p[2]);
    double scale = std::max(std::fabs(camera.scale), .0001);
    double extent = std::max(width, height) / scale * 1.6;
    double center_x = camera.center[0], center_y = camera.center[1];
    // Include pan in the floor extent, so panning does not reveal its boundary.
    extent += std::hypot(camera.pan[0], camera.pan[1]) / scale;
    std::vector<Point3> floor_rect = {{center_x - extent, center_y - extent, floor_z}, {center_x + extent, center_y - extent, floor_z},
                                      {center_x + extent, center_y + extent, floor_z}, {center_x - extent, center_y + extent, floor_z}};
    std::vector<Point3> projected;
    for (const auto& p : floor_rect) projected.push_back(camera.project(p, width, height));
    canvas->create_polygon(flat(projected), {{"fill", floor}, {"outline", ""}, {"tags", "viewport.floor"}});
    ++count;
    double step = grid_spacing(scale);
    if (options.grid && std::fabs(std::sin(camera.pitch * (std::numbers::pi / 180))) > .035) {
        // Up to 31 lines per axis, even with unusual camera scaling or pan.
        int steps = std::min(15, std::max(4, (int)std::ceil(std::max(width, height) / scale / step)));
        double origin_x = std::floor(center_x / step) * step, origin_y = std::floor(center_y / step) * step;
        for (int i = -steps; i <= steps; ++i) {
            double x = origin_x + i * step, y = origin_y + i * step;
            std::array<std::pair<Point3, Point3>, 2> lines = {
                std::make_pair(Point3{x, origin_y - steps * step, floor_z}, Point3{x, origin_y + steps * step, floor_z}),
                std::make_pair(Point3{origin_x - steps * step, y, floor_z}, Point3{origin_x + steps * step, y, floor_z})};
            for (const auto& [a, b] : lines) {
                Point3 pa = camera.project(a, width, height), pb = camera.project(b, width, height);
                canvas->create_line(std::vector<double>{pa[0], pa[1], pb[0], pb[1]}, {{"fill", grid_color}, {"width", 1}, {"tags", "viewport.grid"}});
                ++count;
            }
        }
    }
    // Tryb realistyczny: płytki podłogi (fugi) rysowane zawsze, siatka techniczna nadal tylko na życzenie.
    if (view_style::realistic() && std::fabs(std::sin(camera.pitch * (std::numbers::pi / 180))) > .035) {
        std::string tile_color = mix_color(floor, brightness < .65 ? "#ffffff" : "#8d8272", brightness < .65 ? .06 : .13);
        double tile_extent = std::max(width, height) / scale * .9;
        for (const auto& line : render_style::floor_tile_lines(center_x, center_y, tile_extent, 600, floor_z, options.interactive ? 10 : 16)) {
            Point3 pa = camera.project({line.a[0], line.a[1], line.a[2] + .02}, width, height), pb = camera.project({line.b[0], line.b[1], line.b[2] + .02}, width, height);
            canvas->create_line(std::vector<double>{pa[0], pa[1], pb[0], pb[1]}, {{"fill", tile_color}, {"width", 1}, {"tags", "viewport.tiles"}});
            ++count;
        }
    }
    if (options.shadows && view_style::state.shadows && view_style::realistic()) {
        // Miękki cień rzucany przez cały mebel (rzut wzdłuż światła głównego + półcień), a nie tylko cień kontaktowy.
        auto& cache = context->cast_hulls;
        if (!cache.contains(options.include_fittings)) {
            auto& entry = cache[options.include_fittings];
            for (const auto& group : groups) {
                if (group.second.empty()) continue;
                double low = group.second[0][2], high = low;
                std::vector<render_style::Point2> footprint;
                for (const auto& p : group.second) { low = std::min(low, p[2]); high = std::max(high, p[2]); footprint.push_back({p[0], p[1]}); }
                entry.push_back({low, high, render_style::convex_hull(footprint)});
            }
        }
        render_style::ShadowSettings settings;
        settings.layers = options.interactive ? 3 : 10;  // więcej warstw półcienia = gładszy, bardziej naturalny cień (bez widocznych stopni)
        if (brightness < .65) settings.max_strength = .42;
        const std::string shadow_color = brightness < .65 ? "#101114" : "#4a3f33";  // ciepły, nie szary: cień na jasnej podłodze ma odcień podłogi
        std::vector<std::vector<render_style::ShadowLayer>> all;
        size_t counted = 0;
        for (const auto& item : cache[options.include_fittings]) {
            if (counted++ >= 40) break;
            if (item.hull.size() < 3) continue;
            all.push_back(render_style::cast_shadow(item.hull, std::max(0.0, item.bottom - floor_z), std::max(0.0, item.top - floor_z), settings));
        }
        // warstwy zewnętrzne wszystkich mebli najpierw, potem wewnętrzne — nakładające się cienie nie ciemnieją podwójnie
        for (int layer = 0; layer < settings.layers; ++layer)
            for (const auto& shadow : all) {
                if (layer >= (int)shadow.size()) continue;
                std::vector<Point3> screen;
                for (const auto& p : shadow[(size_t)layer].polygon) screen.push_back(camera.project({p[0], p[1], floor_z + .03}, width, height));
                canvas->create_polygon(flat(screen), {{"fill", mix_color(floor, shadow_color, shadow[(size_t)layer].strength)}, {"outline", ""}, {"tags", "viewport.shadow"}});
                ++count;
            }
    } else if (options.shadows && view_style::state.shadows) {
        // Contact shadows should not suggest a hanging cabinet touches the floor.
        auto& cache = context->shadow_hulls;
        if (!cache.contains(options.include_fittings)) {
            auto& entry = cache[options.include_fittings];
            for (const auto& group : groups) {
                if (group.second.empty()) throw py::ValueError("min() iterable argument is empty");
                double minimum = group.second[0][2];
                for (const auto& p : group.second) minimum = std::min(minimum, p[2]);
                entry.push_back({minimum, convex_hull(group.second)});
            }
        }
        std::vector<const std::vector<Point2>*> eligible;
        for (const auto& [minimum, hull] : cache[options.include_fittings]) if (minimum - floor_z <= 160) eligible.push_back(&hull);
        for (size_t h = 0; h < eligible.size() && h < 40; ++h) {
            const auto& hull = *eligible[h];
            if (hull.size() < 3) continue;
            std::vector<double> xs, ys;
            for (const auto& p : hull) { xs.push_back(p[0]); ys.push_back(p[1]); }
            double cx = py::sum(xs) / (double)hull.size(), cy = py::sum(ys) / (double)hull.size();
            struct Shade { double expansion, offset; const std::string* color; };
            for (const Shade& shade : {Shade{1.045, 10, &shadow_colors[0]}, Shade{1.01, 4, &shadow_colors[1]}}) {
                std::vector<Point3> shadow;
                for (const auto& p : hull)
                    shadow.push_back({cx + (p[0] - cx) * shade.expansion + shade.offset, cy + (p[1] - cy) * shade.expansion + shade.offset, floor_z + .05});
                std::vector<Point3> screen;
                for (const auto& p : shadow) screen.push_back(camera.project(p, width, height));
                canvas->create_polygon(flat(screen), {{"fill", *shade.color}, {"outline", ""}, {"tags", "viewport.shadow"}});
                ++count;
            }
        }
    }
    // Pools are a lightweight floor approximation of LED spill. Direction and
    // height decide whether a strip can illuminate the floor beneath it.
    if (!options.interactive && view_style::state.lighting) {
        size_t limit = std::min<size_t>(32, context->lights.size());
        for (size_t l = 0; l < limit; ++l) {
            const json& light = context->lights[l];
            const json& position = light.at("position");
            double height_above = num(position.at(2)) - floor_z;
            double light_radius = num(py::get(light, "radius", 650));
            if (height_above < 0 || height_above > light_radius) continue;
            json direction = light.contains("direction") ? light["direction"] : json::array({0, 0, -1});
            if (num(direction.at(2)) > -.15) continue;
            double radius = std::min(300.0, std::max(70.0, light_radius * .3));
            double power = brightness >= .65 ? .14 : .36;
            for (double fraction : {1.0, .64}) {
                std::vector<Point3> pool;
                for (int i = 0; i < 16; ++i)
                    pool.push_back({num(position.at(0)) + std::cos(i * (2 * std::numbers::pi) / 16) * radius * fraction,
                                    num(position.at(1)) + std::sin(i * (2 * std::numbers::pi) / 16) * radius * fraction, floor_z + .1});
                std::vector<Point3> screen;
                for (const auto& p : pool) screen.push_back(camera.project(p, width, height));
                canvas->create_polygon(flat(screen), {{"fill", mix_color(floor, py::get(light, "color", "#fff3bf").get<std::string>(), power / fraction)},
                                                      {"outline", ""}, {"tags", "viewport.light_pool"}});
                ++count;
            }
        }
    }
    return json::object({{"items", count}, {"spacing", step}, {"floor", floor_z}});
}

double polygon_area(const std::vector<Point2>& points) {
    std::vector<double> terms;
    for (size_t i = 0; i < points.size(); ++i) {
        const Point2 &a = points[i], &b = points[(i + 1) % points.size()];
        terms.push_back(a[0] * b[1] - b[0] * a[1]);
    }
    return std::fabs(py::sum(terms)) / 2;
}

std::vector<std::pair<Point2, Point2>> grain_segments(const std::vector<Point2>& points, const Point2& direction, int count, const std::string& seed) {
    double length = std::hypot(direction[0], direction[1]);
    if (points.size() < 3 || length < .001) return {};
    double dx = direction[0] / length, dy = direction[1] / length;
    double nx = -dy, ny = dx;
    double low = points[0][0] * nx + points[0][1] * ny, high = low;
    for (const auto& p : points) {
        double value = p[0] * nx + p[1] * ny;
        low = std::min(low, value);
        high = std::max(high, value);
    }
    if (high - low < 12) return {};
    std::vector<std::pair<Point2, Point2>> out;
    std::vector<double> random_fractions;
    if (!seed.empty()) random_fractions = wood_grain::line_fractions(seed, count);
    for (int i = 0; i < count; ++i) {
        // Non-uniform, deterministic spacing avoids a ruler-like striped fill; z ziarnem każda płyta ma własny rozkład.
        double fraction = seed.empty() ? (i + 1 + .12 * std::sin(i * 2.1)) / (count + 1) : random_fractions[(size_t)i];
        double level = low + (high - low) * fraction;
        std::vector<Point2> hits;
        for (size_t k = 0; k < points.size(); ++k) {
            const Point2 &a = points[k], &b = points[(k + 1) % points.size()];
            double ta = a[0] * nx + a[1] * ny, tb = b[0] * nx + b[1] * ny;
            if ((ta <= level && level < tb) || (tb <= level && level < ta)) {
                double t = (level - ta) / (tb - ta);
                hits.push_back({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t});
            }
        }
        std::stable_sort(hits.begin(), hits.end(), [&](const Point2& p, const Point2& q) { return p[0] * dx + p[1] * dy < q[0] * dx + q[1] * dy; });
        for (size_t k = 0; k + 1 < hits.size(); k += 2)
            if (distance(hits[k], hits[k + 1]) > 10) out.push_back({hits[k], hits[k + 1]});
    }
    return out;
}

json paint_faces(tk::Canvas* canvas, const core::Drawn& faces, core::ProjectPtr project_ptr, const PaintFacesOptions& options) {
    const Project& project = *project_ptr;
    if (!options.vector_items) {
        std::pair<double, double> size = options.size;
        if (size.first < 0) size = {(double)canvas->winfo_width(), (double)canvas->winfo_height()};
        return paint_depth_faces(canvas, faces, project, options.selected_ids, options.wood, options.camera, size, options.xray,
                                 options.explode, options.interactive, options.xray_ids, options.stats ? *options.stats : json::object());
    }
    const bool have_size = options.size.first >= 0;
    std::map<std::string, const Part*> owners;
    for (const auto& p : project.parts) owners[p.id] = &p;
    std::map<std::string, const core::Material*> materials;
    for (const auto& m : project.materials) materials[m.id] = &m;
    std::set<std::string> wood_ids;
    for (const auto& m : project.materials) if (wood(m)) wood_ids.insert(m.id);
    const std::set<std::string>& selected_ids = options.selected_ids;
    std::set<std::string> ghost_ids;
    if (view_style::wire()) {
        for (const auto& p : project.parts) ghost_ids.insert(p.id);
    } else if (options.xray) {
        if (options.xray_ids) ghost_ids = *options.xray_ids;
        else
            for (const auto& p : project.parts) {
                std::string role = assembly::inferred_role(p);
                if (role == "door" || role == "drawer") ghost_ids.insert(p.id);
            }
    }
    auto material_of = [&](const std::string& id) -> const core::Material* {
        auto part = owners.find(id);
        if (part == owners.end()) return nullptr;
        auto found = materials.find(part->second->material);
        return found == materials.end() ? nullptr : found->second;
    };
    std::vector<std::pair<double, size_t>> candidates;
    if (options.wood && !options.interactive) {
        for (size_t index = 0; index < faces.size(); ++index) {
            const json& face = faces[index];
            const std::string id = face.at("id").get<std::string>();
            const core::Material* material = material_of(id);
            if (!flag(face, "mesh") && !flag(face, "fitting") && !ghost_ids.contains(id) && material && wood_ids.contains(material->id)) {
                double area = polygon_area(points_of(face));
                if (area > 1500) candidates.push_back({area, index});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<>());
    std::set<size_t> textured;
    for (size_t k = 0; k < candidates.size() && k < 24; ++k) textured.insert(candidates[k].second);
    int items = 0, grain_items = 0;
    for (size_t index = 0; index < faces.size(); ++index) {
        const json& face = faces[index];
        std::vector<Point2> points = points_of(face);
        const std::string id = face.at("id").get<std::string>();
        auto part_found = owners.find(id);
        const Part* part = part_found == owners.end() ? nullptr : part_found->second;
        const core::Material* material = material_of(id);
        std::string color = face.at("color").get<std::string>();
        if (material && !flag(face, "fitting") && !face.contains("normal")) color = mix_color(color, material->color, .18);
        bool selected = flag(face, "selected") || selected_ids.contains(id);
        std::string border = selected ? SELECTION : mix_color(color, "#514b43", .36);
        bool ghost = ghost_ids.contains(id) && !flag(face, "fitting");
        if (ghost) {
            // Preserve all faces in the caller's picking list, but avoid
            // thousands of invisible triangle items. A semantic front contour
            // below provides the readable ghost over the interior.
            if (flag(face, "mesh")) continue;
            canvas->create_polygon(flat2(points), {{"fill", ""}, {"outline", selected ? "#5290ad" : "#91afbd"}, {"width", selected ? 2 : 1},
                                                   {"tags", std::vector<std::string>{"model", id, "viewport.ghost"}}});
        } else {
            canvas->create_polygon(flat2(points), {{"fill", color}, {"outline", flag(face, "mesh") ? color : border},
                                                   {"width", flag(face, "mesh") ? 0 : selected ? 2 : 1},
                                                   {"tags", std::vector<std::string>{"model", id}}});
        }
        ++items;
        if (!textured.contains(index)) continue;
        Point2 direction;
        if (options.camera && have_size) {
            Point3 a = options.camera->project(core::world(*part, 0, 0, 0), options.size.first, options.size.second);
            Point3 b = options.camera->project(core::world(*part, part->grain && !part->grain_v ? part->u : 0, part->grain && !part->grain_v ? 0 : part->v, 0),
                                               options.size.first, options.size.second);
            direction = {b[0] - a[0], b[1] - a[1]};
        } else {
            size_t best = 0;
            double best_length = -1;
            for (size_t k = 0; k < points.size(); ++k) {
                double length = distance(points[k], points[(k + 1) % points.size()]);
                if (length > best_length) { best_length = length; best = k; }
            }
            const Point2 &a = points[best], &b = points[(best + 1) % points.size()];
            direction = {b[0] - a[0], b[1] - a[1]};
        }
        for (const auto& [a, b] : grain_segments(points, direction, wood_grain::line_count(id), id)) {
            if (grain_items >= 220) break;
            canvas->create_line(std::vector<double>{a[0], a[1], b[0], b[1]},
                                {{"fill", mix_color(color, "#5b442f", wood_grain::line_mix(id))}, {"width", 1}, {"tags", std::vector<std::string>{"viewport.grain", id}}});
            ++grain_items;
            ++items;
        }
    }
    if (!ghost_ids.empty() && options.camera && have_size) {
        std::set<std::string> mesh_owners, face_selected;
        for (const auto& f : faces) {
            const std::string id = f.at("id").get<std::string>();
            if (ghost_ids.contains(id)) mesh_owners.insert(id);
            if (flag(f, "selected")) face_selected.insert(id);
        }
        for (const auto& part_id : mesh_owners) {
            const Part& part = *owners.at(part_id);
            Point3 shift = core::explosion_shift(part, *options.camera, options.explode);
            std::vector<Point2> outline;
            for (const auto& [u, v] : part.points()) {
                Point3 point = core::world(part, u, v, 0);
                Point3 q = options.camera->project({point[0] + shift[0], point[1] + shift[1], point[2] + shift[2]}, options.size.first, options.size.second);
                outline.push_back({q[0], q[1]});
            }
            if (outline.size() >= 3) {
                bool selected = selected_ids.contains(part_id) || face_selected.contains(part_id);
                canvas->create_line(flat2(with_first(outline)), {{"fill", selected ? "#5290ad" : "#91afbd"}, {"width", selected ? 2 : 1},
                                                                 {"tags", std::vector<std::string>{"viewport.ghost_outline", part_id}}});
                ++items;
            }
        }
    }
    return json::object({{"items", items}, {"grain", grain_items}, {"raster", false}});
}

void paint_overlay(tk::Canvas* canvas, const core::Camera& camera, std::pair<double, double> size, const json& spacing, bool axes) {
    auto [width, height] = size;
    if (width < 100 || height < 100) return;
    if (axes) {
        std::array<double, 2> origin{49, height - 46};
        Point3 center = camera.view(camera.center);
        struct Axis { const char* label; Point3 vector; const char* color; };
        for (const Axis& axis : {Axis{"X", {1, 0, 0}, "#bd6559"}, Axis{"Y", {0, 1, 0}, "#4a9275"}, Axis{"Z", {0, 0, 1}, "#527cae"}}) {
            Point3 p = camera.view({camera.center[0] + axis.vector[0], camera.center[1] + axis.vector[1], camera.center[2] + axis.vector[2]});
            double dx = (p[0] - center[0]) * 29, dy = -(p[1] - center[1]) * 29;
            if (std::hypot(dx, dy) < 3) continue;
            std::array<double, 2> end{origin[0] + dx, origin[1] + dy};
            canvas->create_line(std::vector<double>{origin[0], origin[1], end[0], end[1]},
                                {{"fill", std::string(axis.color)}, {"width", 2}, {"arrow", "last"}, {"tags", "viewport.axes"}});
            canvas->create_text(end[0] + dx * .2, end[1] + dy * .2,
                                {{"text", std::string(axis.label)}, {"fill", std::string(axis.color)}, {"font", "{Segoe UI} 8 bold"}, {"tags", "viewport.axes"}});
        }
    }
    if (py::truthy(spacing)) {
        canvas->create_text(width - 15, height - 16,
                            {{"text", std::format("Siatka {:g} mm", num(spacing))}, {"anchor", "se"},
                             {"fill", camera._scene_brightness < .65 ? "#b6bec7" : "#8b877f"}, {"font", "{Segoe UI} 8"}, {"tags", "viewport.legend"}});
    }
}

}  // namespace viewport_scene
