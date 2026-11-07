// Rigid furniture placement and reversible, face-to-face magnetic alignment.
//
// Construction coordinates remain unchanged. A cabinet can still regenerate its
// drawers, fittings and machining after placement; production blanks stay local.
//
// Uwaga: oryginał na końcu podmienia funkcje wersjami natywnymi
// (native_model.install_construction, native_layout.install). W wersji C++ całość
// jest natywna, więc te dwa wywołania nie mają odpowiednika — tłumaczona jest
// implementacja referencyjna.
#include "furniture_layout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "configurator_accessories.h"
#include "workspace_worktops.h"

namespace furniture_layout {

using core::Part;
using core::Point;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::local_world;
using core::rotate;
using core::world;
using py::ValueError;

const std::vector<std::pair<std::string, std::string>> DIRECTIONS{{"Prawo", "x+"}, {"Lewo", "x-"}, {"Tył", "y+"}, {"Przód", "y-"}, {"Góra", "z+"}, {"Dół", "z-"}};

namespace {

using Polygon = std::vector<Point>;
using Transform = Point3 (*)(const Part&, double, double, double);

double num(const json& value) { return value.get<double>(); }

std::vector<const Part*> _parts(const Project& project, const std::string& group) {
    std::vector<const Part*> found;
    for (auto& p : project.parts) if (p.group == group) found.push_back(&p);
    if (found.empty()) throw ValueError("Wybierz istniejący mebel.");
    return found;
}

std::vector<Part*> _parts(Project& project, const std::string& group) {
    std::vector<Part*> found;
    for (auto& p : project.parts) if (p.group == group) found.push_back(&p);
    if (found.empty()) throw ValueError("Wybierz istniejący mebel.");
    return found;
}

Box _bounds(const Project& project, const std::string& group, Transform transform) {
    std::vector<Point3> vertices;
    for (const Part* p : _parts(project, group))
        for (auto& uv : p->points())
            for (double t : {0.0, p->t}) vertices.push_back(transform(*p, uv[0], uv[1], t));
    auto extra = configurator_accessories::accessory_vertices(project, group, transform == local_world);
    vertices.insert(vertices.end(), extra.begin(), extra.end());
    Point3 lo, hi;
    for (int i = 0; i < 3; ++i) {
        lo[i] = vertices.at(0)[i]; hi[i] = vertices.at(0)[i];
        for (auto& p : vertices) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
    }
    return {lo, hi};
}

ProjectPtr _placed(const Project& project, const std::string& group, const json& layout, bool validate = true) {
    ProjectPtr result = std::make_shared<Project>(project);
    for (Part* p : _parts(*result, group)) p->layout = layout;
    result = workspace_worktops::refresh_shared_worktops(result, std::set<std::string>{group}, false);
    if (validate) result->validate();
    return result;
}

bool _orthogonal(const Project& project, const std::string& group) {
    return std::abs(std::remainder(num(group_layout(project, group).at("yaw")), 90)) < 1e-7;
}

double _overlap(const Box& a, const Box& b, int axis) {
    return std::min(a.second[axis], b.second[axis]) - std::max(a.first[axis], b.first[axis]);
}

bool _intersects(const Box& a, const Box& b, double tolerance = .01) {
    for (int i = 0; i < 3; ++i) if (!(_overlap(a, b, i) > tolerance)) return false;
    return true;
}

Polygon _footprint(const Project& project, const std::string& group, const Box* box = nullptr) {
    // Irregular tops and corner carcasses occupy their contour, including its
    // empty notch. Ordinary cabinets retain a full closed envelope.
    auto parts = _parts(project, group);
    const Part* irregular = nullptr;
    for (const Part* p : parts) if (py::truthy(py::get(p->assembly, "shared_worktop"))) { irregular = p; break; }
    if (irregular == nullptr)
        for (const Part* p : parts) if (p->role == "bottom" && py::truthy(py::get(p->assembly, "corner_source"))) { irregular = p; break; }
    if (irregular) {
        Polygon out;
        for (auto& uv : irregular->points()) { Point3 w = world(*irregular, uv[0], uv[1], 0); out.push_back({w[0], w[1]}); }
        return out;
    }
    Box bounds = box ? *box : group_bounds(project, group);
    auto& [lo, hi] = bounds;
    return {{lo[0], lo[1]}, {hi[0], lo[1]}, {hi[0], hi[1]}, {lo[0], hi[1]}};
}

bool _footprint_collision(const Box& a, const Polygon& pa, const Box& b, const Polygon& pb) {
    return _intersects(a, b) && workspace_worktops::polygons_overlap(pa, pb);
}

bool _face_contact(const Polygon& pa, const Polygon& pb, const std::string& direction, double gap = 0) {
    if (direction[0] == 'z') return workspace_worktops::polygons_overlap(pa, pb);
    int axis = direction[0] == 'x' ? 0 : 1;
    double delta[2] = {0., 0.};
    delta[axis] = gap + (direction[1] == '+' ? -.02 : .02);
    Polygon moved;
    for (auto& p : pa) moved.push_back({p[0] + delta[0], p[1] + delta[1]});
    return workspace_worktops::polygons_overlap(moved, pb);
}

// (axis, side, coordinate, start, end)
struct FlatFace { int axis; char side; double coordinate, start, end; };

std::vector<FlatFace> _flat_faces(const Polygon& polygon) {
    Polygon poly = workspace_worktops::_ccw(polygon);
    std::vector<FlatFace> result;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point& a = poly[i]; const Point& b = poly[(i + 1) % poly.size()];
        double dx = b[0] - a[0], dy = b[1] - a[1];
        if (std::abs(dx) < 1e-6 && std::abs(dy) > 1e-6) result.push_back({0, dy > 0 ? '+' : '-', a[0], std::min(a[1], b[1]), std::max(a[1], b[1])});
        else if (std::abs(dy) < 1e-6 && std::abs(dx) > 1e-6) result.push_back({1, dx > 0 ? '-' : '+', a[1], std::min(a[0], b[0]), std::max(a[0], b[0])});
    }
    return result;
}

void _check_collision(const Project& project, const std::string& group, const Box& box) {
    Box source = group_bounds(project, group);
    double delta[2] = {box.first[0] - source.first[0], box.first[1] - source.first[1]};
    Polygon polygon;
    for (auto& p : _footprint(project, group, &source)) polygon.push_back({p[0] + delta[0], p[1] + delta[1]});
    for (const std::string& other : groups(project)) {
        Box other_box = group_bounds(project, other);
        if (other != group && _footprint_collision(box, polygon, other_box, _footprint(project, other, &other_box)))
            throw ValueError("Ustawienie nachodzi na mebel „" + other + "”. Zmień stronę, wysokość lub odstęp.");
    }
}

Box _translated(const Box& box, const double delta[3]) {
    Box out;
    for (int i = 0; i < 3; ++i) { out.first[i] = box.first[i] + delta[i]; out.second[i] = box.second[i] + delta[i]; }
    return out;
}

int axis_index(char c) { return c == 'x' ? 0 : c == 'y' ? 1 : 2; }

double center_of(const Box& box, int i) { return (box.first[i] + box.second[i]) / 2; }

}  // namespace

