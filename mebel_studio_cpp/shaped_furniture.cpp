// Persistent curved outlines and dimension-driven furniture starting points.
#include "shaped_furniture.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <set>

#include "assembly.h"
#include "dimension_model.h"
#include "features.h"
#include "fittings.h"
#include "furniture_library.h"
#include "opening_options.h"

namespace shaped_furniture {

using core::Material;
using core::Part;
using core::Point;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> SHAPES{
    {"Prostokąt", "rectangle"}, {"Prostokąt — cztery promienie", "rounded"}, {"Elipsa / owal", "ellipse"}, {"Koło", "circle"},
    {"Kapsuła / obły blat", "capsule"}, {"Łuk na górnej krawędzi", "arch"}, {"Trapez / zwężana noga", "taper"}, {"Półkole / półelipsa", "half_ellipse"}};
const std::vector<std::pair<std::string, std::vector<int>>> THICKNESSES{
    {"Płyta meblowa", {10, 12, 16, 18, 19, 22, 25, 28}}, {"MDF", {6, 8, 10, 12, 16, 18, 19, 22, 25, 30}}, {"Sklejka", {4, 6, 9, 12, 15, 18, 21, 24, 30}},
    {"Drewno lite", {20, 25, 28, 30, 35, 40, 45, 50, 60}}, {"HDF / plecy", {3, 4, 6}}, {"Blat", {28, 38, 40, 50}}};
const std::string CURVE_NOTE =
    "Szkic, podgląd i DXF pokazują łuki jako odcinki. STEP zachowuje dokładne łuki kołowe i eliptyczne. Liczba odcinków zmienia podgląd, "
    "a parametry obrysu pozostają zapisane. Promienie dotyczą obrysu w U/V, a zaokrąglenia kantów 3D edytuje się narzędziem CAD.";

namespace {

constexpr double TAU = 2 * std::numbers::pi;
constexpr double PI = std::numbers::pi;
double radians(double degrees) { return degrees * (PI / 180.0); }
double degrees(double radians_) { return radians_ * (180.0 / PI); }

int resolution(const json& value) {
    double n = finite(value, 4, 24);
    if (std::trunc(n) != n) throw ValueError("Liczba odcinków ćwiartki musi być całkowita: 4–24.");
    return (int)n;
}

// Remove coincident tangencies and pin floating point cardinal extrema.
std::vector<std::vector<double>> finish(const std::vector<Point>& points, double u, double v) {
    std::vector<std::vector<double>> clean;
    for (auto [x, y] : points) {
        x = std::min(u, std::max(0., py::round_to(x, 9)));
        y = std::min(v, std::max(0., py::round_to(y, 9)));
        if (std::abs(x - u) < 1e-7) x = u;
        if (std::abs(y - v) < 1e-7) y = v;
        if (clean.empty() || std::hypot(clean.back()[0] - x, clean.back()[1] - y) > 1e-7) clean.push_back({x, y});
    }
    if (clean.size() > 1 && std::hypot(clean.front()[0] - clean.back()[0], clean.front()[1] - clean.back()[1]) <= 1e-7) clean.pop_back();
    // At submillimetre scale a sampled curve may create forbidden tiny edges.
    // Report this rather than silently changing its radius or outline.
    auto validated = core::validate_polygon(clean);
    double min_x = validated[0][0], min_y = validated[0][1];
    for (const auto& p : validated) { min_x = std::min(min_x, p[0]); min_y = std::min(min_y, p[1]); }
    if (min_x != 0 || min_y != 0) throw ValueError("Kontur jest zbyt mały dla podanej liczby odcinków.");
    std::vector<std::vector<double>> out;
    for (const auto& p : validated) out.push_back({p[0], p[1]});
    return out;
}

// contour(kind, u, v, radii=(30,30,30,30), segments=12, arch_rise=None, taper=10)
struct Options {
    json radii = json::array({30, 30, 30, 30});
    json segments = 12;
    json arch_rise = nullptr;
    json taper = 10;
};

Options options_from(const json& kwargs) {
    Options o;
    for (auto& [key, value] : kwargs.items()) {
        if (key == "radii") o.radii = value;
        else if (key == "segments") o.segments = value;
        else if (key == "arch_rise") o.arch_rise = value;
        else if (key == "taper") o.taper = value;
        else py::type_error("contour() got an unexpected keyword argument '" + key + "'");
    }
    return o;
}

std::vector<std::vector<double>> contour_of(const json& kind_, double u_, double v_, const Options& o) {
    std::string kind = kind_.is_string() ? kind_.get<std::string>() : std::string();
    double u = finite(u_, .1, 10000), v = finite(v_, .1, 10000);
    int n = resolution(o.segments);
    if (kind == "rectangle") return {};
    if (kind == "circle") {
        if (std::abs(u - v) > 1e-7) throw ValueError("Koło wymaga jednakowej szerokości i wysokości U/V.");
        kind = "ellipse";
    }
    std::vector<Point> points;
    if (kind == "ellipse") {
        for (int i = 0; i < 4 * n; ++i) points.push_back({u / 2 + u / 2 * std::cos(i * TAU / (4 * n)), v / 2 + v / 2 * std::sin(i * TAU / (4 * n))});
    } else if (kind == "rounded" || kind == "capsule") {
        json radii = o.radii;
        if (kind == "capsule") radii = json::array({std::min(u, v) / 2, std::min(u, v) / 2, std::min(u, v) / 2, std::min(u, v) / 2});
        if (radii.size() != 4) throw ValueError("Podaj osobny promień dla czterech narożników.");
        std::array<double, 4> r{};
        for (int i = 0; i < 4; ++i) r[i] = finite(radii[i], 0, std::min(u, v));
        for (auto [a, b, length] : std::vector<std::array<double, 3>>{{0, 1, u}, {1, 2, v}, {2, 3, u}, {3, 0, v}})
            if (r[(int)a] + r[(int)b] > length + 1e-7) throw ValueError("Promienie sąsiednich narożników przekraczają długość krawędzi.");
        struct Corner { double radius, cx, cy, start; Point corner; };
        for (const Corner& c : std::vector<Corner>{{r[0], r[0], r[0], PI, {0, 0}}, {r[1], u - r[1], r[1], PI * 1.5, {u, 0}},
                                                   {r[2], u - r[2], v - r[2], 0, {u, v}}, {r[3], r[3], v - r[3], PI / 2, {0, v}}}) {
            if (c.radius == 0) points.push_back(c.corner);
            else for (int i = 0; i <= n; ++i) points.push_back({c.cx + c.radius * std::cos(c.start + i * PI / (2 * n)), c.cy + c.radius * std::sin(c.start + i * PI / (2 * n))});
        }
    } else if (kind == "arch" || kind == "half_ellipse") {
        double rise = kind == "half_ellipse" ? v : finite(!o.arch_rise.is_null() ? o.arch_rise : json(std::min(v / 3, u / 2)), .1, v);
        points = {{0, 0}, {u, 0}};
        if (rise < v) points.push_back({u, v - rise});
        for (int i = 0; i <= 2 * n; ++i) points.push_back({u / 2 + u / 2 * std::cos(i * PI / (2 * n)), v - rise + rise * std::sin(i * PI / (2 * n))});
        if (rise < v) points.push_back({0, v - rise});
    } else if (kind == "taper") {
        double inset = finite(o.taper, 0, u / 2 - .01);
        // The narrower bottom is centered; dimensions still include full top width.
        points = {{inset, 0}, {u - inset, 0}, {u, v}, {0, v}};
    } else throw ValueError("Nieznany kształt konturu.");
    return finish(points, u, v);
}

double ellipse_perimeter(double a, double b) {
    // Simpson quadrature avoids a new scipy dependency and is accurate even for
    // elongated furniture profiles; CAD supplies exact analytic geometry.
    const int n = 512;
    double step = PI / (2 * n);
    std::vector<double> values;
    for (int i = 0; i <= n; ++i) values.push_back(std::hypot(a * std::sin(i * step), b * std::cos(i * step)));
    std::vector<double> odd, even;
    for (int i = 1; i < n; i += 2) odd.push_back(values[i]);
    for (int i = 2; i < n; i += 2) even.push_back(values[i]);
    return 4 * step / 3 * (values[0] + values[n] + 4 * py::sum(odd) + 2 * py::sum(even));
}

std::vector<double> radii_of(const json& profile, const std::string& kind, double u, double v) {
    std::vector<double> r;
    if (kind == "rounded") for (const auto& x : profile.at("radii")) r.push_back(x.get<double>());
    else r = {std::min(u, v) / 2, std::min(u, v) / 2, std::min(u, v) / 2, std::min(u, v) / 2};
    return r;
}

Part& find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return p;
    throw py::StopIteration();
}

}  // namespace

