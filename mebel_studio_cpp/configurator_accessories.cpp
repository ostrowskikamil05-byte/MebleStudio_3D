// Transactional front milling and purchased cabinet accessories, in millimetres.
//
// The defaults are editable workshop dimensions, not manufacturer recipes. Legs
// are purchased hardware, never fictitious panels in the cutting list. A plinth
// is a genuine panel and is created by furniture_configurator, not this module.
#include "configurator_accessories.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <set>

#include "features.h"
#include "fitting_visuals.h"
#include "opening_options.h"

namespace configurator_accessories {

using core::Part;
using core::Project;
using core::ProjectPtr;
using core::Point3;
using core::finite;
using core::local_world;
using core::inverse_layout_point;
using core::inside;
using core::uid;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> FRONT_STYLES{
    {"smooth", "Gładki"}, {"frame", "Ramka frezowana"},
    {"fluted", "Ryflowany"}, {"finger", "Uchwyt frezowany"}};

namespace {

double num(const json& value) { return value.get<double>(); }

Part* find_part(Project& project, const std::string& id) {
    for (auto& p : project.parts) if (p.id == id) return &p;
    return nullptr;
}

bool is_auto_front(const json& item) {
    return py::truthy(py::get(item, "config_auto")) && py::get(item, "configure_feature") == "front_style";
}

// [dict(face='top', u=u, v=v, diameter=d, depth=depth) for u, v, d, depth in part.holes]
json hole_operations(const Part& part) {
    json holes = json::array();
    for (auto& hole : part.holes) {
        if (hole.size() != 4) throw ValueError(hole.size() < 4 ? std::format("not enough values to unpack (expected 4, got {})", hole.size()) : "too many values to unpack (expected 4)");
        holes.push_back({{"face", "top"}, {"u", hole[0]}, {"v", hole[1]}, {"diameter", hole[2]}, {"depth", hole[3]}});
    }
    return holes;
}

json point_json(const Point3& p) { return json::array({p[0], p[1], p[2]}); }
json point_json(const std::array<int, 3>& p) { return json::array({p[0], p[1], p[2]}); }

}  // namespace

long long _integer(const json& value, double low, double high, const std::string& label) {
    double number = finite(value, low, high);
    if (std::trunc(number) != number) {
        throw ValueError(label + ": wpisz liczbę całkowitą.");
    }
    return (long long)number;
}

std::string _color(const json& value) {
    if (!value.is_string() || py::len(value.get<std::string>()) != 7 || value.get<std::string>()[0] != '#') {
        throw ValueError("Kolor akcesorium: #RRGGBB.");
    }
    try {
        py::int16(value.get<std::string>().substr(1));
    } catch (const ValueError&) {
        throw ValueError("Kolor akcesorium: #RRGGBB.");
    }
    return py::lower(value.get<std::string>());
}

json normalize_front_style(const json& style_, const json& cfg_) {
    // Validate requested dimensions; part-specific safe limits are applied later.
    json cfg = py::truthy(cfg_) ? cfg_ : json::object();
    json style = style_ == "plain" || style_ == "slab" ? json("smooth") : style_ == "milled" ? json("frame") : style_;
    if (!style.is_string() || std::none_of(FRONT_STYLES.begin(), FRONT_STYLES.end(), [&](const auto& item) { return item.first == style.get<std::string>(); })) {
        throw ValueError("Styl frontu: smooth, frame, fluted albo finger.");
    }
    return {{"style", style},
            {"front_border", finite(py::get(cfg, "front_border", 60), 5, 250)},
            {"front_depth", finite(py::get(cfg, "front_depth", 3), .1, 30)},
            {"front_radius", finite(py::get(cfg, "front_radius", 6), 0, 80)},
            {"flute_width", finite(py::get(cfg, "flute_width", 8), 2, 40)},
            {"flute_spacing", finite(py::get(cfg, "flute_spacing", 20), 3, 100)},
            {"flute_count", _integer(py::get(cfg, "flute_count", 0), 0, MAX_FLUTES, "Liczba rowków")},
            {"finger_length", finite(py::get(cfg, "finger_length", 120), 20, 500)},
            {"finger_width", finite(py::get(cfg, "finger_width", 24), 6, 80)},
            {"finger_depth", finite(py::get(cfg, "finger_depth", 6), .1, 30)}};
}

double _safe_depth(double thickness, double requested) {
    // A decorative recess never consumes more than 45% of a front and always
    // leaves at least 6 mm. This is a geometry guard, not a strength approval.
    double limit = std::min(thickness * .45, thickness - 6);
    if (limit < .1) {
        throw ValueError("Frezowany element musi być grubszy niż 6 mm.");
    }
    return std::min(requested, limit);
}

json _cut(const std::string& purpose, const std::string& shape, double u, double v, double length, double width, double depth, const json& angle, const std::string& feature, const json& extra) {
    json cut = {{"type", feature == "front_style" ? "pocket" : "groove"},
                {"shape", shape}, {"face", "bottom"}, {"u", u}, {"v", v}, {"length", length},
                {"width", width}, {"depth", depth}, {"angle", angle}, {"config_auto", true},
                {"configure_feature", feature}, {"purpose", purpose}};
    return py::merged(cut, extra);
}

bool _overlaps_drill(const json& cut, double u, double v, double radius) {
    // Circle/slot/rectangle overlap in local UV, independent of panel pose.
    double du = u - num(cut["u"]), dv = v - num(cut["v"]);
    double angle = -num(py::get(cut, "angle", 0)) * (std::numbers::pi / 180);
    double a = du * std::cos(angle) - dv * std::sin(angle), b = du * std::sin(angle) + dv * std::cos(angle);
    if (cut["shape"] == "circle") {
        return std::hypot(a, b) <= num(cut["width"]) / 2 + radius;
    }
    if (cut["shape"] == "slot") {
        return std::hypot(std::max(std::abs(a) - (num(cut["length"]) - num(cut["width"])) / 2, 0.0), b) <= num(cut["width"]) / 2 + radius;
    }
    return std::hypot(std::max(std::abs(a) - num(cut["length"]) / 2, 0.0), std::max(std::abs(b) - num(cut["width"]) / 2, 0.0)) <= radius;
}

std::optional<double> _guard_opposed_drills(const Part& part, const std::vector<json*>& cuts) {
    // Leave a 1 mm web where automatic recesses cross existing blind drilling.
    //
    // This is a geometric collision guard; structural suitability still depends
    // on the actual panel and selected fittings. A pattern uses one uniform depth.
    if (cuts.empty()) {
        return std::nullopt;
    }
    json holes = hole_operations(part);
    for (auto& o : part.operations) if (py::get(o, "face") == "top" || py::get(o, "face") == "bottom") holes.push_back(o);
    double depth = num((*cuts[0])["depth"]);
    for (json* c : cuts) depth = std::min(depth, num((*c)["depth"]));
    for (json* cut : cuts) {
        std::string opposite = (*cut)["face"] == "bottom" ? "top" : "bottom";
        for (auto& op : holes) {
            if (op["face"] == opposite && _overlaps_drill(*cut, num(op["u"]), num(op["v"]), num(op["diameter"]) / 2)) {
                depth = std::min(depth, part.t - num(op["depth"]) - 1);
            }
        }
    }
    if (depth < .1) {
        throw ValueError("Frezowanie koliduje z wierceniem od przeciwnej powierzchni. Zmień wzór lub położenie otworu.");
    }
    for (json* cut : cuts) {
        (*cut)["depth"] = depth;
    }
    return depth;
}

void _clear_front(Project& project, Part& part) {
    json machining = json::array();
    for (auto& cut : part.machining) if (!is_auto_front(cut)) machining.push_back(cut);
    part.machining = machining;
    json hardware = json::array();
    for (auto& h : project.hardware) if (!(py::get(h, "owner_id") == part.id && is_auto_front(h))) hardware.push_back(h);
    project.hardware = hardware;
}

Part& apply_front_to_part(Project& project, Part& part, const json& style_, const json& config) {
    // Batch helper: mutate a caller-owned project copy, without global validation.
    //
    // Callers must validate the finished project before committing it. The public
    // apply_front_style below provides copying and validation for a single front.
    json cfg = normalize_front_style(style_, config);
    if (py::truthy(py::get(part.assembly, "generated_by"))) {
        throw ValueError("Wybierz front, a nie ścianę skrzynki szuflady.");
    }
    _clear_front(project, part);
    std::string style = cfg["style"];
    json actual = cfg;
    if (style != "smooth") {
        if (std::min(part.u, part.v) < 40) {
            throw ValueError("Front jest zbyt mały na wybrany wzór frezowania.");
        }
        double border = std::min(num(cfg["front_border"]), std::min(part.u, part.v) / 4);
        double depth = _safe_depth(part.t, num(cfg["front_depth"]));
        actual["front_border"] = border; actual["front_depth"] = depth;
        double length = part.u - 2 * border, width = part.v - 2 * border;
        if (style == "frame") {
            double radius = std::min(num(cfg["front_radius"]), std::min(length, width) / 2 - .1);
            actual["front_radius"] = radius;
            std::string purpose = "Ramka frontu — wybranie środka, pozostawiony obwód";
            if (radius < .1) {
                part.machining.push_back(_cut(purpose, "rect", part.u / 2, part.v / 2, length, width, depth));
            } else {
                // Two rectangles and four circles form one exact rounded
                // rectangular pocket. These are real exported machining cuts.
                part.machining.push_back(_cut(purpose, "rect", part.u / 2, part.v / 2, length - 2 * radius, width, depth));
                part.machining.push_back(_cut(purpose, "rect", part.u / 2, part.v / 2, length, width - 2 * radius, depth));
                for (double u : {border + radius, part.u - border - radius}) {
                    for (double v : {border + radius, part.v - border - radius}) {
                        part.machining.push_back(_cut(purpose, "circle", u, v, 2 * radius, 2 * radius, depth));
                    }
                }
            }
        } else if (style == "fluted") {
            double groove_width = std::min({num(cfg["flute_width"]), length / 2, width});
            double pitch = std::max(num(cfg["flute_spacing"]), groove_width + 2);
            long long fit = std::max(1LL, (long long)std::floor((length - groove_width) / pitch) + 1);
            long long requested = cfg["flute_count"];
            long long count = requested ? requested : std::min((long long)MAX_FLUTES, fit);
            if (count > fit) {
                throw ValueError(std::format("Na tym froncie mieści się najwyżej {} rowków przy podanym rozstawie.", fit));
            }
            // Keep requested pitch; if the auto count is capped the pattern is
            // centred, never overlaps grooves or silently changes dimensions.
            double first = part.u / 2 - (count - 1) * pitch / 2;
            for (long long i = 0; i < count; ++i) {
                part.machining.push_back(_cut("Ryflowanie frontu", "slot", first + i * pitch,
                                              part.v / 2, width, groove_width, depth, 90));
            }
            actual["flute_width"] = groove_width; actual["flute_spacing"] = pitch; actual["flute_count"] = count;
        } else if (style == "finger") {
            opening_options::clear(project, part.id);
            double pull_width = std::min(num(cfg["finger_width"]), part.v / 3);
            double pull_length = std::min(num(cfg["finger_length"]), part.u - 20);
            double pull_depth = _safe_depth(part.t, num(cfg["finger_depth"]));
            if (pull_length < pull_width) {
                throw ValueError("Front jest zbyt wąski na uchwyt frezowany.");
            }
            double level = part.v - std::max(pull_width / 2 + 8, std::min(40.0, part.v / 4));
            project.hardware.push_back({{"kind", "opening"}, {"owner_id", part.id},
                {"name", opening_options::OPTIONS.at("finger_pocket").at("label")}, {"option", "finger_pocket"},
                {"qty", 0}, {"price", 0}, {"length", pull_length}, {"width", pull_width}, {"depth", pull_depth},
                {"level", level}, {"level_ratio", level / part.v}, {"center_u", part.u / 2},
                {"u_ratio", .5}, {"angle", 0}, {"profile_edge", "top"}, {"color", "#242628"}, {"source", ""},
                {"note", "Frezowanie we froncie; nie jest kupowanym okuciem."},
                {"config_auto", true}, {"configure_feature", "front_style"}});
            opening_options::refresh_openings(project, part.id);
            for (auto& cut : part.machining) {
                if (py::get(cut, "opening_owner") == part.id) {
                    cut["config_auto"] = true; cut["configure_feature"] = "front_style";
                }
            }
            actual["finger_length"] = pull_length; actual["finger_width"] = pull_width; actual["finger_depth"] = pull_depth;
        }
        std::vector<json*> generated;
        for (auto& cut : part.machining) if (is_auto_front(cut)) generated.push_back(&cut);
        std::optional<double> safe_depth = _guard_opposed_drills(part, generated);
        if (safe_depth) {
            actual[style == "finger" ? "finger_depth" : "front_depth"] = *safe_depth;
            for (auto& h : project.hardware) {
                if (py::get(h, "owner_id") == part.id && is_auto_front(h)) {
                    h["depth"] = *safe_depth;
                }
            }
        }
    }
    part.configurator_source["front_style"] = actual;
    return part;
}

ProjectPtr apply_front_style(const Project& project, const std::string& part_id, const json& style, const json& config, bool validate) {
    // Return a modified project copy; failed milling leaves the source unchanged.
    auto result = std::make_shared<Project>(project);
    Part* part = find_part(*result, part_id);
    if (part == nullptr) {
        throw ValueError("Nie znaleziono frontu w projekcie.");
    }
    apply_front_to_part(*result, *part, style, config);
    if (validate) {
        result->validate();
    }
    return result;
}

ProjectPtr apply_front_style(const Project& project, const Part& part, const json& style, const json& config, bool validate) {
    return apply_front_style(project, part.id, style, config, validate);  // getattr(part_id, 'id', part_id)
}

json normalize_accessories(const json& config) {
    json c = py::truthy(config) ? config : json::object();
    long long count = _integer(py::get(c, "leg_count", 4), 4, 6, "Liczba nóg");
    if (count != 4 && count != 6) {
        throw ValueError("Liczba nóg: 4 albo 6.");
    }
    json mount = py::get(c, "led_mount", "under_shelf");
    json side = py::get(c, "led_side", "left");
    if ((mount != "under_shelf" && mount != "side") || (side != "left" && side != "right")) {
        throw ValueError("LED: under_shelf albo side; bok left albo right.");
    }
    return {{"leg_height", finite(py::get(c, "leg_height", 0), 0, 500)}, {"leg_count", count},
        {"leg_diameter", finite(py::get(c, "leg_diameter", 40), 10, 120)},
        {"leg_setback", finite(py::get(c, "leg_setback", 60), 0, 250)},
        {"leg_color", _color(py::get(c, "leg_color", "#242628"))}, {"leg_price", finite(py::get(c, "leg_price", 0), 0, 100000)},
        {"led", py::truthy(py::get(c, "led", false))}, {"led_mount", mount}, {"led_side", side},
        {"led_width", finite(py::get(c, "led_width", 12), 4, 50)}, {"led_depth", finite(py::get(c, "led_depth", 3), .1, 20)},
        {"led_margin", finite(py::get(c, "led_margin", 30), 5, 150)},
        {"led_price_per_m", finite(py::get(c, "led_price_per_m", 0), 0, 100000)},
        {"led_profile", py::truthy(py::get(c, "led_profile", true))}, {"led_diffuser", py::truthy(py::get(c, "led_diffuser", true))},
        {"led_profile_price_per_m", finite(py::get(c, "led_profile_price_per_m", 0), 0, 100000)},
        {"led_diffuser_price_per_m", finite(py::get(c, "led_diffuser_price_per_m", 0), 0, 100000)},
        {"led_source", py::strip(py::str(py::get(c, "led_source", "")))}, {"led_source_price", finite(py::get(c, "led_source_price", 0), 0, 100000)},
        {"led_color", _color(py::get(c, "led_color", "#fff3bf"))}, {"led_targets", py::get(c, "led_targets")}};
}

std::pair<Point3, Point3> _body_bounds(const Project& project, const std::string& group, const json& bounds) {
    if (!bounds.is_null()) {
        json lo, hi;
        if (bounds.contains("lower") && bounds.contains("upper")) {
            lo = bounds["lower"]; hi = bounds["upper"];
        } else {
            lo = json::array({bounds.at("body_x"), bounds.at("body_y"), bounds.at("body_z")});
            hi = json::array({num(lo[0]) + num(bounds.at("width")), num(lo[1]) + num(bounds.at("depth")), num(lo[2]) + num(bounds.at("body_height"))});
        }
        if (lo.size() != 3 || hi.size() != 3) {
            throw ValueError("Obrys korpusu wymaga trzech współrzędnych.");
        }
        Point3 l{finite(lo[0]), finite(lo[1]), finite(lo[2])};
        Point3 h{finite(hi[0]), finite(hi[1]), finite(hi[2])};
        for (int i = 0; i < 3; ++i) if (h[i] <= l[i]) {
            throw ValueError("Korpus musi mieć dodatnie wymiary.");
        }
        return {l, h};
    }
    std::vector<const Part*> body;
    for (auto& p : project.parts)
        if (p.group == group && (p.role == "left" || p.role == "right" || p.role == "bottom" || p.role == "top" || p.role == "partition")
            && !py::truthy(py::get(p.assembly, "generated_by")))
            body.push_back(&p);
    if (body.empty()) {
        throw ValueError("Dodaj korpus, zanim dodasz nogi lub oświetlenie.");
    }
    std::vector<Point3> points;
    for (const Part* p : body) for (auto& [u, v] : p->points()) for (double w : {0.0, p->t}) points.push_back(local_world(*p, u, v, w));
    Point3 lo = points[0], hi = points[0];
    for (auto& pt : points) for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], pt[i]); hi[i] = std::max(hi[i], pt[i]); }
    return {lo, hi};
}

