#pragma once
#include "core.hpp"
#include <string>
#include <vector>

// Room architecture validation (walls, ceilings, floors)

struct RoomConfig {
    double width = 0.0;      // Room width in mm
    double height = 0.0;     // Room height in mm
    double depth = 0.0;      // Room depth in mm
    std::string material_id; // Wall/floor material

    bool isValid() const {
        return width > 0.0 && height > 0.0 && depth > 0.0;
    }
};

RoomConfig create_room(double w, double h, double d, const std::string& mat = "");

bool validate_room_dimensions(const RoomConfig& config);

void mount_wall_unit(Project project, const std::string& part_id, 
                     double x, double y, double z, const std::string& type);

std::vector<RoomConfig> get_available_rooms();
