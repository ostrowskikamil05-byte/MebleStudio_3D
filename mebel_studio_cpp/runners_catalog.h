// Sourced drawer-runner planning dimensions, in millimetres.
//
// The width of an undermount runner refers to the INSIDE of the drawer side.
// Purchased fittings are not manufactured by this program.  This catalogue does
// not contain CNC drilling patterns, rear notches, clip machining or SKU meshes.
//
// Różnice względem Pythona: słowniki z kluczami liczbowymi (depth_by_length,
// article_by_length, load_by_length, travel_by_length, source_by_length) są
// zapisane w JSON z kluczami tekstowymi ("400"); install_construction (podmiana
// funkcji na wersję z biblioteki C++) nie istnieje — obliczenia są skompilowane
// bezpośrednio.
#pragma once
#include <string>
#include "core.h"

namespace runners_catalog {

using json = core::json;

extern const std::string CHECKED_ON;
extern const json PROFILES;  // dict id -> profil, w kolejności wstawiania
extern const json& RUNNER_PROFILES;

// Return an independent serializable profile, accepting its id or mapping.
json get_profile(const json& profile);

// Calculate planning sizes without silently guessing missing product data.
// `cabinet_depth` is usable INSIDE depth, not the total outside cabinet depth.
// `drawer_front_thickness` is the inner box front, not the decorative overlay.
json calculate_runner(const json& profile, const json& cabinet_inside_width, const json& nominal_length, const json& side_thickness,
                      const json& cabinet_depth, const std::string& construction = "three_sided", const json& drawer_front_thickness = nullptr,
                      const json& confirmed_min_depth = nullptr);

// Verified-depth fitting variants; unknown-depth variants are not selected.
json available_lengths(const json& profile, const json& cabinet_depth);

}  // namespace runners_catalog