ProjectPtr placed(const Project& project, const std::string& group, const json& layout, bool validate) { return _placed(project, group, layout, validate); }

std::vector<std::string> groups(const Project& project) {
    std::vector<std::string> out;
    for (auto& p : project.parts) if (std::find(out.begin(), out.end(), p.group) == out.end()) out.push_back(p.group);
    return out;
}

json group_layout(const Project& project, const std::string& group) {
    auto parts = _parts(project, group);
    json layout = json::object();
    for (const char* key : {"x", "y", "z", "yaw"}) layout[key] = py::get(parts[0]->layout, key, 0);
    for (const Part* p : parts)
        for (auto& [key, value] : layout.items())
            if (std::abs(num(py::get(p->layout, key, 0)) - num(value)) > 1e-7)
                throw ValueError("Elementy mebla mają różne ustawienia zestawu. Przypisz je do osobnych mebli lub wyrównaj ich ustawienie.");
    return layout;
}

Box group_bounds(const Project& project, const std::string& group) {
    // Nominal closed cabinet envelope in the room, including fronts and backs.
    return _bounds(project, group, world);
}

Box local_group_bounds(const Project& project, const std::string& group) { return _bounds(project, group, local_world); }

ProjectPtr move_group(const Project& project, const std::string& group, double dx, double dy, double dz, bool validate) {
    // Move one cabinet in room axes, atomically and without altering blanks.
    json layout = group_layout(project, group);
    const char* axes[3] = {"x", "y", "z"}; double deltas[3] = {dx, dy, dz};
    for (int i = 0; i < 3; ++i) {
        double current = num(layout[axes[i]]);
        layout[axes[i]] = finite(current + finite(deltas[i]));
    }
    return _placed(project, group, layout, validate);
}

