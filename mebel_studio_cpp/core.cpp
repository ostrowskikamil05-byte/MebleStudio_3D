// Mebel Studio 3D: shared geometry, project and manufacturing model (millimetres).
#include "core.h"
#include "sheet_nesting.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <tuple>

// Moduły importowane w Pythonie leniwie (wewnątrz funkcji):
#include "shaped_furniture.h"
#include "mechanisms.h"
#include "features.h"
#include "cad_geometry.h"
#include "room_objects.h"
#include "viewport_render.h"
#include "viewport_scene.h"

namespace core {

namespace fs = std::filesystem;

namespace {

json points_json(const std::vector<Point>& points) {
    json out = json::array();
    for (auto& p : points) out.push_back({p[0], p[1]});
    return out;
}

json rows_json(const std::vector<std::vector<double>>& rows) {
    json out = json::array();
    for (auto& row : rows) out.push_back(row);
    return out;
}

std::vector<std::vector<double>> rows_from_json(const json& value) {
    if (!value.is_array()) py::type_error(std::string("'") + value.type_name() + "' object is not iterable");
    std::vector<std::vector<double>> out;
    for (auto& row : value) {
        if (!row.is_array()) py::type_error(std::string("'") + value.type_name() + "' object is not iterable");
        std::vector<double> values; for (auto& item : row) values.push_back(py::to_float(item));
        out.push_back(values);
    }
    return out;
}

// str(list) dla konturu (lista krotek) i otworów (lista list)
std::string contour_repr(const std::vector<std::vector<double>>& rows) {
    std::vector<std::string> items;
    for (auto& row : rows) { std::vector<std::string> cells; for (double x : row) cells.push_back(py::repr(x)); items.push_back("(" + py::join(cells, ", ") + ")"); }
    return "[" + py::join(items, ", ") + "]";
}
std::string holes_repr(const std::vector<std::vector<double>>& rows) {
    std::vector<std::string> items;
    for (auto& row : rows) { std::vector<std::string> cells; for (double x : row) cells.push_back(py::repr(x)); items.push_back("[" + py::join(cells, ", ") + "]"); }
    return "[" + py::join(items, ", ") + "]";
}

std::string unexpected_keyword(const std::string& key) { return "__init__() got an unexpected keyword argument '" + key + "'"; }

}  // namespace

std::string uid() {
    static thread_local std::random_device device;
    std::string out;
    for (int i = 0; i < 12; ++i) out += "0123456789abcdef"[device() % 16];
    return out;
}

double finite(const json& value, double lo, double hi) {
    double x = py::to_float(value);
    if (!std::isfinite(x) || !(lo <= x && x <= hi))
        throw ValueError(std::format("Wartość musi mieścić się w zakresie {:g}–{:g}.", lo, hi));
    return x;
}

double polygon_area(const std::vector<Point>& points) {
    double sum = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i]; const Point& b = points[(i + 1) % points.size()];
        sum += a[0] * b[1] - b[0] * a[1];
    }
    return std::abs(sum) / 2;
}

std::vector<Point> validate_polygon(const std::vector<std::vector<double>>& points) {
    if (!(3 <= points.size() && points.size() <= 128))
        throw ValueError("Kontur wymaga od 3 do 128 punktów.");
    std::vector<Point> p;
    for (auto& point : points) {
        if (point.size() < 2) throw ValueError(std::format("not enough values to unpack (expected 2, got {})", point.size()));
        if (point.size() > 2) throw ValueError("too many values to unpack (expected 2)");
        p.push_back({finite(point[0], 0, 10000), finite(point[1], 0, 10000)});
    }
    if (polygon_area(p) < 1)
        throw ValueError("Kontur musi mieć dodatnią powierzchnię.");
    auto orient = [](const Point& a, const Point& b, const Point& c) {
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    };
    auto on = [&](const Point& a, const Point& b, const Point& c) {
        return std::abs(orient(a, b, c)) < 1e-8 && std::min(a[0], b[0]) - 1e-8 <= c[0] && c[0] <= std::max(a[0], b[0]) + 1e-8 && std::min(a[1], b[1]) - 1e-8 <= c[1] && c[1] <= std::max(a[1], b[1]) + 1e-8;
    };
    for (size_t i = 0; i < p.size(); ++i) {
        const Point& a = p[i]; const Point& b = p[(i + 1) % p.size()];
        if (std::hypot(a[0] - b[0], a[1] - b[1]) < 0.01)
            throw ValueError("Kolejne punkty konturu nie mogą być identyczne.");
        for (size_t j = i + 1; j < p.size(); ++j) {
            if (j == i + 1 || (i == 0 && j == p.size() - 1))
                continue;
            const Point& c = p[j]; const Point& d = p[(j + 1) % p.size()];
            std::array<double, 4> o{orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)};
            if ((o[0] * o[1] < 0 && o[2] * o[3] < 0) || on(a, b, c) || on(a, b, d) || on(c, d, a) || on(c, d, b))
                throw ValueError("Krawędzie konturu nie mogą się przecinać ani stykać.");
        }
    }
    return p;
}

double Material::cost(const Part& part) const {
    return part.area() * (kind == "drewno" ? part.t / 1000 * price_m3 : price_m2);
}

json Material::get_attr(const std::string& key) const {
    if (key == "id") return id;
    if (key == "name") return name;
    if (key == "color") return color;
    if (key == "price_m2") return price_m2;
    if (key == "sheet_u") return sheet_u;
    if (key == "sheet_v") return sheet_v;
    if (key == "kind") return kind;
    if (key == "price_m3") return price_m3;
    throw std::out_of_range("'Material' object has no attribute '" + key + "'");
}

void Material::set_attr(const std::string& key, const json& value) {
    if (key == "id") { if (!value.is_string()) py::type_error("Material.id"); id = value.get<std::string>(); }
    else if (key == "name") { if (!value.is_string()) throw ValueError("Materiał wymaga nazwy."); name = value.get<std::string>(); }
    else if (key == "color") { if (!value.is_string()) throw ValueError("Kolor materiału: #RRGGBB."); color = value.get<std::string>(); }
    else if (key == "price_m2") price_m2 = py::to_float(value);
    else if (key == "sheet_u") sheet_u = py::to_float(value);
    else if (key == "sheet_v") sheet_v = py::to_float(value);
    else if (key == "kind") { if (!value.is_string()) throw ValueError("Rodzaj materiału: plyta, mdf, sklejka, drewno, hdf lub inne."); kind = value.get<std::string>(); }
    else if (key == "price_m3") price_m3 = py::to_float(value);
    else py::type_error(unexpected_keyword(key));
}

std::vector<Point> Part::points() const {
    if (!contour.empty()) {
        std::vector<Point> out;
        for (auto& p : contour) out.push_back({p.at(0), p.at(1)});
        return out;
    }
    return {{0, 0}, {u, 0}, {u, v}, {0, v}};
}

double Part::area() const {
    if (py::truthy(profile)) {
        return shaped_furniture::profile_area(profile, u, v) / 1e6;
    }
    return polygon_area(points()) / 1e6;
}

double Part::edge_m() const {
    if (!contour.empty()) {
        if (!contour_edge) return 0;
        if (py::truthy(profile)) {
            return shaped_furniture::profile_perimeter(profile, u, v) / 1000;
        }
        auto pts = points();
        double sum = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Point& a = pts[i]; const Point& b = pts[(i + 1) % pts.size()];
            sum += std::hypot(a[0] - b[0], a[1] - b[1]);
        }
        return sum / 1000;
    }
    return (v * ((int)edges[0] + (int)edges[1]) + u * ((int)edges[2] + (int)edges[3])) / 1000;
}

