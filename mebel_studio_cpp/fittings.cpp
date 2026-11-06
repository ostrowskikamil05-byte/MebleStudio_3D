// Source-linked fitting recipes. Generic recipes are explicitly marked.
#include "fittings.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "assembly.h"
#include "features.h"

namespace fittings {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::finite;
using py::ValueError;

const std::string SYSTEM32_SOURCE = "https://web.hettich.com/fileadmin/Media_Center/09_Further_products/01_Catalogues/Verarbeitungstechnik_2018_16918_en.pdf";
const std::string HINGE_SOURCE = "https://www.hettich.com/fileadmin/Media_Center/Catalogue/Hinges_Cat_HUK_Online__NEW_96dpi%20.pdf";
const std::string RUNNER_SOURCE = "https://shop.hettich.com/pl_EN/-/KA-5632-full-extension-runner%2C-load-class-45-kg%2C-12-7-mm-installed-width/bp/variant9396199120838";
const std::string CONNECTOR_SOURCE = "https://www.hafele.com/us/en/products/furniture-fittings-living-solutions/connectors-shelf-supports/connectors/50/";

const json RECIPES = json::array({
    {{"id", "system32"}, {"category", "System 32"}, {"name", "System 32 — Ø5 / krok 32 / linia 37"}, {"diameter", 5}, {"depth", 10}, {"pitch", 32}, {"line", 37}, {"verified", "rozstaw i średnica; głębokość robocza"}, {"source", SYSTEM32_SOURCE}},
    {{"id", "plate_direct"}, {"category", "Zawiasy"}, {"name", "Hettich Direkt — Ø5 × 7,5 / 32"}, {"diameter", 5}, {"depth", 7.5}, {"pitch", 32}, {"line", 37}, {"verified", "otwory prowadnika; nie pełny zawias"}, {"source", "https://shop.hettich.com/fr_FR/-/Hettich-Direkt%2C-r%C3%A9glage-en-hauteur-direct/bp/variant70030644287"}},
    {{"id", "hinge45"}, {"category", "Zawiasy"}, {"name", "Puszka Ø35 — wzór mocowania 45 × 9,5"}, {"diameter", 35}, {"depth", 12.5}, {"pitch", 45}, {"offset", 9.5}, {"pilot", 2.8}, {"pilot_depth", 8}, {"verified", "puszka i rozstaw z katalogu; pilot roboczy"}, {"source", HINGE_SOURCE}},
    {{"id", "hinge52"}, {"category", "Zawiasy"}, {"name", "Puszka Ø35 — własny wzór mocowania 52 × 9"}, {"diameter", 35}, {"depth", 12.5}, {"pitch", 52}, {"offset", 9}, {"pilot", 3}, {"pilot_depth", 8}, {"verified", "własny wzorzec roboczy; nie wzór Sensys TH52"}, {"source", ""}},
    {{"id", "hinge_sensys52"}, {"category", "Zawiasy"}, {"name", "Hettich Sensys 9071205 TH52 — Ø35 × 12,8 / 52 × 5,5"}, {"diameter", 35}, {"depth", 12.8}, {"pitch", 52}, {"offset", 5.5}, {"pilot", 2.5}, {"pilot_depth", 8}, {"min_thickness", 15}, {"max_thickness", 24}, {"article", "9071205"}, {"manufacturer", "Hettich"}, {"closing_spring", "sprung"}, {"verified", "nakładany, przykręcany; pilot maks. Ø2,5; głębokość pilota 8 i prowadnik robocze"}, {"source", "https://shop.hettich.com/si_EN/Hinges/Sensys-hinge-series/Hinges/Sensys-110%C2%B0-hinge-with-integrated-silent-system-%28Sensys-8645i%29%2C-nickel-plated%2C-overlay%2C-Opening-angle-110%C2%B0%2C-TH-drilling-pattern-52-x-5-5-mm%2C-for-screwing-on-%28-%29/p/9071205"}},
    {{"id", "hinge_sensys_tb55"}, {"category", "Zawiasy"}, {"name", "Sensys 9073655 — wiercenie TB55 Ø8 × 6 (wpuszczany)"}, {"diameter", 35}, {"depth", 12.8}, {"pitch", 45}, {"offset", 9.5}, {"pilot", 8}, {"pilot_depth", 6}, {"min_thickness", 15}, {"max_thickness", 24}, {"verified", "wiercenia puszki konkretnego zawiasu wpuszczanego; montaż w korpusie ręczny"}, {"source", "https://shop.hettich.com/us_EN/Hinges/Sensys-hinge-series/Hinges/Sensys-110%C2%B0-hinge-without-integrated-silent-system-%28Sensys-8645%29%2C-nickel-plated%2C-inset%2C-Opening-angle-110%C2%B0%2C-TB-drilling-pattern-45-x-9-5-mm%2C-Fix-fast-assembly-%28%C3%B8-8-x-6%29/p/9073655"}},
    {{"id", "confirmat7"}, {"category", "Połączenia"}, {"name", "Konfirmat 7 × 50 — pilot Ø5 / przelot Ø7"}, {"diameter", 5}, {"depth", 35}, {"clearance", 7}, {"countersink", 10}, {"countersink_depth", 2}, {"verified", "pilot Ø5; pozostałe wartości edytowalne robocze"}, {"source", CONNECTOR_SOURCE}},
    {{"id", "confirmat5"}, {"category", "Połączenia"}, {"name", "Konfirmat 5 × 50 — pilot Ø3,5 (roboczy)"}, {"diameter", 3.5}, {"depth", 35}, {"clearance", 5}, {"verified", "wzorzec roboczy"}, {"source", ""}},
    {{"id", "dowel8"}, {"category", "Połączenia"}, {"name", "Kołek drewniany Ø8 × 30"}, {"diameter", 8}, {"depth", 15}, {"verified", "wzorzec roboczy; głębokość na jedną część"}, {"source", ""}},
    {{"id", "dowel6"}, {"category", "Połączenia"}, {"name", "Kołek drewniany Ø6 × 30"}, {"diameter", 6}, {"depth", 15}, {"verified", "wzorzec roboczy"}, {"source", ""}},
    {{"id", "dowel10"}, {"category", "Połączenia"}, {"name", "Kołek drewniany Ø10 × 40"}, {"diameter", 10}, {"depth", 20}, {"verified", "wzorzec roboczy"}, {"source", ""}},
    {{"id", "cam15"}, {"category", "Połączenia"}, {"name", "Mimośród Ø15 — kieszeń robocza"}, {"diameter", 15}, {"depth", 12}, {"verified", "sam otwór pod puszkę, bez kompletnego złącza"}, {"source", "https://files.hafele.co.uk/catalogfiles/www/14CFC294.pdf"}},
    {{"id", "screw35"}, {"category", "Wkręty i śruby"}, {"name", "Wkręt Ø3,5 — pilot Ø2,8 × 8"}, {"diameter", 2.8}, {"depth", 8}, {"verified", "pilot z katalogu Blum"}, {"source", "https://publications.blum.com/2024/catalogue/en/160/"}},
    {{"id", "screw4"}, {"category", "Wkręty i śruby"}, {"name", "Wkręt Ø4 — pilot Ø3 × 12 (roboczy)"}, {"diameter", 3}, {"depth", 12}, {"verified", "wzorzec roboczy"}, {"source", ""}},
    {{"id", "screw5"}, {"category", "Wkręty i śruby"}, {"name", "Wkręt Ø5 — pilot Ø3,5 × 16 (roboczy)"}, {"diameter", 3.5}, {"depth", 16}, {"verified", "wzorzec roboczy"}, {"source", ""}},
    {{"id", "m4"}, {"category", "Wkręty i śruby"}, {"name", "Śruba M4 — przelot Ø4,5"}, {"diameter", 4.5}, {"depth", 0}, {"verified", "luz roboczy, nie gwintowanie"}, {"source", ""}},
    {{"id", "m6"}, {"category", "Wkręty i śruby"}, {"name", "Śruba M6 — przelot Ø6,5"}, {"diameter", 6.5}, {"depth", 0}, {"verified", "luz roboczy, nie gwintowanie"}, {"source", ""}},
    {{"id", "m8"}, {"category", "Wkręty i śruby"}, {"name", "Śruba M8 — przelot Ø9"}, {"diameter", 9}, {"depth", 0}, {"verified", "luz roboczy, nie gwintowanie"}, {"source", ""}},
    {{"id", "shelf5"}, {"category", "Podpórki"}, {"name", "Podpórka półki Ø5 × 10"}, {"diameter", 5}, {"depth", 10}, {"verified", "wzorzec roboczy"}, {"source", SYSTEM32_SOURCE}},
    {{"id", "shelf3"}, {"category", "Podpórki"}, {"name", "Podpórka półki Ø3 × 8"}, {"diameter", 3}, {"depth", 8}, {"verified", "wzorzec roboczy"}, {"source", ""}}});

const std::vector<std::pair<std::string, int>> HANDLES{{"Gałka", 0}, {"Uchwyt 64", 64}, {"Uchwyt 96", 96}, {"Uchwyt 128", 128}, {"Uchwyt 160", 160},
                                                       {"Uchwyt 192", 192}, {"Uchwyt 224", 224}, {"Uchwyt 256", 256}, {"Uchwyt 320", 320}};
const json FINISHES = {{"Czarny mat", "#242628"}, {"Stal szczotkowana", "#adb4bb"}, {"Chrom", "#d3d9df"}, {"Złoty", "#b39a5e"}, {"Biały", "#f0efeb"}, {"Miedź", "#b87957"}};

namespace {

double num(const json& value) { return value.get<double>(); }

Part& find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return p;
    throw py::StopIteration();  // next(...) bez wartości domyślnej
}

// dict.fromkeys([...]) dla liczb: bez powtórzeń, w kolejności pierwszego wystąpienia
std::vector<double> unique_numbers(const std::vector<double>& values) {
    std::vector<double> out;
    for (double v : values) if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    return out;
}

// op = hole_array(...)[0]
json first_hole(const Part& part, double u, double v, double diameter, double depth, const std::string& face) {
    json operations = features::hole_array(part, u, v, diameter, depth, 1, 1, 32, 32, face);
    return operations.at(0);
}

void update(json& target, const json& values) { for (auto& [k, v] : values.items()) target[k] = v; }

}  // namespace

