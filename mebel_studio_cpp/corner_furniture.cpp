// Editable open corner carcasses with real planar production parts.
//
// The concave inner corner can remain square, be chamfered or be rounded.
// Rounded outlines are explicitly tessellated; no diagonal hinge standard is
// invented and this factory does not automatically attach a diagonal front.
#include "corner_furniture.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <numbers>
#include <set>

#include "configurator_accessories.h"
#include "drag_hardware.h"
#include "features.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "workspace_worktops.h"

namespace corner_furniture {

using core::Part;
using core::Point;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::local_world;
using core::validate_polygon;
using furniture_configurator::_geometry;
using furniture_layout::group_layout;
using py::ValueError;

namespace {

constexpr double TAU = 2 * std::numbers::pi;
double num(const json& value) { return value.get<double>(); }
double radians(double degrees) { return degrees * (std::numbers::pi / 180); }
double dist(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[1] - b[1]); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

std::vector<std::vector<double>> rows(const std::vector<Point>& points) {
    std::vector<std::vector<double>> out; for (auto& p : points) out.push_back({p[0], p[1]});
    return out;
}

bool contains_string(const std::set<std::string>& ids, const json& value) { return value.is_string() && ids.contains(value.get<std::string>()); }

}  // namespace

json default_corner_config(const Project* project, const std::string& furniture_type) {
    bool wardrobe = furniture_type == "wardrobe";
    return {{"name", wardrobe ? "Szafa narożna" : "Korpus narożny"}, {"furniture_type", furniture_type}, {"width", 900}, {"depth", 900}, {"height", wardrobe ? 2100 : 720}, {"arm_depth", 560}, {"arm_depth_y", 560},
            {"thickness", 18}, {"shape", "L"}, {"side", "left"}, {"radius", 100}, {"segments", 12},
            {"shelves", wardrobe ? 4 : 1}, {"top_mode", "panel"}, {"rail_width", 80}, {"origin", json::array({0, 0, 0})}, {"diagonal_angle", 45},
            {"fronts", "open"}, {"front_thickness", 18}, {"gap", 2}, {"hinge_pattern", "hinge45"}, {"hinge_count", wardrobe ? 4 : 2}, {"opening_angle", 100},
            {"handle_mode", "handle"}, {"handle_spacing", 128}, {"front_style", "smooth"},
            {"leg_height", 0}, {"leg_count", 6}, {"leg_diameter", 40}, {"leg_setback", 60}, {"leg_color", "#242628"}, {"leg_price", 0},
            {"material", project ? json(project->materials.at(0).id) : json("")},
            {"front_material", project ? json(project->materials.at(std::min<size_t>(1, project->materials.size() - 1)).id) : json("")}};
}

json normalize_corner_config(const json& config, const Project* project) {
    if (!config.is_object()) throw ValueError("Podaj parametry narożnika.");
    json type = py::get(config, "furniture_type", "cabinet");
    json c = default_corner_config(project, type.is_string() ? type.get<std::string>() : ""); for (auto& [k, v] : config.items()) c[k] = v;
    if (!config.contains("arm_depth_y")) c["arm_depth_y"] = c["arm_depth"];
    if (!c["name"].is_string() || py::strip(c["name"]).empty()) throw ValueError("Wpisz nazwę narożnika.");
    c["name"] = py::strip(c["name"]);
    for (auto& [key, low, high] : std::vector<std::tuple<std::string, double, double>>{{"width", 150, 10000}, {"depth", 150, 10000}, {"height", 100, 10000}, {"arm_depth", 80, 3000}, {"arm_depth_y", 80, 3000},
                                                                                       {"thickness", 6, 60}, {"radius", 0, 1500}, {"rail_width", 20, 300}, {"diagonal_angle", 15, 75},
                                                                                       {"front_thickness", 14, 40}, {"gap", .5, 20}, {"opening_angle", 0, 170}, {"handle_spacing", 0, 1000}})
        c[key] = finite(c.at(key), low, high);
    for (auto& [key, low, high] : std::vector<std::tuple<std::string, double, double>>{{"leg_height", 0, 500}, {"leg_diameter", 15, 100}, {"leg_setback", 10, 200}, {"leg_price", 0, 100000}})
        c[key] = finite(c.at(key), low, high);
    if (c.at("leg_count") != 4 && c.at("leg_count") != 6) throw ValueError("Wybierz 4 albo 6 nóg narożnika.");
    for (auto& [key, low, high] : std::vector<std::tuple<std::string, double, double>>{{"shelves", 0, 8}, {"segments", 4, 24}, {"hinge_count", 2, 8}}) {
        double value = finite(c.at(key), low, high);
        if (value != std::trunc(value)) throw ValueError("Liczba " + key + " musi być całkowita.");
        c[key] = (long long)value;
    }
    if (c["shape"] != "L" && c["shape"] != "chamfer" && c["shape"] != "rounded") throw ValueError("Narożnik: L, ścięty albo zaokrąglony.");
    if (c["side"] != "left" && c["side"] != "right") throw ValueError("Wybierz lewy albo prawy narożnik.");
    if (c["top_mode"] != "panel" && c["top_mode"] != "rails") throw ValueError("Góra narożnika: pełna płyta albo listwy.");
    if (c["furniture_type"] != "cabinet" && c["furniture_type"] != "wardrobe") throw ValueError("Typ narożnika: szafka albo szafa.");
    if (c["fronts"] != "open" && c["fronts"] != "orthogonal" && c["fronts"] != "diagonal") throw ValueError("Fronty: otwarte, dwa niezależne albo skośny.");
    if (c["fronts"] == "orthogonal" && c["shape"] != "L") throw ValueError("Dwa niezależne fronty wymagają naroża L. Dla ścięcia wybierz front skośny, dla zaokrąglenia otwarte półki.");
    if (c["fronts"] == "diagonal" && c["shape"] != "chamfer") throw ValueError("Front skośny wymaga naroża ściętego.");
    if (c["handle_mode"] != "handle" && c["handle_mode"] != "none") throw ValueError("Uchwyt narożnika: standardowy albo brak.");
    try { fittings::recipe(c["hinge_pattern"]); }
    catch (const py::StopIteration&) { throw ValueError("Wybierz wzorzec zawiasu narożnika."); }
    if (c["hinge_pattern"] != "hinge45" && c["hinge_pattern"] != "hinge52" && c["hinge_pattern"] != "hinge_sensys52") throw ValueError("Generator narożnika obsługuje standardowy wzór zawiasu nakładanego.");
    if (!c["origin"].is_array() || c["origin"].size() != 3) throw ValueError("Pozycja narożnika wymaga X, Y i Z.");
    c["origin"] = json::array({finite(c["origin"][0]), finite(c["origin"][1]), finite(c["origin"][2])});
    double t = num(c["thickness"]);
    if (num(c["arm_depth"]) >= num(c["depth"]) - 2 * t - 10 || num(c["arm_depth_y"]) >= num(c["width"]) - 2 * t - 10) throw ValueError("Ramiona muszą być płytsze od gabarytów narożnika, z miejscem na boki.");
    if (std::min(num(c["arm_depth"]), num(c["arm_depth_y"])) <= 2 * t + 20 || num(c["height"]) - num(c["leg_height"]) <= 2 * t + 30) throw ValueError("Narożnik jest za mały dla grubości płyt i nóg.");
    double rise = c["shape"] == "chamfer" ? num(c["radius"]) * std::tan(radians(num(c["diagonal_angle"]))) : num(c["radius"]);
    if (c["shape"] != "L" && (num(c["radius"]) > num(c["width"]) - num(c["arm_depth_y"]) - 2 || rise > num(c["depth"]) - num(c["arm_depth"]) - 2))
        throw ValueError("Promień / ścięcie nie mieści się pomiędzy końcami ramion.");
    if (c["top_mode"] == "rails" && num(c["rail_width"]) >= std::min(num(c["arm_depth"]), num(c["arm_depth_y"])) - t)
        throw ValueError("Listwa jest zbyt szeroka dla ramienia narożnika.");
    if (project) {
        std::set<std::string> ids; for (auto& m : project->materials) ids.insert(m.id);
        for (std::string key : {"material", "front_material"}) if (!contains_string(ids, c[key])) throw ValueError("Wybierz materiał narożnika i frontu.");
    }
    c["front_style"] = configurator_accessories::normalize_front_style(c["front_style"], c)["style"];
    return c;
}

std::vector<Point> corner_outline(double width, double depth, double arm_depth, const std::string& shape, double radius, const std::string& side, int segments, std::optional<double> arm_depth_y, double diagonal_angle) {
    double w = width, d = depth, a = arm_depth; double b = arm_depth_y ? *arm_depth_y : arm_depth; std::vector<Point> points{{0, 0}, {w, 0}, {w, a}};
    if (shape == "L" || !radius) points.push_back({b, a});
    else if (shape == "chamfer") { points.push_back({b + radius, a}); points.push_back({b, a + radius * std::tan(radians(diagonal_angle))}); }
    else if (shape == "rounded") {
        double cx = b + radius, cy = a + radius;
        for (int i = 0; i <= segments; ++i)
            points.push_back({cx + radius * std::cos(-std::numbers::pi / 2 - i * std::numbers::pi / (2 * segments)), cy + radius * std::sin(-std::numbers::pi / 2 - i * std::numbers::pi / (2 * segments))});
    } else throw ValueError("Nieznany obrys narożnika.");
    points.push_back({b, d}); points.push_back({0, d});
    if (side == "right") { std::reverse(points.begin(), points.end()); for (auto& p : points) p[0] = w - p[0]; }
    std::vector<std::vector<double>> rounded;
    for (auto& [x, y] : points) rounded.push_back({py::round_to(x, 7), py::round_to(y, 7)});
    return validate_polygon(rounded);
}

json read_corner_config(const Project& project, const std::optional<std::string>& group) {
    for (auto& p : project.parts)
        if (group && p.group == *group && py::truthy(py::get(py::get(p.assembly, "corner_source", json::object()), "config")))
            return p.assembly["corner_source"]["config"];
    return nullptr;
}

std::optional<std::pair<Point3, Point3>> corner_bounds(const Project& project, const std::string& group) {
    json config = read_corner_config(project, group);
    if (config.is_null()) return std::nullopt;
    Point3 lo{num(config["origin"][0]), num(config["origin"][1]), num(config["origin"][2])};
    return std::make_pair(lo, Point3{lo[0] + num(config["width"]), lo[1] + num(config["depth"]), lo[2] + num(config["height"])});
}

json corner_status(const Project& project, const std::string& group) {
    std::vector<const Part*> parts; for (auto& p : project.parts) if (p.group == group) parts.push_back(&p);
    const Part* anchor = nullptr;
    for (const Part* p : parts) if (py::truthy(py::get(py::get(p->assembly, "corner_source", json::object()), "manifest"))) { anchor = p; break; }
    if (!anchor) return {{"configured", false}, {"editable", false}, {"reason", "Ten mebel nie pochodzi z kreatora narożników."}};
    std::map<std::string, const Part*> by_id; for (const Part* p : parts) by_id[p->id] = p;
    for (auto& [_, entry] : anchor->assembly["corner_source"]["manifest"].items()) {
        auto found = entry.at("id").is_string() ? by_id.find(entry["id"].get<std::string>()) : by_id.end();
        if (found == by_id.end() || _geometry(*found->second) != entry.at("signature"))
            return {{"configured", true}, {"editable", false}, {"reason", "Geometrię narożnika zmieniono ręcznie. Automatyczna przebudowa jej nie nadpisze."}};
    }
    json config = read_corner_config(project, group);
    std::string reason = py::get(config, "fronts") == "orthogonal" ? "Dwa niezależne fronty z wzorcami zawiasów." : py::get(config, "fronts") == "diagonal" ? "Front skośny bez automatycznego okucia; zawias dobierz osobno." : "Otwarty korpus narożny.";
    return {{"configured", true}, {"editable", true}, {"reason", reason}};
}

assembly::Frame corner_front_frame(const Project& project, const Part& part) {
    // The front's U/depth/Z frame, with explicit construction-space basis.
    json bay = py::get(py::get(part.assembly, "config", json::object()), "corner_bay");
    if (!py::truthy(bay)) throw ValueError("Front skośny nie ma automatycznej komory ani wzorca zawiasu.");
    std::map<std::string, const Part*> members; for (auto& p : project.parts) members[p.id] = &p;
    auto lookup = [&](const json& id) -> const Part* { auto f = id.is_string() ? members.find(id.get<std::string>()) : members.end(); return f == members.end() ? nullptr : f->second; };
    const Part* support = lookup(bay.at("support_id")); const Part* opposite = lookup(bay.at("opposite_id"));
    if (support == nullptr || opposite == nullptr) throw ValueError("Usunięto bok lub słupek narożnika. Przebuduj korpus.");
    double gap = num(py::get(part.assembly["config"], "gap", 2)); Point3 origin = local_world(part, 0, 0, 0);
    std::array<Point3, 3> basis;
    Point3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int j = 0; j < 3; ++j) { Point3 p = local_world(part, axes[j][0], axes[j][1], axes[j][2]); basis[j] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]}; }
    assembly::Frame f;
    f.lower = {0, 0, part.z - gap}; f.upper = {part.u + 2 * gap, support->u, part.z + part.v + gap}; f.left = support->id; f.right = opposite->id;
    f.inner_left = support->t; f.inner_right = part.u + 2 * gap - opposite->t; f.inner_bottom = part.z; f.inner_top = part.z + part.v;
    f.parts = {support->id, opposite->id}; f.coordinate_system = "front_local"; f.basis = basis; f.origin = origin;
    return f;
}