std::vector<std::vector<double>> contour(const json& kind, double u, double v, const json& options) { return contour_of(kind, u, v, options_from(options)); }

json shape_definition(const json& kind, double u, double v, const json& options) {
    Options o = options_from(options);
    if (o.radii.size() != 4) throw ValueError("Obrys wymaga czterech wartości promieni.");
    json radii = json::array();
    for (const auto& x : o.radii) radii.push_back(finite(x, 0, 10000));
    double arch_rise = finite(o.arch_rise.is_null() ? json(std::min(v / 3, u / 2)) : o.arch_rise, 0, 10000);
    double taper = finite(o.taper, 0, 10000);
    Options check = o;
    check.radii = radii; check.arch_rise = arch_rise; check.taper = taper;
    contour_of(kind, u, v, check);
    return json{{"kind", kind}, {"radii", radii}, {"segments", resolution(o.segments)}, {"arch_rise", arch_rise}, {"taper", taper}};
}

void resize_profile(Part& part, double u, double v) {
    if (py::truthy(part.profile)) {
        json opts = json::object();
        for (auto& [k, value] : part.profile.items()) if (k != "kind") opts[k] = value;
        json kind = part.profile.at("kind");
        part.profile = shape_definition(kind, u, v, opts);
        part.contour = contour(kind, u, v, opts);
    } else if (!part.contour.empty()) {
        for (auto& p : part.contour) { double a = p[0], b = p[1]; p = {a * u / part.u, b * v / part.v}; }
    }
}

