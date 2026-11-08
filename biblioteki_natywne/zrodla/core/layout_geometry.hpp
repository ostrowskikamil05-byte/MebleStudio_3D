#pragma once
#include <array>
#include <vector>

namespace mebel::model::layout_geometry {
using Point = std::array<double, 2>;
using Polygon = std::vector<Point>;

double signed_area(const Polygon& polygon);
double point_segment(const Point& point, const Point& first, const Point& second);
bool inside(const Point& point, const Polygon& polygon, bool strict = false);
bool polygons_overlap(const Polygon& first, const Polygon& second);
bool polygons_overlap(const Polygon& first, const Polygon& second, bool first_ccw, bool second_ccw);
}
