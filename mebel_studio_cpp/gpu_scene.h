// Retained viewport geometry and camera math, independent of VTK/Tk.
//
// Only presentation data is assembled here. CAD solids, materials, dimensions
// and manufacturing records are read without mutation.
//
// Założenia co do viewport_render (moduł tłumaczony osobno):
//   Record   {owner, vertices (Point3), indices (listy indeksów), color, sign, fitting, scene_object, emissive, architecture}
//   Context  {records(interactive), architecture, hardware (json), scene (json), lights (json), stock, room_vertices,
//             architecture_vertices, owners}
//   RenderedFaces {faces (json lista), stats (json)}, normal_center(punkty, indeksy), prepare_scene(project).
// Tablice numpy: vertices -> float32 (n*3), offsets/connectivity -> int64, cell_colors/cell_ids -> uint8 (n*3).
// Klucze krotkowe Pythona (MeshGroup.key) są zapisane jako równoważny napis kanoniczny.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "viewport_render.h"

namespace gpu_scene {

using json = core::json;
using core::Point3;

struct MeshGroup;
using MeshGroupPtr = std::shared_ptr<MeshGroup>;

struct MeshGroup {
    std::string key;                                         // krotka kluczy rekordów (napis kanoniczny)
    std::string owner;
    std::string color;
    std::vector<viewport_render::RecordPtr> records;
    std::vector<float> vertices;                             // (n, 3) float32
    std::vector<int64_t> offsets;
    std::vector<int64_t> connectivity;
    bool fitting = false;
    bool scene_object = false;
    bool emissive = false;
    bool architecture = false;
    std::vector<uint8_t> cell_colors;                        // None == pusty
    std::vector<uint8_t> cell_ids;                           // None == pusty
    bool ghost = false;
    std::vector<MeshGroupPtr> source_groups;
    double opacity = 1;
    bool visual_only = false;

    size_t vertex_count() const { return vertices.size() / 3; }
};

struct Basis { Point3 right, up, toward; };  // toward = dawne "near" (makro near z windows.h)

struct CameraSpec {
    Point3 position, focal_point, up;
    double parallel_scale;
    std::pair<double, double> window_center;
    std::pair<double, double> clipping;
    double distance;
};

struct MaterialStyle { double roughness, metallic, specular, power; bool wood; };

Basis camera_basis(const core::Camera& camera);

// Match Camera.project exactly, including orthographic pan and depth.
CameraSpec camera_spec(const core::Camera& camera, std::pair<double, double> size, const std::vector<Point3>& vertices = {});

std::vector<float> z_to_depth(const std::vector<float>& zbuffer, const CameraSpec& spec);

Point3 unproject(const core::Camera& camera, std::pair<double, double> size, double x, double y, double depth);

std::string record_key(const viewport_render::Record& record);

std::vector<MeshGroupPtr> mesh_groups(viewport_render::Context& context, bool interactive = false);

std::set<std::string> ghost_ids(const core::Project& project, bool xray = false, const std::optional<std::set<std::string>>& xray_ids = std::nullopt);

std::set<std::string> visible_architecture(const core::Project& project, const core::Camera& camera);

// Material finish affects reflection; model material data is untouched.
MaterialStyle material_style(const core::Project& project, const MeshGroup& group);

// Few draw calls, with independent owner/finish colors per polygon.
std::vector<MeshGroupPtr> batch_groups(const std::vector<MeshGroupPtr>& groups, const core::Project& project, const std::set<std::string>& selection,
                                       const std::set<std::string>& ghosts, double explode, const core::Camera& camera);

// Small annotation-compatible silhouettes; exact hits use GPU depth.
viewport_render::RenderedFaces cheap_drawn(viewport_render::Context& context, const std::vector<MeshGroupPtr>& groups, const core::Camera& camera,
                                           std::pair<double, double> size, const std::set<std::string>& selected_ids = {}, double explode = 0,
                                           const std::set<std::string>& visible = {});

}  // namespace gpu_scene
