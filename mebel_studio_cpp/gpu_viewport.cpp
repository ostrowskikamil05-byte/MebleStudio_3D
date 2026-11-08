// Optional retained VTK/OpenGL renderer displayed in the existing Tk Canvas.
#include "gpu_viewport.h"
#include "view_style.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#include "pyutil.h"

#ifdef MEBEL_HAVE_VTK
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkIdTypeArray.h>
#include <vtkLight.h>
#include <vtkNew.h>
#include <vtkOpenGLPolyDataMapper.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkRenderStepsPass.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSSAOPass.h>
#include <vtkShaderProperty.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>
#endif

namespace gpu_viewport {

using depth_raster::Buffer2D;
using gpu_scene::MeshGroup;
using gpu_scene::MeshGroupPtr;

namespace {
double perf_counter() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

[[maybe_unused]] std::array<double, 3> rgb_unit(const std::string& color) {
    return {static_cast<double>(py::int16(color.substr(1, 2))) / 255, static_cast<double>(py::int16(color.substr(3, 2))) / 255, static_cast<double>(py::int16(color.substr(5, 2))) / 255};
}
}  // namespace

#ifdef MEBEL_HAVE_VTK

struct ActorMesh {
    MeshGroupPtr group;
    vtkSmartPointer<vtkPolyData> data;
    vtkSmartPointer<vtkPolyDataNormals> normals;
    vtkSmartPointer<vtkOpenGLPolyDataMapper> mapper;
    vtkSmartPointer<vtkActor> actor;
    vtkSmartPointer<vtkOpenGLPolyDataMapper> id_mapper;
    vtkSmartPointer<vtkActor> id_actor;

