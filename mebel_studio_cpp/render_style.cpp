#include "render_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>

namespace render_style {

namespace {

constexpr double PI = std::numbers::pi;

Point3 normalized(const Point3& v) {
    double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-12) return {0, 0, 1};
    return {v[0] / length, v[1] / length, v[2] / length};
}

double dot(const Point3& a, const Point3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double cross(const Point2& o, const Point2& a, const Point2& b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); }

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("Kolor musi mieć postać #rrggbb.");
}

std::array<int, 3> parse_hex(const std::string& color) {
    if (color.size() != 7 || color[0] != '#') throw std::invalid_argument("Kolor musi mieć postać #rrggbb: " + color);
    return {hex_digit(color[1]) * 16 + hex_digit(color[2]), hex_digit(color[3]) * 16 + hex_digit(color[4]), hex_digit(color[5]) * 16 + hex_digit(color[6])};
}

}  // namespace

const std::vector<ModeInfo>& modes() {
    static const std::vector<ModeInfo> list = {
        {Mode::Classic, "classic", "Klasyczna", "Lekkie, płaskie cieniowanie i cienie kontaktowe — najszybsze, czytelne przy pracy nad konstrukcją."},
        {Mode::Realistic, "realistic", "Realistyczna", "Głębsze cieniowanie ścianek, miękkie cienie rzucane przez mebel i płytki na podłodze — jak na wizualizacji. Rysuje go silnik GPU lub CPU (natywna biblioteka ma stałe cieniowanie)."},
    };
    return list;
}

const ModeInfo& info(Mode mode) {
    for (const auto& m : modes())
        if (m.mode == mode) return m;
    throw std::invalid_argument("Nieznany tryb grafiki.");
}

Mode mode_from(const std::string& key) {
    for (const auto& m : modes())
        if (m.id == key || m.name == key) return m.mode;
    throw std::invalid_argument("Nieznany tryb grafiki: „" + key + "”.");
}

Shading shade_face(Mode mode, const Point3& normal_in, double brightness, double ambient) {
    Point3 n = normalized(normal_in);
    if (mode == Mode::Classic) {
        const Point3 sun = {-.3, -.5, .81};
        double directional = std::fabs(dot(n, sun));
        return {std::min(1.3, std::max(.08, (ambient + .18 * directional) * brightness)), 0};
    }
    // Normalne ścianek nie zawsze są skierowane na zewnątrz, więc światło liczymy dwustronnie.
    static const Point3 key = normalized({-.45, -.60, .66});
    static const Point3 fill = normalized({.70, .20, .30});
    double key_part = std::fabs(dot(n, key));
    double fill_part = std::fabs(dot(n, fill));
    double top = std::fabs(n[2]);
    double ambient_scale = std::min(1.3, std::max(.35, ambient / .82));
    double factor = brightness * (.46 * ambient_scale + .16 * top + .42 * key_part + .14 * fill_part);
    return {std::min(1.25, std::max(.18, factor)), std::min(1.0, std::max(-1.0, key_part - fill_part))};
}

std::array<int, 3> apply_shading(const std::array<int, 3>& base, const Shading& shading) {
    double red = base[0] * shading.factor * (1 + .05 * shading.warm);
    double green = base[1] * shading.factor;
    double blue = base[2] * shading.factor * (1 - .05 * shading.warm);
    auto clamp = [](double x) { return (int)std::lround(std::min(255.0, std::max(0.0, x))); };
    return {clamp(red), clamp(green), clamp(blue)};
}

std::vector<Point2> convex_hull(std::vector<Point2> points) {
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) return points;
    std::vector<Point2> hull(2 * points.size());
    size_t k = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 1e-12) --k;
        hull[k++] = points[i];
    }
    for (size_t i = points.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 1e-12) --k;
        hull[k++] = points[i - 1];
    }
    hull.resize(k - 1);
    return hull;
}

