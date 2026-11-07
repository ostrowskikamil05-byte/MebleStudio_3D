#include "sketch_shapes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <stdexcept>

namespace sketch_shapes {

namespace {

constexpr double PI = std::numbers::pi;
constexpr double EPS = 1e-9;

double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }
double cross(const Point& a, const Point& b) { return a[0] * b[1] - a[1] * b[0]; }
Point sub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1]}; }

void require_finite(double value, const char* what) {
    if (!std::isfinite(value)) throw std::invalid_argument(std::string(what) + " musi być liczbą skończoną.");
}

void limit_points(const std::vector<Point>& points) {
    if (points.size() > MAX_POINTS) throw std::invalid_argument("Kontur przekracza limit " + std::to_string(MAX_POINTS) + " punktów.");
}

// Odległość punktu od odcinka (przy zerowej długości — od punktu).
double segment_distance(const Point& p, const Point& a, const Point& b) {
    Point d = sub(b, a);
    double len2 = d[0] * d[0] + d[1] * d[1];
    if (len2 < EPS) return dist(p, a);
    double t = std::clamp(((p[0] - a[0]) * d[0] + (p[1] - a[1]) * d[1]) / len2, 0.0, 1.0);
    return dist(p, {a[0] + d[0] * t, a[1] + d[1] * t});
}

void simplify_range(const std::vector<Point>& pts, size_t first, size_t last, double tolerance, std::vector<bool>& keep) {
    if (last <= first + 1) return;
    double worst = -1;
    size_t index = first;
    for (size_t i = first + 1; i < last; ++i) {
        double d = segment_distance(pts[i], pts[first], pts[last]);
        if (d > worst) { worst = d; index = i; }
    }
    if (worst > tolerance) {
        keep[index] = true;
        simplify_range(pts, first, index, tolerance, keep);
        simplify_range(pts, index, last, tolerance, keep);
    }
}

bool segments_cross(const Point& a, const Point& b, const Point& c, const Point& d) {
    double d1 = cross(sub(b, a), sub(c, a)), d2 = cross(sub(b, a), sub(d, a));
    double d3 = cross(sub(d, c), sub(a, c)), d4 = cross(sub(d, c), sub(b, c));
    return ((d1 > EPS && d2 < -EPS) || (d1 < -EPS && d2 > EPS)) && ((d3 > EPS && d4 < -EPS) || (d3 < -EPS && d4 > EPS));
}

}  // namespace

std::vector<Point> circle(const Point& center, double radius, int segments) {
    require_finite(radius, "Promień");
    if (radius <= 0) throw std::invalid_argument("Promień okręgu musi być dodatni.");
    if (segments < 8 || segments > 120) throw std::invalid_argument("Okrąg może mieć od 8 do 120 segmentów.");
    std::vector<Point> out;
    for (int i = 0; i < segments; ++i) {
        double angle = 2 * PI * i / segments;
        out.push_back({center[0] + radius * std::cos(angle), center[1] + radius * std::sin(angle)});
    }
    return out;
}

std::vector<Point> regular_polygon(const Point& center, double radius, int sides, double start_degrees) {
    require_finite(radius, "Promień");
    if (radius <= 0) throw std::invalid_argument("Promień wieloboku musi być dodatni.");
    if (sides < 3 || sides > 64) throw std::invalid_argument("Wielobok może mieć od 3 do 64 boków.");
    std::vector<Point> out;
    double start = start_degrees * PI / 180.0;
    for (int i = 0; i < sides; ++i) {
        double angle = start + 2 * PI * i / sides;
        out.push_back({center[0] + radius * std::cos(angle), center[1] + radius * std::sin(angle)});
    }
    return out;
}

std::vector<Point> arc(const Point& center, double radius, double from_radians, double to_radians, int per_quarter) {
    require_finite(radius, "Promień");
    if (radius <= 0) throw std::invalid_argument("Promień łuku musi być dodatni.");
    double sweep = to_radians - from_radians;
    if (std::abs(sweep) < 1e-6) throw std::invalid_argument("Łuk ma zerowy kąt.");
    if (std::abs(sweep) > 2 * PI + 1e-6) throw std::invalid_argument("Łuk nie może mieć więcej niż 360°.");
    int segments = std::max(2, (int)std::ceil(std::abs(sweep) / (PI / 2) * std::max(per_quarter, 1)));
    std::vector<Point> out;
    for (int i = 0; i <= segments; ++i) {
        double angle = from_radians + sweep * i / segments;
        out.push_back({center[0] + radius * std::cos(angle), center[1] + radius * std::sin(angle)});
    }
    return out;
}

