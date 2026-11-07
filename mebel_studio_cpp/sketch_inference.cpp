#include "sketch_inference.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sketch_inference {

namespace {

constexpr double EPS = 1e-9;

double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1]; }
double cross(const Point& a, const Point& b) { return a[0] * b[1] - a[1] * b[0]; }
Point sub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1]}; }
Point add_scaled(const Point& a, const Point& d, double k) { return {a[0] + d[0] * k, a[1] + d[1] * k}; }

std::optional<Point> unit(const Point& v) {
    double len = std::hypot(v[0], v[1]);
    if (len < EPS) return std::nullopt;
    return Point{v[0] / len, v[1] / len};
}

Point closest_on_segment(const Segment& s, const Point& p) {
    Point d = sub(s.b, s.a);
    double len2 = dot(d, d);
    if (len2 < EPS) return s.a;
    double t = std::clamp(dot(sub(p, s.a), d) / len2, 0.0, 1.0);
    return add_scaled(s.a, d, t);
}

std::optional<Point> intersect(const Segment& s1, const Segment& s2) {
    Point r = sub(s1.b, s1.a), q = sub(s2.b, s2.a);
    double denom = cross(r, q);
    if (std::abs(denom) < EPS) return std::nullopt;
    Point delta = sub(s2.a, s1.a);
    double t = cross(delta, q) / denom, u = cross(delta, r) / denom;
    if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return std::nullopt;
    return add_scaled(s1.a, r, t);
}

// Przecięcie nieskończonej prostej (p, dir) z odcinkiem.
std::optional<Point> intersect_line(const Point& p, const Point& dir, const Segment& s) {
    Point q = sub(s.b, s.a);
    double denom = cross(dir, q);
    if (std::abs(denom) < EPS) return std::nullopt;
    Point delta = sub(s.a, p);
    double u = cross(delta, dir) / denom;
    if (u < -1e-9 || u > 1 + 1e-9) return std::nullopt;
    return add_scaled(s.a, q, u);
}

std::vector<Segment> polyline_segments(const Input& in) {
    std::vector<Segment> out;
    for (size_t i = 0; i + 1 < in.points.size(); ++i) out.push_back({in.points[i], in.points[i + 1]});
    if (in.closed && in.points.size() >= 3) out.push_back({in.points.back(), in.points.front()});
    return out;
}

Result make(Kind kind, const Point& point) {
    Result r;
    r.kind = kind;
    r.point = point;
    r.tip = tip_for(kind);
    r.color = color_for(kind);
    return r;
}

void set_direction(Result& r, const Input& in) {
    if (!in.anchor) return;
    r.direction = unit(sub(r.point, *in.anchor));
}

}  // namespace

std::string tip_for(Kind kind) {
    switch (kind) {
        case Kind::Grid: return "Siatka";
        case Kind::Endpoint: return "Punkt końcowy";
        case Kind::Midpoint: return "Środek";
        case Kind::Intersection: return "Przecięcie";
        case Kind::Center: return "Środek okręgu";
        case Kind::OnEdge: return "Na krawędzi";
        case Kind::AxisU: return "Na osi U (czerwonej)";
        case Kind::AxisV: return "Na osi V (zielonej)";
        case Kind::Perpendicular: return "Prostopadle";
        case Kind::Parallel: return "Równolegle";
        case Kind::ExtendEdge: return "Przedłużenie krawędzi";
        case Kind::FromPoint: return "Od punktu";
        case Kind::Locked: return "Kierunek zablokowany";
        case Kind::None: break;
    }
    return "";
}

std::string color_for(Kind kind) {
    switch (kind) {
        case Kind::Endpoint: case Kind::Center: return "#2e9e4f";
        case Kind::Midpoint: return "#12a8c4";
        case Kind::Intersection: case Kind::OnEdge: return "#d63b3b";
        case Kind::AxisU: return "#d63b3b";
        case Kind::AxisV: return "#2e9e4f";
        case Kind::Perpendicular: case Kind::Parallel: return "#c2399c";
        case Kind::ExtendEdge: return "#555555";
        case Kind::FromPoint: return "#8a8a8a";
        case Kind::Locked: return "#e08a00";
        case Kind::Grid: return "#9aa5b1";
        case Kind::None: break;
    }
    return "#4e6b3f";
}

bool is_direction(Kind kind) {
    return kind == Kind::AxisU || kind == Kind::AxisV || kind == Kind::Perpendicular || kind == Kind::Parallel || kind == Kind::ExtendEdge || kind == Kind::Locked;
}

bool is_point_inference(Kind kind) {
    return kind == Kind::Endpoint || kind == Kind::Midpoint || kind == Kind::Intersection || kind == Kind::Center || kind == Kind::OnEdge;
}

