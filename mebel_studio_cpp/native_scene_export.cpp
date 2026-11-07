// Presentation-only snapshots for the integrated native C++ viewport.
#include "native_scene_export.h"
#include "view_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>

#include "depth_raster.h"
#include "gpu_scene.h"
#include "pyutil.h"

namespace native_scene_export {

using core::Part;
using core::Point3;
using core::Project;
using py::ValueError;

namespace {

using Triangle = std::pair<std::array<Point3, 3>, Point3>;
using Record = viewport_render::Record;
using RecordPtr = viewport_render::RecordPtr;

// _TRIANGLES: OrderedDict (LRU 256) o kluczu (id(vertices), id(indices), sign);
// wartością jest (record, triangles) — rekord żyje tak długo, jak wpis.
using MeshKey = std::tuple<const void*, const void*, int>;
struct CacheEntry { RecordPtr record; std::vector<Triangle> triangles; };
std::list<std::pair<MeshKey, CacheEntry>> g_order;
std::map<MeshKey, std::list<std::pair<MeshKey, CacheEntry>>::iterator> g_index;

int kind_of(const Record& record) {
    if (record.architecture) return 3;
    if (record.scene_object) return 2;
    return record.fitting ? 1 : 0;
}

std::string rsplit_last(const std::string& text, char sep) {
    size_t at = text.rfind(sep);
    return at == std::string::npos ? text : text.substr(at + 1);
}

std::string label_of(int kind, const std::string& identity, const std::map<std::string, const Part*>& parts,
                     const std::map<std::string, std::string>& objects) {
    auto part = parts.find(identity);
    if (kind == 0) return part != parts.end() ? part->second->name : identity;
    if (kind == 1) return "Okucia — " + (part != parts.end() ? part->second->name : identity);
    if (kind == 2) {
        auto found = objects.find(identity);
        return found != objects.end() ? found->second : identity;
    }
    static const std::map<std::string, std::string> names = {
        {"floor", "Podłoga"}, {"ceiling", "Sufit"}, {"west", "Ściana zachodnia"},
        {"east", "Ściana wschodnia"}, {"north", "Ściana północna"}, {"south", "Ściana południowa"}};
    auto found = names.find(rsplit_last(identity, ':'));
    return found != names.end() ? found->second : identity;
}

void check_finite(const std::vector<double>& values, const char* what) {
    for (double v : values)
        if (!std::isfinite(v)) throw ValueError(std::string("Dane widoku zawierają niefinitywne wartości: ") + what);
}

// Rzutowanie na niezdegenerowaną płaszczyznę z zachowaniem skierowania ścian.
std::vector<Triangle> face_triangles(const Record& record, const std::vector<int>& face) {
    std::vector<int> indices(face);
    if (record.sign < 0) indices.assign(face.rbegin(), face.rend());
    std::vector<Point3> points;
    for (int i : indices) {
        long long at = i < 0 ? i + (long long)record.vertices.size() : i;
        if (at < 0 || at >= (long long)record.vertices.size()) throw py::IndexError("tuple index out of range");
        points.push_back(record.vertices[(size_t)at]);
    }
    std::vector<Triangle> out;
    if (points.size() < 3) return out;
    {
        std::vector<double> flat;
        for (auto& p : points) flat.insert(flat.end(), p.begin(), p.end());
        check_finite(flat, "wierzchołki");
    }
    double normal[3] = {0, 0, 0};
    for (size_t k = 0; k < points.size(); ++k) {
        const Point3& a = points[k];
        const Point3& b = points[(k + 1) % points.size()];
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    if (std::max({std::abs(normal[0]), std::abs(normal[1]), std::abs(normal[2])}) < 1e-12) return out;
    int drop = 0;
    for (int i = 1; i < 3; ++i) if (std::abs(normal[i]) > std::abs(normal[drop])) drop = i;
    int axes[2], n = 0;
    for (int i = 0; i < 3; ++i) if (i != drop) axes[n++] = i;
    std::vector<depth_raster::Point2> flat;
    for (auto& p : points) flat.push_back({p[(size_t)axes[0]], p[(size_t)axes[1]]});
    for (const auto& triangle : depth_raster::triangulate(flat)) {
        std::array<Point3, 3> vertices;
        for (int k = 0; k < 3; ++k) {
            if (triangle[(size_t)k] < 0 || triangle[(size_t)k] >= (int)points.size()) throw py::IndexError("tuple index out of range");
            vertices[(size_t)k] = points[(size_t)triangle[(size_t)k]];
        }
        const Point3 &a = vertices[0], &b = vertices[1], &c = vertices[2];
        double u[3], v[3];
        for (int i = 0; i < 3; ++i) { u[i] = b[(size_t)i] - a[(size_t)i]; v[i] = c[(size_t)i] - a[(size_t)i]; }
        Point3 cross = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        double length = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        if (length < 1e-12) continue;
        out.push_back({vertices, Point3{cross[0] / length, cross[1] / length, cross[2] / length}});
    }
    return out;
}

// struct.pack('f', x): OverflowError dla skończonej wartości poza zakresem float32
float pack_float(double x) {
    float f = static_cast<float>(x);
    if (std::isinf(f) && !std::isinf(x)) throw std::overflow_error("float too large to pack with f format");
    return f;
}

void put_u32(std::string& out, uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void put_f32(std::string& out, float v) { out.append(reinterpret_cast<const char*>(&v), 4); }

int hex_pair(const std::string& color, size_t at) {
    std::string text = at < color.size() ? color.substr(at, 2) : std::string();
    if (text.empty()) throw ValueError("invalid literal for int() with base 16: ''");
    int value = 0;
    for (char ch : text) {
        int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0) throw ValueError("invalid literal for int() with base 16: '" + text + "'");
        value = value * 16 + digit;
    }
    return value;
}

struct Owner {
    int kind;
    uint32_t flags;
    std::string id;
    std::string label;
    double lo[3], hi[3];
};

}  // namespace

std::pair<std::string, json> build_snapshot(const Project& project, const Options& options) {
    std::shared_ptr<viewport_render::Context> own_context;
    viewport_render::Context* context = options.context;
    if (!context) { own_context = viewport_render::prepare_scene(project); context = own_context.get(); }
    std::set<std::string> selected_ids = options.selected_ids;
    if (options.selected && !options.selected->empty()) selected_ids.insert(*options.selected);
    const std::set<std::string>& xray_ids = options.xray_ids;
    std::map<std::string, const Part*> parts;
    for (auto& p : project.parts) parts[p.id] = &p;
    std::map<std::string, std::string> objects;
    for (auto& obj : py::get(project.scene, "objects", json::array()))
        objects["scene:" + obj["id"].get<std::string>()] = obj["name"].get<std::string>();
    std::set<std::pair<json, json>> leds;
    for (auto& h : project.hardware)
        // Uwaga: insert({json, json}) wybiera przeciążenie initializer_list<pair> i próbuje zamienić napis na parę
        // (json::type_error 302 „type must be array, but is string”) — dlatego para budowana jawnie.
        if (py::get(h, "kind") == "led_strip") leds.insert(std::pair<json, json>(py::get(h, "owner_id"), py::get(h, "color", "#fff3bf")));
    double brightness = py::to_float(py::get(context->scene, "brightness", 1.0));
    check_finite({brightness}, "jasność");
    std::vector<Owner> owner_table;
    std::map<std::pair<int, std::string>, size_t> owner_lookup;
    std::string vertices;
    uint32_t vertex_count = 0;
    double scene_lo[3] = {INFINITY, INFINITY, INFINITY}, scene_hi[3] = {-INFINITY, -INFINITY, -INFINITY};
    long long kinds[4] = {0, 0, 0, 0};
    bool led_on = py::truthy(py::get(context->scene, "led_enabled", true));

    std::vector<RecordPtr> records = context->records(options.interactive);
    records.insert(records.end(), context->architecture.begin(), context->architecture.end());
    for (const RecordPtr& record : records) {
        int kind = kind_of(*record);
        std::string identity = record->owner;
        std::pair<int, std::string> key{kind, identity};
        std::string color = record->color;
        bool is_led = kind == 1 && leds.contains(std::pair<json, json>(json(identity), json(color)));
        bool emitted = record->emissive || (is_led && led_on);
        if (is_led && !led_on) color = "#c7c6bb";
        unsigned flags = emitted ? EMISSIVE : 0;
        if (kind == 1) flags |= FITTING;
        else if (kind == 2) flags |= SCENE;
        else if (kind == 3) flags |= ARCHITECTURE;
        gpu_scene::MeshGroup probe;
        probe.owner = identity; probe.fitting = kind == 1; probe.scene_object = kind == 2;
        probe.emissive = emitted; probe.architecture = kind == 3; probe.color = color;
        if (gpu_scene::material_style(project, probe).wood) flags |= WOOD;
        if (kind == 0 && xray_ids.contains(identity)) flags |= GHOST;
        if (selected_ids.contains(identity)) flags |= SELECTED;
        float rgba[4];
        for (int k = 0; k < 3; ++k) rgba[k] = pack_float(hex_pair(color, (size_t)(1 + 2 * k)) / 255.0);
        rgba[3] = 1.0f;
        Point3 shift{0, 0, 0};
        bool explode = options.explode != 0;
        if (explode && parts.contains(identity)) {
            if (!options.camera) throw json::type_error::create(302, "camera is None", nullptr);
            shift = core::explosion_shift(*parts[identity], *options.camera, options.explode);
        }
        MeshKey mesh_key{&record->vertices, &record->indices, record->sign};
        const std::vector<Triangle>* triangles;
        auto cached = g_index.find(mesh_key);
        if (cached == g_index.end()) {
            CacheEntry entry{record, {}};
            for (const auto& face : record->indices) {
                auto part = face_triangles(*record, face);
                entry.triangles.insert(entry.triangles.end(), part.begin(), part.end());
            }
            g_order.emplace_back(mesh_key, std::move(entry));
            g_index[mesh_key] = std::prev(g_order.end());
            triangles = &g_order.back().second.triangles;
            if (g_order.size() > 256) { g_index.erase(g_order.front().first); g_order.pop_front(); }
        } else {
            g_order.splice(g_order.end(), g_order, cached->second);
            triangles = &cached->second->second.triangles;
        }
        for (const auto& [base_positions, normal] : *triangles) {
            std::array<Point3, 3> positions = base_positions;
            if (explode)
                for (auto& point : positions) for (size_t i = 0; i < 3; ++i) point[i] = point[i] + shift[i];
            auto found = owner_lookup.find(key);
            if (found == owner_lookup.end()) {
                found = owner_lookup.emplace(key, owner_table.size()).first;
                Owner owner{kind, 0, std::string(KIND_NAMES[(size_t)kind]) + ":" + identity, label_of(kind, identity, parts, objects), {}, {}};
                for (int a = 0; a < 3; ++a) { owner.lo[a] = INFINITY; owner.hi[a] = -INFINITY; }
                owner_table.push_back(owner);
                ++kinds[kind];
            }
            uint32_t owner_index = (uint32_t)found->second;
            Owner& owner = owner_table[found->second];
            owner.flags |= flags;
            for (const Point3& position : positions) {
                float packed[3];
                try {
                    for (size_t a = 0; a < 3; ++a) packed[a] = pack_float(position[a]);
                } catch (const std::overflow_error&) {
                    throw ValueError("Geometria przekracza zakres formatu widoku.");
                }
                // Bounds match the float32 coordinates seen by the native renderer.
                check_finite({packed[0], packed[1], packed[2]}, "współrzędne float32");
                for (size_t a = 0; a < 3; ++a) {
                    owner.lo[a] = std::min(owner.lo[a], (double)packed[a]);
                    owner.hi[a] = std::max(owner.hi[a], (double)packed[a]);
                    scene_lo[a] = std::min(scene_lo[a], (double)packed[a]);
                    scene_hi[a] = std::max(scene_hi[a], (double)packed[a]);
                }
                for (float f : packed) put_f32(vertices, f);
                for (double n : normal) put_f32(vertices, (float)n);
                for (float f : rgba) put_f32(vertices, f);
                put_u32(vertices, owner_index);
                put_u32(vertices, flags);
                ++vertex_count;
            }
        }
    }

    // Podłoga i siatka jako zwykła geometria sceny: natywne renderery D3D11/D3D12/Vulkan
    // nie mają własnej siatki, a ich okno zasłania tło rysowane przez Tk. Geometria jest
    // oznaczona jako ARCHITECTURE (ten sam wycinek kamery co podłoga pokoju: patrząc od
    // dołu płyta znika), leży tuż pod meblami i nie wchodzi do granic sceny (Dopasuj).
    // Granice samych brył (do "Dopasuj" i diagnostyki), zanim doliczymy podłogę.
    double fit_lo[3] = {scene_lo[0], scene_lo[1], scene_lo[2]}, fit_hi[3] = {scene_hi[0], scene_hi[1], scene_hi[2]};
    double grid_step = 0;
    if (options.ground && vertex_count) {
        const double margin = 1200;
        double floor_z = std::min(0.0, scene_lo[2]);
        double cx = (scene_lo[0] + scene_hi[0]) / 2, cy = (scene_lo[1] + scene_hi[1]) / 2;
        double hx = std::max(300.0, (scene_hi[0] - scene_lo[0]) / 2) + margin;
        double hy = std::max(300.0, (scene_hi[1] - scene_lo[1]) / 2) + margin;
        double target = std::max(hx, hy) / 9;
        double power = std::pow(10.0, std::floor(std::log10(target)));
        grid_step = 10 * power;
        for (double multiplier : {1.0, 2.0, 5.0, 10.0})
            if (multiplier * power >= target) { grid_step = multiplier * power; break; }
        grid_step = std::max(grid_step, 100.0);
        double line = std::clamp(grid_step * 0.015, 1.5, 8.0);
        uint32_t owner_index = (uint32_t)owner_table.size();
        // Pole właściciela z zapasem na szerokość linii siatki (kreski przy krawędzi wystają o pół szerokości).
        const double owner_half_x = 60000 + 10, owner_half_y = 60000 + 10;
        Owner owner{3, ARCHITECTURE, "architecture:viewport.grid", "Siatka", {cx - owner_half_x, cy - owner_half_y, floor_z - 3}, {cx + owner_half_x, cy + owner_half_y, floor_z - 1}};
        owner_table.push_back(owner);
        auto quad = [&](double x0, double y0, double x1, double y1, double z, const float rgb[3]) {
            const double xs[6] = {x0, x1, x1, x0, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
            for (int i = 0; i < 6; ++i) {
                put_f32(vertices, (float)xs[i]); put_f32(vertices, (float)ys[i]); put_f32(vertices, (float)z);
                put_f32(vertices, 0.f); put_f32(vertices, 0.f); put_f32(vertices, 1.f);
                put_f32(vertices, rgb[0]); put_f32(vertices, rgb[1]); put_f32(vertices, rgb[2]); put_f32(vertices, 1.f);
                put_u32(vertices, owner_index); put_u32(vertices, ARCHITECTURE);
                ++vertex_count;
            }
        };
        // Kolory z zapasem na cieniowanie shadera (.40+.52*diffuse+.16*secondary).
        const float* floor_rgb = view_style::palette().floor_rgb;
        const float* grid_rgb = view_style::palette().grid_rgb;
        const float axis_rgb[3] = {0.55f, 0.58f, 0.64f};
        // Podłoga sięga daleko poza siatkę: natywne okno wypełnia tło kolorem DLL (u Ciebie ciemnym), więc płyta musi
        // zakrywać cały widok przy dowolnym oddaleniu (rzut ortogonalny, brak horyzontu).
        const double far_half = 60000;
        quad(cx - far_half, cy - far_half, cx + far_half, cy + far_half, floor_z - 3, floor_rgb);
        int lines = 0;
        for (double x = std::ceil((cx - hx) / grid_step) * grid_step; x <= cx + hx && lines < 200; x += grid_step, ++lines) {
            double w = std::fabs(x) < 1e-6 ? line * 2 : line;
            quad(x - w / 2, cy - hy, x + w / 2, cy + hy, floor_z - 2, std::fabs(x) < 1e-6 ? axis_rgb : grid_rgb);
        }
        for (double y = std::ceil((cy - hy) / grid_step) * grid_step; y <= cy + hy && lines < 400; y += grid_step, ++lines) {
            double w = std::fabs(y) < 1e-6 ? line * 2 : line;
            quad(cx - hx, y - w / 2, cx + hx, y + w / 2, floor_z - 2, std::fabs(y) < 1e-6 ? axis_rgb : grid_rgb);
        }
        // Natywny renderer sprawdza każdy wierzchołek względem granic sceny z nagłówka — podłoga musi się w nich mieścić,
        // inaczej cała scena jest odrzucana ("Wierzchołek wykracza poza granice sceny") i widok spada do wolnego rysowania.
        for (int a = 0; a < 2; ++a) {
            double center = a == 0 ? cx : cy;
            scene_lo[a] = std::min(scene_lo[a], center - far_half - 10);
            scene_hi[a] = std::max(scene_hi[a], center + far_half + 10);
        }
        scene_lo[2] = std::min(scene_lo[2], floor_z - 3);
        scene_hi[2] = std::max(scene_hi[2], floor_z - 1);
    }

    if (!vertex_count) for (int a = 0; a < 3; ++a) { scene_lo[a] = 0.0; scene_hi[a] = 0.0; }
    std::string result(MAGIC, 8);
    put_u32(result, (uint32_t)VERSION);
    put_u32(result, vertex_count);
    put_u32(result, (uint32_t)owner_table.size());
    for (double v : scene_lo) put_f32(result, pack_float(v));
    for (double v : scene_hi) put_f32(result, pack_float(v));
    put_f32(result, pack_float(brightness));
    for (const Owner& owner : owner_table) {
        put_u32(result, (uint32_t)owner.kind);
        put_u32(result, owner.flags);
        put_u32(result, (uint32_t)owner.id.size());
        put_u32(result, (uint32_t)owner.label.size());
        for (double v : owner.lo) put_f32(result, pack_float(v));
        for (double v : owner.hi) put_f32(result, pack_float(v));
        result += owner.id;
        result += owner.label;
    }
    result += vertices;
    json kinds_json = json::object();
    for (size_t i = 0; i < 4; ++i) kinds_json[KIND_NAMES[i]] = kinds[i];
    json sources = json::array();
    for (const Owner& owner : owner_table) {
        size_t at = owner.id.find(':');
        sources.push_back(json::array({owner.id.substr(at + 1), KIND_NAMES[(size_t)owner.kind]}));
    }
    json diagnostics = {{"format", "MS3DNAT1"}, {"version", VERSION}, {"vertices", vertex_count}, {"triangles", vertex_count / 3},
                        {"owners", owner_table.size()}, {"kinds", kinds_json},
                        {"bounds", json::array({json::array({fit_lo[0], fit_lo[1], fit_lo[2]}), json::array({fit_hi[0], fit_hi[1], fit_hi[2]})})},
                        {"brightness", brightness}, {"grid_step", grid_step}, {"bytes", result.size()}, {"project_unchanged", true}, {"owner_sources", sources}};
    return {std::move(result), std::move(diagnostics)};
}

}  // namespace native_scene_export
