// Retained viewport geometry and camera math, independent of VTK/Tk.
#include "gpu_scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include "assembly.h"
#include "pyutil.h"

#if __has_include("room_architecture.h")
#include "room_architecture.h"
#define GPU_SCENE_HAS_ROOM_ARCHITECTURE 1
#endif

namespace gpu_scene {

using viewport_render::Record;
using viewport_render::RecordPtr;

namespace {
// str.lower() także dla polskich liter (UTF-8).
std::string lower_text(const std::string& text) {
    static const std::vector<std::pair<std::string, std::string>> polish = {
        {"Ą", "ą"}, {"Ć", "ć"}, {"Ę", "ę"}, {"Ł", "ł"}, {"Ń", "ń"}, {"Ó", "ó"}, {"Ś", "ś"}, {"Ź", "ź"}, {"Ż", "ż"}};
    std::string out;
    for (size_t i = 0; i < text.size();) {
        bool replaced = false;
        for (auto& [upper, lower] : polish)
            if (text.compare(i, upper.size(), upper) == 0) { out += lower; i += upper.size(); replaced = true; break; }
        if (replaced) continue;
        char ch = text[i++];
        out += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    return out;
}

bool contains_any(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* word : words) if (text.find(word) != std::string::npos) return true;
    return false;
}

std::array<int, 3> hex_rgb(const std::string& color) {
    return {static_cast<int>(py::int16(color.substr(1, 2))), static_cast<int>(py::int16(color.substr(3, 2))), static_cast<int>(py::int16(color.substr(5, 2)))};
}

std::string ptr_text(const void* pointer) { return std::to_string(reinterpret_cast<uintptr_t>(pointer)); }
}  // namespace

Basis camera_basis(const core::Camera& camera) {
    auto b = camera._basis(); double ca = b[0], sa = b[1], cb = b[2], sb = b[3];
    return {{ca, -sa, 0}, {sa * sb, ca * sb, cb}, {-sa * cb, -ca * cb, sb}};
}

CameraSpec camera_spec(const core::Camera& camera, std::pair<double, double> size, const std::vector<Point3>& vertices) {
    double width = size.first, height = size.second; Basis basis = camera_basis(camera);
    Point3 center = camera.center;
    double radius = 1000;  // max(..., default=1000)
    if (!vertices.empty()) {
        radius = -std::numeric_limits<double>::infinity();
        for (auto& p : vertices) radius = std::max(radius, std::hypot(center[0] - p[0], center[1] - p[1], center[2] - p[2]));
    }
    double distance = std::max(10000.0, radius * 2 + 2000);
    double extent = std::max(1000.0, radius + 1000);
    CameraSpec spec;
    for (int i = 0; i < 3; ++i) spec.position[i] = center[i] + distance * basis.toward[i];
    spec.focal_point = center; spec.up = basis.up;
    spec.parallel_scale = height / (2 * std::max(camera.scale, 1e-6));
    spec.window_center = {-2 * camera.pan[0] / std::max(width, 1.0), 2 * camera.pan[1] / std::max(height, 1.0)};
    spec.clipping = {distance - extent, distance + extent}; spec.distance = distance;
    return spec;
}

std::vector<float> z_to_depth(const std::vector<float>& zbuffer, const CameraSpec& spec) {
    double near_ = spec.clipping.first, far_ = spec.clipping.second;
    // numpy: skalar Pythona jest „słaby” — działania na tablicy float32 zostają w float32.
    float offset = static_cast<float>(spec.distance - near_), range = static_cast<float>(far_ - near_);
    float limit = static_cast<float>(1 - 1e-7);
    std::vector<float> depth(zbuffer.size());
    for (size_t i = 0; i < zbuffer.size(); ++i) {
        float scaled = zbuffer[i] * range;
        depth[i] = offset - scaled;
        if (zbuffer[i] >= limit) depth[i] = -std::numeric_limits<float>::infinity();
    }
    return depth;
}

Point3 unproject(const core::Camera& camera, std::pair<double, double> size, double x, double y, double depth) {
    Basis basis = camera_basis(camera);
    double u = (x - size.first / 2 - camera.pan[0]) / camera.scale;
    double v = -(y - size.second / 2 - camera.pan[1]) / camera.scale;
    Point3 out;
    for (int i = 0; i < 3; ++i) out[i] = camera.center[i] + u * basis.right[i] + v * basis.up[i] + depth * basis.toward[i];
    return out;
}

std::string record_key(const Record& record) {
    // MeshGroup keeps records alive. Numeric identities cannot be recycled
    // while a retained actor still references the corresponding arrays.
    return "(" + record.owner + "," + ptr_text(&record.vertices) + "," + ptr_text(&record.indices) + "," + record.color + "," + std::to_string(record.sign) + "," +
           (record.fitting ? "1" : "0") + (record.scene_object ? "1" : "0") + (record.emissive ? "1" : "0") + (record.architecture ? "1" : "0") + ")";
}

std::vector<MeshGroupPtr> mesh_groups(viewport_render::Context& context, bool interactive) {
    std::vector<RecordPtr> records = context.records(interactive);
    records.insert(records.end(), context.architecture.begin(), context.architecture.end());
    std::set<std::pair<std::string, std::string>> leds;
    if (context.hardware.is_array()) {
        for (auto& h : context.hardware) {
            if (!h.is_object() || py::get(h, "kind") != json("led_strip")) continue;
            json owner = py::get(h, "owner_id"), color = py::get(h, "color", json("#fff3bf"));
            if (owner.is_string() && color.is_string()) leds.insert(std::pair<std::string, std::string>(owner.get<std::string>(), color.get<std::string>()));
        }
    }
    bool led_enabled = !context.scene.is_object() || !context.scene.contains("led_enabled") || py::truthy(context.scene["led_enabled"]);
    using Flags = std::tuple<std::string, std::string, bool, bool, bool, bool>;
    std::map<Flags, size_t> index; std::vector<std::pair<Flags, std::vector<RecordPtr>>> grouped;
    for (auto& record : records) {
        bool is_led = record->fitting && leds.contains({record->owner, record->color});
        std::string color = (!is_led || led_enabled) ? record->color : "#c7c6bb";
        Flags flags{record->owner, color, record->fitting, record->scene_object, record->emissive || (is_led && led_enabled), record->architecture};
        auto found = index.find(flags);
        if (found == index.end()) { found = index.emplace(flags, grouped.size()).first; grouped.push_back({flags, {}}); }
        grouped[found->second].second.push_back(record);
    }
    std::vector<MeshGroupPtr> out;
    for (auto& [flags, items] : grouped) {
        std::vector<float> points; std::vector<std::vector<int64_t>> faces; int64_t base = 0;
        for (auto& record : items) {
            for (auto& v : record->vertices) for (int i = 0; i < 3; ++i) points.push_back(static_cast<float>(v[i]));
            for (auto& face : record->indices) {
                std::vector<int64_t> indices;
                if (record->sign >= 0) for (int i : face) indices.push_back(i + base);
                else for (auto it = face.rbegin(); it != face.rend(); ++it) indices.push_back(*it + base);
                faces.push_back(std::move(indices));
            }
            base += static_cast<int64_t>(record->vertices.size());
        }
        auto group = std::make_shared<MeshGroup>();
        group->offsets.assign(faces.size() + 1, 0);
        for (size_t i = 0; i < faces.size(); ++i) {
            group->offsets[i + 1] = group->offsets[i] + static_cast<int64_t>(faces[i].size());
            group->connectivity.insert(group->connectivity.end(), faces[i].begin(), faces[i].end());
        }
        std::string key = "(";
        for (auto& r : items) key += record_key(*r);
        key += ")";
        auto& [owner, color, fitting, scene_object, emissive, architecture] = flags;
        group->key = key; group->owner = owner; group->color = color; group->records = items; group->vertices = std::move(points);
        group->fitting = fitting; group->scene_object = scene_object; group->emissive = emissive; group->architecture = architecture;
        out.push_back(group);
    }
    return out;
}

std::set<std::string> ghost_ids(const core::Project& project, bool xray, const std::optional<std::set<std::string>>& xray_ids) {
    if (!xray) return {};
    if (xray_ids) return *xray_ids;
    std::set<std::string> out;
    for (auto& p : project.parts) {
        std::string role = assembly::inferred_role(p);
        if (role == "door" || role == "drawer") out.insert(p.id);
    }
    return out;
}

std::set<std::string> visible_architecture(const core::Project& project, const core::Camera& camera) {
#ifdef GPU_SCENE_HAS_ROOM_ARCHITECTURE
    std::set<std::string> out;
    json faces = room_architecture::world_faces(project, camera);
    for (auto& f : faces) out.insert(f["id"].get<std::string>());
    return out;
#else
    (void)project; (void)camera;
    return {};  // except ImportError
#endif
}

MaterialStyle material_style(const core::Project& project, const MeshGroup& group) {
    const core::Part* part = nullptr;
    for (auto& p : project.parts) if (p.id == group.owner) { part = &p; break; }
    const core::Material* material = nullptr;
    if (part) for (auto& m : project.materials) if (m.id == part->material) { material = &m; break; }
    std::string name = lower_text((material ? material->name : std::string()) + " " + (material ? material->kind : std::string()));
    bool metal = group.fitting && !group.scene_object && !group.emissive;
    if (metal) return {.3, .65, .65, 70, false};
    bool glossy = contains_any(name, {"połysk", "polysk", "gloss", "lakier"});
    bool wood = contains_any(name, {"dąb", "oak", "orzech", "sosna", "buk", "jesion", "brzoza", "drewno", "sklejka", "wenge", "mahoń"});
    if (group.architecture && group.owner.size() >= 6 && group.owner.compare(group.owner.size() - 6, 6, ":floor") == 0) {
        auto rgb = hex_rgb(group.color); wood = rgb[0] > rgb[2] * 1.2;
    }
    return {glossy ? .22 : .65, 0, glossy ? .4 : .16, glossy ? 50.0 : 22.0, wood};
}

std::vector<MeshGroupPtr> batch_groups(const std::vector<MeshGroupPtr>& groups, const core::Project& project, const std::set<std::string>& selection,
                                       const std::set<std::string>& ghosts, double explode, const core::Camera& camera) {
    std::map<std::string, const core::Part*> owners; for (auto& p : project.parts) owners[p.id] = &p;
    using Bucket = std::tuple<std::string, bool, bool, bool, double, double, bool, double, bool>;
    std::map<Bucket, size_t> index; std::vector<std::pair<Bucket, std::vector<std::pair<int, MeshGroupPtr>>>> buckets;
    int code = 0;
    for (auto& group : groups) {
        ++code;
        MaterialStyle style = material_style(project, *group);
        bool ghost = ghosts.contains(group->owner) && !group->fitting;
        Bucket bucket{group->architecture ? group->owner : std::string(), group->fitting && !group->scene_object, group->emissive, ghost, style.roughness, style.specular,
                      style.wood, group->opacity, group->visual_only};
        auto found = index.find(bucket);
        if (found == index.end()) { found = index.emplace(bucket, buckets.size()).first; buckets.push_back({bucket, {}}); }
        buckets[found->second].second.push_back({code, group});
    }
    std::vector<MeshGroupPtr> out;
    for (auto& [bucket, items] : buckets) {
        std::vector<float> points; std::vector<int64_t> indices; std::vector<int64_t> sizes; std::vector<uint8_t> colors, ids; std::vector<viewport_render::RecordPtr> records; int64_t base = 0;
        for (auto& [code_, group] : items) {
            auto owner = owners.find(group->owner);
            core::Point3 shift{0, 0, 0};
            if (explode != 0 && owner != owners.end()) shift = core::explosion_shift(*owner->second, camera, explode);
            float fs[3] = {static_cast<float>(shift[0]), static_cast<float>(shift[1]), static_cast<float>(shift[2])};
            for (size_t k = 0; k < group->vertices.size(); ++k) points.push_back(group->vertices[k] + fs[k % 3]);
            for (auto c : group->connectivity) indices.push_back(c + base);
            for (size_t i = 0; i + 1 < group->offsets.size(); ++i) sizes.push_back(group->offsets[i + 1] - group->offsets[i]);
            base += static_cast<int64_t>(group->vertex_count());
            auto rgb = hex_rgb(group->color);
            std::array<uint8_t, 3> cell_color;
            if (selection.contains(group->owner)) {
                const double add[3] = {.3 * 25.5, .62 * 25.5, .78 * 25.5};
                for (int i = 0; i < 3; ++i) {
                    float scaled = static_cast<float>(rgb[i]) * static_cast<float>(.9);          // float32 * skalar
                    cell_color[i] = static_cast<uint8_t>(py::round_half_even(static_cast<double>(scaled) + add[i]));
                }
            } else {
                for (int i = 0; i < 3; ++i) cell_color[i] = static_cast<uint8_t>(rgb[i]);
            }
            size_t cells = group->offsets.size() - 1;
            for (size_t i = 0; i < cells; ++i) colors.insert(colors.end(), cell_color.begin(), cell_color.end());
            uint8_t id_bytes[3] = {static_cast<uint8_t>(code_ & 255), static_cast<uint8_t>((code_ >> 8) & 255), static_cast<uint8_t>((code_ >> 16) & 255)};
            for (size_t i = 0; i < cells; ++i) ids.insert(ids.end(), id_bytes, id_bytes + 3);
            records.insert(records.end(), group->records.begin(), group->records.end());
        }
        auto representative = items.front().second;
        std::string key = "(";
        for (auto& [code_, g] : items) key += g->key + ";";
        key += ")|";
        { std::vector<std::string> sel(selection.begin(), selection.end()); key += py::join(sel, ","); }
        key += std::string("|") + (std::get<3>(bucket) ? "1" : "0") + "|" + py::repr(explode) + "|";
        if (explode != 0) key += py::repr(camera.center[0]) + "," + py::repr(camera.center[1]) + "," + py::repr(camera.center[2]);
        auto batch = std::make_shared<MeshGroup>();
        batch->key = key; batch->owner = representative->architecture ? representative->owner : "@batch"; batch->color = "#ffffff"; batch->records = std::move(records);
        batch->vertices = std::move(points);
        batch->offsets.assign(sizes.size() + 1, 0);
        for (size_t i = 0; i < sizes.size(); ++i) batch->offsets[i + 1] = batch->offsets[i] + sizes[i];
        batch->connectivity = std::move(indices);
        batch->fitting = representative->fitting; batch->scene_object = representative->scene_object; batch->emissive = representative->emissive;
        batch->architecture = representative->architecture; batch->cell_colors = std::move(colors); batch->cell_ids = std::move(ids);
        batch->ghost = std::get<3>(bucket);
        for (auto& [code_, g] : items) batch->source_groups.push_back(g);
        batch->opacity = representative->opacity; batch->visual_only = representative->visual_only;
        out.push_back(batch);
    }
    return out;
}

viewport_render::RenderedFaces cheap_drawn(viewport_render::Context& context, const std::vector<MeshGroupPtr>& groups, const core::Camera& camera,
                                           std::pair<double, double> size, const std::set<std::string>& selected_ids, double explode, const std::set<std::string>& visible) {
    json faces = json::array();
    auto add = [&](const std::string& owner, const std::vector<Point3>& points, const std::string& color, const json& flags) {
        std::vector<Point3> q; for (auto& point : points) q.push_back(camera.project(point, size.first, size.second));
        std::vector<int> range; for (size_t i = 0; i < points.size(); ++i) range.push_back(static_cast<int>(i));
        auto [normal, center] = viewport_render::normal_center(points, range);
        json world = json::array(), projected = json::array(), depths = json::array(); std::vector<double> depth_values;
        for (auto& p : points) world.push_back(json::array({p[0], p[1], p[2]}));
        for (auto& p : q) { projected.push_back(json::array({p[0], p[1]})); depths.push_back(p[2]); depth_values.push_back(p[2]); }
        json face = {{"id", owner}, {"world", world}, {"points", projected}, {"depths", depths}, {"depth", py::sum(depth_values) / static_cast<double>(q.size())},
                     {"normal", json::array({normal[0], normal[1], normal[2]})}, {"center", json::array({center[0], center[1], center[2]})}, {"color", color},
                     {"selected", selected_ids.contains(owner)}, {"mesh", false}};
        for (auto& [k, v] : flags.items()) face[k] = v;
        faces.push_back(face);
    };
    std::map<std::string, std::string> colors; for (auto& g : groups) if (!g->fitting) colors[g->owner] = g->color;
    auto color_of = [&](const std::string& id) { auto f = colors.find(id); return f == colors.end() ? std::string("#c5b395") : f->second; };
    for (const core::Part* part : context.owners) {
        core::Point3 shift{0, 0, 0};
        if (explode != 0) shift = core::explosion_shift(*part, camera, explode);
        std::vector<std::vector<Point3>> rings;
        for (double w : {0.0, part->t}) {
            std::vector<Point3> ring;
            for (auto& uv : part->points()) {
                Point3 p = core::world(*part, uv[0], uv[1], w);
                ring.push_back({p[0] + shift[0], p[1] + shift[1], p[2] + shift[2]});
            }
            rings.push_back(std::move(ring));
        }
        std::vector<Point3> reversed(rings[0].rbegin(), rings[0].rend());
        add(part->id, reversed, color_of(part->id), json::object());
        add(part->id, rings[1], color_of(part->id), json::object());
    }
    for (auto& group : groups) {
        if (!group->fitting) continue;
        if (group->architecture && !visible.contains(group->owner)) continue;
        const Record& record = *group->records.front();
        if (record.indices.empty()) continue;
        std::vector<Point3> points; for (int i : record.indices[0]) points.push_back(record.vertices[i]);
        add(group->owner, points, group->color, json{{"fitting", true}, {"scene_object", group->scene_object}, {"architecture", group->architecture}});
    }
    viewport_render::RenderedFaces result; result.faces = faces;
    long long face_count = 0; for (auto& g : groups) face_count += static_cast<long long>(g->offsets.size()) - 1;
    result.stats = {{"engine", "vtk-opengl"}, {"faces", face_count}, {"representative", true}};
    return result;
}

}  // namespace gpu_scene