void _install_led(Project& project, const std::vector<Part*>& members, const std::string& group, const json& cfg) {
    std::vector<Part*> targets;
    auto by_z_id = [](const Part* a, const Part* b) { return std::tie(a->z, a->id) < std::tie(b->z, b->id); };
    if (!cfg["led_targets"].is_null()) {
        if (!cfg["led_targets"].is_array()) {
            throw ValueError("Lista elementów LED musi zawierać identyfikatory elementów.");
        }
        for (Part* p : members) for (auto& id : cfg["led_targets"]) if (id == p->id) { targets.push_back(p); break; }
        std::set<json> unique(cfg["led_targets"].begin(), cfg["led_targets"].end());
        if (targets.size() != unique.size()) {
            throw ValueError("Nie znaleziono wybranego elementu pod LED w tym meblu.");
        }
    } else if (cfg["led_mount"] == "under_shelf") {
        for (Part* p : members) if (p->role == "shelf" && p->plane == "XY") targets.push_back(p);
        std::stable_sort(targets.begin(), targets.end(), by_z_id);
        if (targets.empty()) {
            for (Part* p : members) if (p->role == "top" && p->plane == "XY") targets.push_back(p);
            std::stable_sort(targets.begin(), targets.end(), by_z_id);
            if (targets.size() > 1) targets.resize(1);
        }
    } else {
        for (Part* p : members) if (cfg["led_side"] == p->role && p->plane == "YZ") targets.push_back(p);
        std::stable_sort(targets.begin(), targets.end(), [](const Part* a, const Part* b) { return a->id < b->id; });
        if (targets.size() > 1) targets.resize(1);
    }
    if (targets.empty()) {
        throw ValueError("Nie znaleziono półki, wieńca lub boku dla wybranego montażu LED.");
    }
    for (Part* p : targets) {
        bool horizontal = cfg["led_mount"] == "under_shelf";
        if ((horizontal && p->plane != "XY") || (!horizontal && p->plane != "YZ")) {
            throw ValueError("LED pod półką wymaga płaszczyzny XY, LED na boku płaszczyzny YZ.");
        }
        auto [major, minor] = horizontal ? std::make_pair(p->u, p->v) : std::make_pair(p->v, p->u);
        double margin = std::min({num(cfg["led_margin"]), major / 4, minor / 4});
        double length = major - 2 * margin, width = num(cfg["led_width"]);
        if (length < width || minor < 2 * margin + width) {
            throw ValueError("Element jest zbyt mały na wybrany profil LED.");
        }
        double depth = _safe_depth(p->t, num(cfg["led_depth"]));
        auto [u, v] = horizontal ? std::make_pair(p->u / 2, margin + width / 2) : std::make_pair(margin + width / 2, p->v / 2);
        std::string face = horizontal || p->role == "right" ? "bottom" : "top";
        json cut = _cut("Rowek pod profil / taśmę LED", "slot", u, v, length, width, depth,
                        horizontal ? 0 : 90, "led");
        cut["face"] = face;
        depth = *_guard_opposed_drills(*p, {&cut});
        p->machining.push_back(cut);
        json base = {{"owner_id", p->id}, {"group", group}, {"config_auto", true}, {"configure_feature", "led"},
                     {"length", length}, {"width", width}, {"depth", depth}, {"u", u}, {"v", v}, {"face", face},
                     {"axis", horizontal ? "u" : "v"}, {"profile", cfg["led_profile"]}, {"diffuser", cfg["led_diffuser"]},
                     {"source", ""}, {"requires_review", true}};
        std::string note = std::format("Własny montaż: rowek {:g} × {:g} mm, długość {:g} mm. Dobierz konkretny profil, moc i zasilacz.", width, depth, length);
        project.hardware.push_back(py::merged(base, {{"kind", "led_strip"}, {"name", "Taśma LED — własny model"},
                                                     {"qty", length / 1000}, {"unit", "m"}, {"price", cfg["led_price_per_m"]}, {"color", cfg["led_color"]}, {"note", note}}));
        if (py::truthy(cfg["led_profile"])) {
            project.hardware.push_back(py::merged(base, {{"kind", "led_profile"}, {"name", "Profil LED — własny model"},
                                                         {"qty", length / 1000}, {"unit", "m"}, {"price", cfg["led_profile_price_per_m"]}, {"note", note}}));
        }
        if (py::truthy(cfg["led_diffuser"])) {
            project.hardware.push_back(py::merged(base, {{"kind", "led_diffuser"}, {"name", "Dyfuzor LED — własny model"},
                                                         {"qty", length / 1000}, {"unit", "m"}, {"price", cfg["led_diffuser_price_per_m"]}, {"note", note}}));
        }
    }
    if (py::truthy(cfg["led_source"])) {
        project.hardware.push_back({{"kind", "led_power"}, {"owner_id", targets[0]->id}, {"group", group},
            {"name", "Zasilacz LED — " + cfg["led_source"].get<std::string>()}, {"qty", 1}, {"unit", "szt."}, {"price", cfg["led_source_price"]},
            {"config_auto", true}, {"configure_feature", "led"}, {"source", ""}, {"requires_review", true},
            {"note", "Model własny. Dobierz moc, napięcie i sposób montażu do rzeczywistej taśmy LED."}});
    }
}