void _corner_hinges(Project& project, Part& front, const json& pattern, const json& count_, const json& price_, bool manual) {
    double count_value = finite(count_, 2, 8);
    if (count_value != std::trunc(count_value)) throw ValueError("Liczba zawiasów musi być całkowita.");
    long long count = (long long)count_value; double price = finite(price_, 0, 1000000); json spec = fittings::recipe(pattern); json bay = py::get(front.assembly.at("config"), "corner_bay");  // front.assembly['config'] — KeyError
    if (!py::truthy(bay)) throw ValueError("Dla frontu skośnego dobierz rzeczywisty zawias osobno. Program nie tworzy jego automatycznych wierceń.");
    if (pattern != "hinge45" && pattern != "hinge52" && pattern != "hinge_sensys52") throw ValueError("Wybierz wzorzec standardowego zawiasu nakładanego.");
    double min_thickness = spec.contains("min_thickness") ? num(spec["min_thickness"]) : num(spec.at("depth")) + 1;
    if (!(min_thickness <= front.t && front.t <= num(py::get(spec, "max_thickness", 10000)))) throw ValueError("Grubość frontu nie mieści się w zakresie wybranego zawiasu.");
    Part* support = bay.at("support_id").is_string() ? find_part(project, bay["support_id"].get<std::string>()) : nullptr;
    if (support == nullptr) throw ValueError("Usunięto bok zawiasu narożnika.");
    Point3 current = local_world(front, 0, 0, bay.at("cup_face") == "top" ? front.t : 0);
    if (dist({current[0], current[1]}, {num(bay.at("hinge_point")[0]), num(bay["hinge_point"][1])}) > .1) throw ValueError("Przesunięto krawędź zawiasową poza bok narożnika. Dopasuj front albo edytuj wiercenia ręcznie.");
    for (auto& p : project.parts) {
        json ops = json::array(); for (auto& op : p.operations) if (py::get(op, "corner_hinge_owner") != front.id) ops.push_back(op);
        p.operations = ops;
    }
    json hardware = json::array();
    for (auto& h : project.hardware) if (!(py::get(h, "owner_id") == front.id && py::get(h, "kind") == "hinge")) hardware.push_back(h);
    project.hardware = hardware;
    double end = std::min(100.0, front.v / 4); json levels = json::array();
    for (long long i = 0; i < count; ++i) levels.push_back(end + (front.v - 2 * end) * i / (count - 1));
    double cup = 5 + num(spec.at("diameter")) / 2; json positions = json::array();
    auto drill = [&](Part& part, double u, double v, double diameter, double depth, const std::string& face, const std::string& purpose) {
        json op = features::hole_array(part, u, v, diameter, depth, 1, 1, 32, 32, face)[0];
        json extra = {{"owner_id", front.id}, {"corner_hinge_owner", front.id}, {"purpose", purpose}, {"recipe_id", pattern}, {"source", spec.at("source")},
                      {"requires_review", true}, {"config_auto", !manual}, {"corner_auto", !manual}};
        for (auto& [k, value] : extra.items()) op[k] = value;
        part.operations.push_back(op);
    };
    std::string cup_face = bay["cup_face"];
    for (auto& level_value : levels) {
        double level = num(level_value);
        drill(front, cup, level, num(spec["diameter"]), num(spec.at("depth")), cup_face, "puszka zawiasu");
        for (double delta : {-num(spec.at("pitch")) / 2, num(spec["pitch"]) / 2}) drill(front, cup - num(spec.at("offset")), level + delta, num(spec.at("pilot")), num(spec.at("pilot_depth")), cup_face, "mocowanie puszki zawiasu");
        double z = front.z + level;
        for (double delta : {-16.0, 16.0}) drill(*support, num(bay.at("support_u")), z + delta - support->z, 3, std::min(10.0, support->t - 2), bay.at("support_face"), "prowadnik zawiasu — wzorzec roboczy");
        Point3 point = local_world(front, cup, level, cup_face == "top" ? front.t : 0); positions.push_back({{"x", point[0]}, {"y", point[1]}, {"z", point[2]}});
    }
    json& config = front.assembly["config"];
    config["hinge_side"] = "left"; config["hinge_pattern"] = pattern; config["hinge_count"] = count; config["cup_edge"] = 5; config["cup_diameter"] = spec["diameter"]; config["cup_depth"] = spec["depth"];
    project.hardware.push_back({{"kind", "hinge"}, {"owner_id", front.id}, {"anchors", json::array({support->id})}, {"name", spec.at("name").get<std::string>() + " + prowadnik roboczy"}, {"qty", count}, {"price", price}, {"source", spec["source"]}, {"recipe_id", pattern},
                                {"levels", levels}, {"positions", positions}, {"mount_face", cup_face}, {"color", "#aab0b6"}, {"requires_review", true}, {"config_auto", !manual}, {"corner_auto", !manual}});
    front.validate(); support->validate();
}

