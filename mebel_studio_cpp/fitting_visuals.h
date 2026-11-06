// Simple fitting geometry for visualisation; purchased items, not manufactured panels.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "core.h"

namespace fitting_visuals {

using Faces = std::vector<std::vector<core::Point3>>;

// (owner, face, color) — krotka zwracana przez fitting_faces(...)
struct FittingFace {
    std::string owner;
    std::vector<core::Point3> face;
    std::string color;
};

// (owner, face, color, sign) — fitting_faces(..., with_metadata=True)
struct FittingFaceMeta {
    std::string owner;
    std::vector<core::Point3> face;
    std::string color;
    int sign;
};

Faces box(double x, double y, double z, double u, double v, double t);

// kinds == None  ->  std::nullopt
std::vector<FittingFace> fitting_faces(const core::Project& project, const std::optional<std::set<std::string>>& kinds = std::nullopt);
std::vector<FittingFaceMeta> fitting_faces_with_metadata(const core::Project& project, const std::optional<std::set<std::string>>& kinds = std::nullopt);

// Lightweight circular bought fitting; no CAD part or cutting blank.
Faces cylinder(double x, double y, double z, double r, double height, int segments = 16);

// Capsule prism in a fitting's local U,V,T, matching a slot-shaped groove.
Faces slot_prism(double u, double v, double z, double length, double width, double height, bool vertical = false, int segments = 8);

Faces rod(const core::Point3& a, const core::Point3& b, double r);

}  // namespace fitting_visuals