ProjectPtr install_accessories(const Project& project, const std::string& group, const json& config, const json& bounds, bool validate) {
    // Return a copy with deterministic replacement of this group's auto fittings.
    //
    // bounds uses construction coordinates: {lower, upper, floor?}, or
    // {body_x,body_y,body_z,width,depth,body_height,floor?}. Height of the body is
    // exclusive of legs. If floor is omitted it is body_z minus leg_height.
    auto result = std::make_shared<Project>(project);
    json cfg = normalize_accessories(config);
    std::vector<Part*> members;
    for (auto& p : result->parts) if (p.group == group) members.push_back(&p);
    if (members.empty()) {
        throw ValueError("Nie znaleziono wybranego mebla.");
    }
    std::set<std::string> ids; for (Part* p : members) ids.insert(p->id);
    for (Part* p : members) {
        json machining = json::array();
        for (auto& c : p->machining) if (!(py::truthy(py::get(c, "config_auto")) && py::get(c, "configure_feature") == "led")) machining.push_back(c);
        p->machining = machining;
    }
    json hardware = json::array();
    for (auto& h : result->hardware) {
        json feature = py::get(h, "configure_feature"); json owner = py::get(h, "owner_id");
        bool auto_fitting = py::truthy(py::get(h, "config_auto")) && (feature == "legs" || feature == "led")
                            && (py::get(h, "group") == group || (owner.is_string() && ids.contains(owner.get<std::string>())));
        if (!auto_fitting) hardware.push_back(h);
    }
    result->hardware = hardware;
    if (py::truthy(cfg["leg_height"])) {
        auto [lo, hi] = _body_bounds(*result, group, bounds);
        Part* owner = nullptr;
        for (Part* p : members) if (p->role == "bottom" && p->plane == "XY" && (!owner || p->z < owner->z)) owner = p;
        if (owner == nullptr) {
            throw ValueError("Nogi wymagają dna mebla, do którego są przypisane.");
        }
        double leg_height = num(cfg["leg_height"]);
        double floor = !bounds.is_null() && bounds.contains("floor") ? finite(bounds["floor"]) : lo[2] - leg_height;
        if (std::abs(floor + leg_height - lo[2]) > .01) {
            throw ValueError("Wysokość nóg musi odpowiadać odległości dna korpusu od podłogi.");
        }
        double radius = num(cfg["leg_diameter"]) / 2;
        // A slightly broader circular foot is part of the purchased leg.
        double foot_radius = radius * 1.15;
        double setback = std::max(num(cfg["leg_setback"]), foot_radius + 1);
        if (std::min(hi[0] - lo[0], hi[1] - lo[1]) <= 2 * setback) {
            throw ValueError("Korpus jest zbyt mały na wybrany rozstaw / średnicę nóg.");
        }
        std::vector<double> xs{lo[0] + setback, hi[0] - setback};
        if (cfg["leg_count"] == 6) {
            xs.insert(xs.begin() + 1, (lo[0] + hi[0]) / 2);
        }
        json positions = json::array();
        for (double x : xs) for (double y : {lo[1] + setback, hi[1] - setback}) positions.push_back({{"x", x}, {"y", y}, {"z", floor}});
        result->hardware.push_back({{"kind", "leg"}, {"owner_id", owner->id}, {"group", group},
            {"name", std::format("Noga meblowa — własny model Ø{:g} × {:g} mm", num(cfg["leg_diameter"]), leg_height)},
            {"qty", cfg["leg_count"]}, {"unit", "szt."}, {"price", cfg["leg_price"]}, {"height", cfg["leg_height"]},
            {"diameter", cfg["leg_diameter"]}, {"setback", setback}, {"positions", positions}, {"color", cfg["leg_color"]},
            {"foot_diameter", foot_radius * 2}, {"config_auto", true}, {"configure_feature", "legs"}, {"source", ""}, {"requires_review", true},
            {"note", "Model zakupowy, bez automatycznego wiercenia mocowań. Dobierz artykuł, nośność i sposób mocowania."}});
    }
    if (py::truthy(cfg["led"])) {
        _install_led(*result, members, group, cfg);
    }
    if (validate) {
        result->validate();
    }
    return result;
}

