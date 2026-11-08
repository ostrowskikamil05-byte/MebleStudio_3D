#include "cad2d.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

#include "sketch_shapes.h"

namespace cad2d {

namespace {

[[noreturn]] void fail(const std::string& text) { throw std::invalid_argument(text); }

std::string num(double x) { return std::format("{:g}", std::round(x * 100) / 100); }

double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
Point sub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1]}; }
Point add(const Point& a, const Point& b) { return {a[0] + b[0], a[1] + b[1]}; }
Point mul(const Point& a, double k) { return {a[0] * k, a[1] * k}; }
double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1]; }
double cross(const Point& a, const Point& b) { return a[0] * b[1] - a[1] * b[0]; }
Point unit(const Point& a) {
    double l = std::hypot(a[0], a[1]);
    if (l < 1e-12) fail("Dwa punkty leżą w tym samym miejscu.");
    return {a[0] / l, a[1] / l};
}
bool finite(const Point& p) { return std::isfinite(p[0]) && std::isfinite(p[1]); }
double norm_positive(double a) {  // do przedziału (0, 2π]
    while (a <= 1e-12) a += 2 * PI;
    while (a > 2 * PI + 1e-12) a -= 2 * PI;
    return a;
}

void check_point(const Point& p) {
    if (!finite(p)) fail("Współrzędna nie jest liczbą.");
    if (std::fabs(p[0]) > 1e5 || std::fabs(p[1]) > 1e5) fail("Współrzędna wykracza poza 100 m od początku układu.");
}

bool on_segment_distance(const Point& a, const Point& b, const Point& p, double* out) {
    Point d = sub(b, a);
    double l2 = dot(d, d);
    double t = l2 < 1e-18 ? 0 : std::clamp(dot(sub(p, a), d) / l2, 0.0, 1.0);
    *out = dist(p, add(a, mul(d, t)));
    return true;
}

double distance_to_segment(const Segment& s, const Point& p) {
    if (std::fabs(s.bulge) < 1e-12) {
        double d;
        on_segment_distance(s.a, s.b, p, &d);
        return d;
    }
    Arc arc = arc_of(s.a, s.b, s.bulge);
    double angle = std::atan2(p[1] - arc.center[1], p[0] - arc.center[0]);
    double rel = arc.sweep >= 0 ? norm_positive(angle - arc.start) : norm_positive(arc.start - angle);
    if (rel <= std::fabs(arc.sweep) + 1e-9) return std::fabs(dist(p, arc.center) - arc.radius);
    return std::min(dist(p, s.a), dist(p, s.b));
}

bool segments_cross(const Point& a, const Point& b, const Point& c, const Point& d) {
    auto orient = [](const Point& p, const Point& q, const Point& r) { return cross(sub(q, p), sub(r, p)); };
    double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool point_in_polygon(const Point& q, const std::vector<Point>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point &a = poly[i], &b = poly[j];
        if ((a[1] > q[1]) != (b[1] > q[1]) && q[0] < (b[0] - a[0]) * (q[1] - a[1]) / (b[1] - a[1]) + a[0]) in = !in;
    }
    return in;
}

}  // namespace

// ------------------------------------------------------------------ łuki

Arc arc_of(const Point& a, const Point& b, double bulge) {
    if (std::fabs(bulge) < 1e-12) fail("Odcinek prosty nie ma łuku.");
    double chord = dist(a, b);
    if (chord < 1e-9) fail("Początek i koniec łuku leżą w tym samym miejscu.");
    Arc arc;
    arc.sweep = 4 * std::atan(bulge);
    arc.radius = chord * (1 + bulge * bulge) / (4 * std::fabs(bulge));
    Point mid = mul(add(a, b), .5);
    Point dir = unit(sub(b, a));
    Point left{-dir[1], dir[0]};
    double offset = (chord / 2) * (1 - bulge * bulge) / (2 * bulge);
    arc.center = add(mid, mul(left, offset));
    arc.start = std::atan2(a[1] - arc.center[1], a[0] - arc.center[0]);
    return arc;
}