json recipe(const json& recipe_id) {
    for (const json& r : RECIPES) if (r.at("id") == recipe_id) return r;
    throw py::StopIteration();
}

ProjectPtr system32(const Project& project, const std::string& part_id, const json& front_, const json& back_, const json& start_, const json& end_, const json& diameter_,
                    const json& depth_, bool both, const std::string& face) {
    ProjectPtr result = std::make_shared<Project>(project);
    Part& p = find_part(*result, part_id);
    double start = finite(start_, 0, 10000), end = finite(end_, 0, 10000), front = finite(front_, 0, 10000), back = finite(back_, 0, 10000);
    double diameter = finite(diameter_, 0, 10000), depth = finite(depth_, 0, 10000);
    if (p.v - start - end < 0) throw ValueError("Początek i koniec rzędu nie mieszczą się na boku.");
    json kept = json::array();
    for (auto& o : p.operations) if (py::get(o, "recipe_id") != "system32") kept.push_back(o);
    p.operations = kept;
    std::vector<double> columns = both ? std::vector<double>{front, p.u - back} : std::vector<double>{front};
    for (double u : unique_numbers(columns)) {
        json operations = features::hole_array(p, u, start, diameter, depth, 1, (long long)std::floor((p.v - start - end) / 32) + 1, 32, 32, face);
        for (auto& op : operations) update(op, {{"recipe_id", "system32"}, {"purpose", "System 32"}, {"source", SYSTEM32_SOURCE}});
        for (auto& op : operations) p.operations.push_back(op);
    }
    result->validate();
    return result;
}

