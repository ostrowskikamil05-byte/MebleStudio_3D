#include "modeler_geometry.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace modeler_geometry {

namespace {

constexpr double PI = std::numbers::pi;
constexpr double MIN_SIZE = .5, MAX_SIZE = 6000;

[[noreturn]] void fail(const std::string& text) { throw std::invalid_argument(text); }

std::string num(double x) { return std::format("{:g}", std::round(x * 100) / 100); }

const char* axis_name(int axis) { return axis == 0 ? "X" : axis == 1 ? "Y" : "Z"; }

// Oś świata, wzdłuż której biegnie lokalna oś płyty: lokalne [u, v, w] → oś świata.
std::array<int, 3> local_axes(const std::string& plane) {
    if (plane == "XY") return {0, 1, 2};
    if (plane == "XZ") return {0, 2, 1};
    if (plane == "YZ") return {1, 2, 0};
    fail("Nieznana płaszczyzna płyty „" + plane + "”.");
}

int normal_axis(const Plane& plane) {
    for (int k = 0; k < 3; ++k)
        if (std::fabs(std::fabs(plane.n[k]) - 1) < 1e-6) return k;
    fail("Rysowanie na ścianie, która nie jest równoległa do osi X, Y lub Z (płyta obrócona), nie jest obsługiwane — ustaw obrót w inspektorze.");
}

}  // namespace

