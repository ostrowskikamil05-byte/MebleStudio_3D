// Mebel Studio 3D: shared geometry, project and manufacturing model (millimetres).
#pragma once
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "pyutil.h"

namespace viewport_render { struct Context; }

namespace core {

using json = py::json;  // nlohmann::ordered_json — dict w Pythonie zachowuje kolejność kluczy

// Odpowiedniki wyjątków Pythona
using py::ValueError;
using py::IndexError;
using py::KeyError;
using py::OSError;
using py::StopIteration;

using Point = std::array<double, 2>;
using Point3 = std::array<double, 3>;
using Drawn = json;  // lista ścian zwracana przez viewport_render::render

inline constexpr int VERSION = 1;

std::string uid();

double finite(const json& x, double lo = -100000, double hi = 100000);

double polygon_area(const std::vector<Point>& points);

std::vector<Point> validate_polygon(const std::vector<std::vector<double>>& points);

struct Part;

struct Material {
    std::string id = uid();
    std::string name = "Dąb naturalny";
    std::string color = "#bd9565";
    double price_m2 = 95;
    double sheet_u = 2800;
    double sheet_v = 2070;
    std::string kind = "plyta";
    double price_m3 = 0;

    double cost(const Part& part) const;

    json get_attr(const std::string& key) const;
    void set_attr(const std::string& key, const json& value);
};

struct Part {
    std::string id = uid();
    std::string name = "Nowy element";
    std::string group = "Mebel";
    double u = 600;
    double v = 400;
    double t = 18;
    std::string plane = "XY";
    double x = 0;
    double y = 0;
    double z = 0;
    double rx = 0;
    double ry = 0;
    double rz = 0;
    std::string material = "";
    bool grain = true;
    std::vector<bool> edges = {false, false, false, false};
    std::vector<std::vector<double>> contour = {};
    std::vector<std::vector<double>> holes = {};
    bool visible = true;
    std::string role = "other";
    json assembly = json::object();
    json operations = json::array();
    json fillets = json::array();
    json chamfers = json::array();
    json machining = json::array();
    json motion = json::object();
    // Exact, editable outline definition; contour remains the drawing tessellation.
    json profile = json::object();
    bool contour_edge = false;
    json decor = json::object();
    json style_source = json::object();
    // The quick configurator records provenance separately from assembly links.
    json configurator_source = json::object();
    // Furniture placement is separate from construction dimensions and motion.
    json layout = json::object();

    std::vector<Point> points() const;
    double area() const;
    double edge_m() const;
    std::pair<double, double> blank(double edge_t) const;
    void validate();

    json get_attr(const std::string& key) const;
    void set_attr(const std::string& key, const json& value);
};

// dataclasses.asdict(...)
json asdict(const Material& m);
json asdict(const Part& p);

bool inside(const Point& q, const std::vector<Point>& p);

json _default_scene();

class Project;
using ProjectPtr = std::shared_ptr<Project>;

class Project {
public:
    std::string name = "Mój projekt";
    std::vector<Part> parts = {};
    std::vector<Material> materials = [] {
        return std::vector<Material>{
            Material{},
            Material{.name = "Biały mat", .color = "#eeeeea", .price_m2 = 65},
            Material{.name = "Grafit", .color = "#4f5661", .price_m2 = 85},
            Material{.name = "MDF", .color = "#ba9871", .price_m2 = 70, .kind = "mdf"},
            Material{.name = "Sklejka", .color = "#d4b58a", .price_m2 = 110, .kind = "sklejka"},
            Material{.name = "Drewno lite — dąb", .color = "#b68a58", .sheet_u = 2500, .sheet_v = 200, .kind = "drewno", .price_m3 = 4500},
            Material{.name = "HDF", .color = "#b69d80", .price_m2 = 25, .kind = "hdf"}};
    }();
    json hardware = json::array();
    json scene = _default_scene();
    double kerf = 3.2;
    double margin = 10;
    double edge_t = 1;
    double edge_price = 3.5;
    double labor = 0;
    double markup = 20;

    Material& mat(const std::string& id);
    const Material& mat(const std::string& id) const;

    void validate();

    json data() const;

    static ProjectPtr from_data(const json& data);

    void save(const std::string& path);

    static ProjectPtr load(const std::string& path);

    std::map<std::string, double> costs() const;

    json get_attr(const std::string& key) const;
    void set_attr(const std::string& key, const json& value);
};

Point3 rotate(const Point3& point, double rx, double ry, double rz);

Point3 local_world(const Part& p, double u, double v, double w);

Point3 layout_point(const Part& p, const Point3& point);

Point3 inverse_layout_point(const Part& p, const Point3& point);

Point3 world(const Part& p, double u, double v, double w);

json faces(const Part& p, bool interactive = false);

class Camera {
public:
    double yaw, pitch, scale;
    Point3 center;
    std::array<double, 2> pan;

    Camera(double yaw = -38, double pitch = 24, double scale = 0.4, Point3 center = {300, 250, 420}, std::array<double, 2> pan = {0, 0});

    std::array<double, 4> _basis() const;
    // viewport_scene.paint_background zapisuje jasność sceny na kamerze (camera._scene_brightness)
    mutable double _scene_brightness = 1;

    Point3 view(const Point3& point) const;

    Point3 project(const Point3& point, double width, double height) const;

    void fit(const Project& project, double width, double height);

private:
    mutable std::optional<std::pair<double, double>> _basis_key;
    mutable std::array<double, 4> _basis_value{};
};

std::string shade(const std::string& color, double factor);

Point3 explosion_shift(const Part& p, const Camera& camera, double explode);

Drawn render_faces(const Project& project, const Camera& camera, double width, double height,
                   const std::optional<std::string>& selected = std::nullopt, double explode = 0,
                   bool interactive = false, viewport_render::Context* context = nullptr);

std::string svg_view(const Project& project, Camera* camera = nullptr, int width = 1100, int height = 720,
                     const std::optional<std::string>& selected = std::nullopt);

std::vector<Part> add_template(Project& project, const std::string& kind, double w = 600, double h = 800, double d = 500, double t = 18,
                               int shelves = 2, bool front = true, Point3 origin = {0, 0, 0}, const std::string& group = "");

// Area-sorted guillotine heuristic, with independent stock for material/thickness.
std::pair<json, std::vector<std::string>> cut_plan(Project& project);

void manufacturing_export(Project& project, const std::string& path);

void csv_export(const Project& project, const std::string& path);

void dxf_export(const Project& project, const std::string& path);

void report_export(Project& project, const std::string& path);

}  // namespace core