ProjectPtr rotate_group(const Project& project, const std::string& group, double angle_, const std::optional<std::vector<double>>& pivot_) {
    // Rotate around room Z; default pivot is the cabinet's current centre.
    json layout = group_layout(project, group);
    double angle = finite(angle_);
    Box box = group_bounds(project, group);
    std::vector<double> pivot;
    if (!pivot_) { for (int i = 0; i < 3; ++i) pivot.push_back((box.first[i] + box.second[i]) / 2); }
    else pivot = *pivot_;
    if (pivot.size() != 3) throw ValueError("Punkt obrotu wymaga współrzędnych X, Y, Z.");
    for (double& v : pivot) v = finite(v);
    const char* axes[3] = {"x", "y", "z"};
    Point3 offset;
    for (int i = 0; i < 3; ++i) offset[i] = num(layout[axes[i]]) - pivot[i];
    Point3 shift = rotate(offset, 0, 0, angle);
    for (int i = 0; i < 3; ++i) layout[axes[i]] = finite(pivot[i] + shift[i]);
    layout["yaw"] = py::mod(num(layout["yaw"]) + angle + 180, 360) - 180;
    if (std::abs(num(layout["yaw"])) < 1e-9) layout["yaw"] = 0;
    return _placed(project, group, layout);
}

ProjectPtr snap_group(const Project& project, const std::string& group, const std::string& target_group, const std::string& direction, const json& align, const json& gap_) {
    // Place a cabinet against a target face; sides refer to room axes.
    if (group == target_group) throw ValueError("Wybierz drugi mebel do połączenia.");
    bool known = false; for (auto& [label, value] : DIRECTIONS) if (value == direction) known = true;
    if (!known) throw ValueError("Strona połączenia: x+, x-, y+, y-, z+ albo z-.");
    if (!_orthogonal(project, group) || !_orthogonal(project, target_group))
        throw ValueError("Magnes ścian wymaga obrotów mebli o wielokrotność 90°. Dla innego kąta ustaw pozycję ręcznie.");
    double gap = finite(gap_, 0, 10000);
    Box source = group_bounds(project, group), target = group_bounds(project, target_group);
    int axis = axis_index(direction[0]); bool positive = direction[1] == '+';
    double delta[3] = {0., 0., 0.};
    delta[axis] = (positive ? target.second[axis] : target.first[axis]) + (positive ? gap : -gap) - (positive ? source.first[axis] : source.second[axis]);
    for (int other_axis = 0; other_axis < 3; ++other_axis) {
        if (other_axis == axis) continue;
        json mode = align.is_object() ? py::get(align, std::string(1, "xyz"[other_axis]), "start") : align;
        if (mode == "center") delta[other_axis] = center_of(target, other_axis) - center_of(source, other_axis);
        else if (mode == "start") delta[other_axis] = target.first[other_axis] - source.first[other_axis];
        else if (mode == "end") delta[other_axis] = target.second[other_axis] - source.second[other_axis];
        else if (mode == "keep") {}
        else throw ValueError("Wyrównanie: start, center, end albo keep.");
    }
    Box moved = _translated(source, delta);
    _check_collision(project, group, moved);
    return move_group(project, group, delta[0], delta[1], delta[2]);
}