// ------------------------------------------------------------------ wektory

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scale(const Vec3& a, double k) { return {a[0] * k, a[1] * k, a[2] * k}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double length(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 unit(const Vec3& a) {
    double l = length(a);
    if (!(l > 1e-12)) fail("Nie można wyznaczyć kierunku z wektora zerowego.");
    return scale(a, 1 / l);
}

// ------------------------------------------------------------------ promień z ekranu

Ray unproject(const core::Camera& camera, double sx, double sy, double width, double height) {
    if (std::fabs(camera.scale) < 1e-9) fail("Skala kamery jest zerowa — nie da się wskazać punktu na scenie.");
    double a = camera.yaw * (PI / 180), b = camera.pitch * (PI / 180);
    double ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
    double u = (sx - width / 2 - camera.pan[0]) / camera.scale;
    double v = -(sy - height / 2 - camera.pan[1]) / camera.scale;
    auto point_at = [&](double depth) {
        double z = v * cb + depth * sb;
        double d = v * sb - depth * cb;
        double x = u * ca + d * sa;
        double y = -u * sa + d * ca;
        return Vec3{camera.center[0] + x, camera.center[1] + y, camera.center[2] + z};
    };
    Ray ray;
    ray.origin = point_at(-1e5);
    ray.dir = {-cb * sa, -cb * ca, sb};
    return ray;
}

Vec3 at(const Ray& ray, double t) { return add(ray.origin, scale(ray.dir, t)); }

// ------------------------------------------------------------------ płaszczyzny

Plane ground_plane(double z) { return Plane{{0, 0, z}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "Podłoga"}; }
Plane front_plane(double y) { return Plane{{0, y, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, "Przód"}; }
Plane side_plane(double x) { return Plane{{x, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, "Bok"}; }

std::optional<double> ray_plane(const Ray& ray, const Plane& plane) {
    double denom = dot(plane.n, ray.dir);
    if (std::fabs(denom) < 1e-6) return std::nullopt;
    return dot(plane.n, sub(plane.origin, ray.origin)) / denom;
}

Point to_plane(const Plane& plane, const Vec3& point) {
    Vec3 d = sub(point, plane.origin);
    return {dot(d, plane.u), dot(d, plane.v)};
}

Vec3 from_plane(const Plane& plane, double a, double b) { return add(plane.origin, add(scale(plane.u, a), scale(plane.v, b))); }

// ------------------------------------------------------------------ płyty

bool axis_aligned(const core::Part& part) {
    if (std::fabs(part.rx) > 1e-9 || std::fabs(part.ry) > 1e-9 || std::fabs(part.rz) > 1e-9) return false;
    if (part.layout.is_object() && part.layout.contains("yaw") && part.layout["yaw"].is_number() && std::fabs(part.layout["yaw"].get<double>()) > 1e-9) return false;
    return true;
}

Box part_box(const core::Part& part) {
    std::vector<Point> points = part.points();
    double umin = points[0][0], umax = umin, vmin = points[0][1], vmax = vmin;
    for (const auto& p : points) {
        umin = std::min(umin, p[0]); umax = std::max(umax, p[0]);
        vmin = std::min(vmin, p[1]); vmax = std::max(vmax, p[1]);
    }
    Box box;
    box.lo = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    box.hi = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (double u : {umin, umax})
        for (double v : {vmin, vmax})
            for (double w : {0.0, part.t}) {
                Vec3 p = core::world(part, u, v, w);
                for (int k = 0; k < 3; ++k) { box.lo[k] = std::min(box.lo[k], p[k]); box.hi[k] = std::max(box.hi[k], p[k]); }
            }
    return box;
}

Vec3 box_size(const Box& box) { return sub(box.hi, box.lo); }

core::Part board_from_box(const Box& box, const std::string& name, const std::string& group, const std::string& material) {
    Vec3 size = box_size(box);
    for (int k = 0; k < 3; ++k) {
        if (!(size[k] >= MIN_SIZE)) fail(std::format("Wymiar {} = {} mm jest mniejszy niż {} mm — bryła nie jest płytą.", axis_name(k), num(size[k]), num(MIN_SIZE)));
        if (size[k] > MAX_SIZE) fail(std::format("Wymiar {} = {} mm przekracza {} mm.", axis_name(k), num(size[k]), num(MAX_SIZE)));
    }
    int thin = 2;
    if (size[1] < size[thin]) thin = 1;
    if (size[0] < size[thin]) thin = 0;
    core::Part part;
    part.name = name;
    part.group = group;
    part.material = material;
    part.role = "other";
    part.assembly = core::json::object();
    part.t = size[thin];
    if (thin == 2) { part.plane = "XY"; part.u = size[0]; part.v = size[1]; }
    else if (thin == 1) { part.plane = "XZ"; part.u = size[0]; part.v = size[2]; }
    else { part.plane = "YZ"; part.u = size[1]; part.v = size[2]; }
    part.x = box.lo[0]; part.y = box.lo[1]; part.z = box.lo[2];
    part.contour.clear();
    return part;
}

core::Part board_from_polygon(const Plane& plane, const std::vector<Vec3>& points, double thickness, const std::string& name, const std::string& group,
                              const std::string& material) {
    if (points.size() < 3) fail("Kontur wymaga przynajmniej trzech punktów.");
    int k = normal_axis(plane);
    double t = std::fabs(thickness);
    if (!(t >= MIN_SIZE)) fail(std::format("Grubość {} mm jest mniejsza niż {} mm.", num(t), num(MIN_SIZE)));
    if (t > MAX_SIZE) fail(std::format("Grubość {} mm przekracza {} mm.", num(t), num(MAX_SIZE)));
    // Osie świata, które tworzą lokalne U i V płyty o normalnej wzdłuż osi k.
    int au = k == 2 ? 0 : k == 1 ? 0 : 1, av = k == 2 ? 1 : 2;
    std::string part_plane = k == 2 ? "XY" : k == 1 ? "XZ" : "YZ";
    double umin = points[0][au], vmin = points[0][av], umax = umin, vmax = vmin;
    for (const auto& p : points) {
        umin = std::min(umin, p[au]); umax = std::max(umax, p[au]);
        vmin = std::min(vmin, p[av]); vmax = std::max(vmax, p[av]);
    }
    std::vector<std::vector<double>> shifted;
    for (const auto& p : points) shifted.push_back({p[au] - umin, p[av] - vmin});
    std::vector<Point> contour = core::validate_polygon(shifted);  // zgłasza samoprzecięcia i zerowe pole
    core::Part part;
    part.name = name; part.group = group; part.material = material; part.role = "other";
    part.assembly = core::json::object();
    part.plane = part_plane;
    part.u = umax - umin; part.v = vmax - vmin; part.t = t;
    part.contour.clear();
    for (const auto& p : contour) part.contour.push_back({p[0], p[1]});
    double along = plane.origin[k] + (thickness * plane.n[k] >= 0 ? 0 : -t);  // grubość płyty biegnie zawsze w stronę dodatniej osi
    Vec3 origin{0, 0, 0};
    origin[au] = umin; origin[av] = vmin; origin[k] = along;
    part.x = origin[0]; part.y = origin[1]; part.z = origin[2];
    return part;
}

Box box_from_rectangle(const Plane& plane, const Vec3& p0, const Vec3& p1, double depth) {
    int k = normal_axis(plane);
    Point a = to_plane(plane, p0), b = to_plane(plane, p1);
    if (std::fabs(a[0] - b[0]) < MIN_SIZE || std::fabs(a[1] - b[1]) < MIN_SIZE)
        fail(std::format("Prostokąt {} × {} mm jest za mały (minimum {} mm).", num(std::fabs(a[0] - b[0])), num(std::fabs(a[1] - b[1])), num(MIN_SIZE)));
    if (std::fabs(depth) < MIN_SIZE) fail(std::format("Wyciągnięcie {} mm jest za małe (minimum {} mm).", num(depth), num(MIN_SIZE)));
    (void)k;
    Vec3 q = add(p0, scale(plane.n, depth));
    Box box;
    for (int i = 0; i < 3; ++i) {
        box.lo[i] = std::min({p0[i], p1[i], q[i]});
        box.hi[i] = std::max({p0[i], p1[i], q[i]});
    }
    return box;
}

// ------------------------------------------------------------------ ściany

std::vector<Face> part_faces(const core::Part& part) {
    std::vector<Point> points = part.points();
    if (points.size() < 3) return {};
    const bool rect = part.contour.empty();
    auto w = [&](double u, double v, double h) { return core::world(part, u, v, h); };
    Vec3 origin = w(0, 0, 0);
    Vec3 axis_u = unit(sub(w(1, 0, 0), origin)), axis_v = unit(sub(w(0, 1, 0), origin)), axis_w = unit(sub(w(0, 0, 1), origin));
    Vec3 center{0, 0, 0};
    for (const auto& p : points) center = add(center, add(w(p[0], p[1], 0), w(p[0], p[1], part.t)));
    center = scale(center, 1.0 / (2 * points.size()));
    std::vector<Face> faces;
    {
        Face top;
        top.id = "top";
        top.plane = Plane{w(points[0][0], points[0][1], part.t), axis_u, axis_v, axis_w, part.name + " / góra"};
        for (const auto& p : points) top.polygon.push_back(w(p[0], p[1], part.t));
        faces.push_back(top);
        Face bottom;
        bottom.id = "bottom";
        bottom.plane = Plane{w(points[0][0], points[0][1], 0), axis_u, axis_v, scale(axis_w, -1), part.name + " / spód"};
        for (const auto& p : points) bottom.polygon.push_back(w(p[0], p[1], 0));
        faces.push_back(bottom);
    }
    static const char* rect_ids[4] = {"v0", "u1", "v1", "u0"};
    for (size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        Vec3 a0 = w(a[0], a[1], 0), b0 = w(b[0], b[1], 0), b1 = w(b[0], b[1], part.t), a1 = w(a[0], a[1], part.t);
        Vec3 along = sub(b0, a0);
        if (length(along) < 1e-9) continue;
        Vec3 u = unit(along);
        Vec3 n = unit(cross(u, axis_w));
        Vec3 middle = scale(add(add(a0, b1), add(b0, a1)), .25);
        if (dot(n, sub(middle, center)) < 0) n = scale(n, -1);
        Face side;
        side.id = rect && points.size() == 4 ? rect_ids[i] : "edge:" + std::to_string(i);
        side.plane = Plane{a0, u, axis_w, n, part.name + " / bok"};
        side.polygon = {a0, b0, b1, a1};
        faces.push_back(side);
    }
    return faces;
}

std::optional<FaceHit> pick_face(const core::Project& project, const core::Camera& camera, double sx, double sy, double width, double height,
                                 const std::set<std::string>& skip) {
    Ray ray = unproject(camera, sx, sy, width, height);
    std::optional<FaceHit> best;
    for (size_t i = 0; i < project.parts.size(); ++i) {
        const core::Part& part = project.parts[i];
        if (!part.visible || skip.contains(part.id)) continue;
        for (const Face& face : part_faces(part)) {
            if (dot(face.plane.n, ray.dir) <= 1e-9) continue;  // ściana odwrócona od patrzącego
            auto t = ray_plane(ray, face.plane);
            if (!t) continue;
            Vec3 hit = at(ray, *t);
            std::vector<Point> polygon;
            for (const auto& p : face.polygon) polygon.push_back(to_plane(face.plane, p));
            if (!core::inside(to_plane(face.plane, hit), polygon)) continue;
            if (!best || *t > best->t) {
                FaceHit candidate;
                candidate.part = i; candidate.face = face.id; candidate.plane = face.plane; candidate.point = hit; candidate.t = *t;
                best = candidate;
            }
        }
    }
    return best;
}

// ------------------------------------------------------------------ edycja

core::Part pull_face(const core::Part& part, const std::string& face, double distance) {
    if (!std::isfinite(distance)) fail("Odległość wyciągnięcia nie jest liczbą.");
    if (!axis_aligned(part))
        fail("Płyta „" + part.name + "” jest obrócona — wyciąganie ścian działa tylko dla płyt równoległych do osi. Zmień obrót w inspektorze.");
    int local = face == "top" || face == "bottom" ? 2 : face == "u0" || face == "u1" ? 0 : face == "v0" || face == "v1" ? 1 : -1;
    if (local < 0) fail("Ściana „" + face + "” nie ma stałego położenia — wyciągać można górę, spód oraz boki płyt prostokątnych (u0, u1, v0, v1).");
    const bool high = face == "top" || face == "u1" || face == "v1";
    if (local != 2) {
        if (!part.contour.empty() || (part.profile.is_object() && !part.profile.empty()))
            fail("Płyta „" + part.name + "” ma niestandardowy kontur — zmień jej wymiar w szkicu albo w inspektorze.");
        if ((part.holes.size() + (part.operations.is_array() ? part.operations.size() : 0) + (part.machining.is_array() ? part.machining.size() : 0)) > 0)
            fail("Płyta „" + part.name + "” ma wiercenia lub frezy — zmiana jej długości lub szerokości przesunęłaby je. Zmień wymiar w inspektorze albo najpierw usuń obróbkę.");
    }
    core::Part result = part;
    double* dims[3] = {&result.u, &result.v, &result.t};
    double next = *dims[local] + distance;
    if (next < MIN_SIZE) fail(std::format("Po zmianie wymiar byłby {} mm — mniej niż {} mm.", num(next), num(MIN_SIZE)));
    if (next > MAX_SIZE) fail(std::format("Po zmianie wymiar byłby {} mm — więcej niż {} mm.", num(next), num(MAX_SIZE)));
    *dims[local] = next;
    if (!high) {
        int world_axis = local_axes(part.plane)[(size_t)local];
        double* origin[3] = {&result.x, &result.y, &result.z};
        *origin[world_axis] -= distance;
    }
    return result;
}

core::Part moved(const core::Part& part, const Vec3& delta) {
    core::Part result = part;
    result.x += delta[0]; result.y += delta[1]; result.z += delta[2];
    return result;
}

std::vector<core::Part> array_copies(const core::Part& part, const Vec3& delta, int count) {
    if (count < 1 || count > 200) fail("Liczba kopii musi mieścić się w zakresie 1 … 200.");
    if (length(delta) < 1e-9) fail("Kopie o zerowym przesunięciu nałożyłyby się na oryginał.");
    std::vector<core::Part> out;
    for (int i = 1; i <= count; ++i) {
        core::Part copy = moved(part, scale(delta, i));
        copy.id = core::uid();
        copy.name = part.name + (count == 1 ? " (kopia)" : std::format(" (kopia {})", i));
        copy.assembly = core::json::object();
        out.push_back(copy);
    }
    return out;
}

// ------------------------------------------------------------------ przyciąganie

std::optional<Snap> snap_to_points(const core::Project& project, const core::Camera& camera, double sx, double sy, double width, double height, double px) {
    std::optional<Snap> best;
    double best_cost = px * px;
    auto consider = [&](const Vec3& point, const char* label, double bias) {
        Vec3 s = camera.project(point, width, height);
        double cost = (s[0] - sx) * (s[0] - sx) + (s[1] - sy) * (s[1] - sy) + bias * bias;
        if (cost < best_cost) {
            best_cost = cost;
            Snap snap;
            snap.point = point; snap.label = label; snap.snapped = true;
            best = snap;
        }
    };
    for (const auto& part : project.parts) {
        if (!part.visible) continue;
        for (const Face& face : part_faces(part)) {
            Vec3 center{0, 0, 0};
            for (size_t i = 0; i < face.polygon.size(); ++i) {
                const Vec3& a = face.polygon[i];
                const Vec3& b = face.polygon[(i + 1) % face.polygon.size()];
                consider(a, "koniec", 0);
                consider(scale(add(a, b), .5), "środek krawędzi", 2);
                center = add(center, a);
            }
            if (!face.polygon.empty()) consider(scale(center, 1.0 / face.polygon.size()), "środek ściany", 4);
        }
    }
    return best;
}

Snap snap_to_axes(const Vec3& anchor, const Vec3& point, double grid) {
    Vec3 delta = sub(point, anchor);
    double len = length(delta);
    Snap snap;
    snap.point = point;
    if (len > 1e-6) {
        for (int k = 0; k < 3; ++k) {
            double others = std::sqrt(std::max(0.0, len * len - delta[k] * delta[k]));
            if (len > 1 && others < len * std::sin(2 * PI / 180)) {
                Vec3 only{0, 0, 0};
                only[k] = delta[k];
                delta = only;
                snap.label = std::string("oś ") + axis_name(k);
                snap.snapped = true;
                break;
            }
        }
    }
    if (grid > 0) for (int k = 0; k < 3; ++k) delta[k] = std::round(delta[k] / grid) * grid;
    snap.point = add(anchor, delta);
    return snap;
}

double snap_pull(const core::Project& project, size_t part_index, const std::string& face, double distance, double tolerance, std::string* label) {
    if (part_index >= project.parts.size()) fail("Płyta do wyciągania nie istnieje.");
    const core::Part& part = project.parts[part_index];
    int local = face == "top" || face == "bottom" ? 2 : face == "u0" || face == "u1" ? 0 : face == "v0" || face == "v1" ? 1 : -1;
    if (local < 0) return std::round(distance);
    const bool high = face == "top" || face == "u1" || face == "v1";
    int axis = local_axes(part.plane)[(size_t)local];
    Box box = part_box(part);
    double current = high ? box.hi[axis] : box.lo[axis];
    double sign = high ? 1 : -1;
    double proposed = current + sign * distance;
    double best = std::numeric_limits<double>::infinity();
    std::vector<double> candidates{0};
    for (size_t i = 0; i < project.parts.size(); ++i) {
        if (i == part_index || !project.parts[i].visible) continue;
        Box other = part_box(project.parts[i]);
        candidates.push_back(other.lo[axis]);
        candidates.push_back(other.hi[axis]);
    }
    for (double c : candidates)
        if (std::fabs(c - current) > 1e-9 && std::fabs(c - proposed) <= tolerance && std::fabs(c - proposed) < std::fabs(best - proposed)) best = c;
    if (std::isfinite(best)) {
        if (label) *label = std::format("wyrównano do ściany {} = {} mm", axis_name(axis), num(best));
        return sign * (best - current);
    }
    if (label) label->clear();
    return std::round(distance);
}

double snap_coordinate(const core::Project& project, int axis, double proposed, double tolerance, std::optional<size_t> skip, std::string* label) {
    if (axis < 0 || axis > 2) fail("Nieznana oś.");
    double best = std::numeric_limits<double>::infinity();
    auto consider = [&](double c) {
        if (std::fabs(c - proposed) <= tolerance && std::fabs(c - proposed) < std::fabs(best - proposed)) best = c;
    };
    consider(0);
    for (size_t i = 0; i < project.parts.size(); ++i) {
        if ((skip && *skip == i) || !project.parts[i].visible) continue;
        Box other = part_box(project.parts[i]);
        consider(other.lo[axis]);
        consider(other.hi[axis]);
    }
    if (std::isfinite(best)) {
        if (label) *label = std::format("wyrównano do ściany {} = {} mm", axis_name(axis), num(best));
        return best;
    }
    if (label) label->clear();
    return std::round(proposed);
}

double pull_distance(const core::Camera& camera, const Vec3& p, const Vec3& n, double dx_px, double dy_px) {
    Vec3 a = camera.project(p, 1000, 1000), b = camera.project(add(p, n), 1000, 1000);
    double sx = b[0] - a[0], sy = b[1] - a[1];
    double denom = sx * sx + sy * sy;
    if (denom < 1e-12) return 0;
    return (dx_px * sx + dy_px * sy) / denom;
}

// ------------------------------------------------------------------ teksty

std::vector<double> parse_numbers(const std::string& text) {
    std::vector<double> values;
    std::string token;
    auto flush = [&] {
        if (token.empty()) return;
        std::string t = token;
        std::replace(t.begin(), t.end(), ',', '.');
        size_t used = 0;
        double value = 0;
        try {
            value = std::stod(t, &used);
        } catch (const std::exception&) {
            fail("„" + token + "” nie jest liczbą. Wpisz wymiary, np. 600x400.");
        }
        if (used != t.size() || !std::isfinite(value)) fail("„" + token + "” nie jest liczbą. Wpisz wymiary, np. 600x400.");
        values.push_back(value);
        token.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        bool separator = c == ';' || c == 'x' || c == 'X' || c == ' ' || c == '\t';
        if (c == 0xC3 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0x97) { separator = true; ++i; }  // „×” w UTF-8
        if (separator) flush();
        else token.push_back((char)c);
    }
    flush();
    if (values.empty()) fail("Wpisz wymiar (np. 600x400 albo 18).");
    return values;
}

std::string suggest_name(const core::Part& part, const std::vector<std::string>& existing) {
    std::string base = part.plane == "YZ" ? "Bok" : part.plane == "XY" ? "Poziom" : "Pion";
    auto taken = [&](const std::string& name) { return std::find(existing.begin(), existing.end(), name) != existing.end(); };
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::string candidate = base + " " + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return base + " " + core::uid().substr(0, 4);
}

std::string describe(const core::Part& part) {
    return std::format("płyta {} × {} × {} mm ({}) w ({}, {}, {})", num(part.u), num(part.v), num(part.t), part.plane, num(part.x), num(part.y), num(part.z));
}

// ------------------------------------------------------------------ profile 2D (CAD) → płyty

namespace {
// Wycięcie przelotowe jako wpis obróbki płyty (współrzędne lokalne płyty wyznaczone z jej położenia w świecie).
core::json cutout_json(const core::Part& part, const Plane& plane, const Cutout& cutout) {
    Vec3 origin = core::world(part, 0, 0, 0);
    Vec3 axis_u = unit(sub(core::world(part, 1, 0, 0), origin)), axis_v = unit(sub(core::world(part, 0, 1, 0), origin));
    Vec3 center = from_plane(plane, cutout.center[0], cutout.center[1]);
    double u = dot(sub(center, origin), axis_u), v = dot(sub(center, origin), axis_v);
    double length = cutout.length, width = cutout.width, angle = 0;
    if (cutout.shape == "circle") {
        if (!(cutout.width >= .5)) fail(std::format("Średnica otworu {} mm jest za mała (minimum 0,5 mm).", num(cutout.width)));
        length = width = cutout.width;
    } else if (cutout.shape == "rect") {
        if (!(cutout.length >= .5) || !(cutout.width >= .5)) fail(std::format("Wycięcie {} × {} mm jest za małe (minimum 0,5 mm).", num(cutout.length), num(cutout.width)));
        // osie płaszczyzny rysowania są osiami świata, więc prostokąt zostaje równoległy do osi płyty — zamieniają się najwyżej długość z szerokością
        double lu = std::fabs(dot(plane.u, axis_u)) * cutout.length + std::fabs(dot(plane.v, axis_u)) * cutout.width;
        double lv = std::fabs(dot(plane.u, axis_v)) * cutout.length + std::fabs(dot(plane.v, axis_v)) * cutout.width;
        length = lu; width = lv;
    } else if (cutout.shape == "slot") {
        if (!(cutout.width >= .5) || !(cutout.length >= cutout.width)) fail(std::format("Szczelina {} × {} mm jest nieprawidłowa (długość nie może być mniejsza od szerokości).", num(cutout.length), num(cutout.width)));
        double a = cutout.angle * PI / 180;
        Vec3 d = add(scale(plane.u, std::cos(a)), scale(plane.v, std::sin(a)));
        angle = std::atan2(dot(d, axis_v), dot(d, axis_u)) * 180 / PI;
        if (angle > 360) angle -= 360;
    } else {
        fail("Nieznany rodzaj wycięcia „" + cutout.shape + "”.");
    }
    return core::json{{"type", "cutout"}, {"shape", cutout.shape}, {"face", "top"}, {"u", u}, {"v", v}, {"length", length}, {"width", width}, {"depth", part.t},
                      {"angle", angle}, {"purpose", "wycięcie z rysunku 2D"}};
}
}  // namespace

core::Part board_from_profile(const Plane& plane, const std::vector<Vec3>& outer, const std::vector<Cutout>& cutouts, double thickness, const std::string& name,
                              const std::string& group, const std::string& material) {
    core::Part part = board_from_polygon(plane, outer, thickness, name, group, material);
    for (const auto& cutout : cutouts) part.machining.push_back(cutout_json(part, plane, cutout));
    return part;
}

Plane face_plane(const core::Part& part, const std::string& face) {
    for (const auto& f : part_faces(part))
        if (f.id == face) return f.plane;
    fail("Płyta „" + part.name + "” nie ma ściany „" + face + "”.");
}

std::vector<Cutout> cutouts_of(const core::Part& part, const Plane& top_plane) {
    std::vector<Cutout> out;
    if (!part.machining.is_array()) return out;
    Vec3 origin = core::world(part, 0, 0, 0);
    Vec3 axis_u = unit(sub(core::world(part, 1, 0, 0), origin)), axis_v = unit(sub(core::world(part, 0, 1, 0), origin));
    for (const auto& cut : part.machining) {
        if (cut.value("type", "") != "cutout") continue;
        double u = cut.at("u").get<double>(), v = cut.at("v").get<double>();
        Vec3 world_center = add(origin, add(scale(axis_u, u), scale(axis_v, v)));
        Cutout c;
        c.shape = cut.value("shape", "rect");
        c.center = to_plane(top_plane, world_center);
        double length = cut.value("length", 0.0), width = cut.value("width", 0.0), angle = cut.value("angle", 0.0);
        if (c.shape == "circle") { c.width = width; c.length = width; }
        else if (c.shape == "rect") {
            if (std::fabs(angle) > 1e-6) continue;  // obrócony prostokąt nie ma odpowiednika w edytorze — zostaje w obróbce płyty
            // długość wzdłuż lokalnego u: przeliczenie na osie płaszczyzny rysowania
            double lp = std::fabs(dot(top_plane.u, axis_u)) * length + std::fabs(dot(top_plane.u, axis_v)) * width;
            double wp = std::fabs(dot(top_plane.v, axis_u)) * length + std::fabs(dot(top_plane.v, axis_v)) * width;
            c.length = lp; c.width = wp;
        } else {
            Vec3 d = add(scale(axis_u, std::cos(angle * PI / 180)), scale(axis_v, std::sin(angle * PI / 180)));
            c.length = length; c.width = width;
            c.angle = std::atan2(dot(d, top_plane.v), dot(d, top_plane.u)) * 180 / PI;
        }
        out.push_back(c);
    }
    return out;
}

core::Part reshape_part(const core::Part& part, const Plane& top_plane, const std::vector<Vec3>& outer, const std::vector<Cutout>& cutouts) {
    if (!axis_aligned(part)) fail("Płyta „" + part.name + "” jest obrócona — kontur można zmieniać tylko w płytach równoległych do osi. Ustaw obrót w inspektorze.");
    if (part.profile.is_object() && !part.profile.empty()) fail("Płyta „" + part.name + "” ma kształt parametryczny (" + part.profile.value("kind", std::string("profil")) + ") — zmień go w oknie kształtów, a nie rysunkiem.");
    if (outer.size() < 3) fail("Kontur wymaga przynajmniej trzech punktów.");
    Vec3 origin = core::world(part, 0, 0, 0);
    Vec3 axis_u = unit(sub(core::world(part, 1, 0, 0), origin)), axis_v = unit(sub(core::world(part, 0, 1, 0), origin));
    std::vector<Point> local;
    double min_u = 1e18, min_v = 1e18, max_u = -1e18, max_v = -1e18;
    for (const auto& p : outer) {
        Point l{dot(sub(p, origin), axis_u), dot(sub(p, origin), axis_v)};
        local.push_back(l);
        min_u = std::min(min_u, l[0]); min_v = std::min(min_v, l[1]); max_u = std::max(max_u, l[0]); max_v = std::max(max_v, l[1]);
    }
    core::Part result = part;
    Vec3 shift = add(scale(axis_u, min_u), scale(axis_v, min_v));
    result.x += shift[0]; result.y += shift[1]; result.z += shift[2];
    std::vector<std::vector<double>> contour;
    for (const auto& l : local) contour.push_back({l[0] - min_u, l[1] - min_v});
    bool rectangle = contour.size() == 4;
    if (rectangle)
        for (const auto& p : contour) {
            bool ox = std::fabs(p[0]) < 1e-6 || std::fabs(p[0] - (max_u - min_u)) < 1e-6, oy = std::fabs(p[1]) < 1e-6 || std::fabs(p[1] - (max_v - min_v)) < 1e-6;
            if (!ox || !oy) rectangle = false;
        }
    core::validate_polygon(contour);  // samoprzecięcia i zerowe pole zgłaszane z powodem
    result.u = max_u - min_u; result.v = max_v - min_v;
    if (rectangle) result.contour.clear();
    else {
        result.contour = contour;
        std::fill(result.edges.begin(), result.edges.end(), false);   // obrzeża tylko dla prostokąta
        result.contour_edge = false;
    }
    result.fillets = core::json::array();
    result.chamfers = core::json::array();
    auto move_uv = [&](core::json& item) {
        if (item.is_object() && item.contains("u") && item.contains("v") && item["u"].is_number() && item["v"].is_number()) {
            item["u"] = item["u"].get<double>() - min_u;
            item["v"] = item["v"].get<double>() - min_v;
        }
    };
    core::json machining = core::json::array();
    if (part.machining.is_array())
        for (auto cut : part.machining) {
            if (cut.value("type", "") == "cutout" && (cut.value("shape", "") != "rect" || std::fabs(cut.value("angle", 0.0)) < 1e-6)) continue;   // wycięcia przejmują kształty z edytora
            move_uv(cut);
            machining.push_back(cut);
        }
    for (auto& hole : result.holes) { if (hole.size() >= 2) { hole[0] -= min_u; hole[1] -= min_v; } }
    if (result.operations.is_array()) for (auto& op : result.operations) move_uv(op);
    result.machining = machining;
    for (const auto& cutout : cutouts) result.machining.push_back(cutout_json(result, top_plane, cutout));
    return result;
}

std::pair<double, double> camera_angles_for_plane(const Plane& plane) {
    double best_score = -1e18;
    std::pair<double, double> best{0, 90};
    for (double pitch : {90.0, -90.0, 0.0})
        for (double yaw : {0.0, 90.0, 180.0, -90.0}) {
            core::Camera camera(yaw, pitch, 1);
            Ray ray = unproject(camera, 100, 100, 200, 200);
            if (std::fabs(dot(ray.dir, plane.n)) < 0.99) continue;   // kamera patrzy wprost na płaszczyznę (z którejkolwiek strony)
            core::Point3 o = camera.project(plane.origin, 200, 200);
            core::Point3 pu = camera.project(add(plane.origin, plane.u), 200, 200);
            core::Point3 pv = camera.project(add(plane.origin, plane.v), 200, 200);
            double score = (pu[0] - o[0]) - (pv[1] - o[1]);   // u w prawo, v w górę (y ekranu rośnie w dół)
            if (score > best_score + 1e-9) { best_score = score; best = {yaw, pitch}; }
        }
    return best;
}

}  // namespace modeler_geometry