std::vector<Point> arc_three_points(const Point& a, const Point& b, const Point& c, int per_quarter) {
    double d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]));
    if (std::abs(d) < 1e-9) throw std::invalid_argument("Trzy punkty łuku leżą na jednej prostej.");
    double a2 = a[0] * a[0] + a[1] * a[1], b2 = b[0] * b[0] + b[1] * b[1], c2 = c[0] * c[0] + c[1] * c[1];
    Point center{(a2 * (b[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - b[1])) / d, (a2 * (c[0] - b[0]) + b2 * (a[0] - c[0]) + c2 * (b[0] - a[0])) / d};
    double radius = dist(center, a);
    double ta = std::atan2(a[1] - center[1], a[0] - center[0]);
    double tc = std::atan2(c[1] - center[1], c[0] - center[0]);
    bool ccw = cross(sub(b, a), sub(c, b)) > 0;
    double sweep = tc - ta;
    if (ccw) { while (sweep <= 0) sweep += 2 * PI; }
    else { while (sweep >= 0) sweep -= 2 * PI; }
    std::vector<Point> out = arc(center, radius, ta, ta + sweep, per_quarter);
    out.front() = a;
    out.back() = c;
    return out;
}

std::vector<Point> rounded_rectangle(const Point& p, const Point& q, double radius, int per_corner) {
    double x0 = std::min(p[0], q[0]), x1 = std::max(p[0], q[0]), y0 = std::min(p[1], q[1]), y1 = std::max(p[1], q[1]);
    double w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) throw std::invalid_argument("Prostokąt musi mieć dodatnią szerokość i wysokość.");
    if (radius < 0) throw std::invalid_argument("Promień zaokrąglenia nie może być ujemny.");
    if (radius * 2 > std::min(w, h) + 1e-9) throw std::invalid_argument("Promień zaokrąglenia jest za duży dla tego prostokąta.");
    if (radius < 1e-9) return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    per_corner = std::clamp(per_corner, 2, 30);
    std::vector<Point> out;
    struct Corner { double cx, cy, from; };
    for (const Corner& c : {Corner{x1 - radius, y0 + radius, -PI / 2}, Corner{x1 - radius, y1 - radius, 0}, Corner{x0 + radius, y1 - radius, PI / 2}, Corner{x0 + radius, y0 + radius, PI}})
        for (int i = 0; i <= per_corner; ++i) {
            double angle = c.from + (PI / 2) * i / per_corner;
            out.push_back({c.cx + radius * std::cos(angle), c.cy + radius * std::sin(angle)});
        }
    limit_points(out);
    return out;
}

const std::vector<ProfileInfo>& profile_catalog() {
    static const std::vector<ProfileInfo> catalog{
        {"Prostokąt", "", 0}, {"Kształt L", "Grubość ramienia [mm]", 100}, {"Kształt U", "Grubość ramion [mm]", 100}, {"Kształt T", "Grubość trzonu i belki [mm]", 100},
        {"Trapez", "Zwężenie góry z każdej strony [mm]", 100}, {"Ścięty róg", "Ścięcie prawego górnego rogu [mm]", 100},
        {"Zaokrąglony prostokąt", "Promień rogów [mm]", 40}, {"Elipsa / okrąg", "Liczba segmentów", 48}, {"Sześciokąt", "", 0}, {"Trójkąt", "", 0}};
    return catalog;
}

