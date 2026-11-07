// Inspektor produkcyjny. Patrz production_inspector.h.
#include "production_inspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "assembly.h"
#include "features.h"
#include "pyutil.h"

namespace production_inspector {

using core::Part;
using core::Point3;
using core::Project;

namespace {

constexpr double EPS = 1e-6;

double jnum(const json& v, double fallback = 0) { return v.is_number() ? v.get<double>() : fallback; }
std::string g1(double v) { return std::format("{:.1f}", v); }
std::string g0(double v) { return std::format("{:g}", std::round(v * 10) / 10); }

bool multiple_of_90(double angle) {
    double r = std::fmod(std::abs(angle), 90.0);
    return r < 1e-4 || 90.0 - r < 1e-4;
}
bool axis_aligned(const Part& p) { return multiple_of_90(p.rx) && multiple_of_90(p.ry) && multiple_of_90(p.rz); }

double elastic_modulus(const std::string& kind) {
    // N/mm²; wartości przybliżone dla typowych płyt (zginanie), służą do oceny ugięcia półek.
    static const std::map<std::string, double> e{{"plyta", 2500}, {"mdf", 3000}, {"sklejka", 5000}, {"drewno", 9000}, {"hdf", 3000}};
    auto found = e.find(kind);
    return found == e.end() ? 2500 : found->second;
}

double next_standard_thickness(double needed) {
    for (double t : {16.0, 18.0, 19.0, 22.0, 25.0, 28.0, 30.0, 36.0, 38.0, 40.0})
        if (t + 1e-9 >= needed) return t;
    return std::ceil(needed);
}

struct Collector {
    std::vector<Finding> items;
    void add(Finding f) { items.push_back(std::move(f)); }
};

Finding make(const std::string& rule, Severity severity, const Part& part, const std::string& message, const std::string& hint = "") {
    Finding f;
    f.rule = rule; f.severity = severity; f.part_id = part.id; f.part_name = part.name; f.group = part.group;
    f.message = message; f.hint = hint;
    return f;
}

const core::Material* material_of(const Project& project, const Part& part) {
    for (const auto& m : project.materials) if (m.id == part.material) return &m;
    return nullptr;
}

// ---------------------------------------------------------------- półki

void check_shelves(const Project& project, const Settings& s, Collector& out) {
    struct Box { const Part* part; Point3 lo, hi; };
    std::map<std::string, std::vector<Box>> by_group;
    for (const auto& p : project.parts) {
        if (!axis_aligned(p)) continue;
        auto [lo, hi] = assembly::bounds(p);
        by_group[p.group].push_back({&p, lo, hi});
    }
    for (const auto& [group, boxes] : by_group) {
        for (const auto& shelf : boxes) {
            const Part& p = *shelf.part;
            if (assembly::inferred_role(p) != "shelf" || py::truthy(py::get(p.assembly, "generated_by"))) continue;
            double dz = shelf.hi[2] - shelf.lo[2];
            if (std::abs(dz - p.t) > 0.5) continue;  // półka musi leżeć poziomo
            double x0 = shelf.lo[0], x1 = shelf.hi[0];
            double depth = shelf.hi[1] - shelf.lo[1];
            if (x1 - x0 < 50 || depth < 50) continue;
            // Pionowe przegrody pod półką skracają przęsło.
            std::vector<std::pair<double, double>> supports;
            for (const auto& other : boxes) {
                if (other.part == &p) continue;
                double dx = other.hi[0] - other.lo[0], dy = other.hi[1] - other.lo[1], dzo = other.hi[2] - other.lo[2];
                if (!(dx < dy && dx < dzo && dzo > dx * 3)) continue;  // cienka, pionowa płyta w płaszczyźnie YZ
                if (other.lo[0] <= x0 + 20 || other.hi[0] >= x1 - 20) continue;
                if (other.lo[2] > shelf.lo[2] + 1 || other.hi[2] < shelf.hi[2] - 1) continue;
                double overlap = std::min(other.hi[1], shelf.hi[1]) - std::max(other.lo[1], shelf.lo[1]);
                if (overlap < depth * 0.5) continue;
                supports.push_back({other.lo[0], other.hi[0]});
            }
            std::sort(supports.begin(), supports.end());
            double span = 0, cursor = x0;
            for (const auto& [a, b] : supports) { span = std::max(span, a - cursor); cursor = b; }
            span = std::max(span, x1 - cursor);
            const auto* mat = material_of(project, p);
            std::string kind = mat ? mat->kind : "plyta";
            double E = elastic_modulus(kind);
            double pressure = s.shelf_load_kg_m2 * 9.81 / 1e6;  // N/mm²
            double t = p.t;
            double sag = 0.15625 * pressure * std::pow(span, 4) / (E * t * t * t);  // 5qL⁴/(384EI), q = p·b, I = b·t³/12
            double limit = span / s.sag_ratio;
            if (sag <= limit) continue;
            double max_span = std::cbrt(E * t * t * t / (0.15625 * pressure * s.sag_ratio));
            double need_t = std::cbrt(0.15625 * pressure * std::pow(span, 3) * s.sag_ratio / E);
            Severity severity = sag > 2 * limit ? Severity::Error : Severity::Warning;
            Finding f = make("shelf_sag", severity, p,
                             std::format("Półka o przęśle {:.0f} mm ugnie się o ok. {} mm przy obciążeniu {:g} kg/m² (dopuszczalne {} mm).", span, g1(sag), s.shelf_load_kg_m2, g1(limit)),
                             std::format("Skróć przęsło do ok. {:.0f} mm (dodaj przegrodę), użyj płyty o grubości min. {} mm albo wzmocnij półkę listwą.", std::floor(max_span / 10) * 10,
                                         g0(next_standard_thickness(need_t))));
            f.value = sag; f.limit = limit;
            out.add(std::move(f));
        }
    }
}

// ---------------------------------------------------------------- otwory

struct Hole {
    std::array<double, 3> entry{}, dir{};  // współrzędne lokalne części (u, v, w)
    double r = 0, depth = 0;
    std::string face, label;
    double a = 0, b = 0;  // pozycja na ścianie
    int op = -1;          // indeks w Part::operations (-1 = otwór z listy holes)
};

std::vector<Hole> holes_of(const Part& p) {
    std::vector<Hole> out;
    auto push = [&](const json& op, int index) {
        Hole h;
        h.op = index;
        double d = jnum(op.at("diameter"));
        if (op.contains("countersink_diameter") && op["countersink_diameter"].is_number()) d = std::max(d, jnum(op["countersink_diameter"]));
        h.r = d / 2; h.depth = jnum(op.at("depth")); h.face = op.at("face").get<std::string>();
        h.a = jnum(op.at("u")); h.b = jnum(op.at("v"));
        auto [point, normal] = features::drill_location(p, op);
        h.entry = point; h.dir = normal;
        std::string purpose = py::get(op, "purpose").is_string() ? py::get(op, "purpose").get<std::string>() : "";
        h.label = std::format("Ø{}", g0(d)) + (purpose.empty() ? "" : " (" + purpose + ")");
        out.push_back(std::move(h));
    };
    for (size_t i = 0; i < p.operations.size(); ++i) {
        const auto& op = p.operations[i];
        if (!op.is_object() || py::get(op, "type") != "drill") continue;
        try { push(op, (int)i); } catch (const std::exception&) {}
    }
    for (const auto& h : p.holes) {
        if (h.size() != 4) continue;
        try { push(json{{"type", "drill"}, {"u", h[0]}, {"v", h[1]}, {"diameter", h[2]}, {"depth", h[3]}, {"face", "top"}, {"purpose", "otwór"}}, -1); } catch (const std::exception&) {}
    }
    return out;
}

double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
std::array<double, 3> sub(const std::array<double, 3>& a, const std::array<double, 3>& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
std::array<double, 3> add_scaled(const std::array<double, 3>& a, const std::array<double, 3>& d, double k) { return {a[0] + d[0] * k, a[1] + d[1] * k, a[2] + d[2] * k}; }

// Najmniejsza odległość między odcinkami p1→q1 i p2→q2 (Ericson, Real-Time Collision Detection).
double segment_distance(const std::array<double, 3>& p1, const std::array<double, 3>& q1, const std::array<double, 3>& p2, const std::array<double, 3>& q2) {
    auto d1 = sub(q1, p1), d2 = sub(q2, p2), r = sub(p1, p2);
    double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    double s = 0, t = 0;
    if (a <= EPS && e <= EPS) {
    } else if (a <= EPS) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        double c = dot(d1, r);
        if (e <= EPS) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            double b = dot(d1, d2), denom = a * e - b * b;
            s = denom > EPS ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0, 1.0); }
            else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    auto c1 = add_scaled(p1, d1, s), c2 = add_scaled(p2, d2, t);
    auto diff = sub(c1, c2);
    return std::sqrt(dot(diff, diff));
}

// Prześwit między dwoma otworami (walcami) po odjęciu promieni: < 0 = przecinają się. Otwory równoległe liczone są osobno
// w poprzek osi i wzdłuż niej (współosiowe otwory z przeciwnych stron płyty zostawiają prześwit równy grubości materiału między dnami).
double hole_clearance(const Hole& h1, const Hole& h2) {
    double parallel = std::abs(dot(h1.dir, h2.dir));
    if (parallel > 1 - 1e-9) {
        const auto& n = h1.dir;
        auto between = sub(h2.entry, h1.entry);
        double axial = dot(between, n);
        std::array<double, 3> lateral{between[0] - n[0] * axial, between[1] - n[1] * axial, between[2] - n[2] * axial};
        double radial_gap = std::sqrt(dot(lateral, lateral)) - h1.r - h2.r;
        double a0 = 0, a1 = h1.depth;  // przedział osiowy otworu 1 (kierunek h1.dir)
        double sign2 = dot(h2.dir, n) > 0 ? 1 : -1;
        double b0 = axial, b1 = axial + sign2 * h2.depth;
        if (b0 > b1) std::swap(b0, b1);
        double axial_gap = std::max(b0 - a1, a0 - b1);  // < 0 = przedziały zachodzą na siebie
        if (radial_gap < 0 && axial_gap < 0) return std::max(radial_gap, axial_gap);
        return std::hypot(std::max(0.0, radial_gap), std::max(0.0, axial_gap));
    }
    return segment_distance(h1.entry, add_scaled(h1.entry, h1.dir, h1.depth), h2.entry, add_scaled(h2.entry, h2.dir, h2.depth)) - h1.r - h2.r;
}

double distance_to_outline(const std::vector<core::Point>& poly, double x, double y) {
    double best = 1e18;
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& a = poly[i]; const auto& b = poly[(i + 1) % poly.size()];
        double vx = b[0] - a[0], vy = b[1] - a[1], len2 = vx * vx + vy * vy;
        double k = len2 > EPS ? std::clamp(((x - a[0]) * vx + (y - a[1]) * vy) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, std::hypot(x - (a[0] + k * vx), y - (a[1] + k * vy)));
    }
    return best;
}

