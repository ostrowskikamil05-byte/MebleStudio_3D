#include "dxf_import.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <stdexcept>

#include "sketch_shapes.h"

namespace dxf_import {

namespace {

constexpr double PI = std::numbers::pi;
constexpr size_t MAX_ENTITIES = 50000;

using Pairs = std::vector<std::pair<int, std::string>>;
struct Entity { std::string type; Pairs data; };

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

double number(const std::string& s) {
    char* end = nullptr;
    std::string t = trim(s);
    double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || !std::isfinite(v)) throw std::invalid_argument("Nieczytelna liczba w pliku DXF: „" + t + "”.");
    return v;
}

double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

// Łuk z wybrzuszeniem (bulge) między p1 a p2 — punkty wewnętrzne i końcowy (bez p1).
void bulge_points(const Point& p1, const Point& p2, double bulge, std::vector<Point>& out) {
    double chord = dist(p1, p2);
    if (std::abs(bulge) < 1e-9 || chord < 1e-9) { out.push_back(p2); return; }
    double theta = 4 * std::atan(bulge);
    double apothem = (chord / 2) * ((1 - bulge * bulge) / (2 * bulge));
    Point mid{(p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2};
    Point normal{-(p2[1] - p1[1]) / chord, (p2[0] - p1[0]) / chord};
    Point center{mid[0] + normal[0] * apothem, mid[1] + normal[1] * apothem};
    double radius = dist(center, p1);
    double start = std::atan2(p1[1] - center[1], p1[0] - center[0]);
    int segments = std::max(2, (int)std::ceil(std::abs(theta) / (PI / 2) * 6));
    for (int i = 1; i < segments; ++i) {
        double angle = start + theta * i / segments;
        out.push_back({center[0] + radius * std::cos(angle), center[1] + radius * std::sin(angle)});
    }
    out.push_back(p2);
}

struct Path { std::vector<Point> points; bool closed = false; };

double polygon_area(const std::vector<Point>& pts) { return std::abs(sketch_shapes::signed_area(pts)); }

// Łączenie rozłącznych odcinków/łuków w łamane (końce w odległości ≤ tol).
std::vector<Path> chain(std::vector<Path> open, double tol) {
    std::vector<Path> done;
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < open.size() && !merged; ++i) {
            for (size_t j = i + 1; j < open.size() && !merged; ++j) {
                auto& a = open[i].points;
                auto& b = open[j].points;
                std::vector<Point> joined;
                if (dist(a.back(), b.front()) <= tol) { joined = a; joined.insert(joined.end(), b.begin() + 1, b.end()); }
                else if (dist(a.back(), b.back()) <= tol) { joined = a; joined.insert(joined.end(), b.rbegin() + 1, b.rend()); }
                else if (dist(a.front(), b.back()) <= tol) { joined = b; joined.insert(joined.end(), a.begin() + 1, a.end()); }
                else if (dist(a.front(), b.front()) <= tol) { joined.assign(a.rbegin(), a.rend()); joined.insert(joined.end(), b.begin() + 1, b.end()); }
                else continue;
                open[i].points = std::move(joined);
                open.erase(open.begin() + (long)j);
                merged = true;
            }
        }
    }
    for (auto& p : open) {
        if (p.points.size() >= 4 && dist(p.points.front(), p.points.back()) <= tol) { p.points.pop_back(); p.closed = true; }
        done.push_back(std::move(p));
    }
    return done;
}

double unit_scale_for(int code, std::string& name) {
    switch (code) {
        case 1: name = "cale"; return 25.4;
        case 2: name = "stopy"; return 304.8;
        case 4: name = "mm"; return 1;
        case 5: name = "cm"; return 10;
        case 6: name = "m"; return 1000;
        default: name = "mm"; return 1;
    }
}

}  // namespace

