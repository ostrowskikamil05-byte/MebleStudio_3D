#pragma once

#include "core.hpp"
#include "configurator_accessories.hpp"
#include "materials.hpp"
#include "mechanisms.hpp"
#include "room_architecture.hpp"
#include "room_objects.hpp"
#include "ui_settings.hpp"
#include "ui_preferences.hpp"
#include "palette.hpp"
#include "developer_logs.hpp"
#include "visual_catalogue.hpp"

Project create_empty_project() {
    Project proj;
    proj.generation = 1;

    // Create default materials
    proj.materials.push_back(Material("Eurodekor", "plyta", "#ceb28f"));
    proj.materials.push_back(Material("EGGER MDF Premium", "mdf", "#ba9871"));
    proj.materials.push_back(Material("Sklejka", "sklejka", "#d4b58a"));

    return proj;
}

void init_logging(const std::string& log_file = "app.log") {
    // Initialize developer logging system
    log_info("MebelStudio3D C++ initialized successfully");
    log_info("Project generation: 1.0 (C++ migration)");
}

std::string create_summary(const Project& project) {
    std::ostringstream summary;
    summary << "Projekt zawiera: " << project.parts.size() << " czesci";
    summary << " i " << project.materials.size() << " materialy.";

    if (!project.hardware.empty()) {
        summary << ", " << project.hardware.size() << " elementow hardware.";
    }

    return summary.str();
}

void validate_project(Project& project, bool strict = false) {
    // Validate all parts have valid dimensions
    for (auto& part : project.parts) {
        if (!part.isValid()) {
            std::ostringstream msg;
            msg << "Czesc ma nieprawidlowe wymiary: u=" << part.u
                << ", v=" << part.v << ", t=" << part.t;
            throw std::runtime_error(msg.str());
        }
    }

    // Validate materials exist
    if (strict && project.materials.empty()) {
        throw std::runtime_error("Brak materialow w projekcie.");
    }
}

void log_info(const std::string& message) {
    // Simplified logging - in production would write to file/console
    // This stub satisfies compilation requirements
    (void)message; // Suppress unused parameter warning
}