void check_holes(const Part& p, const Settings& s, Collector& out) {
    auto holes = holes_of(p);
    if (holes.empty()) return;
    auto poly = p.points();
    for (const auto& h : holes) {
        double need = h.r * 2 >= 20 ? s.rim_large : s.rim_small;
        double rim = 1e18;
        std::string where = "krawędzi";
        if (h.face == "top" || h.face == "bottom") {
            rim = distance_to_outline(poly, h.a, h.b) - h.r;
            double remaining = p.t - h.depth;
            if (remaining > EPS && remaining < s.web) {
                Finding f = make("hole_edge", remaining < s.web / 2 ? Severity::Error : Severity::Warning, p,
                                 std::format("Otwór {} jest tak głęboki, że zostaje tylko {} mm materiału (płyta {} mm).", h.label, g1(remaining), g0(p.t)),
                                 "Zmniejsz głębokość otworu — cienka ścianka pęka przy skręcaniu i montażu.");
                f.value = remaining; f.limit = s.web;
                out.add(std::move(f));
            }
        } else {
            // otwór w krawędzi: a — wzdłuż krawędzi, b — po grubości (lub wysokości krawędzi)
            double along = (h.face == "u0" || h.face == "u1") ? p.v : p.u;
            double across = p.t;
            double rim_end = std::min(h.a, along - h.a) - h.r;
            double rim_face = std::min(h.b, across - h.b) - h.r;
            rim = std::min(rim_end, rim_face);
            where = rim_face <= rim_end ? "powierzchni płyty" : "narożnika";
            need = std::min(need, 3.0);
        }
        if (rim + EPS < need) {
            Finding f = make("hole_edge", rim < need / 2 ? Severity::Error : Severity::Warning, p,
                             std::format("Otwór {} leży {} mm od {} (zalecane min. {} mm).", h.label, g1(std::max(rim, 0.0)), where, g0(need)),
                             "Odsuń otwór od krawędzi — materiał może się wyrwać przy wierceniu lub dokręcaniu.");
            f.value = rim; f.limit = need;
            out.add(std::move(f));
        }
    }
    // Kolizje i zbyt bliskie sąsiedztwo otworów (3D, razem z otworami w krawędziach i z drugiej strony płyty).
    if (holes.size() > 400) return;
    std::vector<size_t> order(holes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    for (size_t i = 0; i < holes.size(); ++i) {
        const Hole& h1 = holes[i];
        for (size_t j = i + 1; j < holes.size(); ++j) {
            const Hole& h2 = holes[j];
            double reach = h1.r + h2.r + h1.depth + h2.depth + 20;
            if (std::abs(h1.entry[0] - h2.entry[0]) > reach || std::abs(h1.entry[1] - h2.entry[1]) > reach || std::abs(h1.entry[2] - h2.entry[2]) > reach) continue;
            double gap = hole_clearance(h1, h2);
            if (gap >= s.web - 1e-6) continue;
            bool same = h1.face == h2.face && std::abs(h1.a - h2.a) < 0.05 && std::abs(h1.b - h2.b) < 0.05;
            bool opposite = (h1.face == "top" && h2.face == "bottom") || (h1.face == "bottom" && h2.face == "top");
            bool aligned = opposite && std::hypot(h1.a - h2.a, h1.b - h2.b) < h1.r + h2.r;
            Finding f;
            if (same && std::abs(h1.r - h2.r) < 0.05 && std::abs(h1.depth - h2.depth) < 0.05) {
                f = make("hole_clash", Severity::Warning, p, std::format("Zdublowany otwór {} w tym samym miejscu ({}; {}).", h1.label, g0(h1.a), g0(h1.b)), "Usuń duplikat wiercenia.");
            } else if (aligned) {
                double total = h1.depth + h2.depth;
                f = make("hole_clash", gap < 0 ? Severity::Error : Severity::Warning, p,
                         gap < 0 ? std::format("Otwory z obu stron płyty zachodzą na siebie i przebijają ją na wylot: {} oraz {} (łączna głębokość {} mm przy grubości {} mm).", h1.label, h2.label, g0(total), g0(p.t))
                                 : std::format("Otwory z obu stron płyty ({} oraz {}) zostawiają tylko {} mm materiału.", h1.label, h2.label, g1(gap)),
                         "Skróć oba otwory (suma głębokości najwyżej grubość − 3 mm) albo przesuń jeden rząd otworów.");
                if (h1.op >= 0 && h2.op >= 0 && p.t - 3 > 6) { f.fixable = true; f.op_a = h1.op; f.op_b = h2.op; }
            } else if (gap < 0) {
                f = make("hole_clash", Severity::Error, p, std::format("Otwory przecinają się: {} oraz {}.", h1.label, h2.label),
                         "Okucia nachodzą na siebie — przesuń jedno z nich albo zmień rozmieszczenie (zawiasy, uchwyt, podpórki).");
            } else {
                f = make("hole_clash", Severity::Warning, p, std::format("Między otworami {} i {} zostaje tylko {} mm materiału.", h1.label, h2.label, g1(gap)),
                         "Zwiększ odstęp — cienka przegroda między otworami pęka.");
            }
            f.value = gap; f.limit = s.web;
            out.add(std::move(f));
        }
    }
}

// ---------------------------------------------------------------- obrzeża

void check_edges(const Project& project, Collector& out) {
    for (const auto& p : project.parts) {
        if (py::truthy(py::get(p.assembly, "generated_by"))) continue;
        std::string role = assembly::inferred_role(p);
        bool front = role == "door" || role == "drawer";
        bool carcass = role == "left" || role == "right" || role == "bottom" || role == "top" || role == "shelf" || role == "worktop";
        if (!front && !carcass) continue;
        const auto* mat = material_of(project, p);
        std::string kind = mat ? mat->kind : "plyta";
        if (kind == "hdf" || kind == "drewno" || kind == "mdf") continue;  // obrzeża dotyczą płyt okleinowanych
        if (!p.contour.empty()) {
            if (front && !p.contour_edge) {
                Finding f = make("edge_banding", Severity::Warning, p, "Front o nieregularnym obrysie nie ma obrzeża na krawędzi.", "Włącz obrzeże konturu.");
                f.edges = {-2}; f.fixable = true;
                out.add(std::move(f));
            }
            continue;
        }
        if (p.edges.size() != 4) continue;
        if (front) {
            static const char* names[4] = {"lewa", "prawa", "dolna", "górna"};
            std::vector<int> missing;
            std::string list;
            for (int e = 0; e < 4; ++e) {
                if (p.edges[(size_t)e]) continue;
                missing.push_back(e);
                list += (list.empty() ? "" : ", ") + std::string(names[e]);
            }
            if (missing.empty()) continue;
            Finding f = make("edge_banding", Severity::Warning, p,
                             std::format("Front bez obrzeża na {} z 4 krawędzi ({}) — wszystkie krawędzie frontu są widoczne.", missing.size(), list), "Okleina chroni przed wilgocią i uszkodzeniem.");
            f.edges = missing; f.fixable = true;
            out.add(std::move(f));
            continue;
        }
        // Korpus: widoczna jest krawędź frontowa (najmniejsze Y w układzie mebla).
        std::array<double, 4> ys{};
        std::array<std::array<double, 2>, 4> mid{{{0, p.v / 2}, {p.u, p.v / 2}, {p.u / 2, 0}, {p.u / 2, p.v}}};
        for (int e = 0; e < 4; ++e) ys[(size_t)e] = core::local_world(p, mid[(size_t)e][0], mid[(size_t)e][1], p.t / 2)[1];
        int best = (int)(std::min_element(ys.begin(), ys.end()) - ys.begin());
        double second = 1e18;
        for (int e = 0; e < 4; ++e) if (e != best) second = std::min(second, ys[(size_t)e]);
        if (second - ys[(size_t)best] < 1.0) continue;  // nie da się jednoznacznie wskazać krawędzi frontowej
        if (p.edges[(size_t)best]) continue;
        Finding f = make("edge_banding", Severity::Info, p, "Krawędź frontowa elementu korpusu bez obrzeża — jest widoczna przy otwartym froncie.", "Okleina krawędzi frontowej wygląda estetycznie i chroni płytę.");
        f.edges = {best}; f.fixable = true;
        out.add(std::move(f));
    }
}

// ---------------------------------------------------------------- kolizje części

void check_collisions(const Project& project, const Settings& s, Collector& out) {
    struct Box { const Part* part; Point3 lo, hi; };
    std::map<std::string, std::vector<Box>> by_group;
    for (const auto& p : project.parts) {
        if (!axis_aligned(p) || !p.visible) continue;
        auto [lo, hi] = assembly::bounds(p);
        by_group[p.group].push_back({&p, lo, hi});
    }
    for (auto& [group, boxes] : by_group) {
        std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.lo[0] < b.lo[0]; });
        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                if (boxes[j].lo[0] >= boxes[i].hi[0] - s.overlap) break;
                const Part& a = *boxes[i].part; const Part& b = *boxes[j].part;
                std::string ga = py::get(a.assembly, "generated_by").is_string() ? py::get(a.assembly, "generated_by").get<std::string>() : "";
                std::string gb = py::get(b.assembly, "generated_by").is_string() ? py::get(b.assembly, "generated_by").get<std::string>() : "";
                if (ga == b.id || gb == a.id || (!ga.empty() && ga == gb)) continue;  // skrzynka i front tworzą jedną całość
                double depth = 1e18;
                bool all = true;
                for (int axis = 0; axis < 3; ++axis) {
                    double o = std::min(boxes[i].hi[(size_t)axis], boxes[j].hi[(size_t)axis]) - std::max(boxes[i].lo[(size_t)axis], boxes[j].lo[(size_t)axis]);
                    if (o <= s.overlap) { all = false; break; }
                    depth = std::min(depth, o);
                }
                if (!all) continue;
                Finding f = make("part_collision", Severity::Warning, a, std::format("Część wnika w „{}” na {} mm.", b.name, g1(depth)),
                                 "Sprawdź wymiary i położenie — jeśli to celowe wpuszczenie (rowek, zakładka), pomiń tę uwagę.");
                f.other_id = b.id; f.other_name = b.name; f.value = depth; f.limit = s.overlap;
                out.add(std::move(f));
            }
        }
    }
}

