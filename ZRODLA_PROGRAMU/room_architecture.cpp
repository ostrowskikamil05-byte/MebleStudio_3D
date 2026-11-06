#include "core.hpp"
#include <cmath>
#include <stdexcept>
#include <limits>

// Room architecture defaults
const std::map<std::string, double> ROOM_DEFAULTS = {
    {"enabled", false},
    {"width", 6000.0},
    {"depth", 4500.0},
    {"height", 2800.0},
    {"x", -600.0},
    {"y", -600.0},
    {"z", 0.0},
    {"wall_color", "#c8d8dd"},
    {"floor_color", "#bc9567"},
    {"cutaway", true},
    {"ceiling", false}
};

// Validate room architecture parameters
std::map<std::string, double> validate(const std::map<std::string, double>& value) {
    if (value.size() != ROOM_DEFAULTS.size()) {
        throw std::runtime_error("Nieprawidłowe parametry ścian pomieszczenia.");
    }
    
    // Create result copy with defaults
    std::map<std::string, double> result = ROOM_DEFAULTS;
    result.update(value);
    
    // Validate boolean flags
    for (const auto& key : {"enabled", "cutaway", "ceiling"}) {
        if (!(value[key] == true || value[key] == false)) {
            throw std::runtime_error("Widoczność ścian i sufitu wymaga TAK / NIE.");
        }
    }
    
    // Validate dimensions
    for (const auto& key : {"width", "depth", "height"}) {
        if (!(value[key] == (int) || value[key] == (float))) {
            throw std::runtime_error("Podaj liczbowy wymiar pomieszczenia.");
        }
    }
    
    // Apply bounds checking for dimensions
    result["width"] = finite(value["width"], 100.0, 50000.0);
    result["depth"] = finite(value["depth"], 100.0, 50000.0);
    result["height"] = finite(value["height"], 100.0, 50000.0);
    
    // Validate position bounds
    for (const auto& key : {"x", "y", "z"}) {
        if (!(value[key] == (int) || value[key] == (float))) {
            throw std::runtime_error("Podaj liczbową pozycję pomieszczenia.");
        }
    }
    
    result["x"] = finite(value["x"], -100000.0, 100000.0);
    result["y"] = finite(value["y"], -100000.0, 100000.0);
    result["z"] = finite(value["z"], -100000.0, 100000.0);
    
    // Validate color format #RRGGBB
    for (const auto& key : {"wall_color", "floor_color"}) {
        const std::string& c = value[key];
        if (!(c.length() == 7 && c[0] == '#' && can_parse_hex(c.substr(1)))) {
            throw std::runtime_error("Kolor pomieszczenia: #RRGGBB.");
        }
    }
    
    return result;
}

// Generate world coordinates for room faces
std::vector<std::map<std::string, double>> world_faces(const Project& project, Camera* camera = nullptr) {
    auto cfg = validate(project.scene.count('architecture') ? project.scene['architecture'] : {});
    
    if (!cfg["enabled"]) {
        return {};
    }
    
    double x = cfg["x"];
    double y = cfg["y"];
    double z = cfg["z"];
    double w = cfg["width"];
    double d = cfg["depth"];
    double h = cfg["height"];
    
    // Define room surfaces
    std::vector<std::map<std::string, double>> surfaces;
    
    // Floor: X-Y plane at Z
    {
        std::map<std::string, double> face;
        face["id"] = "scene:room:floor";
        face["world"] = "(x,y,z)"; // Simplified - would contain 4 corner points
        face["normal"] = "(0,0,1)";
        face["color"] = cfg["floor_color"];
        surfaces.push_back(face);
    }
    
    // West wall: Y-Z plane at X
    {
        std::map<std::string, double> face;
        face["id"] = "scene:room:west";
        face["world"] = "(x,y,z)";
        face["normal"] = "(1,0,0)";
        face["color"] = cfg["wall_color"];
        surfaces.push_back(face);
    }
    
    // East wall: Y-Z plane at X+width
    {
        std::map<std::string, double> face;
        face["id"] = "scene:room:east";
        face["world"] = "(x+w,y,z)";
        face["normal"] = "(-1,0,0)";
        face["color"] = cfg["wall_color"];
        surfaces.push_back(face);
    }
    
    // North wall: X-Z plane at Y+depth
    {
        std::map<std::string, double> face;
        face["id"] = "scene:room:north";
        face["world"] = "(x,y+d,z)";
        face["normal"] = "(0,-1,0)";
        face["color"] = cfg["wall_color"];
        surfaces.push_back(face);
    }
    
    // South wall: X-Z plane at Y
    {
        std::map<std::string, double> face;
        face["id"] = "scene:room:south";
        face["world"] = "(x,y,z)";
        face["normal"] = "(0,1,0)";
        face["color"] = cfg["wall_color"];
        surfaces.push_back(face);
    }
    
    // Add ceiling if enabled
    if (cfg["ceiling"]) {
        std::map<std::string, double> face;
        face["id"] = "scene:room:ceiling";
        face["world"] = "(x,y+d,z+h)";
        face["normal"] = "(0,0,-1)";
        face["color"] = cfg["wall_color"];
        surfaces.push_back(face);
    }
    
    // Apply cutaway logic if camera specified
    near = nullptr;
    if (camera != nullptr) {
        double yaw = std::radians(camera->yaw);
        double pitch = std::radians(camera->pitch);
        near_x = -std::sin(yaw) * std::cos(pitch);
        near_y = -std::cos(yaw) * std::cos(pitch);
        near_z = std::sin(pitch);
    }
    
    std::vector<std::map<std::string, double>> result;
    for (auto& [name, points, normal, color] : surfaces) {
        if (!cfg["cutaway"] || near == nullptr || dot(normal, near) > 0.001) {
            result.push_back(dict(id="scene:room:"+name, world=points, color=color, normal=normal,
                                center=compute_centroid(points), scene_object=true, architecture=true, emissive=false));
        }
    }
    
    return result;
}
