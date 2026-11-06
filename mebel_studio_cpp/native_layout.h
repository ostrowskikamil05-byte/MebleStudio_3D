// Batch placement geometry in C++; retain editable project/CAD publication.
//
// The wire scene contains nominal panel points and one owner-tagged accessory
// geometry pass. Transform, envelope, footprint, magnet and contact loops execute
// inside the native library. No mutable Project receives adapter attributes.
//
// Różnica względem Pythona: install(namespace) (podmiana funkcji furniture_layout
// w Pythonie) nie istnieje — algorytmy układu są skompilowane bezpośrednio w
// furniture_layout.cpp. Funkcje poniżej to warianty kierujące wywołania do
// biblioteki natywnej (native_model::call) z tymi samymi sygnaturami.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "furniture_layout.h"

namespace native_layout {

using json = core::json;

extern const std::vector<std::string> OPERATIONS;

// JSON rejects NaN before entering the DLL: a string lets the native finite
// check reject it at the same point in the operation as the legacy code.
json scalar(double value);
json scalar(const json& value);

// Extract once, without validation, world transforms or panel CAD meshes.
json scene(const core::Project& project, bool geometry = false, const std::optional<std::string>& group = std::nullopt);

std::vector<std::string> groups(const core::Project& project);
json group_layout(const core::Project& project, const std::string& group);
furniture_layout::Box group_bounds(const core::Project& project, const std::string& group);
furniture_layout::Box local_group_bounds(const core::Project& project, const std::string& group);
core::ProjectPtr move_group(const core::Project& project, const std::string& group, double dx = 0, double dy = 0, double dz = 0, bool validate = true);
core::ProjectPtr rotate_group(const core::Project& project, const std::string& group, double angle, const std::optional<std::vector<double>>& pivot = std::nullopt);
core::ProjectPtr snap_group(const core::Project& project, const std::string& group, const std::string& target_group, const std::string& direction = "x+",
                            const json& align = "start", const json& gap = 0);
std::pair<core::ProjectPtr, json> nearest_snap(const core::Project& project, const std::string& group, const json& tolerance = 80, bool align = true,
                                               bool validate = true);
json contacts(const core::Project& project, const std::optional<std::string>& group = std::nullopt, const json& tolerance = 0.1);

}  // namespace native_layout
