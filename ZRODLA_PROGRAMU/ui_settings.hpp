#pragma once
#include "core.hpp"
#include <string>

// UI settings (scaling, views) validation

struct ViewportConfig {
    int scale = 100;       // Scale percentage
    std::string mode;      // orthographic, perspective, etc.
    double distance = 500.0; // Camera distance

    bool isValid() const {
        return scale > 0 && scale <= 1000;
    }
};

ViewportConfig create_viewport_config(int s, const std::string& m = "orthographic", 
                                       double d = 500.0);

std::vector<std::string> get_available_view_modes();

bool validate_viewport_dimensions(const ViewportConfig& config);