// ---------------------------------------------------------------- zawiasy i płyty

void check_hinges(const Project& project, Collector& out) {
    std::map<std::string, double> count;
    for (const auto& h : project.hardware) {
        if (py::get(h, "kind") != "hinge" || !py::get(h, "owner_id").is_string()) continue;
        count[h["owner_id"].get<std::string>()] += jnum(py::get(h, "qty"), 1);
    }
    for (const auto& p : project.parts) {
        auto found = count.find(p.id);
        if (found == count.end() || assembly::inferred_role(p) != "door") continue;
        double height = p.plane == "XZ" || p.plane == "YZ" ? p.v : std::max(p.u, p.v);
        int needed = height <= 900 ? 2 : height <= 1600 ? 3 : height <= 2000 ? 4 : 5;
        if (found->second + 0.5 >= needed) continue;
        Finding f = make("hinge_count", Severity::Warning, p, std::format("Front o wysokości {:.0f} mm ma {:g} zawiasy — zalecane min. {}.", height, found->second, needed),
                         "Dodaj zawias lub zmniejsz front — za mało zawiasów powoduje wypaczanie i zwisanie drzwi.");
        f.value = found->second; f.limit = needed;
        out.add(std::move(f));
    }
}

void check_sheet_fit(const Project& project, Collector& out) {
    for (const auto& p : project.parts) {
        const auto* m = material_of(project, p);
        if (!m) continue;
        auto [a, b] = p.blank(project.edge_t);
        double sw = m->sheet_u - 2 * project.margin, sh = m->sheet_v - 2 * project.margin;
        bool fits = (a <= sw + EPS && b <= sh + EPS) || (!p.grain && b <= sw + EPS && a <= sh + EPS);
        if (fits) continue;
        if (m->kind == "drewno" && std::max(a, b) <= std::max(sw, sh) + EPS) {
            // Drewno lite występuje jako deski: szerszy element skleja się z kilku desek (blat, siedzisko) — to decyzja technologiczna, nie błąd.
            double board = std::min(m->sheet_u, m->sheet_v) - 2 * project.margin;
            int boards = board > 1 ? static_cast<int>(std::ceil(std::min(a, b) / board)) : 0;
            Finding wood = make("sheet_fit", Severity::Warning, p,
                                std::format("Formatka {:g} × {:g} mm jest szersza niż deska {:g} × {:g} mm — trzeba ją skleić z ok. {} desek.", a, b, m->sheet_u, m->sheet_v, boards),
                                "Zaplanuj klejenie desek na szerokość (wpisz to w uwagach do zamówienia) albo wybierz płytę meblową.");
            out.add(std::move(wood));
            continue;
        }
        Finding f = make("sheet_fit", Severity::Error, p,
                         std::format("Formatka {:g} × {:g} mm nie mieści się na płycie {:g} × {:g} mm (margines {:g} mm){}.", a, b, m->sheet_u, m->sheet_v, project.margin,
                                     p.grain ? " — usłojenie blokuje obrót" : ""),
                         "Podziel element na dwie części, wybierz większy format płyty albo wyłącz usłojenie.");
        out.add(std::move(f));
    }
}