double profile_area(const json& profile, double u, double v) {
    std::string kind = profile.at("kind");
    if (kind == "ellipse" || kind == "circle") return PI * u * v / 4;
    if (kind == "half_ellipse") return PI * u * v / 4;
    if (kind == "arch") { double rise = profile.at("arch_rise"); return u * (v - rise) + PI * u * rise / 4; }
    if (kind == "rounded" || kind == "capsule") {
        std::vector<double> squares;
        for (double x : radii_of(profile, kind, u, v)) squares.push_back(x * x);
        return u * v - (1 - PI / 4) * py::sum(squares);
    }
    if (kind == "taper") return v * (u - profile.at("taper").get<double>());
    return u * v;
}

double profile_perimeter(const json& profile, double u, double v) {
    std::string kind = profile.at("kind");
    if (kind == "ellipse" || kind == "circle") return ellipse_perimeter(u / 2, v / 2);
    if (kind == "half_ellipse") return u + ellipse_perimeter(u / 2, v) / 2;
    if (kind == "arch") { double rise = profile.at("arch_rise"); return u + 2 * (v - rise) + ellipse_perimeter(u / 2, rise) / 2; }
    if (kind == "rounded" || kind == "capsule") return 2 * (u + v) + (PI / 2 - 2) * py::sum(radii_of(profile, kind, u, v));
    if (kind == "taper") { double inset = profile.at("taper"); return 2 * u - 2 * inset + 2 * std::hypot(v, inset); }
    return 2 * (u + v);
}

void refresh_decor(Part& part) {
    json cfg = part.decor;
    if (py::get(cfg, "type") != "fluting") throw ValueError("Nieznana dekoracja parametryczna.");
    double width = finite(py::get(cfg, "width", 8), .5, 1000), pitch = finite(py::get(cfg, "pitch", 24), width + .5, 1000);
    double depth = finite(py::get(cfg, "depth", 3), .1, part.t - .1);
    double margin_u = finite(py::get(cfg, "margin_u", 30), 0, part.u / 2);
    double margin_v = finite(py::get(cfg, "margin_v", 20), 0, part.v / 2 - .01);
    json face = py::get(cfg, "face", "bottom");
    if (face != "top" && face != "bottom") throw ValueError("Ryflowanie wymaga górnej lub dolnej powierzchni.");
    double usable = part.u - 2 * margin_u;
    if (usable < width || part.v - 2 * margin_v < .1) throw ValueError("Za mały element dla marginesów ryflowania.");
    int count = (int)((usable - width) / pitch) + 1;
    json manual = json::array();
    for (const auto& c : part.machining) if (py::get(c, "generated_decor") != "fluting") manual.push_back(c);
    if ((int)manual.size() + count > 200) throw ValueError("Ryflowanie przekracza 200 operacji. Zwiększ podziałkę.");
    double start = (part.u - (count - 1) * pitch) / 2;
    json machining = manual;
    for (int k = 0; k < count; ++k)
        machining.push_back(json{{"type", "groove"}, {"shape", "rect"}, {"face", face}, {"u", start + k * pitch}, {"v", part.v / 2}, {"length", width},
                                 {"width", part.v - 2 * margin_v}, {"depth", depth}, {"angle", 0}, {"generated_decor", "fluting"}, {"purpose", "Ryflowanie parametryczne"}});
    part.machining = machining;
    part.decor = json{{"type", "fluting"}, {"width", width}, {"pitch", pitch}, {"depth", depth}, {"margin_u", margin_u}, {"margin_v", margin_v}, {"face", face}};
}

void remove_decor(Part& part) {
    json kept = json::array();
    for (const auto& c : part.machining) if (py::get(c, "generated_decor") != "fluting") kept.push_back(c);
    part.machining = kept;
    part.decor = json::object();
}

ProjectPtr apply_shape(const Project& project, const std::string& part_id, const std::string& kind, const json& u_, const json& v_, const json& thickness,
                       const json& options) {
    const Part* found = nullptr;
    for (const auto& p : project.parts) if (p.id == part_id) { found = &p; break; }
    if (found == nullptr) throw ValueError("Element usunięto z projektu. Otwórz edytor dla istniejącego elementu.");
    const Part& p = *found;
    if (py::truthy(py::get(p.assembly, "generated_by"))) throw ValueError("Skrzynkę szuflady zmieniaj przez jej front i parametry prowadnic.");
    if (py::truthy(p.fillets) || py::truthy(p.chamfers))
        throw ValueError("Przed zmianą obrysu usuń fazy i zaokrąglenia kantów w edytorze CAD; numeracja krawędzi zmieni się.");
    double u = finite(u_.is_null() ? json(p.u) : u_, .1, 10000);
    double v = finite(v_.is_null() ? json(p.v) : v_, .1, 10000);
    double t = finite(thickness.is_null() ? json(p.t) : thickness, .1, 10000);
    auto new_contour = contour(kind, u, v, options);
    // Shared dimension logic regenerates linked holes and handle positions first.
    Project intermediate = project;
    find_part(intermediate, part_id).profile = json::object();
    ProjectPtr result = dimension_model::resize_dimensions(intermediate, part_id, u, v, t);
    Part q = find_part(*result, part_id);
    q.profile = kind != "rectangle" ? shape_definition(kind, u, v, options) : json::object();
    q.contour = new_contour;
    if (!new_contour.empty()) q.edges = {false, false, false, false};
    q.validate();
    return features::replace_part(*result, q);
}