ProjectPtr regenerate_corner_hinges(const Project& project, const std::string& part_id, const json& pattern, const json& count, const json& price, bool manual, bool validate) {
    auto result = std::make_shared<Project>(project); Part* front = find_part(*result, part_id);
    if (front == nullptr) throw py::StopIteration();
    _corner_hinges(*result, *front, pattern, count, price, manual);
    if (validate) result->validate();
    return result;
}

void _front_finish(Project& project, Part& front, const json& config) {
    json cup_face_value = py::get(py::get(py::get(front.assembly, "config", json::object()), "corner_bay", json::object()), "cup_face", "top");
    std::string outside = cup_face_value == "top" ? "bottom" : "top";
    if (config.at("handle_mode") == "handle" && config.at("front_style") != "finger") {
        double spacing = num(config.at("handle_spacing")); double u = front.u - 35; double v = front.v / 2;
        if (spacing > front.v - 20) throw ValueError("Rozstaw uchwytu jest za duży dla wysokości frontu.");
        for (double level : !spacing ? std::vector<double>{v} : std::vector<double>{v - spacing / 2, v + spacing / 2}) {
            json op = features::hole_array(front, u, level, 4.5, front.t, 1, 1, 32, 32, outside)[0];
            op["recipe_id"] = "handle"; op["purpose"] = "Przelot M4 uchwytu"; op["config_auto"] = true; op["corner_auto"] = true; front.operations.push_back(op);
        }
        project.hardware.push_back({{"kind", "handle"}, {"owner_id", front.id}, {"name", !spacing ? std::string("Gałka") : std::format("Uchwyt {:g} mm", spacing)}, {"qty", 1}, {"price", 0}, {"u", u}, {"v", v}, {"spacing", config["handle_spacing"]}, {"vertical", true}, {"color", "#242628"}, {"projection", 28}, {"mount_face", outside}, {"config_auto", true}, {"corner_auto", true}});
    }
    // The style helper's reference surface is bottom. Mirror its surface names
    // temporarily so its opposed-drill depth guard also covers this orientation.
    auto mirror_faces = [&] {
        for (auto& op : front.operations) if (op.at("face") == "top" || op["face"] == "bottom") op["face"] = op["face"] == "top" ? "bottom" : "top";
    };
    if (outside == "top") mirror_faces();
    configurator_accessories::apply_front_to_part(project, front, config.at("front_style"), config);
    if (outside == "top") {
        mirror_faces();
        for (auto& cut : front.machining) if (py::truthy(py::get(cut, "config_auto")) && py::get(cut, "face") == "bottom") cut["face"] = "top";
    }
    for (auto& item : project.hardware) if (py::get(item, "owner_id") == front.id && py::truthy(py::get(item, "config_auto"))) item["corner_auto"] = true;
}