// Ogranicza liczbę uwag tej samej reguły dla jednej części (reszta jako jedno zbiorcze podsumowanie).
std::vector<Finding> limit_per_part(std::vector<Finding>& findings, size_t keep) {
    std::map<std::pair<std::string, std::string>, size_t> seen;
    std::map<std::pair<std::string, std::string>, size_t> skipped;
    std::map<std::pair<std::string, std::string>, Finding> sample;
    std::vector<Finding> kept, dropped;
    for (auto& f : findings) {
        auto key = std::make_pair(f.part_id, f.rule);
        if (f.rule == "edge_banding" || f.rule == "part_collision") { kept.push_back(std::move(f)); continue; }
        if (seen[key]++ < keep) { kept.push_back(std::move(f)); continue; }
        ++skipped[key];
        if (!sample.contains(key) || f.severity > sample[key].severity) sample[key] = f;
        dropped.push_back(std::move(f));
    }
    for (auto& [key, count] : skipped) {
        Finding f = sample[key];
        f.message = std::format("…i jeszcze {} podobnych uwag w tej części (np. {})", count, f.message);
        f.value = 0; f.limit = 0; f.fixable = false; f.op_a = f.op_b = -1;
        kept.push_back(std::move(f));
    }
    findings = std::move(kept);
    return dropped;
}

}  // namespace

