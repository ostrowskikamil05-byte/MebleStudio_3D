// Tłumaczenie 1:1 viewport_render.py (patrz viewport_render.h).
#include "viewport_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include "cad_geometry.h"
#include "fitting_visuals.h"
#include "pyutil.h"
#include "room_architecture.h"
#include "room_objects.h"

namespace viewport_render {

using core::Part;
using core::Project;

namespace {

// sum() liczb float (CPython 3.12+, Neumaier) bez alokacji pomocniczych wektorów
class FSum {
public:
    void add(double x) {
        double t = f_ + x;
        if (std::isfinite(t)) {
            if (std::fabs(f_) >= std::fabs(x)) c_ += (f_ - t) + x;
            else c_ += (x - t) + f_;
        } else c_ = 0.0;
        f_ = t;
    }
    double result() const { return (c_ != 0.0 && std::isfinite(c_)) ? f_ + c_ : f_; }
private:
    double f_ = 0.0, c_ = 0.0;
};

double num(const json& value) { return py::to_float(value); }

Point3 point_of(const json& value) {
    if (!value.is_array() || value.size() < 3) throw py::IndexError("tuple index out of range");
    return {num(value[0]), num(value[1]), num(value[2])};
}

Polygon polygon_of(const json& value) {
    Polygon out;
    for (const auto& p : value) out.push_back(point_of(p));
    return out;
}

json json_of(const Point3& p) { return json::array({p[0], p[1], p[2]}); }

std::string number_text(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return std::format("{}", (long long)v);
    return py::repr(v);
}

std::string clean_number(double v) { return number_text(v + 0.0); }  // -0.0 == 0.0

char mode_char(Mode mode) { return mode == Mode::Exact ? 'F' : mode == Mode::Coarse ? 'T' : 'D'; }

std::string slice(const std::string& s, size_t start, size_t length) {
    return start >= s.size() ? std::string() : s.substr(start, length);
}

long long g_local_misses = 0;
json g_last_stats = json::object();

// funkcje LRU: wartości są shared_ptr, więc referencja z cache nie jest przechowywana
using IndexedPtr = std::shared_ptr<const Indexed>;

py::LruCache<IndexedPtr>& local_cache() { static py::LruCache<IndexedPtr> cache(192); return cache; }
py::LruCache<TopoDS_Shape>& display_cache() { static py::LruCache<TopoDS_Shape> cache(96); return cache; }
py::LruCache<RecordPtr>& world_cache() { static py::LruCache<RecordPtr> cache(256); return cache; }
py::LruCache<std::vector<RecordPtr>>& fitting_cache() { static py::LruCache<std::vector<RecordPtr>> cache(8); return cache; }
py::LruCache<std::shared_ptr<Context>>& scene_cache() { static py::LruCache<std::shared_ptr<Context>> cache(6); return cache; }

// OCCT: Shape.copy(mesh=False) + Shape.tessellate(tolerance, angularTolerance)
struct Tessellation {
    std::vector<Point3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

Tessellation tessellate(const TopoDS_Shape& source, double tolerance, double angular_tolerance) {
    // Kopia bez siatki: inaczej OCCT użyłby wcześniejszej, drobniejszej triangulacji.
    BRepBuilderAPI_Copy copier(source, true, false);
    TopoDS_Shape shape = copier.Shape();
    if (!BRepTools::Triangulation(shape, tolerance)) BRepMesh_IncrementalMesh(shape, tolerance, true, angular_tolerance);
    Tessellation out;
    int offset = 0;
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int f = 1; f <= faces.Extent(); ++f) {
        TopoDS_Face face = TopoDS::Face(faces(f));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) poly = BRep_Tool::Triangulation(face, loc);
        if (poly.IsNull()) throw std::runtime_error("'NoneType' object has no attribute 'NbNodes'");
        gp_Trsf trsf = loc.Transformation();
        bool reverse = face.Orientation() == TopAbs_REVERSED;
        for (int i = 1; i <= poly->NbNodes(); ++i) {
            gp_Pnt p = poly->Node(i).Transformed(trsf);
            out.vertices.push_back({p.X(), p.Y(), p.Z()});
        }
        for (int i = 1; i <= poly->NbTriangles(); ++i) {
            int n1, n2, n3;
            poly->Triangle(i).Get(n1, n2, n3);
            out.triangles.push_back(reverse ? std::array<int, 3>{n1 + offset - 1, n3 + offset - 1, n2 + offset - 1}
                                            : std::array<int, 3>{n1 + offset - 1, n2 + offset - 1, n3 + offset - 1});
        }
        offset += poly->NbNodes();
    }
    return out;
}

std::vector<core::Point> points_from_json(const json& value) {
    std::vector<core::Point> out;
    for (const auto& p : value) out.push_back({num(p.at(0)), num(p.at(1))});
    return out;
}

TopoDS_Shape display_solid(const std::string& key) {
    return display_cache().get(key, [](const std::string& k) {
        json data = json::parse(k);
        return cad_geometry::cut_drills(cad_geometry::cached_shape(k), data.at("drilling"), num(data.at("thickness")),
                                        points_from_json(data.at("points")));
    });
}

}  // namespace

// ---------------------------------------------------------------------------
std::string frozen_text(const json& value) {
    if (value.is_object()) {
        std::map<std::string, std::string> sorted;
        for (auto it = value.begin(); it != value.end(); ++it) sorted[it.key()] = frozen_text(it.value());
        std::string out = "{";
        for (auto& [k, v] : sorted) out += json(k).dump() + ":" + v + ",";
        return out + "}";
    }
    if (value.is_array()) {
        std::string out = "(";
        for (const auto& v : value) out += frozen_text(v) + ",";
        return out + ")";
    }
    if (value.is_number()) return number_text(value.get<double>());
    return value.dump();
}

bool mesh_part(const Part& part) {
    if (py::truthy(part.fillets) || py::truthy(part.chamfers) || py::truthy(part.machining)) return true;
    for (const auto& o : part.operations) {
        const std::string face = o.at("face").get<std::string>();
        if (face != "top" && face != "bottom") return true;
    }
    return false;
}

std::string GeomKey::text() const {
    if (cad) return "cad\x1f" + shape;
    std::string out = "stock\x1f";
    for (const auto& p : points) out += clean_number(p[0]) + "," + clean_number(p[1]) + ";";
    return out + "\x1f" + clean_number(t);
}