std::vector<Point3> accessory_vertices(const Project& project, const std::optional<std::string>& group, bool local, bool include_all) {
    // Nominal fitting envelope, with no expensive panel CAD mesh generation.
    //
    // Includes new support/accessory geometry (legs and LED) by default. Set
    // include_all=True for handles, hinges, tracks and every visible fitting.
    // local=True reverses only the owner's final room placement; it preserves
    // construction transforms and the supplied closed/posed state of the model.
    std::map<std::string, const Part*> owners; for (auto& p : project.parts) owners[p.id] = &p;
    std::vector<Point3> vertices;
    std::optional<std::set<std::string>> kinds;
    if (!include_all) kinds = std::set<std::string>{"leg", "led_strip"};
    for (auto& [owner, face, color] : fitting_visuals::fitting_faces(project, kinds)) {
        auto found = owners.find(owner);
        const Part* p = found != owners.end() ? found->second : nullptr;
        if (p == nullptr || !p->visible || (group && p->group != *group)) {
            continue;
        }
        for (auto& point : face) vertices.push_back(local ? inverse_layout_point(*p, point) : point);
    }
    return vertices;
}

const std::vector<std::string> LED_SURFACES{"top", "bottom", "u0", "u1", "v0", "v1"};

LedSurface led_surface(const Part& part, const std::string& face) {
    // Face coordinates A,B with an exterior normal, in the part's UVW frame.
    if (std::find(LED_SURFACES.begin(), LED_SURFACES.end(), face) == LED_SURFACES.end()) throw ValueError("Wybierz powierzchnię LED: top/bottom/u0/u1/v0/v1.");
    if (face == "top") return {{0, 0, part.t}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, part.u, part.v};
    if (face == "bottom") return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, part.u, part.v};
    if (face == "u0") return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, part.v, part.t};
    if (face == "u1") return {{part.u, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, part.v, part.t};
    if (face == "v0") return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, part.u, part.t};
    return {{0, part.v, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, part.u, part.t};
}