std::pair<double, double> Part::blank(double edge_t) const {
    if (!contour.empty()) {
        return {u, v};
    }
    return {u - edge_t * ((int)edges[0] + (int)edges[1]), v - edge_t * ((int)edges[2] + (int)edges[3])};
}

void Part::validate() {
    if (!configurator_source.is_object())
        throw ValueError("Nieprawidłowe dane kreatora mebla.");
    if (!configurator_source.empty() && (configurator_source.contains("schema") || configurator_source.contains("key") || configurator_source.contains("config") || configurator_source.contains("manifest"))) {
        const json& source = configurator_source;
        if (py::get(source, "schema") != 1 || !py::get(source, "key").is_string())
            throw ValueError("Nieprawidłowa wersja lub część kreatora mebla.");
        if (source.contains("config")) {
            if (!source["config"].is_object() || !py::get(source, "manifest").is_object())
                throw ValueError("Nieprawidłowy zapis konfiguracji mebla.");
            for (auto& [_, entry] : source["manifest"].items()) {
                if (!entry.is_object() || !py::get(entry, "id").is_string() || !py::get(entry, "signature").is_string())
                    throw ValueError("Nieprawidłowe powiązania konfiguracji mebla.");
            }
        }
    }
    bool bad_layout = !layout.is_object();
    if (!bad_layout) for (auto& [key, _] : layout.items()) if (key != "x" && key != "y" && key != "z" && key != "yaw") bad_layout = true;
    if (bad_layout)
        throw ValueError("Nieprawidłowe ustawienie mebla w zestawie.");
    for (std::string key : {"x", "y", "z", "yaw"}) {
        if (layout.contains(key)) layout[key] = finite(layout[key]);
    }
    if (!profile.is_object() || !decor.is_object() || !style_source.is_object())
        throw ValueError("Nieprawidłowe parametry obrysu, obrzeża lub dekoracji.");
    if (!motion.is_object()) throw ValueError("Nieprawidłowe dane mechanizmu.");
    if (!motion.empty()) {
        bool known = false;
        for (auto& [_, mode] : mechanisms::MODES) if (py::get(motion, "mode") == mode) known = true;
        if (!known) throw ValueError("Nieznany mechanizm.");
        finite(motion.contains("angle") ? motion["angle"] : json(100), 0, 180); finite(motion.contains("travel") ? motion["travel"] : json(400), 0, 3000);
    }
    if (!assembly.is_object() || !operations.is_array())
        throw ValueError("Nieprawidłowe dane konstrukcyjne elementu.");
    if (py::strip(name).empty())
        throw ValueError("Wpisz nazwę elementu.");
    u = finite(u, 0.1, 10000);
    v = finite(v, 0.1, 10000);
    t = finite(t, 0.1, 10000);
    x = finite(x); y = finite(y); z = finite(z);
    rx = finite(rx); ry = finite(ry); rz = finite(rz);
    if (plane != "XY" && plane != "XZ" && plane != "YZ")
        throw ValueError("Nieprawidłowa płaszczyzna elementu.");
    if (edges.size() != 4)
        throw ValueError("Nieprawidłowe obrzeża.");
    if (py::truthy(profile)) {
        json kwargs = json::object();
        for (auto& [k, val] : profile.items()) if (k != "kind") kwargs[k] = val;
        profile = shaped_furniture::shape_definition(py::get(profile, "kind"), u, v, kwargs);
        kwargs = json::object();
        for (auto& [k, val] : profile.items()) if (k != "kind") kwargs[k] = val;
        contour = shaped_furniture::contour(profile.at("kind"), u, v, kwargs);
    }
    if (!contour.empty()) {
        auto validated = validate_polygon(contour);
        contour.clear();
        for (auto& p : validated) contour.push_back({p[0], p[1]});
        double min_x = contour[0][0], min_y = contour[0][1], max_x = contour[0][0], max_y = contour[0][1];
        for (auto& p : contour) { min_x = std::min(min_x, p[0]); min_y = std::min(min_y, p[1]); max_x = std::max(max_x, p[0]); max_y = std::max(max_y, p[1]); }
        if (min_x != 0 || min_y != 0)
            throw ValueError("Kontur musi zaczynać się od minimum U=0 i V=0.");
        u = max_x;
        v = max_y;
        if (std::any_of(edges.begin(), edges.end(), [](bool e) { return e; }))
            throw ValueError("Dla konturu nieregularnego obrzeża należy wyłączyć.");
    }
    if (py::truthy(decor)) {
        shaped_furniture::refresh_decor(*this);
    }
    for (auto& hole : holes) {
        if (hole.size() != 4)
            throw ValueError("Otwór: U; V; średnica; głębokość.");
        double a = finite(hole[0], 0, 10000), b = finite(hole[1], 0, 10000), d = finite(hole[2], 0, 10000), depth = finite(hole[3], 0, 10000);
        if (d <= 0 || depth <= 0 || depth > t || !(d / 2 <= a && a <= u - d / 2) || !(d / 2 <= b && b <= v - d / 2))
            throw ValueError("Otwór wykracza poza element lub jego grubość.");
        // Every sampled point on the circle must lie inside the contour.
        for (int i = 0; i < 72; ++i) {
            Point q{a + d / 2 * std::cos(i * 2 * std::numbers::pi / 72), b + d / 2 * std::sin(i * 2 * std::numbers::pi / 72)};
            if (!inside(q, points()))
                throw ValueError("Otwór wykracza poza kontur.");
        }
    }
    for (auto& operation : operations) {
        features::validate_drill(*this, operation);
    }
    std::set<std::string> valid_edges;
    for (std::string kind : {"top", "bottom", "corner"})
        for (size_t i = 0; i < points().size(); ++i) valid_edges.insert(kind + ":" + std::to_string(i));
    if (py::truthy(profile) && (py::truthy(fillets) || py::truthy(chamfers))) {
        valid_edges.clear();
        for (auto& spec : cad_geometry::edge_specs(points(), t, profile, {u, v})) valid_edges.insert(spec.edge);
    }
    auto edge_set = [](const json& items) { std::set<json> out; for (auto& item : items) out.insert(item.at("edge")); return out; };
    auto valid = [&](const json& edge) { return edge.is_string() && valid_edges.contains(edge.get<std::string>()); };
    if (!fillets.is_array() || edge_set(fillets).size() != fillets.size())
        throw ValueError("Zaokrąglenia wymagają unikalnych krawędzi.");
    for (auto& fillet : fillets) {
        if (!valid(fillet.at("edge"))) throw ValueError("Zaokrąglenie wskazuje nieistniejącą krawędź.");
        finite(fillet.at("radius"), .01, 10000);
    }
    if (!chamfers.is_array() || edge_set(chamfers).size() != chamfers.size()) throw ValueError("Fazy wymagają unikalnych krawędzi.");
    {
        auto f = edge_set(fillets), c = edge_set(chamfers);
        for (auto& e : f) if (c.contains(e)) throw ValueError("Jeden kant może mieć fazę albo zaokrąglenie.");
    }
    for (auto& chamfer : chamfers) {
        if (!valid(chamfer.at("edge"))) throw ValueError("Faza wskazuje nieistniejącą krawędź.");
        finite(chamfer.at("distance"), .01, 10000);
    }
    if (!machining.is_array() || machining.size() > 200) throw ValueError("Maksymalnie 200 operacji frezowania na element.");
    for (auto& cut : machining) {
        features::validate_cut(*this, cut);
    }
    bool side_operation = false;
    for (auto& o : operations) if (o.at("face") != "top" && o.at("face") != "bottom") side_operation = true;
    if (py::truthy(fillets) || py::truthy(chamfers) || py::truthy(machining) || side_operation) {
        cad_geometry::validate_fillets(*this);
    }
}

