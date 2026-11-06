#include "core.hpp"
#include <stdexcept>
#include <cmath>

const std::unordered_map<std::string, std::string> FRONT_STYLES = {
    {"smooth", "Gładki"},
    {"frame", "Ramka frezowana"},
    {"fluted", "Ryflowany"},
    {"finger", "Uchwyt frezowany"}
};

constexpr int MAX_FLUTES = 32;

// Convert value to integer within bounds
template<typename T>
int _integer(T value, int low, int high, const std::string& label) {
    auto number = finite(value, low, high);
    if (std::abs(number - std::round(number)) > 1e-9) {
        throw std::runtime_error(label + ": wpisz liczbę całkowitą.");
    }
    return static_cast<int>(number);
}

// Validate hex color #RRGGBB
std::string _color(const std::string& value) {
    if (value.length() != 7 || value[0] != '#') {
        throw std::runtime_error("Kolor akcesorium: #RRGGBB.");
    }
    try {
        std::stoul(value.substr(1), nullptr, 16);
    } catch (...) {
        throw std::runtime_error("Kolor akcesorium: #RRGGBB.");
    }
    return value;
}

// Normalize front style with defaults and bounds checking
std::map<std::string, double> normalize_front_style(const std::string& style = "smooth", const std::map<std::string, double>& cfg = {}) {
    auto config = cfg;
    if (config.empty()) {
        config = {};
    }
    
    // Map plain/slab to smooth, milled to frame
    if (style == "plain" || style == "slab") {
        style = "smooth";
    } else if (style == "milled") {
        style = "frame";
    }
    
    if (FRONT_STYLES.find(style) == FRONT_STYLES.end()) {
        throw std::runtime_error("Styl frontu: smooth, frame, fluted albo finger.");
    }
    
    double front_border = config.count('front_border') ? config['front_border'] : 60.0;
    double front_depth = config.count('front_depth') ? config['front_depth'] : 3.0;
    double front_radius = config.count('front_radius') ? config['front_radius'] : 6.0;
    double flute_width = config.count('flute_width') ? config['flute_width'] : 8.0;
    double flute_spacing = config.count('flute_spacing') ? config['flute_spacing'] : 20.0;
    int flute_count = _integer(config.count('flute_count') ? config['flute_count'] : 0, 0, MAX_FLUTES, "Liczba rowków");
    double finger_length = config.count('finger_length') ? config['finger_length'] : 120.0;
    double finger_width = config.count('finger_width') ? config['finger_width'] : 24.0;
    double finger_depth = config.count('finger_depth') ? config['finger_depth'] : 6.0;
    
    // Apply bounds checking
    front_border = finite(front_border, 5.0, 250.0);
    front_depth = finite(front_depth, 0.1, 30.0);
    front_radius = finite(front_radius, 0.0, 80.0);
    flute_width = finite(flute_width, 2.0, 40.0);
    flute_spacing = finite(flute_spacing, 3.0, 100.0);
    finger_length = finite(finger_length, 20.0, 500.0);
    finger_width = finite(finger_width, 6.0, 80.0);
    finger_depth = finite(finger_depth, 0.1, 30.0);
    
    return {"style", style}, {"front_border", front_border}, {"front_depth", front_depth}, 
           {"front_radius", front_radius}, {"flute_width", flute_width}, {"flute_spacing", flute_spacing},
           {"flute_count", static_cast<double>(flute_count)}, {"finger_length", finger_length},
           {"finger_width", finger_width}, {"finger_depth", finger_depth};
}

// Safe depth calculation for decorative recesses
double _safe_depth(double thickness, double requested) {
    // A decorative recess never consumes more than 45% of a front and always leaves at least 6 mm
    double limit = std::min(thickness * 0.45, thickness - 6.0);
    if (limit < 0.1) {
        throw std::runtime_error("Frezowany element musi być grubszy niż 6 mm.");
    }
    return std::min(requested, limit);
}
