// Manufacturing geometry descriptions in a part's local coordinates.
#include "features.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "assembly.h"

namespace features {

using core::Part;
using core::Point;
using core::Point3;
using core::finite;
using core::inside;
using core::uid;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> FACES{
    {"Góra (+T)", "top"}, {"Dół (−T)", "bottom"}, {"Krawędź U=0", "u0"}, {"Krawędź U=max", "u1"}, {"Krawędź V=0", "v0"}, {"Krawędź V=max", "v1"}};

namespace {
constexpr double TAU = 2 * std::numbers::pi;
double num(const json& value) { return value.get<double>(); }
bool is_face(const json& face) {
    return std::any_of(FACES.begin(), FACES.end(), [&](const auto& item) { return face == item.second; });
}
}  // namespace

void validate_drill(const Part& part, const json& op) {
    json face = py::get(op, "face");
    if (py::get(op, "type") != "drill" || !is_face(face)) throw ValueError("Nieprawidłowy typ lub strona wiercenia.");
    double a = finite(op.at("u"), 0, 10000), b = finite(op.at("v"), 0, 10000), d = finite(op.at("diameter"), 0, 10000), h = finite(op.at("depth"), 0, 10000);
    if (d <= 0 || h <= 0) throw ValueError("Średnica i głębokość muszą być dodatnie.");
    if (py::truthy(py::get(op, "countersink_diameter"))) {
        double outer = finite(op["countersink_diameter"], d, 10000); finite(py::get(op, "countersink_depth", 0), .1, h);
        d = outer;
    }
    std::map<std::string, std::array<double, 3>> limits{{"top", {part.u, part.v, part.t}}, {"bottom", {part.u, part.v, part.t}},
        {"u0", {part.v, part.t, part.u}}, {"u1", {part.v, part.t, part.u}},
        {"v0", {part.u, part.t, part.v}}, {"v1", {part.u, part.t, part.v}}};
    const auto& limit = limits.at(face.get<std::string>());
    if (!(d / 2 <= a && a <= limit[0] - d / 2) || !(d / 2 <= b && b <= limit[1] - d / 2) || h > limit[2])
        throw ValueError("Wiercenie wykracza poza powierzchnię lub grubość w kierunku wiercenia.");
    if (face == "top" || face == "bottom") {
        auto points = part.points();
        for (int i = 0; i < 72; ++i) {
            if (!inside({a + d / 2 * std::cos(i * TAU / 72), b + d / 2 * std::sin(i * TAU / 72)}, points))
                throw ValueError("Wiercenie wykracza poza kontur.");
        }
    }
}

std::pair<Point3, Point3> drill_location(const Part& part, const json& op) {
    double a = num(op.at("u")), b = num(op.at("v")); std::string face = op.at("face");
    std::map<std::string, std::pair<Point3, Point3>> locations{{"top", {{a, b, part.t}, {0, 0, -1}}}, {"bottom", {{a, b, 0}, {0, 0, 1}}},
        {"u0", {{0, a, b}, {1, 0, 0}}}, {"u1", {{part.u, a, b}, {-1, 0, 0}}},
        {"v0", {{a, 0, b}, {0, 1, 0}}}, {"v1", {{a, part.v, b}, {0, -1, 0}}}};
    return locations.at(face);
}

void validate_cut(const Part& part, const json& cut) {
    json type = py::get(cut, "type"), shape = py::get(cut, "shape");
    if ((type != "pocket" && type != "cutout" && type != "groove") || (shape != "rect" && shape != "circle" && shape != "slot"))
        throw ValueError("Obróbka: kieszeń, otwór przelotowy lub rowek; kształt prostokątny, kołowy lub podłużny.");
    if (py::get(cut, "face") != "top" && py::get(cut, "face") != "bottom") throw ValueError("Frezowanie obsługuje powierzchnie +T i −T.");
    double u = finite(cut.at("u"), 0, 10000), v = finite(cut.at("v"), 0, 10000);
    double length = finite(cut.at("length"), .1, 10000), width = finite(cut.at("width"), .1, 10000);
    double depth = cut["type"] == "cutout" ? part.t : finite(cut.at("depth"), .1, part.t);
    if (cut["type"] != "cutout" && depth >= part.t) throw ValueError("Kieszeń i rowek muszą mieć głębokość mniejszą od grubości. Dla otworu wybierz wycięcie przelotowe.");
    if (cut["shape"] == "circle") length = width;
    if (cut["shape"] == "slot" && length < width) throw ValueError("Długość otworu podłużnego musi być co najmniej równa jego szerokości.");
    std::vector<Point> samples;
    if (cut["shape"] == "circle") {
        for (int i = 0; i < 72; ++i) samples.push_back({u + width / 2 * std::cos(i * TAU / 72), v + width / 2 * std::sin(i * TAU / 72)});
    } else for (auto [du, dv] : std::vector<std::pair<int, int>>{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) samples.push_back({u + du * length / 2, v + dv * width / 2});
    double angle = finite(py::get(cut, "angle", 0), -360, 360) * (std::numbers::pi / 180);
    for (auto& [x, y] : samples) {
        double rx = u + (x - u) * std::cos(angle) - (y - v) * std::sin(angle), ry = v + (x - u) * std::sin(angle) + (y - v) * std::cos(angle);
        x = rx; y = ry;
    }
    for (auto& [x, y] : samples)
        if (!(-1e-7 <= x && x <= part.u + 1e-7) || !(-1e-7 <= y && y <= part.v + 1e-7)) throw ValueError("Kontur obróbki wykracza poza wymiar formatki.");
    auto points = part.points();
    for (auto& point : samples) if (!inside(point, points)) throw ValueError("Obróbka wykracza poza kontur części.");
}

json hole_array(const Part& part, double u, double v, double diameter, double depth, const json& nu_, const json& nv_, double du, double dv, const std::string& face) {
    long long nu = (long long)finite(nu_, 1, 100), nv = (long long)finite(nv_, 1, 100);
    if (nu * nv > 500) throw ValueError("Maksymalnie 500 otworów w jednym szyku.");
    std::string batch = uid(); json operations = json::array();
    for (long long j = 0; j < nv; ++j) {
        for (long long i = 0; i < nu; ++i) {
            json op = {{"type", "drill"}, {"u", u + i * du}, {"v", v + j * dv}, {"diameter", diameter}, {"depth", depth}, {"face", face}, {"batch", batch}, {"purpose", "szyk ręczny"}};
            validate_drill(part, op); operations.push_back(op);
        }
    }
    return operations;
}

core::ProjectPtr replace_part(const core::Project& project, const Part& part) {
    auto result = std::make_shared<core::Project>(project);
    auto found = std::find_if(result->parts.begin(), result->parts.end(), [&](const Part& p) { return p.id == part.id; });
    if (found == result->parts.end()) throw py::StopIteration();
    *found = part; result = assembly::rebuild_dependents(*result, part.id); result->validate(); return result;
}

void shift_features(Part& part, double du, double dv, double dt) {
    // Keep drilling coordinates attached to material when a local origin moves.
    for (auto& op : part.operations) {
        std::string face = op.at("face");
        auto [a, b] = face == "top" || face == "bottom" ? std::make_pair(du, dv) : face.starts_with("u") ? std::make_pair(dv, dt) : std::make_pair(du, dt);
        op["u"] = num(op["u"]) - a; op["v"] = num(op["v"]) - b;
    }
    for (auto& cut : part.machining) { cut["u"] = num(cut.at("u")) - du; cut["v"] = num(cut.at("v")) - dv; }
}

// Verified C++ geometry calculations; exact CAD validation is retained.
// (W Pythonie: native_model.install_shapes(globals(), 'features') — tu bez odpowiednika.)

}  // namespace features
