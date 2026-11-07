// Cached native geometry for catalogue cards; no CAD kernel or image service.
//
// Furniture cards are lightweight construction schematics using the same library
// parameters. Room equipment uses its actual viewport mesh. Fittings show their
// planning dimensions and are explicitly simplified, never manufacturer meshes.
#include "catalogue_previews.h"

#include <algorithm>
#include <cmath>

#include "tk.h"
#include "fitting_visuals.h"
#include "room_objects.h"

namespace catalogue_previews {

using core::Camera;
using core::shade;
using core::Point3;
using fitting_visuals::box;
using fitting_visuals::cylinder;
using fitting_visuals::rod;

namespace {
double num(const json& value) { return value.get<double>(); }
}  // namespace

ColoredFaces preview_faces(const json& item) {
    return _faces(py::dumps({{"kind", item.at("kind")}, {"preview", py::get(item, "preview", json::object())}, {"payload", py::get(item, "payload", json::object())}}, true, -1, true));
}

ColoredFaces _faces(const std::string& key_) {
    static py::LruCache<ColoredFaces> cache(256);
    return cache.get(key_, [](const std::string& key) {
        json data = json::parse(key); json kind = data["kind"]; json p = data["preview"]; json options = data["payload"]; ColoredFaces out;
        auto put = [&](const std::vector<Face>& faces, const std::string& color) { for (auto& face : faces) out.emplace_back(face, color); };
        auto bx = [&](double x, double y, double z, double w, double d, double h, const std::string& color = "#bea174") { put(box(x, y, z, w, d, h), color); };
        if (kind == "furniture") {
            double w = num(p.at("width")), d = num(p.at("depth")), h = num(p.at("body_height")), t = num(py::get(p, "thickness", 18)); json family = p.at("family"); json legs = py::get(p, "leg_height", 0); std::string front = "#e6ddd0"; std::string wood = "#bd9b6d";
            if (family == "table" || family == "desk") {
                bx(0, 0, h - t, w, d, t, wood); double leg = std::max(40.0, t * 2);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{0, 0}, {w - leg, 0}, {0, d - leg}, {w - leg, d - leg}}) bx(x, y, 0, leg, leg, h - t, wood);
            } else if (family == "bed") {
                bx(0, t, h - 200, t, d - 2 * t, 200, wood); bx(w - t, t, h - 200, t, d - 2 * t, 200, wood); bx(0, d - t, 0, w, t, h + 400, wood); bx(0, 0, 0, w, t, h, wood);
                for (int i = 0; i < 12; ++i) bx(t, t + (d - 2 * t - 70) * i / 11, h - 30, w - 2 * t, 70, t, "#e2c699");
            } else if (family == "bench") {
                double seat = std::min(450.0, h * .6); bx(0, 0, seat, w, d, t, wood); bx(0, d - t, seat, w, t, h - seat, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{0, 0}, {w - 60, 0}, {0, d - 60}, {w - 60, d - 60}}) bx(x, y, 0, 60, 60, seat, wood);
            } else {
                double l = num(legs);
                bx(0, 0, l, t, d, h, wood); bx(w - t, 0, l, t, d, h, wood); bx(t, 0, l, w - 2 * t, d, t, wood);
                if (py::get(p, "top_mode") == "rails") {
                    bx(t, 0, l + h - t, w - 2 * t, 80, t, wood); bx(t, d - 80, l + h - t, w - 2 * t, 80, t, wood);
                } else bx(t, 0, l + h - t, w - 2 * t, d, t, wood);
                if (py::truthy(py::get(p, "back", family != "shelf" && family != "chest"))) bx(0, d, l, w, 3, h, "#92754e");
                if (family == "chest") {
                    bx(t, 0, t, w - 2 * t, t, h - t, wood); bx(t, d - t, t, w - 2 * t, t, h - t, wood); bx(0, 0, h, w, d, t, wood);
                } else {
                    json sections = py::get(p, "sections");
                    if (!py::truthy(sections))
                        sections = json::array({{{"height", h}, {"shelves", py::get(p, "shelves", 0)}, {"columns", 1}, {"front", family == "drawers" ? "drawers" : py::truthy(py::get(p, "fronts")) ? "doors" : "open"}, {"doors", py::get(p, "fronts", 1)}, {"drawers", py::get(p, "fronts", 3)}}});
                    double z = l;
                    for (auto& section : sections) {
                        double sh = num(section.at("height")); long long cols = py::get(section, "columns", 1).get<long long>(); double colw = w / cols;
                        if (z > l) bx(t, 0, z - t / 2, w - 2 * t, d, t, wood);
                        for (long long col = 0; col < cols; ++col) {
                            if (col) bx(col * colw - t / 2, 0, z + t, t, d, sh - 2 * t, wood);
                            long long n = py::get(section, "shelves", 0).get<long long>();
                            for (long long i = 0; i < n; ++i) bx(col * colw + t, 4, z + t + (sh - 3 * t) * (i + 1) / (n + 1), colw - 2 * t, d - 8, t, wood);
                            json mode = py::get(section, "front", "open");
                            if (mode == "doors") {
                                double count = std::max(1.0, num(py::get(section, "doors", 1))); double span = (colw - 2 * (count + 1)) / count;
                                for (long long i = 0; i < count; ++i) {
                                    double xx = col * colw + 2 + i * (span + 2); bx(xx, -t, z + 2, span, t, sh - 4, front);
                                    put(rod({xx + span - 30, -t - 12, z + sh * .43}, {xx + span - 30, -t - 12, z + sh * .57}, 4), "#455356");
                                }
                            } else if (mode == "drawers") {
                                double count = std::max(1.0, num(py::get(section, "drawers", 3))); double fh = (sh - 2 * (count + 1)) / count;
                                for (long long i = 0; i < count; ++i) {
                                    double zz = z + 2 + i * (fh + 2); bx(col * colw + 2, -t, zz, colw - 4, t, fh, front);
                                    put(rod({col * colw + colw * .34, -t - 12, zz + fh / 2}, {col * colw + colw * .66, -t - 12, zz + fh / 2}, 4), "#455356");
                                }
                            } else if (mode == "cargo" || mode == "laundry") {
                                // Jeden wysuwany front na całą strefę z pionowym uchwytem.
                                bx(col * colw + 2, -t, z + 2, colw - 4, t, sh - 4, front);
                                put(rod({col * colw + colw * .5, -t - 12, z + sh * .6}, {col * colw + colw * .5, -t - 12, z + sh * .86}, 4), "#455356");
                            }
                        }
                        z += sh;
                    }
                }
                if (py::truthy(legs)) {
                    for (auto [x, y] : std::vector<std::pair<double, double>>{{60, 60}, {w - 60, 60}, {60, d - 60}, {w - 60, d - 60}}) put(cylinder(x, y, 0, 20, l, 10), "#353f41");
                }
                if (family == "island") bx(-20, -20, h, w + 40, d + 40, 38, "#7e6c59");
            }
        } else if (kind == "shaped") {
            // Uproszczona bryła wzorca obłego: elipsoidalne blaty i okrągłe nogi (bez siatki CAD).
            double w = num(p.at("width")), d = num(p.at("depth")), h = num(p.at("body_height"));
            std::string family = p.at("family").get<std::string>();
            const std::string wood = "#bd9b6d", light = "#e6ddd0";
            const double t = 28;
            const double pi = std::acos(-1.0);
            auto slab = [&](double cx, double cy, double rx, double ry, double z, double th, const std::string& color, bool half = false) {
                const int n = 28;
                std::vector<core::Point3> top, bottom;
                for (int i = 0; i < n; ++i) {
                    double a = half ? pi * i / (n - 1) : 2 * pi * i / n;
                    double x = cx + rx * std::cos(a), y = cy + ry * std::sin(a);
                    top.push_back({x, y, z + th});
                    bottom.push_back({x, y, z});
                }
                out.emplace_back(top, color);
                for (int i = 0; i < n; ++i) {
                    int j = (i + 1) % n;
                    out.emplace_back(std::vector<core::Point3>{bottom[i], bottom[j], top[j], top[i]}, color);
                }
            };
            auto leg = [&](double x, double y, double r, double height, double z = 0) { put(cylinder(x, y, z, r, height, 12), wood); };
            if (family == "rounded_table") {
                slab(w / 2, d / 2, w / 2, d / 2, h - t, t, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{w * .18, d * .22}, {w * .82, d * .22}, {w * .18, d * .78}, {w * .82, d * .78}}) leg(x, y, 32, h - t);
            } else if (family == "oval_table") {
                slab(w / 2, d / 2, w / 2, d / 2, h - t, t, wood);
                slab(w / 2, d / 2, w * .36, d * .36, h * .3, 18, wood);
                leg(w * .28, d / 2, 38, h - t);
                leg(w * .72, d / 2, 38, h - t);
            } else if (family == "rounded_desk") {
                bx(0, 0, h - t, w, d, t, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{40, 40}, {w - 90, 40}, {40, d - 90}, {w - 90, d - 90}}) bx(x, y, 0, 50, 50, h - t, wood);
            } else if (family == "chair") {
                slab(w / 2, d / 2, w / 2, d / 2, 450, 30, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{40, 40}, {w - 40, 40}, {40, d - 40}, {w - 40, d - 40}}) leg(x, y, 18, 450);
                bx(30, d - 30, 480, w - 60, 24, h - 480, wood);
            } else if (family == "stool") {
                slab(w / 2, d / 2, w / 2, d / 2, h - 30, 30, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{w * .25, d * .3}, {w * .75, d * .3}, {w * .5, d * .8}}) leg(x, y, 18, h - 30);
            } else if (family == "console") {
                slab(w / 2, d / 2, w / 2, d / 2, h - t, t, wood);
                bx(w * .12, d * .2, 0, 40, d * .6, h - t, wood);
                bx(w * .88 - 40, d * .2, 0, 40, d * .6, h - t, wood);
                bx(w * .12, d * .15, h * .2, w * .76, d * .7, 18, wood);
            } else if (family == "vanity") {
                double table = 750;
                slab(w / 2, d / 2, w / 2, d / 2, table - t, t, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{70, 60}, {w - 70, 60}, {70, d - 60}, {w - 70, d - 60}}) leg(x, y, 24, table - t);
                bx(w * .2, d - 20, table, w * .6, 20, h - table, "#d8dde0");
            } else if (family == "slatted") {
                bx(0, 0, 0, w, d, h - t, wood);
                bx(4, -16, 4, w / 2 - 6, 16, h - t - 8, light);
                bx(w / 2 + 2, -16, 4, w / 2 - 6, 16, h - t - 8, light);
                slab(w / 2, d / 2, w / 2 + 10, d / 2 + 10, h - t, t, wood);
            } else if (family == "nightstand") {
                bx(0, 0, 0, w, d, h - t, wood);
                bx(4, -16, 4, w - 8, 16, h - t - 8, light);
                slab(w / 2, d / 2, w / 2 + 12, d / 2 + 12, h - t, t, wood);
            } else if (family == "bench") {
                slab(w / 2, d / 2, w / 2, d / 2, 430, 40, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{60, 60}, {w - 60, 60}, {60, d - 60}, {w - 60, d - 60}}) leg(x, y, 25, 430);
                bx(0, d - 30, 470, w, 30, h - 470, wood);
            } else if (family == "mirror") {
                bx(0, 0, 0, w, d, h, "#d8dde0");
                bx(w * .1, -4, h * .08, w * .8, 4, h * .84, "#aeb7bb");
            } else if (family == "half_table") {
                slab(w / 2, 0, w / 2, d, h - t, t, wood, true);
                leg(w * .15, d * .25, 28, h - t);
                leg(w * .85, d * .25, 28, h - t);
                leg(w / 2, d * .85, 28, h - t);
            } else {
                bx(0, 0, h - t, w, d, t, wood);
                for (auto [x, y] : std::vector<std::pair<double, double>>{{20, 20}, {w - 60, 20}, {20, d - 60}, {w - 60, d - 60}}) bx(x, y, 0, 40, 40, h - t, wood);
            }
        } else if (kind == "appliance") {
            json spec = json::object(); for (std::string k : {"type", "width", "depth", "height", "color"}) spec[k] = p.at(k);
            spec["interactive"] = true;
            for (auto& [face, color, emissive, glass] : room_objects::_geometry(py::dumps(spec, true, -1, true))) out.emplace_back(face, color);
        } else if (kind == "leg") {
            double foot = std::min(6.0, num(p.at("height")) / 4); std::string color = py::get(p, "color", "#353f41");
            put(cylinder(30, 30, 0, num(p.at("diameter")) * 1.15 / 2, foot, 14), color); put(cylinder(30, 30, foot, num(p.at("diameter")) / 2, num(p.at("height")) - foot, 14), color);
        } else if (kind == "runner") {
            double length = num(py::get(p, "length", 400)); double clearance = num(py::get(p, "clearance", 12.7)); double height = num(py::get(p, "height", 46));
            const char* colors[] = {"#aab5ba", "#d6dcde", "#73848b"};
            for (int i = 0; i < 3; ++i) bx(i * length * .1, i * clearance * .27, i * height * .08, length * (1 - i * .12), clearance * .3, height * (1 - i * .14), colors[i]);
            if (py::get(p, "mount") == "undermount") bx(length * .04, clearance, 0, length * .88, 36, 8, "#aab5ba");
        } else if (kind == "hinge") {
            double diameter = num(p.at("diameter")); double pitch = num(p.at("pitch")); double offset = num(p.at("offset")); bx(0, -pitch / 2 - 8, 0, diameter + 15, pitch + 16, 3, "#a8b3b7");
            put(cylinder(5 + diameter / 2, 0, 3, diameter / 2, num(py::get(p, "depth", 12.5)), 18), "#9ba9b1"); bx(diameter + 8, -8, 5, 55, 16, 8, "#b8c3c7"); bx(diameter + 60, -20, 0, 22, 40, 4, "#809198");
            for (double y : {-pitch / 2, pitch / 2}) put(cylinder(5 + diameter / 2 - offset, y, 3, 2, 4, 8), "#44565d");
        } else if (kind == "handle") {
            json mode = py::get(options, "option"); json spacing = py::get(options, "spacing", 128);
            if (mode == "finger_pocket" || mode == "finger_slot") {
                bx(0, 0, 0, 170, 48, 18, "#c5a475"); bx(25, -.5, 7, 120, 6, 8, "#5b4636");
            } else if (mode == "edge_pull") { bx(0, 0, 0, 150, 10, 20, "#414e54"); bx(0, -25, 18, 150, 35, 3, "#414e54"); }
            else if (py::truthy(mode) && (mode.get<std::string>().starts_with("tip") || mode.get<std::string>().starts_with("salice"))) { put(cylinder(12, 12, 0, 5, num(py::get(p, "depth", 50)), 12), "#b3bec3"); put(cylinder(12, 12, num(py::get(p, "depth", 50)), 3, 10, 10), "#49555c"); }
            else if (mode == "none") bx(0, 0, 0, 150, 8, 90, "#d1bb9d");
            else if (!py::truthy(spacing)) { put(cylinder(12, 12, 0, 6, 18, 10), "#505c61"); put(cylinder(12, 12, 18, 12, 10, 14), "#87979f"); }
            else {
                double s = num(spacing);
                for (double x : {0.0, s}) put(rod({x, 0, 0}, {x, 0, 25}, 5), "#53656d");
                put(rod({0, 0, 25}, {s, 0, 25}, 5), "#a4b3ba");
            }
        } else if (kind == "led") {
            double width = num(options.at("width")); bx(0, 0, 0, 300, width, 4, "#75848c");
            bx(0, 1, 4, 300, std::max(1.0, width - 2), 1, "#eadfc0");
            for (int x = 6; x < 300; x += 18) bx(x, width * .3, 5, 6, width * .4, .7, "#fff4bd");
        } else if (kind == "material") bx(0, 0, 0, 180, 120, 12, p.at("color").get<std::string>());
        return out;
    });
}