    ActorMesh(const MeshGroupPtr& group_, const core::Project& project) : group(group_) {
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto coordinates = vtkSmartPointer<vtkFloatArray>::New();
        coordinates->SetNumberOfComponents(3); coordinates->SetNumberOfTuples(static_cast<vtkIdType>(group->vertex_count()));
        if (!group->vertices.empty()) std::memcpy(coordinates->GetPointer(0), group->vertices.data(), group->vertices.size() * sizeof(float));
        points->SetData(coordinates);
        auto offsets = vtkSmartPointer<vtkIdTypeArray>::New(); offsets->SetNumberOfValues(static_cast<vtkIdType>(group->offsets.size()));
        for (size_t i = 0; i < group->offsets.size(); ++i) offsets->SetValue(static_cast<vtkIdType>(i), static_cast<vtkIdType>(group->offsets[i]));
        auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New(); connectivity->SetNumberOfValues(static_cast<vtkIdType>(group->connectivity.size()));
        for (size_t i = 0; i < group->connectivity.size(); ++i) connectivity->SetValue(static_cast<vtkIdType>(i), static_cast<vtkIdType>(group->connectivity[i]));
        auto cells = vtkSmartPointer<vtkCellArray>::New(); cells->SetData(offsets, connectivity);
        auto source = vtkSmartPointer<vtkPolyData>::New(); source->SetPoints(points); source->SetPolys(cells);
        normals = vtkSmartPointer<vtkPolyDataNormals>::New(); normals->SetInputData(source);
        normals->SetFeatureAngle(42); normals->SplittingOn(); normals->ConsistencyOff();
        normals->AutoOrientNormalsOff(); normals->ComputePointNormalsOn();
        normals->Update(); data = normals->GetOutput();
        for (auto& [name, values] : {std::pair<const char*, const std::vector<uint8_t>*>{"Beauty", &group->cell_colors}, {"TargetId", &group->cell_ids}}) {
            auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New(); colors->SetNumberOfComponents(3);
            colors->SetNumberOfTuples(static_cast<vtkIdType>(values->size() / 3));
            if (!values->empty()) std::memcpy(colors->GetPointer(0), values->data(), values->size());
            colors->SetName(name); data->GetCellData()->AddArray(colors);
        }
        mapper = vtkSmartPointer<vtkOpenGLPolyDataMapper>::New(); mapper->SetInputData(data);
        mapper->SetScalarModeToUseCellFieldData(); mapper->SetColorModeToDirectScalars();
        mapper->SelectColorArray("Beauty"); mapper->ScalarVisibilityOn();
        // World-space grain needs the actual model coordinates, rather than
        // the mapper's automatic [0..1] vertex compression coordinates.
        mapper->SetVBOShiftScaleMethod(0);
        actor = vtkSmartPointer<vtkActor>::New(); actor->SetMapper(mapper);
        if (gpu_scene::material_style(project, *group->source_groups.front()).wood) {
            // Procedural grain stays in world coordinates while orbiting. It
            // only changes beauty RGB; the separate ID actor stays untouched.
            vtkShaderProperty* shader = actor->GetShaderProperty();
            shader->AddVertexShaderReplacement("//VTK::PositionVC::Dec", true,
                "//VTK::PositionVC::Dec\nout vec3 furnitureGrain;\n", false);
            shader->AddVertexShaderReplacement("//VTK::PositionVC::Impl", true,
                "//VTK::PositionVC::Impl\nfurnitureGrain=vertexMC.xyz;\n", false);
            shader->AddFragmentShaderReplacement("//VTK::Light::Dec", true,
                "//VTK::Light::Dec\nin vec3 furnitureGrain;\n", false);
            shader->AddFragmentShaderReplacement("//VTK::Light::Impl", true,
                "//VTK::Light::Impl\nfloat along=furnitureGrain.z+.17*furnitureGrain.y;\n"
                "float ring=furnitureGrain.x+9.*sin(along*.008)+2.*sin(along*.025);\n"
                "float fiber=sin(ring*.24+3.*sin(ring*.02)+.45*sin(along*.11));\n"
                "gl_FragData[0].rgb *= .985+.028*fiber+.009*sin(ring*1.7);\n", false);
        }
        id_mapper = vtkSmartPointer<vtkOpenGLPolyDataMapper>::New(); id_mapper->SetInputData(data);
        id_mapper->SetScalarModeToUseCellFieldData(); id_mapper->SetColorModeToDirectScalars();
        id_mapper->SelectColorArray("TargetId"); id_mapper->ScalarVisibilityOn();
        id_actor = vtkSmartPointer<vtkActor>::New(); id_actor->SetMapper(id_mapper); id_actor->VisibilityOff();
        id_actor->GetProperty()->LightingOff(); id_actor->GetProperty()->SetColor(1, 1, 1);
    }
};

struct Backend {
    vtkSmartPointer<vtkRenderWindow> window;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkUnsignedCharArray> rgba_array;
    vtkSmartPointer<vtkFloatArray> z_array;
    std::vector<vtkSmartPointer<vtkLight>> lights;
    vtkSmartPointer<vtkSSAOPass> ssao;
    vtkSmartPointer<vtkRenderStepsPass> steps;
};

#else

struct ActorMesh { MeshGroupPtr group; };
struct Backend {};

#endif  // MEBEL_HAVE_VTK

GPUViewport::GPUViewport() {
#ifdef MEBEL_HAVE_VTK
    backend = std::make_unique<Backend>();
    closed = false;
    try {
        backend->window = vtkSmartPointer<vtkRenderWindow>::New(); backend->window->SetOffScreenRendering(1);
        backend->window->SetMultiSamples(view_style::state.aa); backend->window->SetAlphaBitPlanes(1);
        // No visible VTK window needs buffer presentation/vsync. Canvas owns
        // the displayed image and the readback always uses the back buffer.
        backend->window->SetSwapBuffers(0);
        backend->renderer = vtkSmartPointer<vtkRenderer>::New(); backend->renderer->SetBackground(0, 0, 0);
        backend->renderer->SetBackgroundAlpha(0); backend->renderer->SetAutomaticLightCreation(false);
        backend->window->AddRenderer(backend->renderer); backend->window->SetSize(64, 64);
        backend->window->Render();
        if (!backend->window->SupportsOpenGL()) throw GPUUnavailable("Sterownik nie obsługuje OpenGL wymaganego przez VTK.");
        const char* report = backend->window->ReportCapabilities(); capabilities = report ? report : "";
    } catch (...) {
        close(); throw;
    }
    size = {64, 64};
    backend->rgba_array = vtkSmartPointer<vtkUnsignedCharArray>::New(); backend->z_array = vtkSmartPointer<vtkFloatArray>::New();
#else
    throw GPUUnavailable("VTK/OpenGL nie jest dostępny: program zbudowano bez VTK (MEBEL_HAVE_VTK).");
#endif
}

GPUViewport::~GPUViewport() { close(); }

void GPUViewport::close() {
    if (closed) return;
    closed = true;
#ifdef MEBEL_HAVE_VTK
    if (backend) {
        try {
            if (backend->renderer) {
                backend->renderer->RemoveAllViewProps(); backend->renderer->RemoveAllLights(); backend->renderer->SetPass(nullptr);
            }
        } catch (...) {}
        try { if (backend->window) backend->window->Finalize(); } catch (...) {}
    }
#endif
    meshes.clear(); current.clear(); groups.clear(); context.reset();
    if (backend) {
#ifdef MEBEL_HAVE_VTK
        backend->lights.clear(); backend->rgba_array = nullptr; backend->z_array = nullptr; backend->ssao = nullptr; backend->steps = nullptr;
#endif
    }
    bounds.clear(); drawn = json::array(); ids.clear(); lookup.clear();
    depth_buffer.reset(); id_buffer.reset(); kind_buffer.reset();
    surface_owner.clear(); surface_kind.clear();
    camera.reset(); signature.reset(); diagnostics = json::object();
}

bool GPUViewport::sync(const core::Project& project, bool interactive, const std::set<std::string>& selection, const std::set<std::string>& ghosts, double explode,
                       const core::Camera& camera_) {
    std::shared_ptr<viewport_render::Context> next = viewport_render::prepare_scene(project);
    bool mode_ = interactive;
    std::string key = std::to_string(reinterpret_cast<uintptr_t>(next.get())) + "|" + (mode_ ? "1" : "0") + "|";
    { std::vector<std::string> sel(selection.begin(), selection.end()); key += py::join(sel, ",") + "|"; }
    { std::vector<std::string> gh(ghosts.begin(), ghosts.end()); key += py::join(gh, ",") + "|"; }
    key += py::repr(explode) + "|";
    if (explode != 0) key += py::repr(camera_.center[0]) + "," + py::repr(camera_.center[1]) + "," + py::repr(camera_.center[2]);
    if (signature && *signature == key) return false;
    std::vector<MeshGroupPtr> grouped = (context == next && mode && *mode == mode_) ? groups : gpu_scene::mesh_groups(*next, mode_);
    std::vector<MeshGroupPtr> batches = gpu_scene::batch_groups(grouped, project, selection, ghosts, explode, camera_);
    auto previous = meshes; std::map<std::string, std::shared_ptr<ActorMesh>> next_meshes; std::vector<std::shared_ptr<ActorMesh>> next_current;
    for (auto& group : batches) {
        std::shared_ptr<ActorMesh> mesh;
        auto found = previous.find(group->key);
        if (found != previous.end()) mesh = found->second;
#ifdef MEBEL_HAVE_VTK
        else mesh = std::make_shared<ActorMesh>(group, project);
#else
        else mesh = std::make_shared<ActorMesh>(ActorMesh{group});
#endif
        next_meshes[group->key] = mesh; next_current.push_back(mesh);
#ifdef MEBEL_HAVE_VTK
        if (std::find(current.begin(), current.end(), mesh) == current.end()) {
            backend->renderer->AddActor(mesh->actor); backend->renderer->AddActor(mesh->id_actor);
        }
#endif
    }
#ifdef MEBEL_HAVE_VTK
    for (auto& mesh : current) {
        if (std::find(next_current.begin(), next_current.end(), mesh) == next_current.end()) {
            backend->renderer->RemoveActor(mesh->actor); backend->renderer->RemoveActor(mesh->id_actor);
        }
    }
#endif
    context = next; mode = mode_; groups = grouped; meshes = std::move(next_meshes); current = std::move(next_current);
    signature = key;
    std::set<std::string> owner_set; for (auto& g : grouped) owner_set.insert(g->owner);
    ids.assign(owner_set.begin(), owner_set.end()); lookup.clear();
    for (size_t i = 0; i < ids.size(); ++i) lookup[ids[i]] = static_cast<int>(i);
    surface_owner.assign(1, -1); surface_kind.assign(1, 0);
    for (auto& g : grouped) {
        surface_owner.push_back(lookup[g->owner]);
        surface_kind.push_back(g->scene_object ? 2 : g->fitting ? 1 : 0);
    }
    // Read only the projected scene rectangle. Empty pixels outside it are
    // already known, without synchronous full-screen GPU readbacks.
    if (!batches.empty()) {
        std::array<float, 3> lo, hi;
        lo.fill(std::numeric_limits<float>::infinity()); hi.fill(-std::numeric_limits<float>::infinity());
        for (auto& g : batches) {
            for (size_t k = 0; k < g->vertices.size(); ++k) { lo[k % 3] = std::min(lo[k % 3], g->vertices[k]); hi[k % 3] = std::max(hi[k % 3], g->vertices[k]); }
        }
        bounds.clear();
        const int corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
        for (auto& bit : corners) {
            core::Point3 p;
            for (int i = 0; i < 3; ++i) p[i] = static_cast<double>(bit[i] ? hi[i] : lo[i]);
            bounds.push_back(p);
        }
    } else {
        bounds.clear();
    }
    return true;
}

void GPUViewport::configure_camera(const core::Camera& camera_, std::pair<double, double> size_, double sample_scale_) {
    output_size = {std::max(1, static_cast<int>(size_.first)), std::max(1, static_cast<int>(size_.second))};
    size = {std::max(1, static_cast<int>(py::round_half_even(output_size.first * sample_scale_))), std::max(1, static_cast<int>(py::round_half_even(output_size.second * sample_scale_)))};
    sample_scale = std::min(static_cast<double>(size.first) / output_size.first, static_cast<double>(size.second) / output_size.second);
#ifdef MEBEL_HAVE_VTK
    backend->window->SetSize(size.first, size.second);
#endif
    std::vector<core::Point3> vertices = context->stock;
    vertices.insert(vertices.end(), context->room_vertices.begin(), context->room_vertices.end());
    vertices.insert(vertices.end(), context->architecture_vertices.begin(), context->architecture_vertices.end());
    gpu_scene::CameraSpec camera_spec = gpu_scene::camera_spec(camera_, {static_cast<double>(output_size.first), static_cast<double>(output_size.second)}, vertices);
#ifdef MEBEL_HAVE_VTK
    vtkCamera* active = backend->renderer->GetActiveCamera(); active->ParallelProjectionOn();
    active->SetPosition(camera_spec.position[0], camera_spec.position[1], camera_spec.position[2]);
    active->SetFocalPoint(camera_spec.focal_point[0], camera_spec.focal_point[1], camera_spec.focal_point[2]);
    active->SetViewUp(camera_spec.up[0], camera_spec.up[1], camera_spec.up[2]); active->SetParallelScale(camera_spec.parallel_scale);
    active->SetWindowCenter(camera_spec.window_center.first, camera_spec.window_center.second);
    active->SetClippingRange(camera_spec.clipping.first, camera_spec.clipping.second);
    active->SetUseExplicitAspectRatio(true);
    active->SetExplicitAspectRatio(static_cast<double>(output_size.first) / output_size.second);
#endif
    spec = camera_spec; camera = camera_;
    int x0, y0, x1, y1;
    if (!bounds.empty()) {
        double min_x = std::numeric_limits<double>::infinity(), min_y = min_x, max_x = -min_x, max_y = -min_x;
        for (auto& point : bounds) {
            core::Point3 p = camera_.project(point, output_size.first, output_size.second);
            min_x = std::min(min_x, p[0]); max_x = std::max(max_x, p[0]); min_y = std::min(min_y, p[1]); max_y = std::max(max_y, p[1]);
        }
        const int padding = 16;
        x0 = std::max(0, static_cast<int>(std::floor(min_x * size.first / output_size.first)) - padding);
        y0 = std::max(0, static_cast<int>(std::floor(min_y * size.second / output_size.second)) - padding);
        x1 = std::min(size.first - 1, static_cast<int>(std::ceil(max_x * size.first / output_size.first)) + padding);
        y1 = std::min(size.second - 1, static_cast<int>(std::ceil(max_y * size.second / output_size.second)) + padding);
        if (x1 < x0 || y1 < y0) x0 = y0 = x1 = y1 = 0;
    } else {
        x0 = y0 = x1 = y1 = 0;
    }
    roi = {x0, y0, x1, y1};
}

void GPUViewport::configure_lights(const core::Project& project, const core::Camera& camera_) {
    double brightness = 1;
    if (project.scene.is_object() && project.scene.contains("brightness")) brightness = project.scene["brightness"].get<double>();
    brightness = std::max(.1, std::min(2.0, brightness));
    const json& lights = context->lights;
    double boost = quality == "high" || view_style::realistic() ? 1.8 : 1;
    json key_lights = json::array();
    if (lights.is_array()) for (auto& l : lights) key_lights.push_back(json::array({py::get(l, "position"), py::get(l, "color"), py::get(l, "intensity"), py::get(l, "direction", json::array())}));
    std::string key = py::repr(brightness) + "|" + py::repr(boost) + "|" + key_lights.dump();
    if (!has_light_key || key != light_key) {
#ifdef MEBEL_HAVE_VTK
        backend->renderer->RemoveAllLights(); backend->lights.clear();
        // Stable studio sources in world space keep material highlights
        // coherent while orbiting rather than baking camera-based colors.
        core::Point3 center = camera_.center;
        struct Studio { core::Point3 position; double intensity; const char* color; };
        for (const Studio& source : {Studio{{-3000, -4000, 6000}, .75, "#fff4df"}, Studio{{4000, 1000, 4500}, .35, "#d9e8ff"}}) {
            auto light = vtkSmartPointer<vtkLight>::New(); light->SetLightTypeToSceneLight();
            light->SetPosition(source.position[0], source.position[1], source.position[2]); light->SetFocalPoint(center[0], center[1], center[2]);
            auto c = rgb_unit(source.color); light->SetColor(c[0], c[1], c[2]); light->SetIntensity(source.intensity * brightness * boost);
            backend->renderer->AddLight(light); backend->lights.push_back(light);
        }
        // A compact LED sampling budget keeps real lighting interactive.
        size_t count = 0;
        if (lights.is_array()) for (auto& lamp : lights) {
            if (count++ >= 12) break;
            auto light = vtkSmartPointer<vtkLight>::New(); light->SetLightTypeToSceneLight(); light->PositionalOn();
            json p = lamp["position"]; light->SetPosition(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
            json direction = py::get(lamp, "direction", json::array({0, 0, -1}));
            light->SetFocalPoint(p[0].get<double>() + direction[0].get<double>() * 100, p[1].get<double>() + direction[1].get<double>() * 100, p[2].get<double>() + direction[2].get<double>() * 100);
            light->SetConeAngle(py::truthy(py::get(lamp, "direction")) ? 85 : 180);
            auto c = rgb_unit(py::get(lamp, "color", json("#fff3bf")).get<std::string>()); light->SetColor(c[0], c[1], c[2]);
            light->SetIntensity(py::get(lamp, "intensity", json(1)).get<double>() * brightness * 1.4);
            double radius = std::max(1.0, py::get(lamp, "radius", json(650)).get<double>()); light->SetAttenuationValues(1, 0, 1 / (radius * radius));
            backend->renderer->AddLight(light); backend->lights.push_back(light);
        }
#else
        (void)camera_;
#endif
        light_key = key; has_light_key = true;
    }
}

void GPUViewport::quality_pass(const std::string& quality_, bool interactive) {
    bool use_ssao = (quality_ == "high" || view_style::realistic()) && !interactive;
#ifdef MEBEL_HAVE_VTK
    if (use_ssao) {
        if (!backend->ssao) {
            backend->steps = vtkSmartPointer<vtkRenderStepsPass>::New();
            backend->ssao = vtkSmartPointer<vtkSSAOPass>::New(); backend->ssao->SetDelegatePass(backend->steps);
            backend->ssao->SetRadius(70); backend->ssao->SetBias(1.5); backend->ssao->SetKernelSize(12);
            backend->ssao->SetIntensityScale(.35); backend->ssao->SetIntensityShift(.65);
            backend->ssao->BlurOn();
        }
        backend->renderer->SetPass(backend->ssao);
    } else {
        backend->renderer->SetPass(nullptr);
    }
#else
    (void)use_ssao;
#endif
}

void GPUViewport::material_pass(const core::Project& project, const std::set<std::string>& visible) {
#ifdef MEBEL_HAVE_VTK
    for (auto& mesh : current) {
        const MeshGroup& group = *mesh->group; vtkActor* actor = mesh->actor;
        actor->SetVisibility(!group.architecture || visible.contains(group.owner));
        mesh->id_actor->VisibilityOff();
        actor->SetPosition(0, 0, 0);
        vtkProperty* prop = actor->GetProperty(); auto c = rgb_unit(group.color); prop->SetColor(c[0], c[1], c[2]); prop->LightingOn();
        prop->SetAmbient(.2); prop->SetDiffuse(.78); prop->SetInterpolationToPhong();
        gpu_scene::MaterialStyle style = gpu_scene::material_style(project, *group.source_groups.front()); prop->SetSpecular(style.specular); prop->SetSpecularPower(style.power);
        if ((quality == "high" || view_style::realistic()) && !group.emissive) {
            prop->SetInterpolationToPBR(); prop->SetRoughness(style.roughness); prop->SetMetallic(style.metallic);
        }
        prop->SetOpacity(group.ghost ? .13 : group.opacity);
        prop->SetRepresentationToSurface(); prop->EdgeVisibilityOff();
        if (group.emissive) {
            prop->SetAmbient(1); prop->SetDiffuse(0); prop->SetSpecular(0);
        }
    }
#else
    (void)project; (void)visible;
#endif
}

Rgba GPUViewport::read_rgba(bool cropped) {
    int width = size.first, height = size.second;
    auto [x0, y0, x1, y1] = roi;
    Rgba result;
#ifdef MEBEL_HAVE_VTK
    if (!backend->window->GetRGBACharPixelData(x0, height - 1 - y1, x1, height - 1 - y0, 0, backend->rgba_array)) throw GPUUnavailable("Nie udało się odczytać obrazu OpenGL.");
    int rows = y1 - y0 + 1, cols = x1 - x0 + 1;
    const unsigned char* raw = backend->rgba_array->GetPointer(0);
    result.rows = rows; result.cols = cols; result.data.resize(static_cast<size_t>(rows) * cols * 4);
    for (int r = 0; r < rows; ++r)  // [::-1] — odwrócenie kolejności wierszy
        std::memcpy(&result.data[static_cast<size_t>(r) * cols * 4], raw + static_cast<size_t>(rows - 1 - r) * cols * 4, static_cast<size_t>(cols) * 4);
#else
    throw GPUUnavailable("Nie udało się odczytać obrazu OpenGL.");
#endif
    if (cropped) return result;
    Rgba full; full.rows = height; full.cols = width; full.data.assign(static_cast<size_t>(height) * width * 4, 0);
    for (int r = 0; r < result.rows; ++r)
        std::memcpy(&full.data[(static_cast<size_t>(y0 + r) * width + x0) * 4], &result.data[static_cast<size_t>(r) * result.cols * 4], static_cast<size_t>(result.cols) * 4);
    return full;
}

std::vector<float> GPUViewport::read_depth_roi() {
    auto [x0, y0, x1, y1] = roi; int height = size.second;
    int rows = y1 - y0 + 1, cols = x1 - x0 + 1;
    std::vector<float> z(static_cast<size_t>(rows) * cols);
#ifdef MEBEL_HAVE_VTK
    if (!backend->window->GetZbufferData(x0, height - 1 - y1, x1, height - 1 - y0, backend->z_array)) throw GPUUnavailable("Nie udało się odczytać bufora głębokości OpenGL.");
    const float* raw = backend->z_array->GetPointer(0);
    for (int r = 0; r < rows; ++r) std::memcpy(&z[static_cast<size_t>(r) * cols], raw + static_cast<size_t>(rows - 1 - r) * cols, static_cast<size_t>(cols) * sizeof(float));
#else
    (void)height;
    throw GPUUnavailable("Nie udało się odczytać bufora głębokości OpenGL.");
#endif
    return z;
}

Buffer2D<float> GPUViewport::read_depth() {
    int width = size.first, height = size.second;
    auto [x0, y0, x1, y1] = roi;
    std::vector<float> z = read_depth_roi();
    Buffer2D<float> depth(height, width, -std::numeric_limits<float>::infinity());
    std::vector<float> converted = gpu_scene::z_to_depth(z, *spec);
    int cols = x1 - x0 + 1;
    for (int r = 0; r <= y1 - y0; ++r) for (int c = 0; c < cols; ++c) depth.at(y0 + r, x0 + c) = converted[static_cast<size_t>(r) * cols + c];
    return depth;
}

Buffer2D<int32_t> GPUViewport::owner_pass() {
#ifdef MEBEL_HAVE_VTK
    backend->renderer->SetPass(nullptr);
    for (auto& mesh : current) {
        bool visible = mesh->actor->GetVisibility() && !mesh->group->ghost && !mesh->group->visual_only;
        mesh->id_actor->SetVisibility(visible); mesh->actor->VisibilityOff();
    }
    backend->window->Render();
#endif
    return read_owner();
}

Buffer2D<int32_t> GPUViewport::read_owner() {
    Rgba colors = read_rgba(true);
    auto [x0, y0, x1, y1] = roi;
    Buffer2D<int32_t> owners(size.second, size.first, -1);
    kind_buffer = Buffer2D<uint8_t>(size.second, size.first, 0);
    for (int r = 0; r < colors.rows; ++r) {
        for (int c = 0; c < colors.cols; ++c) {
            const uint8_t* px = &colors.data[(static_cast<size_t>(r) * colors.cols + c) * 4];
            int32_t code = static_cast<int32_t>(px[0]) | (static_cast<int32_t>(px[1]) << 8) | (static_cast<int32_t>(px[2]) << 16);
            if (code < 0 || code >= static_cast<int32_t>(surface_owner.size())) code = 0;
            owners.at(y0 + r, x0 + c) = surface_owner[code];
            kind_buffer->at(y0 + r, x0 + c) = surface_kind[code];
        }
    }
    (void)x1; (void)y1;
    return owners;
}

std::pair<Buffer2D<int32_t>, Buffer2D<float>> GPUViewport::ghost_fallback(Buffer2D<int32_t> owner, Buffer2D<float> depth) {
    // Prefer opaque interiors; retain ghost hits where the image is empty.
    bool any_ghost = false; for (auto& mesh : current) if (mesh->group->ghost) { any_ghost = true; break; }
    if (!any_ghost) return {owner, depth};
    Buffer2D<uint8_t> kinds = *kind_buffer;
#ifdef MEBEL_HAVE_VTK
    for (auto& mesh : current) mesh->id_actor->SetVisibility(mesh->group->ghost);
    backend->window->Render();
#endif
    Buffer2D<int32_t> ghost_owner = read_owner(); Buffer2D<float> ghost_depth = read_depth();
    std::vector<bool> room(ids.size(), false);
    for (size_t i = 0; i < ids.size(); ++i) room[i] = ids[i].rfind("scene:room:", 0) == 0;
    for (size_t k = 0; k < owner.data.size(); ++k) {
        bool background = owner.data[k] < 0;
        if (owner.data[k] >= 0 && room[static_cast<size_t>(owner.data[k])]) background = background || (ghost_depth.data[k] > depth.data[k] + .001f);
        if (background && ghost_owner.data[k] >= 0) {
            owner.data[k] = ghost_owner.data[k]; depth.data[k] = ghost_depth.data[k]; kinds.data[k] = kind_buffer->data[k];
        }
    }
    kind_buffer = kinds;
    return {owner, depth};
}

Rgba GPUViewport::frame(const core::Project& project, const core::Camera& camera_, std::pair<double, double> size_, const FrameOptions& options) {
    if (closed) throw GPUUnavailable("Kontekst OpenGL jest zamknięty.");
    double start = perf_counter();
    std::set<std::string> ghosts = gpu_scene::ghost_ids(project, options.xray, options.xray_ids);
    std::set<std::string> selection = options.selected_ids;
    if (options.selected) selection.insert(*options.selected);
    bool changed = sync(project, options.interactive, selection, ghosts, options.explode, camera_);
    double prepared = perf_counter();
    double scale;
    if (options.interactive) scale = options.quality == "draft" ? .45 : options.quality == "balanced" ? .55 : options.quality == "high" ? .75 : .55;
    else scale = options.quality == "draft" ? .75 : 1;
    quality = options.quality;
    configure_camera(camera_, size_, scale);
    configure_lights(project, camera_);
    std::set<std::string> visible = gpu_scene::visible_architecture(project, camera_);
    material_pass(project, visible);
    quality_pass(options.quality, options.interactive); double configured = perf_counter();
#ifdef MEBEL_HAVE_VTK
    backend->window->Render();
#endif
    double rendered = perf_counter(); Rgba color = read_rgba(); double colored = perf_counter();
    double beauty = perf_counter();
    Buffer2D<int32_t> owner = owner_pass();
    // The ID pass defines picking and annotation occlusion. Ghost surfaces
    // deliberately expose interior ownership, so use its matching depth.
    Buffer2D<float> depth = read_depth();
    std::tie(owner, depth) = ghost_fallback(std::move(owner), std::move(depth));
    if (size != output_size) {
        // Same nearest pixel-centre mapping as Pillow and the CPU renderer.
        std::vector<int> xs(output_size.first), ys(output_size.second);
        for (int i = 0; i < output_size.first; ++i) xs[i] = std::min(size.first - 1, static_cast<int>(std::floor((i + .5) * size.first / output_size.first)));
        for (int i = 0; i < output_size.second; ++i) ys[i] = std::min(size.second - 1, static_cast<int>(std::floor((i + .5) * size.second / output_size.second)));
        Buffer2D<float> depth2(output_size.second, output_size.first, 0); Buffer2D<int32_t> owner2(output_size.second, output_size.first, 0); Buffer2D<uint8_t> kind2(output_size.second, output_size.first, 0);
        for (int y = 0; y < output_size.second; ++y) for (int x = 0; x < output_size.first; ++x) {
            depth2.at(y, x) = depth.at(ys[y], xs[x]); owner2.at(y, x) = owner.at(ys[y], xs[x]); kind2.at(y, x) = kind_buffer->at(ys[y], xs[x]);
        }
        depth = std::move(depth2); owner = std::move(owner2); kind_buffer = std::move(kind2);
        // Image.resize(NEAREST): ImagingTransform ze współczynnikiem a0 = in/out i przesunięciem 0.5*a0.
        Rgba resized; resized.rows = output_size.second; resized.cols = output_size.first; resized.data.resize(static_cast<size_t>(resized.rows) * resized.cols * 4);
        double a0x = static_cast<double>(color.cols) / output_size.first, a0y = static_cast<double>(color.rows) / output_size.second;
        for (int y = 0; y < resized.rows; ++y) {
            int sy = std::clamp(static_cast<int>(std::floor(a0y * .5 + y * a0y)), 0, color.rows - 1);
            for (int x = 0; x < resized.cols; ++x) {
                int sx = std::clamp(static_cast<int>(std::floor(a0x * .5 + x * a0x)), 0, color.cols - 1);
                std::memcpy(&resized.data[(static_cast<size_t>(y) * resized.cols + x) * 4], &color.data[(static_cast<size_t>(sy) * color.cols + sx) * 4], 4);
            }
        }
        color = std::move(resized);
    }
    depth_buffer = depth; id_buffer = owner;
    material_pass(project, visible);
    long long face_count = 0; for (auto& g : groups) face_count += static_cast<long long>(g->offsets.size()) - 1;
    double finished = perf_counter();
    diagnostics = {{"engine", "vtk-opengl"}, {"geometry_changed", changed}, {"interactive", options.interactive}, {"quality", options.quality}, {"sample_scale", sample_scale},
                   {"actors", current.size()}, {"faces", face_count}, {"geometry_ms", (prepared - start) * 1000}, {"beauty_ms", (beauty - prepared) * 1000},
                   {"configure_ms", (configured - prepared) * 1000}, {"render_ms", (rendered - configured) * 1000}, {"rgba_read_ms", (colored - rendered) * 1000},
                   {"depth_read_ms", (beauty - colored) * 1000}, {"owner_pass_ms", (finished - beauty) * 1000}, {"total_ms", (finished - start) * 1000}};
    viewport_render::RenderedFaces cheap = gpu_scene::cheap_drawn(*context, groups, camera_, size_, selection, options.explode, visible);
    drawn = cheap.faces;
    output_roi = {std::max(0, static_cast<int>(std::floor(static_cast<double>(roi[0]) * output_size.first / size.first))),
                  std::max(0, static_cast<int>(std::floor(static_cast<double>(roi[1]) * output_size.second / size.second))),
                  std::min(output_size.first - 1, static_cast<int>(std::ceil(static_cast<double>(roi[2] + 1) * output_size.first / size.first))),
                  std::min(output_size.second - 1, static_cast<int>(std::ceil(static_cast<double>(roi[3] + 1) * output_size.second / size.second)))};
    return color;
}

// --- Stan GPU przypięty do płótna (w Pythonie: atrybuty canvas._gpu_viewport, _gpu_photo, _gpu_destroy_bound)
namespace {
struct CanvasGpu {
    std::unique_ptr<GPUViewport> viewport;
    std::unique_ptr<tk::PhotoImage> photo;
    bool destroy_bound = false;
};
std::map<tk::Canvas*, CanvasGpu>& registry() { static std::map<tk::Canvas*, CanvasGpu> states; return states; }
}  // namespace

native_viewport::ViewportResult paint_gpu(tk::Canvas* canvas, const Request& request) {
    if (!request.project) throw py::ValueError("paint_gpu wymaga projektu.");
    auto* state_target = dynamic_cast<depth_raster::CanvasState*>(canvas);
    if (state_target == nullptr) throw GPUUnavailable("Płótno nie przechowuje buforów głębokości.");
    CanvasGpu& state = registry()[canvas];
    try {
        if (!state.viewport || state.viewport->closed) {
            state.viewport = std::make_unique<GPUViewport>();
            if (!state.destroy_bound) {
                canvas->bind("<Destroy>", [canvas](tk::Event& event) {
                    if (event.widget == canvas) { close_gpu(canvas); registry().erase(canvas); }
                    return std::string();
                }, true);
                registry()[canvas].destroy_bound = true;
            }
        }
        GPUViewport& viewport = *state.viewport;
        FrameOptions options; options.interactive = request.interactive; options.selected_ids = request.selected_ids; options.selected = request.selected;
        options.explode = request.explode; options.xray = request.xray; options.xray_ids = request.xray_ids; options.quality = request.quality;
        Rgba rgba = viewport.frame(*request.project, request.camera, request.size, options);
        state_target->_depth_buffer = viewport.depth_buffer; state_target->_id_buffer = viewport.id_buffer;
        state_target->_depth_ids = viewport.ids; state_target->_depth_sample_scale = viewport.sample_scale;
        state_target->_render_diagnostics = viewport.diagnostics;
        auto [x0, y0, x1, y1] = viewport.output_roi;
        int cols = x1 - x0 + 1, rows = y1 - y0 + 1;
        std::vector<uint8_t> crop(static_cast<size_t>(rows) * cols * 4);
        for (int r = 0; r < rows; ++r) std::memcpy(&crop[static_cast<size_t>(r) * cols * 4], &rgba.data[(static_cast<size_t>(y0 + r) * rgba.cols + x0) * 4], static_cast<size_t>(cols) * 4);
        state.photo = std::make_unique<tk::PhotoImage>(canvas, cols, rows, crop);
        canvas->create_image(x0, y0, {{"anchor", "nw"}, {"image", *state.photo}, {"tags", "viewport.gpu_model"}});
        state_target->_gpu_error.reset();
        native_viewport::ViewportResult result; result.ok = true; result.engine = "vtk-opengl"; result.drawn = viewport.drawn; result.diagnostics = viewport.diagnostics;
        return result;
    } catch (const GPUUnavailable& exc) {
        close_gpu(canvas);
        state_target->_gpu_error = exc.what();
        native_viewport::ViewportResult result; result.ok = false; result.engine = "cpu"; result.error = exc.what();
        return result;
    }
}

json pick_world(tk::Canvas* canvas, double x, double y) {
    auto found = registry().find(canvas);
    if (found == registry().end() || !found->second.viewport || found->second.viewport->closed) return nullptr;
    GPUViewport& viewport = *found->second.viewport;
    if (!viewport.id_buffer || !viewport.depth_buffer || !viewport.kind_buffer || !viewport.camera) return nullptr;
    int px = static_cast<int>(std::floor(x)), py_ = static_cast<int>(std::floor(y));
    int height = viewport.id_buffer->rows, width = viewport.id_buffer->cols;
    if (!(0 <= px && px < width && 0 <= py_ && py_ < height)) return nullptr;
    int index = viewport.id_buffer->at(py_, px); double depth = static_cast<double>(viewport.depth_buffer->at(py_, px));
    if (index < 0 || !std::isfinite(depth)) return nullptr;
    int kind_code = viewport.kind_buffer->at(py_, px);
    static const char* kinds[3] = {"part", "fitting", "scene"};
    std::string kind = kinds[kind_code];
    core::Point3 w = gpu_scene::unproject(*viewport.camera, {static_cast<double>(viewport.output_size.first), static_cast<double>(viewport.output_size.second)}, px + .5, py_ + .5, depth);
    return json{{"id", viewport.ids[static_cast<size_t>(index)]}, {"kind", kind}, {"precise_surface", kind == "part"}, {"world", json::array({w[0], w[1], w[2]})}, {"depth", depth}};
}

void close_gpu(tk::Canvas* canvas) {
    auto found = registry().find(canvas);
    if (found == registry().end()) return;
    try {
        if (found->second.viewport) found->second.viewport->close();
    } catch (...) {
        found->second.viewport.reset(); throw;
    }
    found->second.viewport.reset();
}

}  // namespace gpu_viewport
