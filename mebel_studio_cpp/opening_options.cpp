// Handle alternatives; sourced ejector bores and editable finger-pull machining.
#include "opening_options.h"

#include <algorithm>

#include "assembly.h"
#include "core.h"
#include "features.h"
#include "fitting_visuals.h"

namespace opening_options {

using core::Part;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using fitting_visuals::Faces;
using py::ValueError;

const std::string BLUM = "https://d2.blum.com/services/BEC003/tip0001-ep-234_ep_dok_bau_%24sen_%24aof_%24v6.pdf";
const std::string SALICE = "https://www.salice.com/downloads/2853/2143/Salice-Magnetic-Push-applications-ENG.pdf";

const json OPTIONS = json{
    {"tip_short", {{"label", "Blum TIP-ON 956.1004 — magnes, krótki"}, {"diameter", 10}, {"depth", 50}, {"gap", 2.6}, {"hinge_spring", "unsprung"}, {"source", BLUM},
                   {"note", "Zawiasy bez sprężyny; front nakładany do ok. 1300 mm; płytka przyklejana. Nie dobiera numeru zawiasu."}}},
    {"tip_long", {{"label", "Blum TIP-ON 956A1004 — magnes, długi"}, {"diameter", 10}, {"depth", 76}, {"gap", 2.6}, {"hinge_spring", "unsprung"}, {"source", BLUM},
                  {"note", "Zawiasy bez sprężyny; wysokie fronty; płytka przyklejana. Program obsługuje fronty nakładane."}}},
    {"tip_bumper", {{"label", "Blum TIP-ON 956A1006 — odbojnik"}, {"diameter", 10}, {"depth", 76}, {"gap", 3.2}, {"hinge_spring", "sprung"}, {"source", BLUM},
                    {"note", "Wariant do zawiasów ze sprężyną; dobór siły wymaga sprawdzenia. Bez płytki magnesu."}}},
    {"salice_push", {{"label", "Salice DPM — Magnetic Push"}, {"diameter", 10}, {"depth", 40}, {"gap", 3}, {"source", SALICE},
                     {"note", "Otwór Ø10 o głębokości minimum 40 mm. Płytka DP38XX91 przyklejana. Luz 3 mm jest ustawieniem roboczym."}}},
    {"edge_pull", {{"label", "Uchwyt krawędziowy / profil — własny"}, {"source", ""},
                   {"note", "Własny model zakupowy; długość i wysunięcie edytowalne. Brak automatycznych wierceń."}}},
    {"finger_pocket", {{"label", "Uchwyt frezowany — kieszeń podłużna"}, {"source", ""},
                       {"note", "Własna kieszeń na zewnętrznej powierzchni frontu. Wymiary i głębokość dobierz do materiału."}}},
    {"finger_slot", {{"label", "Uchwyt wycięty — otwór podłużny"}, {"source", ""}, {"note", "Własne wycięcie przelotowe z końcami półkolistymi."}}},
    {"none", {{"label", "Gładki front — usuń uchwyt"}, {"source", ""}, {"note", "Usuwa uchwyt i poprzednią obróbkę otwierania."}}}};

namespace {

Part& find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return p;
    throw py::StopIteration();  // next(...) bez wartości domyślnej
}

bool one_of(const json& value, std::initializer_list<const char*> items) {
    for (const char* item : items) if (value == item) return true;
    return false;
}

// _spring_state(part, spec, requested=None)
json spring_state(const Part& part, const json& spec, const json& requested = nullptr) {
    json cfg = py::get(part.assembly, "config", json::object());
    json state = !requested.is_null() ? requested : py::get(cfg, "hinge_spring", "unknown");
    if (!one_of(state, {"unknown", "sprung", "unsprung"})) throw ValueError("Stan zawiasu: unknown, sprung albo unsprung.");
    // The sourced Sensys screw-on recipe is an integrated closing hinge, not
    // the generic 52 mm drilling pattern of an unsprung alternative.
    if (py::get(cfg, "hinge_pattern") == "hinge_sensys52") {
        if (state == "unsprung") throw ValueError("Wybrany Sensys 9071205 ma sprężynę zamykającą. Zmień zawias, zanim dobierzesz magnetyczny TIP-ON.");
        state = "sprung";
    }
    json required = py::get(spec, "hinge_spring");
    if (required == "unsprung" && state != "unsprung")
        throw ValueError("Magnetyczny TIP-ON wymaga zawiasów bez sprężyny. Wybierz właściwy zawias i potwierdź „Bez sprężyny” w konfiguracji otwierania.");
    if (required == "sprung" && state == "unsprung")
        throw ValueError("TIP-ON z odbojnikiem jest przeznaczony do zawiasów ze sprężyną. Dla zawiasu bez sprężyny wybierz wariant magnetyczny.");
    return state;
}

}  // namespace