ProjectPtr fit_corner_part(const Project& project, const std::string& part_id) {
    // Explicitly restore this managed component to its corner parameters.
    auto result = std::make_shared<Project>(project); Part* part = find_part(*result, part_id);
    if (part == nullptr) throw py::StopIteration();
    json config = read_corner_config(*result, part->group);
    if (config.is_null()) throw ValueError("Wybierz część narożnika.");
    Part* anchor = nullptr;
    for (auto& p : result->parts) if (p.group == part->group && py::truthy(py::get(py::get(p.assembly, "corner_source", json::object()), "manifest"))) { anchor = &p; break; }
    if (anchor == nullptr) throw py::StopIteration();
    json key = py::get(py::get(part->assembly, "corner_source", json::object()), "key");
    anchor->assembly["corner_source"]["manifest"].at(key.get<std::string>())["signature"] = _geometry(*part);
    std::string group = part->group;
    return create_corner(*result, config, group).first;
}

std::pair<ProjectPtr, std::string> create_corner(const Project& project, const json& config, const std::optional<std::string>& group_) {
    json c = normalize_corner_config(config, &project); auto result = std::make_shared<Project>(project);
    json old_config = group_ && !group_->empty() ? read_corner_config(project, group_) : json(nullptr);
    std::vector<const Part*> existing;
    if (py::truthy(old_config)) for (auto& p : project.parts) if (p.group == *group_) existing.push_back(&p);
    std::map<std::string, const Part*> old; json layout = json::object(); std::string group;
    if (!existing.empty()) {
        json status = corner_status(project, *group_);
        if (!py::truthy(status["editable"])) throw ValueError(status["reason"].get<std::string>());
        for (const Part* p : existing) if (py::truthy(py::get(p->assembly, "corner_source"))) old[p->assembly["corner_source"].at("key").get<std::string>()] = p;
        layout = group_layout(project, *group_);
        for (const Part* p : existing) if (!py::truthy(py::get(p->assembly, "corner_source")))
            throw ValueError("Do narożnika dodano własne elementy. Zmień je osobno albo utwórz nowy narożnik, aby ich nie nadpisać.");
        if (c["name"] != *group_ && std::any_of(project.parts.begin(), project.parts.end(), [&](const Part& p) { return c["name"] == p.group; })) throw ValueError("Mebel o tej nazwie już istnieje.");
        std::erase_if(result->parts, [&](const Part& p) { return p.group == *group_; }); group = c["name"];
    } else {
        std::string base = group_ && !group_->empty() ? *group_ : c["name"].get<std::string>(); group = base; int i = 2;
        std::set<std::string> occupied; for (auto& p : result->parts) occupied.insert(p.group);
        while (occupied.contains(group)) { group = std::format("{} ({})", base, i); ++i; }
    }
    std::set<std::string> old_ids; for (const Part* p : existing) old_ids.insert(p->id);
    json retained_attachments = json::array();
    for (auto& item : result->hardware) if (contains_string(old_ids, py::get(item, "owner_id")) && py::truthy(py::get(item, "attachment_kind"))) retained_attachments.push_back(item);
    { json kept = json::array(); for (auto& item : result->hardware) if (!(contains_string(old_ids, py::get(item, "owner_id")) && py::truthy(py::get(item, "corner_auto")))) kept.push_back(item); result->hardware = kept; }
    for (auto& p : result->parts) {
        json ops = json::array(); for (auto& op : p.operations) if (!(contains_string(old_ids, py::get(op, "corner_hinge_owner")) && py::truthy(py::get(op, "config_auto")))) ops.push_back(op);
        p.operations = ops;
    }
    c["name"] = group; double w = num(c["width"]), d = num(c["depth"]), h = num(c["height"]) - num(c["leg_height"]), t = num(c["thickness"]), a = num(c["arm_depth"]), b = num(c["arm_depth_y"]);
    double ox = num(c["origin"][0]), oy = num(c["origin"][1]), floor = num(c["origin"][2]); double oz = floor + num(c["leg_height"]);
    std::string shape = c["shape"], side = c["side"];
    auto outline = corner_outline(w, d, a, shape, num(c["radius"]), side, c["segments"].get<int>(), b, num(c["diagonal_angle"])); std::vector<Part*> created;
    // W Pythonie części są referencjami; rezerwacja utrzymuje wskaźniki (bottom, end_x...) ważne.
    result->parts.reserve(result->parts.size() + 32);
    auto add = [&](const std::string& key, const std::string& name, double u, double v, const std::string& plane, double x, double y, double z, const std::string& role = "partition",
                   const std::optional<std::vector<Point>>& contour = std::nullopt, std::optional<double> thick = std::nullopt, const std::string& material = "", bool mirror = true) {
        Part p{.name = name, .group = group, .u = u, .v = v, .t = thick ? *thick : t, .plane = plane, .x = ox + x, .y = oy + y, .z = oz + z,
               .material = !material.empty() ? material : c["material"].get<std::string>(), .role = role, .layout = layout};
        if (contour && !contour->empty()) { p.contour = rows(*contour); p.contour_edge = true; }
        if (side == "right" && plane != "XY" && mirror) {
            // Mirror rectangular vertical members without changing their local
            // rotations; ordinary manufacturing U/V remain positive.
            double span = plane == "YZ" ? t : u; p.x = ox + w - x - span;
        }
        auto found = old.find(key);
        if (found != old.end()) {
            const Part& previous = *found->second;
            p.id = previous.id; p.name = previous.name; p.grain = previous.grain; p.visible = previous.visible; p.edges = previous.edges; p.contour_edge = previous.contour_edge;
            std::string material_key = role == "door" ? "front_material" : "material";
            if (py::get(old_config, material_key, old_config.at("material")) == c[material_key]) p.material = previous.material;
            p.holes = previous.holes; p.fillets = previous.fillets; p.chamfers = previous.chamfers;
            p.operations = json::array(); for (auto& op : previous.operations) if (!py::truthy(py::get(op, "config_auto"))) p.operations.push_back(op);
            p.machining = json::array(); for (auto& op : previous.machining) if (!py::truthy(py::get(op, "config_auto"))) p.machining.push_back(op);
        }
        p.assembly["corner_source"] = {{"schema", 1}, {"key", key}}; p.validate(); result->parts.push_back(p); created.push_back(&result->parts.back()); return &result->parts.back();
    };
    Part* bottom = add("bottom", "Dno narożne", w, d, "XY", 0, 0, 0, "bottom", outline);
    add("back_x", "Bok tylny X", w - t, h - 2 * t, "XZ", t, 0, t, "partition");
    add("back_y", "Bok tylny Y", d, h - 2 * t, "YZ", 0, 0, t, "left");
    Part* end_x = add("end_x", "Bok końcowy X", a - t, h - 2 * t, "YZ", w - t, t, t, "right");
    Part* end_y = add("end_y", "Bok końcowy Y", b - t, h - 2 * t, "XZ", t, d - t, t, "partition");
    if (c["top_mode"] == "panel") add("top", "Góra narożna", w, d, "XY", 0, 0, h - t, "top", outline);
    else {
        double r = num(c["rail_width"]); double x = side == "left" ? t : 0;
        add("rail_x", "Listwa narożna X", w - t, r, "XY", x, 0, h - t, "top");
        x = side == "left" ? 0 : w - r;
        add("rail_y", "Listwa narożna Y", r, d - r, "XY", x, r, h - t, "top");
    }
    double inset = t + 2;
    auto shelf_outline = corner_outline(w - 2 * inset, d - 2 * inset, a - 2 * inset, shape, num(c["radius"]), side, c["segments"].get<int>(), b - 2 * inset, num(c["diagonal_angle"]));
    long long shelves = c["shelves"];
    for (long long index = 0; index < shelves; ++index) {
        double level = t + (h - 3 * t) * (index + 1) / (shelves + 1);
        add(std::format("shelf.{}", index), "Półka narożna " + std::to_string(index + 1), w - 2 * inset, d - 2 * inset, "XY", inset, inset, level, "shelf", shelf_outline);
    }
    std::string front_material = c["front_material"];
    if (c["fronts"] == "orthogonal") {
        double gap = num(c["gap"]), ft = num(c["front_thickness"]); double span_x = w - b - ft - 3 * gap; double span_y = d - a - 2 * gap;
        if (std::min(span_x, span_y) < 80) throw ValueError("Ramiona są za krótkie dla dwóch niezależnych frontów. Powiększ gabaryty lub wybierz otwarty narożnik.");
        Part* post_x = add("post_x", "Słupek narożny X", 70, h - 2 * t, "YZ", b - t, a - 70, t);
        Part* post_y = add("post_y", "Słupek narożny Y", 70, h - 2 * t, "XZ", b, a - t, t);
        bool right = side == "right";
        Part* front_x = add("front.x", "Front ramienia X", span_x, h - 2 * gap, "XZ", right ? gap : w - gap, right ? a + gap : a + gap + ft, gap, "door", std::nullopt, ft, front_material, false);
        front_x->rz = right ? 0 : 180;
        Part* front_y = add("front.y", "Front ramienia Y", span_y, h - 2 * gap, right ? "XZ" : "YZ", right ? w - b - gap - ft : b + gap + ft, d - gap, gap, "door", std::nullopt, ft, front_material, false);
        front_y->rz = right ? -90 : 180;
        double support_x = end_y->x - ox;
        struct Bay { Part* front; Part* support; Part* opposite; double support_u; std::string support_face, cup_face; int sign; };
        for (auto& [front, support, opposite, support_u, support_face, cup_face, sign] : std::vector<Bay>{
                 {front_x, end_x, post_x, a - 37 - t, right ? "top" : "bottom", right ? "bottom" : "top", right ? -1 : 1},
                 {front_y, end_y, post_y, (right ? w - b + 37 : b - 37) - support_x, "bottom", "top", 1}}) {
            Point3 edge = local_world(*front, 0, 0, cup_face == "top" ? front->t : 0);
            json bay = {{"support_id", support->id}, {"opposite_id", opposite->id}, {"support_face", support_face}, {"support_u", support_u}, {"cup_face", cup_face},
                        {"hinge_point", json::array({edge[0], edge[1]})}, {"opening_sign", sign}};
            front->assembly["group"] = group; front->assembly["role"] = "door";
            front->assembly["config"] = {{"corner_bay", bay}, {"gap", gap}, {"hinge_side", "left"}, {"hinge_count", c["hinge_count"]}, {"hinge_pattern", c["hinge_pattern"]}};
            front->motion = {{"mode", "hinge_left"}, {"angle", c["opening_angle"]}, {"travel", 0}};
            _corner_hinges(*result, *front, c["hinge_pattern"], c["hinge_count"], 0);
            _front_finish(*result, *front, c);
        }
    } else if (c["fronts"] == "diagonal") {
        double gap = num(c["gap"]), ft = num(c["front_thickness"]); double rise = num(c["radius"]) * std::tan(radians(num(c["diagonal_angle"])));
        Point p{b + num(c["radius"]), a}, q{b, a + rise};
        if (side == "right") { Point np{w - q[0], q[1]}, nq{w - p[0], p[1]}; p = np; q = nq; }
        double length = dist(p, q);
        if (length - 2 * gap < 40) throw ValueError("Ścięcie jest za krótkie dla frontu skośnego.");
        double ux = (q[0] - p[0]) / length, uy = (q[1] - p[1]) / length;
        Part* front = add("front.diagonal", "Front skośny — okucie dobierz osobno", length - 2 * gap, h - 2 * gap, "XZ", p[0] + uy * (gap + ft) + ux * gap, p[1] - ux * (gap + ft) + uy * gap, gap, "door", std::nullopt, ft, front_material, false);
        front->rz = std::atan2(uy, ux) * (180 / std::numbers::pi); front->motion = {{"mode", "none"}};
        front->assembly["group"] = group; front->assembly["role"] = "door"; front->assembly["config"] = {{"gap", gap}, {"corner_diagonal", true}};
        _front_finish(*result, *front, c);
    }
    if (py::truthy(c["leg_height"])) {
        double s = std::min({num(c["leg_setback"]), a / 3, b / 3}); std::vector<Point> positions{{s, s}, {w - s, a / 2}, {b / 2, d - s}, {b / 2, a / 2}};
        if (c["leg_count"] == 6) { positions.push_back({w - s, s}); positions.push_back({s, d - s}); }
        if (side == "right") for (auto& p : positions) p[0] = w - p[0];
        double diameter = num(c["leg_diameter"]);
        for (size_t i = 0; i < positions.size(); ++i)
            for (size_t j = i + 1; j < positions.size(); ++j)
                if (dist(positions[i], positions[j]) < diameter - 1e-6) throw ValueError("Nogi narożnika nakładają się. Zmniejsz ich średnicę albo wybierz 4 nogi.");
        for (auto& [x, y] : positions) {
            for (int i = 0; i < 32; ++i)
                if (!core::inside({x + diameter / 2 * std::cos(i * TAU / 32), y + diameter / 2 * std::sin(i * TAU / 32)}, outline))
                    throw ValueError("Noga narożnika nie mieści się pod obrysem. Zmień średnicę lub odsunięcie.");
        }
        json leg_positions = json::array(); for (auto& [x, y] : positions) leg_positions.push_back({{"x", ox + x}, {"y", oy + y}, {"z", floor}});
        result->hardware.push_back({{"kind", "leg"}, {"owner_id", bottom->id}, {"name", "Noga narożnika — własny model"}, {"qty", c["leg_count"]}, {"price", c["leg_price"]}, {"height", c["leg_height"]}, {"diameter", c["leg_diameter"]}, {"color", c["leg_color"]}, {"positions", leg_positions}, {"config_auto", true}, {"corner_auto", true}});
    }
    std::set<std::string> new_keys; for (Part* p : created) new_keys.insert(p->assembly["corner_source"]["key"].get<std::string>());
    for (auto& [key, p] : old) {
        bool manual_work = !p->holes.empty() || py::truthy(p->fillets) || py::truthy(p->chamfers);
        for (auto* list : {&p->operations, &p->machining}) for (auto& op : *list) if (!py::truthy(py::get(op, "config_auto"))) manual_work = true;
        for (auto& item : result->hardware) if (py::get(item, "owner_id") == p->id) manual_work = true;
        if (!new_keys.contains(key) && manual_work) throw ValueError("Zmiana usuwa część narożnika z ręczną obróbką lub okuciem.");
    }
    for (auto& item : result->hardware) {
        if (contains_string(old_ids, py::get(item, "owner_id")) && py::truthy(py::get(item, "group"))) item["group"] = group;
        if (py::truthy(py::get(item, "corner_auto"))) {
            const json* previous = nullptr;
            for (auto& h : project.hardware) if (py::get(h, "owner_id") == py::get(item, "owner_id") && py::get(h, "kind") == py::get(item, "kind") && py::truthy(py::get(h, "corner_auto"))) { previous = &h; break; }
            if (previous) {
                for (std::string key : {"price", "supplier", "note"}) {
                    if (previous->contains(key) && !(key == "price" && item.at("kind") == "leg" && py::truthy(old_config) && c["leg_price"] != py::get(old_config, "leg_price", 0))) item[key] = (*previous)[key];
                }
            }
        }
    }
    json manifest = json::object();
    for (Part* p : created) manifest[p->assembly["corner_source"]["key"].get<std::string>()] = {{"id", p->id}, {"signature", _geometry(*p)}};
    bottom->assembly["corner_source"]["config"] = c; bottom->assembly["corner_source"]["manifest"] = manifest;
    // Rebuilding standard hinge recipes removes old hinge rows. Restore the
    // manual attachment descriptors before their final authoritative refresh.
    for (auto& item : retained_attachments) {
        if (std::none_of(result->hardware.begin(), result->hardware.end(), [&](const json& h) { return py::get(h, "attachment_id") == py::get(item, "attachment_id"); })) result->hardware.push_back(item);
    }
    result = workspace_worktops::refresh_shared_worktops(result, false);
    drag_hardware::refresh_manual_attachments(*result); result->validate(); return {result, group};
}