json led_proposal(Part& part, const json& local_hit, const json& options_) {
    // Pure fitted LED description; no stock item or machining is created here.
    json options = py::truthy(options_) ? options_ : json::object(); json hit = py::truthy(local_hit) ? local_hit : json::object();
    if (!hit.is_object()) {
        json zipped = json::object(); const char* keys[] = {"u", "v", "w"};
        for (size_t i = 0; i < 3 && i < hit.size(); ++i) zipped[keys[i]] = hit[i];
        hit = zipped;
    }
    json face_value = py::truthy(py::get(options, "face")) ? options["face"] : py::get(hit, "face", "bottom");
    if (!face_value.is_string()) throw ValueError("Wybierz powierzchnię LED: top/bottom/u0/u1/v0/v1.");
    std::string face = face_value;
    auto [origin, a, b, n, la, lb] = led_surface(part, face);
    if ((face == "u0" || face == "u1" || face == "v0" || face == "v1") && (!part.contour.empty() || py::truthy(part.profile))) {
        throw ValueError("Montaż na krawędzi konturu wymaga ręcznego doboru; wybierz powierzchnię +T lub −T.");
    }
    json axis_value = py::get(options, "axis", "auto");
    if (axis_value != "auto" && axis_value != "u" && axis_value != "v") throw ValueError("Kierunek LED: auto/u/v.");
    std::string axis = axis_value == "auto" ? (la >= lb ? "u" : "v") : axis_value.get<std::string>();
    double width = finite(py::get(options, "width", 12), 1, 100);
    double margin = finite(py::get(options, "margin", 30), 0, 500);
    auto [major, minor] = axis == "u" ? std::make_pair(la, lb) : std::make_pair(lb, la);
    margin = std::min(margin, major / 4);
    if (minor < width + 1 || major - 2 * margin < width) throw ValueError("Profil LED nie mieści się na tej powierzchni. Zmniejsz szerokość lub wybierz inną powierzchnię.");
    Point3 point{finite(py::get(hit, "u", part.u / 2)), finite(py::get(hit, "v", part.v / 2)), finite(py::get(hit, "w", part.t / 2))};
    std::array<double, 2> coords{};
    for (int k = 0; k < 2; ++k) {
        const std::array<int, 3>& vec = k == 0 ? a : b;
        for (int i = 0; i < 3; ++i) coords[k] += (point[i] - origin[i]) * vec[i];
    }
    for (int k = 0; k < 2; ++k) {
        double size = k == 0 ? la : lb;
        if (!(-.01 <= coords[k] && coords[k] <= size + .01)) throw ValueError("Punkt montażu leży poza wybraną powierzchnią.");
    }
    json requested = py::get(options, "length");
    // requested in (None, '', 0)
    bool length_auto = requested.is_null() || requested == "" || (requested.is_number() && num(requested) == 0) || requested == false;
    double length = length_auto ? major - 2 * margin : std::min(finite(requested, width, 10000), major - 2 * margin);
    double along = length_auto ? major / 2 : std::min(major - margin - length / 2, std::max(margin + length / 2, coords[axis == "u" ? 0 : 1]));
    double across = std::min(minor - width / 2 - .5, std::max(width / 2 + .5, coords[axis == "u" ? 1 : 0]));
    auto [u, v] = axis == "u" ? std::make_pair(along, across) : std::make_pair(across, along);
    if (face == "top" || face == "bottom") {
        auto points = part.points();
        auto contained = [&](double l) {
            for (auto [da, db] : std::vector<std::pair<int, int>>{{-1, -1}, {-1, 1}, {1, -1}, {1, 1}}) {
                core::Point q{u + (axis == "u" ? da * l / 2 : db * width / 2), v + (axis == "u" ? db * width / 2 : da * l / 2)};
                if (!inside(q, points)) return false;
            }
            return true;
        };
        // Start at the cursor for shaped parts, then shorten without leaving the actual contour.
        if ((!part.contour.empty() || py::truthy(part.profile)) && !contained(length)) {
            u = point[0]; v = point[1];
            for (int i = 0; i < 60; ++i) {
                if (contained(length)) break;
                length *= .9;
            }
            if (length < width || !contained(length)) throw ValueError("LED nie mieści się w konturze przy tym punkcie montażu.");
        }
    }
    json groove_value = py::get(options, "groove", false);
    if (!groove_value.is_boolean()) throw ValueError("Rowek LED wymaga TAK / NIE.");
    bool groove = groove_value;
    if (groove && face != "top" && face != "bottom") throw ValueError("Rowek CAD obsługuje powierzchnie +T i −T. Na krawędzi wybierz montaż bez rowka.");
    double depth = groove ? _safe_depth(part.t, finite(py::get(options, "depth", 3), .1, 20)) : finite(py::get(options, "depth", 2), .1, 20);
    json cut = {{"type", "groove"}, {"shape", "slot"}, {"face", face}, {"u", u}, {"v", v}, {"length", length}, {"width", width}, {"depth", depth},
                {"angle", axis == "u" ? 0 : 90}, {"purpose", "Rowek LED — ręczne rozmieszczenie"}, {"config_auto", false}};
    if (groove) {
        json ops = hole_operations(part); for (auto& op : part.operations) ops.push_back(op);
        for (auto& op : ops) {
            if (py::get(op, "face") == face && _overlaps_drill(cut, num(op["u"]), num(op["v"]), num(op["diameter"]) / 2))
                throw ValueError("Rowek LED koliduje z otworem od tej samej powierzchni. Przesuń LED lub wyłącz rowek.");
        }
        _guard_opposed_drills(part, {&cut}); depth = num(cut["depth"]);
        features::validate_cut(part, cut);
    }
    Point3 center{origin[0] + u * a[0] + v * b[0], origin[1] + u * a[1] + v * b[1], origin[2] + u * a[2] + v * b[2]};
    return {{"face", face}, {"local_face", face}, {"u", u}, {"v", v}, {"axis", axis}, {"length", length}, {"width", width}, {"depth", depth},
            {"margin", margin}, {"groove", groove}, {"local_center", point_json(center)},
            {"local_tangent", point_json(axis == "u" ? a : b)}, {"local_cross", point_json(axis == "u" ? b : a)}, {"local_normal", point_json(n)},
            {"a_ratio", u / la}, {"b_ratio", v / lb}, {"length_auto", length_auto},
            {"requested_length", length_auto ? json(nullptr) : requested},
            {"profile", py::truthy(py::get(options, "profile", true))}, {"diffuser", py::truthy(py::get(options, "diffuser", true))},
            {"color", _color(py::get(options, "color", "#fff3bf"))}, {"cut", groove ? cut : json(nullptr)}};
}

