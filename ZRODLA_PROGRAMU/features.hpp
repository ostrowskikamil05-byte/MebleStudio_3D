#pragma once
#include "core.hpp"

// Geometry validation functions for holes, cuts, and machining operations
// Forward declarations for validation types

struct Cutout {
    double length, width, depth;
    double u, v; // Position coordinates
    std::string face = "bottom";
    std::string purpose = "cutout";
};

struct Fillet {
    double radius;
    double u, v;
    std::string edge = "u0";
};

// Validation functions declared in features.cpp
bool validate_hole_dimensions(const Hole& hole, const Part& part);
bool validate_cutout_dimensions(const Cutout& cutout, const Part& part);
bool validate_fillet_dimensions(const Fillet& fillet, const Part& part);
bool validate_machining_recipe(const std::string& recipe_id);
Hole create_validated_hole(const Hole& hole_template, const Part& part);
Cutout create_validated_cutout(const Cutout& cutout_template, const Part& part);
Fillet create_validated_fillet(const Fillet& fillet_template, const Part& part);
void validate_part_machining(const Part& part);
double calculate_machining_time(const Part& part, double base_speed = 100.0);
bool check_machining_conflicts(const Part& part);
