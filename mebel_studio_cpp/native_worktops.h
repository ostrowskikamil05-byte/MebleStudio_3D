// Native shared-worktop solver with the original editor lifecycle retained.
//
// Różnica względem Pythona: install(namespace) (podmiana _distance/_offset/
// _union/_geometry w workspace_worktops) nie istnieje — solver jest wywoływany
// bezpośrednio. Funkcje poniżej mają te same sygnatury co implementacje
// podmieniane w Pythonie i kierują wywołania do biblioteki natywnej.
#pragma once
#include <string>
#include <tuple>
#include <vector>
#include "core.h"

namespace native_worktops {

using json = core::json;
using Polygon = std::vector<core::Point>;

extern const std::vector<std::string> OPERATIONS;

json _polygon(const Polygon& points);

// Odpowiednik install(): sprawdza dostępność biblioteki i wersję API solvera
// (zwraca false, gdy biblioteki brak; RuntimeError, gdy wersja nie pasuje).
bool install();

double distance(const Polygon& a, const Polygon& b);
Polygon offset(const Polygon& poly, double amount);
Polygon geometry_union(const std::vector<Polygon>& polygons);

// Zwraca (kontur, początek, kotwice).
std::tuple<Polygon, core::Point3, json> geometry(const core::Project& project, const std::vector<std::string>& groups, const json& config);

}  // namespace native_worktops