std::string Pose::text() const {
    return plane + "\x1f" + clean_number(x) + "\x1f" + clean_number(y) + "\x1f" + clean_number(z) + "\x1f" +
           clean_number(rx) + "\x1f" + clean_number(ry) + "\x1f" + clean_number(rz) + "\x1f" + frozen_text(layout);
}

GeomKey geometry_key(const Part& part) {
    GeomKey key;
    if (mesh_part(part)) {
        key.cad = true;
        key.shape = cad_geometry::shape_key(part);
    } else {
        key.points = part.points();
        key.t = part.t;
    }
    return key;
}

Pose pose_key(const Part& part) {
    Pose pose;
    pose.plane = part.plane;
    pose.x = part.x; pose.y = part.y; pose.z = part.z;
    pose.rx = part.rx; pose.ry = part.ry; pose.rz = part.rz;
    pose.layout = part.layout;
    return pose;
}

Indexed indexed(const std::vector<Polygon>& polygons) {
    Indexed out;
    std::map<std::array<double, 3>, int> lookup;
    for (const auto& polygon : polygons) {
        Face face;
        for (const auto& point : polygon) {
            std::array<double, 3> pointkey{py::round_to(point[0], 9), py::round_to(point[1], 9), py::round_to(point[2], 9)};
            auto found = lookup.find(pointkey);
            int index;
            if (found == lookup.end()) {
                index = (int)out.vertices.size();
                lookup[pointkey] = index;
                out.vertices.push_back(point);
            } else index = found->second;
            face.push_back(index);
        }
        out.indices.push_back(std::move(face));
    }
    return out;
}