Report inspect(const Project& project, const Settings& settings) {
    Report report;
    report.settings = settings;
    report.parts_checked = (int)project.parts.size();
    Collector out;
    check_shelves(project, settings, out);
    for (const auto& p : project.parts) check_holes(p, settings, out);
    check_edges(project, out);
    check_collisions(project, settings, out);
    check_hinges(project, out);
    check_sheet_fit(project, out);
    // Sumy liczone są z pełnej listy (łącznie z uwagami ukrytymi w podsumowaniu części).
    for (const auto& f : out.items) {
        if (f.severity == Severity::Error) ++report.errors;
        else if (f.severity == Severity::Warning) ++report.warnings;
        else ++report.infos;
    }
    report.hidden = limit_per_part(out.items, 4);
    std::stable_sort(out.items.begin(), out.items.end(), [](const Finding& a, const Finding& b) { return a.severity > b.severity; });
    report.findings = std::move(out.items);
    return report;
}

std::string severity_name(Severity severity) {
    return severity == Severity::Error ? "błąd" : severity == Severity::Warning ? "ostrzeżenie" : "uwaga";
}

std::string rule_title(const std::string& rule) {
    static const std::map<std::string, std::string> titles{
        {"shelf_sag", "Ugięcie półek"}, {"hole_edge", "Otwory przy krawędzi"}, {"hole_clash", "Kolizje okuć i otworów"}, {"edge_banding", "Brakujące obrzeża"},
        {"part_collision", "Kolizje części"}, {"hinge_count", "Liczba zawiasów"}, {"sheet_fit", "Formatki poza płytą"}};
    auto found = titles.find(rule);
    return found == titles.end() ? rule : found->second;
}

