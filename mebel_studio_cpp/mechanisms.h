// Presentation-only opening transforms: production model remains closed.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace mechanisms {

using json = core::json;

// dict etykieta -> tryb (kolejność jak w Pythonie)
extern const std::vector<std::pair<std::string, std::string>> MODES;

json effective_motion(const core::Part& part);

core::ProjectPtr posed_project(const core::ProjectPtr& project, const std::map<std::string, double>& amounts);

core::ProjectPtr set_motion(const core::Project& project, const std::string& part_id, const std::string& mode,
                            const json& angle = 100, const json& travel = 400);

void refresh_mechanism_hardware(core::Project& project, const std::string& part_id);

}  // namespace mechanisms