std::pair<Point3, Point3> normal_center(const std::vector<Point3>& vertices, const std::vector<int>& face) {
    // A valid contour can begin with three collinear points. Newell's normal
    // uses the complete planar boundary, so it does not lose such a surface.
    double normal[3] = {0., 0., 0.};
    size_t count = face.size();
    for (size_t k = 0; k < count; ++k) {
        const Point3& a = vertices[(size_t)face[k]];
        const Point3& b = vertices[(size_t)face[(k + 1) % count]];
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    FSum squares;
    for (double v : normal) squares.add(v * v);
    double length = std::sqrt(squares.result());
    if (length == 0) length = 1;
    Point3 unit{normal[0] / length, normal[1] / length, normal[2] / length};
    if (count == 0) throw std::domain_error("division by zero");
    Point3 center;
    for (int i = 0; i < 3; ++i) {
        FSum total;
        for (int j : face) total.add(vertices[(size_t)j][(size_t)i]);
        center[(size_t)i] = total.result() / (double)count;
    }
    return {unit, center};
}

std::shared_ptr<Record> geometry(Indexed data, const GeometryOptions& options) {
    auto mesh = std::make_shared<Mesh>();
    mesh->vertices = std::move(data.vertices);
    mesh->indices = std::move(data.indices);
    for (const auto& face : mesh->indices) {
        auto [normal, center] = normal_center(mesh->vertices, face);
        mesh->normals.push_back(normal);
        mesh->centers.push_back(center);
    }
    auto record = std::make_shared<Record>(mesh);
    record->owner = options.owner; record->color = options.color; record->mesh = options.mesh;
    record->fitting = options.fitting; record->scene_object = options.scene_object; record->emissive = options.emissive;
    record->sign = options.sign;
    return record;
}

Indexed merge_coplanar(const Indexed& data) {
    // Merge only a single simple boundary; multiple loops retain true holes.
    //
    // Native Canvas cannot express a polygon with holes. Filling the outer loop
    // would silently conceal drilling, so every such surface retains triangles.
    const auto& vertices = data.vertices;
    std::map<std::array<double, 4>, std::vector<Face>> groups;
    std::vector<std::array<double, 4>> order;
    for (const auto& face : data.indices) {
        auto [normal, _center] = normal_center(vertices, face);
        FSum total;
        for (size_t i = 0; i < 3; ++i) total.add(normal[i] * vertices[(size_t)face[0]][i]);
        double offset = total.result();
        std::array<double, 4> key{py::round_to(normal[0], 6), py::round_to(normal[1], 6), py::round_to(normal[2], 6),
                                  py::round_to(offset, 5)};
        auto found = groups.find(key);
        if (found == groups.end()) { order.push_back(key); found = groups.emplace(key, std::vector<Face>{}).first; }
        found->second.push_back(face);
    }
    Indexed out;
    out.vertices = vertices;
    auto& result = out.indices;
    for (const auto& group_key : order) {
        const auto& faces = groups[group_key];
        if (faces.size() < 2) { result.insert(result.end(), faces.begin(), faces.end()); continue; }
        std::map<std::pair<int, int>, long long> position;
        std::map<long long, std::pair<int, int>> edges;
        long long sequence = 0;
        for (const auto& face : faces) {
            for (size_t k = 0; k < face.size(); ++k) {
                int a = face[k], b = face[(k + 1) % face.size()];
                std::pair<int, int> key{std::min(a, b), std::max(a, b)};
                auto found = position.find(key);
                if (found != position.end()) { edges.erase(found->second); position.erase(found); }
                else { position[key] = sequence; edges[sequence] = {a, b}; ++sequence; }
            }
        }
        std::map<int, int> following;
        std::vector<int> following_order;
        for (auto& [_seq, ab] : edges) {
            if (!following.contains(ab.first)) following_order.push_back(ab.first);
            following[ab.first] = ab.second;
        }
        if (following.size() != edges.size() || edges.empty()) { result.insert(result.end(), faces.begin(), faces.end()); continue; }
        int first = following_order.front();
        std::vector<int> boundary{first};
        std::unordered_set<int> seen{first};
        int cursor = following[first];
        while (cursor != first && following.contains(cursor) && !seen.contains(cursor)) {
            boundary.push_back(cursor); seen.insert(cursor);
            cursor = following[cursor];
        }
        if (cursor != first || boundary.size() != edges.size()) { result.insert(result.end(), faces.begin(), faces.end()); continue; }
        // Discard straight boundary subdivisions left by planar triangulation.
        Face simple;
        size_t n = boundary.size();
        for (size_t i = 0; i < n; ++i) {
            int b = boundary[i], a = boundary[(i + n - 1) % n], c = boundary[(i + 1) % n];
            double u[3], v[3];
            for (size_t j = 0; j < 3; ++j) {
                u[j] = vertices[(size_t)b][j] - vertices[(size_t)a][j];
                v[j] = vertices[(size_t)c][j] - vertices[(size_t)b][j];
            }
            double cross[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            FSum squares;
            for (double x : cross) squares.add(x * x);
            if (squares.result() > 1e-14) simple.push_back(b);
        }
        if (simple.size() >= 3) result.push_back(simple);
        else result.insert(result.end(), faces.begin(), faces.end());
    }
    return out;
}

std::shared_ptr<const Indexed> local_geometry(const GeomKey& key, Mode mode, bool explicit_mode) {
    std::string text = key.text() + "\x1e" + mode_char(mode) + (explicit_mode ? "2" : "1");
    return local_cache().get(text, [&](const std::string&) -> IndexedPtr {
        ++g_local_misses;
        if (!key.cad) {
            const auto& points = key.points;
            double thickness = key.t;
            std::vector<Point3> bot, top;
            for (const auto& p : points) { bot.push_back({p[0], p[1], 0}); top.push_back({p[0], p[1], thickness}); }
            std::vector<Polygon> polygons;
            polygons.push_back(Polygon(bot.rbegin(), bot.rend()));
            polygons.push_back(top);
            size_t n = bot.size();
            for (size_t i = 0; i < n; ++i) polygons.push_back({bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]});
            return std::make_shared<Indexed>(indexed(polygons));
        }
        if (mode == Mode::Exact) {
            std::vector<Polygon> polygons;
            for (const auto& t : cad_geometry::cached_mesh(key.shape)) polygons.push_back({t[0], t[1], t[2]});
            return std::make_shared<Indexed>(indexed(polygons));
        }
        // Copy without mesh: OCCT otherwise reuses a previously finer triangulation
        // and ignores the requested preview tolerance. The manufacturing solid and
        // cad_geometry.cached_mesh remain untouched.
        Tessellation tess = tessellate(display_solid(key.shape), mode == Mode::Coarse ? 1.0 : .45,
                                       mode == Mode::Coarse ? .65 : .35);
        std::vector<Polygon> polygons;
        for (const auto& t : tess.triangles)
            polygons.push_back({tess.vertices[(size_t)t[0]], tess.vertices[(size_t)t[1]], tess.vertices[(size_t)t[2]]});
        return std::make_shared<Indexed>(merge_coplanar(indexed(polygons)));
    });
}

namespace {
std::pair<std::vector<Point3>, int> transform_with(const Part& part, const std::vector<Point3>& vertices) {
    Point3 origin = core::world(part, 0, 0, 0);
    std::array<Point3, 3> axes;
    const double basis[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int k = 0; k < 3; ++k) {
        Point3 w = core::world(part, basis[k][0], basis[k][1], basis[k][2]);
        axes[(size_t)k] = {w[0] - origin[0], w[1] - origin[1], w[2] - origin[2]};
    }
    const Point3 &a = axes[0], &b = axes[1], &c = axes[2];
    std::vector<Point3> result;
    result.reserve(vertices.size());
    for (const auto& [u, v, w] : vertices)
        result.push_back({origin[0] + a[0] * u + b[0] * v + c[0] * w,
                          origin[1] + a[1] * u + b[1] * v + c[1] * w,
                          origin[2] + a[2] * u + b[2] * v + c[2] * w});
    double determinant = a[0] * (b[1] * c[2] - b[2] * c[1]) - b[0] * (a[1] * c[2] - a[2] * c[1]) +
                         c[0] * (a[1] * b[2] - a[2] * b[1]);
    return {result, determinant >= 0 ? 1 : -1};
}
}  // namespace

std::pair<std::vector<Point3>, int> transform(const Part& part, const std::vector<Point3>& vertices) {
    return transform_with(part, vertices);
}

std::pair<std::vector<Point3>, int> transform(const Pose& pose, const std::vector<Point3>& vertices) {
    Part proxy;  // SimpleNamespace(plane, x, y, z, rx, ry, rz, layout)
    proxy.plane = pose.plane;
    proxy.x = pose.x; proxy.y = pose.y; proxy.z = pose.z;
    proxy.rx = pose.rx; proxy.ry = pose.ry; proxy.rz = pose.rz;
    proxy.layout = pose.layout;
    return transform_with(proxy, vertices);
}

bool small_mounting_drill(const json& operation) {
    // Tiny generated screw holes need no curved drill walls during motion.
    //
    // User drilling, large openings and the full CAD solid are always preserved.
    // Handle M4 through bores previously forced every decorated front back into
    // its dense CAD preview even though the holes sit beneath bought handles.
    json type = py::get(operation, "type");
    json auto_flag = py::get(operation, "config_auto");
    if (!(type.is_string() && type.get<std::string>() == "drill")) return false;
    if (!(auto_flag.is_boolean() && auto_flag.get<bool>())) return false;
    double diameter = num(py::get(operation, "diameter", 0));
    return 0 < diameter && diameter <= 8;
}

namespace {
double span_of(const Part& part, const std::string& face) {
    if (face == "top" || face == "bottom") return part.t;
    if (face == "u0" || face == "u1") return part.u;
    if (face == "v0" || face == "v1") return part.v;
    throw py::KeyError("'" + face + "'");
}

bool has_deep_feature(const Part& part) {
    for (const auto& c : part.machining) {
        json type = py::get(c, "type");
        if ((type.is_string() && type.get<std::string>() == "cutout") || num(py::get(c, "depth", 0)) >= part.t) return true;
    }
    for (const auto& o : part.operations)
        if (num(py::get(o, "depth", 0)) >= span_of(part, o.at("face").get<std::string>()) && !small_mounting_drill(o)) return true;
    for (const auto& h : part.holes)
        if (h.at(3) >= part.t) return true;
    return false;
}
}  // namespace

std::optional<std::vector<RecordPtr>> preview_front(const Part& part, const std::string& color) {
    // Keep the front silhouette during motion; restore decorative cuts after.
    json style = py::get(py::get(part.configurator_source, "front_style", json::object()), "style");
    if (!(style.is_string() && (style.get<std::string>() == "frame" || style.get<std::string>() == "fluted"))) return std::nullopt;
    if (py::truthy(part.profile) || py::truthy(part.fillets) || py::truthy(part.chamfers)) return std::nullopt;
    if (has_deep_feature(part)) return std::nullopt;
    GeomKey stock_key;
    stock_key.points = part.points();
    stock_key.t = part.t;
    auto stock = local_geometry(stock_key);
    auto [vertices, sign] = transform(part, stock->vertices);
    GeometryOptions options;
    options.owner = part.id; options.color = color; options.mesh = true; options.sign = sign;
    return std::vector<RecordPtr>{geometry(Indexed{vertices, stock->indices}, options)};
}

std::optional<std::vector<RecordPtr>> preview_board(const Part& part, const GeomKey&, const std::string& color) {
    // Small blind drilling stays in production; movement uses the stock board.
    //
    // Exact contours and through cut-outs are kept. Standard rectilinear boards
    // with hinge/connector bores or shallow LED channels do not need hundreds of
    // subpixel drill-wall triangles while being moved around the room.
    if (py::truthy(part.profile) || !part.contour.empty() || py::truthy(part.fillets) || py::truthy(part.chamfers)) return std::nullopt;
    if (has_deep_feature(part)) return std::nullopt;
    GeomKey stock;
    stock.points = part.points();
    stock.t = part.t;
    return std::vector<RecordPtr>{owner_record(part, stock, color)};
}

std::vector<Polygon> surface_patches(const Polygon& polygon) {
    // Give large walls local light samples without drawing a visible tile grid.
    if (polygon.size() != 4) return {polygon};
    const Point3 &a = polygon[0], &b = polygon[1], &c = polygon[2], &d = polygon[3];
    auto dist = [](const Point3& p, const Point3& q) { return std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); };
    double u = std::max(dist(a, b), dist(c, d)), v = std::max(dist(a, d), dist(b, c));
    if (std::max(u, v) < 700) return {polygon};
    int nu = std::min(8, std::max(1, (int)std::ceil(u / 400))), nv = std::min(8, std::max(1, (int)std::ceil(v / 400)));
    auto point = [&](double s, double t) {
        Point3 p;
        for (size_t i = 0; i < 3; ++i) p[i] = a[i] * (1 - s) * (1 - t) + b[i] * s * (1 - t) + c[i] * s * t + d[i] * (1 - s) * t;
        return p;
    };
    std::vector<Polygon> out;
    for (int y = 0; y < nv; ++y)
        for (int x = 0; x < nu; ++x)
            out.push_back({point((double)x / nu, (double)y / nv), point((double)(x + 1) / nu, (double)y / nv),
                           point((double)(x + 1) / nu, (double)(y + 1) / nv), point((double)x / nu, (double)(y + 1) / nv)});
    return out;
}