std::string summary_line(const Report& report) {
    if (report.findings.empty()) return "Inspektor: bez uwag — projekt gotowy do produkcji";
    auto plural = [](int n, const char* one, const char* few, const char* many) {
        int m10 = n % 10, m100 = n % 100;
        return std::format("{} {}", n, n == 1 ? one : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many);
    };
    std::vector<std::string> parts;
    if (report.errors) parts.push_back(plural(report.errors, "błąd", "błędy", "błędów"));
    if (report.warnings) parts.push_back(plural(report.warnings, "ostrzeżenie", "ostrzeżenia", "ostrzeżeń"));
    if (report.infos) parts.push_back(plural(report.infos, "uwaga", "uwagi", "uwag"));
    return "Inspektor: " + py::join(parts, " · ");
}

core::ProjectPtr apply_fixes(const Project& project, const Report& report, int* fixed) {
    auto result = std::make_shared<Project>(project);
    int count = 0;
    std::vector<const Finding*> all;
    for (const auto& f : report.findings) all.push_back(&f);
    for (const auto& f : report.hidden) all.push_back(&f);
    for (const Finding* pf : all) {
        const Finding& f = *pf;
        if (!f.fixable) continue;
        auto found = std::find_if(result->parts.begin(), result->parts.end(), [&](const Part& p) { return p.id == f.part_id; });
        if (found == result->parts.end()) continue;
        if (f.rule == "edge_banding") {
            for (int e : f.edges) {
                if (e == -2) {
                    if (!found->contour_edge) { found->contour_edge = true; ++count; }
                } else if (e >= 0 && e < 4 && found->contour.empty() && !found->edges[(size_t)e]) {
                    found->edges[(size_t)e] = true; ++count;
                }
            }
        } else if (f.rule == "hole_clash" && f.op_a >= 0 && f.op_b >= 0 && (size_t)f.op_a < found->operations.size() && (size_t)f.op_b < found->operations.size()) {
            json& a = found->operations[(size_t)f.op_a];
            json& b = found->operations[(size_t)f.op_b];
            double da = jnum(py::get(a, "depth")), db = jnum(py::get(b, "depth"));
            double allowed = found->t - report.settings.web;
            if (da + db <= allowed + EPS || da <= 0 || db <= 0) continue;
            double scale = allowed / (da + db);
            double na = std::floor(da * scale * 10) / 10, nb = std::floor(db * scale * 10) / 10;
            if (na < 3 || nb < 3) continue;
            a["depth"] = na; b["depth"] = nb;
            ++count;
        }
    }
    if (count) result->validate();
    if (fixed) *fixed = count;
    return result;
}

