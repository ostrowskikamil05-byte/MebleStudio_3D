#pragma once
#include "core.hpp"
#include <string>

// UI preferences validation (colors, views, theme settings)

struct ThemeConfig {
    std::string name;      // light, dark, auto
    std::string accent_color; // Default color scheme
    bool show_grid = true;  // Show grid in viewport
    
    bool isValid() const {
        return !name.empty();
    }
};

ThemeConfig create_theme(const std::string& n, const std::string& accent = "#3b82f6", 
                         bool g = true);

std::vector<ThemeConfig> get_available_themes();

bool validate_theme_settings(const ThemeConfig& theme);
