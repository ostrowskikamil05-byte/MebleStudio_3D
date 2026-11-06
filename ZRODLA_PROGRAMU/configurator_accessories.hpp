#pragma once
#include "core.hpp"
#include <string>
#include <vector>

// Accessory validation for front panels (końcówki, nasadki, etc.)

struct FittingConfig {
    std::string id;
    std::string name;
    double thickness = 0.0;
    double width = 0.0;
    double height = 0.0;
    double diameter = 0.0;
    bool adjustable = false;
    std::string source;
    std::string note;

    FittingConfig() = default;
    FittingConfig(const std::string& id_, const std::string& name_, 
                  double thickness_ = 0.0, double width_ = 0.0, 
                  double height_ = 0.0, double diameter_ = 0.0,
                  bool adjustable_ = false, const std::string& source_ = "", 
                  const std::string& note_ = "")
        : id(id_), name(name_), thickness(thickness_), width(width_), 
          height(height_), diameter(diameter_), adjustable(adjustable_), 
          source(source_), note(note_) {}

    bool isValid() const {
        return !id.empty();
    }
};

FittingConfig create_fitting(const std::string& id, const std::string& name,
                              double thickness = 0.0, double width = 0.0, 
                              double height = 0.0, double diameter = 0.0,
                              bool adjustable = false,
                              const std::string& source = "", const std::string& note = "");

bool validate_fitting_dimensions(const FittingConfig& config);

void mount_fitting(Project project, const std::string& part_id, 
                    const FittingConfig& config);

void unmount_fitting(Project project, const std::string& part_id);

std::vector<FittingConfig> get_mounted_fittings(const Project& project, 
                                                  const std::string& part_id);
