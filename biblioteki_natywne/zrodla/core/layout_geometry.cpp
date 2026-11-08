#include "layout_geometry.hpp"
#include "core_api.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace mebel::model::layout_geometry {
namespace {
double cross(const Point& first, const Point& second) {
    return first[0] * second[1] - first[1] * second[0];
}
Point difference(const Point& first, const Point& second) {
    return {first[0] - second[0], first[1] - second[1]};
}
bool ray_inside(const Point& point, const Polygon& polygon) {
    bool result = false;
    for (size_t index = 0; index < polygon.size(); ++index) {
        const auto& first = polygon[index];
        const auto& second = polygon[(index + 1) % polygon.size()];
        const double boundary = (second[0] - first[0]) * (point[1] - first[1]) -
                                (second[1] - first[1]) * (point[0] - first[0]);
        if (std::abs(boundary) < 1e-7 &&
            std::min(first[0], second[0]) - 1e-7 <= point[0] &&
            point[0] <= std::max(first[0], second[0]) + 1e-7 &&
            std::min(first[1], second[1]) - 1e-7 <= point[1] &&
            point[1] <= std::max(first[1], second[1]) + 1e-7) {
            return true;
        }
        if ((first[1] > point[1]) != (second[1] > point[1]) &&
            point[0] < (second[0] - first[0]) * (point[1] - first[1]) /
                           (second[1] - first[1]) + first[0]) {
            result = !result;
        }
    }
    return result;
}
}

double signed_area(const Polygon& polygon) {
    // Python 3.13 sum() compensates floating-point cancellation. The winding
    // must agree even for nearly degenerate contours before offset probes.
    double sum = 0, correction = 0;
    for (size_t index = 0; index < polygon.size(); ++index) {
        const auto& first = polygon[index];
        const auto& second = polygon[(index + 1) % polygon.size()];
        const double value = first[0] * second[1] - second[0] * first[1];
        const double next = sum + value;
        correction += std::abs(sum) >= std::abs(value)
            ? (sum - next) + value : (value - next) + sum;
        sum = next;
    }
    return (std::isfinite(correction) ? sum + correction : sum) / 2;
}

double point_segment(const Point& point, const Point& first, const Point& second) {
    const auto vector = difference(second, first);
    const double length = vector[0] * vector[0] + vector[1] * vector[1];
    const double fraction = length
        ? std::max(0., std::min(1., ((point[0] - first[0]) * vector[0] +
                                    (point[1] - first[1]) * vector[1]) / length))
        : 0.;
    return std::hypot(point[0] - (first[0] + fraction * vector[0]),
                      point[1] - (first[1] + fraction * vector[1]));
}

bool inside(const Point& point, const Polygon& polygon, bool strict) {
    for (size_t index = 0; index < polygon.size(); ++index) {
        if (point_segment(point, polygon[index], polygon[(index + 1) % polygon.size()]) < 1e-6) {
            return !strict;
        }
    }
    return ray_inside(point, polygon);
}

bool polygons_overlap(const Polygon& first, const Polygon& second) {
    return polygons_overlap(first, second, signed_area(first) > 0, signed_area(second) > 0);
}