json Part::get_attr(const std::string& key) const {
    if (key == "id") return id;
    if (key == "name") return name;
    if (key == "group") return group;
    if (key == "u") return u;
    if (key == "v") return v;
    if (key == "t") return t;
    if (key == "plane") return plane;
    if (key == "x") return x;
    if (key == "y") return y;
    if (key == "z") return z;
    if (key == "rx") return rx;
    if (key == "ry") return ry;
    if (key == "rz") return rz;
    if (key == "material") return material;
    if (key == "grain") return grain;
    if (key == "grain_v") return grain_v;
    if (key == "edges") return edges;
    if (key == "contour") return rows_json(contour);
    if (key == "holes") return rows_json(holes);
    if (key == "visible") return visible;
    if (key == "role") return role;
    if (key == "assembly") return assembly;
    if (key == "operations") return operations;
    if (key == "fillets") return fillets;
    if (key == "chamfers") return chamfers;
    if (key == "machining") return machining;
    if (key == "motion") return motion;
    if (key == "profile") return profile;
    if (key == "contour_edge") return contour_edge;
    if (key == "decor") return decor;
    if (key == "style_source") return style_source;
    if (key == "configurator_source") return configurator_source;
    if (key == "layout") return layout;
    throw std::out_of_range("'Part' object has no attribute '" + key + "'");
}

void Part::set_attr(const std::string& key, const json& value) {
    auto text = [&](const char* message) {
        if (!value.is_string()) { if (message) throw ValueError(message); py::type_error("Part." + key); }
        return value.get<std::string>();
    };
    if (key == "id") id = text(nullptr);
    else if (key == "name") name = text("Wpisz nazwę elementu.");
    else if (key == "group") group = text(nullptr);
    else if (key == "u") u = py::to_float(value);
    else if (key == "v") v = py::to_float(value);
    else if (key == "t") t = py::to_float(value);
    else if (key == "plane") plane = text("Nieprawidłowa płaszczyzna elementu.");
    else if (key == "x") x = py::to_float(value);
    else if (key == "y") y = py::to_float(value);
    else if (key == "z") z = py::to_float(value);
    else if (key == "rx") rx = py::to_float(value);
    else if (key == "ry") ry = py::to_float(value);
    else if (key == "rz") rz = py::to_float(value);
    else if (key == "material") material = text(nullptr);
    else if (key == "grain") grain = value.is_boolean() ? value.get<bool>() : py::truthy(value);
    else if (key == "grain_v") grain_v = value.is_boolean() ? value.get<bool>() : py::truthy(value);
    else if (key == "edges") {
        if (!value.is_array()) throw ValueError("Nieprawidłowe obrzeża.");
        edges.clear();
        for (auto& e : value) { if (!e.is_boolean()) throw ValueError("Nieprawidłowe obrzeża."); edges.push_back(e.get<bool>()); }
    }
    else if (key == "contour") contour = rows_from_json(value);
    else if (key == "holes") holes = rows_from_json(value);
    else if (key == "visible") visible = value.is_boolean() ? value.get<bool>() : py::truthy(value);
    else if (key == "role") role = text("Nieprawidłowe dane konstrukcyjne elementu.");
    else if (key == "assembly") assembly = value;
    else if (key == "operations") operations = value;
    else if (key == "fillets") fillets = value;
    else if (key == "chamfers") chamfers = value;
    else if (key == "machining") machining = value;
    else if (key == "motion") motion = value;
    else if (key == "profile") profile = value;
    else if (key == "contour_edge") {
        if (!value.is_boolean()) throw ValueError("Nieprawidłowe parametry obrysu, obrzeża lub dekoracji.");
        contour_edge = value.get<bool>();
    }
    else if (key == "decor") decor = value;
    else if (key == "style_source") style_source = value;
    else if (key == "configurator_source") configurator_source = value;
    else if (key == "layout") layout = value;
    else py::type_error(unexpected_keyword(key));
}

// dataclasses.asdict
json asdict(const Material& m) {
    json out = json::object();
    for (std::string key : {"id", "name", "color", "price_m2", "sheet_u", "sheet_v", "kind", "price_m3"}) out[key] = m.get_attr(key);
    return out;
}

json asdict(const Part& p) {
    json out = json::object();
    for (std::string key : {"id", "name", "group", "u", "v", "t", "plane", "x", "y", "z", "rx", "ry", "rz", "material", "grain", "grain_v", "edges", "contour", "holes",
                            "visible", "role", "assembly", "operations", "fillets", "chamfers", "machining", "motion", "profile", "contour_edge", "decor",
                            "style_source", "configurator_source", "layout"})
        out[key] = p.get_attr(key);
    return out;
}


bool inside(const Point& q, const std::vector<Point>& p) {
    double x = q[0], y = q[1];
    bool hit = false;
    for (size_t i = 0; i < p.size(); ++i) {
        const Point& a = p[i]; const Point& b = p[(i + 1) % p.size()];
        double cross = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
        if (std::abs(cross) < 1e-7 && std::min(a[0], b[0]) - 1e-7 <= x && x <= std::max(a[0], b[0]) + 1e-7 && std::min(a[1], b[1]) - 1e-7 <= y && y <= std::max(a[1], b[1]) + 1e-7)
            return true;
        if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0])
            hit = !hit;
    }
    return hit;
}

json _default_scene() {
    return room_objects::default_scene();
}

Material& Project::mat(const std::string& id) {
    for (auto& m : materials) if (m.id == id) return m;
    throw StopIteration();
}

const Material& Project::mat(const std::string& id) const {
    for (auto& m : materials) if (m.id == id) return m;
    throw StopIteration();
}