void clear(Project& project, const std::string& part_id) {
    for (auto& p : project.parts) {
        json kept = json::array();
        for (const auto& o : p.operations)
            if (!(py::get(o, "opening_owner") == part_id) && !(p.id == part_id && py::get(o, "recipe_id") == "handle")) kept.push_back(o);
        p.operations = kept;
        if (p.id == part_id) {
            json cuts = json::array();
            for (const auto& c : p.machining) if (!(py::get(c, "opening_owner") == part_id)) cuts.push_back(c);
            p.machining = cuts;
        }
    }
    json hardware = json::array();
    for (const auto& h : project.hardware)
        if (!(py::get(h, "owner_id") == part_id && one_of(py::get(h, "kind"), {"handle", "opening"}))) hardware.push_back(h);
    project.hardware = hardware;
}

ProjectPtr install(const Project& project, const std::string& part_id, const std::string& option, const InstallOptions& o) {
    auto result = std::make_shared<Project>(project);
    Part* p = &find_part(*result, part_id);
    if (py::truthy(py::get(p->assembly, "generated_by"))) throw ValueError("Wybierz front, a nie część skrzynki.");
    if (!OPTIONS.contains(option)) throw ValueError("Nieznany sposób otwierania.");
    if (p->plane != "XZ" && option != "none" && option != "finger_pocket" && option != "finger_slot")
        throw ValueError("Wybierz pionowy front w płaszczyźnie XZ.");
    clear(*result, part_id);
    if (option == "none") { result->validate(); return result; }
    const json& spec = OPTIONS.at(option);
    if (spec.contains("diameter")) {
        if (p->role != "door" || !py::truthy(p->assembly)) throw ValueError("Wypychacz wymaga drzwiczek połączonych z korpusem.");
        json state = spring_state(*p, spec, o.hinge_spring);
        json cfg = p->assembly.at("config");
        cfg["opening_gap"] = spec["gap"]; cfg["hinge_spring"] = state;
        std::string group = p->group;
        auto [attached, np] = assembly::attach(*result, *p, group, "door", cfg);
        result = attached; p = np;
        if (option == "tip_short" && p->v > 1300) throw ValueError("Krótki TIP-ON: front do ok. 1300 mm; wybierz długi wariant.");
    }
    double length = finite(o.length, 10, 2000), width = finite(o.width, 4, 200), depth = finite(o.depth, .1, 200);
    double level = finite(!o.level.is_null() ? o.level : json(p->v / 2), 0, p->v);
    double center_u = finite(!o.center_u.is_null() ? o.center_u : json(p->u / 2), 0, p->u);
    double angle = finite(o.angle, -360, 360);
    if (o.profile_edge != "top" && o.profile_edge != "bottom" && o.profile_edge != "left" && o.profile_edge != "right")
        throw ValueError("Wybierz górną, dolną, lewą albo prawą krawędź profilu.");
    result->hardware.push_back(json{{"kind", "opening"}, {"manual", true}, {"owner_id", part_id}, {"name", spec["label"]}, {"qty", 1}, {"price", 0}, {"option", option},
                                    {"length", length}, {"width", width}, {"depth", depth}, {"level", level}, {"level_ratio", level / p->v},
                                    {"center_u", center_u}, {"u_ratio", center_u / p->u}, {"angle", angle}, {"profile_edge", o.profile_edge},
                                    {"color", o.color}, {"source", spec["source"]}, {"note", spec["note"]}, {"requires_review", true}});
    refresh_openings(*result, part_id);
    result->validate();
    return result;
}

ProjectPtr install(const Project& project, const std::string& part_id, const std::string& option, const json& length, const json& width, const json& depth,
                   const json& hinge_spring) {
    InstallOptions o;
    o.length = length; o.width = width; o.depth = depth; o.hinge_spring = hinge_spring;
    return install(project, part_id, option, o);
}

ProjectPtr install(const Project& project, const std::string& part_id, const std::string& option, const json& length, const json& width, const json& depth,
                   const json& level, const json& center_u, const json& angle, const json& hinge_spring, const std::string& profile_edge) {
    InstallOptions o;
    o.length = length; o.width = width; o.depth = depth; o.level = level; o.center_u = center_u; o.angle = angle; o.hinge_spring = hinge_spring; o.profile_edge = profile_edge;
    return install(project, part_id, option, o);
}

ProjectPtr install(const Project& project, const std::string& part_id, const std::string& option, const json& kwargs) {
    InstallOptions o;
    for (auto& [key, value] : kwargs.items()) {
        if (key == "length") o.length = value;
        else if (key == "width") o.width = value;
        else if (key == "depth") o.depth = value;
        else if (key == "level") o.level = value;
        else if (key == "color") o.color = value;
        else if (key == "hinge_spring") o.hinge_spring = value;
        else if (key == "center_u") o.center_u = value;
        else if (key == "angle") o.angle = value;
        else if (key == "profile_edge") o.profile_edge = value.get<std::string>();
        else py::type_error("install() got an unexpected keyword argument '" + key + "'");
    }
    return install(project, part_id, option, o);
}