bool polygons_overlap(const Polygon& first, const Polygon& second, bool first_ccw, bool second_ccw) {
    // Exact algorithm and tolerances from workspace_worktops.polygons_overlap.
    // In particular a shared edge/vertex alone is not a positive-area overlap.
    for (int pass = 0; pass < 2; ++pass) {
        const auto& polygon = pass ? second : first;
        const auto& other = pass ? first : second;
        auto ccw = polygon;
        if (!(pass ? second_ccw : first_ccw)) {
            std::reverse(ccw.begin(), ccw.end());
        }
        for (size_t index = 0; index < ccw.size(); ++index) {
            const auto& point = ccw[index];
            const auto& next = ccw[(index + 1) % ccw.size()];
            if (inside(point, other, true)) {
                return true;
            }
            const double length = std::hypot(point[0] - next[0], point[1] - next[1]);
            if (length) {
                const Point probe{
                    (point[0] + next[0]) / 2 - (next[1] - point[1]) * 1e-4 / length,
                    (point[1] + next[1]) / 2 + (next[0] - point[0]) * 1e-4 / length};
                if (inside(probe, other, true)) {
                    return true;
                }
            }
        }
        for (size_t index = 0; index < polygon.size(); ++index) {
            const auto& point = polygon[index];
            const auto& next = polygon[(index + 1) % polygon.size()];
            for (size_t other_index = 0; other_index < other.size(); ++other_index) {
                const auto& start = other[other_index];
                const auto& end = other[(other_index + 1) % other.size()];
                const auto vector = difference(next, point);
                const auto other_vector = difference(end, start);
                const double denominator = cross(vector, other_vector);
                if (std::abs(denominator) < 1e-9) {
                    continue;
                }
                const auto distance = difference(start, point);
                const double fraction = cross(distance, other_vector) / denominator;
                const double other_fraction = cross(distance, vector) / denominator;
                if (1e-7 < fraction && fraction < 1 - 1e-7 &&
                    1e-7 < other_fraction && other_fraction < 1 - 1e-7) {
                    return true;
                }
            }
        }
    }
    return false;
}
}

namespace mebel::model {
namespace {
layout_geometry::Polygon polygon_argument(const Json& input) {
    if (!input.is_array()) {
        throw std::invalid_argument("Obrys wymaga listy punktow.");
    }
    layout_geometry::Polygon result;
    result.reserve(input.size());
    for (const auto& point : input) {
        if (!point.is_array() || point.size() != 2) {
            throw std::invalid_argument("Punkt obrysu wymaga dwoch wspolrzednych.");
        }
        layout_geometry::Point value{};
        for (size_t axis = 0; axis < 2; ++axis) {
            if (!point[axis].is_number() && !point[axis].is_boolean()) {
                throw std::invalid_argument("Wspolrzedna obrysu musi byc liczba.");
            }
            value[axis] = point[axis].is_boolean() ? double(point[axis].get<bool>()) : point[axis].get<double>();
            if (!std::isfinite(value[axis])) {
                throw std::invalid_argument("Wspolrzedna obrysu musi byc skonczona.");
            }
        }
        result.push_back(value);
    }
    return result;
}
}

std::optional<Json> dispatch_workspace_geometry(const Json& request) {
    if (request.value("module", std::string()) != "workspace_geometry" ||
        request.value("function", std::string()) != "polygons_overlap") {
        return std::nullopt;
    }
    const Json args = request.value("args", Json::array());
    const Json kwargs = request.value("kwargs", Json::object());
    if (args.size() > 4) {
        throw std::invalid_argument("Za duzo argumentow przecinania obrysow.");
    }
    for (auto item = kwargs.begin(); item != kwargs.end(); ++item) {
        if (item.key() != "a" && item.key() != "b" && item.key() != "a_ccw" && item.key() != "b_ccw") {
            throw std::invalid_argument("Nieznany argument przecinania obrysow: " + item.key());
        }
    }
    auto argument = [&](size_t index, const char* name) -> const Json& {
        if (index < args.size()) {
            if (kwargs.contains(name)) {
                throw std::invalid_argument("Argument obrysu podano dwukrotnie.");
            }
            return args[index];
        }
        if (kwargs.contains(name)) {
            return kwargs.at(name);
        }
        throw std::invalid_argument(std::string("Brakuje argumentu obrysu: ") + name);
    };
    const auto first = polygon_argument(argument(0, "a"));
    const auto second = polygon_argument(argument(1, "b"));
    auto winding = [&](size_t index, const char* name, const layout_geometry::Polygon& polygon) {
        if (index >= args.size() && !kwargs.contains(name)) {
            return layout_geometry::signed_area(polygon) > 0;
        }
        const auto& value = argument(index, name);
        if (!value.is_boolean()) {
            throw std::invalid_argument("Orientacja obrysu wymaga wartosci logicznej.");
        }
        return value.get<bool>();
    };
    return layout_geometry::polygons_overlap(first, second, winding(2, "a_ccw", first), winding(3, "b_ccw", second));
}
}