ProjectPtr add_handle(const Project& project, const std::string& part_id, const json& spacing_, const json& u_, const json& v_, const json& vertical, const json& color, const json& projection_) {
    ProjectPtr result = std::make_shared<Project>(project);
    Part& p = find_part(*result, part_id);
    double spacing = finite(spacing_, 0, 1000), projection = finite(projection_, 1, 200);
    double u = finite(!u_.is_null() ? u_ : json(p.u - 40), 0, p.u), v = finite(!v_.is_null() ? v_ : json(p.v / 2), 0, p.v);
    json hardware = json::array();
    for (auto& h : result->hardware) if (!(py::get(h, "owner_id") == p.id && py::get(h, "kind") == "handle")) hardware.push_back(h);
    result->hardware = hardware;
    json kept = json::array();
    for (auto& o : p.operations) if (py::get(o, "recipe_id") != "handle") kept.push_back(o);
    p.operations = kept;
    std::vector<std::pair<double, double>> centers;
    if (spacing == 0) centers = {{u, v}};
    else if (py::truthy(vertical)) centers = {{u, v - spacing / 2}, {u, v + spacing / 2}};
    else centers = {{u - spacing / 2, v}, {u + spacing / 2, v}};
    for (auto [a, b] : centers) {
        json op = first_hole(p, a, b, 4.5, p.t, "bottom");
        update(op, {{"recipe_id", "handle"}, {"purpose", "Przelot pod śrubę M4 uchwytu"}});
        p.operations.push_back(op);
    }
    assembly::accessory(*result, p, spacing == 0 ? std::string("Gałka") : std::format("Uchwyt {:g} mm", spacing), 1, 0, {}, json::array());
    json& last = result->hardware.at(result->hardware.size() - 1);
    update(last, {{"kind", "handle"}, {"manual", true}, {"design_u", p.u}, {"design_v", p.v},
                  {"u_anchor", p.u - u < 80 ? "right" : u < 80 ? "left" : std::abs(u - p.u / 2) < .1 ? "center" : "fixed"},
                  {"v_anchor", std::abs(v - p.v / 2) < .1 ? "center" : "fixed"},
                  {"u", u}, {"v", v}, {"spacing", spacing}, {"vertical", py::truthy(vertical)}, {"color", color}, {"projection", projection}});
    result->validate();
    return result;
}

