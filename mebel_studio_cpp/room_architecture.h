// Presentation room enclosure; never creates manufactured furniture parts.
#pragma once
#include <vector>
#include "core.h"

namespace room_architecture {

using json = core::json;

json defaults();

// Zwraca kanoniczną kopię; ValueError dla nieprawidłowych danych.
json validate(const json& value);

// camera == None  ->  nullptr (wszystkie ściany, bez odsłaniania od strony kamery)
json world_faces(const core::Project& project, const core::Camera* camera = nullptr);
json world_faces(const core::Project& project, const core::Camera& camera);

std::vector<core::Point3> vertices(const core::Project& project);

}  // namespace room_architecture
