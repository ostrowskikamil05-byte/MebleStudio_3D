// Explicit, editable construction recipes. No machine or vendor certification.
#include "assembly.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "corner_furniture.h"
#include "drag_hardware.h"
#include "fittings.h"
#include "mechanisms.h"
#include "opening_options.h"
#include "runner_installation.h"
#include "runners_catalog.h"

namespace assembly {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::Point3;
using core::finite;
using core::uid;
using core::local_world;
using py::ValueError;
using py::StopIteration;

const std::vector<std::pair<std::string, std::string>> ROLES{
    {"Inny / swobodny element", "other"}, {"Bok lewy", "left"}, {"Bok prawy", "right"},
    {"Ścianka / przegroda", "partition"}, {"Góra / wieniec", "top"}, {"Dół / dno", "bottom"},
    {"Blat", "worktop"}, {"Drzwiczki", "door"}, {"Półka", "shelf"}, {"Plecy", "back"},
    {"Szuflada z frontem", "drawer"}};
const json DEFAULTS = {{"gap", 2}, {"level", 300}, {"offset", 0}, {"hinge_side", "left"}, {"hinge_count", 2},
    {"hinge_end", 100}, {"cup_diameter", 35}, {"cup_depth", 12.5}, {"cup_edge", 5},
    {"plate_line", 37}, {"plate_spacing", 32}, {"screw_diameter", 3}, {"screw_depth", 10},
    {"drawer_clearance", 13}, {"drawer_depth", 400}, {"drawer_height", 120}, {"drawer_t", 16},
    {"drawer_bottom", 6}, {"drawer_raise", 0}, {"drawer_front_gap", 20}, {"slide_line", 25},
    {"slide_first", 37}, {"slide_last", 250}, {"shelf_end", 40}, {"shelf_diameter", 5}, {"shelf_depth", 10},
    {"hinge_price", 0}, {"slide_price", 0}, {"shelf_price", 0}, {"auto_hardware", true}};
const std::vector<std::pair<std::string, std::string>> PARAMETERS{
    {"hinge_count", "Liczba zawiasów"}, {"hinge_end", "Zawias od końca frontu [mm]"},
    {"cup_diameter", "Średnica puszki [mm]"}, {"cup_depth", "Głębokość puszki [mm]"},
    {"cup_edge", "Puszka: od krawędzi do obwodu [mm]"},
    {"plate_line", "Linia prowadnika od przodu korpusu [mm]"},
    {"plate_spacing", "Rozstaw wkrętów prowadnika [mm]"},
    {"screw_diameter", "Średnica otworów wkrętów [mm]"}, {"screw_depth", "Głębokość otworów wkrętów [mm]"},
    {"hinge_price", "Cena zawiasu z prowadnikiem [zł]"},
    {"drawer_clearance", "Luz prowadnicy na stronę [mm]"},
    {"drawer_depth", "Głębokość skrzynki [mm]"}, {"drawer_height", "Wysokość skrzynki [mm]"},
    {"drawer_t", "Grubość ścian skrzynki [mm]"}, {"drawer_bottom", "Grubość dna skrzynki [mm]"},
    {"drawer_raise", "Podniesienie skrzynki nad frontem [mm]"},
    {"drawer_front_gap", "Skrzynka od przodu korpusu [mm]"},
    {"slide_line", "Linia prowadnicy nad dnem skrzynki [mm]"},
    {"slide_first", "Pierwszy wkręt prowadnicy od przodu [mm]"},
    {"slide_last", "Drugi wkręt prowadnicy od przodu [mm]"},
    {"slide_price", "Cena pary prowadnic [zł]"},
    {"shelf_end", "Podpórka od końca półki [mm]"},
    {"shelf_diameter", "Średnica otworu podpórki [mm]"}, {"shelf_depth", "Głębokość otworu podpórki [mm]"},
    {"shelf_price", "Cena podpórki [zł]"}};

namespace {

double num(const json& value) { return value.get<double>(); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

const Part* find_part(const Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

bool contains(const json& list, const std::string& value) {
    if (!list.is_array()) return false;
    for (auto& item : list) if (item == value) return true;
    return false;
}

json& last_hardware(Project& project) { return project.hardware.back(); }

}  // namespace

std::string inferred_role(const Part& part) {
    if (part.role != "other") return part.role;
    if (part.name.starts_with("Front ")) return "door";
    if (part.name.starts_with("Półka ")) return "shelf";
    static const std::map<std::string, std::string> names{{"Bok lewy", "left"}, {"Bok prawy", "right"}, {"Dno", "bottom"}, {"Wieniec", "top"},
                                                          {"Blat", "worktop"}, {"Plecy nakładane HDF", "back"}};
    auto found = names.find(part.name);
    return found != names.end() ? found->second : "other";
}

std::pair<Point3, Point3> bounds(const Part& part) {
    // Assembly recipes always work in the cabinet's construction coordinates.
    std::vector<Point3> vertices;
    for (auto& [u, v] : part.points()) for (double t : {0.0, part.t}) vertices.push_back(local_world(part, u, v, t));
    Point3 lo = vertices[0], hi = vertices[0];
    for (auto& p : vertices) for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    return {lo, hi};
}

Frame frame(const Project& project, const std::string& group, const std::optional<std::string>& exclude, const json& bay) {
    if (py::truthy(bay)) {
        if (!bay.is_object()) throw ValueError("Nieprawidłowe odniesienie komory mebla.");
        std::map<std::string, const Part*> members;
        for (auto& p : project.parts) if (p.group == group) members[p.id] = &p;
        std::array<const Part*, 4> found{};
        int index = 0;
        for (std::string key : {"left", "right", "bottom", "top"}) {
            if (!bay.contains(key) || !bay[key].is_string() || !members.contains(bay[key].get<std::string>()))
                throw ValueError("Usunięto bok lub przegrodę komory. Przebuduj mebel w kreatorze.");
            found[index++] = members[bay[key].get<std::string>()];
        }
        const Part& left = *found[0]; const Part& right = *found[1]; const Part& bottom = *found[2]; const Part& top = *found[3];
        if (left.plane != "YZ" || right.plane != "YZ" || bottom.plane != "XY" || top.plane != "XY") throw ValueError("Komora wymaga pionowych boków i poziomych przegród.");
        for (const Part* p : found) for (double a : {p->rx, p->ry, p->rz}) if (std::abs(a) > 1e-8) throw ValueError("Boki komory wymagają zerowych lokalnych obrotów.");
        Point3 lo{left.x + (py::truthy(py::get(bay, "left_mid")) ? left.t / 2 : 0), std::max(left.y, right.y), bottom.z + (py::truthy(py::get(bay, "bottom_mid")) ? bottom.t / 2 : 0)};
        Point3 hi{right.x + (py::truthy(py::get(bay, "right_mid")) ? right.t / 2 : right.t), std::min(left.y + left.u, right.y + right.u), top.z + (py::truthy(py::get(bay, "top_mid")) ? top.t / 2 : top.t)};
        for (int i = 0; i < 3; ++i) if (hi[i] <= lo[i]) throw ValueError("Komora nie ma dodatnich wymiarów.");
        Frame f{lo, hi, left.id, right.id, left.x + left.t, right.x, bottom.z + bottom.t, top.z, {}, std::nullopt, std::nullopt, std::nullopt};
        for (const Part* p : found) if (std::find(f.parts.begin(), f.parts.end(), p->id) == f.parts.end()) f.parts.push_back(p->id);
        return f;
    }
    std::vector<const Part*> parts;
    for (auto& p : project.parts) {
        std::string role = inferred_role(p);
        if (p.group == group && (!exclude || p.id != *exclude)
            && !py::truthy(py::get(p.assembly, "generated_by")) && role != "door" && role != "drawer" && role != "back" && role != "worktop" && role != "shelf" && role != "accessory_panel")
            parts.push_back(&p);
    }
    if (parts.empty()) throw ValueError("Wybrany mebel nie ma konstrukcji odniesienia. Dodaj najpierw korpus lub własne boki.");
    for (const Part* p : parts) for (double angle : {p->rx, p->ry, p->rz})
        if (std::abs(angle) > 1e-8)
            throw ValueError("Automatyczne dopasowanie wymaga korpusu bez obrotów. Dla obróconej konstrukcji wybierz element swobodny.");
    std::vector<std::pair<Point3, Point3>> boxes; for (const Part* p : parts) boxes.push_back(bounds(*p));
    Point3 lower = boxes[0].first, upper = boxes[0].second;
    for (auto& [lo, hi] : boxes) for (int i = 0; i < 3; ++i) { lower[i] = std::min(lower[i], lo[i]); upper[i] = std::max(upper[i], hi[i]); }
    const Part* left = nullptr; const Part* right = nullptr;
    for (const Part* p : parts) if (inferred_role(*p) == "left" && (!left || p->x < left->x)) left = p;
    for (const Part* p : parts) if (inferred_role(*p) == "right" && (!right || p->x > right->x)) right = p;
    // Legacy/custom side panels can also be identified geometrically.
    std::vector<const Part*> vertical; for (const Part* p : parts) if (p->plane == "YZ") vertical.push_back(p);
    if (left == nullptr) for (const Part* p : vertical) if (!left || p->x < left->x) left = p;
    if (right == nullptr) for (const Part* p : vertical) if (!right || p->x > right->x) right = p;
    double inner_left = left ? left->x + left->t : lower[0];
    double inner_right = right && right != left ? right->x : upper[0];
    double inner_bottom = lower[2]; bool any_bottom = false;
    for (const Part* p : parts) if (inferred_role(*p) == "bottom") { inner_bottom = any_bottom ? std::max(inner_bottom, p->z + p->t) : p->z + p->t; any_bottom = true; }
    double inner_top = upper[2]; bool any_top = false;
    for (const Part* p : parts) if (inferred_role(*p) == "top") { inner_top = any_top ? std::min(inner_top, p->z) : p->z; any_top = true; }
    Frame f{lower, upper, left ? std::optional<std::string>(left->id) : std::nullopt, right ? std::optional<std::string>(right->id) : std::nullopt,
            inner_left, inner_right, inner_bottom, inner_top, {}, std::nullopt, std::nullopt, std::nullopt};
    for (const Part* p : parts) f.parts.push_back(p->id);
    return f;
}

Frame frame_for_part(const Project& project, const Part& part) {
    json config = py::get(part.assembly, "config", json::object());
    if (py::truthy(py::get(config, "corner_bay"))) {
        return corner_furniture::corner_front_frame(project, part);
    }
    return frame(project, part.group, std::nullopt, py::get(config, "bay"));
}

json recipe(const json& values) {
    json config = DEFAULTS;
    if (py::truthy(values)) for (auto& [key, value] : values.items()) config[key] = value;
    for (auto& [key, _] : DEFAULTS.items()) {
        if (key == "hinge_side" || key == "auto_hardware") continue;
        config[key] = finite(config[key], 0, !key.ends_with("_price") ? 10000 : 1e6);
    }
    if (config["hinge_side"] != "left" && config["hinge_side"] != "right") throw ValueError("Wybierz lewą lub prawą stronę zawiasów.");
    double hinge_count = num(config["hinge_count"]);
    if (hinge_count != std::trunc(hinge_count) || !(2 <= hinge_count && hinge_count <= 8))
        throw ValueError("Liczba zawiasów: od 2 do 8.");
    config["hinge_count"] = (long long)hinge_count;
    return config;
}

std::pair<double, double> suggested_size(const Project& project, const std::string& group, const std::string& role, double thickness, const json& config, const std::optional<std::string>& exclude) {
    json c = recipe(config); Frame f = frame(project, group, exclude, py::get(c, "bay"));
    Point3 lo = f.lower, hi = f.upper; double width = hi[0] - lo[0]; double depth = hi[1] - lo[1]; double height = hi[2] - lo[2];
    double inner = f.inner_right - f.inner_left; double gap = num(c["gap"]);
    (void)thickness;
    if (role == "left" || role == "right") return {depth, height};
    if (role == "partition") return {depth, f.inner_top - f.inner_bottom};
    if (role == "top" || role == "bottom") return {inner, depth};
    if (role == "shelf") return {inner - 2 * gap, depth - 2 * gap};
    if (role == "worktop") return {width, depth};
    if (role == "door") return {width - 2 * gap, height - 2 * gap};
    if (role == "drawer") return {width - 2 * gap, num(c["drawer_height"]) + num(c["drawer_bottom"]) + 2 * gap + 20};
    if (role == "back") return {width, height};
    throw ValueError("Dopasowanie jest dostępne dla elementów konstrukcyjnych.");
}

void clear_generated(Project& project, const std::string& owner_id) {
    std::set<std::string> generated;
    for (auto& p : project.parts) if (py::get(p.assembly, "generated_by") == owner_id) generated.insert(p.id);
    std::set<std::string> removed = generated; removed.insert(owner_id);
    auto kept_by_owner = [&](const json& items) {
        json kept = json::array();
        for (auto& item : items) { json owner = py::get(item, "owner_id"); if (!(owner.is_string() && removed.contains(owner.get<std::string>()))) kept.push_back(item); }
        return kept;
    };
    std::erase_if(project.parts, [&](const Part& p) { return generated.contains(p.id); });
    project.hardware = kept_by_owner(project.hardware);
    for (auto& p : project.parts) {
        p.operations = kept_by_owner(p.operations);
    }
}

void delete_with_connections(Project& project, const std::string& part_id) {
    std::vector<std::string> affected;
    for (auto& p : project.parts) if (contains(py::get(p.assembly, "anchors", json::array()), part_id)) affected.push_back(p.id);
    for (auto& owner : affected) {
        clear_generated(project, owner);
        Part* p = find_part(project, owner);
        if (p) { p->assembly = json::object(); p->role = "other"; }
    }
    clear_generated(project, part_id);
    std::erase_if(project.parts, [&](const Part& p) { return p.id == part_id; });
}

void drill(Part& part, const Part& owner, double u, double v, double diameter, double depth, const std::string& face, const std::string& purpose) {
    if (diameter <= 0 || depth <= 0) throw ValueError("Średnica i głębokość wiercenia muszą być dodatnie.");
    if (depth >= part.t) throw ValueError(std::format("{}: otwór {} wymaga mniejszej głębokości niż grubość elementu.", part.name, purpose));
    json operation = {{"type", "drill"}, {"u", u}, {"v", v}, {"diameter", diameter}, {"depth", depth}, {"face", face},
                      {"owner_id", owner.id}, {"purpose", purpose}, {"requires_review", true}};
    part.operations.push_back(operation);
    try { part.validate(); }
    catch (const ValueError& e) { throw ValueError(std::format("{}, {}: {}", part.name, purpose, e.what())); }
}

void accessory(Project& project, const Part& owner, const std::string& name, const json& qty, const json& price, const std::vector<const Part*>& anchors, const json& positions) {
    json anchor_ids = json::array(); for (const Part* p : anchors) anchor_ids.push_back(p->id);
    project.hardware.push_back({{"name", name}, {"qty", qty}, {"price", price}, {"owner_id", owner.id},
        {"anchors", anchor_ids}, {"positions", positions}, {"requires_review", true}, {"recipe", "generic-editable-1"}});
}

bool rectangular(const Part& part) {
    return std::abs(part.area() * 1e6 - part.u * part.v) < 1e-5;
}

std::pair<Part*, Part*> require_sides(Project& project, const Frame& f) {
    Part* left = f.left ? find_part(project, *f.left) : nullptr; Part* right = f.right ? find_part(project, *f.right) : nullptr;
    if (!left || !right || left->id == right->id)
        throw ValueError("Automatyczne okucia wymagają dwóch boków korpusu. Nadaj im funkcje Bok lewy i Bok prawy.");
    return {left, right};
}

void set_placement(Part& q, const Frame& f, const json& c) {
    Point3 lo = f.lower, hi = f.upper; double gap = num(c["gap"]); std::string role = q.role;
    q.rx = q.ry = q.rz = 0;
    if (role == "left" || role == "right" || role == "partition") {
        q.plane = "YZ"; q.x = role == "left" ? lo[0] : role == "right" ? hi[0] - q.t : f.inner_left + num(c["offset"]);
        q.y = lo[1]; q.z = lo[2];
        if (role == "partition") q.z = f.inner_bottom;
    } else if (role == "top" || role == "bottom" || role == "worktop" || role == "shelf") {
        q.plane = "XY"; q.x = f.inner_left + (role == "shelf" ? gap : 0); q.y = lo[1] + (role == "shelf" ? gap : 0);
        q.z = role == "top" ? hi[2] - q.t : role == "bottom" ? lo[2] : role == "worktop" ? hi[2] : lo[2] + num(c["level"]);
        if (role == "worktop") q.x = lo[0];
    } else {
        bool front = role == "door" || role == "drawer";
        q.plane = "XZ"; q.x = lo[0] + (front ? gap : 0);
        q.y = front ? lo[1] - q.t - num(py::get(c, "opening_gap", gap)) : hi[1];
        q.z = lo[2] + (role == "door" ? gap : role == "drawer" ? num(c["level"]) : 0);
        if (role == "door" && c["hinge_side"] == "right") q.x = hi[0] - gap - q.u;
        if (role == "door") q.y -= num(py::get(c, "slide_lane", 0));
    }
    // Sketch dimensions are authoritative; never silently rescale them.
    if (role == "shelf" || role == "top" || role == "bottom" || role == "partition") {
        if (q.x < f.inner_left - 1e-6 || q.x + (role == "partition" ? q.t : q.u) > f.inner_right + 1e-6)
            throw ValueError("Element nie mieści się między bokami. Użyj „Dopasuj prostokąt do mebla” lub zmień szkic.");
    }
    if (role == "shelf" && !(f.inner_bottom <= q.z && q.z <= f.inner_top - q.t))
        throw ValueError("Wysokość półki wykracza poza wnętrze korpusu.");
    if ((role == "door" || role == "drawer") && (q.x + q.u > hi[0] - gap + 1e-6 || q.z + q.v > hi[2] - gap + 1e-6))
        throw ValueError("Front wykracza poza mebel. Zmień jego wymiary lub wysokość montażu.");
}

void check_overlap(const Project& project, const Part& q) {
    auto [lo, hi] = bounds(q);
    for (auto& p : project.parts) {
        if (p.id == q.id || p.group != q.group || py::get(p.assembly, "generated_by") == q.id) continue;
        auto [a, b] = bounds(p);
        bool all = true;
        for (int i = 0; i < 3; ++i) if (!(std::min(hi[i], b[i]) - std::max(lo[i], a[i]) > 0.01)) all = false;
        if (all)
            throw ValueError(std::format("Element koliduje z „{}”. Dla boku/góry/dna zaznacz zastępowanie, albo edytuj istniejący element.", p.name));
    }
}

void hinges(Project& project, Part& q, const Frame& f, const json& c) {
    auto [left, right] = require_sides(project, f); Part* support = c["hinge_side"] == "left" ? left : right;
    // An overlay door must reach the chosen carcass side.
    bool left_side = c["hinge_side"] == "left";
    double edge = left_side ? q.x : q.x + q.u;
    double expected = left_side ? f.lower[0] + num(c["gap"]) : f.upper[0] - num(c["gap"]);
    if (std::abs(edge - expected) > 0.1) throw ValueError("Krawędź zawiasowa frontu musi być przy wybranym boku korpusu.");
    long long count = c["hinge_count"].get<long long>(); double diameter = num(c["cup_diameter"]); double end = std::min(num(c["hinge_end"]), q.v / 4);
    if (q.v < diameter * 2 + 8) throw ValueError("Front jest za niski dla dwóch puszek zawiasów Ø35.");
    if (q.v <= 2 * end) throw ValueError("Front jest za niski dla odległości zawiasów od końców.");
    double cup = num(c["cup_edge"]) + diameter / 2;
    double u = left_side ? cup : q.u - cup;
    json positions = json::array(); json hinge_spec = json::object();
    for (long long i = 0; i < count; ++i) {
        double v = end + (q.v - 2 * end) * i / (count - 1);
        drill(q, q, u, v, diameter, num(c["cup_depth"]), "top", "puszka zawiasu");
        if (py::truthy(py::get(c, "hinge_pattern"))) {
            json r = fittings::recipe(c["hinge_pattern"]); hinge_spec = r; double pilot_u = left_side ? u - num(r["offset"]) : u + num(r["offset"]);
            if (!(num(py::get(r, "min_thickness", 0)) <= q.t && q.t <= num(py::get(r, "max_thickness", 10000)))) throw ValueError("Grubość frontu nie mieści się w zakresie wybranego zawiasu.");
            for (double pilot_v : {v - num(r["pitch"]) / 2, v + num(r["pitch"]) / 2}) {
                drill(q, q, pilot_u, pilot_v, num(r["pilot"]), num(r["pilot_depth"]), "top", "mocowanie puszki zawiasu");
            }
        }
        double z = q.z + v;
        for (double delta : {-num(c["plate_spacing"]) / 2, num(c["plate_spacing"]) / 2}) {
            drill(*support, q, f.lower[1] + num(c["plate_line"]) - support->y, z + delta - support->z,
                  num(c["screw_diameter"]), num(c["screw_depth"]), support == left ? "top" : "bottom", "prowadnik zawiasu");
        }
        positions.push_back({{"x", edge}, {"y", f.lower[1]}, {"z", z}});
    }
    accessory(project, q, "Zawias puszkowy + prowadnik — wzorzec roboczy", count, c["hinge_price"], {support}, positions);
    json spring = py::get(hinge_spec, "closing_spring", py::get(c, "hinge_spring", "unknown"));
    if (spring == "sprung" && py::get(c, "hinge_spring") == "unsprung")
        throw ValueError("Wybrany zawias ma sprężynę. Wybierz zawias bez sprężyny dla magnetycznego wypychacza.");
    json levels = json::array(); for (long long i = 0; i < count; ++i) levels.push_back(end + (q.v - 2 * end) * i / (count - 1));
    json& last = last_hardware(project);
    last["kind"] = "hinge"; last["levels"] = levels; last["color"] = "#aab0b6"; last["closing_spring"] = spring;
    if (py::truthy(py::get(hinge_spec, "article"))) {
        last["article"] = hinge_spec["article"]; last["manufacturer"] = py::get(hinge_spec, "manufacturer", ""); last["source"] = hinge_spec.at("source");
    }
}

void shelf_supports(Project& project, Part& q, const Frame& f, const json& c) {
    auto [left, right] = require_sides(project, f); double end = num(c["shelf_end"]); json positions = json::array();
    if (q.v <= 2 * end) throw ValueError("Półka jest za płytka dla rozstawu podpórek.");
    if (std::abs(q.x - f.inner_left - num(c["gap"])) > .1 || std::abs(q.x + q.u - f.inner_right + num(c["gap"])) > .1)
        throw ValueError("Półka z podpórkami musi sięgać obu boków z wybranym luzem.");
    for (auto [side, face] : std::vector<std::pair<Part*, std::string>>{{left, "top"}, {right, "bottom"}}) {
        for (double v : {end, q.v - end}) {
            double z = q.z - num(c["shelf_diameter"]) / 2;
            drill(*side, q, q.y + v - side->y, z - side->z, num(c["shelf_diameter"]), num(c["shelf_depth"]), face, "podpórka półki");
            positions.push_back({{"x", side == left ? side->x + side->t : side->x}, {"y", q.y + v}, {"z", z}});
        }
    }
    accessory(project, q, "Podpórka półki — wzorzec roboczy", 4, c["shelf_price"], {left, right}, positions);
}

void drawer_box(Project& project, Part& q_in, const Frame& f, json& c) {
    // W Pythonie części są obiektami-referencjami. Tu rezerwujemy miejsce na
    // pięć ścian skrzynki, aby wskaźniki do części projektu (q, boki korpusu,
    // boki skrzynki) pozostały ważne po ich dodaniu.
    std::string q_id = q_in.id;
    bool q_in_project = find_part(project, q_id) == &q_in;
    project.parts.reserve(project.parts.size() + 5);
    Part& q = q_in_project ? *find_part(project, q_id) : q_in;
    auto [left, right] = require_sides(project, f);
    if (!rectangular(q)) throw ValueError("Skrzynka szuflady wymaga prostokątnego frontu.");
    double width = f.inner_right - f.inner_left - 2 * num(c["drawer_clearance"]);
    double depth = num(c["drawer_depth"]), height = num(c["drawer_height"]), t = num(c["drawer_t"]), bottom = num(c["drawer_bottom"]);
    json depth_context = runner_installation::drawer_depth_context(project, q, c);
    json plan = nullptr;
    if (py::truthy(py::get(c, "runner_profile"))) {
        plan = runners_catalog::calculate_runner(c["runner_profile"], f.inner_right - f.inner_left, c["runner_nominal"], t,
            depth_context["usable_depth"], "four_sided", t,
            py::get(c, "runner_confirmed_min_depth"));
        if (!py::truthy(plan["valid"])) {
            std::vector<std::string> errors; for (auto& e : plan["errors"]) errors.push_back(e.get<std::string>());
            throw ValueError(py::join(errors, "\n"));
        }
        width = num(plan["drawer_outer_width"]); depth = num(plan["drawer_length"]); c["drawer_clearance"] = plan["external_clearance_per_side"]; c["drawer_depth"] = plan["drawer_length"];
    }
    if (std::min({width - 2 * t, depth - 2 * t, height, t, bottom}) <= 0) throw ValueError("Nieprawidłowe wymiary skrzynki szuflady.");
    double x = f.inner_left + num(c["drawer_clearance"]); double y = num(depth_context["start_y"]); double z = q.z + num(c["gap"]) + num(c["drawer_raise"]);
    if (y + depth > num(depth_context["end_y"]) + 1e-8 || z < f.inner_bottom || z + bottom + height > f.inner_top)
        throw ValueError("Skrzynka szuflady nie mieści się w korpusie. Zmień wysokość, głębokość lub poziom montażu.");
    if (z + bottom + height > q.z + q.v) throw ValueError("Front jest za niski dla wysokości skrzynki.");
    // Check occupied volume against shelves and partitions already present.
    for (auto& p : project.parts) {
        if (p.group != q.group || p.id == q.id || py::get(p.assembly, "generated_by") == q.id) continue;
        std::string role = inferred_role(p);
        if (role == "left" || role == "right" || role == "top" || role == "bottom" || role == "door") continue;
        auto [lo, hi] = bounds(p);
        Point3 a{x + width, y + depth, z + bottom + height}, c0{x, y, z};
        bool all = true;
        for (int i = 0; i < 3; ++i) if (!(std::min(a[i], hi[i]) - std::max(c0[i], lo[i]) > 0.01)) all = false;
        if (all)
            throw ValueError(std::format("Skrzynka szuflady koliduje z elementem „{}”. Przesuń go lub zmień poziom szuflady.", p.name));
    }
    std::vector<Part*> children;
    auto child = [&](const std::string& name, double u, double v, double thick, const std::string& plane, double px, double py_, double pz, const std::string& material = "") {
        Part p{.name = q.name + " / " + name, .group = q.group, .u = u, .v = v, .t = thick, .plane = plane,
               .x = px, .y = py_, .z = pz, .material = !material.empty() ? material : q.material, .role = "drawer_box",
               .assembly = {{"generated_by", q.id}}, .layout = q.layout};
        p.validate(); project.parts.push_back(p); children.push_back(&project.parts.back()); return &project.parts.back();
    };
    std::string bottom_material = q.material;
    for (auto& m : project.materials) if (m.kind == "hdf") { bottom_material = m.id; break; }
    Part* a = nullptr; Part* b = nullptr;
    if (py::truthy(plan) && plan["mount"] == "undermount") {
        double recess = std::max(py::truthy(plan["bottom_recess_min"]) ? num(plan["bottom_recess_min"]) : 0.0, num(py::get(c, "drawer_bottom_recess", 12)));
        if (!plan["bottom_recess_max"].is_null() && recess > num(plan["bottom_recess_max"]))
            throw ValueError(std::format("Cofnięcie dna: dla tej serii dopuszczalne maksymalnie {:g} mm.", num(plan["bottom_recess_max"])));
        if (height - recess - bottom <= 0) throw ValueError("Bok szuflady jest za niski dla cofnięcia dna i jego grubości.");
        a = child("bok lewy", depth, height, t, "YZ", x, y, z);
        b = child("bok prawy", depth, height, t, "YZ", x + width - t, y, z);
        child("przód skrzynki", width - 2 * t, height - recess - bottom, t, "XZ", x + t, y, z + recess + bottom);
        child("tył skrzynki", width - 2 * t, height - recess - bottom, t, "XZ", x + t, y + depth - t, z + recess + bottom);
        child("dno wpuszczone — model skrzynki", width - 2 * t, depth - 2 * t, bottom, "XY", x + t, y + t, z + recess, bottom_material);
    } else {
        a = child("bok lewy", depth, height, t, "YZ", x, y, z + bottom);
        b = child("bok prawy", depth, height, t, "YZ", x + width - t, y, z + bottom);
        child("przód skrzynki", width - 2 * t, height, t, "XZ", x + t, y, z + bottom);
        child("tył skrzynki", width - 2 * t, height, t, "XZ", x + t, y + depth - t, z + bottom);
        child("dno nakładane", width, depth, bottom, "XY", x, y, z, bottom_material);
    }
    json children_ids = json::array(); for (Part* p : children) children_ids.push_back(p->id);
    q.assembly["children"] = children_ids;
    if (py::truthy(plan)) {
        json profile = runners_catalog::PROFILES.at(c["runner_profile"].get<std::string>());
        json positions = json::array();
        if (plan["mount"] == "undermount") positions = json::array({json{{"x", x}, {"y", y}, {"z", z}}, json{{"x", x + width}, {"y", y}, {"z", z}}});
        else for (Part* side : {left, right}) positions.push_back({{"x", side == left ? side->x + side->t : side->x}, {"y", y}, {"z", z + bottom + num(c["slide_line"])}});
        accessory(project, q, profile["label"].get<std::string>() + " — para", 1, c["slide_price"], {left, right}, positions);
        json& last = last_hardware(project);
        json source = py::truthy(py::get(plan, "variant_source")) ? plan["variant_source"] : plan["sources"][0];
        last["kind"] = "runner"; last["mount"] = plan["mount"]; last["length"] = c["runner_nominal"]; last["closed_front_y"] = q.y; last["color"] = "#aab0b6";
        last["runner_profile"] = c["runner_profile"]; last["dimensions"] = plan; last["source"] = source; last["requires_review"] = true;
        last["installation_status"] = "Wymiary planowania; wiercenia mocujące, zaczepy, rowki i nośność wymagają doboru wariantu.";
        return;
    }
    if (!py::truthy(c["auto_hardware"])) return;
    if (num(c["slide_first"]) >= num(c["slide_last"]) || num(c["slide_last"]) >= depth)
        throw ValueError("Otwory prowadnicy muszą być kolejno rozmieszczone wewnątrz głębokości skrzynki.");
    if (!(0 < num(c["slide_line"]) && num(c["slide_line"]) < height)) throw ValueError("Linia prowadnicy musi leżeć na boku skrzynki.");
    json positions = json::array();
    struct Side { Part* side; Part* box; std::string face; std::string box_face; };
    for (auto& [side, box, face, box_face] : std::vector<Side>{{left, a, "top", "bottom"}, {right, b, "bottom", "top"}}) {
        for (double distance : {num(c["slide_first"]), num(c["slide_last"])}) {
            double level = z + bottom + num(c["slide_line"]); double world_y = y + distance;
            drill(*side, q, world_y - side->y, level - side->z, num(c["screw_diameter"]), num(c["screw_depth"]), face, "prowadnica korpusu");
            drill(*box, q, distance, num(c["slide_line"]), num(c["screw_diameter"]), num(c["screw_depth"]), box_face, "prowadnica skrzynki");
        }
        positions.push_back({{"x", side == left ? side->x + side->t : side->x}, {"y", y}, {"z", z + bottom + num(c["slide_line"])}});
    }
    accessory(project, q, "Para prowadnic bocznych — wzorzec roboczy", 1, c["slide_price"], {left, right}, positions);
    json& last = last_hardware(project);
    last["kind"] = "runner"; last["length"] = depth; last["closed_front_y"] = q.y; last["color"] = "#aab0b6";
}

std::pair<ProjectPtr, Part*> attach(const Project& project, const Part& part, const std::string& group, const std::string& role, const json& config) {
    // Return an atomic candidate project; validation failure never mutates input.
    auto candidate = std::make_shared<Project>(project); Part q = part; json c = recipe(config);
    json retained_hardware = json::array();
    for (auto& h : candidate->hardware) if (py::get(h, "owner_id") == q.id && py::truthy(py::get(h, "manual"))) retained_hardware.push_back(h);
    json saved_features = json::object();
    for (auto& p : candidate->parts) {
        if (py::get(p.assembly, "generated_by") != q.id) continue;
        json features = json::object();
        for (std::string key : {"fillets", "chamfers", "machining", "holes", "operations"}) features[key] = p.get_attr(key);
        saved_features[p.name] = features;
    }
    clear_generated(*candidate, q.id);
    std::optional<Frame> f;
    if (role != "other") f = frame(*candidate, group, std::nullopt, py::get(c, "bay"));
    std::erase_if(candidate->parts, [&](const Part& p) { return p.id == q.id; });
    json operations = json::array();
    for (auto& op : q.operations) if (py::get(op, "owner_id") != q.id) operations.push_back(op);
    q.operations = operations;
    q.group = group; q.role = role;
    for (auto& p : candidate->parts) if (p.group == group && p.id != q.id) { q.layout = p.layout; break; }
    if (role == "other") {
        q.assembly = json::object(); candidate->parts.push_back(q); candidate->validate(); return {candidate, find_part(*candidate, q.id)};
    }
    if (std::none_of(ROLES.begin(), ROLES.end(), [&](const auto& item) { return item.second == role; })) throw ValueError("Nieznana funkcja elementu.");
    set_placement(q, *f, c);
    check_overlap(*candidate, q);
    json anchors = json::array(); for (auto& id : f->parts) if (id != q.id) anchors.push_back(id);
    q.assembly = {{"group", group}, {"role", role}, {"config", c}, {"anchors", anchors}, {"requires_review", true}};
    candidate->parts.reserve(candidate->parts.size() + 6);  // q oraz do pięciu ścian skrzynki szuflady
    candidate->parts.push_back(q);
    Part& added = candidate->parts.back();
    if (role == "door" && py::truthy(c["auto_hardware"])) hinges(*candidate, added, *f, c);
    else if (role == "shelf" && py::truthy(c["auto_hardware"])) shelf_supports(*candidate, added, *f, c);
    else if (role == "drawer") {
        drawer_box(*candidate, added, *f, c);
        // W Pythonie q.assembly['config'] i c to ten sam słownik, który drawer_box modyfikuje.
        find_part(*candidate, q.id)->assembly["config"] = c;
    }
    for (auto& p : candidate->parts) {
        if (py::get(p.assembly, "generated_by") == q.id && saved_features.contains(p.name)) {
            for (auto& [key, value] : saved_features[p.name].items()) {
                if (key == "operations") { for (auto& op : value) if (!py::truthy(py::get(op, "owner_id"))) p.operations.push_back(op); }
                else p.set_attr(key, value);
            }
        }
    }
    for (auto& h : retained_hardware) candidate->hardware.push_back(h);
    fittings::refresh_handles(*candidate, q.id);
    mechanisms::refresh_mechanism_hardware(*candidate, q.id);
    opening_options::refresh_openings(*candidate, q.id);
    candidate->validate(); return {candidate, find_part(*candidate, q.id)};
}

ProjectPtr rebuild_dependents(const Project& project, const std::string& changed_id) {
    auto result = std::make_shared<Project>(project);
    std::vector<std::string> owners;
    for (auto& p : result->parts) if (contains(py::get(p.assembly, "anchors", json::array()), changed_id)) owners.push_back(p.id);
    for (auto& owner : owners) {
        Part* p = find_part(*result, owner);
        if (p == nullptr) throw StopIteration();
        if (py::truthy(py::get(py::get(p->assembly, "config", json::object()), "corner_bay"))) {
            json cfg = p->assembly["config"]; result = corner_furniture::regenerate_corner_hinges(*result, p->id, cfg["hinge_pattern"], cfg["hinge_count"], 0, false, false);
        } else result = attach(*result, *p, p->group, p->role, py::get(p->assembly, "config")).first;
    }
    drag_hardware::refresh_manual_attachments(*result);
    result->validate(); return result;
}

// Verified C++ calculation ports; UI and exact CAD validators are retained.
// (W Pythonie: native_model.install_construction(globals(), 'assembly') podmieniał
// te funkcje na porty C++. Ten plik jest już w C++, więc podmiana nie ma odpowiednika.)

}  // namespace assembly