std::pair<ProjectPtr, json> nearest_snap(const Project& project, const std::string& group, const json& tolerance_, bool align, bool validate) {
    // Snap a near face and optionally its nearby edges; never move far away.
    double tolerance = finite(tolerance_, 0, 1000);
    Box source = group_bounds(project, group);
    if (!_orthogonal(project, group)) return {std::make_shared<Project>(project), json(nullptr)};
    std::vector<std::pair<std::string, Box>> envelopes;  // dict z kolejnością wstawiania
    for (auto& name : groups(project)) envelopes.push_back({name, group_bounds(project, name)});
    std::map<std::string, bool> orthogonal;
    for (auto& [name, box] : envelopes) orthogonal[name] = _orthogonal(project, name);
    std::map<std::string, Polygon> footprints;
    for (auto& [name, box] : envelopes) footprints[name] = _footprint(project, name, &box);
    std::map<std::string, std::vector<FlatFace>> flat_faces;
    for (auto& [name, poly] : footprints) flat_faces[name] = _flat_faces(poly);

    struct Candidate { double travel; std::string other, direction; double delta[3]; json info; };
    std::vector<Candidate> candidates;
    // (direction, distance, face_range)
    struct FaceRange { int axis; double start, end, source_start, source_end; };
    struct Proposal { std::string direction; double distance; std::optional<FaceRange> face_range; };

    for (auto& [other, target] : envelopes) {
        if (other == group || !orthogonal[other]) continue;
        std::vector<Proposal> proposals;
        for (const FlatFace& face : flat_faces[other]) {
            for (const FlatFace& own : flat_faces[group]) {
                if (face.axis != own.axis || face.side == own.side) continue;
                if (std::min(face.end, own.end) - std::max(face.start, own.start) < -tolerance) continue;
                proposals.push_back({std::string(1, "xyz"[face.axis]) + face.side, face.coordinate - own.coordinate, FaceRange{1 - face.axis, face.start, face.end, own.start, own.end}});
            }
        }
        for (std::string direction : {"z+", "z-"}) {
            proposals.push_back({direction, (direction[1] == '+' ? target.second[2] : target.first[2]) - (direction[1] == '+' ? source.first[2] : source.second[2]), std::nullopt});
        }
        for (const Proposal& proposal : proposals) {
            const std::string& direction = proposal.direction; double distance = proposal.distance;
            int axis = axis_index(direction[0]);
            if (std::abs(distance) > tolerance + 1e-8) continue;
            std::vector<int> tangent; for (int i = 0; i < 3; ++i) if (i != axis) tangent.push_back(i);
            double delta[3] = {0., 0., 0.}; delta[axis] = distance;
            bool too_far = false; for (int i : tangent) if (_overlap(source, target, i) < -tolerance) too_far = true;
            if (too_far) continue;
            if (align) {
                for (int i : tangent) {
                    std::vector<double> possibilities;
                    if (proposal.face_range && proposal.face_range->axis == i) {
                        const FaceRange& range = *proposal.face_range;
                        possibilities = {range.start - range.source_start, range.end - range.source_end, (range.start + range.end - range.source_start - range.source_end) / 2};
                    } else {
                        possibilities = {target.first[i] - source.first[i], target.second[i] - source.second[i]};
                        possibilities.push_back(center_of(target, i) - center_of(source, i));
                    }
                    double correction = possibilities[0];
                    for (double candidate : possibilities) if (std::abs(candidate) < std::abs(correction)) correction = candidate;
                    if (std::abs(correction) <= tolerance) delta[i] = correction;
                }
            }
            Box moved = _translated(source, delta);
            Polygon polygon; for (auto& p : footprints[group]) polygon.push_back({p[0] + delta[0], p[1] + delta[1]});
            bool overlaps = false; for (int i : tangent) if (_overlap(moved, target, i) <= .01) overlaps = true;
            if (overlaps) continue;
            if (!_face_contact(polygon, footprints[other], direction)) continue;
            bool collides = false;
            for (auto& [other_group, box] : envelopes)
                if (other_group != group && _footprint_collision(moved, polygon, box, footprints[other_group])) { collides = true; break; }
            if (collides) continue;
            double travel = std::sqrt(py::sum({delta[0] * delta[0], delta[1] * delta[1], delta[2] * delta[2]}));
            json info = {{"group", group}, {"target_group", other}, {"direction", direction}, {"delta", json::array({delta[0], delta[1], delta[2]})}, {"distance", travel}};
            Candidate candidate{travel, other, direction, {delta[0], delta[1], delta[2]}, info};
            candidates.push_back(candidate);
        }
    }
    if (candidates.empty()) return {std::make_shared<Project>(project), json(nullptr)};
    const Candidate* best = &candidates[0];
    for (const Candidate& c : candidates)
        if (std::tie(c.travel, c.other, c.direction) < std::tie(best->travel, best->other, best->direction)) best = &c;
    return {move_group(project, group, best->delta[0], best->delta[1], best->delta[2], validate), best->info};
}

