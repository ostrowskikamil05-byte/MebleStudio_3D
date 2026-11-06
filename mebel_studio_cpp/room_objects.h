// Editable purchased room objects, separate from manufactured furniture.
//
// All catalogue sizes are illustrative workshop defaults, not branded appliances
// or manufacturer installation clearances. No Part, holes, stock or toolpaths are
// created. Geometry is deliberately light and supports an offline Tk viewport.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace room_objects {

using json = core::json;

extern const json ROOMS;    // dict klucz -> etykieta
extern const json CATALOG;  // dict rodzaj -> {name,width,depth,height,color[,z]}
inline constexpr size_t MAX_OBJECTS = 200;

json default_scene();
json normalize_object(const json& obj);
// Return a canonical copy, raise for invalid input, never mutate the input.
json validate_scene(const json& scene);
json scene_of(const core::Project& project);

// make_object(kind, **values)
json make_object(const std::string& kind, const json& values = json::object());
core::ProjectPtr add_object(const core::Project& project, const std::string& kind, const json& values = json::object());
core::ProjectPtr update_object(const core::Project& project, const std::string& object_id, const json& changes);
core::ProjectPtr delete_object(const core::Project& project, const std::string& object_id);
core::ProjectPtr set_scene(const core::Project& project, const json& changes = json::object());

core::Point3 object_point(const json& obj, const core::Point3& point);
std::vector<core::Point3> object_vertices(const core::Project& project, bool include_disabled = false);
std::pair<core::Point3, core::Point3> object_bounds(const json& obj);

// One purchased item per object, including temporarily hidden objects.
json inventory(const core::Project& project);
double purchased_total(const core::Project& project);
json object_lights(const core::Project& project);

// (face, color, emissive, glass); wynik buforowany jak functools.lru_cache(maxsize=256)
struct GeometryFace {
    std::vector<core::Point3> face;
    std::string color;
    bool emissive;
    bool glass;
};
const std::vector<GeometryFace>& _geometry(const std::string& key);

json room_world_faces(const core::Project& project, bool interactive = false);
json room_faces(const core::Project& project, const core::Camera& camera, std::pair<double, double> size,
                const std::optional<std::string>& selected = std::nullopt, bool interactive = false);

}  // namespace room_objects