const std::vector<Template> TEMPLATES{
    {"Jadalnia", "Stół obły — cztery nogi", 1600, 760, 850, "rounded_table", "Nowoczesny", "Obły blat, cztery okrągłe nogi oraz rama pod blatem."},
    {"Salon", "Stolik owalny — dwa postumenty", 1100, 440, 600, "oval_table", "Organiczny", "Eliptyczny blat i półka oraz dwie okrągłe podpory."},
    {"Biuro", "Biurko z obłym blatem", 1300, 760, 650, "rounded_desk", "Skandynawski", "Osobne promienie narożników i nogi z trapezowym profilem."},
    {"Jadalnia", "Krzesło drewniane z łukiem", 480, 840, 460, "chair", "Drewno lite", "Obłe siedzisko, łuk oparcia, okrągłe nogi i belki łączące."},
    {"Kuchnia", "Taboret okrągły", 400, 460, 400, "stool", "Drewno lite", "Okrągłe siedzisko, trzy nogi i belki łączące."},
    {"Przedpokój", "Konsola obła", 1100, 780, 380, "console", "Japandi", "Blat kapsułowy, dwie podpory z łukiem i dolna półka."},
    {"Sypialnia", "Toaletka z lustrem łukowym", 1000, 1400, 450, "vanity", "Art déco", "Obły blat z czterema nogami oraz lustro z łukowym obrysem."},
    {"Salon", "Komoda z ryflowanymi frontami", 1200, 820, 430, "slatted", "Japandi", "Korpus z dwoma frontami, obły blat i pionowe frezowane rowki."},
    {"Sypialnia", "Szafka nocna — obły blat", 450, 520, 400, "nightstand", "Nowoczesny", "Korpus z zawiasami i uchwytem oraz obły nakładany blat."},
    {"Ogród", "Ławka z łukowym oparciem", 1400, 850, 500, "bench", "Drewno lite", "Obłe siedzisko, łukowy profil oparcia i okrągłe nogi."},
    {"Łazienka", "Lustro łukowe na podkładzie", 650, 1000, 24, "mirror", "Art déco", "Dekoracyjny model lustra i obłego podkładu bez okuć do zawieszenia."},
    {"Salon", "Stolik przyścienny półowalny", 900, 750, 380, "half_table", "Organiczny", "Półeliptyczny blat, dwie tylne nogi i noga z przodu."}};

std::string source_signature(const Part& part, const json& origin) {
    json contour_json = json::array();
    for (const auto& p : part.contour) contour_json.push_back(json(p));
    json position = json::array();
    const double pos[3] = {part.x, part.y, part.z};
    for (int i = 0; i < 3; ++i) position.push_back(py::round_to(pos[i] - origin.at(i).get<double>(), 7));
    json data = json{{"size", json::array({part.u, part.v, part.t})}, {"plane", part.plane}, {"rotation", json::array({part.rx, part.ry, part.rz})},
                     {"position", position}, {"contour", contour_json}, {"profile", part.profile}, {"role", part.role}};
    return py::sha256_hex(py::dumps(data, true, -1, true));
}

