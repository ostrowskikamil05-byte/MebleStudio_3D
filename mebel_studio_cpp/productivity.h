// Independent part transforms, arrays and reusable furniture project templates.
#pragma once
#include <string>
#include "core.h"

namespace productivity {

using json = core::json;

core::Part independent_copy(const core::Part& part);

// count: Python przyjmuje int/float; liczba kopii 1–100.
core::ProjectPtr part_array(const core::Project& project, const std::string& part_id, const json& count, const json& dx, const json& dy, const json& dz);

// axis: "u" albo "v"
core::Part mirror_part(const core::Part& part, const std::string& axis);

core::ProjectPtr extract_group(const core::Project& project, const std::string& group);

// offset: trzy liczby (X, Y, Z)
core::ProjectPtr insert_template(const core::Project& project, const core::Project& template_, const std::string& name, const json& offset = json::array({0, 0, 0}));

core::ProjectPtr equal_shelves(const core::Project& project, const std::string& group, const json& count, double thickness, const std::string& material);

}  // namespace productivity