void Project::validate() {
    scene = room_objects::validate_scene(scene);
    if (materials.empty() || parts.size() > 2000)
        throw ValueError("Projekt wymaga materiałów i może mieć najwyżej 2000 elementów.");
    std::vector<std::string> ids; for (auto& m : materials) ids.push_back(m.id);
    std::set<std::string> unique_ids(ids.begin(), ids.end()), unique_parts;
    for (auto& p : parts) unique_parts.insert(p.id);
    if (unique_ids.size() != ids.size() || unique_parts.size() != parts.size())
        throw ValueError("Powtarzające się identyfikatory.");
    for (auto& m : materials) {
        if (m.kind != "plyta" && m.kind != "mdf" && m.kind != "sklejka" && m.kind != "drewno" && m.kind != "hdf" && m.kind != "inne")
            throw ValueError("Rodzaj materiału: plyta, mdf, sklejka, drewno, hdf lub inne.");
        finite(m.price_m3, 0, 1e9);
        if (py::strip(m.name).empty())
            throw ValueError("Materiał wymaga nazwy.");
        finite(m.price_m2, 0, 1e6);
        finite(m.sheet_u, 1, 10000); finite(m.sheet_v, 1, 10000);
        if (py::len(m.color) != 7 || m.color[0] != '#')
            throw ValueError("Kolor materiału: #RRGGBB.");
        py::int16(m.color.substr(1));
    }
    finite(kerf, 0, 100000); finite(margin, 0, 100000); finite(edge_t, 0, 100000);
    finite(edge_price, 0, 100000); finite(labor, 0, 100000); finite(markup, 0, 100000);
    for (auto& p : parts) {
        p.validate();
        if (std::find(ids.begin(), ids.end(), p.material) == ids.end())
            throw ValueError("Element wskazuje nieznany materiał.");
        auto blank = p.blank(edge_t);
        if (std::min(blank.first, blank.second) <= 0)
            throw ValueError("Obrzeże jest grubsze od wymiaru elementu.");
    }
    for (auto& h : hardware) {
        json name_ = py::get(h, "name");
        if (!name_.is_string() || py::strip(h["name"].get<std::string>()).empty())
            throw ValueError("Akcesorium wymaga nazwy.");
        finite(h.at("qty"), 0, 100000); finite(h.at("price"), 0, 100000);
        if (py::truthy(py::get(h, "owner_id")) && !(h["owner_id"].is_string() && unique_parts.contains(h["owner_id"].get<std::string>())))
            throw ValueError("Okucie wskazuje usunięty element.");
    }
    const std::set<std::string>& part_ids = unique_parts;
    for (auto& p : parts) {
        for (auto& operation : p.operations) {
            if (py::truthy(py::get(operation, "owner_id")) && !(operation["owner_id"].is_string() && part_ids.contains(operation["owner_id"].get<std::string>())))
                throw ValueError("Wiercenie wskazuje usunięty element.");
        }
        json anchors = p.assembly.contains("anchors") ? p.assembly["anchors"] : json::array();
        for (auto& i : anchors)
            if (!(i.is_string() && part_ids.contains(i.get<std::string>())))
                throw ValueError("Powiązanie konstrukcyjne wskazuje usunięty element.");
    }
}

json Project::data() const {
    json out = json::object();
    out["version"] = VERSION;
    out["name"] = name;
    out["parts"] = json::array(); for (auto& p : parts) out["parts"].push_back(asdict(p));
    out["materials"] = json::array(); for (auto& m : materials) out["materials"].push_back(asdict(m));
    out["hardware"] = hardware;
    out["scene"] = scene;
    out["kerf"] = kerf;
    out["margin"] = margin;
    out["edge_t"] = edge_t;
    out["edge_price"] = edge_price;
    out["labor"] = labor;
    out["markup"] = markup;
    return out;
}

ProjectPtr Project::from_data(const json& data_) {
    json data = data_;
    if (!data.is_object()) py::type_error(std::string("'") + data.type_name() + "' object has no attribute 'pop'");
    json version = data.contains("version") ? data["version"] : json(nullptr);
    data.erase("version");
    if (version != VERSION)
        throw ValueError("Nieobsługiwana wersja projektu.");
    if (!data.contains("materials")) throw KeyError("'materials'");
    std::vector<Material> mats;
    for (auto& m : data["materials"]) {
        if (!m.is_object()) py::type_error("argument after ** must be a mapping");
        Material material; for (auto& [k, val] : m.items()) material.set_attr(k, val); mats.push_back(material);
    }
    data.erase("materials");
    if (!data.contains("parts")) throw KeyError("'parts'");
    std::vector<Part> parts;
    for (auto& p : data["parts"]) {
        if (!p.is_object()) py::type_error("argument after ** must be a mapping");
        Part part; for (auto& [k, val] : p.items()) part.set_attr(k, val); parts.push_back(part);
    }
    data.erase("parts");
    auto obj = std::make_shared<Project>();
    obj->materials = mats; obj->parts = parts;
    for (auto& [k, val] : data.items()) obj->set_attr(k, val);
    obj->validate();
    return obj;
}

void Project::save(const std::string& path_) {
    validate();
    fs::path path = py::to_path(path_);
    fs::path dir = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    // tempfile.mkstemp(prefix=path.name+'.', dir=str(path.parent))
    static thread_local std::random_device device;
    fs::path tmp;
    for (int attempt = 0;; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 8; ++i) suffix += "abcdefghijklmnopqrstuvwxyz0123456789_"[device() % 37];
        tmp = dir / (path.filename().u8string() + u8"." + std::u8string(suffix.begin(), suffix.end()));
        if (!fs::exists(tmp)) break;
        if (attempt > 10000) throw OSError(std::make_error_code(std::errc::file_exists), "No usable temporary file name found");
    }
    try {
        {
            std::ofstream f(tmp);  // tryb tekstowy, jak os.fdopen(fd,'w')
            if (!f) throw OSError(std::error_code(errno, std::generic_category()), tmp.string());
            f << py::dumps(data(), false, 2);
            f.close();
            if (!f) throw OSError(std::error_code(errno, std::generic_category()), tmp.string());
        }
        fs::rename(tmp, path);
    } catch (...) {
        std::error_code ec;
        if (fs::exists(tmp, ec)) fs::remove(tmp, ec);
        throw;
    }
    std::error_code ec;
    if (fs::exists(tmp, ec)) fs::remove(tmp, ec);
}

ProjectPtr Project::load(const std::string& path) {
    if (fs::file_size(py::to_path(path)) > 10'000'000)
        throw ValueError("Plik projektu jest zbyt duży.");
    std::ifstream f(py::to_path(path), std::ios::binary);
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
    std::stringstream buffer; buffer << f.rdbuf();
    json parsed;
    try { parsed = json::parse(buffer.str()); }
    catch (const json::parse_error& e) { throw ValueError(e.what()); }  // json.JSONDecodeError jest ValueError
    return from_data(parsed);
}

std::map<std::string, double> Project::costs() const {
    double panels = 0; for (auto& p : parts) panels += mat(p.material).cost(p);
    double edge = 0; for (auto& p : parts) edge += p.edge_m(); edge *= edge_price;
    double hardware_ = 0; for (auto& h : hardware) hardware_ += h.at("qty").get<double>() * h.at("price").get<double>();
    json objects = scene.contains("objects") ? scene["objects"] : json::array();
    for (auto& obj : objects) hardware_ += obj.contains("price") ? obj["price"].get<double>() : 0;
    double base = panels + edge + hardware_ + labor;
    return {{"panels", panels}, {"edges", edge}, {"hardware", hardware_}, {"labor", labor}, {"base", base}, {"total", base * (1 + markup / 100)}};
}

json Project::get_attr(const std::string& key) const {
    if (key == "name") return name;
    if (key == "hardware") return hardware;
    if (key == "scene") return scene;
    if (key == "kerf") return kerf;
    if (key == "margin") return margin;
    if (key == "edge_t") return edge_t;
    if (key == "edge_price") return edge_price;
    if (key == "labor") return labor;
    if (key == "markup") return markup;
    if (key == "parts" || key == "materials") return data()[key];
    throw std::out_of_range("'Project' object has no attribute '" + key + "'");
}

void Project::set_attr(const std::string& key, const json& value) {
    if (key == "name") { if (!value.is_string()) py::type_error("Project.name"); name = value.get<std::string>(); }
    else if (key == "hardware") hardware = value;
    else if (key == "scene") scene = value;
    else if (key == "kerf") kerf = py::to_float(value);
    else if (key == "margin") margin = py::to_float(value);
    else if (key == "edge_t") edge_t = py::to_float(value);
    else if (key == "edge_price") edge_price = py::to_float(value);
    else if (key == "labor") labor = py::to_float(value);
    else if (key == "markup") markup = py::to_float(value);
    else py::type_error(unexpected_keyword(key));
}

