// Visible face hit to exact local U,V,W; no CAD work during pointer movement.
#pragma once
#include <array>
#include <optional>
#include "core.h"

class App;

namespace attachment_picker {

using json = core::json;

core::Point3 inverse_world(const core::Part& part, const core::Point3& point);

std::optional<std::array<double, 3>> _barycentric(const std::array<double, 2>& point, const std::array<std::array<double, 2>, 3>& triangle);

// Zwraca dict(target_id=..., local_hit=...) albo None
std::optional<json> pick(App* app, int x, int y);

}  // namespace attachment_picker
