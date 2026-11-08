// Optional OpenCascade CAD kernel, exact edge fillets and STEP solids.
#pragma once
#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include "core.h"

namespace cad_geometry {

using json = core::json;
using Edge = std::string;  // identyfikator krawędzi, np. "top:3"

void kernel();

// Coordinate compatible anchor carrying an exact curve descriptor.
// W Pythonie spec[2] jest albo zwykłą krotką punktu (geom_type == nullopt),
// albo EdgeAnchor (krotka z atrybutami geom_type, samples, length, chain).
struct EdgeAnchor {
    core::Point3 point{};
    std::optional<std::string> geom_type;
    std::vector<core::Point3> samples;
    double length = 0;
    std::optional<std::vector<std::string>> chain;
};

// (id, nazwa, początek/kotwica, koniec)
struct EdgeSpec {
    Edge edge;
    std::string name;
    EdgeAnchor geometry;
    core::Point3 end{};
};

// Curve polyline for selection highlighting, not manufacturing geometry.
std::vector<core::Point3> edge_path(const EdgeSpec& spec);

// Actual rim edges and sharp vertical corners, excluding smooth seams.
std::vector<EdgeSpec> _solid_edge_specs(const TopoDS_Shape& shape, double thickness);

std::vector<EdgeSpec> _profile_edge_specs(const std::string& key);  // @lru_cache(maxsize=128)

std::vector<EdgeSpec> edge_specs(const std::vector<core::Point>& points, double thickness, const json& profile = nullptr);
std::vector<EdgeSpec> edge_specs(const std::vector<core::Point>& points, double thickness, const json& profile, std::pair<double, double> size);

TopoDS_Edge match_edge(const TopoDS_Shape& shape, const EdgeAnchor& a, const core::Point3& b);

// OCCT propagates a constant treatment through a tangent edge chain.
std::pair<json, json> grouped_edge_features(const std::map<std::string, EdgeSpec>& specifications, const json& fillets, const json& chamfers);

// Extrude exact parametric outline; polygon points remain a GUI proxy.
TopoDS_Shape outline_solid(const json& data);

// Liczniki pamięci podręcznej brył i siatek (do testów i dziennika wydajności).
struct CacheStats {
    size_t shape_hits = 0, shape_misses = 0, mesh_hits = 0, mesh_misses = 0;
};
CacheStats cache_stats();
void reset_cache_stats();

// Klucz pamięci podręcznej pomija wiercenia (bryła bazowa nie zależy od okuć).
TopoDS_Shape cached_shape(const std::string& key);  // @lru_cache(maxsize=128)

std::string shape_key(const core::Part& part);

TopoDS_Shape local_shape(const core::Part& part);

using Triangle = std::array<core::Point3, 3>;

std::vector<Triangle> cached_mesh(const std::string& key);  // @lru_cache(maxsize=128)

std::vector<Triangle> mesh(const core::Part& part);

TopoDS_Shape cut_drills(TopoDS_Shape shape, const json& drilling, double thickness, const std::vector<core::Point>& points, bool check_only = false, bool check_boundary = false);

TopoDS_Shape drilled_shape(const core::Part& part, bool check_only = false);

void validate_fillets(const core::Part& part);

void step_export(core::Project& project, const std::string& path, const std::string& export_type = "STEP");

void stl_export(core::Project& project, const std::string& path);

}  // namespace cad_geometry