Point3 rotate(const Point3& point, double rx, double ry, double rz) {
    double x = point[0], y = point[1], z = point[2];
    double a = rx * (std::numbers::pi / 180), b = ry * (std::numbers::pi / 180), c = rz * (std::numbers::pi / 180);
    std::tie(y, z) = std::make_pair(y * std::cos(a) - z * std::sin(a), y * std::sin(a) + z * std::cos(a));
    std::tie(x, z) = std::make_pair(x * std::cos(b) + z * std::sin(b), -x * std::sin(b) + z * std::cos(b));
    std::tie(x, y) = std::make_pair(x * std::cos(c) - y * std::sin(c), x * std::sin(c) + y * std::cos(c));
    return {x, y, z};
}

Point3 local_world(const Part& p, double u, double v, double w) {
    Point3 local = std::map<std::string, Point3>{{"XY", {u, v, w}}, {"XZ", {u, w, v}}, {"YZ", {w, u, v}}}.at(p.plane);
    auto [a, b, c] = rotate(local, p.rx, p.ry, p.rz);
    return {a + p.x, b + p.y, c + p.z};
}

Point3 layout_point(const Part& p, const Point3& point) {
    const json& placement = p.layout;
    if (!py::truthy(placement)) return point;
    auto get = [&](const char* key) { return placement.contains(key) ? placement[key].get<double>() : 0.0; };
    auto [a, b, c] = py::truthy(py::get(placement, "yaw")) ? rotate(point, 0, 0, placement["yaw"].get<double>()) : point;
    return {a + get("x"), b + get("y"), c + get("z")};
}

Point3 inverse_layout_point(const Part& p, const Point3& point) {
    const json& placement = p.layout;
    if (!py::truthy(placement)) return point;
    auto get = [&](const char* key) { return placement.contains(key) ? placement[key].get<double>() : 0.0; };
    return rotate({point[0] - get("x"), point[1] - get("y"), point[2] - get("z")}, 0, 0, -get("yaw"));
}

Point3 world(const Part& p, double u, double v, double w) {
    return layout_point(p, local_world(p, u, v, w));
}

json faces(const Part& p, bool interactive) {
    return viewport_render::geometry_faces(p, interactive);
}

Camera::Camera(double yaw, double pitch, double scale, Point3 center, std::array<double, 2> pan)
    : yaw(yaw), pitch(pitch), scale(scale), center(center), pan(pan) {}

std::array<double, 4> Camera::_basis() const {
    std::pair<double, double> key{yaw, pitch};
    if (_basis_key != key) {
        double a = yaw * (std::numbers::pi / 180), b = pitch * (std::numbers::pi / 180);
        _basis_key = key;
        _basis_value = {std::cos(a), std::sin(a), std::cos(b), std::sin(b)};
    }
    return _basis_value;
}

Point3 Camera::view(const Point3& point) const {
    double x = point[0] - center[0], y = point[1] - center[1], z = point[2] - center[2];
    auto [ca, sa, cb, sb] = _basis();
    double u = x * ca - y * sa;
    double d = x * sa + y * ca;
    return {u, z * cb + d * sb, -d * cb + z * sb};
}

Point3 Camera::project(const Point3& point, double width, double height) const {
    auto [u, v, d] = view(point);
    return {width / 2 + pan[0] + u * scale, height / 2 + pan[1] - v * scale, d};
}

void Camera::fit(const Project& project, double width, double height) {
    viewport_scene::fit_scene(*this, project, {width, height}, 120);
}

std::string shade(const std::string& color, double factor) {
    std::string out = "#";
    for (size_t i : {1, 3, 5}) {
        long long value = py::int16(color.substr(i, 2));
        out += std::format("{:02x}", (long long)std::min(255.0, std::max(0.0, std::nearbyint(value * factor))));
    }
    return out;
}

Point3 explosion_shift(const Part& p, const Camera& camera, double explode) {
    Point3 center = world(p, p.u / 2, p.v / 2, p.t);
    return {(center[0] - camera.center[0]) * explode, (center[1] - camera.center[1]) * explode, (center[2] - camera.center[2]) * explode};
}

Drawn render_faces(const Project& project, const Camera& camera, double width, double height,
                   const std::optional<std::string>& selected, double explode, bool interactive, viewport_render::Context* context) {
    return viewport_render::render(project, camera, width, height, selected, explode,
                                   interactive, context);
}

