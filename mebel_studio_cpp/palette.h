// Editable decorative palette. Approximate screen colours, no vendor colour certification.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace palette {

extern const std::vector<std::pair<std::string, std::string>> PALETTE;

// Dodaje brakujące materiały „Dekor — …”; zwraca ten sam projekt.
core::ProjectPtr install_palette(core::ProjectPtr project);

}  // namespace palette