ProjectPtr install_manual_led(const Project& project, const std::string& part_id, const json& local_hit, const json& options_, const json& attachment_id, bool validate) {
    // Atomic purchased LED placement on any planar local face; groove is opt-in.
    auto result = std::make_shared<Project>(project); Part* part = find_part(*result, part_id);
    if (part == nullptr) throw ValueError("Nie znaleziono elementu dla LED.");
    json cfg = led_proposal(*part, local_hit, options_); json options = py::truthy(options_) ? options_ : json::object();
    json ident = py::truthy(attachment_id) ? attachment_id : json(uid());
    json hardware = json::array();
    for (auto& h : result->hardware) if (py::get(h, "attachment_id") != ident) hardware.push_back(h);
    result->hardware = hardware;
    for (auto& p : result->parts) {
        json machining = json::array();
        for (auto& c : p.machining) if (py::get(c, "attachment_id") != ident) machining.push_back(c);
        p.machining = machining;
    }
    json cut = cfg["cut"]; cfg.erase("cut");
    if (py::truthy(cut)) part->machining.push_back(py::merged(cut, {{"attachment_id", ident}, {"manual", true}}));
    json common = py::merged(cfg, {{"owner_id", part->id}, {"group", part->group}, {"attachment_id", ident}, {"manual", true}, {"config_auto", false},
                                   {"configure_feature", "manual_led"}, {"source", ""}, {"requires_review", true}});
    std::string note = std::string(cfg["groove"].get<bool>() ? "Rowek" : "Montaż powierzchniowy bez rowka") + std::format("; {:g} × {:g} mm. Własny model; dobierz artykuł, moc i mocowanie.", num(cfg["length"]), num(cfg["width"]));
    for (auto& [kind, label, price] : std::vector<std::tuple<std::string, std::string, std::string>>{{"led_strip", "Taśma LED", "price_per_m"}, {"led_profile", "Profil LED", "profile_price_per_m"}, {"led_diffuser", "Dyfuzor LED", "diffuser_price_per_m"}}) {
        if ((kind == "led_profile" && !py::truthy(cfg["profile"])) || (kind == "led_diffuser" && !py::truthy(cfg["diffuser"]))) continue;
        result->hardware.push_back(py::merged(common, {{"kind", kind}, {"name", label + " — ręczne rozmieszczenie"}, {"qty", num(cfg["length"]) / 1000}, {"unit", "m"},
                                                       {"price", finite(py::get(options, price, 0), 0, 100000)}, {"note", note}, {"default_note", note}}));
    }
    if (validate) result->validate();
    return result;
}

