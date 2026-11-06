#pragma once
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>

// Basic project structures for MebelStudio3D C++ implementation

struct Part {
    std::string id;
    double u = 0.0;  // Width dimension (mm)
    double v = 0.0;  // Height dimension (mm)  
    double t = 18.0; // Thickness (mm)
    double z = 0.0;  // Depth from floor
    std::string name;
    int generation = 0;
    std::map<std::string, double> assembly;
    std::vector<MachiningOp> machining;
    std::vector<Operation> operations;
    int material = -1;
    
    // Check if part is valid (dimensions positive)
    bool isValid() const {
        return u > 0.0 && v > 0.0 && t > 0.0;
    }
};

struct Hole {
    double diameter;
    double depth;
    double u, v; // Center position
    double face_index = 0; // Which face (top=0, bottom=1, etc.)
};

// Material properties for wood/materials
struct Material {
    int id;
    std::string name;
    double density = 0.0; // kg/m^3
    double hardness = 0.0; // Janka hardness scale or similar
    double shrinkage_x = 0.0; // X-direction shrinkage (%)
    double shrinkage_y = 0.0; // Y-direction shrinkage (%)
    double shrinkage_z = 0.0; // Z-direction shrinkage (%)
    int color_id = -1;

    Material() = default;
    Material(int id_, const std::string& name_, 
             double density_ = 0.0, double hardness_ = 0.0,
             double shrinkage_x_ = 0.0, double shrinkage_y_ = 0.0, 
             double shrinkage_z_ = 0.0, int color_id_ = -1)
        : id(id_), name(name_), density(density_), hardness(hardness_),
          shrinkage_x(shrinkage_x_), shrinkage_y(shrinkage_y_), 
          shrinkage_z(shrinkage_z_), color_id(color_id_) {}
};

// Machining operations like holes, cutouts, fillets
using MachiningOp = std::map<std::string, double>;

// Generic operation types (handles, hinges, etc.)
using Operation = std::map<std::string, double>;

struct Project {
    std::vector<Part> parts;
    std::vector<Material> materials;
    std::vector<Operation> hardware; // Hinges, handles, etc.
    int generation = 0;
    
    Project() = default;
};
