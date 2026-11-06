#pragma once
#include "core.hpp"

// Material presets with editable workshop thickness choices
// Forward declaration
Project apply_material_preset(Project project, const std::string& part_id, 
                              const std::string& name, double thickness);