void draw_preview(tk::Canvas* canvas, const json& item, int width, int height) {
    canvas->delete_("preview"); ColoredFaces faces = preview_faces(item);
    if (faces.empty()) return;
    Camera camera(-38, 22, 1, {0, 0, 0});
    std::vector<Point3> vertices; for (auto& [face, color] : faces) for (auto& v : face) vertices.push_back(camera.view(v));
    std::array<double, 2> lo, hi;
    for (int i : {0, 1}) {
        lo[i] = vertices[0][i]; hi[i] = vertices[0][i];
        for (auto& v : vertices) { lo[i] = std::min(lo[i], v[i]); hi[i] = std::max(hi[i], v[i]); }
    }
    double scale = std::min((width - 18) / std::max(hi[0] - lo[0], 1.0), (height - 14) / std::max(hi[1] - lo[1], 1.0)); double cx = (lo[0] + hi[0]) / 2, cy = (lo[1] + hi[1]) / 2;
    auto mean_depth = [&](const Face& face) { double sum = 0; for (auto& v : face) sum += camera.view(v)[2]; return sum / face.size(); };
    ColoredFaces ordered = faces;
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) { return mean_depth(a.first) < mean_depth(b.first); });
    for (auto& [face, color] : ordered) {
        std::vector<double> coords;
        for (auto& v : face) { auto [x, y, depth] = camera.view(v); coords.push_back(width / 2.0 + (x - cx) * scale); coords.push_back(height / 2.0 - (y - cy) * scale); }
        canvas->create_polygon(coords, {{"fill", color}, {"outline", shade(color, .78)}, {"width", .5}, {"tags", std::vector<std::string>{"preview", py::str(item.at("id"))}}});
    }
    if (item.at("kind") == "material") {
        json spec = py::get(item, "preview", json::object()); json wood = py::get(spec, "wood", false);
        if (py::truthy(wood)) {
            for (int i = 0; i < 8; ++i) canvas->create_line(std::vector<double>{20.0, 20.0 + i * 11, width - 20.0, 30.0 + i * 9}, {{"fill", shade(spec.at("color").get<std::string>(), .91)}, {"width", 1}, {"tags", "preview"}});
        }
    }
}

}  // namespace catalogue_previews