Project& refresh_manual_led(Project& project) {
    // Refresh dimensions on a caller-owned candidate; retain attachment IDs/prices.
    json records = json::array();
    for (auto& h : project.hardware) if (py::get(h, "kind") == "led_strip" && py::get(h, "configure_feature") == "manual_led") records.push_back(h);
    std::map<std::string, Part*> owners; for (auto& p : project.parts) owners[p.id] = &p;
    for (auto& h : records) {
        auto found = h.at("owner_id").is_string() ? owners.find(h["owner_id"].get<std::string>()) : owners.end();
        Part* p = found != owners.end() ? found->second : nullptr;
        if (p == nullptr) throw ValueError("Ręczny LED wskazuje usunięty element; usuń montaż razem z właścicielem.");
        std::string face = h.at("local_face"); auto [origin, a, b, n, la, lb] = led_surface(*p, face);
        Point3 center;
        for (int i = 0; i < 3; ++i) center[i] = origin[i] + la * num(h.at("a_ratio")) * a[i] + lb * num(h.at("b_ratio")) * b[i];
        json options = json::object(); for (std::string k : {"axis", "width", "depth", "margin", "groove", "profile", "diffuser", "color"}) options[k] = h.at(k);
        json length = h.at("length");
        options["face"] = face; options["length"] = py::truthy(py::get(h, "length_auto")) ? json(nullptr) : py::get(h, "requested_length", length); options["price_per_m"] = h.at("price");
        for (auto& item : project.hardware) {
            if (py::get(item, "attachment_id") == h["attachment_id"]) {
                if (py::get(item, "kind") == "led_profile") options["profile_price_per_m"] = item.at("price");
                if (py::get(item, "kind") == "led_diffuser") options["diffuser_price_per_m"] = item.at("price");
            }
        }
        // Remove old groove before checking collisions against the refreshed host.
        ProjectPtr candidate = install_manual_led(project, p->id, {{"u", center[0]}, {"v", center[1]}, {"w", center[2]}}, options, h["attachment_id"], false);
        std::map<json, json> prior;
        for (auto& item : project.hardware) if (py::get(item, "attachment_id") == h["attachment_id"]) prior[item.at("kind")] = item;
        for (auto& item : candidate->hardware) {
            json old = nullptr;
            if (py::get(item, "attachment_id") == h["attachment_id"]) { auto it = prior.find(py::get(item, "kind")); if (it != prior.end()) old = it->second; }
            if (py::truthy(old)) {
                for (std::string key : {"name", "price", "article", "manufacturer", "supplier", "source", "note"}) {
                    if (key == "note" && py::get(old, "note") == py::get(old, "default_note")) continue;
                    if (old.contains(key)) item[key] = old[key];
                }
            }
        }
        project.parts = candidate->parts; project.hardware = candidate->hardware; owners.clear(); for (auto& q : project.parts) owners[q.id] = &q;
    }
    return project;
}