void report_export(const Project& project, const Report& report, const std::string& path) {
    auto esc = [](const std::string& s) { return py::html_escape(s); };
    std::string out = "<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\"><title>Inspektor produkcyjny</title><style>"
                      "@page{size:A4;margin:12mm}body{font:12px Segoe UI,Arial;color:#1d2733;margin:16px}h1{font-size:20px;margin:0 0 4px}h2{font-size:15px;margin:16px 0 6px}"
                      "table{border-collapse:collapse;width:100%}th,td{border:1px solid #b9c0c8;padding:4px 7px;text-align:left;vertical-align:top}th{background:#eef1f4}"
                      ".b{display:inline-block;border-radius:9px;padding:1px 8px;font-size:11px;color:#fff}.e{background:#c0392b}.w{background:#d68910}.i{background:#2e86c1}"
                      ".hint{color:#5b6773;font-size:11px}.kpi{display:flex;gap:10px;margin:8px 0}.kpi div{border:1px solid #c9d0d8;border-radius:6px;padding:6px 12px;background:#f7f9fb}.kpi b{display:block;font-size:18px}"
                      "</style></head><body>";
    out += "<h1>Inspektor produkcyjny — " + esc(project.name) + "</h1>";
    out += std::format("<small>Sprawdzono {} części · obciążenie półek {:g} kg/m² · dopuszczalne ugięcie L/{:g}</small>", report.parts_checked, report.settings.shelf_load_kg_m2, report.settings.sag_ratio);
    out += std::format("<div class=\"kpi\"><div><b>{}</b>błędy</div><div><b>{}</b>ostrzeżenia</div><div><b>{}</b>uwagi</div></div>", report.errors, report.warnings, report.infos);
    if (report.findings.empty()) out += "<p><b>Bez uwag.</b> Projekt przeszedł wszystkie kontrole.</p>";
    std::vector<std::string> rules;
    for (const auto& f : report.findings) if (std::find(rules.begin(), rules.end(), f.rule) == rules.end()) rules.push_back(f.rule);
    for (const auto& rule : rules) {
        out += "<h2>" + esc(rule_title(rule)) + "</h2><table><tr><th>Waga</th><th>Mebel / część</th><th>Opis</th></tr>";
        for (const auto& f : report.findings) {
            if (f.rule != rule) continue;
            const char* cls = f.severity == Severity::Error ? "e" : f.severity == Severity::Warning ? "w" : "i";
            out += std::format("<tr><td><span class=\"b {}\">{}</span></td><td>{}<br><span class=\"hint\">{}</span></td><td>{}", cls, severity_name(f.severity), esc(f.part_name), esc(f.group), esc(f.message));
            if (!f.hint.empty()) out += "<br><span class=\"hint\">→ " + esc(f.hint) + "</span>";
            out += "</td></tr>";
        }
        out += "</table>";
    }
    out += "</body></html>";
    py::write_text(path, out);
}

}  // namespace production_inspector
