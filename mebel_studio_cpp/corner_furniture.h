// Editable open corner carcasses with real planar production parts.
//
// The concave inner corner can remain square, be chamfered or be rounded.
// Rounded outlines are explicitly tessellated; no diagonal hinge standard is
// invented and this factory does not automatically attach a diagonal front.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "assembly.h"
#include "core.h"

namespace corner_furniture {

using json = core::json;

json default_corner_config(const core::Project* project = nullptr, const std::string& furniture_type = "cabinet");

json normalize_corner_config(const json& config, const core::Project* project = nullptr);

std::vector<core::Point> corner_outline(double width, double depth, double arm_depth, const std::string& shape = "L", double radius = 100, const std::string& side = "left",
                                        int segments = 12, std::optional<double> arm_depth_y = std::nullopt, double diagonal_angle = 45);

json read_corner_config(const core::Project& project, const std::optional<std::string>& group);

std::optional<std::pair<core::Point3, core::Point3>> corner_bounds(const core::Project& project, const std::string& group);

json corner_status(const core::Project& project, const std::string& group);

// The front's U/depth/Z frame, with explicit construction-space basis.
assembly::Frame corner_front_frame(const core::Project& project, const core::Part& part);

void _corner_hinges(core::Project& project, core::Part& front, const json& pattern, const json& count, const json& price = 0, bool manual = false);

core::ProjectPtr regenerate_corner_hinges(const core::Project& project, const std::string& part_id, const json& pattern = "hinge45", const json& count = 2,
                                          const json& price = 0, bool manual = false, bool validate = true);

void _front_finish(core::Project& project, core::Part& front, const json& config);

// Explicitly restore this managed component to its corner parameters.
core::ProjectPtr fit_corner_part(const core::Project& project, const std::string& part_id);

std::pair<core::ProjectPtr, std::string> create_corner(const core::Project& project, const json& config, const std::optional<std::string>& group = std::nullopt);

core::ProjectPtr resize_corner_furniture(const core::Project& project, const std::string& group, const json& width, const json& height, const json& depth, const json& origin = nullptr);

// Convert a recognised, unmodified cabinet in one atomic transaction.
std::pair<core::ProjectPtr, std::string> convert_to_corner(const core::Project& project, const std::string& group, const std::string& shape = "L", const std::string& side = "left",
                                                          const json& arm_depth = nullptr, const json& width = nullptr, const json& depth = nullptr,
                                                          const std::string& fronts = "orthogonal", const json& changes = json::object());

}  // namespace corner_furniture
