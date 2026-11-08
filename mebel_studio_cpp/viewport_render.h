// Cached software viewport. Geometry is independent of the orbit camera.
//
// Exact CAD faces remain available to manufacturing and exports. The settled
// viewport tessellates the same drilled solid at a visual tolerance. Movement
// uses coarse solids, stock boards and separate bought-component proxies;
// decorative cuts, fine recess walls and drilling return when movement ends.
// Only presentation data is cached. Parts, their CNC geometry and project data
// are never modified here.
//
// Tłumaczenie 1:1 viewport_render.py. Obiekt Geometry z Pythona to Record:
// kopia (copy.copy) współdzieli geometrię (wierzchołki, indeksy, normalne,
// środki), ale ma własny owner/color/lit_colors — adresy record.vertices i
// record.indices są więc stałe tak samo jak id() krotek w Pythonie.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core.h"
#include "render_style.h"

namespace viewport_render {

using json = core::json;
using Point3 = core::Point3;
using Polygon = std::vector<Point3>;
using Face = std::vector<int>;

// Tryb local_geometry(key, interactive): False / True / 'display'.
enum class Mode { Exact, Coarse, Display };

// ('cad', shape_key) albo ('stock', points, thickness)
struct GeomKey {
    bool cad = false;
    std::string shape;                 // key[1] dla 'cad'
    std::vector<core::Point> points;   // key[1] dla 'stock'
    double t = 0;                      // key[2] dla 'stock'
    std::string text() const;
};

// (plane, x, y, z, rx, ry, rz, frozen(layout)) — także atrapa SimpleNamespace z world_record.
struct Pose {
    std::string plane = "XY";
    double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
    json layout = json::object();
    std::string text() const;
};

// (id, geometry_key, pose_key, visible, group, material) — element part_keys
struct PartKey {
    std::string id;
    GeomKey geometry;
    Pose pose;
    bool visible = true;
    std::string group, material;
};

struct Indexed {
    std::vector<Point3> vertices;
    std::vector<Face> indices;
};

struct Mesh {
    std::vector<Point3> vertices;
    std::vector<Face> indices;
    std::vector<Point3> normals, centers;
};

struct Record {
    std::shared_ptr<const Mesh> geometry;
    const std::vector<Point3>& vertices;
    const std::vector<Face>& indices;
    const std::vector<Point3>& normals;
    const std::vector<Point3>& centers;
    std::string owner, color;
    bool mesh = false, fitting = false, scene_object = false, emissive = false, architecture = false;
    int sign = 1;
    mutable std::optional<std::vector<std::string>> lit_colors;
    mutable int lit_mode = 0;  // tryb grafiki, dla którego policzono lit_colors (0 klasyczny, 1 realistyczny, 2 szkic ołówkiem)