std::string svg_view(const Project& project, Camera* camera, int width, int height, const std::optional<std::string>& selected) {
    Camera default_camera;
    if (camera == nullptr) camera = &default_camera;
    if (camera->scale <= 0) camera->fit(project, width, height);
    std::vector<std::string> items{std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{0}" height="{1}" viewBox="0 0 {0} {1}"><rect width="100%" height="100%" fill="#eef1f5"/>)", width, height)};
    for (auto& f : render_faces(project, *camera, width, height, selected)) {
        std::vector<std::string> pts;
        for (auto& point : f.at("points")) pts.push_back(std::format("{:.2f},{:.2f}", point.at(0).get<double>(), point.at(1).get<double>()));
        bool mesh = py::truthy(py::get(f, "mesh"));
        std::string border = mesh ? f.at("color").get<std::string>() : py::truthy(f.at("selected")) ? "#e68c36" : "#59616b";
        items.push_back(std::format(R"(<polygon points="{}" fill="{}" stroke="{}" stroke-width="{}"/>)", py::join(pts, " "), f.at("color").get<std::string>(), border, mesh ? 0 : 1));
    }
    items.push_back("</svg>");
    return py::join(items, "");
}

std::vector<Part> add_template(Project& project, const std::string& kind, double w, double h, double d, double t, int shelves, bool front, Point3 origin, const std::string& group_) {
    for (double val : {w, h, d, t}) finite(val, 1, 10000);
    if (w <= 2 * t || h <= 2 * t || d <= t || shelves < 0 || shelves > 30)
        throw ValueError("Nieprawidłowe wymiary konstrukcji lub liczba półek.");
    std::string group = !group_.empty() ? group_ : std::map<std::string, std::string>{{"cabinet", "Szafka"}, {"wardrobe", "Szafa"}, {"table", "Stół"}, {"shelf", "Regał"}}.at(kind);
    auto [ox, oy, oz] = origin;
    std::vector<Part> result;
    auto add = [&](const std::string& name, double u, double v, double thick, const std::string& plane, double x, double y, double z, const std::string& mat = "", const std::vector<bool>& edges = {}) {
        Part p{.name = name, .group = group, .u = u, .v = v, .t = thick, .plane = plane, .x = x + ox, .y = y + oy, .z = z + oz, .material = !mat.empty() ? mat : project.materials.at(0).id, .edges = !edges.empty() ? edges : std::vector<bool>(4, false)};
        const Part* reference = nullptr;
        for (auto& q : project.parts) if (q.group == group) { reference = &q; break; }
        if (reference) p.layout = reference->layout;
        p.validate(); result.push_back(p);
    };
    if (kind == "table") {
        double leg = std::max(40.0, t * 2);
        if (std::min(w, d) <= leg * 2 || h <= t)
            throw ValueError("Stół jest zbyt mały dla nóg.");
        add("Blat", w, d, t, "XY", 0, 0, h - t, "", std::vector<bool>(4, true));
        std::vector<std::pair<double, double>> legs{{0, 0}, {w - leg, 0}, {0, d - leg}, {w - leg, d - leg}};
        for (size_t i = 0; i < legs.size(); ++i) {
            auto [x, y] = legs[i];
            add(std::format("Noga {}", i + 1), leg, h - t, leg, "XZ", x, y, 0);
        }
    } else {
        add("Bok lewy", d, h, t, "YZ", 0, 0, 0);
        add("Bok prawy", d, h, t, "YZ", w - t, 0, 0);
        add("Dno", w - 2 * t, d, t, "XY", t, 0, 0);
        add("Wieniec", w - 2 * t, d, t, "XY", t, 0, h - t);
        for (int i = 0; i < shelves; ++i) {
            double z = t + (h - 2 * t) * (i + 1) / (shelves + 1) - t / 2;
            add(std::format("Półka {}", i + 1), w - 2 * t - 2, d - 10, t, "XY", t + 1, 5, z, "", {false, false, true, false});
        }
        if (kind != "shelf") {
            std::string hdf = project.materials.at(0).id;
            for (auto& m : project.materials) if (m.kind == "hdf") { hdf = m.id; break; }
            add("Plecy nakładane HDF", w, h, 3, "XZ", 0, d, 0, hdf);
            if (front) {
                if (w <= 7) throw ValueError("Za mała szerokość frontów.");
                for (int i = 0; i < 2; ++i) {
                    add(std::format("Front {}", i + 1), (w - 6) / 2, h - 4, t, "XZ", 2 + i * (w / 2 - 1), -t - 2, 2, project.materials.at(1).id, std::vector<bool>(4, true));
                }
            }
        }
    }
    project.parts.insert(project.parts.end(), result.begin(), result.end());
    return result;
}

std::pair<json, std::vector<std::string>> cut_plan(Project& project) {
    project.validate();
    json sheets = json::array(); std::vector<std::string> unplaced;
    std::vector<const Part*> parts; for (auto& p : project.parts) parts.push_back(&p);
    std::stable_sort(parts.begin(), parts.end(), [&](const Part* a, const Part* b) {
        auto ba = a->blank(project.edge_t), bb = b->blank(project.edge_t);
        return ba.first * ba.second > bb.first * bb.second;
    });
    for (const Part* p : parts) {
        auto [u, v] = p->blank(project.edge_t); double k = project.kerf; const Material& m = project.mat(p->material);
        std::vector<std::tuple<double, double, bool>> options;
        if (!p->must_turn()) options.push_back({u, v, false});
        if (p->may_turn() || p->must_turn()) options.push_back({v, u, true});
        std::optional<std::tuple<double, size_t, size_t, double, double, bool>> chosen;
        for (size_t si = 0; si < sheets.size(); ++si) {
            auto& s = sheets[si];
            if (s["material"] != p->material || s["thickness"] != p->t) continue;
            for (size_t ri = 0; ri < s["free"].size(); ++ri) {
                auto& r = s["free"][ri];
                for (auto [a, b, rot] : options) {
                    if (a + k <= r[2].get<double>() + 1e-8 && b + k <= r[3].get<double>() + 1e-8) {
                        double score = r[2].get<double>() * r[3].get<double>() - (a + k) * (b + k);
                        if (!chosen || score < std::get<0>(*chosen)) chosen = std::make_tuple(score, si, ri, a, b, rot);
                    }
                }
            }
        }
        if (!chosen) {
            double sw = m.sheet_u - 2 * project.margin, sh = m.sheet_v - 2 * project.margin;
            std::vector<std::tuple<double, double, bool>> possible;
            for (auto& o : options) if (std::get<0>(o) <= sw + 1e-8 && std::get<1>(o) <= sh + 1e-8) possible.push_back(o);
            if (possible.empty()) {
                unplaced.push_back(p->id); continue;
            }
            auto [a, b, rot] = possible[0];
            sheets.push_back({{"material", p->material}, {"thickness", p->t}, {"u", m.sheet_u}, {"v", m.sheet_v},
                              {"free", json::array({json::array({project.margin, project.margin, sw + k, sh + k})})}, {"pieces", json::array()}});
            chosen = std::make_tuple(0.0, sheets.size() - 1, (size_t)0, a, b, rot);
        }
        auto [_, si, ri, a, b, rot] = *chosen; auto& s = sheets[si];
        json rect = s["free"][ri]; s["free"].erase(ri);
        double x = rect[0], y = rect[1], fw = rect[2], fh = rect[3];
        s["pieces"].push_back({{"id", p->id}, {"x", x}, {"y", y}, {"u", a}, {"v", b}, {"rotated", rot}});
        double rw = fw - a - k; double bh = fh - b - k;
        // A vertical guillotine cut, then a horizontal cut in the left strip.
        if (rw > 1e-8) s["free"].push_back(json::array({x + a + k, y, rw, fh}));
        if (bh > 1e-8) s["free"].push_back(json::array({x, y + b + k, a + k, bh}));
    }
    return {sheets, unplaced};
}

void manufacturing_export(Project& project, const std::string& path) {
    project.validate();
    json materials = json::array(); for (auto& m : project.materials) materials.push_back(asdict(m));
    json data = {{"schema", "mebelstudio-manufacturing-1"}, {"units", "mm"},
                 {"project", project.name}, {"machine_program", false},
                 {"coordinate_system", "Local U,V; W=0 at bottom, W=thickness at top. Drilling face selects entry and local coordinates. Edge U faces use V,T; edge V faces use U,T. Assembly position/rotation and hardware positions use furniture construction coordinates; room_placement is applied last. world_origin/world_basis map local U,V,W to the room."},
                 {"materials", materials}, {"hardware", project.hardware}, {"parts", json::array()}};
    for (auto& p : project.parts) {
        json edge_legend = json::array();
        if (py::truthy(p.fillets) || py::truthy(p.chamfers)) {
            for (auto& spec : cad_geometry::edge_specs(p.points(), p.t, p.profile, {p.u, p.v})) {
                // W Pythonie ta zmienna nazywała się `path` i nadpisywała argument funkcji.
                auto edge_path = cad_geometry::edge_path(spec);
                json midpoint = json::array();
                if (edge_path.size() > 2) midpoint = edge_path[edge_path.size() / 2];
                else for (size_t i = 0; i < edge_path.front().size(); ++i) midpoint.push_back((edge_path.front()[i] + edge_path.back()[i]) / 2);
                json tangent_chain = json::array();
                if (spec.geometry.chain) for (auto& item : *spec.geometry.chain) tangent_chain.push_back(item);
                else tangent_chain.push_back(spec.edge);
                edge_legend.push_back({{"id", spec.edge}, {"name", spec.name},
                    {"geometry", spec.geometry.geom_type.value_or("LINE")}, {"start", edge_path.front()},
                    {"midpoint", midpoint}, {"end", edge_path.back()},
                    {"tangent_chain", tangent_chain}});
            }
        }
        json world_basis = json::array();
        Point3 origin = world(p, 0, 0, 0);
        for (Point3 axis : {Point3{1, 0, 0}, Point3{0, 1, 0}, Point3{0, 0, 1}}) {
            Point3 end = world(p, axis[0], axis[1], axis[2]);
            world_basis.push_back({end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]});
        }
        json drilling = json::array();
        for (auto& hole : p.holes) {
            if (hole.size() != 4) throw ValueError(hole.size() < 4 ? std::format("not enough values to unpack (expected 4, got {})", hole.size()) : "too many values to unpack (expected 4)");
            drilling.push_back({{"u", hole[0]}, {"v", hole[1]}, {"diameter", hole[2]}, {"depth", hole[3]}, {"face", "top"}});
        }
        for (auto& op : p.operations) drilling.push_back(op);
        auto blank = p.blank(project.edge_t);
        data["parts"].push_back({{"id", p.id}, {"name", p.name}, {"group", p.group}, {"material", p.material},
            {"thickness", p.t}, {"finished_size", {p.u, p.v}}, {"cut_size", {blank.first, blank.second}},
            {"contour", points_json(p.points())}, {"edges", p.edges}, {"grain_along_u", p.grain && !p.grain_v}, {"grain_along_v", p.grain && p.grain_v},
            {"outline_definition", p.profile}, {"full_contour_edge", p.contour_edge},
            {"contour_edge_length_m", p.edge_m()}, {"decoration", p.decor},
            {"outline_geometry", py::truthy(p.profile) ? "exact_in_STEP_drawing_tessellation" : "polygon"},
            {"edge_numbering", py::truthy(p.profile) ? "exact_CAD_outline" : "polygon_vertices"}, {"edge_legend", edge_legend},
            {"assembly", {{"plane", p.plane}, {"position", {p.x, p.y, p.z}}, {"rotation", {p.rx, p.ry, p.rz}},
                {"room_placement", p.layout}, {"world_origin", origin},
                {"world_basis", world_basis}}},
            {"role", p.role}, {"construction", p.assembly}, {"motion", p.motion},
            {"fillets", p.fillets}, {"profile_status", py::truthy(p.fillets) || py::truthy(p.chamfers) || py::truthy(p.machining) ? "nominal_before_3d_features" : "nominal"},
            {"chamfers", p.chamfers}, {"machining", p.machining},
            {"drilling", drilling}});
    }
    py::write_text(path, py::dumps(data, false, 2));
}

