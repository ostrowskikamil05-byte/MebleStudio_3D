// Atomic direct edits and furniture resizing with thickness preservation.
#pragma once
#include <string>
#include "core.h"

namespace direct_edit {

using json = core::json;

core::ProjectPtr resize_part(const core::Project& project, const std::string& part_id, const std::string& face, const json& distance);

core::ProjectPtr resize_furniture(const core::Project& project, const std::string& group, double width, double height, double depth, const json& origin = nullptr);

core::ProjectPtr resize_furniture_face(const core::Project& project, const std::string& group, const std::string& face, const json& distance);

}  // namespace direct_edit