std::pair<ProjectPtr, bool> resolve_overlap(const Project& project, const std::string& group) {
    // Wypchnięcie z sąsiednich mebli: jeśli przesuwany mebel wszedł w inny, ślizga się po jego ściance
    // (najmniejsze przesunięcie w osi X albo Y), zamiast przenikać. Dopiero potem działa magnes.
    if (!_orthogonal(project, group)) return {std::make_shared<Project>(project), false};
    Box source = group_bounds(project, group);
    Polygon footprint = _footprint(project, group, &source);
    struct Other { Box box; Polygon polygon; };
    std::vector<Other> others;
    for (auto& name : groups(project)) {
        if (name == group) continue;
        Box box = group_bounds(project, name);
        others.push_back({box, _footprint(project, name, &box)});
    }
    double total[3] = {0., 0., 0.};
    bool changed = false, clear = false;
    for (int iteration = 0; iteration < 8; ++iteration) {
        Box current = _translated(source, total);
        Polygon polygon;
        for (auto& p : footprint) polygon.push_back({p[0] + total[0], p[1] + total[1]});
        const Other* hit = nullptr;
        for (const Other& other : others)
            if (_footprint_collision(current, polygon, other.box, other.polygon)) { hit = &other; break; }
        if (hit == nullptr) { clear = true; break; }
        double best = 1e18, best_push = 0;
        int best_axis = 0;
        for (int axis = 0; axis < 2; ++axis) {
            for (double push : {hit->box.second[axis] - current.first[axis], hit->box.first[axis] - current.second[axis]}) {
                if (std::abs(push) < best) { best = std::abs(push); best_push = push; best_axis = axis; }
            }
        }
        total[best_axis] += best_push;
        changed = true;
    }
    if (!changed || !clear) return {std::make_shared<Project>(project), false};
    return {move_group(project, group, total[0], total[1], total[2], false), true};
}

json contacts(const Project& project, const std::optional<std::string>& group, const json& tolerance_) {
    // Current geometric contacts; separation or movement releases them freely.
    double tolerance = finite(tolerance_, 0, 1000);
    auto names = groups(project); json result = json::array();
    std::map<std::string, Box> envelopes; std::map<std::string, bool> orthogonal; std::map<std::string, Polygon> footprints; std::map<std::string, std::vector<FlatFace>> flat_faces;
    for (auto& name : names) envelopes[name] = group_bounds(project, name);
    for (auto& name : names) orthogonal[name] = _orthogonal(project, name);
    for (auto& name : names) footprints[name] = _footprint(project, name, &envelopes[name]);
    for (auto& name : names) flat_faces[name] = _flat_faces(footprints[name]);
    for (size_t index = 0; index < names.size(); ++index) {
        const std::string& first = names[index];
        for (size_t j = index + 1; j < names.size(); ++j) {
            const std::string& second = names[j];
            if (group && *group != first && *group != second) continue;
            if (!orthogonal[first] || !orthogonal[second]) continue;
            const Box& a = envelopes[first]; const Box& b = envelopes[second];
            std::vector<std::pair<std::string, double>> proposals;
            for (const FlatFace& face : flat_faces[second]) {
                for (const FlatFace& own : flat_faces[first]) {
                    if (face.axis != own.axis || face.side == own.side) continue;
                    if (std::min(face.end, own.end) - std::max(face.start, own.start) <= .01) continue;
                    proposals.push_back({std::string(1, "xyz"[face.axis]) + face.side, face.coordinate - own.coordinate});
                }
            }
            for (std::string direction : {"z+", "z-"})
                proposals.push_back({direction, (direction[1] == '+' ? b.second[2] : b.first[2]) - (direction[1] == '+' ? a.first[2] : a.second[2])});
            std::set<std::string> seen;
            for (auto& [direction, gap] : proposals) {
                int axis = axis_index(direction[0]);
                if (seen.contains(direction) || !(std::abs(gap) <= tolerance)) continue;
                bool overlapping = true; for (int i = 0; i < 3; ++i) if (i != axis && !(_overlap(a, b, i) > .01)) overlapping = false;
                if (overlapping && _face_contact(footprints[first], footprints[second], direction, gap)) {
                    result.push_back({{"group", first}, {"target_group", second}, {"direction", direction}, {"gap", std::abs(gap)}});
                    seen.insert(direction);
                }
            }
        }
    }
    return result;
}

}  // namespace furniture_layout
