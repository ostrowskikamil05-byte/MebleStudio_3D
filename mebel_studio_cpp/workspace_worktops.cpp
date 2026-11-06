#include "workspace_worktops.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include "assembly.h"
#include "corner_furniture.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "native_model.h"
#include "native_worktops.h"
#include "pyutil.h"

namespace workspace_worktops {

using core::ValueError;
using core::Part;
using core::Point3;

namespace {

[[noreturn]] void zero_division() { throw std::domain_error("float division by zero"); }  // ZeroDivisionError

double num(const json& v) { return v.get<double>(); }

double cross(const Point& a, const Point& b) { return a[0] * b[1] - a[1] * b[0]; }

// zip(poly, poly[1:] + poly[:1])
template <class F>
void each_edge(const Polygon& poly, F&& fn) {
    for (size_t i = 0; i < poly.size(); ++i) fn(poly[i], poly[(i + 1) % poly.size()]);
}

double point_segment(const Point& p, const Point& a, const Point& b) {
    Point v = {b[0] - a[0], b[1] - a[1]};
    double length = v[0] * v[0] + v[1] * v[1];
    double t = 0;
    if (length != 0) t = std::max(0.0, std::min(1.0, ((p[0] - a[0]) * v[0] + (p[1] - a[1]) * v[1]) / length));
    return std::hypot(p[0] - (a[0] + t * v[0]), p[1] - (a[1] + t * v[1]));
}

bool inside_(const Point& point, const Polygon& poly, bool strict = false) {
    bool touching = false;
    each_edge(poly, [&](const Point& a, const Point& b) { touching = touching || point_segment(point, a, b) < 1e-6; });
    if (touching) return !strict;
    return core::inside(point, poly);
}

json polygon_json(const Polygon& poly) { return native_worktops::_polygon(poly); }

// _install_native_geometry(): rozstrzygane raz, przy pierwszym użyciu.
bool native_geometry() {
    static const bool enabled = [] {
        if (!native_model::available()) return false;
        json info = native_model::call("native", "info");
        if (info.value("layout_api_version", 0) != 1 || info.value("workspace_geometry_api_version", 0) != 1)
            throw native_model::RuntimeError("Biblioteka C++ nie obsługuje układu mebli. Użyj kompletnej paczki bieżącego etapu migracji.");
        return true;
    }();
    return enabled;
}

// _install_native_worktops(globals())
bool native_solver() {
    static const bool enabled = native_worktops::install();
    return enabled;
}

bool overlap_py(const Polygon& a, const Polygon& b) {
    for (int pass = 0; pass < 2; ++pass) {
        const Polygon& poly = pass == 0 ? a : b;
        const Polygon& other = pass == 0 ? b : a;
        Polygon ccw = _ccw(poly);
        bool found = false;
        each_edge(ccw, [&](const Point& p, const Point& q) {
            if (found) return;
            if (inside_(p, other, true)) { found = true; return; }
            double length = std::hypot(p[0] - q[0], p[1] - q[1]);
            if (length != 0) {
                Point probe = {(p[0] + q[0]) / 2 - (q[1] - p[1]) * 1e-4 / length, (p[1] + q[1]) / 2 + (q[0] - p[0]) * 1e-4 / length};
                if (inside_(probe, other, true)) found = true;
            }
        });
        if (found) return true;
        each_edge(poly, [&](const Point& p, const Point& q) {
            if (found) return;
            each_edge(other, [&](const Point& r, const Point& s) {
                if (found) return;
                Point v = {q[0] - p[0], q[1] - p[1]}, w = {s[0] - r[0], s[1] - r[1]};
                double den = cross(v, w);
                if (std::abs(den) < 1e-9) return;
                Point rp = {r[0] - p[0], r[1] - p[1]};
                double t = cross(rp, w) / den, u = cross(rp, v) / den;
                if (1e-7 < t && t < 1 - 1e-7 && 1e-7 < u && u < 1 - 1e-7) found = true;
            });
        });
        if (found) return true;
    }
    return false;
}

double distance_py(const Polygon& a, const Polygon& b) {
    if (polygons_overlap(a, b)) return 0;
    bool any = false;
    double best = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const Polygon& poly = pass == 0 ? a : b;
        const Polygon& other = pass == 0 ? b : a;
        for (const Point& p : poly)
            each_edge(other, [&](const Point& r, const Point& s) {
                double d = point_segment(p, r, s);
                if (!any || d < best) { best = d; any = true; }
            });
    }
    if (!any) throw ValueError("min() arg is an empty sequence");
    return best;
}

double distance(const Polygon& a, const Polygon& b) {
    if (native_solver()) return native_worktops::distance(a, b);
    return distance_py(a, b);
}

Polygon offset_py(const Polygon& poly_, double amount) {
    if (amount == 0) return poly_;
    Polygon poly = _ccw(poly_);
    std::vector<std::pair<Point, Point>> lines;
    each_edge(poly, [&](const Point& a, const Point& b) {
        double dx = b[0] - a[0], dy = b[1] - a[1];
        double length = std::hypot(dx, dy);
        if (length == 0) zero_division();
        lines.push_back({Point{a[0] + dy * amount / length, a[1] - dx * amount / length}, Point{dx, dy}});
    });
    Polygon result;
    for (size_t index = 0; index < lines.size(); ++index) {
        const auto& [b, v] = lines[index];
        const auto& [a, u] = lines[(index + lines.size() - 1) % lines.size()];
        double den = cross(u, v);
        if (std::abs(den) < 1e-9) { result.push_back(b); continue; }
        Point delta = {b[0] - a[0], b[1] - a[1]};
        double t = cross(delta, v) / den;
        result.push_back({a[0] + t * u[0], a[1] + t * u[1]});
    }
    return result;
}

Polygon offset(const Polygon& poly, double amount) {
    if (native_solver()) return native_worktops::offset(poly, amount);
    return offset_py(poly, amount);
}

// Trace the exposed, split boundary of connected simple polygons.
Polygon union_py(const std::vector<Polygon>& input) {
    std::vector<Polygon> polygons;
    for (const auto& poly : input) polygons.push_back(_ccw(poly));
    using Edge = std::pair<Point, Point>;
    std::set<Edge> edges;
    for (size_t index = 0; index < polygons.size(); ++index) {
        each_edge(polygons[index], [&](const Point& a, const Point& b) {
            Point v = {b[0] - a[0], b[1] - a[1]};
            double length = v[0] * v[0] + v[1] * v[1];
            std::vector<double> raw = {0., 1.};
            for (size_t other_index = 0; other_index < polygons.size(); ++other_index) {
                if (other_index == index) continue;
                each_edge(polygons[other_index], [&](const Point& p, const Point& q) {
                    Point w = {q[0] - p[0], q[1] - p[1]}, pa = {p[0] - a[0], p[1] - a[1]};
                    double den = cross(v, w);
                    if (std::abs(den) > 1e-9) {
                        double t = cross(pa, w) / den, u = cross(pa, v) / den;
                        if (-1e-7 <= t && t <= 1 + 1e-7 && -1e-7 <= u && u <= 1 + 1e-7) raw.push_back(std::max(0.0, std::min(1.0, t)));
                    } else if (std::abs(cross(pa, v)) < 1e-5) {
                        for (const Point& point : {p, q}) {
                            if (length == 0) zero_division();
                            double t = ((point[0] - a[0]) * v[0] + (point[1] - a[1]) * v[1]) / length;
                            if (0 < t && t < 1) raw.push_back(t);
                        }
                    }
                });
            }
            std::set<double> unique;  // sorted(set(round(t, 10) for t in cuts))
            for (double t : raw) unique.insert(py::round_to(t, 10));
            std::vector<double> cuts(unique.begin(), unique.end());
            for (size_t k = 0; k + 1 < cuts.size(); ++k) {
                double start = cuts[k], end = cuts[k + 1];
                if (end - start < 1e-8) continue;
                Point midpoint = {a[0] + (start + end) * v[0] / 2, a[1] + (start + end) * v[1] / 2};
                bool covered = false;
                for (size_t i = 0; i < polygons.size() && !covered; ++i)
                    covered = i != index && inside_(midpoint, polygons[i], true);
                if (covered) continue;
                Point p = {py::round_to(a[0] + start * v[0], 6), py::round_to(a[1] + start * v[1], 6)};
                Point q = {py::round_to(a[0] + end * v[0], 6), py::round_to(a[1] + end * v[1], 6)};
                if (p != q) edges.insert({p, q});
            }
        });
    }
    {
        std::set<Edge> kept;
        for (const auto& edge : edges)
            if (!edges.count({edge.second, edge.first})) kept.insert(edge);
        edges = std::move(kept);
    }
    if (edges.empty()) throw ValueError("Nie można wyznaczyć obrysu wspólnego blatu.");
    std::vector<Polygon> loops;
    while (!edges.empty()) {
        Edge first = *edges.begin();
        edges.erase(edges.begin());
        Polygon loop = {first.first};
        Point current = first.second;
        while (current != loop[0]) {
            loop.push_back(current);
            std::vector<Edge> following;
            for (const auto& edge : edges)
                if (edge.first == current) following.push_back(edge);
            if (following.size() != 1) throw ValueError("Korpusy łączą się tylko punktem lub tworzą niejednoznaczny obrys blatu.");
            Edge edge = following[0];
            edges.erase(edge);
            current = edge.second;
        }
        // Remove collinear split points; meaningful corners remain stable.
        bool changed = true;
        while (changed && loop.size() > 3) {
            changed = false;
            for (size_t i = 0; i < loop.size(); ++i) {
                const Point& p = loop[i];
                const Point& a = loop[(i + loop.size() - 1) % loop.size()];
                const Point& b = loop[(i + 1) % loop.size()];
                if (std::abs(cross({p[0] - a[0], p[1] - a[1]}, {b[0] - p[0], b[1] - p[1]})) < 1e-5) {
                    loop.erase(loop.begin() + static_cast<long>(i));
                    changed = true;
                    break;
                }
            }
        }
        loops.push_back(loop);
    }
    if (loops.size() != 1)
        throw ValueError("Wspólny blat wymaga jednego połączonego obrysu bez zamkniętego otworu. Rozdziel go na kilka blatów.");
    return _ccw(loops[0]);
}

Polygon geometry_union(const std::vector<Polygon>& polygons) {
    if (native_solver()) return native_worktops::geometry_union(polygons);
    return union_py(polygons);
}

// Zwraca (kontur, początek, kotwice).
std::tuple<Polygon, Point3, json> geometry(const core::Project& project, const std::vector<std::string>& groups, const json& config) {
    if (native_solver()) return native_worktops::geometry(project, groups, config);
    std::vector<std::tuple<Polygon, double, std::string>> bodies;
    for (const auto& group : groups) bodies.push_back(_body(project, group));
    if (bodies.empty()) throw ValueError("max() arg is an empty sequence");
    std::vector<double> heights;
    for (const auto& body : bodies) heights.push_back(std::get<1>(body));
    if (*std::max_element(heights.begin(), heights.end()) - *std::min_element(heights.begin(), heights.end()) > .1)
        throw ValueError("Szafki pod wspólnym blatem muszą mieć tę samą wysokość górnej powierzchni.");
    std::vector<Polygon> polygons;
    for (const auto& body : bodies) polygons.push_back(std::get<0>(body));
    for (size_t i = 0; i < polygons.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (polygons_overlap(polygons[i], polygons[j])) throw ValueError("Korpusy pod wspólnym blatem nachodzą na siebie.");
    std::set<size_t> visited = {0};
    while (true) {
        std::set<size_t> grown = visited;
        for (size_t i = 0; i < polygons.size(); ++i)
            for (size_t j : visited)
                if (distance(polygons[i], polygons[j]) <= .5) { grown.insert(i); break; }
        if (grown == visited) break;
        visited = grown;
    }
    if (visited.size() != polygons.size()) throw ValueError("Dosuń szafki do siebie magnesem. Wspólny blat wymaga sąsiadujących korpusów.");
    std::vector<Polygon> offsets;
    for (const auto& poly : polygons) offsets.push_back(offset(poly, num(config.at("overhang"))));
    Polygon polygon = geometry_union(offsets);
    double x = polygon[0][0], y = polygon[0][1];
    for (const auto& p : polygon) { x = std::min(x, p[0]); y = std::min(y, p[1]); }
    std::vector<std::vector<double>> shifted;
    for (const auto& p : polygon) shifted.push_back({py::round_to(p[0] - x, 6), py::round_to(p[1] - y, 6)});
    Polygon contour = core::validate_polygon(shifted);
    if (contour.size() > 128) throw ValueError("Obrys wspólnego blatu ma zbyt wiele punktów. Zmniejsz liczbę modułów lub podziel blat.");
    json anchors = json::array();
    for (const auto& body : bodies) anchors.push_back(std::get<2>(body));
    return {contour, Point3{x, y, py::sum(heights) / static_cast<double>(heights.size())}, anchors};
}

Part* source_of(core::Project& project, const std::string& group) {
    for (auto& p : project.parts)
        if (p.group == group && py::truthy(py::get(p.assembly, "shared_worktop"))) return &p;
    return nullptr;
}

std::vector<std::vector<double>> rows_of(const Polygon& polygon) {
    std::vector<std::vector<double>> rows;
    for (const auto& p : polygon) rows.push_back({p[0], p[1]});
    return rows;
}

void set_extent(Part& part, const Polygon& contour, const Point3& origin) {
    part.contour = rows_of(contour);
    double u = contour[0][0], v = contour[0][1];
    for (const auto& p : contour) { u = std::max(u, p[0]); v = std::max(v, p[1]); }
    part.u = u;
    part.v = v;
    part.x = origin[0];
    part.y = origin[1];
    part.z = origin[2];
}

void fit(const core::Project& project, Part& part) {
    json& source = part.assembly.at("shared_worktop");
    if (py::get(source, "signature") != json(_signature(part)))
        throw ValueError("Geometrię lub pozycję wspólnego blatu zmieniono ręcznie. Automatyczne dopasowanie jej nie nadpisze.");
    std::map<std::string, const Part*> by_id;
    for (const auto& p : project.parts) by_id[p.id] = &p;
    const json anchors = source.at("anchors");
    for (const auto& anchor : anchors)
        if (!by_id.count(anchor.get<std::string>())) throw ValueError("Usunięto szafkę pod wspólnym blatem. Utwórz blat dla pozostałych szafek.");
    std::vector<std::string> names;
    for (const auto& anchor : anchors) {
        const std::string& group = by_id.at(anchor.get<std::string>())->group;
        if (std::find(names.begin(), names.end(), group) == names.end()) names.push_back(group);
    }
    auto [contour, origin, new_anchors] = geometry(project, names, source.at("config"));
    set_extent(part, contour, origin);
    part.validate();
    // source.update(anchors=..., status='ready', reason='', signature=...)
    json& updated = part.assembly.at("shared_worktop");
    updated["anchors"] = new_anchors;
    updated["status"] = "ready";
    updated["reason"] = "";
    updated["signature"] = _signature(part);
}

}  // namespace

double _signed(const Polygon& poly) {
    std::vector<double> terms;
    each_edge(poly, [&](const Point& a, const Point& b) { terms.push_back(a[0] * b[1] - b[0] * a[1]); });
    return py::sum(terms) / 2;
}

Polygon _ccw(const Polygon& poly) {
    if (_signed(poly) > 0) return poly;
    return Polygon(poly.rbegin(), poly.rend());
}

bool polygons_overlap(const Polygon& a, const Polygon& b) {
    if (native_geometry()) {
        return native_model::call("workspace_geometry", "polygons_overlap", json::array({polygon_json(a), polygon_json(b)}),
                                  json{{"a_ccw", _signed(a) > 0}, {"b_ccw", _signed(b) > 0}}).get<bool>();
    }
    return overlap_py(a, b);
}

std::tuple<Polygon, double, std::string> _body(const core::Project& project, const std::string& group) {
    std::vector<const Part*> parts;
    for (const auto& p : project.parts)
        if (p.group == group) parts.push_back(&p);
    if (parts.empty()) throw ValueError("Usunięto szafkę pod wspólnym blatem.");
    json layout = furniture_layout::group_layout(project, group);
    if (std::abs(std::remainder(num(layout.at("yaw")), 90)) > 1e-7) throw ValueError("Wspólny blat wymaga obrotów korpusów o wielokrotność 90°.");
    const Part* corner = nullptr;
    for (const Part* p : parts)
        if (py::truthy(py::get(p->assembly, "corner_source")) && p->role == "bottom") { corner = p; break; }
    Polygon polygon;
    double z;
    std::string anchor;
    if (corner) {
        const json& cfg = corner->assembly.at("corner_source").at("config");
        for (const auto& q : corner->points()) {
            Point3 w = core::world(*corner, q[0], q[1], 0);
            polygon.push_back({w[0], w[1]});
        }
        double leg = cfg.contains("leg_height") ? num(cfg.at("leg_height")) : 0.0;
        z = core::world(*corner, 0, 0, num(cfg.at("height")) - leg)[2];
        anchor = corner->id;
    } else {
        json cfg = furniture_configurator::read_config(project, group);
        Point3 lo, hi;
        if (py::truthy(cfg)) {
            lo = {num(cfg.at("origin").at(0)), num(cfg.at("origin").at(1)), num(cfg.at("origin").at(2))};
            lo[2] += num(cfg.at("leg_height"));
            hi = {lo[0] + num(cfg.at("width")), lo[1] + num(cfg.at("depth")),
                  lo[2] + num(cfg.at("height")) - num(cfg.at("leg_height")) - (py::truthy(cfg.at("worktop")) ? num(cfg.at("worktop_thickness")) : 0.0)};
        } else {
            assembly::Frame f = assembly::frame(project, group);
            lo = f.lower;
            hi = f.upper;
        }
        const Part& ref = *parts[0];
        for (auto [x, y] : std::vector<std::pair<double, double>>{{lo[0], lo[1]}, {hi[0], lo[1]}, {hi[0], hi[1]}, {lo[0], hi[1]}}) {
            Point3 w = core::layout_point(ref, Point3{x, y, lo[2]});
            polygon.push_back({w[0], w[1]});
        }
        z = core::layout_point(ref, Point3{lo[0], lo[1], hi[2]})[2];
        anchor = parts[0]->id;
        for (const Part* p : parts)
            if (py::get(p->configurator_source, "key") == json("carcass.left")) { anchor = p->id; break; }
    }
    return {_ccw(polygon), z, anchor};
}

std::string _signature(const Part& part) {
    json data = json::object();
    for (const char* key : {"u", "v", "t", "plane", "x", "y", "z", "rx", "ry", "rz", "contour", "profile", "layout"}) data[key] = part.get_attr(key);
    return py::sha256_hex(py::dumps(data, true, -1, true));
}

json read_shared_worktop(const core::Project& project, const std::string& group) {
    for (const auto& p : project.parts)
        if (p.group == group && py::truthy(py::get(p.assembly, "shared_worktop"))) return p.assembly.at("shared_worktop");
    return nullptr;
}

std::pair<core::ProjectPtr, std::string> install_shared_worktop(
    const core::Project& project, const std::vector<std::string>& groups, double thickness, double overhang,
    const std::optional<std::string>& material_, const std::string& name, bool replace_individual) {
    std::vector<std::string> names;
    for (const auto& g : groups)
        if (std::find(names.begin(), names.end(), g) == names.end()) names.push_back(g);  // dict.fromkeys
    json config = json::object();
    config["thickness"] = core::finite(thickness, 1, 100);
    config["overhang"] = core::finite(overhang, 0, 300);
    if (names.empty()) throw ValueError("Zaznacz szafki pod wspólny blat.");
    auto result = std::make_shared<core::Project>(project);  // copy.deepcopy
    std::string material = (material_ && !material_->empty()) ? *material_ : result->materials[0].id;
    if (std::none_of(result->materials.begin(), result->materials.end(), [&](const core::Material& m) { return m.id == material; }))
        throw ValueError("Wybierz materiał blatu.");
    if (py::strip(name).empty()) throw ValueError("Wpisz nazwę blatu.");
    std::set<std::string> chosen_ids;
    for (const auto& p : result->parts)
        if (std::find(names.begin(), names.end(), p.group) != names.end()) chosen_ids.insert(p.id);
    for (const auto& p : result->parts) {
        json shared = py::get(p.assembly, "shared_worktop", json::object());
        json anchors = py::get(shared, "anchors", json::array());
        for (const auto& anchor : anchors)
            if (anchor.is_string() && chosen_ids.count(anchor.get<std::string>()))
                throw ValueError("Wybrane szafki mają już wspólny blat. Dopasuj istniejący albo usuń go przed utworzeniem nowego.");
    }
    for (const auto& group : names) {
        json cfg = furniture_configurator::read_config(*result, group);
        json corner = corner_furniture::read_corner_config(*result, group);
        if (cfg.is_null() && corner.is_null()) {
            try {
                cfg = furniture_configurator::infer_config(*result, group);
            } catch (const std::invalid_argument&) {}  // ValueError
        }
        bool individual = std::any_of(result->parts.begin(), result->parts.end(), [&](const Part& p) { return p.group == group && p.role == "worktop"; });
        if (individual) {
            if (!replace_individual) throw ValueError("Szafka ma własny blat. Wyłącz go albo wybierz zastąpienie blatów jednym wspólnym.");
            if (!py::truthy(cfg)) throw ValueError("Szafka zawiera ręczny blat. Usuń go świadomie przed dodaniem wspólnego.");
            cfg["height"] = num(cfg.at("height")) - num(cfg.at("worktop_thickness"));
            cfg["worktop"] = false;
        }
        if (py::truthy(cfg) && (individual || py::get(cfg, "top_mode", "panel") != json("rails") ||
                                furniture_configurator::read_config(*result, group).is_null())) {
            cfg["top_mode"] = "rails";
            result = furniture_configurator::configure(*result, cfg, group).first;
        } else if (py::truthy(corner) && corner.at("top_mode") != json("rails")) {
            corner["top_mode"] = "rails";
            result = corner_furniture::create_corner(*result, corner, group).first;
        } else if (cfg.is_null() && corner.is_null()) {
            assembly::Frame f = assembly::frame(*result, group);
            double depth = f.upper[1] - f.lower[1];
            for (const auto& p : result->parts)
                if (p.group == group && assembly::inferred_role(p) == "top" && p.plane == "XY" && p.v > depth / 2)
                    throw ValueError("Ręczna góra korpusu wymaga świadomej zamiany na listwy. Kreator wspólnego blatu jej nie usuwa.");
        }
    }
    auto [contour, origin, anchors] = geometry(*result, names, config);
    std::set<std::string> occupied;
    for (const auto& p : result->parts) occupied.insert(p.group);
    std::string base = py::strip(name), group = base;
    for (int i = 2; occupied.count(group); ++i) group = base + " (" + std::to_string(i) + ")";
    Part part;
    part.name = base;
    part.group = group;
    part.t = config.at("thickness").get<double>();
    part.material = material;
    part.role = "worktop";
    part.contour_edge = true;
    set_extent(part, contour, origin);
    part.validate();
    json shared = json::object();
    shared["schema"] = 1;
    shared["anchors"] = anchors;
    shared["config"] = config;
    shared["signature"] = _signature(part);
    shared["status"] = "ready";
    shared["reason"] = "";
    part.assembly["shared_worktop"] = shared;
    result->parts.push_back(part);
    result->validate();
    return {result, group};
}

core::ProjectPtr fit_shared_worktop(const core::Project& project, const std::string& group) {
    auto result = std::make_shared<core::Project>(project);
    Part* part = source_of(*result, group);
    if (part == nullptr) throw ValueError("Wybierz wspólny blat.");
    fit(*result, *part);
    result->validate();
    return result;
}

core::ProjectPtr refresh_shared_worktops(core::ProjectPtr project, const std::optional<std::set<std::string>>& changed_groups, bool validate) {
    std::vector<std::string> linked;
    for (const auto& p : project->parts)
        if (py::truthy(py::get(p.assembly, "shared_worktop"))) linked.push_back(p.id);
    if (linked.empty()) return project;
    auto result = std::make_shared<core::Project>(*project);  // copy.deepcopy
    std::map<std::string, size_t> index_of;
    for (size_t i = 0; i < result->parts.size(); ++i) index_of[result->parts[i].id] = i;
    for (const auto& id : linked) {
        size_t position = index_of.at(id);
        Part& part = result->parts[position];
        json anchors = py::get(part.assembly.at("shared_worktop"), "anchors", json::array());
        std::set<std::string> owners;
        bool all_present = true;
        for (const auto& anchor : anchors) {
            auto found = index_of.find(anchor.get<std::string>());
            if (found != index_of.end()) owners.insert(result->parts[found->second].group);
            else all_present = false;
        }
        if (changed_groups) {
            bool disjoint = std::none_of(owners.begin(), owners.end(), [&](const std::string& g) { return changed_groups->count(g) > 0; });
            if (!changed_groups->count(part.group) && disjoint && all_present) continue;
        }
        Part candidate = part;  // copy.deepcopy(part)
        try {
            fit(*result, candidate);
        } catch (const std::invalid_argument& error) {  // ValueError
            json& source = part.assembly.at("shared_worktop");
            source["status"] = "needs_fit";
            source["reason"] = error.what();
            continue;
        } catch (const std::out_of_range& error) {      // KeyError
            json& source = part.assembly.at("shared_worktop");
            source["status"] = "needs_fit";
            source["reason"] = error.what();
            continue;
        } catch (const json::exception& error) {        // TypeError (i brakujące klucze JSON)
            json& source = part.assembly.at("shared_worktop");
            source["status"] = "needs_fit";
            source["reason"] = error.what();
            continue;
        }
        result->parts[position] = candidate;
    }
    if (validate) result->validate();
    return result;
}

core::ProjectPtr refresh_shared_worktops(core::ProjectPtr project, bool validate) {
    return refresh_shared_worktops(std::move(project), std::nullopt, validate);
}

}  // namespace workspace_worktops