Result infer(const Input& in) {
    const double tol = std::max(in.tolerance, 1e-6);
    Result result = make(Kind::None, in.raw);
    std::vector<Segment> own = polyline_segments(in);
    std::vector<Segment> all = own;
    all.insert(all.end(), in.reference.begin(), in.reference.end());

    // --- zablokowany kierunek (Shift / strzałki): rzut kursora na prostą anchor + t·dir
    if (in.anchor && in.lock_direction) {
        const Point dir = *unit(*in.lock_direction);
        const Point& a = *in.anchor;
        Point p = add_scaled(a, dir, dot(sub(in.raw, a), dir));
        Kind kind = std::abs(dir[1]) < 1e-9 ? Kind::AxisU : std::abs(dir[0]) < 1e-9 ? Kind::AxisV : Kind::Locked;
        Result locked = make(kind, p);
        // przecięcia zablokowanej prostej z krawędziami mają pierwszeństwo, jeśli są blisko kursora
        if (in.snap_points) {
            double best = tol;
            for (const auto& s : all) {
                auto hit = intersect_line(a, dir, s);
                if (hit && dist(*hit, p) < best) { best = dist(*hit, p); locked = make(Kind::Intersection, *hit); }
            }
        }
        if (locked.kind == Kind::Intersection) locked.guides.push_back({a, locked.point, color_for(kind)});
        locked.direction = dir;
        return locked;
    }

    // --- punkty: końcowy › środek › przecięcie › środek okręgu › na krawędzi
    if (in.snap_points) {
        auto nearest = [&](const std::vector<Point>& candidates) -> std::optional<Point> {
            std::optional<Point> best;
            double best_distance = tol;
            for (const auto& c : candidates) {
                double d = dist(c, in.raw);
                if (d <= best_distance) { best_distance = d; best = c; }
            }
            return best;
        };
        std::vector<Point> endpoints = in.points, midpoints;
        for (const auto& s : all) {
            endpoints.push_back(s.a);
            endpoints.push_back(s.b);
            midpoints.push_back({(s.a[0] + s.b[0]) / 2, (s.a[1] + s.b[1]) / 2});
        }
        if (auto p = nearest(endpoints)) { result = make(Kind::Endpoint, *p); set_direction(result, in); return result; }
        if (auto p = nearest(midpoints)) { result = make(Kind::Midpoint, *p); set_direction(result, in); return result; }
        std::vector<Point> crossings;
        const size_t limit = std::min<size_t>(all.size(), 80);
        for (size_t i = 0; i < limit; ++i)
            for (size_t j = i + 1; j < limit; ++j)
                if (auto hit = intersect(all[i], all[j])) crossings.push_back(*hit);
        if (auto p = nearest(crossings)) { result = make(Kind::Intersection, *p); set_direction(result, in); return result; }
        if (auto p = nearest(in.centers)) { result = make(Kind::Center, *p); set_direction(result, in); return result; }
        std::optional<Point> on_edge;
        double best_distance = tol;
        for (const auto& s : all) {
            Point c = closest_on_segment(s, in.raw);
            double d = dist(c, in.raw);
            if (d <= best_distance) { best_distance = d; on_edge = c; }
        }
        if (on_edge) { result = make(Kind::OnEdge, *on_edge); set_direction(result, in); return result; }
    }

    Point p = in.raw;

    // --- kierunki od ostatniego punktu
    if (in.anchor && in.snap_points) {
        const Point& a = *in.anchor;
        Point d = sub(in.raw, a);
        double length = std::hypot(d[0], d[1]);
        if (length > tol) {
            if (std::abs(d[1]) <= tol) { result = make(Kind::AxisU, {in.raw[0], a[1]}); }
            else if (std::abs(d[0]) <= tol) { result = make(Kind::AxisV, {a[0], in.raw[1]}); }
            else {
                // prostopadle / równolegle do ostatniego odcinka konturu oraz do najbliższej krawędzi
                std::vector<Segment> refs;
                if (!own.empty() && !in.closed) refs.push_back(own.back());
                std::optional<Segment> hover;
                double hover_distance = tol * 6;
                for (const auto& s : all) {
                    double dd = dist(closest_on_segment(s, in.raw), in.raw);
                    if (dd < hover_distance) { hover_distance = dd; hover = s; }
                }
                if (hover) refs.push_back(*hover);
                double best = tol;
                Kind best_kind = Kind::None;
                Point best_dir{0, 0};
                for (const auto& s : refs) {
                    auto direction = unit(sub(s.b, s.a));
                    if (!direction) continue;
                    if (std::abs((*direction)[0]) < 1e-9 || std::abs((*direction)[1]) < 1e-9) {
                        // odcinek osiowy: prostopadłe i równoległe pokrywa się z osiami U/V
                        continue;
                    }
                    const Point normal{-(*direction)[1], (*direction)[0]};
                    for (auto [candidate, kind] : {std::pair<Point, Kind>{normal, Kind::Perpendicular}, std::pair<Point, Kind>{*direction, Kind::Parallel}}) {
                        double off = std::abs(cross(d, candidate));
                        if (off <= best && std::abs(dot(d, candidate)) > tol) { best = off; best_kind = kind; best_dir = candidate; }
                    }
                }
                // przedłużenie krawędzi: półprosta od końca odcinka, poza nim
                if (best_kind == Kind::None) {
                    double best_ext = tol;
                    for (const auto& s : all) {
                        for (int end = 0; end < 2; ++end) {
                            const Point& tip = end == 0 ? s.a : s.b;
                            const Point& other = end == 0 ? s.b : s.a;
                            if (dist(tip, in.raw) > tol * 40) continue;
                            auto direction = unit(sub(tip, other));
                            if (!direction) continue;
                            Point v = sub(in.raw, tip);
                            double along = dot(v, *direction);
                            if (along <= tol) continue;
                            double off = std::abs(cross(v, *direction));
                            if (off <= best_ext) { best_ext = off; best_kind = Kind::ExtendEdge; best_dir = *direction;
                                result = make(Kind::ExtendEdge, add_scaled(tip, *direction, along));
                                result.guides.push_back({tip, result.point, color_for(Kind::ExtendEdge)}); }
                        }
                    }
                    if (best_kind == Kind::ExtendEdge) { set_direction(result, in); p = result.point; }
                } else {
                    double along = dot(d, best_dir);
                    result = make(best_kind, add_scaled(a, best_dir, along));
                    result.guides.push_back({a, result.point, color_for(best_kind)});
                }
            }
            if (result.kind == Kind::AxisU || result.kind == Kind::AxisV) result.guides.push_back({a, result.point, color_for(result.kind)});
            if (result.kind != Kind::None) { p = result.point; set_direction(result, in); }
        }
    }

    // --- Shift / ortogonalnie: wymuś poziomo albo pionowo względem anchor
    if (in.ortho && in.anchor && result.kind == Kind::None) {
        const Point& a = *in.anchor;
        bool horizontal = std::abs(in.raw[0] - a[0]) >= std::abs(in.raw[1] - a[1]);
        result = make(horizontal ? Kind::AxisU : Kind::AxisV, horizontal ? Point{in.raw[0], a[1]} : Point{a[0], in.raw[1]});
        result.guides.push_back({a, result.point, color_for(result.kind)});
        p = result.point;
        set_direction(result, in);
    }

    // --- „od punktu”: wyrównanie współrzędnej do innego wierzchołka (linia kropkowana)
    if (in.snap_points && (result.kind == Kind::None || result.kind == Kind::AxisU || result.kind == Kind::AxisV)) {
        std::vector<Point> vertices = in.points;
        for (const auto& s : in.reference) { vertices.push_back(s.a); vertices.push_back(s.b); }
        std::optional<Point> align_x, align_y;
        double best_x = tol, best_y = tol;
        for (const auto& q : vertices) {
            if (in.anchor && dist(q, *in.anchor) < EPS) continue;
            double dx = std::abs(q[0] - p[0]), dy = std::abs(q[1] - p[1]);
            if (result.kind != Kind::AxisV && dx <= best_x) { best_x = dx; align_x = q; }
            if (result.kind != Kind::AxisU && dy <= best_y) { best_y = dy; align_y = q; }
        }
        if (align_x || align_y) {
            Point snapped = p;
            std::vector<Guide> guides = result.guides;
            if (align_x) snapped[0] = (*align_x)[0];
            if (align_y) snapped[1] = (*align_y)[1];
            if (align_x) guides.push_back({*align_x, snapped, color_for(Kind::FromPoint)});
            if (align_y) guides.push_back({*align_y, snapped, color_for(Kind::FromPoint)});
            Kind keep = result.kind;
            result = make(keep == Kind::None ? Kind::FromPoint : keep, snapped);
            result.guides = guides;
            if (keep != Kind::None) result.tip += " · od punktu";
            p = snapped;
            set_direction(result, in);
        }
    }

    // --- siatka dla współrzędnych, które nic jeszcze nie ustaliło
    if (in.grid && in.grid_step > 0) {
        auto round_to = [&](double value) { return std::nearbyint(value / in.grid_step) * in.grid_step; };
        bool free_x = result.kind != Kind::AxisV && result.kind != Kind::FromPoint && result.kind != Kind::Perpendicular && result.kind != Kind::Parallel && result.kind != Kind::ExtendEdge;
        bool free_y = result.kind != Kind::AxisU && result.kind != Kind::FromPoint && result.kind != Kind::Perpendicular && result.kind != Kind::Parallel && result.kind != Kind::ExtendEdge;
        if (result.kind == Kind::None) {
            result = make(Kind::Grid, {round_to(p[0]), round_to(p[1])});
            set_direction(result, in);
        } else if (result.kind == Kind::AxisU || result.kind == Kind::AxisV) {
            if (free_x) result.point[0] = round_to(result.point[0]);
            if (free_y) result.point[1] = round_to(result.point[1]);
            for (auto& g : result.guides) if (dist(g.from, *in.anchor) < EPS) g.to = result.point;
            set_direction(result, in);
        }
    }
    return result;
}

}  // namespace sketch_inference