std::vector<Point> profile(const std::string& name, double w, double h, double p) {
    require_finite(w, "Szerokość");
    require_finite(h, "Wysokość");
    require_finite(p, "Parametr");
    if (w <= 0 || h <= 0) throw std::invalid_argument("Szerokość i wysokość profilu muszą być dodatnie.");
    if (name == "Prostokąt") return {{0, 0}, {w, 0}, {w, h}, {0, h}};
    if (name == "Kształt L") {
        if (p <= 0 || p >= std::min(w, h)) throw std::invalid_argument("Grubość ramienia L musi być dodatnia i mniejsza niż szerokość i wysokość.");
        return {{0, 0}, {w, 0}, {w, p}, {p, p}, {p, h}, {0, h}};
    }
    if (name == "Kształt U") {
        if (p <= 0 || p * 2 >= w || p >= h) throw std::invalid_argument("Grubość ramion U musi być dodatnia, mniejsza niż połowa szerokości i niż wysokość.");
        return {{0, 0}, {w, 0}, {w, h}, {w - p, h}, {w - p, p}, {p, p}, {p, h}, {0, h}};
    }
    if (name == "Kształt T") {
        if (p <= 0 || p >= w || p >= h) throw std::invalid_argument("Grubość T musi być dodatnia i mniejsza niż szerokość i wysokość.");
        double cx = w / 2;
        return {{cx - p / 2, 0}, {cx + p / 2, 0}, {cx + p / 2, h - p}, {w, h - p}, {w, h}, {0, h}, {0, h - p}, {cx - p / 2, h - p}};
    }
    if (name == "Trapez") {
        if (p < 0 || p * 2 >= w) throw std::invalid_argument("Zwężenie trapezu musi być w zakresie 0 … połowa szerokości.");
        if (p == 0) return {{0, 0}, {w, 0}, {w, h}, {0, h}};
        return {{0, 0}, {w, 0}, {w - p, h}, {p, h}};
    }
    if (name == "Ścięty róg") {
        if (p <= 0 || p >= std::min(w, h)) throw std::invalid_argument("Ścięcie rogu musi być dodatnie i mniejsze niż szerokość i wysokość.");
        return {{0, 0}, {w, 0}, {w, h - p}, {w - p, h}, {0, h}};
    }
    if (name == "Zaokrąglony prostokąt") return rounded_rectangle({0, 0}, {w, h}, p, 6);
    if (name == "Elipsa / okrąg") {
        int segments = (int)std::lround(p);
        if (segments < 8 || segments > 120) throw std::invalid_argument("Elipsa może mieć od 8 do 120 segmentów.");
        std::vector<Point> out = circle({w / 2, h / 2}, 0.5, segments);
        for (auto& pt : out) pt = {w / 2 + (pt[0] - w / 2) * w, h / 2 + (pt[1] - h / 2) * h};
        return out;
    }
    if (name == "Sześciokąt") {
        std::vector<Point> out = regular_polygon({0.5, 0.5}, 0.5, 6, 0);
        double xmin = out[0][0], xmax = xmin;
        for (auto& pt : out) { xmin = std::min(xmin, pt[0]); xmax = std::max(xmax, pt[0]); }
        double ymin = 1e9, ymax = -1e9;
        for (auto& pt : out) { ymin = std::min(ymin, pt[1]); ymax = std::max(ymax, pt[1]); }
        for (auto& pt : out) pt = {(pt[0] - xmin) / (xmax - xmin) * w, (pt[1] - ymin) / (ymax - ymin) * h};
        return out;
    }
    if (name == "Trójkąt") return {{0, 0}, {w, 0}, {w / 2, h}};
    throw std::invalid_argument("Nieznany profil: " + name);
}

