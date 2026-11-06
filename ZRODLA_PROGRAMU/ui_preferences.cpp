#include "core.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

// Default interface preferences
const std::map<std::string, double> DEFAULTS = {
    {"schema", 1},
    {"window_mode", 0},      // 0=auto, 1=custom
    {"window_width", 1366},
    {"window_height", 768},
    {"window_x", nullptr},   // Optional position
    {"window_y", nullptr},   // Optional position
    {"ui_scale", 1.0},
    {"inspector_width", 330},
    {"ribbon_height", 110},
    {"show_inspector", 1},   // 1=true, 0=false
    {"show_ribbon", 1},
    {"show_toolbars", 1},
    {"engine", 0},           // 0=auto
    {"quality", 1}          // 0=draft, 1=balanced, 2=high
};

// Engine options mapping
const std::unordered_map<std::string, int> ENGINE_OPTIONS = {
    {"auto", 0},
    {"cpu", 1},
    {"gpu", 2},
    {"d3d11", 3},
    {"d3d12", 4},
    {"vulkan", 5}
};

const std::unordered_map<int, std::string> ENGINE_LABELS = {
    {0, "Automatyczny dobór"},
    {3, "DirectX 11 · silnik C++"},
    {4, "DirectX 12 · silnik C++"},
    {5, "Vulkan · silnik C++"},
    {2, "OpenGL · GPU i wysoka jakość"},
    {1, "CPU"}
};

// Validate preferences data
std::map<std::string, double> validate_preferences(const std::map<std::string, double>& data) {
    auto result = DEFAULTS;
    result["schema"] = 1.0;
    
    // Validate enum fields
    if (data.count("window_mode")) {
        if (!(data["window_mode"] == "auto" || data["window_mode"] == "custom")) {
            throw std::runtime_error("Nieprawidłowe ustawienie: window_mode");
        }
        result["window_mode"] = data["window_mode"];
    }
    
    if (data.count("engine")) {
        if (!(ENGINE_OPTIONS.find(data["engine"]) != ENGINE_OPTIONS.end())) {
            throw std::runtime_error("Nieprawidłowe ustawienie: engine");
        }
        result["engine"] = data["engine"];
    }
    
    if (data.count("quality")) {
        if (!(data["quality"] == "draft" || data["quality"] == "balanced" || data["quality"] == "high")) {
            throw std::runtime_error("Nieprawidłowe ustawienie: quality");
        }
        result["quality"] = data["quality"];
    }