std::vector<Point2> offset_convex(const std::vector<Point2>& hull, double distance, int arc_steps) {
    if (hull.size() < 3 || distance <= 1e-9) return hull;
    std::vector<Point2> out;
    size_t n = hull.size();
    arc_steps = std::max(1, arc_steps);
    for (size_t i = 0; i < n; ++i) {
        const Point2& prev = hull[(i + n - 1) % n];
        const Point2& cur = hull[i];
        const Point2& next = hull[(i + 1) % n];
        // wielokąt jest przeciwny do ruchu wskazówek zegara: normalna zewnętrzna krawędzi (dx,dy) to (dy,-dx)
        double a0 = std::atan2(-(cur[0] - prev[0]), cur[1] - prev[1]);
        double a1 = std::atan2(-(next[0] - cur[0]), next[1] - cur[1]);
        double delta = a1 - a0;
        while (delta < 0) delta += 2 * PI;
        while (delta >= 2 * PI) delta -= 2 * PI;
        int steps = std::max(1, (int)std::ceil(delta / (PI / 2) * arc_steps));
        for (int s = 0; s <= steps; ++s) {
            double a = a0 + delta * s / steps;
            out.push_back({cur[0] + distance * std::cos(a), cur[1] + distance * std::sin(a)});
        }
    }
    return out;
}

std::vector<ShadowLayer> cast_shadow(const std::vector<Point2>& footprint, double bottom_z, double top_z, const ShadowSettings& s) {
    if (footprint.size() < 3) return {};
    bottom_z = std::max(0.0, bottom_z);
    top_z = std::max(bottom_z, top_z);
    // Rzut sylwetki wzdłuż światła głównego: stopa na podłodze + szczyt przesunięty proporcjonalnie do wysokości.
    std::vector<Point2> region = footprint;
    for (const auto& p : footprint) region.push_back({p[0] + s.offset_per_height_x * top_z, p[1] + s.offset_per_height_y * top_z});
    std::vector<Point2> hull = convex_hull(region);
    if (hull.size() < 3) return {};
    // Mebel uniesiony nad podłogą rzuca słabszy i szerszy cień.
    double lift = 1 + bottom_z / 250.0;
    double strength = s.max_strength / lift;
    double spread = s.spread * (1 + bottom_z / 180.0) * (1 + top_z / 1500.0);
    int n = std::max(1, s.layers);
    std::vector<ShadowLayer> layers;
    for (int i = 0; i < n; ++i) {
        double fraction = n == 1 ? 0.0 : 1.0 - (double)i / (n - 1);  // 1 = najbardziej zewnętrzna
        double layer_strength = strength * std::pow((double)(i + 1) / n, 1.6);
        layers.push_back({offset_convex(hull, spread * fraction), layer_strength});
    }
    return layers;
}

std::string blend_hex(const std::string& base, const std::string& over, double amount) {
    amount = std::min(1.0, std::max(0.0, amount));
    auto a = parse_hex(base), b = parse_hex(over);
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", (int)std::lround(a[0] * (1 - amount) + b[0] * amount), (int)std::lround(a[1] * (1 - amount) + b[1] * amount),
                  (int)std::lround(a[2] * (1 - amount) + b[2] * amount));
    return buffer;
}

std::vector<Segment3> floor_tile_lines(double cx, double cy, double extent, double tile, double z, int max_lines) {
    if (!(tile > 1) || !(extent > 0)) throw std::invalid_argument("Płytki podłogi: bok i zasięg muszą być dodatnie.");
    int n = std::min(max_lines, std::max(2, (int)std::ceil(extent / tile)));
    double ox = std::floor(cx / tile) * tile, oy = std::floor(cy / tile) * tile;
    std::vector<Segment3> lines;
    for (int i = -n; i <= n; ++i) {
        lines.push_back({{ox + i * tile, oy - n * tile, z}, {ox + i * tile, oy + n * tile, z}});
        lines.push_back({{ox - n * tile, oy + i * tile, z}, {ox + n * tile, oy + i * tile, z}});
    }
    return lines;
}