double bulge_three_points(const Point& a, const Point& through, const Point& c) {
    check_point(a); check_point(through); check_point(c);
    double d = 2 * (a[0] * (through[1] - c[1]) + through[0] * (c[1] - a[1]) + c[0] * (a[1] - through[1]));
    if (std::fabs(d) < 1e-9) fail("Trzy punkty leżą na jednej prostej — łuk przez nie nie istnieje. Wskaż punkt poza prostą.");
    double a2 = dot(a, a), b2 = dot(through, through), c2 = dot(c, c);
    Point o{(a2 * (through[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - through[1])) / d, (a2 * (c[0] - through[0]) + b2 * (a[0] - c[0]) + c2 * (through[0] - a[0])) / d};
    bool ccw = cross(sub(through, a), sub(c, through)) > 0;
    double alpha = std::atan2(a[1] - o[1], a[0] - o[0]), gamma = std::atan2(c[1] - o[1], c[0] - o[0]);
    double sweep = ccw ? norm_positive(gamma - alpha) : -norm_positive(alpha - gamma);
    if (std::fabs(sweep) > 359 * PI / 180) fail("Łuk jest prawie pełnym okręgiem — użyj narzędzia Okrąg albo rozdziel łuk na dwa.");
    return std::tan(sweep / 4);
}

double bulge_tangent(const Point& a, const Point& tangent_direction, const Point& b) {
    check_point(a); check_point(b);
    Point t = unit(tangent_direction), d = sub(b, a);
    if (dot(d, d) < 1e-12) fail("Początek i koniec łuku leżą w tym samym miejscu.");
    double phi = std::atan2(cross(t, d), dot(t, d));
    if (std::fabs(phi) < 1e-6) fail("Koniec leży na przedłużeniu poprzedniego odcinka — łuk styczny byłby prostą. Wskaż punkt z boku.");
    if (std::fabs(phi) > 175 * PI / 180) fail("Łuk styczny miałby się zawracać o niemal 360° — wskaż punkt dalej w kierunku odcinka.");
    return std::tan(phi / 2);
}

double bulge_center(const Point& center, const Point& a, const Point& b, bool counter_clockwise) {
    check_point(center); check_point(a); check_point(b);
    if (dist(center, a) < 1e-9 || dist(center, b) < 1e-9) fail("Środek łuku nie może leżeć w jego końcu.");
    double alpha = std::atan2(a[1] - center[1], a[0] - center[0]), gamma = std::atan2(b[1] - center[1], b[0] - center[0]);
    double sweep = counter_clockwise ? norm_positive(gamma - alpha) : -norm_positive(alpha - gamma);
    if (std::fabs(sweep) > 359 * PI / 180) fail("Koniec łuku leży na jego początku — łuk byłby pełnym okręgiem. Użyj narzędzia Okrąg.");
    return std::tan(sweep / 4);
}

// ------------------------------------------------------------------ odcinki

size_t segment_count(const Path& path) {
    if (path.v.size() < 2) return 0;
    return path.closed ? path.v.size() : path.v.size() - 1;
}

Segment segment(const Path& path, size_t index) {
    if (index >= segment_count(path)) fail(std::format("Odcinek {} nie istnieje (kontur ma {}).", index + 1, segment_count(path)));
    return {path.v[index].p, path.v[(index + 1) % path.v.size()].p, path.v[index].bulge};
}

double segment_length(const Segment& s) {
    if (std::fabs(s.bulge) < 1e-12) return dist(s.a, s.b);
    Arc arc = arc_of(s.a, s.b, s.bulge);
    return arc.radius * std::fabs(arc.sweep);
}

Point segment_point(const Segment& s, double t) {
    if (std::fabs(s.bulge) < 1e-12) return add(s.a, mul(sub(s.b, s.a), t));
    Arc arc = arc_of(s.a, s.b, s.bulge);
    double angle = arc.start + arc.sweep * t;
    return {arc.center[0] + arc.radius * std::cos(angle), arc.center[1] + arc.radius * std::sin(angle)};
}

Point segment_direction(const Segment& s, bool at_end) {
    if (std::fabs(s.bulge) < 1e-12) return unit(sub(s.b, s.a));
    Arc arc = arc_of(s.a, s.b, s.bulge);
    double angle = arc.start + (at_end ? arc.sweep : 0);
    double sign = arc.sweep >= 0 ? 1 : -1;
    return {-std::sin(angle) * sign, std::cos(angle) * sign};
}

// ------------------------------------------------------------------ przybliżenie

namespace {
std::vector<Point> tessellate_once(const Path& path, double tolerance) {
    std::vector<Point> out;
    const size_t n = path.v.size();
    const size_t segments = segment_count(path);
    for (size_t i = 0; i < segments; ++i) {
        Segment s = segment(path, i);
        out.push_back(s.a);
        if (std::fabs(s.bulge) < 1e-12) continue;
        Arc arc = arc_of(s.a, s.b, s.bulge);
        double step = arc.radius <= tolerance ? PI / 2 : 2 * std::acos(1 - tolerance / arc.radius);
        step = std::clamp(step, 0.02, PI / 2);
        int parts = std::max(2, (int)std::ceil(std::fabs(arc.sweep) / step));
        for (int k = 1; k < parts; ++k) out.push_back(segment_point(s, (double)k / parts));
    }
    if (!path.closed && n > 0) out.push_back(path.v.back().p);
    return out;
}
}  // namespace

std::vector<Point> tessellate(const Path& path, double tolerance, size_t max_points) {
    if (path.v.size() < 2) fail("Kontur ma mniej niż 2 punkty.");
    if (!(tolerance > 0)) fail("Tolerancja przybliżenia łuków musi być dodatnia.");
    for (const auto& v : path.v) { check_point(v.p); if (!std::isfinite(v.bulge)) fail("Wybrzuszenie łuku nie jest liczbą."); }
    double tol = tolerance;
    while (true) {
        std::vector<Point> points = tessellate_once(path, tol);
        if (points.size() <= max_points) return points;
        tol *= 1.5;
        if (tol > 20)
            fail(std::format("Po przybliżeniu łuków kontur ma {} punktów, a płyta może mieć najwyżej {}. Uprość kształt (mniej łuków albo mniejsze łuki).", points.size(), max_points));
    }
}

// ------------------------------------------------------------------ kształty

Path polyline(const std::vector<Point>& points, bool closed) {
    Path path;
    path.closed = closed;
    path.kind = closed ? "kontur" : "linia";
    for (const auto& p : points) { check_point(p); path.v.push_back({p, 0}); }
    return path;
}

Path rectangle(const Point& a, const Point& b) {
    check_point(a); check_point(b);
    if (std::fabs(a[0] - b[0]) < .5 || std::fabs(a[1] - b[1]) < .5) fail("Prostokąt ma wymiar mniejszy niż 0,5 mm.");
    double x0 = std::min(a[0], b[0]), x1 = std::max(a[0], b[0]), y0 = std::min(a[1], b[1]), y1 = std::max(a[1], b[1]);
    Path path = polyline({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true);
    path.kind = "prostokąt";
    return path;
}

Path rounded_rectangle(const Point& a, const Point& b, double radius) {
    Path path = rectangle(a, b);
    double w = std::fabs(a[0] - b[0]), h = std::fabs(a[1] - b[1]);
    if (!(radius > 0)) fail("Promień zaokrąglenia musi być dodatni.");
    if (radius > std::min(w, h) / 2 + 1e-9) fail(std::format("Promień {} mm jest większy niż połowa krótszego boku ({} mm).", num(radius), num(std::min(w, h) / 2)));
    double x0 = std::min(a[0], b[0]), x1 = std::max(a[0], b[0]), y0 = std::min(a[1], b[1]), y1 = std::max(a[1], b[1]);
    const double k = std::tan(PI / 8);
    Path out;
    out.closed = true;
    out.kind = "prostokąt zaokrąglony";
    // od lewego dolnego rogu, przeciwnie do ruchu wskazówek: dół, łuk, prawy bok, łuk, góra, łuk, lewy bok, łuk
    out.v = {{{x0 + radius, y0}, 0}, {{x1 - radius, y0}, k}, {{x1, y0 + radius}, 0}, {{x1, y1 - radius}, k},
             {{x1 - radius, y1}, 0}, {{x0 + radius, y1}, k}, {{x0, y1 - radius}, 0}, {{x0, y0 + radius}, k}};
    if (radius >= std::min(w, h) / 2 - 1e-9) {  // pełne zaokrąglenie: odcinki o zerowej długości znikają
        Path clean;
        clean.closed = true;
        clean.kind = out.kind;
        for (size_t i = 0; i < out.v.size(); ++i) {
            const Vertex& cur = out.v[i];
            const Vertex& nxt = out.v[(i + 1) % out.v.size()];
            if (std::fabs(cur.bulge) < 1e-12 && dist(cur.p, nxt.p) < 1e-6) continue;
            clean.v.push_back(cur);
        }
        return clean;
    }
    return out;
}

Path circle(const Point& center, double radius) {
    check_point(center);
    if (!(radius >= .25)) fail("Promień okręgu musi wynosić co najmniej 0,25 mm.");
    Path path;
    path.closed = true;
    path.kind = "okrąg";
    path.v = {{{center[0] - radius, center[1]}, 1}, {{center[0] + radius, center[1]}, 1}};
    return path;
}

Path slot(const Point& a, const Point& b, double width) {
    check_point(a); check_point(b);
    if (!(width >= .5)) fail("Szerokość szczeliny musi wynosić co najmniej 0,5 mm.");
    if (dist(a, b) < 1e-6) fail("Środki końców szczeliny leżą w tym samym miejscu — użyj okręgu.");
    Point d = unit(sub(b, a)), n{-d[1], d[0]};
    double r = width / 2;
    Path path;
    path.closed = true;
    path.kind = "szczelina";
    // a-prawa → b-prawa (linia), półokrąg wokół b, b-lewa → a-lewa (linia), półokrąg wokół a
    path.v = {{add(a, mul(n, -r)), 0}, {add(b, mul(n, -r)), 1}, {add(b, mul(n, r)), 0}, {add(a, mul(n, r)), 1}};
    return path;
}

Path regular_polygon(const Point& center, double radius, int sides, double start_degrees) {
    check_point(center);
    if (sides < 3 || sides > 64) fail("Wielobok foremny ma od 3 do 64 boków.");
    if (!(radius >= .5)) fail("Promień wieloboku musi wynosić co najmniej 0,5 mm.");
    std::vector<Point> pts;
    for (int i = 0; i < sides; ++i) {
        double ang = (start_degrees + 360.0 * i / sides) * PI / 180;
        pts.push_back({center[0] + radius * std::cos(ang), center[1] + radius * std::sin(ang)});
    }
    Path path = polyline(pts, true);
    path.kind = "wielobok";
    return path;
}

// ------------------------------------------------------------------ edycja

namespace {
struct Corner {
    Point vertex, t1, t2;
    double bulge = 0;
};

Corner corner_for(const Path& path, size_t index, double distance_along, bool arc, double radius) {
    const size_t n = path.v.size();
    if (index >= n) fail(std::format("Wierzchołek {} nie istnieje (kontur ma {}).", index + 1, n));
    if (!path.closed && (index == 0 || index + 1 == n)) fail("Koniec otwartej linii nie ma narożnika do zaokrąglenia — wybierz wierzchołek między dwoma odcinkami.");
    size_t prev = (index + n - 1) % n;
    if (std::fabs(path.v[prev].bulge) > 1e-12 || std::fabs(path.v[index].bulge) > 1e-12)
        fail("Ten narożnik styka się z łukiem — zaokrąglić lub ściąć można tylko narożnik między dwoma odcinkami prostymi.");
    Point p = path.v[index].p, a = path.v[prev].p, b = path.v[(index + 1) % n].p;
    Point d1 = unit(sub(a, p)), d2 = unit(sub(b, p));
    double len1 = dist(a, p), len2 = dist(b, p);
    double cosine = std::clamp(dot(d1, d2), -1.0, 1.0);
    double alpha = std::acos(cosine);  // kąt wewnętrzny między bokami
    if (alpha < 1e-3 || alpha > PI - 1e-3) fail("Boki są równoległe (kąt 0° lub 180°) — nie ma narożnika.");
    double t = arc ? radius / std::tan(alpha / 2) : distance_along;
    if (!(t > 1e-6)) fail("Wartość zaokrąglenia lub fazki musi być dodatnia.");
    if (t > len1 - 1e-6 || t > len2 - 1e-6) {
        std::string why = arc ? std::format("Promień {} mm wymaga {} mm na każdym boku, a krótszy bok ma {} mm. Zmniejsz promień.", num(radius), num(t), num(std::min(len1, len2)))
                              : std::format("Fazka {} mm jest dłuższa niż krótszy bok ({} mm).", num(t), num(std::min(len1, len2)));
        fail(why);
    }
    Corner c;
    c.vertex = p;
    c.t1 = add(p, mul(d1, t));
    c.t2 = add(p, mul(d2, t));
    if (arc) {
        double deflection = PI - alpha;
        double turn = cross(mul(d1, -1), d2);
        c.bulge = (turn >= 0 ? 1 : -1) * std::tan(deflection / 4);
    }
    return c;
}

Path replace_corner(const Path& path, size_t index, const Corner& c) {
    Path out;
    out.closed = path.closed;
    out.kind = "kontur";
    for (size_t i = 0; i < path.v.size(); ++i) {
        if (i == index) { out.v.push_back({c.t1, c.bulge}); out.v.push_back({c.t2, 0}); }
        else out.v.push_back(path.v[i]);
    }
    return out;
}
}  // namespace

Path fillet_vertex(const Path& path, size_t index, double radius) {
    if (!(radius > 0) || !std::isfinite(radius)) fail("Promień zaokrąglenia musi być dodatni.");
    return replace_corner(path, index, corner_for(path, index, 0, true, radius));
}

Path chamfer_vertex(const Path& path, size_t index, double distance) {
    if (!(distance > 0) || !std::isfinite(distance)) fail("Odległość fazki musi być dodatnia.");
    return replace_corner(path, index, corner_for(path, index, distance, false, 0));
}

Path move_vertex(const Path& path, size_t index, const Point& to) {
    check_point(to);
    if (index >= path.v.size()) fail(std::format("Wierzchołek {} nie istnieje (kontur ma {}).", index + 1, path.v.size()));
    Path out = path;
    out.v[index].p = to;
    out.kind = out.kind == "linia" ? "linia" : "kontur";
    return out;
}

Path remove_vertex(const Path& path, size_t index) {
    if (index >= path.v.size()) fail(std::format("Wierzchołek {} nie istnieje (kontur ma {}).", index + 1, path.v.size()));
    if (path.closed && path.v.size() <= 3) fail("Kontur zamknięty musi mieć co najmniej 3 wierzchołki.");
    if (!path.closed && path.v.size() <= 2) fail("Linia musi mieć co najmniej 2 punkty.");
    Path out = path;
    out.v.erase(out.v.begin() + (long)index);
    size_t prev = (index + out.v.size() - 1) % out.v.size();
    if (!(index == 0 && !out.closed)) out.v[prev].bulge = 0;  // odcinek łączący sąsiadów jest prosty
    out.kind = "kontur";
    return out;
}

Path set_segment_length(const Path& path, size_t index, double length) {
    if (!(length >= .1) || !std::isfinite(length)) fail("Długość odcinka musi wynosić co najmniej 0,1 mm.");
    Segment s = segment(path, index);
    if (std::fabs(s.bulge) > 1e-12) fail("Długość można ustawić tylko odcinkowi prostemu; łukowi zmień promień.");
    Point d = unit(sub(s.b, s.a));
    return move_vertex(path, (index + 1) % path.v.size(), add(s.a, mul(d, length)));
}

Path translate(const Path& path, const Point& delta) {
    check_point(delta);
    Path out = path;
    for (auto& v : out.v) v.p = add(v.p, delta);
    return out;
}

Path close_path(const Path& path) {
    if (path.closed) return path;
    if (path.v.size() < 3) fail("Aby zamknąć kontur, potrzebne są co najmniej 3 punkty.");
    Path out = path;
    out.closed = true;
    out.kind = "kontur";
    if (dist(out.v.front().p, out.v.back().p) < 1e-6) {
        out.v.pop_back();
        if (out.v.size() < 3) fail("Aby zamknąć kontur, potrzebne są co najmniej 3 różne punkty.");
    }
    return out;
}

// ------------------------------------------------------------------ pomiary

std::array<Point, 2> bounds(const Path& path) {
    if (path.v.empty()) fail("Pusty kontur nie ma obrysu.");
    Point lo{1e18, 1e18}, hi{-1e18, -1e18};
    auto take = [&](const Point& p) { lo = {std::min(lo[0], p[0]), std::min(lo[1], p[1])}; hi = {std::max(hi[0], p[0]), std::max(hi[1], p[1])}; };
    for (const auto& v : path.v) take(v.p);
    for (size_t i = 0; i < segment_count(path); ++i) {
        Segment s = segment(path, i);
        if (std::fabs(s.bulge) < 1e-12) continue;
        Arc arc = arc_of(s.a, s.b, s.bulge);
        for (int q = 0; q < 4; ++q) {  // skrajne punkty okręgu leżące na łuku
            double angle = q * PI / 2;
            double rel = arc.sweep >= 0 ? norm_positive(angle - arc.start) : norm_positive(arc.start - angle);
            if (rel <= std::fabs(arc.sweep)) take({arc.center[0] + arc.radius * std::cos(angle), arc.center[1] + arc.radius * std::sin(angle)});
        }
    }
    return {lo, hi};
}

double area(const Path& path) {
    if (!path.closed) return 0;
    return std::fabs(sketch_shapes::signed_area(tessellate(path, .05, 4096)));
}

double distance_to_path(const Path& path, const Point& p) {
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < segment_count(path); ++i) best = std::min(best, distance_to_segment(segment(path, i), p));
    if (path.v.size() == 1) best = dist(path.v[0].p, p);
    return best;
}

std::optional<size_t> nearest_vertex(const Path& path, const Point& p, double tolerance) {
    std::optional<size_t> best;
    double best_d = tolerance;
    for (size_t i = 0; i < path.v.size(); ++i) {
        double d = dist(path.v[i].p, p);
        if (d <= best_d) { best = i; best_d = d; }
    }
    return best;
}

std::optional<size_t> nearest_segment(const Path& path, const Point& p, double tolerance) {
    std::optional<size_t> best;
    double best_d = tolerance;
    for (size_t i = 0; i < segment_count(path); ++i) {
        double d = distance_to_segment(segment(path, i), p);
        if (d <= best_d) { best = i; best_d = d; }
    }
    return best;
}

std::optional<CircleInfo> as_circle(const Path& path) {
    if (!path.closed || path.kind != "okrąg" || path.v.size() != 2) return std::nullopt;
    if (std::fabs(path.v[0].bulge - 1) > 1e-9 || std::fabs(path.v[1].bulge - 1) > 1e-9) return std::nullopt;
    return CircleInfo{mul(add(path.v[0].p, path.v[1].p), .5), dist(path.v[0].p, path.v[1].p) / 2};
}

std::optional<std::array<Point, 2>> as_rectangle(const Path& path) {
    if (!path.closed || path.v.size() != 4) return std::nullopt;
    for (const auto& v : path.v) if (std::fabs(v.bulge) > 1e-12) return std::nullopt;
    auto b = bounds(path);
    for (const auto& v : path.v) {
        bool on_x = std::fabs(v.p[0] - b[0][0]) < 1e-6 || std::fabs(v.p[0] - b[1][0]) < 1e-6;
        bool on_y = std::fabs(v.p[1] - b[0][1]) < 1e-6 || std::fabs(v.p[1] - b[1][1]) < 1e-6;
        if (!on_x || !on_y) return std::nullopt;
    }
    return b;
}

std::optional<SlotInfo> as_slot(const Path& path) {
    if (!path.closed || path.kind != "szczelina" || path.v.size() != 4) return std::nullopt;
    if (std::fabs(path.v[1].bulge - 1) > 1e-9 || std::fabs(path.v[3].bulge - 1) > 1e-9 || std::fabs(path.v[0].bulge) > 1e-12 || std::fabs(path.v[2].bulge) > 1e-12) return std::nullopt;
    Point a_right = path.v[0].p, b_right = path.v[1].p, b_left = path.v[2].p, a_left = path.v[3].p;
    return SlotInfo{mul(add(a_right, a_left), .5), mul(add(b_right, b_left), .5), dist(a_right, a_left)};
}

std::string describe(const Path& path) {
    if (path.v.empty()) return "pusty";
    size_t arcs = 0;
    for (const auto& v : path.v) if (std::fabs(v.bulge) > 1e-12) ++arcs;
    if (auto c = as_circle(path)) return std::format("okrąg Ø{} mm", num(c->radius * 2));
    if (auto r = as_rectangle(path)) return std::format("prostokąt {} × {} mm", num((*r)[1][0] - (*r)[0][0]), num((*r)[1][1] - (*r)[0][1]));
    if (auto s = as_slot(path)) return std::format("szczelina {} × {} mm", num(dist(s->a, s->b) + s->width), num(s->width));
    auto b = path.v.size() >= 2 ? bounds(path) : std::array<Point, 2>{path.v[0].p, path.v[0].p};
    std::string text = std::format("{} {} × {} mm, {} wierzch.", path.kind, num(b[1][0] - b[0][0]), num(b[1][1] - b[0][1]), path.v.size());
    if (arcs > 0) text += std::format(", {} łuków", arcs);
    if (!path.closed) text += ", otwarta";
    return text;
}

// ------------------------------------------------------------------ przyciąganie

std::vector<Snap> snap_points(const Path& path) {
    std::vector<Snap> out;
    if (auto c = as_circle(path)) {
        out.push_back({c->center, "środek okręgu"});
        out.push_back({{c->center[0] + c->radius, c->center[1]}, "kwadrant"});
        out.push_back({{c->center[0] - c->radius, c->center[1]}, "kwadrant"});
        out.push_back({{c->center[0], c->center[1] + c->radius}, "kwadrant"});
        out.push_back({{c->center[0], c->center[1] - c->radius}, "kwadrant"});
        return out;
    }
    for (const auto& v : path.v) out.push_back({v.p, "koniec"});
    for (size_t i = 0; i < segment_count(path); ++i) {
        Segment s = segment(path, i);
        out.push_back({segment_point(s, .5), std::fabs(s.bulge) < 1e-12 ? "środek" : "środek łuku"});
        if (std::fabs(s.bulge) > 1e-12) out.push_back({arc_of(s.a, s.b, s.bulge).center, "środek łuku (oś)"});
    }
    return out;
}

std::optional<Snap> nearest_snap(const std::vector<Path>& paths, const Point& cursor, double tolerance) {
    std::optional<Snap> best;
    double best_d = tolerance;
    for (const auto& path : paths)
        for (const auto& s : snap_points(path)) {
            double d = dist(s.p, cursor);
            if (d <= best_d) { best = s; best_d = d; }
        }
    return best;
}

Point snap_to_grid(const Point& p, double step) {
    if (!(step > 0)) return p;
    return {std::round(p[0] / step) * step, std::round(p[1] / step) * step};
}

Point snap_to_axis(const Point& from, const Point& to, double degrees) {
    Point d = sub(to, from);
    double l = std::hypot(d[0], d[1]);
    if (l < 1e-9) return to;
    double angle = std::atan2(d[1], d[0]) * 180 / PI;
    for (double axis : {0.0, 90.0, 180.0, -90.0, -180.0}) {
        if (std::fabs(angle - axis) <= degrees) {
            double r = axis * PI / 180;
            return {from[0] + l * std::round(std::cos(r)), from[1] + l * std::round(std::sin(r))};
        }
    }
    return to;
}

// ------------------------------------------------------------------ sprawdzanie

void validate_profile(const Path& path, const std::string& name) {
    if (!path.closed) fail(name + " nie jest zamknięty — zamknij go (klawisz C albo kliknij pierwszy punkt).");
    if (path.v.size() < 2) fail(name + " ma za mało punktów.");
    std::vector<Point> pts = tessellate(path, .2, MAX_CONTOUR_POINTS);
    if (pts.size() < 3) fail(name + " ma mniej niż 3 punkty.");
    if (sketch_shapes::self_intersects(pts)) fail(name + " przecina sam siebie — popraw punkty, aby boki się nie krzyżowały.");
    if (area(path) < 1) fail(name + " ma pole mniejsze niż 1 mm².");
}

bool polygon_contains(const std::vector<Point>& outer, const std::vector<Point>& inner) {
    if (outer.size() < 3 || inner.size() < 3) return false;
    for (const auto& p : inner) if (!point_in_polygon(p, outer)) return false;
    for (size_t i = 0; i < inner.size(); ++i)
        for (size_t j = 0; j < outer.size(); ++j)
            if (segments_cross(inner[i], inner[(i + 1) % inner.size()], outer[j], outer[(j + 1) % outer.size()])) return false;
    return true;
}

std::vector<Group> group_shapes(const std::vector<Path>& shapes) {
    const size_t n = shapes.size();
    std::vector<std::vector<Point>> poly(n);
    for (size_t i = 0; i < n; ++i) {
        validate_profile(shapes[i], std::format("Kształt {} ({})", i + 1, describe(shapes[i])));
        poly[i] = tessellate(shapes[i], .2, 4096);
    }
    std::vector<std::vector<bool>> inside(n, std::vector<bool>(n, false));  // inside[i][j]: kształt i leży w j
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (polygon_contains(poly[j], poly[i])) { inside[i][j] = true; continue; }
            // przecinanie się konturów (żaden nie zawiera drugiego)
            if (i < j && !polygon_contains(poly[i], poly[j])) {
                for (size_t a = 0; a < poly[i].size(); ++a)
                    for (size_t b = 0; b < poly[j].size(); ++b)
                        if (segments_cross(poly[i][a], poly[i][(a + 1) % poly[i].size()], poly[j][b], poly[j][(b + 1) % poly[j].size()]))
                            fail(std::format("Kształty {} ({}) i {} ({}) przecinają się. Rozdziel je albo narysuj jeden kontur.", i + 1, describe(shapes[i]), j + 1, describe(shapes[j])));
            }
        }
    std::vector<Group> groups;
    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) if (inside[i][j]) ++depth[i];
    for (size_t i = 0; i < n; ++i) {
        if (depth[i] > 1) fail(std::format("Kształt {} ({}) leży w otworze innego kształtu (zagnieżdżenie na dwóch poziomach) — narysuj go osobno poza płytą.", i + 1, describe(shapes[i])));
        if (depth[i] == 0) groups.push_back({i, {}});
    }
    for (size_t i = 0; i < n; ++i) {
        if (depth[i] != 1) continue;
        for (auto& g : groups) if (inside[i][g.outer]) g.inner.push_back(i);
    }
    return groups;
}