Result parse(const std::string& text) {
    if (text.rfind("AutoCAD Binary DXF", 0) == 0) throw std::invalid_argument("To jest binarny plik DXF. Zapisz rysunek jako tekstowy DXF (ASCII).");
    Pairs pairs;
    {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            lines.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
        if (lines.size() < 2) throw std::invalid_argument("Plik DXF jest pusty.");
        if (lines.size() % 2 != 0) throw std::invalid_argument("Uszkodzony plik DXF: nieparzysta liczba linii (kody grup i wartości muszą występować parami).");
        for (size_t i = 0; i < lines.size(); i += 2) {
            std::string code = trim(lines[i]);
            char* end = nullptr;
            long value = std::strtol(code.c_str(), &end, 10);
            if (code.empty() || *end != '\0') throw std::invalid_argument("Uszkodzony plik DXF: w linii " + std::to_string(i + 1) + " oczekiwano kodu grupy, jest „" + code + "”.");
            pairs.push_back({(int)value, trim(lines[i + 1])});
        }
    }

    Result result;
    std::vector<Entity> entities;
    std::string section;
    int insunits = 0;
    for (size_t i = 0; i < pairs.size(); ++i) {
        const auto& [code, value] = pairs[i];
        if (code == 0 && value == "SECTION" && i + 1 < pairs.size() && pairs[i + 1].first == 2) { section = pairs[i + 1].second; ++i; continue; }
        if (code == 0 && value == "ENDSEC") { section.clear(); continue; }
        if (section == "HEADER" && code == 9 && value == "$INSUNITS" && i + 1 < pairs.size()) insunits = (int)number(pairs[i + 1].second);
        if (section == "ENTITIES") {
            if (code == 0) entities.push_back({value, {}});
            else if (!entities.empty()) entities.back().data.push_back({code, value});
        }
    }
    result.unit_scale = unit_scale_for(insunits, result.unit_name);
    if (entities.size() > MAX_ENTITIES) throw std::invalid_argument("Plik DXF ma ponad " + std::to_string(MAX_ENTITIES) + " obiektów — zbyt duży na szkic elementu.");

    std::vector<Path> open;
    std::vector<std::pair<std::string, size_t>> skipped_types;
    auto skip = [&](const std::string& type) {
        ++result.skipped;
        auto it = std::find_if(skipped_types.begin(), skipped_types.end(), [&](const auto& e) { return e.first == type; });
        if (it == skipped_types.end()) skipped_types.push_back({type, 1}); else ++it->second;
    };
    auto read_double = [](const Pairs& d, int code, double fallback) {
        for (const auto& [c, v] : d) if (c == code) return number(v);
        return fallback;
    };

    for (size_t i = 0; i < entities.size(); ++i) {
        const Entity& e = entities[i];
        if (e.type == "LINE") {
            Point a{read_double(e.data, 10, 0), read_double(e.data, 20, 0)}, b{read_double(e.data, 11, 0), read_double(e.data, 21, 0)};
            ++result.entities;
            if (dist(a, b) > 1e-9) open.push_back({{a, b}, false});
        } else if (e.type == "CIRCLE") {
            ++result.entities;
            Contour c;
            c.center = {read_double(e.data, 10, 0), read_double(e.data, 20, 0)};
            c.radius = read_double(e.data, 40, 0);
            if (c.radius <= 0) { result.warnings.push_back("Okrąg o zerowym promieniu został pominięty."); continue; }
            c.is_circle = true; c.closed = true;
            int segments = c.radius * result.unit_scale < 5 ? 24 : 48;
            for (int k = 0; k < segments; ++k) c.points.push_back({c.center[0] + c.radius * std::cos(2 * PI * k / segments), c.center[1] + c.radius * std::sin(2 * PI * k / segments)});
            c.area = polygon_area(c.points);
            result.contours.push_back(std::move(c));
        } else if (e.type == "ARC") {
            ++result.entities;
            Point center{read_double(e.data, 10, 0), read_double(e.data, 20, 0)};
            double radius = read_double(e.data, 40, 0), a0 = read_double(e.data, 50, 0), a1 = read_double(e.data, 51, 0);
            if (radius <= 0) { result.warnings.push_back("Łuk o zerowym promieniu został pominięty."); continue; }
            double sweep = a1 - a0;
            while (sweep <= 0) sweep += 360;
            int segments = std::max(2, (int)std::ceil(sweep / 90.0 * 8));
            Path p;
            for (int k = 0; k <= segments; ++k) {
                double angle = (a0 + sweep * k / segments) * PI / 180;
                p.points.push_back({center[0] + radius * std::cos(angle), center[1] + radius * std::sin(angle)});
            }
            open.push_back(std::move(p));
        } else if (e.type == "LWPOLYLINE") {
            ++result.entities;
            struct V { double x = 0, y = 0, bulge = 0; };
            std::vector<V> vs;
            int flags = 0;
            for (const auto& [c, v] : e.data) {
                if (c == 70) flags = (int)number(v);
                else if (c == 10) vs.push_back({number(v), 0, 0});
                else if (c == 20 && !vs.empty()) vs.back().y = number(v);
                else if (c == 42 && !vs.empty()) vs.back().bulge = number(v);
            }
            if (vs.size() < 2) { result.warnings.push_back("Polilinia z mniej niż 2 wierzchołkami została pominięta."); continue; }
            bool closed = flags & 1;
            Path p;
            p.points.push_back({vs[0].x, vs[0].y});
            size_t segs = closed ? vs.size() : vs.size() - 1;
            for (size_t k = 0; k < segs; ++k) {
                const V& a = vs[k];
                const V& b = vs[(k + 1) % vs.size()];
                bulge_points({a.x, a.y}, {b.x, b.y}, a.bulge, p.points);
            }
            if (closed && p.points.size() > 1 && dist(p.points.front(), p.points.back()) < 1e-9) p.points.pop_back();
            p.closed = closed;
            if (closed) {
                Contour c; c.points = p.points; c.closed = true; c.area = polygon_area(c.points);
                result.contours.push_back(std::move(c));
            } else open.push_back(std::move(p));
        } else if (e.type == "POLYLINE") {
            ++result.entities;
            int flags = 0;
            for (const auto& [c, v] : e.data) if (c == 70) flags = (int)number(v);
            struct V { double x = 0, y = 0, bulge = 0; };
            std::vector<V> vs;
            size_t k = i + 1;
            for (; k < entities.size() && entities[k].type == "VERTEX"; ++k)
                vs.push_back({read_double(entities[k].data, 10, 0), read_double(entities[k].data, 20, 0), read_double(entities[k].data, 42, 0)});
            if (k < entities.size() && entities[k].type == "SEQEND") ++k;
            i = k - 1;
            if (flags & (8 | 16 | 64)) { skip("POLYLINE 3D/siatka"); continue; }
            if (vs.size() < 2) { result.warnings.push_back("Polilinia z mniej niż 2 wierzchołkami została pominięta."); continue; }
            bool closed = flags & 1;
            Path p;
            p.points.push_back({vs[0].x, vs[0].y});
            size_t segs = closed ? vs.size() : vs.size() - 1;
            for (size_t m = 0; m < segs; ++m) {
                const V& a = vs[m];
                const V& b = vs[(m + 1) % vs.size()];
                bulge_points({a.x, a.y}, {b.x, b.y}, a.bulge, p.points);
            }
            if (closed && p.points.size() > 1 && dist(p.points.front(), p.points.back()) < 1e-9) p.points.pop_back();
            p.closed = closed;
            if (closed) {
                Contour c; c.points = p.points; c.closed = true; c.area = polygon_area(c.points);
                result.contours.push_back(std::move(c));
            } else open.push_back(std::move(p));
        } else if (e.type == "VERTEX" || e.type == "SEQEND") {
            continue;
        } else {
            skip(e.type);
        }
    }

    // łączenie otwartych odcinków (tolerancja 0,02 mm w jednostkach rysunku)
    for (auto& path : chain(std::move(open), 0.02 / std::max(result.unit_scale, 1e-9))) {
        Contour c;
        c.points = std::move(path.points);
        c.closed = path.closed;
        if (c.closed) c.area = polygon_area(c.points);
        result.contours.push_back(std::move(c));
    }

    if (result.contours.empty()) throw std::invalid_argument("Plik DXF nie zawiera obsługiwanych obiektów (LINE, LWPOLYLINE, POLYLINE, ARC, CIRCLE).");
    for (auto& c : result.contours) {
        for (auto& p : c.points) p = {p[0] * result.unit_scale, p[1] * result.unit_scale};
        c.center = {c.center[0] * result.unit_scale, c.center[1] * result.unit_scale};
        c.radius *= result.unit_scale;
        c.area *= result.unit_scale * result.unit_scale;
    }
    for (const auto& [type, count] : skipped_types) result.warnings.push_back("Pominięto " + std::to_string(count) + " × " + type + " (niewspierany typ obiektu).");
    return result;
}

