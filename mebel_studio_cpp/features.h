// Manufacturing geometry descriptions in a part's local coordinates.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace features {

using json = core::json;

extern const std::vector<std::pair<std::string, std::string>> FACES;

void validate_drill(const core::Part& part, const json& op);

std::pair<core::Point3, core::Point3> drill_location(const core::Part& part, const json& op);

void validate_cut(const core::Part& part, const json& cut);

json hole_array(const core::Part& part, double u, double v, double diameter, double depth, const json& nu = 1, const json& nv = 1, double du = 32, double dv = 32, const std::string& face = "top");

core::ProjectPtr replace_part(const core::Project& project, const core::Part& part);

// Keep drilling coordinates attached to material when a local origin moves.
void shift_features(core::Part& part, double du = 0, double dv = 0, double dt = 0);

}  // namespace features
