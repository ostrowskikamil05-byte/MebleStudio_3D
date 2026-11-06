// Apply a sourced planning profile to a managed wooden drawer.
#pragma once
#include <string>
#include "core.h"

namespace runner_installation {

using json = core::json;

// Clear closed-box envelope, including inset backs, in world millimetres.
//
// The front offset and rear safety gap are deducted exactly once. A back
// outside the carcass does not reduce its depth. Only backs intersecting the
// planned drawer's X/Z envelope constrain that drawer.
json drawer_depth_context(const core::Project& project, const core::Part& part, const json& config = nullptr);

// confirmed_min_depth jest argumentem nazwanym (po `*`) w Pythonie.
core::ProjectPtr apply_runner(const core::Project& project, const std::string& part_id, const json& profile_id, const json& nominal_length,
                              const json& side_thickness = 16, const json& bottom_thickness = 6, const json& recess = 12,
                              const json& confirmed_min_depth = nullptr);

}  // namespace runner_installation