ProjectPtr cabinet_connectors(const Project& project, const std::string& group, const json& recipe_id_, const json& front_, const json& back_) {
    // Drill mating structural panels in world coordinates, axis aligned cabinet only.
    ProjectPtr result = std::make_shared<Project>(project);
    assembly::Frame f = assembly::frame(*result, group);
    auto [left, right] = assembly::require_sides(*result, f);
    json r = recipe(recipe_id_);
    std::string recipe_id = recipe_id_.is_string() ? recipe_id_.get<std::string>() : "";
    static const std::vector<std::string> supported{"confirmat7", "confirmat5", "dowel8", "dowel6", "dowel10"};
    if (std::find(supported.begin(), supported.end(), recipe_id) == supported.end()) throw ValueError("Ten wzór nie obsługuje automatycznego połączenia korpusu.");
    std::string tag = "connector:" + group;
    json hardware = json::array();
    for (auto& h : result->hardware) if (py::get(h, "connection_tag") != tag) hardware.push_back(h);
    result->hardware = hardware;
    for (auto& p : result->parts) {
        json kept = json::array();
        for (auto& o : p.operations) if (py::get(o, "connection_tag") != tag) kept.push_back(o);
        p.operations = kept;
    }
    std::vector<Part*> panels;
    for (const std::string& id : f.parts) {
        Part& p = find_part(*result, id);
        if ((assembly::inferred_role(p) == "top" || assembly::inferred_role(p) == "bottom") && p.plane == "XY") panels.push_back(&p);
    }
    if (panels.empty()) throw ValueError("Brak dna lub wieńca między bokami.");
    double front = num(front_), back = num(back_);
    for (Part* panel : panels) {
        if (std::abs(panel->x - f.inner_left) > .1 || std::abs(panel->x + panel->u - f.inner_right) > .1) throw ValueError("Dno i wieniec muszą przylegać do boków.");
        long long qty = 0;
        struct Side { Part* side; const char* outer; const char* edge; };
        for (auto [side, outer, edge] : {Side{left, "bottom", "u0"}, Side{right, "top", "u1"}}) {
            for (double distance : unique_numbers({front, panel->v - back})) {
                double y = panel->y + distance, z = panel->z + panel->t / 2;
                double diameter, side_depth; std::string side_face;
                if (recipe_id.rfind("dowel", 0) == 0) {
                    diameter = num(r["diameter"]); side_depth = std::min(num(r["depth"]), side->t - 2); side_face = side == left ? "top" : "bottom";
                } else {
                    diameter = num(r["clearance"]); side_depth = side->t; side_face = outer;
                }
                json a = first_hole(*side, y - side->y, z - side->z, diameter, side_depth, side_face);
                json b = first_hole(*panel, distance, panel->t / 2, num(r["diameter"]), num(r["depth"]), edge);
                if (recipe_id == "confirmat7") update(a, {{"countersink_diameter", r["countersink"]}, {"countersink_depth", r["countersink_depth"]}});
                for (auto [op, owner] : {std::pair<json*, Part*>{&a, side}, std::pair<json*, Part*>{&b, panel}}) {
                    update(*op, {{"recipe_id", recipe_id}, {"connection_tag", tag}, {"owner_id", panel->id}, {"purpose", r["name"]}, {"source", r["source"]}});
                    owner->operations.push_back(*op);
                }
                ++qty;
            }
        }
        assembly::accessory(*result, *panel, r["name"].get<std::string>(), qty, 0, {left, right}, json::array());
        update(result->hardware.at(result->hardware.size() - 1), {{"kind", "connector"}, {"connection_tag", tag}, {"recipe_id", recipe_id}, {"front", front_}, {"back", back_}, {"source", r["source"]}});
    }
    result->validate();
    return result;
}

