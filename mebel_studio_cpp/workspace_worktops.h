// One real, persistent worktop over connected cabinet footprints.
//
// Polygon union and fitting run offline without an additional CAD package. A
// linked top keeps its previous geometry when a support is missing, separated or
// manually edited; its status records why an explicit fit cannot be completed.
//
// Różnica względem Pythona: podmiana funkcji na końcu modułu (_install_native_geometry,
// _install_native_worktops) jest tu rozstrzygana przy pierwszym użyciu — gdy biblioteka
// C++ jest dostępna, polygons_overlap i solver (_distance/_offset/_union/_geometry)
// wywołują ją, w przeciwnym razie działa kod z tego pliku.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core.h"

namespace workspace_worktops {

using json = core::json;
using Point = core::Point;
using Polygon = std::vector<core::Point>;

double _signed(const Polygon& poly);
Polygon _ccw(const Polygon& poly);

// Positive area intersection, excluding a touching edge or a vertex.
bool polygons_overlap(const Polygon& a, const Polygon& b);

// Zwraca (obrys, z, id kotwicy).
std::tuple<Polygon, double, std::string> _body(const core::Project& project, const std::string& group);

// hashlib.sha256(json.dumps(data, sort_keys=True)).hexdigest()
std::string _signature(const core::Part& part);

json read_shared_worktop(const core::Project& project, const std::string& group);

// Zwraca (projekt, nazwa grupy blatu).
std::pair<core::ProjectPtr, std::string> install_shared_worktop(
    const core::Project& project, const std::vector<std::string>& groups, double thickness = 38, double overhang = 20,
    const std::optional<std::string>& material = std::nullopt, const std::string& name = "Wspólny blat", bool replace_individual = true);

core::ProjectPtr fit_shared_worktop(const core::Project& project, const std::string& group);

// changed_groups == None  ->  std::nullopt
core::ProjectPtr refresh_shared_worktops(core::ProjectPtr project, const std::optional<std::set<std::string>>& changed_groups, bool validate = true);
core::ProjectPtr refresh_shared_worktops(core::ProjectPtr project, bool validate = true);

}  // namespace workspace_worktops
