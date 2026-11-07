// Rigid furniture placement and reversible, face-to-face magnetic alignment.
//
// Construction coordinates remain unchanged. A cabinet can still regenerate its
// drawers, fittings and machining after placement; production blanks stay local.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace furniture_layout {

using json = core::json;

// Para (min, max) — krotka dwóch punktów 3D z Pythona.
using Box = std::pair<core::Point3, core::Point3>;

// DIRECTIONS = {'Prawo': 'x+', ...} (kolejność jak w Pythonie)
extern const std::vector<std::pair<std::string, std::string>> DIRECTIONS;

// Publiczny dostęp do wewnętrznego _placed (używany przez native_layout).
core::ProjectPtr placed(const core::Project& project, const std::string& group, const json& layout, bool validate = true);

std::vector<std::string> groups(const core::Project& project);

// {'x':..., 'y':..., 'z':..., 'yaw':...}
json group_layout(const core::Project& project, const std::string& group);

// Nominal closed cabinet envelope in the room, including fronts and backs.
Box group_bounds(const core::Project& project, const std::string& group);

Box local_group_bounds(const core::Project& project, const std::string& group);

// Move one cabinet in room axes, atomically and without altering blanks.
core::ProjectPtr move_group(const core::Project& project, const std::string& group, double dx = 0, double dy = 0, double dz = 0, bool validate = true);

// Rotate around room Z; default pivot is the cabinet's current centre.
core::ProjectPtr rotate_group(const core::Project& project, const std::string& group, double angle, const std::optional<std::vector<double>>& pivot = std::nullopt);

// Place a cabinet against a target face; sides refer to room axes.
// start/centre/end aligns the remaining axes; a dict can specify per-axis alignment.
core::ProjectPtr snap_group(const core::Project& project, const std::string& group, const std::string& target_group, const std::string& direction = "x+",
                            const json& align = "start", const json& gap = 0);

// Snap a near face and optionally its nearby edges; never move far away.
// Returns (candidate, information) or an unchanged copy with None (null).
std::pair<core::ProjectPtr, json> nearest_snap(const core::Project& project, const std::string& group, const json& tolerance = 80, bool align = true, bool validate = true);
// Rząd szafek stykających się bokami wzdłuż X: {count, from, to, length}; null, gdy nie ma takiego mebla.
json row_extent(const core::Project& project, const std::string& group);
// Wypycha mebel z przenikania innych mebli (ślizg po ściance); drugi element = czy coś przesunięto.
std::pair<core::ProjectPtr, bool> resolve_overlap(const core::Project& project, const std::string& group);

// Current geometric contacts; separation or movement releases them freely.
// group == None  ->  std::nullopt
json contacts(const core::Project& project, const std::optional<std::string>& group = std::nullopt, const json& tolerance = 0.1);

}  // namespace furniture_layout