std::vector<Point> fillet_corner(const std::vector<Point>& pts, size_t index, double radius, bool closed, int per_quarter) {
    if (index >= pts.size()) throw std::invalid_argument("Nie ma takiego punktu.");
    if (radius <= 0) throw std::invalid_argument("Promień zaokrąglenia musi być dodatni.");
    size_t n = pts.size();
    if (!closed && (index == 0 || index + 1 == n)) throw std::invalid_argument("Końcowy punkt otwartego konturu nie ma narożnika do zaokrąglenia.");
    if (n < 3) throw std::invalid_argument("Zaokrąglenie wymaga przynajmniej trzech punktów.");
    const Point& P = pts[index];
    const Point& A = pts[(index + n - 1) % n];
    const Point& B = pts[(index + 1) % n];
    double la = dist(A, P), lb = dist(B, P);
    if (la < EPS || lb < EPS) throw std::invalid_argument("Sąsiednie punkty pokrywają się z narożnikiem.");
    Point u1{(A[0] - P[0]) / la, (A[1] - P[1]) / la}, u2{(B[0] - P[0]) / lb, (B[1] - P[1]) / lb};
    double cos_theta = std::clamp(u1[0] * u2[0] + u1[1] * u2[1], -1.0, 1.0);
    double theta = std::acos(cos_theta);
    if (theta < 1e-3 || theta > PI - 1e-3) throw std::invalid_argument("Narożnik leży na prostej — nie ma czego zaokrąglić.");
    double tangent = radius / std::tan(theta / 2);
    if (tangent > la + 1e-9 || tangent > lb + 1e-9)
        throw std::invalid_argument("Promień jest za duży dla sąsiednich odcinków (maksymalnie " + std::to_string((long long)std::floor(std::min(la, lb) * std::tan(theta / 2))) + " mm).");
    Point t1{P[0] + u1[0] * tangent, P[1] + u1[1] * tangent}, t2{P[0] + u2[0] * tangent, P[1] + u2[1] * tangent};
    Point bis{u1[0] + u2[0], u1[1] + u2[1]};
    double bl = std::hypot(bis[0], bis[1]);
    double to_center = radius / std::sin(theta / 2);
    Point center{P[0] + bis[0] / bl * to_center, P[1] + bis[1] / bl * to_center};
    double a1 = std::atan2(t1[1] - center[1], t1[0] - center[0]), a2 = std::atan2(t2[1] - center[1], t2[0] - center[0]);
    double sweep = a2 - a1;
    while (sweep > PI) sweep -= 2 * PI;
    while (sweep < -PI) sweep += 2 * PI;
    std::vector<Point> arc_points = arc(center, radius, a1, a1 + sweep, per_quarter);
    arc_points.front() = t1;
    arc_points.back() = t2;
    std::vector<Point> out;
    for (size_t i = 0; i < n; ++i) {
        if (i == index) out.insert(out.end(), arc_points.begin(), arc_points.end());
        else out.push_back(pts[i]);
    }
    limit_points(out);
    return out;
}

std::vector<Point> chamfer_corner(const std::vector<Point>& pts, size_t index, double distance, bool closed) {
    if (index >= pts.size()) throw std::invalid_argument("Nie ma takiego punktu.");
    if (distance <= 0) throw std::invalid_argument("Odległość fazy musi być dodatnia.");
    size_t n = pts.size();
    if (!closed && (index == 0 || index + 1 == n)) throw std::invalid_argument("Końcowy punkt otwartego konturu nie ma narożnika do ścięcia.");
    if (n < 3) throw std::invalid_argument("Fazowanie wymaga przynajmniej trzech punktów.");
    const Point& P = pts[index];
    const Point& A = pts[(index + n - 1) % n];
    const Point& B = pts[(index + 1) % n];
    double la = dist(A, P), lb = dist(B, P);
    if (distance > la - 1e-9 || distance > lb - 1e-9) throw std::invalid_argument("Faza jest za duża dla sąsiednich odcinków (maksymalnie " + std::to_string((long long)std::floor(std::min(la, lb))) + " mm).");
    Point c1{P[0] + (A[0] - P[0]) / la * distance, P[1] + (A[1] - P[1]) / la * distance};
    Point c2{P[0] + (B[0] - P[0]) / lb * distance, P[1] + (B[1] - P[1]) / lb * distance};
    std::vector<Point> out;
    for (size_t i = 0; i < n; ++i) {
        if (i == index) { out.push_back(c1); out.push_back(c2); }
        else out.push_back(pts[i]);
    }
    limit_points(out);
    return out;
}

std::vector<Point> remove_point(const std::vector<Point>& pts, size_t index, bool closed) {
    if (index >= pts.size()) throw std::invalid_argument("Nie ma takiego punktu.");
    if (closed && pts.size() <= 3) throw std::invalid_argument("Zamknięty kontur wymaga przynajmniej trzech punktów.");
    std::vector<Point> out = pts;
    out.erase(out.begin() + (long)index);
    return out;
}

