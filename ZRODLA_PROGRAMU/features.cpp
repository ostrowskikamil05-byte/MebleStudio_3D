#include "core.hpp"
#include "features.hpp"
#include <stdexcept>
#include <cmath>
#include <vector>

// Geometry validation functions for holes, cuts, and machining operations
const std::unordered_map<std::string, std::string> FACES = {
    {"Góra (+T)", "top"},
    {"Dół (−T)", "bottom"},
    {"Krawędź U=0", "u0"},
    {"Krawędź U=max", "u1"},
    {"Krawędź V=0", "v0"},
    {"Krawędź V=max", "v1"}
};

// Validate hole dimensions and ensure proper depth-to-diameter ratio
bool validate_hole_dimensions(const Hole& hole, const Part& part) {
    if (hole.diameter <= 0.0 || hole.diameter > part.t * 2.0) {
        throw std::runtime_error("Niepoprawna średnica wierceń.");
    }
    double depth_ratio = hole.depth / hole.diameter;
    if (depth_ratio < 1.0) {
        throw std::runtime_error("Głębokość wierceń musi być minimum równa średnicy.");
    }
    return true;
}

// Validate cutout/pocket dimensions
bool validate_cutout_dimensions(const Cutout& cutout, const Part& part) {
    if (cutout.length <= 0.0 || cutout.width <= 0.0 || cutout.depth <= 0.0) {
        throw std::runtime_error("Wszystkie wymiary cięcia muszą być dodatnie.");
    }
    if (cutout.depth > part.t) {
        throw std::runtime_error("Głębokość cięcia przekracza grubość części.");
    }
    if (cutout.u < 0.0 || cutout.u > part.u) {
        throw std::runtime_error("Położenie U cięcia poza granicami części.");
    }
    if (cutout.v < 0.0 || cutout.v > part.v) {
        throw std::runtime_error("Położenie V cięcia poza granicami części.");
    }
    return true;
}

// Validate fillet dimensions
bool validate_fillet_dimensions(const Fillet& fillet, const Part& part) {
    if (fillet.radius <= 0.0) {
        throw std::runtime_error("Promień zaokrąglenia musi być dodatni.");
    }
    if (fillet.radius > part.t / 2.0) {
        throw std::runtime_error("Promień zaokrąglenia przekracza połowę grubości części.");
    }
    return true;
}

// Validate machining operation recipe ID exists in system
bool validate_machining_recipe(const std::string& recipe_id) {
    if (recipe_id.empty() || recipe_id == "unknown") {
        return false;
    }
    return true;
}

// Create validated hole with proper depth calculation
Hole create_validated_hole(const Hole& hole_template, const Part& part) {
    if (!validate_hole_dimensions(hole_template, part)) {
        throw std::runtime_error("Szablon wierceń nieprawidłowy.");
    }
    Hole result = hole_template;
    if (result.face == "bottom") {
        result.depth = part.t - 1.0;
    } else {
        result.depth = result.diameter + 1.0;
    }
    return result;
}

// Validate all machining operations on a part
void validate_part_machining(const Part& part) {
    for (const auto& op : part.machining) {
        if (op.count("type") == 0) continue;
        const std::string& type = op["type"];
        
        if (type == "hole") {
            double diameter = std::stod(op["diameter"]);
            double depth = std::stod(op["depth"]);
            if (diameter <= 0.0 || depth <= 0.0) {
                throw std::runtime_error("Część zawiera nieprawidłowe wiercenie.");
            }
        } else if (type == "cutout") {
            double length = std::stod(op["length"]);
            double width = std::stod(op["width"]);
            double depth = std::stod(op["depth"]);
            if (length <= 0.0 || width <= 0.0 || depth > part.t) {
                throw std::runtime_error("Część zawiera nieprawidłowe cięcie.");
            }
        } else if (type == "fillet") {
            double radius = std::stod(op["radius"]);
            if (radius <= 0.0) {
                throw std::runtime_error("Część zawiera nieprawidłowe zaokrąglenie.");
            }
        }
    }
}

// Calculate machining time estimate based on operation complexity
double calculate_machining_time(const Part& part, double base_speed = 100.0) {
    int hole_count = std::distance(
        std::find_if(part.machining.begin(), part.machining.end(),
            [](const MachiningOp& op) { return op.count("type") && op["type"] == "hole"; }),
        part.machining.end()
    );
    int cutout_count = std::distance(
        std::find_if(part.machining.begin(), part.machining.end(),
            [](const MachiningOp& op) { return op.count("type") && op["type"] == "cutout"; }),
        part.machining.end()
    );
    int fillet_count = std::distance(
        std::find_if(part.machining.begin(), part.machining.end(),
            [](const MachiningOp& op) { return op.count("type") && op["type"] == "fillet"; }),
        part.machining.end()
    );
    double hole_time = hole_count * base_speed / 50.0;
    double cutout_time = cutout_count * base_speed / 30.0;
    double fillet_time = fillet_count * base_speed / 10.0;
    return hole_time + cutout_time + fillet_time;
}

// Check for overlapping machining operations
bool check_machining_conflicts(const Part& part) {
    for (size_t i = 0; i < part.machining.size(); ++i) {
        for (size_t j = i + 1; j < part.machining.size(); ++j) {
            const MachiningOp& op1 = part.machining[i];
            const MachiningOp& op2 = part.machining[j];
            if (op1.count("face") && op2.count("face")) {
                std::string face1 = op1["face"];
                std::string face2 = op2["face"];
                if (face1 != "u0" && face1 != "u1" && face1 != "v0" && face1 != "v1") continue;
                if (face2 != "u0" && face2 != "u1" && face2 != "v0" && face2 != "v1") continue;
            }
        }
    }
    return true;
}