void refresh_openings(Project& project, const std::string& part_id) {
    Part* p = nullptr;
    for (auto& q : project.parts) if (q.id == part_id) { p = &q; break; }
    if (p == nullptr) return;
    std::vector<json*> hs;
    for (auto& h : project.hardware) if (py::get(h, "owner_id") == part_id && py::get(h, "kind") == "opening") hs.push_back(&h);
    if (hs.empty()) return;
    for (auto& q : project.parts) {
        json kept = json::array();
        for (const auto& o : q.operations) if (!(py::get(o, "opening_owner") == part_id)) kept.push_back(o);
        q.operations = kept;
    }
    {
        json cuts = json::array();
        for (const auto& c : p->machining) if (!(py::get(c, "opening_owner") == part_id)) cuts.push_back(c);
        p->machining = cuts;
    }
    for (json* hp : hs) {
        json& h = *hp;
        h["level"] = py::get(h, "level_ratio", .5).get<double>() * p->v;
        std::string option = h.at("option");
        const json& spec = OPTIONS.at(option);
        h["center_u"] = py::get(h, "u_ratio", .5).get<double>() * p->u;
        if (spec.contains("diameter")) {
            json state = spring_state(*p, spec);
            h["hinge_spring"] = state;
            h["compatibility_status"] = state == "unknown" ? "Zawias wymaga potwierdzenia sprężyny i sprawdzenia siły wypychania."
                                                           : "Rodzaj sprężyny zgodny; dobierz konkretny artykuł zawiasu i siłę wypychania.";
            assembly::Frame f = assembly::frame_for_part(project, *p);
            auto [left, right] = assembly::require_sides(project, f);
            Part* side = py::get(py::get(p->assembly, "config", json::object()), "hinge_side", "left") == "left" ? right : left;
            double level = p->z + h["level"].get<double>() - side->z;
            json op = features::hole_array(*side, level, side->t / 2, spec["diameter"].get<double>(), spec["depth"].get<double>(), 1, 1, 32, 32, "u0").at(0);
            op["opening_owner"] = part_id; op["owner_id"] = part_id; op["purpose"] = spec["label"]; op["source"] = spec["source"];
            side->operations.push_back(op);
            Point3 origin = core::local_world(*side, 0, level, side->t / 2);
            h["anchors"] = json::array({side->id}); h["origin"] = json::array({origin[0], origin[1], origin[2]});
            h["length"] = spec["depth"]; h["width"] = spec["diameter"];
        } else if (option == "finger_pocket" || option == "finger_slot") {
            bool slot = option == "finger_slot";
            json cut = json{{"type", slot ? "cutout" : "pocket"}, {"shape", "slot"}, {"face", "bottom"}, {"u", h["center_u"]}, {"v", h["level"]},
                            {"length", h["length"]}, {"width", h["width"]}, {"depth", slot ? json(p->t) : h["depth"]}, {"angle", py::get(h, "angle", 0)},
                            {"opening_owner", part_id}, {"purpose", spec["label"]}};
            p->machining.push_back(cut);
        }
    }
}

Faces opening_faces(const Project&, const Part& p, const json& h) {
    using fitting_visuals::box;
    using fitting_visuals::rod;
    if (h.at("option") == "edge_pull") {
        std::string edge = py::get(h, "profile_edge", "top");
        bool vertical = edge == "left" || edge == "right";
        double depth = h["depth"];
        double length = std::min(h["length"].get<double>(), vertical ? p.v : p.u);
        double limit = vertical ? p.v : p.u;
        Faces local = box((limit - length) / 2, -3, -depth, length, 3, depth);
        for (auto& face : box((limit - length) / 2, -3, -depth, length, 18, 3)) local.push_back(face);
        for (auto& face : local)
            for (auto& pt : face) {
                double u = pt[0], v = pt[1], t = pt[2];
                if (edge == "top") pt = {u, p.v + v, t};
                else if (edge == "bottom") pt = {u, -v, t};
                else if (edge == "left") pt = {v, u, t};
                else pt = {p.u - v, u, t};
            }
        Faces out;
        for (const auto& face : local) {
            std::vector<Point3> world_face;
            for (const auto& pt : face) world_face.push_back(core::world(p, pt[0], pt[1], pt[2]));
            out.push_back(world_face);
        }
        return out;
    }
    if (h.contains("origin")) {
        double x = h["origin"][0], y = h["origin"][1], z = h["origin"][2];
        Faces out;
        for (const auto& face : rod({x, y - 1, z}, {x, y + h["length"].get<double>(), z}, h["width"].get<double>() / 2)) {
            std::vector<Point3> world_face;
            for (const auto& pt : face) world_face.push_back(core::layout_point(p, pt));
            out.push_back(world_face);
        }
        return out;
    }
    return {};
}

}  // namespace opening_options
