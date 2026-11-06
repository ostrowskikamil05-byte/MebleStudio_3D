// Optional retained VTK/OpenGL renderer displayed in the existing Tk Canvas.
//
// The offscreen framebuffer leaves Canvas input, detached tabs and annotation
// overlays under Tk control. RGBA, real depth and an encoded visible-owner pass
// are read from the same retained geometry. VTK is imported only on first use.
//
// W C++ VTK jest zależnością czasu budowania: kod VTK kompiluje się przy zdefiniowanym
// MEBEL_HAVE_VTK (CMake: find_package(VTK COMPONENTS RenderingOpenGL2 FiltersCore ... ) +
// vtk_module_autoinit). Bez niego GPUViewport zgłasza GPUUnavailable, a wywołujący wraca do CPU
// (odpowiednik ImportError z `_vtk()`).
#pragma once
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "depth_raster.h"
#include "gpu_scene.h"
#include "native_viewport.h"
#include "tk.h"

namespace gpu_viewport {

using json = core::json;

struct GPUUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };

struct Request {
    core::ProjectPtr project; core::Camera camera; std::pair<double, double> size; bool interactive = false;
    std::set<std::string> selected_ids; std::optional<std::string> selected; double explode = 0; bool xray = false;
    std::set<std::string> xray_ids; std::string quality = "balanced";
};

struct FrameOptions {
    bool interactive = false;
    std::set<std::string> selected_ids;
    std::optional<std::string> selected;
    double explode = 0;
    bool xray = false;
    std::optional<std::set<std::string>> xray_ids;
    std::string quality = "balanced";
};

struct ActorMesh;  // aktor VTK + aktor identyfikatorów (definicja w gpu_viewport.cpp)
struct Backend;    // okno i renderer VTK

// (h, w, 4) uint8, wiersze od góry
struct Rgba {
    int rows = 0, cols = 0;
    std::vector<uint8_t> data;
};

class GPUViewport {
public:
    bool closed = false;
    std::shared_ptr<viewport_render::Context> context;
    std::vector<gpu_scene::MeshGroupPtr> groups;
    std::map<std::string, std::shared_ptr<ActorMesh>> meshes;
    std::vector<std::shared_ptr<ActorMesh>> current;
    std::vector<std::string> ids;
    std::map<std::string, int> lookup;
    std::optional<core::Camera> camera;
    std::pair<int, int> size{64, 64};
    std::pair<int, int> output_size{64, 64};
    double sample_scale = 1;
    std::string capabilities;
    json diagnostics = json::object();
    std::optional<depth_raster::Buffer2D<float>> depth_buffer;
    std::optional<depth_raster::Buffer2D<int32_t>> id_buffer;
    std::optional<depth_raster::Buffer2D<uint8_t>> kind_buffer;
    json drawn = json::array();
    std::array<int, 4> roi{0, 0, 0, 0};
    std::array<int, 4> output_roi{0, 0, 0, 0};
    std::string quality = "balanced";

    GPUViewport();
    ~GPUViewport();
    GPUViewport(const GPUViewport&) = delete;
    GPUViewport& operator=(const GPUViewport&) = delete;

    void close();
    bool sync(const core::Project& project, bool interactive, const std::set<std::string>& selection, const std::set<std::string>& ghosts, double explode,
              const core::Camera& camera);
    void configure_camera(const core::Camera& camera, std::pair<double, double> size, double sample_scale = 1);
    void configure_lights(const core::Project& project, const core::Camera& camera);
    void quality_pass(const std::string& quality, bool interactive);
    void material_pass(const core::Project& project, const std::set<std::string>& visible);
    Rgba read_rgba(bool cropped = false);
    std::vector<float> read_depth_roi();
    depth_raster::Buffer2D<float> read_depth();
    depth_raster::Buffer2D<int32_t> owner_pass();
    depth_raster::Buffer2D<int32_t> read_owner();
    std::pair<depth_raster::Buffer2D<int32_t>, depth_raster::Buffer2D<float>> ghost_fallback(depth_raster::Buffer2D<int32_t> owner, depth_raster::Buffer2D<float> depth);
    Rgba frame(const core::Project& project, const core::Camera& camera, std::pair<double, double> size, const FrameOptions& options = {});

private:
    std::unique_ptr<Backend> backend;
    std::vector<uint8_t> surface_kind;
    std::vector<int32_t> surface_owner;
    std::vector<core::Point3> bounds;
    std::optional<bool> mode;
    std::optional<std::string> signature;
    std::string light_key;
    bool has_light_key = false;
    std::optional<gpu_scene::CameraSpec> spec;
};

// Draw GPU image and publish existing depth/ID buffers; controlled fallback.
// project == nullptr -> ValueError('paint_gpu wymaga projektu.')
native_viewport::ViewportResult paint_gpu(tk::Canvas* canvas, const Request& request);

// Zwraca {id, kind, precise_surface, world, depth} albo null.
json pick_world(tk::Canvas* canvas, double x, double y);

void close_gpu(tk::Canvas* canvas);

}  // namespace gpu_viewport
