// Atomic drag placement and pure, inexpensive fitting previews.
//
// Supported sourced recipes retain their existing limitations. A runner planning
// profile does not invent manufacturer clip drilling or notches. LED groove
// machining is available only on the CAD engine's broad +T/-T surfaces.
#include "drag_hardware.h"
#include "journal.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "app.h"
#include "assembly.h"
#include "configurator_accessories.h"
#include "corner_furniture.h"
#include "fitting_visuals.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "room_objects.h"
#include "runner_installation.h"
#include "runners_catalog.h"

namespace drag_hardware {

using core::Part;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::uid;
using core::world;
using core::inside;
using configurator_accessories::install_manual_led;
using configurator_accessories::led_proposal;
using configurator_accessories::refresh_manual_led;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> KINDS{{"hinge", "Zawiasy + prowadniki"}, {"runner", "Para prowadnic"}, {"led", "LED na powierzchni"},
                                                             {"handle", "Uchwyt"}, {"lamp", "Lampa"}, {"bulb", "Żarówka"}};

namespace {

double num(const json& value) { return value.get<double>(); }

std::string canonical_kind(const std::string& kind) {
    static const std::map<std::string, std::string> aliases{{"hinges", "hinge"}, {"slide", "runner"}, {"led_strip", "led"}, {"light", "bulb"}};
    auto found = aliases.find(kind); return found != aliases.end() ? found->second : kind;
}

// dict(zip(('u','v','w'), hit))
json zip_uvw(const json& hit) {
    json out = json::object(); const char* keys[] = {"u", "v", "w"};
    for (size_t i = 0; i < 3 && i < hit.size(); ++i) out[keys[i]] = hit[i];
    return out;
}

json point_json(const Point3& p) { return json::array({p[0], p[1], p[2]}); }

void update(json& target, const json& values) { for (auto& [k, v] : values.items()) target[k] = v; }

bool in(const std::set<std::string>& set, const json& value) { return value.is_string() && set.contains(value.get<std::string>()); }

}  // namespace

Part& _part(Project& project, const json& target) {
    for (auto& p : project.parts) if (json(p.id) == target) return p;
    throw ValueError("Wskaż istniejący element mebla.");
}

const Part& _part(const Project& project, const json& target) {
    for (auto& p : project.parts) if (json(p.id) == target) return p;
    throw ValueError("Wskaż istniejący element mebla.");
}

void _creator_changed(Project& project, const Part& owner, const NamedParts& old_children) {
    // Keep managed generated children stable after a legitimate runner rebuild.
    std::string owner_id = owner.id;
    std::vector<Part*> children; for (auto& p : project.parts) if (py::get(p.assembly, "generated_by") == owner_id) children.push_back(&p);
    std::map<std::string, std::string> mapping;
    for (Part* p : children) {
        const Part* old = nullptr;
        for (auto& [name, part] : old_children) if (name == p->name) { old = &part; break; }
        if (old == nullptr && p->plane == "XY") for (auto& [_, q] : old_children) if (q.plane == "XY") { old = &q; break; }
        if (old) {
            mapping[p->id] = old->id; p->configurator_source = old->configurator_source;
        }
    }
    if (!mapping.empty()) furniture_configurator::_remap(project, mapping);
    std::set<std::string> touched{owner_id}; for (Part* p : children) touched.insert(p->id);
    for (auto& p : project.parts) {
        if (!p.configurator_source.is_object() || !p.configurator_source.contains("manifest")) continue;
        for (auto& [key, entry] : p.configurator_source["manifest"].items()) {
            if (in(touched, entry.at("id"))) {
                const Part* q = nullptr; for (auto& candidate : project.parts) if (json(candidate.id) == entry["id"]) { q = &candidate; break; }
                if (q) entry["signature"] = furniture_configurator::_geometry(*q);
            }
        }
    }
}

ProjectPtr _hinge_install(ProjectPtr project, const json& target, const json& hit, const json& options, const json& ident) {
    Part* p = &_part(*project, target);
    if (p->role != "door" || !py::truthy(p->assembly)) throw ValueError("Przeciągnij zawiasy na drzwi połączone z korpusem.");
    if (py::truthy(py::get(p->assembly, "corner_source")) && !py::truthy(py::get(py::get(p->assembly, "config", json::object()), "corner_bay")))
        throw ValueError("Front skośny wymaga doboru rzeczywistego zawiasu; nie ma automatycznego wzorca montażu.");
    json cfg = assembly::recipe(py::get(p->assembly, "config", json::object())); json pattern = py::get(options, "recipe_id", "hinge45");
    if (pattern != "hinge45" && pattern != "hinge52" && pattern != "hinge_sensys52") throw ValueError("Wybierz obsługiwany wzór zawiasu nakładanego.");
    json spec = fittings::recipe(pattern); double count = finite(py::get(options, "count", cfg["hinge_count"]), 2, 8);
    if (std::trunc(count) != count) throw ValueError("Liczba zawiasów musi być całkowita.");
    update(cfg, {{"hinge_pattern", pattern}, {"hinge_count", (long long)count}, {"hinge_price", finite(py::get(options, "price", 0), 0, 100000)},
                 {"cup_diameter", spec.at("diameter")}, {"cup_depth", spec.at("depth")}});
    json saved_options = py::merged(options, {{"recipe_id", pattern}, {"count", (long long)count}, {"price", cfg["hinge_price"]}});
    json attachment = {{"attachment_id", ident}, {"manual", true}, {"config_auto", false}, {"attachment_kind", "hinge"}, {"attachment_options", saved_options}, {"attachment_hit", hit}};
    if (py::truthy(py::get(cfg, "corner_bay"))) {
        ProjectPtr result = corner_furniture::regenerate_corner_hinges(*project, p->id, pattern, (long long)count, cfg["hinge_price"], true, false);
        for (auto& h : result->hardware) {
            if (py::get(h, "owner_id") == p->id && py::get(h, "kind") == "hinge") update(h, attachment);
        }
        for (auto& q : result->parts) {
            for (auto& op : q.operations) {
                if (py::get(op, "corner_hinge_owner") == p->id) update(op, {{"attachment_id", ident}, {"manual", true}, {"config_auto", false}});
            }
        }
        return result;
    }
    if (py::truthy(py::get(options, "side"))) {
        if (options["side"] != "left" && options["side"] != "right") throw ValueError("Strona zawiasów: left/right.");
        cfg["hinge_side"] = options["side"];
    }
    std::set<std::string> purposes{"puszka zawiasu", "mocowanie puszki zawiasu", "prowadnik zawiasu"};
    for (auto& q : project->parts) {
        json ops = json::array(); for (auto& o : q.operations) if (!(py::get(o, "owner_id") == p->id && in(purposes, py::get(o, "purpose")))) ops.push_back(o);
        q.operations = ops;
    }
    { json kept = json::array(); for (auto& h : project->hardware) if (!(py::get(h, "owner_id") == p->id && py::get(h, "kind") == "hinge")) kept.push_back(h); project->hardware = kept; }
    size_t before = project->hardware.size(); assembly::Frame f = assembly::frame_for_part(*project, *p); assembly::hinges(*project, *p, f, cfg);
    p->assembly["config"] = cfg;
    for (size_t i = before; i < project->hardware.size(); ++i) update(project->hardware[i], attachment);
    for (auto& q : project->parts) {
        for (auto& op : q.operations) {
            if (py::get(op, "owner_id") == p->id && in(purposes, py::get(op, "purpose")))
                update(op, {{"attachment_id", ident}, {"manual", true}, {"config_auto", false}, {"source", spec.at("source")}});
        }
    }
    return project;
}

ProjectPtr _runner_install(ProjectPtr project, const json& target, const json& hit, const json& options, const json& ident) {
    Part& p = _part(*project, target);
    if (p.role != "drawer" || !py::truthy(p.assembly)) throw ValueError("Przeciągnij prowadnicę na front zarządzanej szuflady.");
    json profile = py::get(options, "profile_id", "hettich_ka5632");
    if (!profile.is_string() || !runners_catalog::PROFILES.contains(profile.get<std::string>())) throw ValueError("Wybierz profil prowadnicy z katalogu.");
    double length = finite(py::get(options, "length", 400), 50, 1500);
    double price = finite(py::get(options, "price", 0), 0, 100000);
    NamedParts old_children;
    for (auto& q : project->parts) {
        if (py::get(q.assembly, "generated_by") != p.id) continue;
        auto found = std::find_if(old_children.begin(), old_children.end(), [&](const auto& item) { return item.first == q.name; });
        if (found != old_children.end()) found->second = q; else old_children.emplace_back(q.name, q);
    }
    std::set<std::string> child_ids; for (auto& [_, q] : old_children) child_ids.insert(q.id);
    json retained = json::array(); for (auto& h : project->hardware) if (in(child_ids, py::get(h, "owner_id")) && py::truthy(py::get(h, "manual"))) retained.push_back(h);
    { json kept = json::array(); for (auto& h : project->hardware) if (!in(child_ids, py::get(h, "owner_id")) && !(py::get(h, "owner_id") == p.id && py::get(h, "kind") == "runner")) kept.push_back(h); project->hardware = kept; }
    std::string id = p.id;
    ProjectPtr result = runner_installation::apply_runner(*project, id, profile, length, py::get(options, "side_thickness", 16), py::get(options, "bottom_thickness", 6),
                                                          py::get(options, "recess", 12), py::get(options, "confirmed_min_depth"));
    Part& owner = _part(*result, id); _creator_changed(*result, owner, old_children);
    for (auto& h : retained) result->hardware.push_back(h);
    for (auto& h : result->hardware) {
        if (py::get(h, "owner_id") == id && py::get(h, "kind") == "runner")
            update(h, {{"price", price}, {"manual", true}, {"config_auto", false}, {"attachment_id", ident}, {"attachment_kind", "runner"}, {"attachment_options", options}, {"attachment_hit", hit}});
    }
    refresh_manual_led(*result);
    return result;
}

json _light_values(const Project& project, const std::string& kind_, const json& target, const json& hit_, const json& options) {
    json hit = py::truthy(hit_) ? hit_ : json::object(); std::string kind = kind_ == "light" ? "bulb" : kind_;
    json point;
    if (hit.is_object() && hit.contains("world")) point = hit["world"];
    else if (!target.is_null()) {
        const Part& p = _part(project, target);
        if (!hit.is_object()) hit = zip_uvw(hit);
        point = point_json(world(p, num(py::get(hit, "u", p.u / 2)), num(py::get(hit, "v", p.v / 2)), num(py::get(hit, "w", p.t))));
    } else if (hit.is_object() && hit.contains("x") && hit.contains("y") && hit.contains("z")) point = json::array({hit["x"], hit["y"], hit["z"]});
    else throw ValueError("Wskaż miejsce światła na scenie lub powierzchni mebla.");
    std::vector<double> values; for (auto& v : point) values.push_back(finite(v));
    if (values.size() != 3) throw ValueError("Pozycja światła wymaga XYZ.");
    bool bulb = kind == "bulb";
    double width = finite(py::get(options, "width", bulb ? 80 : 350), 1, 15000);
    double depth = finite(py::get(options, "depth", bulb ? 80 : 350), 1, 15000);
    double z = values[2] + finite(py::get(options, "offset", 0));
    double height = finite(py::get(options, "height", bulb ? 120 : 600), 1, 15000);
    return {{"x", values[0] - width / 2}, {"y", values[1] - depth / 2}, {"z", z},
            {"width", width}, {"depth", depth}, {"height", height},
            {"color", py::get(options, "color", bulb ? "#fff0bc" : "#c5b497")}, {"price", py::get(options, "price", 0)}};
}

ProjectPtr _light_install(const Project& project, const std::string& kind, const json& target, const json& hit, const json& options) {
    return room_objects::add_object(project, kind, _light_values(project, kind, target, hit, options));
}

ProjectPtr attach(const Project& project, const std::string& kind_, const json& target_id, const json& local_hit, const json& options_, const json& attachment_id) {
    // Return one validated candidate; failure leaves the source entirely intact.
    json options = py::truthy(options_) ? options_ : json::object(); json hit = py::truthy(local_hit) ? local_hit : json::object();
    std::string kind = canonical_kind(kind_);
    if (std::none_of(KINDS.begin(), KINDS.end(), [&](const auto& item) { return item.first == kind; })) throw ValueError("Nieznane okucie do przeciągania.");
    json ident = py::truthy(attachment_id) ? attachment_id : json(uid()); auto result = std::make_shared<Project>(project);
    if (kind == "led") result = install_manual_led(*result, target_id.is_string() ? target_id.get<std::string>() : std::string(), hit, options, ident, false);
    else if (kind == "hinge") result = _hinge_install(result, target_id, hit, options, ident);
    else if (kind == "runner") result = _runner_install(result, target_id, hit, options, ident);
    else if (kind == "handle") {
        const Part& p = _part(*result, target_id); std::string id = p.id;
        if (!hit.is_object()) hit = zip_uvw(hit);
        result = fittings::add_handle(*result, id, py::get(options, "spacing", 128), py::get(hit, "u", p.u / 2), py::get(hit, "v", p.v / 2),
                                      py::get(options, "vertical", p.role == "door"), py::get(options, "color", "#242628"), py::get(options, "projection", 28));
        json face = py::truthy(py::get(options, "face")) ? options["face"] : py::get(hit, "face", "bottom");
        if (face != "top" && face != "bottom") throw ValueError("Uchwyt wymaga powierzchni +T lub −T.");
        update(result->hardware.back(), {{"attachment_id", ident}, {"attachment_kind", "handle"}, {"price", finite(py::get(options, "price", 0), 0, 100000)}, {"config_auto", false}, {"mount_face", face}});
        for (auto& op : _part(*result, id).operations) {
            if (py::get(op, "recipe_id") == "handle") update(op, {{"face", face}, {"attachment_id", ident}, {"config_auto", false}, {"manual", true}});
        }
    } else result = _light_install(*result, kind, target_id, hit, options);
    if (kind == "hinge" || kind == "handle") refresh_manual_led(*result);
    result->validate(); return result;
}

json preview_attachment(const Project& project, const std::string& kind, const json& target_id, const json& local_hit, const json& options) {
    // Pure ghost proposal. An invalid target is a red proposal, not an exception.
    json data = {{"kind", kind}, {"target_id", target_id}, {"local_hit", local_hit}, {"options", py::truthy(options) ? options : json::object()},
                 {"valid", false}, {"message", ""}, {"world_faces", json::array()}, {"length", nullptr}};
    auto failed = [&](const std::string& message) { data["message"] = message.empty() ? "Nie można dopasować tego okucia." : message; };
    try {
        std::string canonical = canonical_kind(kind);
        if (canonical == "led") {
            Part p = _part(project, target_id); json h = led_proposal(p, local_hit, options);
            for (std::string key : {"price_per_m", "profile_price_per_m", "diffuser_price_per_m"}) finite(py::get(py::truthy(options) ? options : json::object(), key, 0), 0, 100000);
            json faces = json::array();
            for (auto& [face, color] : configurator_accessories::led_local_geometry(p, h)) { json f = json::array(); for (auto& point : face) f.push_back(point_json(world(p, point[0], point[1], point[2]))); faces.push_back(f); }
            data["valid"] = true; data["length"] = h["length"]; data["world_faces"] = faces;
            data["message"] = std::format("LED {:g} × {:g} mm · ", num(h["length"]), num(h["width"])) + (py::truthy(h["groove"]) ? "rowek" : "bez rowka");
            return data;
        }
        if (canonical == "lamp" || canonical == "bulb") {
            json obj = room_objects::make_object(canonical, _light_values(project, canonical, target_id, local_hit, py::truthy(options) ? options : json::object()));
            Project holder; holder.scene = room_objects::default_scene(); holder.scene["objects"] = json::array({obj});
            json faces = json::array(); for (auto& f : room_objects::room_world_faces(holder, true)) faces.push_back(f.at("world"));
            data["valid"] = true; data["message"] = "Światło — pozycja w scenie"; data["world_faces"] = faces;
            return data;
        }
        if (canonical == "hinge" || canonical == "runner") {
            const Part& part = _part(project, target_id);
            if (part.role != (canonical == "hinge" ? "door" : "drawer"))
                throw ValueError(canonical == "hinge" ? "Przeciągnij zawiasy na drzwi." : "Przeciągnij prowadnicę na front szuflady.");
        }
        ProjectPtr candidate = attach(project, kind, target_id, local_hit, options, "preview_attachment");
        if (canonical == "lamp" || canonical == "bulb") {
            json ident = candidate->scene["objects"].back()["id"]; json faces = json::array();
            for (auto& f : room_objects::room_world_faces(*candidate, true)) if (f.at("object_id") == ident) faces.push_back(f.at("world"));
            data["world_faces"] = faces; data["message"] = "Światło — pozycja w scenie";
        } else {
            std::string fitted_kind = canonical == "led" ? "led_strip" : canonical;
            const json* fitted = nullptr;
            for (auto& h : candidate->hardware) if (py::get(h, "owner_id") == target_id && py::get(h, "kind") == fitted_kind) fitted = &h;
            if (fitted) {
                const json& h = *fitted; data["length"] = py::get(h, "length");
                data["message"] = canonical == "led" ? std::format("LED {:g} × {:g} mm · ", num(h.at("length")), num(h.at("width"))) + (py::truthy(py::get(h, "groove")) ? "rowek" : "bez rowka") : h.at("name").get<std::string>();
            }
            json kept = json::array(); for (auto& h : candidate->hardware) if (py::get(h, "attachment_id") == "preview_attachment") kept.push_back(h);
            candidate->hardware = kept;
            json faces = json::array();
            for (auto& [owner, face, color] : fitting_visuals::fitting_faces(*candidate, std::set<std::string>{fitted_kind}))
                if (json(owner) == target_id) { json f = json::array(); for (auto& point : face) f.push_back(point_json(point)); faces.push_back(f); }
            data["world_faces"] = faces;
        }
        data["valid"] = true;
    } catch (const ValueError& error) { failed(error.what()); }
    catch (const py::TypeError& error) { failed(error.what()); }        // TypeError, KeyError z dict
    catch (const std::out_of_range& error) { failed(error.what()); }    // KeyError, IndexError
    catch (const py::StopIteration& error) { failed(error.what()); }
    catch (const std::overflow_error& error) { failed(error.what()); }  // OverflowError
    return data;
}

Project& refresh_manual_attachments(Project& project, const std::optional<std::set<std::string>>& changed_ids) {
    // Refresh a caller-owned candidate after assembly/configurator rebuilding.
    json records = json::array();
    for (auto& h : project.hardware)
        if ((py::get(h, "attachment_kind") == "hinge" || py::get(h, "attachment_kind") == "runner") && (!changed_ids || in(*changed_ids, h.at("owner_id")))) records.push_back(h);
    ProjectPtr self(ProjectPtr(), &project);  // niewłaścicielski wskaźnik do projektu wywołującego
    for (auto& h : records) {
        json kind = h["attachment_kind"];
        ProjectPtr result = kind == "hinge" ? _hinge_install(self, h.at("owner_id"), py::get(h, "attachment_hit", json::object()), py::get(h, "attachment_options", json::object()), h.at("attachment_id"))
                                            : _runner_install(self, h.at("owner_id"), py::get(h, "attachment_hit", json::object()), py::get(h, "attachment_options", json::object()), h.at("attachment_id"));
        if (result.get() != &project) { project.parts = result->parts; project.hardware = result->hardware; }
    }
    json handles = json::array();
    for (auto& h : project.hardware) if (py::get(h, "attachment_kind") == "handle" && (!changed_ids || in(*changed_ids, h.at("owner_id")))) handles.push_back(h);
    for (auto& h : handles) {
        json kept = json::array();
        for (auto& item : project.hardware) if (!(py::get(item, "owner_id") == h.at("owner_id") && py::get(item, "kind") == "handle") || py::get(item, "attachment_id") == h.at("attachment_id")) kept.push_back(item);
        project.hardware = kept;
        fittings::refresh_handles(project, h["owner_id"].get<std::string>());
        for (auto& op : _part(project, h["owner_id"]).operations) {
            if (py::get(op, "recipe_id") == "handle") update(op, {{"face", py::get(h, "mount_face", "bottom")}, {"attachment_id", h.at("attachment_id")}, {"config_auto", false}, {"manual", true}});
        }
    }
    refresh_manual_led(project); return project;
}

HardwareDragController::HardwareDragController(App* app, HardwarePanel* panel) : app(app), panel(panel) {}

void HardwareDragController::_note(const std::string& text) {
    if (panel != nullptr) panel->note.set(text);
    app->status.set(text);
}

std::string HardwareDragController::begin(const std::string& kind_, const json& options_, tk::Event* event) {
    if (!app->flush_parameter_drafts()) {
        _note("Popraw parametry mebla przed montażem wyposażenia."); return "break";
    }
    cancel(); kind = kind_; options = py::truthy(options_) ? options_ : json::object();
    journal::event(app, "okucia", "wybrano do montażu: " + kind_ + (options.is_object() && !options.empty() ? " · opcje " + options.dump() : std::string()) + (event != nullptr ? " (przeciąganie z panelu)" : " (kliknięcie kafelka)"));
    project_generation = app->project_generation;
    start = event != nullptr ? std::optional<std::pair<double, double>>({(double)event->x_root, (double)event->y_root}) : std::nullopt;
    _note("Przeciągnij na mebel albo kliknij wybrany element. Esc anuluje.");
    return "break";
}

std::pair<double, double> HardwareDragController::_position(const tk::Event& event) {
    auto* canvas = app->canvas;
    return {(double)event.x_root - canvas->winfo_rootx(), (double)event.y_root - canvas->winfo_rooty()};
}

std::optional<json> HardwareDragController::_target(double x, double y) {
    if (!(0 <= x && x < app->canvas->winfo_width()) || !(0 <= y && y < app->canvas->winfo_height())) return std::nullopt;
    if (app->pick_attachment_target) return app->pick_attachment_target(x, y);
    for (auto it = app->drawn.rbegin(); it != app->drawn.rend(); ++it) {
        const json& f = *it;
        std::vector<core::Point> points; for (auto& pt : f.at("points")) points.push_back({num(pt[0]), num(pt[1])});
        bool owned = std::any_of(app->project->parts.begin(), app->project->parts.end(), [&](const Part& p) { return json(p.id) == f.at("id"); });
        if (inside({x, y}, points) && owned) {
            const Part& p = _part(*app->project, f["id"]);
            return json{{"target_id", p.id}, {"local_hit", {{"u", p.u / 2}, {"v", p.v / 2}, {"w", p.t}, {"face", "top"}}}};
        }
    }
    return std::nullopt;
}

std::optional<std::string> HardwareDragController::move(const tk::Event& event, bool force) {
    if (!active()) return std::nullopt;
    if (app->project_generation != project_generation) {
        cancel(); _note("Wczytano inny projekt. Wybierz kafelek ponownie."); return "break";
    }
    if (start && std::hypot(start->first - event.x_root, start->second - event.y_root) > 5) dragging = true;
    auto [x, y] = _position(event); pointer = {x, y};
    double now = py::monotonic();
    if (!force && now - _last_move < .033) return "break";
    _last_move = now;
    bool any_open = false; for (auto& [_, v] : app->open_amounts) if (v > 1e-7) any_open = true;
    if (app->explode.get() || any_open) {
        proposal = {{"valid", false}, {"message", "Zamknij mechanizmy i wyłącz widok rozstrzelony przed montażem."}, {"world_faces", json::array()}};
        draw_ghost(); return "break";
    }
    std::optional<json> target = _target(x, y);
    if (target) {
        if (_cache_project != app->project.get()) {
            _cache.clear(); _cache_project = app->project.get();
        }
        json hit = py::get(*target, "local_hit");
        if (hit.is_object() && py::truthy(py::get(*target, "face"))) hit["face"] = (*target)["face"];
        json quantized = hit;
        if (quantized.is_object()) {
            for (auto& [k, v] : quantized.items()) {
                if (v.is_number() || v.is_boolean()) v = (long long)py::round_half_even(num(v.is_boolean() ? json(v.get<bool>() ? 1 : 0) : v) / 2) * 2;
            }
        }
        bool static_kind = *kind == "hinge" || *kind == "hinges" || *kind == "runner" || *kind == "slide";
        std::string key = py::dumps(json::array({*kind, py::get(*target, "target_id"), py::dumps(options, true, -1, true), static_kind ? "" : py::dumps(quantized, true, -1, true)}));
        auto cached = std::find_if(_cache.begin(), _cache.end(), [&](const auto& item) { return item.first == key; });
        if (force || cached == _cache.end()) {
            proposal = preview_attachment(*app->project, *kind, py::get(*target, "target_id"), force ? hit : quantized, options);
            if (!force) {
                if (cached != _cache.end()) cached->second = proposal; else _cache.emplace_back(key, proposal);
            }
            while (_cache.size() > 64) _cache.pop_front();
        } else proposal = cached->second;
        proposal["local_hit"] = hit;
    } else proposal = {{"valid", false}, {"message", "Wskaż powierzchnię mebla lub miejsce światła."}, {"world_faces", json::array()}};
    draw_ghost(); return "break";
}

void HardwareDragController::draw_ghost() {
    auto* canvas = app->canvas; canvas->delete_("hardware_ghost");
    if (!active() || proposal.is_null()) return;
    std::string color = py::truthy(proposal["valid"]) ? "#23986a" : "#c6574b"; int w = canvas->winfo_width(), h = canvas->winfo_height();
    for (auto& face : proposal["world_faces"]) {
        std::vector<double> coords;
        for (auto& point : face) { Point3 p = app->camera.project({num(point[0]), num(point[1]), num(point[2])}, w, h); coords.push_back(p[0]); coords.push_back(p[1]); }
        canvas->create_polygon(coords, {{"fill", ""}, {"outline", color}, {"width", 2}, {"tags", "hardware_ghost"}});
    }
    if (pointer) {
        auto [x, y] = *pointer; std::string message = proposal["message"]; std::string label;
        // proposal['message'][:130] (znaki, nie bajty)
        size_t chars = 0; for (size_t i = 0; i < message.size(); ++i) { if (((unsigned char)message[i] & 0xC0) != 0x80 && ++chars > 130) break; label += message[i]; }
        int item = canvas->create_text(std::min(w - 8.0, std::max(8.0, x + 12)), std::min(h - 10.0, std::max(10.0, y + 18)), {{"text", label}, {"anchor", "nw"}, {"fill", color}, {"width", 300}, {"tags", "hardware_ghost"}});
        auto bb = canvas->bbox(item);
        if (bb) {
            int bg = canvas->create_rectangle((*bb)[0] - 5, (*bb)[1] - 4, (*bb)[2] + 5, (*bb)[3] + 4, {{"fill", "#ffffff"}, {"outline", color}, {"tags", "hardware_ghost"}}); canvas->tag_lower(bg, item);
        }
    }
}

std::optional<std::string> HardwareDragController::target_click(const tk::Event& event) {
    if (!active()) return std::nullopt;
    move(event, true);
    if (!active()) return "break";
    if (proposal.is_null() || !py::truthy(proposal["valid"])) {
        std::string why = !proposal.is_null() ? proposal["message"].get<std::string>() : "Wskaż element.";
        journal::event(app, "okucia", "montaż " + *kind + " odrzucony: " + why);
        _note(why); return "break";
    }
    auto failed = [&](const std::exception& error) { journal::event(app, "okucia", std::string("montaż nie powiódł się: ") + error.what()); _note(error.what()); };
    try {
        json p = proposal;
        journal::event(app, "okucia", "montaż " + *kind + " na części " + py::get(p, "target_id").dump() + " · miejsce " + py::get(p, "local_hit").dump());
        ProjectPtr candidate = attach(*app->project, *kind, py::get(p, "target_id"), py::get(p, "local_hit"), options);
        app->snapshot(); app->project = candidate;
        json target = py::get(p, "target_id"); std::string message = p["message"]; std::string commit_kind = *kind; kind.reset(); cancel();  // kind.reset() — udany montaż nie jest „anulowaniem”
        if (commit_kind == "lamp" || commit_kind == "bulb" || commit_kind == "light") {
            app->selected.reset(); app->selected_scene = candidate->scene["objects"].back()["id"].get<std::string>();
            app->edit_mode.set("layout");
        } else if (!target.is_null()) app->selected = target.get<std::string>();
        journal::event(app, "okucia", "dodano " + commit_kind + ": " + message);
        app->refresh(); app->draw(); _note("Dodano: " + message);
    } catch (const ValueError& error) { failed(error); } catch (const py::TypeError& error) { failed(error); } catch (const std::out_of_range& error) { failed(error); }
    return "break";
}

std::optional<std::string> HardwareDragController::end(const tk::Event& event) {
    if (!active()) return std::nullopt;
    auto [x, y] = _position(event);
    if (dragging) return target_click(event);
    // Releasing a clicked tray tile keeps the tool armed for a canvas click.
    if (!start && 0 <= x && x < app->canvas->winfo_width() && 0 <= y && y < app->canvas->winfo_height()) return target_click(event);
    return "break";
}

std::string HardwareDragController::cancel(tk::Event*) {
    if (kind) journal::event(app, "okucia", "anulowano montaż: " + *kind);
    kind.reset(); options = json::object(); proposal = nullptr; pointer.reset(); start.reset(); dragging = false;
    _last_move = 0;
    _cache.clear(); _cache_project = nullptr;
    if (app->canvas) app->canvas->delete_("hardware_ghost");
    return "break";
}

}  // namespace drag_hardware