json geometry_faces(const Part& part, bool interactive) {
    // Compatibility API for core.faces; return fresh mutable world polygons.
    auto geo = local_geometry(geometry_key(part), interactive ? Mode::Coarse : Mode::Exact, true);
    auto [points, _sign] = transform(part, geo->vertices);
    json out = json::array();
    for (const auto& face : geo->indices) {
        json polygon = json::array();
        for (int i : face) polygon.push_back(json_of(points[(size_t)i]));
        out.push_back(std::move(polygon));
    }
    return out;
}

std::vector<std::shared_ptr<Record>> fitting_geometry(const Project& project, const std::vector<PartKey>& part_keys) {
    // Room-object drags must not regenerate stationary furniture fittings.
    std::string key;
    for (const auto& p : part_keys)
        key += p.id + "\x1f" + p.geometry.text() + "\x1f" + p.pose.text() + "\x1f" + (p.visible ? "1" : "0") + "\x1e";
    key += frozen_text(project.hardware);
    std::vector<RecordPtr> cached = fitting_cache().get(key, [&](const std::string&) {
        std::vector<std::tuple<std::string, std::string, int>> order;
        std::map<std::tuple<std::string, std::string, int>, std::vector<Polygon>> grouped;
        for (const auto& item : fitting_visuals::fitting_faces_with_metadata(project)) {
            auto k = std::make_tuple(item.owner, item.color, item.sign);
            auto found = grouped.find(k);
            if (found == grouped.end()) { order.push_back(k); found = grouped.emplace(k, std::vector<Polygon>{}).first; }
            found->second.push_back(item.face);
        }
        std::vector<RecordPtr> records;
        for (const auto& k : order) {
            GeometryOptions options;
            options.owner = std::get<0>(k); options.color = std::get<1>(k); options.fitting = true; options.sign = std::get<2>(k);
            records.push_back(geometry(indexed(grouped[k]), options));
        }
        return records;
    });
    // Each context owns its colors. Only immutable geometry arrays are shared.
    std::vector<std::shared_ptr<Record>> out;
    for (const auto& record : cached) out.push_back(std::make_shared<Record>(*record));
    return out;
}

RecordPtr world_record(const GeomKey& key, const Pose& pose, Mode mode, bool lighting_patches) {
    std::string text = key.text() + "\x1e" + pose.text() + "\x1e" + mode_char(mode) + (lighting_patches ? "1" : "0");
    return world_cache().get(text, [&](const std::string&) -> RecordPtr {
        auto geo = local_geometry(key, mode, true);
        auto [vertices, sign] = transform(pose, geo->vertices);
        std::vector<Face> indices = geo->indices;
        bool patched = false;
        if (lighting_patches) {
            std::vector<Polygon> polygons;
            for (const auto& face : indices) {
                Polygon corners;
                for (int j : face) corners.push_back(vertices[(size_t)j]);
                auto patches = surface_patches(corners);
                patched = patched || patches.size() > 1;
                polygons.insert(polygons.end(), patches.begin(), patches.end());
            }
            if (patched) { Indexed again = indexed(polygons); vertices = std::move(again.vertices); indices = std::move(again.indices); }
        }
        GeometryOptions options;
        options.color = "#000000"; options.mesh = key.cad || patched; options.sign = sign;
        return geometry(Indexed{std::move(vertices), std::move(indices)}, options);
    });
}

std::shared_ptr<Record> owner_record(const Part& part, const GeomKey& key, const std::string& color, Mode mode,
                                     bool lighting_patches) {
    auto record = std::make_shared<Record>(*world_record(key, pose_key(part), mode, lighting_patches));
    record->owner = part.id;
    record->color = color;
    return record;
}