void refresh_handles(Project& project, const std::string& part_id) {
    Part& p = find_part(project, part_id);
    std::vector<json*> handles;
    for (auto& h : project.hardware) if (py::get(h, "owner_id") == part_id && py::get(h, "kind") == "handle") handles.push_back(&h);
    if (handles.empty()) return;
    json kept = json::array();
    for (auto& o : p.operations) if (py::get(o, "recipe_id") != "handle") kept.push_back(o);
    p.operations = kept;
    for (json* handle : handles) {
        json& h = *handle;
        if (py::get(h, "u_anchor") == "right") h["u"] = p.u - (num(py::get(h, "design_u", p.u)) - num(h.at("u")));
        else if (py::get(h, "u_anchor") == "center") h["u"] = p.u / 2;
        if (py::get(h, "v_anchor") == "center") h["v"] = p.v / 2;
        h["design_u"] = p.u; h["design_v"] = p.v;
        json spacing_value = h.at("spacing"); double spacing = num(spacing_value);
        double u = num(h.at("u")), v = num(h.at("v"));
        std::vector<std::pair<double, double>> centers;
        if (!py::truthy(spacing_value)) centers = {{u, v}};
        else if (py::truthy(h.at("vertical"))) centers = {{u, v - spacing / 2}, {u, v + spacing / 2}};
        else centers = {{u - spacing / 2, v}, {u + spacing / 2, v}};
        for (auto [a, b] : centers) {
            json op = first_hole(p, a, b, 4.5, p.t, "bottom");
            update(op, {{"recipe_id", "handle"}, {"purpose", "Przelot pod śrubę M4 uchwytu"}});
            p.operations.push_back(op);
        }
    }
}

}  // namespace fittings
