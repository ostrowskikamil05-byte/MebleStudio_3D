// Transactional front milling and purchased cabinet accessories, in millimetres.
//
// The defaults are editable workshop dimensions, not manufacturer recipes. Legs
// are purchased hardware, never fictitious panels in the cutting list. A plinth
// is a genuine panel and is created by furniture_configurator, not this module.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace configurator_accessories {

using json = core::json;

extern const std::vector<std::pair<std::string, std::string>> FRONT_STYLES;
inline constexpr int MAX_FLUTES = 32;

long long _integer(const json& value, double low, double high, const std::string& label);

std::string _color(const json& value);

// Validate requested dimensions; part-specific safe limits are applied later.
json normalize_front_style(const json& style = "smooth", const json& cfg = nullptr);

double _safe_depth(double thickness, double requested);

json _cut(const std::string& purpose, const std::string& shape, double u, double v, double length, double width, double depth,
          const json& angle = 0, const std::string& feature = "front_style", const json& extra = json::object());

// Circle/slot/rectangle overlap in local UV, independent of panel pose.
bool _overlaps_drill(const json& cut, double u, double v, double radius);

// Leave a 1 mm web where automatic recesses cross existing blind drilling.
// cuts wskazują słowniki, które mają zostać zmienione (w Pythonie: referencje).
std::optional<double> _guard_opposed_drills(const core::Part& part, const std::vector<json*>& cuts);

void _clear_front(core::Project& project, core::Part& part);

// Batch helper: mutate a caller-owned project copy, without global validation.
core::Part& apply_front_to_part(core::Project& project, core::Part& part, const json& style = "smooth", const json& config = nullptr);

// Return a modified project copy; failed milling leaves the source unchanged.
core::ProjectPtr apply_front_style(const core::Project& project, const std::string& part_id, const json& style = "smooth", const json& config = nullptr, bool validate = true);
core::ProjectPtr apply_front_style(const core::Project& project, const core::Part& part, const json& style = "smooth", const json& config = nullptr, bool validate = true);

json normalize_accessories(const json& config = nullptr);

std::pair<core::Point3, core::Point3> _body_bounds(const core::Project& project, const std::string& group, const json& bounds);

void _install_led(core::Project& project, const std::vector<core::Part*>& members, const std::string& group, const json& cfg);

// Return a copy with deterministic replacement of this group's auto fittings.
core::ProjectPtr install_accessories(const core::Project& project, const std::string& group, const json& config = nullptr, const json& bounds = nullptr, bool validate = true);

// Nominal fitting envelope, with no expensive panel CAD mesh generation.
std::vector<core::Point3> accessory_vertices(const core::Project& project, const std::optional<std::string>& group = std::nullopt, bool local = false, bool include_all = false);

extern const std::vector<std::string> LED_SURFACES;

// Face coordinates A,B with an exterior normal, in the part's UVW frame.
struct LedSurface {
    core::Point3 origin;
    std::array<int, 3> a, b, n;
    double la, lb;
};
LedSurface led_surface(const core::Part& part, const std::string& face);

// Pure fitted LED description; no stock item or machining is created here.
json led_proposal(core::Part& part, const json& local_hit = nullptr, const json& options = nullptr);

// Atomic purchased LED placement on any planar local face; groove is opt-in.
core::ProjectPtr install_manual_led(const core::Project& project, const std::string& part_id, const json& local_hit = nullptr, const json& options = nullptr,
                                    const json& attachment_id = nullptr, bool validate = true);

// Refresh dimensions on a caller-owned candidate; retain attachment IDs/prices.
core::Project& refresh_manual_led(core::Project& project);

// Lightweight geometry/color pairs; supports legacy automatic LED records.
std::vector<std::pair<std::vector<core::Point3>, std::string>> led_local_geometry(const core::Part& part, const json& hardware);

}  // namespace configurator_accessories
