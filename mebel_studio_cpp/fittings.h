// Source-linked fitting recipes. Generic recipes are explicitly marked.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace fittings {

using json = core::json;

extern const std::string SYSTEM32_SOURCE;
extern const std::string HINGE_SOURCE;
extern const std::string RUNNER_SOURCE;
extern const std::string CONNECTOR_SOURCE;
extern const json RECIPES;
// [('Gałka', 0), ('Uchwyt 64', 64), ...]
extern const std::vector<std::pair<std::string, int>> HANDLES;
// dict nazwa -> kolor (kolejność jak w Pythonie)
extern const json FINISHES;

// deepcopy(next(r for r in RECIPES if r['id']==recipe_id)); StopIteration, gdy brak.
json recipe(const json& recipe_id);

core::ProjectPtr system32(const core::Project& project, const std::string& part_id, const json& front = 37, const json& back = 37, const json& start = 64,
                          const json& end = 64, const json& diameter = 5, const json& depth = 10, bool both = true, const std::string& face = "top");

// u, v == None  ->  nullptr
core::ProjectPtr add_handle(const core::Project& project, const std::string& part_id, const json& spacing = 128, const json& u = nullptr, const json& v = nullptr,
                            const json& vertical = true, const json& color = "#242628", const json& projection = 28);

// Drill mating structural panels in world coordinates, axis aligned cabinet only.
core::ProjectPtr cabinet_connectors(const core::Project& project, const std::string& group, const json& recipe_id = "confirmat7", const json& front = 50, const json& back = 50);

void refresh_handles(core::Project& project, const std::string& part_id);

}  // namespace fittings
