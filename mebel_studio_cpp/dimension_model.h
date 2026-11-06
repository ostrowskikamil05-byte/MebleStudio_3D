// Shared dimension edits and fit operations for sketch, forms and 3D.
#pragma once
#include <optional>
#include <string>
#include "core.h"

namespace dimension_model {

using json = core::json;

core::ProjectPtr resize_dimensions(const core::Project& project, const std::string& part_id, const json& u, const json& v, const json& t);

core::ProjectPtr fit_part(const core::Project& project, const std::string& part_id);

core::ProjectPtr regenerate_hinges(const core::Project& project, const std::string& part_id, const json& pattern = "hinge45", const json& count = 2,
                                   const std::optional<std::string>& side = std::nullopt);

}  // namespace dimension_model