std::vector<RecordPtr> fitting_preview(const RecordPtr& record, const Part& part, bool emissive) {
    // One oriented box per connected bought component, never per whole door.
    if (emissive || record->indices.size() < 20) return {record};
    std::vector<int> parent(record->vertices.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
    auto find = [&](int i) {
        while (parent[(size_t)i] != i) {
            parent[(size_t)i] = parent[(size_t)parent[(size_t)i]];
            i = parent[(size_t)i];
        }
        return i;
    };
    for (const auto& face : record->indices) {
        int root = find(face[0]);
        for (size_t k = 1; k < face.size(); ++k) parent[(size_t)find(face[k])] = root;
    }
    std::vector<int> roots;
    std::map<int, std::vector<Point3>> groups;
    for (size_t i = 0; i < record->vertices.size(); ++i) {
        int root = find((int)i);
        auto found = groups.find(root);
        if (found == groups.end()) { roots.push_back(root); found = groups.emplace(root, std::vector<Point3>{}).first; }
        found->second.push_back(record->vertices[i]);
    }
    Point3 origin = core::world(part, 0, 0, 0);
    std::array<Point3, 3> basis;
    const double axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (size_t k = 0; k < 3; ++k) {
        Point3 w = core::world(part, axes[k][0], axes[k][1], axes[k][2]);
        basis[k] = {w[0] - origin[0], w[1] - origin[1], w[2] - origin[2]};
    }
    std::vector<Polygon> polygons;
    for (int root : roots) {
        const auto& component = groups[root];
        std::vector<Point3> local;
        for (const auto& point : component) {
            Point3 q;
            for (size_t k = 0; k < 3; ++k) {
                FSum total;
                for (size_t j = 0; j < 3; ++j) total.add((point[j] - origin[j]) * basis[k][j]);
                q[k] = total.result();
            }
            local.push_back(q);
        }
        Point3 lo = local[0], hi = local[0];
        for (const auto& p : local)
            for (size_t i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
        bool flat = false;
        for (size_t i = 0; i < 3; ++i) if (hi[i] - lo[i] < .0001) flat = true;
        if (flat) {
            // Single flat faces belong to thin track/extrusion purchases.
            // Preserve them rather than manufacturing a fake thickness.
            std::set<Point3> members(component.begin(), component.end());
            std::set<int> selected;
            for (size_t i = 0; i < record->vertices.size(); ++i) if (members.contains(record->vertices[i])) selected.insert((int)i);
            for (const auto& face : record->indices) {
                bool all = true;
                for (int i : face) if (!selected.contains(i)) { all = false; break; }
                if (!all) continue;
                Polygon polygon;
                for (int i : face) polygon.push_back(record->vertices[(size_t)i]);
                polygons.push_back(std::move(polygon));
            }
            continue;
        }
        const int bits[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
        std::vector<Point3> corners;
        for (const auto& bit : bits)
            corners.push_back({lo[0] + (hi[0] - lo[0]) * bit[0], lo[1] + (hi[1] - lo[1]) * bit[1], lo[2] + (hi[2] - lo[2]) * bit[2]});
        auto [vertices, _sign] = transform(part, corners);
        const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
        for (const auto& face : faces)
            polygons.push_back({vertices[(size_t)face[0]], vertices[(size_t)face[1]], vertices[(size_t)face[2]], vertices[(size_t)face[3]]});
    }
    Indexed data = indexed(polygons);
    auto [_unused, sign] = transform(part, {});
    GeometryOptions options;
    options.owner = record->owner; options.color = record->color; options.fitting = true; options.sign = sign;
    return {geometry(std::move(data), options)};
}

std::array<int, 3> rgb(const std::string& color) {
    return {(int)py::int16(slice(color, 1, 2)), (int)py::int16(slice(color, 3, 2)), (int)py::int16(slice(color, 5, 2))};
}

std::string color_hex(const std::vector<double>& values) {
    std::string out = "#";
    for (double v : values) {
        double rounded = std::min(255.0, std::max(0.0, py::round_half_even(v)));
        out += std::format("{:02x}", (int)rounded);
    }
    return out;
}

std::string lit_color(const std::string& color, const Point3& normal, const Point3& center, const json& lights,
                      double brightness, const std::string& room, bool emissive) {
    auto base = rgb(color);
    if (emissive) return color;
    double ambient = room == "light" || room == "bright" ? .96 : room == "dark" ? .32 : .82;
    const double sun[3] = {-.3, -.5, .81};
    FSum directional_sum;
    for (size_t i = 0; i < 3; ++i) directional_sum.add(normal[i] * sun[i]);
    double directional = std::fabs(directional_sum.result());
    double factor = std::min(1.3, std::max(.08, (ambient + .18 * directional) * brightness));
    std::vector<double> result;
    for (int v : base) result.push_back(v * factor);
    for (const auto& light : lights) {
        Point3 position = point_of(light.at("position"));
        double delta[3];
        for (size_t i = 0; i < 3; ++i) delta[i] = position[i] - center[i];
        FSum norm;
        for (double d : delta) norm.add(d * d);
        double distance = std::sqrt(norm.result());
        double radius = num(py::get(light, "radius", 650));
        if (distance >= radius) continue;
        json direction = py::get(light, "direction");
        if (py::truthy(direction)) {
            FSum along;
            for (size_t i = 0; i < 3; ++i) along.add(delta[i] * num(direction.at(i)));
            if (along.result() > radius * .025) continue;
        }
        FSum dot;
        for (size_t i = 0; i < 3; ++i) dot.add(normal[i] * delta[i]);
        double incidence = .28 + .72 * std::fabs(dot.result() / std::max(1.0, distance));
        double power = std::min(.8, std::pow(1 - distance / radius, 2.0) * num(py::get(light, "intensity", 1)) * incidence);
        json color_value = py::get(light, "color", "#fff3bf");
        auto lamp = rgb(color_value.get<std::string>());
        for (size_t i = 0; i < 3; ++i) result[i] = result[i] + base[i] * power * (lamp[i] / 255.0) * .7;
    }
    return color_hex(result);
}

std::vector<std::string> lit_colors(const std::string& color, const std::vector<Point3>& normals,
                                    const std::vector<Point3>& centers, const json& lights, double brightness,
                                    const std::string& room, bool emissive) {
    // Shade a complete immutable geometry in NumPy, including multiple LEDs.
    // Arytmetyka float32 jak w numpy 2 (NEP 50); bez FMA (-ffp-contract=off).
    size_t count = normals.size();
    if (emissive) return std::vector<std::string>(count, color);
    if (!count) return {};
    auto base_int = rgb(color);
    const float base[3] = {(float)base_int[0], (float)base_int[1], (float)base_int[2]};
    float ambient = (float)(room == "light" || room == "bright" ? .96 : room == "dark" ? .32 : .82);
    const float sun[3] = {(float)-.3, (float)-.5, (float).81};
    std::vector<float> factor(count);
    std::vector<std::array<float, 3>> result(count);
    for (size_t r = 0; r < count; ++r) {
        float n0 = (float)normals[r][0], n1 = (float)normals[r][1], n2 = (float)normals[r][2];
        float directional = std::fabs(n0 * sun[0] + n1 * sun[1] + n2 * sun[2]);
        float f = (ambient + (float).18 * directional) * (float)brightness;
        f = std::min(std::max(f, (float).08), (float)1.3);
        for (size_t k = 0; k < 3; ++k) result[r][k] = f * base[k];
    }
    for (const auto& light : lights) {
        Point3 position_d = point_of(light.at("position"));
        const float position[3] = {(float)position_d[0], (float)position_d[1], (float)position_d[2]};
        double radius_d = num(py::get(light, "radius", 650));
        float radius = (float)radius_d;
        float intensity = (float)num(py::get(light, "intensity", 1));
        json direction = py::get(light, "direction");
        bool directional = !direction.is_null();
        float dir[3] = {0, 0, 0};
        if (directional) for (size_t k = 0; k < 3; ++k) dir[k] = (float)num(direction.at(k));
        float limit = (float)(radius_d * .025);
        auto lamp_int = rgb(py::get(light, "color", "#fff3bf").get<std::string>());
        const float lamp[3] = {(float)lamp_int[0], (float)lamp_int[1], (float)lamp_int[2]};
        for (size_t r = 0; r < count; ++r) {
            float c[3] = {(float)centers[r][0], (float)centers[r][1], (float)centers[r][2]};
            float n[3] = {(float)normals[r][0], (float)normals[r][1], (float)normals[r][2]};
            float delta[3] = {position[0] - c[0], position[1] - c[1], position[2] - c[2]};
            float distance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            float incidence = (float).28 + (float).72 * std::fabs((n[0] * delta[0] + n[1] * delta[1] + n[2] * delta[2]) / std::max(1.0f, distance));
            float ratio = 1 - distance / radius;
            float power = std::min((float).8, ratio * ratio * intensity * incidence);
            bool valid = distance < radius;
            if (directional) valid = valid && (delta[0] * dir[0] + delta[1] * dir[1] + delta[2] * dir[2]) <= limit;
            if (!valid) power = 0;
            for (size_t k = 0; k < 3; ++k) result[r][k] += base[k] * power * (lamp[k] / 255.0f) * (float).7;
        }
    }
    std::vector<std::string> out;
    out.reserve(count);
    for (const auto& row : result) {
        std::string text = "#";
        for (size_t k = 0; k < 3; ++k) {
            float v = std::min(255.0f, std::max(0.0f, std::nearbyint(row[k])));
            text += std::format("{:02x}", (int)v);
        }
        out.push_back(std::move(text));
    }
    return out;
}

json furniture_lights(const Project& project, const std::map<std::string, const Part*>& owners) {
    json lights = json::array();
    if (!py::truthy(py::get(project.scene, "led_enabled", true))) return lights;
    for (const auto& h : project.hardware) {
        json owner_id = py::get(h, "owner_id");
        const Part* p = nullptr;
        if (owner_id.is_string()) { auto found = owners.find(owner_id.get<std::string>()); if (found != owners.end()) p = found->second; }
        json kind = py::get(h, "kind");
        if (!(kind.is_string() && kind.get<std::string>() == "led_strip") || p == nullptr) continue;
        double u = num(py::get(h, "u", p->u / 2)), v = num(py::get(h, "v", p->v / 2));
        json face = py::get(h, "face");
        bool top = face.is_string() && face.get<std::string>() == "top";
        double z = top ? p->t : 0;
        Point3 local = h.contains("local_center") ? point_of(h["local_center"]) : Point3{u, v, z};
        Point3 normal = h.contains("local_normal") ? point_of(h["local_normal"]) : Point3{0, 0, top ? 1.0 : -1.0};
        json axis = py::get(h, "axis");
        bool axis_v = axis.is_string() && axis.get<std::string>() == "v";
        Point3 tangent = h.contains("local_tangent") ? point_of(h["local_tangent"]) : (axis_v ? Point3{0, 1, 0} : Point3{1, 0, 0});
        Point3 origin = core::world(*p, local[0], local[1], local[2]);
        Point3 tip = core::world(*p, local[0] + normal[0], local[1] + normal[1], local[2] + normal[2]);
        Point3 direction{tip[0] - origin[0], tip[1] - origin[1], tip[2] - origin[2]};
        double length = num(py::get(h, "length", p->u));
        int count = std::max(1, std::min(6, (int)std::ceil(length / 240)));
        for (int i = 0; i < count; ++i) {
            double delta = length * ((i + .5) / count - .5);
            Point3 position = core::world(*p, local[0] + delta * tangent[0], local[1] + delta * tangent[1], local[2] + delta * tangent[2]);
            json light = json::object();
            light["position"] = json_of(position);
            light["direction"] = json_of(direction);
            light["color"] = py::get(h, "color", "#fff3bf");
            light["radius"] = 650;
            light["intensity"] = .65 / std::pow((double)count, .35);
            light["led"] = true;
            lights.push_back(std::move(light));
        }
    }
    return lights;
}

// --- SceneContext -------------------------------------------------------------
Context::Context(const Project& project, const std::vector<PartKey>& part_keys) {
    for (const auto& p : project.parts) {
        if (!p.visible) continue;
        auto snapshot = std::make_shared<Part>(p);
        snapshot->configurator_source = json::object({{"front_style", py::get(p.configurator_source, "front_style", json::object())}});
        owner_store.push_back(snapshot);
        owner_map[p.id] = snapshot.get();
        owners.push_back(snapshot.get());
    }
    std::map<std::string, std::string> materials;
    for (const auto& m : project.materials) materials[m.id] = m.color;
    for (size_t index = 0; index < project.parts.size() && index < part_keys.size(); ++index) {
        const Part& p = project.parts[index];
        const PartKey& key = part_keys[index];
        if (!p.visible) continue;
        GeomKey stock_key;
        stock_key.points = p.points();
        stock_key.t = p.t;
        auto stock_geo = local_geometry(stock_key);
        auto [stock_points, _sign] = transform(p, stock_geo->vertices);
        stock.insert(stock.end(), stock_points.begin(), stock_points.end());
        auto& group = groups[p.group];
        group.insert(group.end(), stock_points.begin(), stock_points.end());
        auto found = materials.find(p.material);
        parts.push_back({p.id, key.geometry, found == materials.end() ? "#c5b395" : found->second, mesh_part(p)});
    }
    for (const auto& record : fitting_geometry(project, part_keys)) {
        if (!owner_map.contains(record->owner)) continue;
        fittings.push_back(record);
        auto& group = groups[owner_map[record->owner]->group];
        group.insert(group.end(), record->vertices.begin(), record->vertices.end());
    }
    for (const auto& f : room_objects::room_world_faces(project)) {
        if (py::truthy(py::get(f, "architecture"))) continue;
        auto patches = surface_patches(polygon_of(f.at("world")));
        GeometryOptions options;
        options.owner = f.at("id").get<std::string>();
        options.color = f.at("color").get<std::string>();
        options.mesh = patches.size() > 1; options.fitting = true; options.scene_object = true;
        options.emissive = py::truthy(py::get(f, "emissive", false));
        room.push_back(geometry(indexed(patches), options));
    }
    room_vertices = room_objects::object_vertices(project);
    room_lights = room_objects::object_lights(project);
    for (const auto& f : room_architecture::world_faces(project)) {
        GeometryOptions options;
        options.owner = f.at("id").get<std::string>();
        options.color = f.at("color").get<std::string>();
        options.mesh = true; options.fitting = true; options.scene_object = true;
        auto record = geometry(indexed(surface_patches(polygon_of(f.at("world")))), options);
        record->architecture = true;
        architecture.push_back(record);
    }
    // Camera "Fit" targets furniture and purchased objects. The room
    // inspector has a separate action to fit the enclosing walls.
    architecture_vertices = room_architecture::vertices(project);
    lights = furniture_lights(project, owner_map);
    for (const auto& light : room_lights) lights.push_back(light);
    scene = project.scene;
    std::set<std::pair<std::string, std::string>> leds;
    for (const auto& h : project.hardware) {
        json kind = py::get(h, "kind");
        if (kind.is_string() && kind.get<std::string>() == "led_strip")
            leds.insert(std::pair<std::string, std::string>(py::str(py::get(h, "owner_id")), py::get(h, "color", "#fff3bf").get<std::string>()));
    }
    for (const auto& record : fittings) {
        bool led = leds.contains({record->owner, record->color});
        for (const auto& preview_record : fitting_preview(record, *owner_map.at(record->owner), led)) fitting_previews.push_back(preview_record);
    }
}

const Part* Context::owner(const std::string& id) const {
    auto found = owner_map.find(id);
    return found == owner_map.end() ? nullptr : found->second;
}

std::vector<RecordPtr> Context::records(bool interactive) {
    std::vector<RecordPtr> result;
    for (const auto& entry : parts) {
        const std::string& owner_id = entry.owner;
        const Part& part = *owner_map.at(owner_id);
        if (interactive) {
            auto found = preview.find(owner_id);
            if (found == preview.end()) {
                std::optional<std::vector<RecordPtr>> chosen;
                if (entry.is_mesh) {
                    chosen = preview_front(part, entry.color);
                    if (!chosen) chosen = preview_board(part, entry.key, entry.color);
                } else {
                    // Full stock can contain dozens of LED light patches.
                    // Never reuse that record during motion, regardless of
                    // which LOD was rendered first.
                    chosen = std::vector<RecordPtr>{owner_record(part, entry.key, entry.color, Mode::Exact, false)};
                }
                if (!chosen) chosen = std::vector<RecordPtr>{owner_record(part, entry.key, entry.color, Mode::Coarse)};
                found = preview.emplace(owner_id, *chosen).first;
            }
            result.insert(result.end(), found->second.begin(), found->second.end());
        } else {
            auto found = full.find(owner_id);
            if (found == full.end()) {
                RecordPtr record = owner_record(part, entry.key, entry.color, entry.is_mesh ? Mode::Display : Mode::Exact,
                                                !lights.empty() && !entry.is_mesh);
                found = full.emplace(owner_id, record).first;
                // Build the camera-independent preview once while the
                // edited shape is first displayed, so the first drag never
                // enters OCCT or waits for a new tessellation.
                if (entry.is_mesh && !preview.contains(owner_id)) {
                    auto proxy = preview_front(part, entry.color);
                    if (!proxy) proxy = preview_board(part, entry.key, entry.color);
                    if (!proxy) proxy = std::vector<RecordPtr>{owner_record(part, entry.key, entry.color, Mode::Coarse)};
                    preview[owner_id] = *proxy;
                }
            }
            result.push_back(found->second);
        }
    }
    const auto& bought = interactive ? fitting_previews : fittings;
    result.insert(result.end(), bought.begin(), bought.end());
    result.insert(result.end(), room.begin(), room.end());
    return result;
}

const std::vector<std::string>& Context::face_colors(const Record& record) const {
    // An integer id can be recycled when cold preview is replaced by full
    // LOD. Cache on the actual object, so geometry and colors live together.
    if (!record.lit_colors) {
        json lighting = py::get(scene, "ambient", "neutral");
        std::string room_name = py::str(py::get(scene, "lighting", lighting));
        double brightness = num(py::get(scene, "brightness", 1));
        // Purchase metal retains its selected finish, while room walls and
        // furniture receive light. LED diffuser itself visibly switches off.
        if (record.fitting && !record.scene_object) {
            bool is_led = false;
            for (const auto& h : hardware) {
                json kind = py::get(h, "kind");
                if (kind.is_string() && kind.get<std::string>() == "led_strip" &&
                    py::str(py::get(h, "owner_id")) == record.owner && py::get(h, "color", "#fff3bf").get<std::string>() == record.color)
                    is_led = true;
            }
            std::string base = record.color;
            if (is_led) {
                if (!py::truthy(py::get(scene, "led_enabled", true))) base = "#c7c6bb";
                record.lit_colors = std::vector<std::string>(record.indices.size(), base);
            } else {
                std::vector<std::string> colors;
                auto channels = rgb(base);
                const double sun[3] = {-.3, -.5, .81};
                for (const auto& n : record.normals) {
                    FSum total;
                    for (size_t i = 0; i < 3; ++i) total.add(n[i] * sun[i]);
                    double factor = .74 + .26 * std::fabs(total.result());
                    std::vector<double> values;
                    for (int v : channels) values.push_back(v * factor);
                    colors.push_back(color_hex(values));
                }
                record.lit_colors = std::move(colors);
            }
        } else {
            record.lit_colors = lit_colors(record.color, record.normals, record.centers, lights, brightness, room_name, record.emissive);
        }
    }
    if (!(record.indices.size() == record.normals.size() && record.normals.size() == record.centers.size() &&
          record.centers.size() == record.lit_colors->size()))
        throw py::ValueError("Niespójna geometria widoku: liczba powierzchni, normalnych i kolorów musi być jednakowa.");
    return *record.lit_colors;
}

void clear_caches() {
    scene_cache().clear();
    fitting_cache().clear();
    world_cache().clear();
    local_cache().clear();
    display_cache().clear();
    g_local_misses = 0;
}

long long local_geometry_misses() { return g_local_misses; }

std::shared_ptr<Context> prepare_scene(const Project& project, bool) {
    std::vector<PartKey> part_keys;
    std::string key;
    for (const auto& p : project.parts) {
        PartKey item;
        item.id = p.id; item.geometry = geometry_key(p); item.pose = pose_key(p);
        item.visible = p.visible; item.group = p.group; item.material = p.material;
        key += item.id + "\x1f" + item.geometry.text() + "\x1f" + item.pose.text() + "\x1f" + (item.visible ? "1" : "0") + "\x1f" +
               item.group + "\x1f" + item.material + "\x1e";
        part_keys.push_back(std::move(item));
    }
    key += "\x1d";
    for (const auto& m : project.materials) key += m.id + "\x1f" + m.color + "\x1e";
    key += "\x1d" + frozen_text(project.hardware) + "\x1d" + frozen_text(project.scene);
    return scene_cache().get(key, [&](const std::string&) {
        auto context = std::make_shared<Context>(project, part_keys);
        context->hardware = project.hardware;
        return context;
    });
}

const json& last_stats() { return g_last_stats; }

RenderedFaces render_with_stats(const Project& project, const core::Camera& camera, double width, double height,
                                const std::optional<std::string>& selected, double explode, bool interactive,
                                Context* context) {
    long long before = g_local_misses;
    std::shared_ptr<Context> own;
    if (context == nullptr) { own = prepare_scene(project, interactive); context = own.get(); }
    // A camera batch projects each shared vertex once, without per-vertex trig.
    auto [ca, sa, cb, sb] = camera._basis();
    auto [cx, cy, cz] = camera.center;
    double scale = camera.scale;
    double ox = width / 2 + camera.pan[0], oy = height / 2 + camera.pan[1];
    struct Item { double depth; json face; };
    std::vector<Item> out;
    std::vector<RecordPtr> records = context->records(interactive);
    if (!context->architecture.empty()) {
        std::set<std::string> visible;
        for (const auto& f : room_architecture::world_faces(project, camera)) visible.insert(f.at("id").get<std::string>());
        for (const auto& r : context->architecture) if (visible.contains(r->owner)) records.push_back(r);
    }
    for (const auto& record_ptr : records) {
        const Record& record = *record_ptr;
        const Part* owner = context->owner(record.owner);
        double sx = 0, sy = 0, sz = 0;
        if (explode != 0 && owner != nullptr) {
            Point3 shift = core::explosion_shift(*owner, camera, explode);
            sx = shift[0]; sy = shift[1]; sz = shift[2];
        }
        std::vector<Point3> projected;
        projected.reserve(record.vertices.size());
        for (const auto& vertex : record.vertices) {
            double x = vertex[0] + sx - cx, y = vertex[1] + sy - cy, z = vertex[2] + sz - cz;
            double u = x * ca - y * sa, d = x * sa + y * ca;
            projected.push_back({ox + u * scale, oy - (z * cb + d * sb) * scale, -d * cb + z * sb});
        }
        const std::vector<std::string>& colors = context->face_colors(record);
        for (size_t i = 0; i < record.indices.size(); ++i) {
            const Face& indices = record.indices[i];
            if ((record.mesh || record.fitting || interactive) && !record.scene_object && record.sign) {
                const Point3& n = record.normals[i];
                double facing = (-n[0] * sa * cb - n[1] * ca * cb + n[2] * sb) * record.sign;
                if (facing < -1e-8) continue;
            }
            std::vector<Point3> q;
            q.reserve(indices.size());
            for (int j : indices) q.push_back(projected[(size_t)j]);
            // Tiny mesh triangles are subpixel detail during movement. Removing
            // those reduces Tk pressure without hiding large pockets or grooves.
            if (record.mesh && indices.size() == 3) {
                double area = std::fabs((q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[1][1] - q[0][1]) * (q[2][0] - q[0][0]));
                if (area < (interactive ? .45 : .08)) continue;
            }
            json points = json::array(), depths = json::array(), world = json::array();
            FSum depth_total;
            for (const auto& p : q) {
                points.push_back(json::array({p[0], p[1]}));
                depths.push_back(p[2]);
                depth_total.add(p[2]);
            }
            for (int j : indices) world.push_back(json_of(record.vertices[(size_t)j]));
            double depth = depth_total.result() / (double)q.size();
            json face = json::object();
            face["points"] = std::move(points);
            face["depth"] = depth;
            face["depths"] = std::move(depths);
            face["color"] = colors[i];
            face["id"] = record.owner;
            face["selected"] = selected.has_value() && record.owner == *selected;
            face["mesh"] = record.mesh;
            face["normal"] = json_of(record.normals[i]);
            face["center"] = json_of(record.centers[i]);
            face["world"] = std::move(world);
            if (record.fitting) face["fitting"] = true;
            if (record.scene_object) face["scene_object"] = true;
            if (record.emissive) face["emissive"] = true;
            if (record.architecture) face["architecture"] = true;
            out.push_back({depth, std::move(face)});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    RenderedFaces result;
    for (auto& item : out) result.faces.push_back(std::move(item.face));
    result.stats = json::object({{"cold_geometry", g_local_misses > before}, {"interactive", interactive},
                                 {"faces", (long long)result.faces.size()}});
    g_last_stats = result.stats;
    return result;
}

json render(const Project& project, const core::Camera& camera, double width, double height,
            const std::optional<std::string>& selected, double explode, bool interactive, Context* context) {
    return render_with_stats(project, camera, width, height, selected, explode, interactive, context).faces;
}

}  // namespace viewport_render