    explicit Record(std::shared_ptr<const Mesh> g)
        : geometry(std::move(g)), vertices(geometry->vertices), indices(geometry->indices),
          normals(geometry->normals), centers(geometry->centers) {}
    // copy.copy(record): ta sama geometria, osobne pola prezentacji
    Record(const Record& o)
        : geometry(o.geometry), vertices(geometry->vertices), indices(geometry->indices),
          normals(geometry->normals), centers(geometry->centers), owner(o.owner), color(o.color), mesh(o.mesh),
          fitting(o.fitting), scene_object(o.scene_object), emissive(o.emissive), architecture(o.architecture),
          sign(o.sign), lit_colors(o.lit_colors), lit_mode(o.lit_mode) {}
    Record& operator=(const Record&) = delete;
};
using RecordPtr = std::shared_ptr<const Record>;

struct GeometryOptions {
    std::string owner, color;
    bool mesh = false, fitting = false, scene_object = false, emissive = false;
    int sign = 1;
};

// class Geometry
std::shared_ptr<Record> geometry(Indexed data, const GeometryOptions& options);

// --- funkcje modułu --------------------------------------------------------
// frozen(value): kanoniczny tekst z posortowanymi kluczami słowników (klucz pamięci podręcznej)
std::string frozen_text(const json& value);
bool mesh_part(const core::Part& part);
GeomKey geometry_key(const core::Part& part);
Pose pose_key(const core::Part& part);
Indexed indexed(const std::vector<Polygon>& polygons);
std::shared_ptr<const Indexed> local_geometry(const GeomKey& key, Mode mode = Mode::Exact, bool explicit_mode = false);
Indexed merge_coplanar(const Indexed& data);
std::pair<std::vector<Point3>, int> transform(const Pose& pose, const std::vector<Point3>& vertices);
std::pair<std::vector<Point3>, int> transform(const core::Part& part, const std::vector<Point3>& vertices);
std::optional<std::vector<RecordPtr>> preview_front(const core::Part& part, const std::string& color);
std::optional<std::vector<RecordPtr>> preview_board(const core::Part& part, const GeomKey& key, const std::string& color);
bool small_mounting_drill(const json& operation);
std::vector<Polygon> surface_patches(const Polygon& polygon);
json geometry_faces(const core::Part& part, bool interactive = false);
std::pair<Point3, Point3> normal_center(const std::vector<Point3>& points, const std::vector<int>& indices);
std::vector<std::shared_ptr<Record>> fitting_geometry(const core::Project& project, const std::vector<PartKey>& part_keys);
RecordPtr world_record(const GeomKey& key, const Pose& pose, Mode mode = Mode::Exact, bool lighting_patches = false);
std::shared_ptr<Record> owner_record(const core::Part& part, const GeomKey& key, const std::string& color,
                                     Mode mode = Mode::Exact, bool lighting_patches = false);
std::vector<RecordPtr> fitting_preview(const RecordPtr& record, const core::Part& part, bool emissive = false);
std::array<int, 3> rgb(const std::string& color);
std::string color_hex(const std::vector<double>& values);
std::string lit_color(const std::string& color, const Point3& normal, const Point3& center, const json& lights,
                      double brightness = 1, const std::string& room = "neutral", bool emissive = false);
// Tryb realistyczny: półkulowe światło otoczenia + światło główne i wypełniające + światła LED (bez zmian względem klasycznego).
std::vector<std::string> lit_colors_realistic(const std::string& color, const std::vector<Point3>& normals,
                                              const std::vector<Point3>& centers, const json& lights, double brightness,
                                              const std::string& room, bool emissive, render_style::Mode mode = render_style::Mode::Realistic);
std::vector<std::string> lit_colors(const std::string& color, const std::vector<Point3>& normals,
                                    const std::vector<Point3>& centers, const json& lights, double brightness = 1,
                                    const std::string& room = "neutral", bool emissive = false);
json furniture_lights(const core::Project& project, const std::map<std::string, const core::Part*>& owners);

// dict z zachowaną kolejnością wstawiania (groups.setdefault(...).extend(...))
struct OrderedGroups {
    std::vector<std::pair<std::string, std::vector<Point3>>> items;
    std::vector<Point3>& operator[](const std::string& key) {
        for (auto& item : items) if (item.first == key) return item.second;
        items.emplace_back(key, std::vector<Point3>{});
        return items.back().second;
    }
    auto begin() const { return items.begin(); }
    auto end() const { return items.end(); }
    size_t size() const { return items.size(); }
};

// class SceneContext
struct Context {
    Context(const core::Project& project, const std::vector<PartKey>& part_keys);

    std::vector<RecordPtr> records(bool interactive = false);
    const std::vector<std::string>& face_colors(const Record& record) const;
    const core::Part* owner(const std::string& id) const;

    // owner id -> migawka części (tylko widoczne), w kolejności projektu
    std::vector<std::shared_ptr<core::Part>> owner_store;
    std::map<std::string, const core::Part*> owner_map;
    std::vector<const core::Part*> owners;

    OrderedGroups groups;
    std::vector<Point3> stock;
    struct PartEntry { std::string owner; GeomKey key; std::string color; bool is_mesh; };
    std::vector<PartEntry> parts;
    std::vector<RecordPtr> fittings, room;
    std::map<std::string, RecordPtr> full;
    std::vector<Point3> room_vertices;
    json room_lights = json::array();
    std::vector<RecordPtr> architecture;
    std::vector<Point3> architecture_vertices;
    json lights = json::array();
    json scene = json::object();
    json hardware = json::array();
    std::map<std::string, std::vector<RecordPtr>> preview;
    std::vector<RecordPtr> fitting_previews;
    // paint_background dopisuje dynamiczny atrybut context.shadow_hulls
    std::map<bool, std::vector<std::pair<double, std::vector<std::array<double, 2>>>>> shadow_hulls;
    // Tryb realistyczny: obwiednia stóp, dolna i górna krawędź każdej grupy mebli (do cienia rzucanego).
    struct CastHull { double bottom, top; std::vector<std::array<double, 2>> hull; };
    std::map<bool, std::vector<CastHull>> cast_hulls;
};

struct RenderedFaces { core::json faces = core::json::array(); core::json stats = core::json::object(); };

void clear_caches();
std::shared_ptr<Context> prepare_scene(const core::Project& project, bool interactive = false);
// Statystyki ostatniego render() — w Pythonie atrybut `.stats` zwróconej listy.
const json& last_stats();
RenderedFaces render_with_stats(const core::Project& project, const core::Camera& camera, double width, double height,
                                const std::optional<std::string>& selected = std::nullopt, double explode = 0,
                                bool interactive = false, Context* context = nullptr);
json render(const core::Project& project, const core::Camera& camera, double width, double height,
            const std::optional<std::string>& selected = std::nullopt, double explode = 0, bool interactive = false,
            Context* context = nullptr);
// Liczba chybień pamięci podręcznej local_geometry (cache_info().misses).
long long local_geometry_misses();

}  // namespace viewport_render