std::pair<ProjectPtr, std::string> create_shaped_furniture(const Project& project, long long index, std::optional<double> width, std::optional<double> height,
                                                           std::optional<double> depth, double thickness_, Point3 origin, const std::optional<std::string>& group_in,
                                                           const std::optional<std::string>& material_in, const std::optional<std::string>& front_material, const json& segments) {
    long long template_index = index;
    if (index < -(long long)TEMPLATES.size() || index >= (long long)TEMPLATES.size()) throw py::IndexError("list index out of range");
    const Template& tpl = TEMPLATES[(size_t)(index < 0 ? index + (long long)TEMPLATES.size() : index)];
    const std::string kind = tpl.kind;
    double w = finite(width ? *width : tpl.width, 1, 10000), h = finite(height ? *height : tpl.height, 1, 10000);
    double d = finite(depth ? *depth : tpl.depth, 1, 10000), t = finite(thickness_, 1, 10000);
    int n = resolution(segments);
    auto result = std::make_shared<Project>(project);
    std::string material;
    if (material_in && !material_in->empty()) material = *material_in;
    else {
        const Material* found = nullptr;
        for (const auto& m : result->materials) if (m.kind == "drewno") { found = &m; break; }
        material = found ? found->id : result->materials.at(0).id;
    }
    {
        bool exists = false;
        for (const auto& m : result->materials) if (m.id == material) exists = true;
        if (!exists) throw ValueError("Wybierz istniejący materiał.");
    }
    std::string base = group_in && !group_in->empty() ? *group_in : tpl.name;
    std::string group = base;
    {
        std::set<std::string> occupied;
        for (const auto& p : result->parts) occupied.insert(p.group);
        int i = 2;
        while (occupied.count(group)) { group = std::format("{} ({})", base, i); ++i; }
    }
    double ox = finite(origin[0]), oy = finite(origin[1]), oz = finite(origin[2]);
    if (std::min(w, d) <= 4 || h <= t) throw ValueError("Mebel musi mieć dodatnie wymiary i miejsce na grubość materiału.");

    auto add = [&](const std::string& label, double u, double v, double thick, const std::string& plane, double x, double y, double z,
                   const std::string& shape = "rectangle", json opts = json::object()) -> Part& {
        json contour_options = opts;
        contour_options["segments"] = n;
        Part p;
        p.name = label; p.group = group; p.u = u; p.v = v; p.t = thick; p.plane = plane; p.x = ox + x; p.y = oy + y; p.z = oz + z; p.material = material;
        p.contour = contour(shape, u, v, contour_options);
        p.profile = shape != "rectangle" ? shape_definition(shape, u, v, contour_options) : json::object();
        p.validate();
        result->parts.push_back(p);
        return result->parts.back();
    };
    auto leg = [&](const std::string& label, double x, double y, double height_, double diameter = 50) { add(label, diameter, diameter, height_, "XY", x, y, 0, "circle"); };
    auto four_round_legs = [&](double height_, double inset, double diameter = 50) {
        if (std::min(w, d) < 2 * (inset + diameter)) throw ValueError("Mebel jest za mały dla czterech nóg; zwiększ szerokość i głębokość.");
        int k = 0;
        for (auto [x, y] : std::vector<std::array<double, 2>>{{inset, inset}, {w - inset - diameter, inset}, {inset, d - inset - diameter}, {w - inset - diameter, d - inset - diameter}}) {
            leg(std::format("Noga okrągła {}", k + 1), x, y, height_, diameter);
            ++k;
        }
    };
    auto frame_rails = [&](double level, double inset = 70, double diameter = 50) {
        double rail_h = std::min(60., std::max(20., h / 12)), rail_t = std::min(t, 25.);
        for (auto [label, y] : std::vector<std::pair<std::string, double>>{{"Rama przednia", inset + diameter / 2 - rail_t / 2}, {"Rama tylna", d - inset - diameter / 2 - rail_t / 2}})
            add(label, w - 2 * inset - diameter, rail_h, rail_t, "XZ", inset + diameter / 2, y, level - rail_h);
        for (auto [label, x] : std::vector<std::pair<std::string, double>>{{"Rama lewa", inset + diameter / 2 - rail_t / 2}, {"Rama prawa", w - inset - diameter / 2 - rail_t / 2}})
            add(label, d - 2 * inset - diameter, rail_h, rail_t, "YZ", x, inset + diameter / 2, level - rail_h);
    };
    auto radii4 = [](double r) { return json::array({r, r, r, r}); };

    if (kind == "rounded_table" || kind == "rounded_desk" || kind == "half_table") {
        double top = h - t, radius = std::min(w, d) * .1;
        std::string shape = kind == "half_table" ? "half_ellipse" : "rounded";
        add(kind != "half_table" ? "Blat obły" : "Blat półowalny", w, d, t, "XY", 0, 0, top, shape, json{{"radii", radii4(radius)}});
        double inset = std::min(70., std::min(w, d) * .12), diameter = std::min(60., std::min(w, d) * .1);
        if (kind == "rounded_desk") {
            int k = 0;
            for (auto [x, y] : std::vector<std::array<double, 2>>{{inset, inset}, {w - inset - diameter, inset}, {inset, d - inset - diameter}, {w - inset - diameter, d - inset - diameter}}) {
                add(std::format("Noga zwężana {}", k + 1), diameter, top, diameter, "XZ", x, y, 0, "taper", json{{"taper", diameter * .18}});
                ++k;
            }
        } else if (kind == "half_table") {
            int k = 0;
            for (auto [x, y] : std::vector<std::array<double, 2>>{{inset, 0}, {w - inset - diameter, 0}, {w / 2 - diameter / 2, d - diameter - 20}}) {
                leg(std::format("Noga okrągła {}", k + 1), x, y, top, diameter);
                ++k;
            }
        } else four_round_legs(top, inset, diameter);
        if (kind != "half_table") frame_rails(top, inset, diameter);
    } else if (kind == "oval_table") {
        add("Blat eliptyczny", w, d, t, "XY", 0, 0, h - t, "ellipse");
        double dia = std::min(d * .38, w * .2), baseheight = h - t;
        int k = 0;
        for (double cx : {w * .28, w * .72}) { leg(std::format("Postument okrągły {}", k + 1), cx - dia / 2, d / 2 - dia / 2, baseheight, dia); ++k; }
        add("Półka owalna", w * .8, d * .8, std::min(t, 22.), "XY", w * .1, d * .1, std::min(h * .2, 80.), "ellipse");
    } else if (kind == "chair") {
        double seat_h = h * .54, seat_t = std::min(t, 35.), diameter = std::min(42., std::min(w, d) * .095), inset = std::min(32., std::min(w, d) * .07);
        if (seat_h <= seat_t + 50 || h - seat_h - seat_t < 100) throw ValueError("Zwiększ wysokość krzesła.");
        add("Siedzisko obłe", w, d, seat_t, "XY", 0, 0, seat_h - seat_t, "rounded", json{{"radii", radii4(std::min(w, d) * .13)}});
        four_round_legs(seat_h - seat_t, inset, diameter);
        frame_rails(seat_h - seat_t, inset, diameter);
        double back_t = std::min(t, 24.), back_h = h - seat_h;
        add("Oparcie łukowe", w - 2 * inset, back_h, back_t, "XZ", inset, d - back_t - inset, seat_h, "arch", json{{"arch_rise", std::min(back_h * .3, w * .25)}});
        // Extend two back supports from seat to near the arch, as separate blanks.
        int k = 0;
        for (double x : {inset, w - inset - diameter}) {
            add(std::format("Słupek oparcia {}", k + 1), diameter, diameter, back_h * .85, "XY", x, d - inset - diameter, seat_h, "circle");
            ++k;
        }
    } else if (kind == "stool") {
        if (std::abs(w - d) > 1e-7) throw ValueError("Taboret okrągły wymaga równej szerokości i głębokości.");
        add("Siedzisko okrągłe", w, d, t, "XY", 0, 0, h - t, "circle");
        double dia = std::min(w * .115, 46.), r = w * .30;
        std::vector<std::array<double, 2>> positions;
        int k = 0;
        for (int angle : {30, 150, 270}) {
            double a = radians(angle), x = w / 2 + r * std::cos(a) - dia / 2, y = d / 2 + r * std::sin(a) - dia / 2;
            leg(std::format("Noga taboretu {}", k + 1), x, y, h - t, dia);
            positions.push_back({x + dia / 2, y + dia / 2});
            ++k;
        }
        // Chord rails are XY blanks with thickness along Z, then rotated in-plane.
        for (size_t i = 0; i < positions.size(); ++i) {
            auto a = positions[i], b = positions[(i + 1) % positions.size()];
            double length = std::hypot(a[0] - b[0], a[1] - b[1]), beam = std::min(t, 25.);
            double angle = degrees(std::atan2(b[1] - a[1], b[0] - a[0])), rad = radians(angle);
            Part& p = add(std::format("Łączyna {}", i + 1), length, beam, beam, "XY", a[0] + beam / 2 * std::sin(rad), a[1] - beam / 2 * std::cos(rad), h * .3);
            p.rz = angle;
        }
    } else if (kind == "console") {
        add("Blat kapsułowy", w, d, t, "XY", 0, 0, h - t, "capsule");
        double support_t = std::min(t, 35.);
        int k = 0;
        for (double x : {w * .22 - support_t / 2, w * .78 - support_t / 2}) {
            add(std::format("Podpora obła {}", k + 1), d * .7, h - t, support_t, "YZ", x, d * .15, 0, "arch", json{{"arch_rise", std::min((h - t) * .15, d * .35)}});
            ++k;
        }
        add("Półka kapsułowa", w * .9, d * .8, std::min(t, 22.), "XY", w * .05, d * .1, h * .15, "capsule");
    } else if (kind == "vanity") {
        double desk_h = std::min(760., h * .58);
        if (h - desk_h < 100) throw ValueError("Toaletka wymaga miejsca na lustro nad blatem.");
        add("Blat toaletki", w, d, t, "XY", 0, 0, desk_h - t, "rounded", json{{"radii", radii4(std::min(w, d) * .1)}});
        four_round_legs(desk_h - t, std::min(w, d) * .1, std::min(50., std::min(w, d) * .12));
        double mirror_h = h - desk_h, mirror_w = std::min(w * .65, mirror_h * 1.2);
        Part& panel_ref = add("Podkład lustra łukowego", mirror_w, mirror_h, std::min(t, 18.), "XZ", (w - mirror_w) / 2, d - 30, desk_h, "arch",
                              json{{"arch_rise", std::min(mirror_h * .35, mirror_w / 2)}});
        double px = panel_ref.x, py_ = panel_ref.y, pz = panel_ref.z;
        std::string mirror_mat;
        for (const auto& m : result->materials) if (m.name == "Lustro — model dekoracyjny") { mirror_mat = m.id; break; }
        if (mirror_mat.empty()) {
            Material m;
            m.name = "Lustro — model dekoracyjny"; m.kind = "inne"; m.color = "#b8dce7"; m.price_m2 = 0;
            mirror_mat = m.id;
            result->materials.push_back(m);
        }
        Part& mirror = add("Lustro łukowe", mirror_w - 20, mirror_h - 20, 4, "XZ", px - ox + 10, py_ - oy - 4, pz - oz + 10, "arch",
                           json{{"arch_rise", std::min((mirror_h - 20) * .35, (mirror_w - 20) / 2)}});
        mirror.material = mirror_mat;
    } else if (kind == "slatted" || kind == "nightstand") {
        long long library_index = kind == "slatted" ? 18 : 11;
        double corpus_t = std::min(t, 22.), body_h = h - t;
        std::string front = front_material ? *front_material : material;
        std::tie(result, group) = furniture_library::create_furniture(*result, library_index, w, body_h, d, corpus_t, {ox, oy, oz}, group, material, front);
        add("Nakładany blat obły", w + 20, d + 20, t, "XY", -10, -10, body_h, "rounded", json{{"radii", radii4(std::min(w, d) * .08)}}).role = "worktop";
        if (kind == "slatted") {
            for (auto& front_part : result->parts) {
                if (!(front_part.group == group && front_part.role == "door")) continue;
                front_part.decor = json{{"type", "fluting"}, {"width", 8}, {"pitch", 24}, {"depth", std::min(3., front_part.t * .2)},
                                        {"margin_u", front_part.u * .1}, {"margin_v", front_part.v * .05}, {"face", "bottom"}};
                refresh_decor(front_part);
            }
        }
    } else if (kind == "bench") {
        double seat_h = std::min(450., h * .54), diameter = std::min(60., d * .13), inset = std::min(55., d * .1);
        add("Siedzisko ławki obłe", w, d, t, "XY", 0, 0, seat_h - t, "rounded", json{{"radii", radii4(std::min(w, d) * .15)}});
        four_round_legs(seat_h - t, inset, diameter);
        frame_rails(seat_h - t, inset, diameter);
        add("Oparcie ławki łukowe", w, h - seat_h, std::min(t, 25.), "XZ", 0, d - t, seat_h, "arch", json{{"arch_rise", std::min((h - seat_h) * .22, w * .15)}});
        int k = 0;
        for (double x : {inset, w - inset - diameter}) { leg(std::format("Słupek oparcia ławki {}", k + 1), x, d - diameter, h - t, diameter); ++k; }
    } else if (kind == "mirror") {
        if (d <= 4) throw ValueError("Lustro z podkładem wymaga głębokości większej niż 4 mm.");
        double rise = std::min(h * .3, w / 2);
        add("Podkład lustra — łuk", w, h, std::min(t, d - 4), "XZ", 0, 4, 0, "arch", json{{"arch_rise", rise}});
        std::string mirror_mat;
        for (const auto& m : result->materials) if (m.name == "Lustro — model dekoracyjny") { mirror_mat = m.id; break; }
        if (mirror_mat.empty()) {
            Material m;
            m.name = "Lustro — model dekoracyjny"; m.kind = "inne"; m.color = "#b8dce7"; m.price_m2 = 0;
            mirror_mat = m.id;
            result->materials.push_back(m);
        }
        Part& mirror = add("Tafla lustra — łuk", w - 20, h - 20, 4, "XZ", 10, 0, 10, "arch", json{{"arch_rise", std::min((h - 20) * .3, (w - 20) / 2)}});
        mirror.material = mirror_mat;
    } else throw ValueError("Nieznany wzorzec mebla obłego.");
    result->validate();
    json source = json{{"index", template_index}, {"width", w}, {"height", h}, {"depth", d}, {"thickness", t}, {"origin", json::array({ox, oy, oz})},
                       {"segments", n}, {"material", material}, {"front_material", front_material && !front_material->empty() ? *front_material : material}};
    for (auto& p : result->parts) {
        if (p.group != group) continue;
        json style = source;
        style["label"] = p.name;
        style["signature"] = source_signature(p, source["origin"]);
        p.style_source = style;
    }
    return {result, group};
}

