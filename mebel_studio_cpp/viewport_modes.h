// Presentation visibility does not alter furniture or manufacturing data.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "core.h"

namespace viewport_modes {

inline const std::vector<std::string> XRAY_MODES = {"Cały zestaw", "Wybrany mebel", "Wybrany element", "Fronty"};
inline const std::vector<std::string> DRILL_MODES = {"Także zasłonięte", "Widoczne powierzchnie"};

std::set<std::string> xray_part_ids(core::ProjectPtr project, bool enabled = false, const std::string& scope = "Cały zestaw",
                                    const std::optional<std::string>& selected = std::nullopt);

}  // namespace viewport_modes
