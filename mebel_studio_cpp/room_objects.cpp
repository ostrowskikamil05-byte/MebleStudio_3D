// Editable purchased room objects, separate from manufactured furniture.
#include "room_objects.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

#include "core.h"
#include "fitting_visuals.h"
#include "room_architecture.h"

namespace room_objects {

using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::shade;
using fitting_visuals::Faces;
using fitting_visuals::box;
using fitting_visuals::cylinder;
using fitting_visuals::rod;
using py::ValueError;

const json ROOMS = json{{"neutral", "Neutralne"}, {"kitchen", "Kuchnia"}, {"bathroom", "Łazienka"},
                        {"living", "Salon"}, {"bedroom", "Sypialnia"}, {"garage", "Garaż / warsztat"}, {"garden", "Ogród"}};

const json CATALOG = json{
    {"washer", {{"name", "Pralka"}, {"width", 600}, {"depth", 600}, {"height", 850}, {"color", "#f1f0eb"}}},
    {"dryer", {{"name", "Suszarka"}, {"width", 600}, {"depth", 620}, {"height", 850}, {"color", "#e8e7e3"}}},
    {"fridge", {{"name", "Lodówka"}, {"width", 600}, {"depth", 650}, {"height", 1850}, {"color", "#b8c0c4"}}},
    {"oven", {{"name", "Piekarnik"}, {"width", 595}, {"depth", 560}, {"height", 595}, {"color", "#373d43"}, {"z", 600}}},
    {"hob", {{"name", "Płyta grzewcza"}, {"width", 600}, {"depth", 520}, {"height", 60}, {"color", "#242b30"}, {"z", 800}}},
    {"hood", {{"name", "Okap"}, {"width", 600}, {"depth", 450}, {"height", 650}, {"color", "#b7bec2"}, {"z", 1400}}},
    {"dishwasher", {{"name", "Zmywarka"}, {"width", 600}, {"depth", 570}, {"height", 820}, {"color", "#d7dadd"}}},
    {"washbasin", {{"name", "Umywalka"}, {"width", 600}, {"depth", 450}, {"height", 180}, {"color", "#f0f1ed"}, {"z", 760}}},
    {"sink", {{"name", "Zlew"}, {"width", 780}, {"depth", 480}, {"height", 200}, {"color", "#a9b2b6"}, {"z", 620}}},
    {"faucet", {{"name", "Kran"}, {"width", 140}, {"depth", 220}, {"height", 350}, {"color", "#aab3b8"}, {"z", 820}}},
    {"shower", {{"name", "Kabina prysznicowa"}, {"width", 900}, {"depth", 900}, {"height", 2000}, {"color", "#b1c8c7"}}},
    {"mirror", {{"name", "Lustro"}, {"width", 700}, {"depth", 30}, {"height", 900}, {"color", "#c0d1d6"}, {"z", 900}}},
    {"shelf", {{"name", "Półka zakupowa z podpórkami"}, {"width", 800}, {"depth", 250}, {"height", 120}, {"color", "#bc9970"}, {"z", 1000}}},
    {"hanger", {{"name", "Wieszak"}, {"width", 600}, {"depth", 80}, {"height", 160}, {"color", "#515f63"}, {"z", 1250}}},
    {"lamp", {{"name", "Lampa wisząca"}, {"width", 350}, {"depth", 350}, {"height", 600}, {"color", "#c5b497"}, {"z", 1900}}},
    {"bulb", {{"name", "Żarówka"}, {"width", 80}, {"depth", 80}, {"height", 120}, {"color", "#fff0bc"}, {"z", 1950}}}};

namespace {

constexpr double TAU = 2 * std::numbers::pi;  // math.tau

const std::set<std::string> OBJECT_KEYS{"id", "type", "name", "x", "y", "z", "yaw", "width", "depth", "height", "color", "enabled", "price", "light_enabled"};
const std::set<std::string> SCENE_KEYS{"room", "brightness", "led_enabled", "lights_enabled", "objects", "architecture"};

// _color(value)
std::string color_of(const json& value) {
    if (!value.is_string() || py::len(value.get<std::string>()) != 7 || value.get<std::string>()[0] != '#') throw ValueError("Kolor wyposażenia: #RRGGBB.");
    std::string text = value.get<std::string>();
    try { py::int16(text.substr(1)); }
    catch (const std::invalid_argument&) { throw ValueError("Kolor wyposażenia: #RRGGBB."); }
    return py::lower(text);
}

// _number(value, low, high)
double number(const json& value, double low = -100000, double high = 100000) {
    if (value.is_boolean()) throw ValueError("Wpisz wartość liczbową wyposażenia.");
    try { return finite(value, low, high); }
    catch (const nlohmann::json::exception&) { throw ValueError("Wpisz skończoną wartość liczbową wyposażenia."); }
}

bool valid_identifier(const std::string& s) {
    if (s.empty() || s.size() > 80) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

// dict(a, **b) / a.update(b)
void update(json& target, const json& source) { for (auto& [k, v] : source.items()) target[k] = v; }

json point_json(const Point3& p) { return json::array({p[0], p[1], p[2]}); }

}  // namespace

json default_scene() {
    return json{{"room", "neutral"}, {"brightness", 1.0}, {"led_enabled", true}, {"lights_enabled", true}, {"objects", json::array()},
                {"architecture", room_architecture::defaults()}};
}

json normalize_object(const json& obj) {
    if (!obj.is_object()) throw ValueError("Nieprawidłowe dane obiektu wyposażenia.");
    for (auto& [key, v] : obj.items()) if (!OBJECT_KEYS.count(key)) throw ValueError("Nieprawidłowe dane obiektu wyposażenia.");
    json kind = py::get(obj, "type");
    if (!kind.is_string() || !CATALOG.contains(kind.get<std::string>())) throw ValueError("Wybierz obiekt z katalogu wyposażenia pomieszczenia.");
    json ident = py::get(obj, "id");
    if (!ident.is_string() || !valid_identifier(ident.get<std::string>())) throw ValueError("Obiekt sceny wymaga identyfikatora długości do 80 znaków.");
    const json& def = CATALOG.at(kind.get<std::string>());
    json result = def;
    result["id"] = ident; result["type"] = kind; result["x"] = 0; result["y"] = 0; result["z"] = py::get(def, "z", 0); result["yaw"] = 0;
    result["enabled"] = true; result["price"] = 0; result["light_enabled"] = true;
    update(result, obj);
    if (!result["name"].is_string() || py::strip(result["name"].get<std::string>()).empty() || py::len(result["name"].get<std::string>()) > 160)
        throw ValueError("Nazwa wyposażenia: od 1 do 160 znaków.");
    result["name"] = py::strip(result["name"].get<std::string>());
    for (const char* key : {"x", "y", "z"}) result[key] = number(result[key]);
    for (const char* key : {"width", "depth", "height"}) result[key] = number(result[key], 1, 15000);
    result["yaw"] = py::mod(number(result["yaw"], -360000, 360000) + 180, 360) - 180;
    result["price"] = number(result["price"], 0, 10000000);
    result["color"] = color_of(result["color"]);
    if (!result["enabled"].is_boolean() || !result["light_enabled"].is_boolean()) throw ValueError("Widoczność i światło obiektu wymagają TAK / NIE.");
    return result;
}

json validate_scene(const json& scene) {
    if (!scene.is_object()) throw ValueError("Nieprawidłowe ustawienia pomieszczenia.");
    for (auto& [key, v] : scene.items()) if (!SCENE_KEYS.count(key)) throw ValueError("Nieprawidłowe ustawienia pomieszczenia.");
    json result = default_scene();
    update(result, scene);
    if (!result["room"].is_string() || !ROOMS.contains(result["room"].get<std::string>())) throw ValueError("Wybierz pomieszczenie z listy.");
    result["brightness"] = number(result["brightness"], .1, 2);
    result["architecture"] = room_architecture::validate(result["architecture"]);
    if (!result["led_enabled"].is_boolean() || !result["lights_enabled"].is_boolean()) throw ValueError("Przełączniki LED i lamp wymagają TAK / NIE.");
    if (!result["objects"].is_array() || result["objects"].size() > MAX_OBJECTS)
        throw ValueError("Scena może mieć najwyżej " + std::to_string(MAX_OBJECTS) + " zakupowych obiektów.");
    json objects = json::array();
    for (const auto& obj : result["objects"]) objects.push_back(normalize_object(obj));
    result["objects"] = objects;
    std::set<std::string> ids;
    for (const auto& obj : objects) ids.insert(obj["id"].get<std::string>());
    if (ids.size() != objects.size()) throw ValueError("Obiekty sceny mają powtarzające się identyfikatory.");
    return result;
}

json scene_of(const Project& project) { return validate_scene(project.scene); }

json make_object(const std::string& kind, const json& values) {
    json rest = values;
    json ident = rest.contains("id") ? rest["id"] : json(core::uid());
    rest.erase("id");
    if (rest.contains("type")) py::type_error("dict() got multiple values for keyword argument 'type'");
    json obj = json::object();
    obj["type"] = kind; obj["id"] = ident;
    update(obj, rest);
    return normalize_object(obj);
}

ProjectPtr add_object(const Project& project, const std::string& kind, const json& values) {
    auto result = std::make_shared<Project>(project);
    json scene = scene_of(*result);
    scene["objects"].push_back(make_object(kind, values));
    result->scene = validate_scene(scene);
    return result;
}

ProjectPtr update_object(const Project& project, const std::string& object_id, const json& changes) {
    if (changes.contains("id")) throw ValueError("Identyfikator obiektu jest stały; dodaj nowy obiekt, aby utworzyć kopię.");
    auto result = std::make_shared<Project>(project);
    json scene = scene_of(*result);
    std::optional<size_t> index;
    for (size_t i = 0; i < scene["objects"].size(); ++i) if (scene["objects"][i]["id"] == object_id) { index = i; break; }
    if (!index) throw ValueError("Obiekt został usunięty; wybierz istniejące wyposażenie.");
    json merged = scene["objects"][*index];
    update(merged, changes);
    scene["objects"][*index] = normalize_object(merged);
    result->scene = validate_scene(scene);
    return result;
}

ProjectPtr delete_object(const Project& project, const std::string& object_id) {
    auto result = std::make_shared<Project>(project);
    json scene = scene_of(*result);
    bool present = false;
    for (const auto& obj : scene["objects"]) if (obj["id"] == object_id) present = true;
    if (!present) throw ValueError("Nie znaleziono wyposażenia do usunięcia.");
    json objects = json::array();
    for (const auto& obj : scene["objects"]) if (obj["id"] != object_id) objects.push_back(obj);
    scene["objects"] = objects;
    result->scene = scene;
    return result;
}

ProjectPtr set_scene(const Project& project, const json& changes) {
    if (changes.contains("objects")) throw ValueError("Obiekty dodawaj lub edytuj osobno.");
    auto result = std::make_shared<Project>(project);
    json scene = scene_of(*result);
    update(scene, changes);
    result->scene = validate_scene(scene);
    return result;
}

Point3 object_point(const json& obj, const Point3& point) {
    double cx = obj["width"].get<double>() / 2, cy = obj["depth"].get<double>() / 2;
    Point3 r = core::rotate({point[0] - cx, point[1] - cy, point[2]}, 0, 0, obj["yaw"].get<double>());
    return {r[0] + cx + obj["x"].get<double>(), r[1] + cy + obj["y"].get<double>(), r[2] + obj["z"].get<double>()};
}

namespace {
constexpr double CORNERS[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
std::vector<Point3> corners(const json& obj) {
    std::vector<Point3> out;
    for (const auto& c : CORNERS)
        out.push_back(object_point(obj, {c[0] * obj["width"].get<double>(), c[1] * obj["depth"].get<double>(), c[2] * obj["height"].get<double>()}));
    return out;
}
}  // namespace

std::vector<Point3> object_vertices(const Project& project, bool include_disabled) {
    std::vector<Point3> out;
    json scene = scene_of(project);
    for (const auto& obj : scene["objects"]) {
        if (!(include_disabled || obj["enabled"].get<bool>())) continue;
        for (const auto& p : corners(obj)) out.push_back(p);
    }
    return out;
}

std::pair<Point3, Point3> object_bounds(const json& obj_) {
    json obj = normalize_object(obj_);
    auto points = corners(obj);
    Point3 lo, hi;
    for (int i = 0; i < 3; ++i) {
        lo[i] = points[0][i]; hi[i] = points[0][i];
        for (const auto& p : points) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    }
    return {lo, hi};
}

json inventory(const Project& project) {
    json rows = json::array();
    json scene = scene_of(project);
    for (const auto& obj : scene["objects"])
        rows.push_back(json{{"id", obj["id"]}, {"type", obj["type"]}, {"name", obj["name"]}, {"qty", 1}, {"unit", "szt."},
                            {"price", obj["price"]}, {"total", obj["price"]}, {"enabled", obj["enabled"]},
                            {"width", obj["width"]}, {"depth", obj["depth"]}, {"height", obj["height"]}, {"source", ""},
                            {"note", "Własny model zakupowego wyposażenia; sprawdź artykuł i wymiary montażowe."}});
    return rows;
}

double purchased_total(const Project& project) {
    std::vector<double> totals;
    for (const auto& row : inventory(project)) totals.push_back(row["total"].get<double>());
    return py::sum(totals);
}

json object_lights(const Project& project) {
    json scene = scene_of(project);
    json out = json::array();
    if (!scene["lights_enabled"].get<bool>()) return out;
    for (const auto& obj : scene["objects"]) {
        std::string type = obj["type"];
        if (!(obj["enabled"].get<bool>() && obj["light_enabled"].get<bool>() && (type == "lamp" || type == "bulb"))) continue;
        double w = obj["width"], d = obj["depth"], h = obj["height"];
        // min(3000, max(800, w*5)): wynik zachowuje typ wybranego argumentu
        double scaled = w * 5;
        json radius = scaled > 800 ? json(scaled) : json(800);
        if (!(radius.get<double>() < 3000)) radius = 3000;
        out.push_back(json{{"id", obj["id"]}, {"position", point_json(object_point(obj, {w / 2, d / 2, h * .18}))},
                           {"color", type == "lamp" ? json("#fff0ba") : obj["color"]}, {"intensity", scene["brightness"]},
                           {"radius", radius}, {"kind", type}});
    }
    return out;
}

namespace {

using Ring = std::vector<Point3>;

Ring ellipse(double cx, double cy, double z, double rx, double ry, int count = 16) {
    Ring out;
    for (int i = 0; i < count; ++i) out.push_back({cx + rx * std::cos(i * TAU / count), cy + ry * std::sin(i * TAU / count), z});
    return out;
}

std::vector<GeometryFace> ring_xz(double cx, double y, double cz, double rx, double rz, const std::string& color, const std::string& glass_color, int count) {
    Ring outer, inner;
    for (int i = 0; i < count; ++i) outer.push_back({cx + rx * std::cos(i * TAU / count), y, cz + rz * std::sin(i * TAU / count)});
    for (int i = 0; i < count; ++i) inner.push_back({cx + rx * .82 * std::cos(i * TAU / count), y - .001, cz + rz * .82 * std::sin(i * TAU / count)});
    std::vector<GeometryFace> out{{inner, glass_color, false, true}};
    for (int i = 0; i < count; ++i) out.push_back({{outer[i], outer[(i + 1) % count], inner[(i + 1) % count], inner[i]}, color, false, false});
    return out;
}

std::vector<GeometryFace> bowl(double cx, double cy, double w, double d, double h, const std::string& color, int count, bool rectangular_rim = false) {
    Ring outer = ellipse(cx, cy, h, w / 2, d / 2, count);
    if (rectangular_rim) {
        outer.clear();
        for (int i = 0; i < count; ++i) {
            double a = i * TAU / count, c = std::cos(a), s = std::sin(a), scale = std::max(std::abs(c), std::abs(s));
            outer.push_back({cx + w / 2 * c / scale, cy + d / 2 * s / scale, h});
        }
    }
    Ring inner = ellipse(cx, cy, h - .025 * h, w * .41, d * .39, count);
    Ring low = ellipse(cx, cy, h * .1, w * .20, d * .20, count);
    Ring shell = ellipse(cx, cy, 0, w * .24, d * .23, count);
    std::vector<GeometryFace> faces{{low, shade(color, .70), false, false}};
    for (int i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        faces.push_back({{outer[i], outer[j], inner[j], inner[i]}, color, false, false});
        faces.push_back({{inner[i], inner[j], low[j], low[i]}, shade(color, .86), false, false});
        faces.push_back({{outer[i], outer[j], shell[j], shell[i]}, shade(color, .94), false, false});
    }
    return faces;
}

std::vector<GeometryFace> build_geometry(const std::string& key) {
    json data = json::parse(key);
    std::string kind = data["type"];
    double w = data["width"], d = data["depth"], h = data["height"];
    std::string color = data["color"];
    int n = py::truthy(py::get(data, "interactive")) ? 8 : 16;
    std::vector<GeometryFace> out;
    auto put = [&](const Faces& faces, std::optional<std::string> col = std::nullopt, bool emissive = false, bool glass = false) {
        for (const auto& face : faces) out.push_back({face, col ? *col : color, emissive, glass});
    };
    auto bx = [&](double x, double y, double z, double a, double b, double c, std::optional<std::string> col = std::nullopt) { put(box(x, y, z, a, b, c), col); };
    auto extend = [&](const std::vector<GeometryFace>& more) { out.insert(out.end(), more.begin(), more.end()); };
    if (kind == "washer" || kind == "dryer" || kind == "fridge" || kind == "oven" || kind == "dishwasher") {
        bx(0, d * .01, 0, w, d * .99, h);
        if (kind == "washer" || kind == "dryer") {
            bx(w * .04, 0, h * .82, w * .92, d * .012, h * .13, shade(color, .88));
            extend(ring_xz(w * .5, d * .006, h * .43, w * .29, std::min(h * .26, w * .29), "#606b72", "#384c58", n));
            extend(ring_xz(w * .79, 0, h * .886, w * .036, w * .036, "#87939a", "#bdc9c7", n));
        } else if (kind == "fridge") {
            bx(w * .025, 0, h * .025, w * .95, d * .012, h * .29, shade(color, .97));
            bx(w * .025, 0, h * .33, w * .95, d * .012, h * .64, shade(color, 1.04));
            double r = std::min(w * .012, d * .014);
            put(rod({w * .9, d * .04, h * .12}, {w * .9, d * .04, h * .25}, r), "#5c6c75");
            put(rod({w * .9, d * .04, h * .45}, {w * .9, d * .04, h * .78}, r), "#5c6c75");
        } else if (kind == "oven") {
            bx(w * .05, 0, h * .15, w * .9, d * .015, h * .58, "#1f2f38");
            bx(w * .08, 0, h * .8, w * .84, d * .018, h * .13, "#8c999e");
            double r = std::min(h * .012, d * .015);
            put(rod({w * .12, d * .04, h * .75}, {w * .88, d * .04, h * .75}, r), "#c2c9cb");
        } else {
            bx(w * .025, 0, h * .12, w * .95, d * .012, h * .76, shade(color, .98));
            bx(w * .025, 0, h * .9, w * .95, d * .014, h * .065, "#626e74");
            bx(w * .06, d * .03, 0, w * .88, d * .02, h * .09, "#455159");
        }
    } else if (kind == "hob") {
        bx(0, 0, 0, w, d, h, "#26323b");
        for (auto [cx, cy, r0] : std::vector<std::array<double, 3>>{{w * .25, d * .3, w * .13}, {w * .72, d * .3, w * .11}, {w * .27, d * .73, w * .11}, {w * .72, d * .73, w * .15}}) {
            double r = std::min(r0, d * .18);
            Ring ring = ellipse(cx, cy, h, r, r, n), inner = ellipse(cx, cy, h, r * .94, r * .94, n);
            for (int i = 0; i < n; ++i) put({{ring[i], ring[(i + 1) % n], inner[(i + 1) % n], inner[i]}}, "#b7c2c6");
        }
    } else if (kind == "hood") {
        bx(w * .32, d * .24, h * .2, w * .36, d * .5, h * .8);
        Ring r0{{0, 0, 0}, {w, 0, 0}, {w, d, 0}, {0, d, 0}};
        Ring r1{{w * .25, d * .18, h * .2}, {w * .75, d * .18, h * .2}, {w * .75, d * .82, h * .2}, {w * .25, d * .82, h * .2}};
        Faces faces{r0, r1};
        for (int i = 0; i < 4; ++i) faces.push_back({r0[i], r0[(i + 1) % 4], r1[(i + 1) % 4], r1[i]});
        put(faces);
    } else if (kind == "washbasin" || kind == "sink") {
        if (kind == "washbasin") extend(bowl(w / 2, d / 2, w, d, h, color, n));
        else {
            extend(bowl(w * .32, d / 2, w * .64, d, h, color, n, true));
            bx(w * .64, 0, h * .94, w * .36, d, h * .06, shade(color, .95));
            for (int k = 0; k < 5; ++k) bx(w * (.69 + .055 * k), d * .12, h * .995, w * .007, d * .76, h * .005, shade(color, .8));
        }
    } else if (kind == "faucet") {
        double r = std::min({w * .12, d * .065, h * .05});
        double cx = w * .45, cy = d * .74;
        put(cylinder(cx, cy, 0, r * 1.3, h * .07, n));
        put(rod({cx, cy, h * .05}, {cx, cy, h * .84}, r));
        put(rod({cx, cy, h * .84}, {cx, d * .15, h * .84}, r));
        put(rod({cx, d * .15, h * .84}, {cx, d * .15, h * .68}, r));
        put(rod({cx, cy, h * .35}, {w * .84, cy, h * .35}, r * .55));
    } else if (kind == "shower") {
        bx(0, 0, 0, w, d, h * .025, "#e4e9e6");
        double thickness = std::min(w, d) * .013;
        for (auto [x, y] : std::vector<std::array<double, 2>>{{thickness, thickness}, {w - thickness, thickness}, {w - thickness, d - thickness}, {thickness, d - thickness}})
            put(rod({x, y, h * .025}, {x, y, h - thickness}, thickness * .45), "#788a91");
        put({{{thickness, thickness, h * .025}, {w - thickness, thickness, h * .025}, {w - thickness, thickness, h - thickness}, {thickness, thickness, h - thickness}},
             {{w - thickness, thickness, h * .025}, {w - thickness, d - thickness, h * .025}, {w - thickness, d - thickness, h - thickness}, {w - thickness, thickness, h - thickness}}},
            color, false, true);
        for (double y : {thickness, d - thickness}) put(rod({thickness, y, h - thickness}, {w - thickness, y, h - thickness}, thickness * .45), "#788a91");
    } else if (kind == "mirror") {
        bx(0, d * .3, 0, w, d * .7, h, "#4e626e");
        put({{{w * .025, 0, h * .025}, {w * .975, 0, h * .025}, {w * .975, 0, h * .975}, {w * .025, 0, h * .975}}}, color, false, true);
    } else if (kind == "shelf") {
        bx(0, 0, h * .75, w, d, h * .25);
        double r = std::min(h * .055, d * .018);
        for (double x : {w * .15, w * .85}) {
            put(rod({x, d * .94, r}, {x, d * .94, h * .75}, r), "#6a797c");
            put(rod({x, d * .94, r}, {x, d * .12, h * .75}, r), "#6a797c");
        }
    } else if (kind == "hanger") {
        bx(0, d * .75, h * .38, w, d * .25, h * .24);
        double r = std::min({d * .04, h * .04, w * .012});
        for (int k = 0; k < 5; ++k) {
            double x = w * (.1 + .2 * k);
            put(rod({x, d * .78, h * .5}, {x, d * .2, h * .5}, r));
            put(rod({x, d * .2, h * .5}, {x, d * .2, h * .73}, r));
        }
    } else if (kind == "lamp" || kind == "bulb") {
        if (kind == "lamp") {
            put(cylinder(w / 2, d / 2, h * .04, std::min(w, d) * .12, h * .26, n), "#fff0ba", true);
            put(cylinder(w / 2, d / 2, h * .38, std::min(w, d) * .035, h * .62, n), "#505759");
            Ring a = ellipse(w / 2, d / 2, h * .03, w * .49, d * .49, n), b = ellipse(w / 2, d / 2, h * .42, w * .25, d * .25, n);
            Faces faces;
            for (int i = 0; i < n; ++i) faces.push_back({a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]});
            put(faces);
        } else {
            std::vector<Ring> rings;
            for (auto [level, rx] : std::vector<std::array<double, 2>>{{0, .1}, {h * .2, .49}, {h * .55, .45}, {h * .75, .2}}) rings.push_back(ellipse(w / 2, d / 2, level, w * rx, d * rx, n));
            Faces faces{rings[0], rings.back()};
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < n; ++i) faces.push_back({rings[k][i], rings[k][(i + 1) % n], rings[k + 1][(i + 1) % n], rings[k + 1][i]});
            put(faces, color, true);
            put(cylinder(w / 2, d / 2, h * .75, std::min(w, d) * .2, h * .25, n), "#7c878d");
        }
    }
    return out;
}

py::LruCache<std::vector<GeometryFace>>& geometry_cache() {
    static py::LruCache<std::vector<GeometryFace>> cache(256);
    return cache;
}

}  // namespace

const std::vector<GeometryFace>& _geometry(const std::string& key) { return geometry_cache().get(key, build_geometry); }

json room_world_faces(const Project& project, bool interactive) {
    json scene = scene_of(project);
    json faces = json::array();
    for (const auto& obj : scene["objects"]) {
        if (!obj["enabled"].get<bool>()) continue;
        json spec = json::object();
        for (const char* k : {"type", "width", "depth", "height", "color"}) spec[k] = obj[k];
        spec["interactive"] = interactive;
        std::string key = py::dumps(spec, true, -1, true);
        bool lit = scene["lights_enabled"].get<bool>() && obj["light_enabled"].get<bool>();
        for (const auto& g : _geometry(key)) {
            json world = json::array();
            for (const auto& point : g.face) world.push_back(point_json(object_point(obj, point)));
            faces.push_back(json{{"world", world}, {"color", g.color}, {"id", "scene:" + obj["id"].get<std::string>()}, {"object_id", obj["id"]},
                                 {"scene_object", true}, {"fitting", true}, {"emissive", g.emissive && lit}, {"glass", g.glass}});
        }
    }
    return faces;
}

json room_faces(const Project& project, const core::Camera& camera, std::pair<double, double> size, const std::optional<std::string>& selected, bool interactive) {
    auto [width, height] = size;
    std::vector<json> out;
    for (const auto& face : room_world_faces(project, interactive)) {
        const json& points = face["world"];
        auto at = [&](size_t i) { return Point3{points[i][0], points[i][1], points[i][2]}; };
        Point3 a = at(0), b = at(1), c = at(2);
        Point3 ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        Point3 normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        double norm = std::sqrt(py::sum({normal[0] * normal[0], normal[1] * normal[1], normal[2] * normal[2]}));
        if (norm == 0) norm = 1;
        for (auto& v : normal) v /= norm;
        constexpr double LIGHT[3] = {-.3, -.5, .81};
        double light = std::abs(py::sum({normal[0] * LIGHT[0], normal[1] * LIGHT[1], normal[2] * LIGHT[2]}));
        json screen = json::array();
        std::vector<double> depths;
        for (size_t i = 0; i < points.size(); ++i) {
            Point3 q = camera.project(at(i), width, height);
            screen.push_back(json::array({q[0], q[1]}));
            depths.push_back(q[2]);
        }
        double count = (double)points.size();
        std::array<double, 3> center{};
        for (int k = 0; k < 3; ++k) {
            std::vector<double> column;
            for (size_t i = 0; i < points.size(); ++i) column.push_back(points[i][k].get<double>());
            center[k] = py::sum(column) / count;
        }
        json item = face;
        item["points"] = screen; item["depth"] = py::sum(depths) / count; item["normal"] = point_json(normal); item["center"] = point_json(center);
        item["color"] = face["emissive"].get<bool>() ? face["color"] : json(shade(face["color"].get<std::string>(), .7 + .3 * light));
        item["selected"] = selected && (*selected == face["id"].get<std::string>() || *selected == face["object_id"].get<std::string>());
        item["mesh"] = false;
        out.push_back(item);
    }
    std::stable_sort(out.begin(), out.end(), [](const json& x, const json& y) { return x["depth"].get<double>() < y["depth"].get<double>(); });
    json result = json::array();
    for (auto& item : out) result.push_back(std::move(item));
    return result;
}

}  // namespace room_objects