Contour to_contour(const Path& path, double tolerance) {
    std::vector<Point> pts = tessellate(path, tolerance);
    Contour out;
    Point lo{1e18, 1e18}, hi{-1e18, -1e18};
    for (const auto& p : pts) { lo = {std::min(lo[0], p[0]), std::min(lo[1], p[1])}; hi = {std::max(hi[0], p[0]), std::max(hi[1], p[1])}; }
    out.offset = lo;
    for (const auto& p : pts) out.points.push_back({p[0] - lo[0], p[1] - lo[1]});
    out.u = hi[0] - lo[0];
    out.v = hi[1] - lo[1];
    return out;
}


// ------------------------------------------------------------------ rysowanie odręczne

namespace {

struct CircleFit { Point c{0, 0}; double r = 0; bool ok = false; };

// Okrąg najmniejszych kwadratów (Kåsa) na współrzędnych wyśrodkowanych.
CircleFit fit_circle(const std::vector<Point>& p, size_t first, size_t last) {
    CircleFit out;
    const size_t n = last - first + 1;
    if (n < 3) return out;
    Point mean{0, 0};
    for (size_t i = first; i <= last; ++i) { mean[0] += p[i][0]; mean[1] += p[i][1]; }
    mean[0] /= (double)n; mean[1] /= (double)n;
    double suu = 0, suv = 0, svv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (size_t i = first; i <= last; ++i) {
        double u = p[i][0] - mean[0], v = p[i][1] - mean[1];
        suu += u * u; suv += u * v; svv += v * v; suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
    }
    double det = suu * svv - suv * suv;
    if (std::fabs(det) < 1e-9 * std::max(1.0, suu * svv)) return out;
    double b1 = 0.5 * (suuu + suvv), b2 = 0.5 * (svvv + svuu);
    double uc = (b1 * svv - b2 * suv) / det, vc = (b2 * suu - b1 * suv) / det;
    out.c = {mean[0] + uc, mean[1] + vc};
    out.r = std::sqrt(uc * uc + vc * vc + (suu + svv) / (double)n);
    out.ok = std::isfinite(out.r) && std::isfinite(out.c[0]) && std::isfinite(out.c[1]);
    return out;
}

double chord_distance(const Point& a, const Point& b, const Point& p) {
    double l = dist(a, b);
    if (l < 1e-12) return dist(a, p);
    return std::fabs(cross(sub(b, a), sub(p, a))) / l;
}

// Dopasowuje p[i..j] do jednego odcinka, jednego łuku albo dzieli w miejscu największej odchyłki. Dopisuje wierzchołki od i do j-1 (j dopisuje wywołujący).
void fit_range(const std::vector<Point>& p, size_t i, size_t j, double tol, std::vector<Vertex>& out, size_t& lines, size_t& arcs, int depth) {
    double worst = 0;
    size_t split = i + 1;
    for (size_t k = i + 1; k < j; ++k) {
        double d = chord_distance(p[i], p[j], p[k]);
        if (d > worst) { worst = d; split = k; }
    }
    if (j - i < 2 || worst <= tol) { out.push_back({p[i], 0}); ++lines; return; }
    if (j - i >= 4 && depth < 24) {
        CircleFit disc = fit_circle(p, i, j);
        if (disc.ok && disc.r >= 2 * tol && disc.r < 1e5 && dist(p[i], p[j]) > tol) {
            double deviation = 0;
            for (size_t k = i; k <= j; ++k) deviation = std::max(deviation, std::fabs(dist(p[k], disc.c) - disc.r));
            if (deviation <= tol) {
                // kierunek obiegu z sumy iloczynów wektorowych kolejnych promieni
                double turn = 0;
                for (size_t k = i; k < j; ++k) turn += cross(sub(p[k], disc.c), sub(p[k + 1], disc.c));
                double pi_ = 0;
                try {
                    pi_ = bulge_center(disc.c, p[i], p[j], turn > 0);
                } catch (const std::invalid_argument&) {
                    pi_ = 0;
                }
                // łuk do 300° i zgodny z kolejnością punktów (środkowy punkt po tej samej stronie cięciwy co łuk)
                if (pi_ != 0 && 4 * std::atan(std::fabs(pi_)) <= 300 * PI / 180) {
                    out.push_back({p[i], pi_}); ++arcs;
                    return;
                }
            }
        }
    }
    if (depth >= 40) { out.push_back({p[i], 0}); ++lines; return; }
    fit_range(p, i, split, tol, out, lines, arcs, depth + 1);
    fit_range(p, split, j, tol, out, lines, arcs, depth + 1);
}

}  // namespace