void csv_export(const Project& project, const std::string& path) {
    std::ofstream f(py::to_path(path), std::ios::binary);  // newline='' — csv.writer sam pisze \r\n
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
    f << "\xEF\xBB\xBF";  // utf-8-sig
    auto writerow = [&](const std::vector<std::string>& row) {
        std::vector<std::string> cells;
        for (auto& cell : row) {
            if (cell.find_first_of(";\"\r\n") != std::string::npos) {
                std::string quoted = "\"";
                for (char c : cell) { if (c == '"') quoted += "\"\""; else quoted += c; }
                cells.push_back(quoted + "\"");
            } else cells.push_back(cell);
        }
        f << py::join(cells, ";") << "\r\n";
    };
    writerow({"ID", "Grupa", "Element", "Materiał", "U gotowe mm", "V gotowe mm", "Grubość mm", "U do cięcia mm", "V do cięcia mm", "Obrzeże U0", "Obrzeże U1", "Obrzeże V0", "Obrzeże V1", "Włókna wzdłuż U", "Kontur", "Otwory U,V,D,głębokość mm", "Funkcja", "Wiercenia i standardy JSON", "Zaokrąglenia 3D JSON", "Fazy 3D JSON", "Frezowanie JSON", "Obrys parametryczny JSON", "Obrzeże całego obrysu", "Długość obrzeża m"});
    for (auto& p : project.parts) {
        auto blank = p.blank(project.edge_t);
        std::vector<std::string> row{p.id, p.group, p.name, project.mat(p.material).name, py::repr(p.u), py::repr(p.v), py::repr(p.t), py::repr(blank.first), py::repr(blank.second)};
        for (bool e : p.edges) row.push_back(e ? "TAK" : "");
        for (auto& cell : {std::string(p.grain ? (p.grain_v ? "TAK (V)" : "TAK") : ""), py::dumps(rows_json(p.contour)), py::dumps(rows_json(p.holes)), p.role, py::dumps(p.operations, false), py::dumps(p.fillets), py::dumps(p.chamfers), py::dumps(p.machining), py::dumps(p.profile), std::string(p.contour_edge ? "TAK" : ""), py::repr(p.edge_m())})
            row.push_back(cell);
        writerow(row);
    }
    if (!f) throw OSError(std::error_code(errno, std::generic_category()), path);
}