std::optional<assembly::Frame> styled_bounds(const Project& project, const std::string& group) {
    const Part* part = nullptr;
    for (const auto& p : project.parts) if (p.group == group && py::truthy(p.style_source)) { part = &p; break; }
    if (part == nullptr) return std::nullopt;
    const json& source = part->style_source;
    assembly::Frame f;
    f.lower = {source.at("origin")[0], source.at("origin")[1], source.at("origin")[2]};
    f.upper = {f.lower[0] + source.at("width").get<double>(), f.lower[1] + source.at("depth").get<double>(), f.lower[2] + source.at("height").get<double>()};
    return f;
}

ProjectPtr resize_shaped_furniture(const Project& project, const std::string& group, const json& width, const json& height, const json& depth) {
    std::vector<const Part*> old_parts;
    for (const auto& p : project.parts) if (p.group == group) old_parts.push_back(&p);
    if (old_parts.empty() || std::none_of(old_parts.begin(), old_parts.end(), [](const Part* p) { return py::truthy(p->style_source); })) return nullptr;
    if (std::any_of(old_parts.begin(), old_parts.end(), [](const Part* p) { return !py::truthy(p->style_source); }))
        throw ValueError("Do wzorca dodano własne elementy. Zmieniaj ich wymiary osobno lub utwórz wzorzec ponownie z biblioteki.");
    for (const Part* p : old_parts) {
        const json& source = p->style_source;
        if (json(source_signature(*p, source.at("origin"))) != py::get(source, "signature"))
            throw ValueError("Geometrię wzorca zmieniono ręcznie. Zmiana całego wzorca nadpisałaby szkic lub pozycję; edytuj element osobno albo utwórz nowy wzorzec.");
    }
    json source = old_parts[0]->style_source;
    std::map<std::string, Part> old_by_label;
    for (const Part* p : old_parts) old_by_label[p->style_source.at("label").get<std::string>()] = *p;
    if (old_by_label.size() != old_parts.size()) throw ValueError("Wzorzec zawiera kopie elementów; edytuj je oddzielnie.");
    auto result = std::make_shared<Project>(project);
    std::set<std::string> old_ids;
    for (const Part* p : old_parts) old_ids.insert(p->id);
    auto owned_by_old = [&](const json& h) { json o = py::get(h, "owner_id"); return o.is_string() && old_ids.count(o.get<std::string>()) > 0; };
    json manuals = json::array();
    for (const auto& h : result->hardware) if (owned_by_old(h) && py::truthy(py::get(h, "manual"))) manuals.push_back(h);
    {
        std::vector<Part> kept;
        for (const auto& p : result->parts) if (p.group != group) kept.push_back(p);
        result->parts = kept;
    }
    {
        json kept = json::array();
        for (const auto& h : result->hardware) if (!owned_by_old(h)) kept.push_back(h);
        result->hardware = kept;
    }
    std::string actual;
    std::tie(result, actual) = create_shaped_furniture(*result, source.at("index").get<long long>(), width.get<double>(), height.get<double>(), depth.get<double>(),
        source.at("thickness").get<double>(), {source["origin"][0], source["origin"][1], source["origin"][2]}, group, source.at("material").get<std::string>(),
        source.at("front_material").get<std::string>(), source.at("segments"));
    std::set<std::string> new_names, old_names;
    for (const auto& p : result->parts) if (p.group == actual) new_names.insert(p.name);
    for (const auto& [label, p] : old_by_label) old_names.insert(label);
    if (new_names != old_names) throw ValueError("Nie można dopasować elementów starego wzorca.");
    std::map<std::string, std::string> ids;
    for (auto& p : result->parts) if (p.group == actual) ids[p.id] = old_by_label.at(p.name).id;
    auto mapped = [&](const json& value) { return value.is_string() && ids.count(value.get<std::string>()) ? json(ids.at(value.get<std::string>())) : value; };
    std::vector<std::string> new_ids;
    for (auto& p : result->parts) {
        if (p.group != actual) continue;
        const Part& old = old_by_label.at(p.name);
        p.id = ids.at(p.id); p.name = old.name; p.material = old.material;
        p.layout = old.layout;
        p.contour_edge = old.contour_edge; p.edges = old.edges; p.motion = old.motion;
        p.holes = old.holes; p.fillets = old.fillets; p.chamfers = old.chamfers;
        // The definition, not previously generated groove positions, is retained.
        p.decor = old.decor;
        json cuts = json::array();
        for (const auto& c : old.machining) if (!py::truthy(py::get(c, "generated_decor"))) cuts.push_back(c);
        p.machining = cuts;
        for (const auto& op : old.operations) if (!py::truthy(py::get(op, "owner_id")) && !py::truthy(py::get(op, "opening_owner"))) p.operations.push_back(op);
        for (const char* key : {"anchors", "children"}) {
            if (p.assembly.contains(key)) {
                json out = json::array();
                for (const auto& i : p.assembly[key]) out.push_back(mapped(i));
                p.assembly[key] = out;
            }
        }
        if (py::truthy(py::get(p.assembly, "generated_by"))) p.assembly["generated_by"] = mapped(p.assembly["generated_by"]);
        for (auto& op : p.operations) if (py::truthy(py::get(op, "owner_id"))) op["owner_id"] = mapped(op["owner_id"]);
        new_ids.push_back(p.id);
    }
    {
        std::set<std::string> new_set(new_ids.begin(), new_ids.end());
        json kept = json::array();
        for (const auto& h : result->hardware) {
            json o = py::get(h, "owner_id");
            bool in_ids = o.is_string() && ids.count(o.get<std::string>());
            if (!(in_ids && py::truthy(py::get(h, "manual")))) kept.push_back(h);
        }
        result->hardware = kept;
    }
    for (auto& h : result->hardware) {
        json o = py::get(h, "owner_id");
        if (o.is_string() && ids.count(o.get<std::string>())) h["owner_id"] = ids.at(o.get<std::string>());
        if (h.contains("anchors")) {
            json out = json::array();
            for (const auto& i : h["anchors"]) out.push_back(mapped(i));
            h["anchors"] = out;
        }
    }
    for (const auto& m : manuals) result->hardware.push_back(m);
    // Snapshot parts before attach() replaces the project.
    std::vector<Part> snapshot;
    for (const auto& p : result->parts) if (p.group == actual) snapshot.push_back(p);
    for (const Part& p : snapshot) {
        const Part* old = nullptr;
        for (const Part* q : old_parts) if (q->id == p.id) { old = q; break; }
        if (old == nullptr) throw py::StopIteration();
        json role = py::get(old->assembly, "role");
        if (role == "door" || role == "shelf" || role == "drawer" || role == "back" || role == "worktop") {
            json cfg = py::get(old->assembly, "config", json::object());
            if (cfg.contains("level")) cfg["level"] = cfg["level"].get<double>() * (height.get<double>() / source.at("height").get<double>());
            result = assembly::attach(*result, p, group, p.role, cfg).first;
        }
    }
    std::vector<std::string> in_group;
    for (const auto& p : result->parts) if (p.group == group) in_group.push_back(p.id);
    for (const auto& id : in_group) {
        fittings::refresh_handles(*result, id);
        opening_options::refresh_openings(*result, id);
        Part& p = find_part(*result, id);
        p.validate();
        p.style_source["signature"] = source_signature(p, p.style_source.at("origin"));
    }
    result->validate();
    return result;
}

}  // namespace shaped_furniture