ProjectPtr resize_corner_furniture(const Project& project, const std::string& group, const json& width, const json& height, const json& depth, const json& origin) {
    json config = read_corner_config(project, group);
    if (config.is_null()) return nullptr;
    config["width"] = width; config["height"] = num(height) + num(py::get(config, "leg_height", 0)); config["depth"] = depth;
    if (!origin.is_null()) config["origin"] = origin;
    return create_corner(project, config, group).first;
}

std::pair<ProjectPtr, std::string> convert_to_corner(const Project& project, const std::string& group, const std::string& shape, const std::string& side,
                                                    const json& arm_depth, const json& width, const json& depth, const std::string& fronts, const json& changes) {
    // Convert a recognised, unmodified cabinet in one atomic transaction.
    //
    // IDs of matching structural members/fronts survive. Manual geometry,
    // machining, additional panels and fittings require a separate new corner.
    // Existing corner groups are rebuilt using their own persistent recipe.
    json current = read_corner_config(project, group);
    if (py::truthy(current)) {
        current["shape"] = shape; current["side"] = side; current["fronts"] = fronts; for (auto& [k, v] : changes.items()) current[k] = v;
        for (auto& [key, value] : std::vector<std::pair<std::string, json>>{{"arm_depth", arm_depth}, {"width", width}, {"depth", depth}})
            if (!value.is_null()) current[key] = value;
        return create_corner(project, current, group);
    }
    std::vector<const Part*> members; for (auto& p : project.parts) if (p.group == group) members.push_back(&p);
    if (members.empty()) throw ValueError("Wybierz szafkę albo szafę do konwersji.");
    json oldcfg = furniture_configurator::read_config(project, group); bool legacy = oldcfg.is_null();
    if (legacy) oldcfg = furniture_configurator::infer_config(project, group);
    else {
        json status = furniture_configurator::configuration_status(project, group);
        if (!py::truthy(status["editable"])) throw ValueError(status["reason"].get<std::string>());
        for (const Part* p : members) if (!py::truthy(py::get(p->configurator_source, "key"))) throw ValueError("Mebel zawiera własne elementy. Utwórz osobny narożnik, aby ich nie nadpisać.");
    }
    for (const Part* p : members) if (assembly::inferred_role(*p) == "drawer") throw ValueError("Konwersja narożna obsługuje szafki i szafy z półkami lub drzwiami. Zachowaj szuflady w oddzielnym module.");
    std::set<std::string> ids; for (const Part* p : members) ids.insert(p->id);
    for (const Part* p : members) {
        bool manual = !p->holes.empty() || py::truthy(p->fillets) || py::truthy(p->chamfers);
        if (!legacy) for (auto* list : {&p->operations, &p->machining}) for (auto& op : *list) if (!py::truthy(py::get(op, "config_auto"))) manual = true;
        if (manual) throw ValueError("Mebel zawiera ręczną obróbkę. Utwórz nowy narożnik, aby jej nie nadpisać.");
    }
    if (!legacy) for (auto& h : project.hardware) if (contains_string(ids, py::get(h, "owner_id")) && !py::truthy(py::get(h, "config_auto"))) throw ValueError("Mebel zawiera ręczne okucia. Utwórz osobny narożnik, aby je zachować.");
    json c = default_corner_config(&project, num(oldcfg.at("height")) >= 1500 ? "wardrobe" : "cabinet");
    for (std::string key : {"thickness", "front_thickness", "gap", "material", "front_material", "hinge_pattern", "hinge_count", "opening_angle", "leg_height", "leg_count", "leg_diameter", "leg_setback", "leg_color", "leg_price", "handle_spacing", "front_style", "rail_width", "top_mode"})
        if (oldcfg.contains(key)) c[key] = oldcfg[key];
    c["name"] = group; c["origin"] = oldcfg.at("origin"); c["height"] = num(oldcfg["height"]) - (py::truthy(py::get(oldcfg, "worktop")) ? num(oldcfg.at("worktop_thickness")) : 0);
    c["shape"] = shape; c["side"] = side; c["fronts"] = fronts;
    c["handle_mode"] = py::get(oldcfg, "handle_mode") == "none" ? "none" : "handle";
    json shelves = 0; bool any_shelves = false;
    for (auto& s : oldcfg.at("sections")) if (py::truthy(s.at("enabled"))) { if (!any_shelves || s.at("shelves") > shelves) shelves = s["shelves"]; any_shelves = true; }
    c["shelves"] = shelves;
    json arm = arm_depth.is_null() ? oldcfg.at("depth") : arm_depth;
    c["arm_depth"] = arm; c["arm_depth_y"] = arm; c["width"] = width.is_null() ? json(std::max(num(oldcfg.at("width")), num(arm) + 340)) : width; c["depth"] = depth.is_null() ? json(num(arm) + 340) : depth;
    for (auto& [k, v] : changes.items()) c[k] = v;
    c = normalize_corner_config(c, &project);
    json layout = group_layout(project, group);
    Project baseline = project; std::erase_if(baseline.parts, [&](const Part& p) { return ids.contains(p.id); });
    { json kept = json::array(); for (auto& h : baseline.hardware) if (!contains_string(ids, py::get(h, "owner_id"))) kept.push_back(h); baseline.hardware = kept; }
    for (auto& p : baseline.parts) { json ops = json::array(); for (auto& op : p.operations) if (!contains_string(ids, py::get(op, "owner_id"))) ops.push_back(op); p.operations = ops; }
    auto [result, newgroup] = create_corner(baseline, c, group);
    std::map<std::string, const Part*> old_by_key, old_by_name;
    for (const Part* p : members) if (py::truthy(py::get(p->configurator_source, "key"))) old_by_key[p->configurator_source["key"].get<std::string>()] = p;
    for (const Part* p : members) old_by_name[p->name] = p;
    std::map<std::string, std::string> names{{"carcass.left", "Bok lewy"}, {"carcass.right", "Bok prawy"}, {"carcass.bottom", "Dno"}, {"carcass.top", "Wieniec"}, {"carcass.back", "Plecy nakładane HDF"}, {"main.bay.0.door.0", "Front 1"}, {"main.bay.0.door.1", "Front 2"}};
    std::map<std::string, std::string> matches{{"back_y", "carcass.left"}, {"end_x", "carcass.right"}, {"bottom", "carcass.bottom"}, {"top", "carcass.top"}, {"rail_x", "carcass.top"}, {"rail_y", "carcass.rail_back"}, {"back_x", "carcass.back"}, {"front.x", "main.bay.0.door.0"}, {"front.y", "main.bay.0.door.1"}};
    std::map<std::string, std::string> changed_ids; std::vector<Part*> newparts; for (auto& p : result->parts) if (p.group == newgroup) newparts.push_back(&p);
    auto lookup = [](const std::map<std::string, const Part*>& map, const std::optional<std::string>& key) -> const Part* {
        if (!key) return nullptr;
        auto f = map.find(*key);
        return f == map.end() ? nullptr : f->second;
    };
    for (Part* p : newparts) {
        std::string key = p->assembly["corner_source"]["key"]; std::optional<std::string> oldkey;
        if (matches.contains(key)) oldkey = matches[key];
        if (key.starts_with("shelf.")) oldkey = "main.bay.0.shelf." + std::to_string(std::stoi(key.substr(6)) + 1);
        const Part* previous = lookup(old_by_key, oldkey);
        if (!previous) previous = lookup(old_by_name, oldkey && names.contains(*oldkey) ? names[*oldkey] : std::string());
        if (!previous && key.starts_with("shelf.")) previous = lookup(old_by_name, "Półka " + std::to_string(std::stoi(key.substr(6)) + 1));
        if (previous) {
            changed_ids[p->id] = previous->id; p->id = previous->id; p->visible = previous->visible; p->grain = previous->grain;
            // HDF backing becomes a structural side; its old material is not
            // reused at the new panel thickness.
            if (key != "back_x") p->material = previous->material;
        }
        p->layout = layout;
    }
    std::function<json(const json&)> remap = [&](const json& value) -> json {
        if (value.is_object()) { json out = json::object(); for (auto& [k, v] : value.items()) out[k] = remap(v); return out; }
        if (value.is_array()) { json out = json::array(); for (auto& v : value) out.push_back(remap(v)); return out; }
        if (value.is_string()) { auto f = changed_ids.find(value.get<std::string>()); return f != changed_ids.end() ? json(f->second) : value; }
        return value;
    };
    for (Part* p : newparts) { p->assembly = remap(p->assembly); p->operations = remap(p->operations); p->machining = remap(p->machining); }
    result->hardware = remap(result->hardware);
    Part* bottom = nullptr; for (Part* p : newparts) if (p->role == "bottom") { bottom = p; break; }
    if (bottom == nullptr) throw py::StopIteration();
    json manifest = json::object(); for (Part* p : newparts) manifest[p->assembly["corner_source"]["key"].get<std::string>()] = {{"id", p->id}, {"signature", _geometry(*p)}};
    bottom->assembly["corner_source"]["manifest"] = manifest;
    result = workspace_worktops::refresh_shared_worktops(result, false);
    result->validate(); return {result, newgroup};
}

// Verified C++ calculation ports; UI and exact CAD validators are retained.
// (W Pythonie: native_model.install_construction(globals(), 'corner_furniture') — tu bez odpowiednika.)

}  // namespace corner_furniture
