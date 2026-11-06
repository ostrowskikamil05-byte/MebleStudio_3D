// Sparse, view-dependent dimensions and depth-tested manufacturing marks.
//
// Specyfikacje wymiarów (dict w Pythonie) są strukturą DimensionSpec; do
// app->visible_dimensions trafiają jako json (axis, a, b, value, label, x, y, text).
#pragma once
#include <array>
#include <string>
#include <vector>
#include "core.h"

class App;

namespace viewport_annotations {

inline const std::vector<std::string> MODES{"Wybrany mebel", "Wybrany element", "Cały zestaw"};

struct DimensionSpec {
    int axis = 0;
    core::Point3 a{}, b{};
    double value = 0;
    std::string label;
};

core::Point3 project_local(App* app, const core::Part& part, const core::Point3& point);
std::vector<DimensionSpec> dimension_specs(App* app, const core::Project& display);
void paint_dimensions(App* app, const core::Project& display);
void paint_drills(App* app, const core::Project& display);
void paint(App* app, const core::Project& display);
inline void paint(App* app, const core::ProjectPtr& display) { paint(app, *display); }

}  // namespace viewport_annotations