std::string rgb_hex(const std::array<int, 3>& c) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", std::min(255, std::max(0, c[0])), std::min(255, std::max(0, c[1])), std::min(255, std::max(0, c[2])));
    return buffer;
}

std::vector<ThumbPoly> mode_thumbnail(Mode mode, double w, double h) {
    if (!(w >= 30 && h >= 30)) throw std::invalid_argument("Miniatura trybu grafiki wymaga pola co najmniej 30 × 30 px.");
    const std::string floor_color = "#ece8e0";
    std::vector<ThumbPoly> out;
    out.push_back({{{0, 0}, {w, 0}, {w, h}, {0, h}}, floor_color, false, false});
    const double s = std::min(w, h) * 0.30, cx = w * 0.44, gy = h * 0.76, height = s * 1.1;
    const Point2 front{cx, gy}, right{cx + s, gy - s * .5}, back{cx, gy - s}, left{cx - s, gy - s * .5};
    const bool real = mode == Mode::Realistic;
    if (real) {
        const double t = s * .8;
        const Point2 a{t, -t * .5}, b{-t, -t * .5};
        for (int i = -4; i <= 4; ++i) {
            Point2 p0{front[0] + i * a[0] - 5 * b[0], front[1] + i * a[1] - 5 * b[1]}, p1{front[0] + i * a[0] + 3 * b[0], front[1] + i * a[1] + 3 * b[1]};
            Point2 q0{front[0] + i * b[0] - 5 * a[0], front[1] + i * b[1] - 5 * a[1]}, q1{front[0] + i * b[0] + 3 * a[0], front[1] + i * b[1] + 3 * a[1]};
            out.push_back({{p0, p1}, blend_hex(floor_color, "#8d8272", .13), true, false});
            out.push_back({{q0, q1}, blend_hex(floor_color, "#8d8272", .13), true, false});
        }
    }
    ShadowSettings settings;
    settings.layers = real ? 6 : 2;
    settings.spread = real ? s * .35 : s * .12;
    settings.max_strength = real ? .55 : .38;
    settings.offset_per_height_x = .55;
    settings.offset_per_height_y = .10;
    for (const auto& layer : cast_shadow({front, right, back, left}, 0, real ? height * .8 : 0, settings))
        out.push_back({layer.polygon, blend_hex(floor_color, real ? "#3a342c" : "#2f3031", layer.strength), false, false});
    auto face_color = [&](const Point3& normal) { return rgb_hex(apply_shading({201, 162, 107}, shade_face(mode, normal, 1.0, .82))); };
    const Point2 up{0, -height};
    auto lift = [&](const Point2& p) { return Point2{p[0] + up[0], p[1] + up[1]}; };
    out.push_back({{left, front, lift(front), lift(left)}, face_color({-1, 0, 0}), false, true});
    out.push_back({{front, right, lift(right), lift(front)}, face_color({0, -1, 0}), false, true});
    out.push_back({{lift(front), lift(right), lift(back), lift(left)}, face_color({0, 0, 1}), false, true});
    // front (drzwiczki) w jaśniejszym kolorze, wcięty w prawą ściankę
    Point2 d0{front[0] + (right[0] - front[0]) * .12, front[1] + (right[1] - front[1]) * .12 - height * .10};
    Point2 d1{front[0] + (right[0] - front[0]) * .88, front[1] + (right[1] - front[1]) * .88 - height * .10};
    out.push_back({{d0, d1, {d1[0], d1[1] - height * .8}, {d0[0], d0[1] - height * .8}},
                   rgb_hex(apply_shading({233, 224, 207}, shade_face(mode, {0, -1, 0}, 1.0, .82))), false, true});
    return out;
}

}  // namespace render_style