Outline pick_outline(const Result& result, size_t max_points) {
    const Contour* best = nullptr;
    size_t closed_count = 0;
    Outline out;
    for (const auto& c : result.contours) {
        if (!c.closed) { ++out.open_paths; continue; }
        ++closed_count;
        if (!best || c.area > best->area) best = &c;
    }
    if (!best) throw std::invalid_argument("W pliku DXF nie ma zamkniętego konturu (" + std::to_string(out.open_paths) + " niezamkniętych linii). Domknij kontur w programie CAD.");
    auto inside = [&](const Contour& c) {
        // punkt na kontur wewnątrz obrysu (parzystość przecięć)
        const Point& q = c.points.front();
        bool in = false;
        const auto& poly = best->points;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            if (((poly[i][1] > q[1]) != (poly[j][1] > q[1])) && (q[0] < (poly[j][0] - poly[i][0]) * (q[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0])) in = !in;
        return in;
    };
    for (const auto& c : result.contours) {
        if (!c.closed || &c == best) continue;
        if (inside(c)) { ++out.inner_contours; if (c.is_circle) ++out.inner_circles; }
        else ++out.outer_candidates;
    }
    std::vector<Point> pts = best->points;
    out.original_points = pts.size();
    if (sketch_shapes::signed_area(pts) < 0) std::reverse(pts.begin(), pts.end());
    if (pts.size() > max_points) { pts = sketch_shapes::simplify(pts, max_points); out.simplified = true; }
    double xmin = pts[0][0], ymin = pts[0][1], xmax = xmin, ymax = ymin;
    for (const auto& p : pts) { xmin = std::min(xmin, p[0]); ymin = std::min(ymin, p[1]); xmax = std::max(xmax, p[0]); ymax = std::max(ymax, p[1]); }
    for (auto& p : pts) p = {p[0] - xmin, p[1] - ymin};
    out.points = std::move(pts);
    out.width = xmax - xmin;
    out.height = ymax - ymin;
    return out;
}

}  // namespace dxf_import