std::vector<std::pair<std::vector<Point3>, std::string>> led_local_geometry(const Part& part, const json& hardware) {
    // Lightweight geometry/color pairs; supports legacy automatic LED records.
    const json& h = hardware; double length = num(py::get(h, "length", 0)), width = num(py::get(h, "width", 0)), depth = num(py::get(h, "depth", 0));
    if (std::min({length, width, depth}) <= 0) return {};
    Point3 center, a, b, normal; bool groove;
    auto vec3 = [](const json& v) { return Point3{num(v[0]), num(v[1]), num(v[2])}; };
    if (py::get(h, "local_center").is_null()) {
        bool top = py::get(h, "face") == "top"; center = {num(h.at("u")), num(h.at("v")), top ? part.t : 0};
        a = py::get(h, "axis") == "v" ? Point3{0, 1, 0} : Point3{1, 0, 0}; b = py::get(h, "axis") == "v" ? Point3{1, 0, 0} : Point3{0, 1, 0};
        normal = top ? Point3{0, 0, 1} : Point3{0, 0, -1}; groove = true;
    } else { center = vec3(h["local_center"]); a = vec3(h.at("local_tangent")); b = vec3(h.at("local_cross")); normal = vec3(h.at("local_normal")); groove = py::truthy(py::get(h, "groove", false)); }
    auto geometry = [&](double l, double w, double z, double height) {
        Point3 cross{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        bool flip = cross[0] * normal[0] + cross[1] * normal[1] + cross[2] * normal[2] < 0;
        std::vector<std::vector<Point3>> faces;
        for (auto face : fitting_visuals::slot_prism(0, 0, z, l, w, height)) {
            if (flip) std::reverse(face.begin(), face.end());
            std::vector<Point3> out;
            for (auto& [x, y, t] : face) out.push_back({center[0] + x * a[0] + y * b[0] + t * normal[0], center[1] + x * a[1] + y * b[1] + t * normal[1], center[2] + x * a[2] + y * b[2] + t * normal[2]});
            faces.push_back(out);
        }
        return faces;
    };
    std::vector<std::pair<std::vector<Point3>, std::string>> out;
    bool profile = py::truthy(py::get(h, "profile", true));
    if (profile) for (auto& f : geometry(length, width, groove ? -depth : 0, depth)) out.emplace_back(f, "#949ba2");
    double light_width = profile ? width * .75 : width; double light_length = std::max(light_width, length - width * .25);
    std::string color = py::get(h, "color", "#fff3bf");
    for (auto& f : geometry(light_length, light_width, groove ? 0 : depth, .3)) out.emplace_back(f, color);
    return out;
}

}  // namespace configurator_accessories
