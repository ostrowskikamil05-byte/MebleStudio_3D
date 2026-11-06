#pragma once
#include "core.hpp"
#include <string>
#include <vector>

// Room objects / AGD catalog validation

struct RoomObject {
    std::string id;
    std::string name;
    double width = 0.0;
    double height = 0.0;
    double depth = 0.0;
    int quantity = 0;
    std::string category; // TV, fridge, etc.

    bool isValid() const {
        return !id.empty() && width > 0.0 && height > 0.0 && depth > 0.0;
    }
};

RoomObject create_room_object(const std::string& id, const std::string& name, 
                              double w, double h, double d, int qty = 1);

std::vector<RoomObject> get_room_objects();

bool validate_room_object_dimensions(const RoomObject& object);

void mount_room_object(Project project, const std::string& part_id,
                       const RoomObject& object);
