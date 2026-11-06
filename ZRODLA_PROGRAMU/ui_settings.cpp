#include "core.hpp"
#include "ui_settings.hpp"
#include <stdexcept>

// Default interface preferences
const std::unordered_map<std::string, double> DEFAULTS = {
    {"window_mode", 1.0},      // Auto or custom window size
    {"window_width", 1280.0},  // Custom preset width
    {"window_height", 720.0},  // Custom preset height
    {"ui_scale", 1.0},         // UI scale factor
    {"inspector_width", 260},   // Sidebar panel width
    {"ribbon_height", 48}       // Top ribbon height
};

// Window mode presets
const std::unordered_map<std::string, std::pair<int, int>> WINDOW_PRESETS = {
    {"900x600", {900, 600}},
    {"1280x720", {1280, 720}},
    {"1366x768", {1366, 768}},
    {"1920x1080", {1920, 1080}}
};

// UI scale options
const std::unordered_map<double, std::string> UI_SCALES = {
    {0.75, "75%"},
    {0.9, "90%"},
    {1.0, "100%"},
    {1.1, "110%"},
    {1.25, "125%"},
    {1.5, "150%"},
    {1.75, "175%"},
    {2.0, "200%"}
};

// Viewport engine labels
const std::unordered_map<std::string, std::string> ENGINE_LABELS = {
    {"cpu", "CPU (standard)"},
    {"opengl", "OpenGL (wymaga GPU)"},
    {"native", "Native C++ (szybszy, stabilny)"}
};

// Viewport quality modes
const std::unordered_map<std::string, int> QUALITY_MODES = {
    {"draft", 1},      // Quick preview
    {"balanced", 2},   // Balanced
    {"high", 3}        // High quality
};
