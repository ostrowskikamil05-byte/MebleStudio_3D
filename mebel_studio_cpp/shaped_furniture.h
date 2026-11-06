// Persistent curved outlines and dimension-driven furniture starting points.
//
// Drawing contours are tessellated. CAD solids use exact circles/ellipses/arcs.
// Furniture presets are design starting points, not tested structural plans.
//
// Argumenty nazwane Pythona (radii, segments, arch_rise, taper) są przekazywane
// jako obiekt JSON `options`; nieznany klucz daje TypeError jak w Pythonie.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "assembly.h"
#include "core.h"

namespace shaped_furniture {

using json = core::json;

extern const std::vector<std::pair<std::string, std::string>> SHAPES;
extern const std::vector<std::pair<std::string, std::vector<int>>> THICKNESSES;
extern const std::string CURVE_NOTE;

// Return a local normalized profile; radii: LL, LR, UR, UL in U/V.
std::vector<std::vector<double>> contour(const json& kind, double u, double v, const json& options = json::object());

// Canonical JSON-safe definition. Corner radii are fixed millimetres.
json shape_definition(const json& kind, double u, double v, const json& options = json::object());

// Update outline before assigning U/V; preserve radii and arch rise.
void resize_profile(core::Part& part, double u, double v);

double profile_area(const json& profile, double u, double v);
double profile_perimeter(const json& profile, double u, double v);

// Rebuild tagged flutes; keep manually created machining unchanged.
void refresh_decor(core::Part& part);
void remove_decor(core::Part& part);

// Atomic edit, preserving meaningful drills, linked fittings and dependencies.
core::ProjectPtr apply_shape(const core::Project& project, const std::string& part_id, const std::string& kind, const json& u = nullptr, const json& v = nullptr,
                             const json& thickness = nullptr, const json& options = json::object());

struct Template {
    std::string category, name;
    double width, height, depth;
    std::string kind, style, description;
};
extern const std::vector<Template> TEMPLATES;

// Build a furniture concept with dimensioned manufacturable planar parts.
std::pair<core::ProjectPtr, std::string> create_shaped_furniture(const core::Project& project, long long index, std::optional<double> width = std::nullopt,
                                                                 std::optional<double> height = std::nullopt, std::optional<double> depth = std::nullopt,
                                                                 double thickness = 28, core::Point3 origin = {0, 0, 0},
                                                                 const std::optional<std::string>& group = std::nullopt,
                                                                 const std::optional<std::string>& material = std::nullopt,
                                                                 const std::optional<std::string>& front_material = std::nullopt, const json& segments = 12);

// Track geometry changed manually; colors and machining may be retained.
std::string source_signature(const core::Part& part, const json& origin);

// Nominal envelope for preset handles, including rotated components (lower/upper w polach Frame).
std::optional<assembly::Frame> styled_bounds(const core::Project& project, const std::string& group);

// Rebuild an unchanged preset with ID retention and real support lengths.
// Zwraca nullptr (Python: None), gdy grupa nie jest wzorcem.
core::ProjectPtr resize_shaped_furniture(const core::Project& project, const std::string& group, const json& width, const json& height, const json& depth);

}  // namespace shaped_furniture