StrokeFit fit_stroke(const std::vector<Point>& stroke, double tolerance) {
    if (!(tolerance > 1e-6)) fail("Tolerancja dopasowania musi być dodatnia.");
    std::vector<Point> p;
    for (const Point& q : stroke) {
        check_point(q);
        if (p.empty() || dist(q, p.back()) >= tolerance * 0.3) p.push_back(q);
    }
    double length = 0;
    for (size_t i = 1; i < p.size(); ++i) length += dist(p[i - 1], p[i]);
    if (p.size() < 2 || length < 3 * tolerance) fail("Pociągnięcie jest zbyt krótkie — przeciągnij myszą dalej, trzymając lewy przycisk.");
    // zamknięte: koniec wraca w pobliże początku
    const double gap = dist(p.front(), p.back());
    const bool closed = p.size() >= 8 && gap < std::max(4 * tolerance, 0.1 * length);
    if (closed) { p.back() = p.front(); }
    StrokeFit fit;
    if (closed) {
        std::vector<Point> body(p.begin(), p.end() - 1);
        // okrąg
        CircleFit disc = fit_circle(body, 0, body.size() - 1);
        if (disc.ok && disc.r >= 0.25) {
            double deviation = 0;
            for (const Point& q : body) deviation = std::max(deviation, std::fabs(dist(q, disc.c) - disc.r));
            // pokrycie kątowe: wszystkie ćwiartki odwiedzone
            bool quadrant[4] = {false, false, false, false};
            for (const Point& q : body) {
                double a = std::atan2(q[1] - disc.c[1], q[0] - disc.c[0]);
                quadrant[(int)std::floor((a + PI) / (PI / 2)) % 4] = true;
            }
            if (deviation <= std::max(1.5 * tolerance, 0.06 * disc.r) && quadrant[0] && quadrant[1] && quadrant[2] && quadrant[3]) {
                fit.path = circle(disc.c, disc.r);
                fit.description = std::format("okrąg R {} mm", num(disc.r));
                fit.arcs = 2;
                return fit;
            }
        }
        // prostokąt równoległy do osi: punkty leżą przy obwodzie ramki, każdy bok odwiedzony
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (const Point& q : body) { x0 = std::min(x0, q[0]); y0 = std::min(y0, q[1]); x1 = std::max(x1, q[0]); y1 = std::max(y1, q[1]); }
        const double w = x1 - x0, h = y1 - y0;
        if (w > 4 * tolerance && h > 4 * tolerance) {
            const double edge_tol = std::max(1.5 * tolerance, 0.05 * std::min(w, h));
            bool left = false, right = false, bottom = false, top = false, fits = true;
            for (const Point& q : body) {
                double d = std::min({q[0] - x0, x1 - q[0], q[1] - y0, y1 - q[1]});
                if (d > edge_tol) { fits = false; break; }
                left |= q[0] - x0 <= edge_tol; right |= x1 - q[0] <= edge_tol; bottom |= q[1] - y0 <= edge_tol; top |= y1 - q[1] <= edge_tol;
            }
            if (fits && left && right && bottom && top) {
                fit.path = rectangle({x0, y0}, {x1, y1});
                fit.description = std::format("prostokąt {} × {} mm", num(w), num(h));
                fit.lines = 4;
                return fit;
            }
        }
        // kontur zamknięty: dzielimy na dwie połowy w najdalszym od początku punkcie, żeby wierzchołek startowy nie był sztucznym narożnikiem
        size_t far = 1;
        double far_d = 0;
        for (size_t k = 1; k < body.size(); ++k) if (dist(body[k], body[0]) > far_d) { far_d = dist(body[k], body[0]); far = k; }
        std::vector<Vertex> verts;
        size_t lines = 0, arcs = 0;
        fit_range(p, 0, far, tolerance, verts, lines, arcs, 0);
        fit_range(p, far, p.size() - 1, tolerance, verts, lines, arcs, 0);
        fit.path.v = verts;
        fit.path.closed = true;
        fit.path.kind = "kontur";
        fit.lines = lines; fit.arcs = arcs;
        if (fit.path.v.size() < 3 && arcs == 0) fail("Zamknięty kształt z dwóch punktów nie istnieje — narysuj większy obrys.");
        fit.description = std::format("kontur zamknięty: {} odc. + {} łuk.", lines, arcs);
        return fit;
    }
    std::vector<Vertex> verts;
    size_t lines = 0, arcs = 0;
    fit_range(p, 0, p.size() - 1, tolerance, verts, lines, arcs, 0);
    verts.push_back({p.back(), 0});
    fit.path.v = verts;
    fit.path.closed = false;
    fit.path.kind = "linia";
    fit.lines = lines; fit.arcs = arcs;
    if (verts.size() == 2 && verts[0].bulge == 0) {
        // pojedynczy prosty odcinek: wyrównanie do osi, gdy odchyłka jest mała
        verts[1].p = snap_to_axis(verts[0].p, verts[1].p, 4);
        fit.path.v = verts;
    }
    fit.description = std::format("{} odc. + {} łuk.", lines, arcs);
    return fit;
}

}  // namespace cad2d