std::vector<Point> simplify(const std::vector<Point>& pts, size_t max_points) {
    if (pts.size() <= max_points) return pts;
    if (max_points < 3) throw std::invalid_argument("Za mało punktów dopuszczonych do uproszczenia.");
    std::vector<Point> loop = pts;
    loop.push_back(pts.front());
    double low = 0, high = 1;
    for (const auto& p : pts) high = std::max(high, dist(p, pts.front()));
    std::vector<bool> best;
    auto count_with = [&](double tolerance, std::vector<bool>& keep) {
        keep.assign(loop.size(), false);
        keep.front() = keep.back() = true;
        simplify_range(loop, 0, loop.size() - 1, tolerance, keep);
        size_t count = 0;
        for (size_t i = 0; i + 1 < keep.size(); ++i) count += keep[i];
        return count;
    };
    std::vector<bool> keep;
    for (int iteration = 0; iteration < 40; ++iteration) {
        double mid = (low + high) / 2;
        if (count_with(mid, keep) <= max_points) { high = mid; best = keep; }
        else low = mid;
    }
    if (best.empty()) count_with(high, best);
    std::vector<Point> out;
    for (size_t i = 0; i + 1 < loop.size(); ++i) if (best[i]) out.push_back(loop[i]);
    if (out.size() < 3) throw std::invalid_argument("Nie udało się uprościć konturu.");
    return out;
}

namespace {

double parse_number_with_unit(std::string token, const std::string& original) {
    double scale = 1;
    auto ends_with = [&](const char* suffix) { std::string s(suffix); return token.size() > s.size() && token.compare(token.size() - s.size(), s.size(), s) == 0; };
    if (ends_with("mm")) token.resize(token.size() - 2);
    else if (ends_with("cm")) { token.resize(token.size() - 2); scale = 10; }
    else if (ends_with("m")) { token.resize(token.size() - 1); scale = 1000; }
    if (token.empty()) throw std::invalid_argument("Nie rozumiem wartości „" + original + "”.");
    char* end = nullptr;
    double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(value)) throw std::invalid_argument("Nie rozumiem wartości „" + original + "”.");
    return value * scale;
}

}  // namespace

ValueInput parse_value_input(const std::string& input, bool comma_separates) {
    std::string text;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = (unsigned char)input[i];
        if (c == 0xC3 && i + 1 < input.size() && (unsigned char)input[i + 1] == 0x97) { text += ';'; ++i; }  // „×”
        else if (c == 'x' || c == 'X') text += ';';
        else text += (char)c;
    }
    ValueInput out;
    std::string angle_text;
    if (auto lt = text.find('<'); lt != std::string::npos) {
        angle_text = text.substr(lt + 1);
        text = text.substr(0, lt);
        std::string trimmed;
        for (size_t i = 0; i < angle_text.size(); ++i) {
            unsigned char c = (unsigned char)angle_text[i];
            if (c == 0xC2 && i + 1 < angle_text.size() && (unsigned char)angle_text[i + 1] == 0xB0) { ++i; continue; }  // „°”
            if (c != ' ') trimmed += (char)c;
        }
        for (auto& c : trimmed) if (c == ',') c = '.';
        char* end = nullptr;
        double a = std::strtod(trimmed.c_str(), &end);
        if (trimmed.empty() || end == trimmed.c_str() || *end != '\0' || !std::isfinite(a)) throw std::invalid_argument("Nie rozumiem kąta „" + angle_text + "”.");
        out.has_angle = true;
        out.angle = a;
    }
    bool other_separator = text.find(';') != std::string::npos || text.find(' ') != std::string::npos;
    size_t commas = (size_t)std::count(text.begin(), text.end(), ',');
    for (auto& c : text) {
        if (c == ',') c = (comma_separates && !other_separator && commas == 1) ? ';' : '.';
    }
    std::string token;
    auto flush = [&] {
        if (!token.empty()) { out.numbers.push_back(parse_number_with_unit(token, input)); token.clear(); }
    };
    for (char c : text) {
        if (c == ';' || c == ' ' || c == '\t') flush();
        else token += c;
    }
    flush();
    if (out.numbers.empty() && !out.has_angle) throw std::invalid_argument("Wpisz wartość liczbową.");
    return out;
}

double signed_area(const std::vector<Point>& pts) {
    double area = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Point& a = pts[i];
        const Point& b = pts[(i + 1) % pts.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    return area / 2;
}

bool self_intersects(const std::vector<Point>& pts) {
    size_t n = pts.size();
    if (n < 4) return false;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segments_cross(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])) return true;
        }
    return false;
}

}  // namespace sketch_shapes
