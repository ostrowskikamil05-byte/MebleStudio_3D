// Native shared-worktop solver with the original editor lifecycle retained.
#include "native_worktops.h"

#include <stdexcept>

#include "native_model.h"
#include "pyutil.h"
#include "workspace_worktops.h"

namespace native_worktops {

using core::Point;
using core::Point3;

const std::vector<std::string> OPERATIONS = {"_distance", "_offset", "_union", "_geometry"};

json _polygon(const Polygon& points) {
    json out = json::array();
    for (const auto& p : points) out.push_back(json::array({p[0], p[1]}));
    return out;
}

namespace {

Polygon polygon_from(const json& value) {
    Polygon out;
    for (const auto& p : value) out.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
    return out;
}

bool ccw(const Polygon& points) { return workspace_worktops::_signed(points) > 0; }

}  // namespace

bool install() {
    if (!native_model::available()) return false;
    if (native_model::call("native", "info").value("worktop_api_version", 0) != 1)
        throw std::runtime_error("Biblioteka C++ nie obsługuje solvera wspólnego blatu. Użyj kompletnej paczki Etap 3.");
    return true;
}

double distance(const Polygon& a, const Polygon& b) {
    if (a.empty() || b.empty()) throw std::invalid_argument("min() arg is an empty sequence");
    return native_model::call("workspace_worktop", "distance", json::array({_polygon(a), _polygon(b)}), json{{"a_ccw", ccw(a)}, {"b_ccw", ccw(b)}}).get<double>();
}

Polygon offset(const Polygon& poly, double amount) {
    if (amount == 0) return poly;
    json value = native_model::call("workspace_worktop", "offset", json::array({_polygon(poly), amount}), json{{"ccw", ccw(poly)}});
    return polygon_from(value);
}

Polygon geometry_union(const std::vector<Polygon>& polygons) {
    json wire = json::array(), flags = json::array();
    for (const auto& poly : polygons) { wire.push_back(_polygon(poly)); flags.push_back(ccw(poly)); }
    json value = native_model::call("workspace_worktop", "union", json::array({wire}), json{{"ccw", flags}});
    return polygon_from(value);
}

std::tuple<Polygon, Point3, json> geometry(const core::Project& project, const std::vector<std::string>& groups, const json& config) {
    json rows = json::array();
    for (const auto& group : groups) {
        auto [polygon, z, anchor] = workspace_worktops::_body(project, group);
        rows.push_back(json{{"polygon", _polygon(polygon)}, {"z", z}, {"anchor", anchor}, {"ccw", ccw(polygon)}});
    }
    if (rows.empty()) throw std::invalid_argument("max() arg is an empty sequence");
    json result = native_model::call("workspace_worktop", "geometry", json::array({rows, config.at("overhang")}));
    const json& o = result.at("origin");
    return {polygon_from(result.at("contour")), Point3{o.at(0).get<double>(), o.at(1).get<double>(), o.at(2).get<double>()}, result.at("anchors")};
}

}  // namespace native_worktops