void dxf_export(const Project& project, const std::string& path) {
    for (auto& p : project.parts) {
        bool special = py::truthy(p.fillets) || py::truthy(p.chamfers) || py::truthy(p.machining);
        for (auto& o : p.operations) if ((o.at("face") != "top" && o.at("face") != "bottom") || py::truthy(py::get(o, "countersink_diameter"))) special = true;
        if (special)
            throw ValueError("Projekt ma obróbki bryłowe 3D lub wiercenia krawędziowe. Użyj eksportu STEP; zwykły DXF nie opisuje tej obróbki.");
    }
    std::vector<std::string> lines{"0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", "AC1015", "9", "$INSUNITS", "70", "4", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"}; double offset = 0;
    auto pair = [&](int code, const json& value) { lines.push_back(std::to_string(code)); lines.push_back(py::str(value)); };
    for (auto& p : project.parts) {
        std::string layer = "P_" + p.id;
        pair(0, "LWPOLYLINE"); pair(8, layer); pair(90, p.points().size()); pair(70, 1);
        for (auto& [x, y] : p.points()) { pair(10, x + offset); pair(20, y); }
        for (auto& hole : p.holes) {
            if (hole.size() != 4) throw ValueError(hole.size() < 4 ? std::format("not enough values to unpack (expected 4, got {})", hole.size()) : "too many values to unpack (expected 4)");
            double x = hole[0], y = hole[1], d = hole[2];
            pair(0, "CIRCLE"); pair(8, "H_" + p.id); pair(10, x + offset); pair(20, y); pair(30, 0); pair(40, d / 2);
        }
        for (auto& op : p.operations) {
            std::string face = op.at("face").get<std::string>();
            std::transform(face.begin(), face.end(), face.begin(), [](unsigned char c) { return (char)std::toupper(c); });
            pair(0, "CIRCLE"); pair(8, "AUTO_" + face + "_" + p.id);
            pair(10, op.at("u").get<double>() + offset); pair(20, op.at("v")); pair(30, 0); pair(40, op.at("diameter").get<double>() / 2);
        }
        offset += p.u + 100;
    }
    for (std::string s : {"0", "ENDSEC", "0", "EOF"}) lines.push_back(s);
    std::string text = py::join(lines, "\n") + "\n";
    for (unsigned char c : text) if (c >= 0x80) throw ValueError("'ascii' codec can't encode character: ordinal not in range(128)");
    py::write_text(path, text);
}

void report_export(Project& project, const std::string& path) {
    auto esc = py::html_escape; auto [sheets, bad] = sheet_nesting::plan(project); auto c = project.costs();
    auto part_name = [&](const json& id) -> std::string {
        for (auto& p : project.parts) if (json(p.id) == id) return p.name;
        throw StopIteration();
    };
    Camera cam(-38, 24, 0); cam.fit(project, 1100, 720);
    std::vector<std::string> out{"<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>" + esc(project.name) + "</title><style>body{font:15px Arial;color:#202938;max-width:1100px;margin:32px auto;padding:20px}table{border-collapse:collapse;width:100%;font-size:13px}td,th{border:1px solid #ccd3dc;padding:8px;text-align:left}th{background:#eef1f5}svg{max-width:100%;height:auto}h2{margin-top:32px}@media print{button{display:none}body{margin:0}svg{max-height:500px}h2{break-after:avoid}}</style><button onclick=\"print()\">Drukuj / zapisz PDF</button><h1>" + esc(project.name) + "</h1><p>Mebel Studio 3D · Wymiary w mm · Dokument roboczy</p>", svg_view(project, &cam)};
    out.push_back("<h2>Formatki</h2><p>Wymiary gotowe uwzględniają obrzeża. Wymiar do cięcia odejmuje grubość wskazanych obrzeży. Kontury nieregularne wymagają osobnej obróbki.</p><table><tr><th>Grupa / element</th><th>Materiał</th><th>Gotowe U × V × T</th><th>Do cięcia U × V</th><th>Obrzeża m</th></tr>");
    for (auto& p : project.parts) {
        auto [a, b] = p.blank(project.edge_t);
        out.push_back(std::format("<tr><td>{} / {}</td><td>{}</td><td>{:g} × {:g} × {:g}</td><td>{:g} × {:g}{}</td><td>{:.2f}</td></tr>", esc(p.group), esc(p.name), esc(project.mat(p.material).name), p.u, p.v, p.t, a, b, !p.contour.empty() ? " · kontur" : "", p.edge_m()));
    }
    out.push_back("</table><h2>Rzuty konstrukcji</h2>");
    for (auto& [name, yaw, pitch] : std::vector<std::tuple<std::string, double, double>>{{"Przód", 0, 0}, {"Bok", -90, 0}, {"Góra", 0, 90}}) {
        Camera view_cam(yaw, pitch, 0); view_cam.fit(project, 800, 500);
        out.push_back("<h3>" + name + "</h3>" + svg_view(project, &view_cam, 800, 500));
    }
    out.push_back("<h2>Kontury i otwory</h2>");
    for (auto& p : project.parts) {
        if (!p.contour.empty() || !p.holes.empty())
            out.push_back("<p><b>" + esc(p.name) + "</b> · ID " + p.id + " · kontur U,V: " + esc(contour_repr(p.contour)) + " · otwory U,V,średnica,głębokość: " + esc(holes_repr(p.holes)) + "</p>");
        if (py::truthy(p.operations))
            out.push_back("<p><b>" + esc(p.name) + "</b> · wiercenia automatyczne (wzorzec roboczy): " + esc(py::dumps(p.operations, false)) + "</p>");
        if (py::truthy(p.chamfers) || py::truthy(p.machining))
            out.push_back("<p><b>" + esc(p.name) + "</b> · fazy: " + esc(py::dumps(p.chamfers)) + "; frezowanie: " + esc(py::dumps(p.machining)) + ". Dokładna geometria w STEP.</p>");
        if (py::truthy(p.fillets)) {
            std::string numbering = py::truthy(p.profile) ? "rzeczywiste łuki i odcinki obrysu CAD" : "punkty konturu";
            out.push_back("<p><b>" + esc(p.name) + "</b> · zaokrąglenia bryłowe: " + esc(py::dumps(p.fillets)) + ". Numeracja krawędzi: " + numbering + "; dokładna geometria w STEP.</p>");
        }
    }
    out.push_back("<h2>Okucia i powiązania</h2><table><tr><th>Okucie</th><th>Ilość</th><th>Powiązanie</th><th>Status</th></tr>");
    for (auto& item : project.hardware) {
        std::string owner = "Ręczne";
        for (auto& p : project.parts) if (json(p.id) == py::get(item, "owner_id")) { owner = p.name; break; }
        out.push_back(std::format("<tr><td>{}</td><td>{:g}</td><td>{}</td><td>{}</td></tr>", esc(item.at("name").get<std::string>()), item.at("qty").get<double>(), esc(owner), py::truthy(py::get(item, "owner_id")) ? "Wzorzec roboczy — do sprawdzenia" : "Ręczne"));
    }
    out.push_back("</table>");
    out.push_back("<h2>Układ rozkroju</h2><p>" + std::string(sheet_nesting::mode() == sheet_nesting::Mode::Optimized ? "Optymalizator gilotynowy (wiele wariantów, wybrany najlepszy); nie gwarantuje absolutnego minimum płyt." : "Heurystyka gilotynowa; nie gwarantuje minimum płyt.") + " Grupowanie według materiału i grubości. Kierunek włókien wzdłuż U. Rzaz " + std::format("{:g} mm; margines {:g} mm.</p>", project.kerf, project.margin));
    for (size_t i = 0; i < sheets.size(); ++i) {
        auto& s = sheets[i];
        out.push_back(std::format("<h3>Płyta {}: {}, {:g} mm</h3><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\"><rect width=\"{}\" height=\"{}\" fill=\"#f2f4f7\" stroke=\"#333\"/>", i + 1, esc(project.mat(s["material"].get<std::string>()).name), s["thickness"].get<double>(), py::str(s["u"]), py::str(s["v"]), py::str(s["u"]), py::str(s["v"])));
        for (size_t n = 0; n < s["pieces"].size(); ++n) {
            auto& r = s["pieces"][n];
            part_name(r["id"]);
            out.push_back(std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"#c4a276\" stroke=\"#444\"/><text x=\"{}\" y=\"{}\" font-size=\"28\">{}</text>", py::str(r["x"]), py::str(r["y"]), py::str(r["u"]), py::str(r["v"]), py::repr(r["x"].get<double>() + 8), py::repr(r["y"].get<double>() + 35), n + 1));
        }
        std::vector<std::string> labels;
        for (size_t n = 0; n < s["pieces"].size(); ++n) {
            auto& r = s["pieces"][n];
            labels.push_back(std::format("{}: {} {:g}×{:g}", n + 1, esc(part_name(r["id"])), r["u"].get<double>(), r["v"].get<double>()));
        }
        out.push_back("</svg><p>" + py::join(labels, "; ") + "</p>");
    }
    if (!bad.empty()) {
        std::vector<std::string> names; for (auto& i : bad) names.push_back(esc(part_name(i)));
        out.push_back("<p>Nie mieszczą się na płycie: " + py::join(names, ", ") + "</p>");
    }
    out.push_back("<h2>Kosztorys orientacyjny</h2><table>");
    for (auto& [label, key] : std::vector<std::pair<std::string, std::string>>{{"Materiał według powierzchni netto", "panels"}, {"Obrzeża", "edges"}, {"Akcesoria", "hardware"}, {"Robocizna", "labor"}, {"Koszt bazowy", "base"}, {std::format("Cena z narzutem {:g}%", project.markup), "total"}})
        out.push_back(std::format("<tr><td>{}</td><td>{:.2f} zł</td></tr>", label, c.at(key)));
    double stock = 0;
    for (auto& s : sheets) stock += project.mat(s["material"].get<std::string>()).cost(Part{.u = s["u"].get<double>(), .v = s["v"].get<double>(), .t = s["thickness"].get<double>()});
    out.push_back(std::format("</table><p>Zakup pełnych płyt z układu: {:.2f} zł. Kosztorys netto powierzchni nie obejmuje odpadów, podatku, transportu ani dodatkowej obróbki.</p><p>DXF zawiera nominalne kontury i koła otworów, bez głębokości, kompensacji narzędzia ani programu CNC. Model nie sprawdza wytrzymałości i wszystkich kolizji. Przed produkcją zweryfikuj konstrukcję oraz dokumentację.</p></html>", stock));
    py::write_text(path, py::join(out, ""));
}

// Verified C++ calculation ports; UI and exact CAD validators are retained.
// (W Pythonie: native_model.install_core(globals()) podmieniał te funkcje na
// porty C++. Ten plik jest już w C++, więc podmiana nie ma odpowiednika.)

}  // namespace core
