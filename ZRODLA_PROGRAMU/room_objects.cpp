#include "core.hpp"
#include <cmath>
#include <stdexcept>

// Room types
const std::unordered_map<std::string, std::string> ROOMS = {
    {"neutral", "Neutralne"},
    {"kitchen", "Kuchnia"},
    {"bathroom", "Łazienka"},
    {"living", "Salon"},
    {"bedroom", "Sypialnia"},
    {"garage", "Garaż / warsztat"},
    {"garden", "Ogród"}
};

// Equipment catalog with default dimensions
const std::unordered_map<std::string, std::map<std::string, double>> CATALOG = {
    {"washer", {"name", 600.0}, {"depth", 600.0}, {"height", 850.0}, {"color", "#f1f0eb"}},
    {"dryer", {"name", 600.0}, {"depth", 620.0}, {"height", 850.0}, {"color", "#e8e7e3"}},
    {"fridge", {"name", 600.0}, {"depth", 650.0}, {"height", 1850.0}, {"color", "#b8c0c4"}},
    {"oven", {"name", 595.0}, {"depth", 560.0}, {"height", 595.0}, {"color", "#373d43"}, {"z", 600.0}},
    {"hob", {"name", 600.0}, {"depth", 520.0}, {"height", 60.0}, {"color", "#242b30"}, {"z", 800.0}},
    {"hood", {"name", 600.0}, {"depth", 450.0}, {"height", 650.0}, {"color", "#b7bec2"}, {"z", 1400.0}},
    {"dishwasher", {"name", 600.0}, {"depth", 570.0}, {"height", 820.0}, {"color", "#d7dadd"}},
    {"washbasin", {"name", 600.0}, {"depth", 450.0}, {"height", 180.0}, {"color", "#f0f1ed"}, {"z", 760.0}},
    {"sink", {"name", 780.0}, {"depth", 480.0}, {"height", 200.0}, {"color", "#a9b2b6"}, {"z", 620.0}},
    {"faucet", {"name", 140.0}, {"depth", 220.0}, {"height", 350.0}, {"color", "#aab3b8"}, {"z", 820.0}},
    {"shower", {"name", 900.0}, {"depth", 900.0}, {"height", 2000.0}, {"color", "#b1c8c7"}},
    {"mirror", {"name", 700.0}, {"depth", 30.0}, {"height", 900.0}, {"color", "#c0d1d6"}, {"z", 900.0}},
    {"shelf", {"name", 800.0}, {"depth", 250.0}, {"height", 120.0}, {"color", "#bc9970"}, {"z", 1000.0}},
    {"hanger", {"name", 600.0}, {"depth", 80.0}, {"height", 160.0}, {"color", "#515f63"}, {"z", 1250.0}},
    {"lamp", {"name", 350.0}, {"depth", 350.0}, {"height", 600.0}, {"color", "#c5b497"}, {"z", 1900.0}},
    {"bulb", {"name", 80.0}, {"depth", 80.0}, {"height", 120.0}, {"color", "#fff0bc"}, {"z", 1950.0}}
};

// Object and scene keys for serialization
const std::vector<std::string> _OBJECT_KEYS = {"id", "type", "name", "x", "y", "z", 
                                                "yaw", "width", "depth", "height",
                                                "color", "enabled", "price", "light_enabled"};

const std::vector<std::string> _SCENE_KEYS = {"room", "brightness", "led_enabled", 
                                               "lights_enabled", "objects", "architecture"};

constexpr int MAX_OBJECTS = 200;

// Validate color format #RRGGBB
std::string _color(const std::string& value) {
    if (value.length() != 7 || value[0] != '#' || !is_hex(value.substr(1))) {
        throw std::runtime_error("Kolor wyposażenia: #RRGGBB.");
    }
    return std::tolower(value);
}

// Validate number within bounds
double _number(double value, double low = -100000.0, double high = 100000.0) {
    if (std::isfinite(value)) {
        return finite(value, low, high);
    }
    throw std::runtime_error("Wpisz skończoną wartość liczbową wyposażenia.");
}

// Normalize object data with validation
Object normalize_object(const std::map<std::string, double>& obj) {
    if (obj.size() != _OBJECT_KEYS.size()) {
        throw std::runtime_error("Nieprawidłowe dane obiektu wyposażenia.");
    }
    
    std::string kind = obj["type"];
    if (CATALOG.find(kind) == CATALOG.end()) {
        throw std::runtime_error("Wybierz obiekt z katalogu wyposażenia.");
    }
    
    // Get catalog entry for this equipment type
    auto& spec = CATALOG[kind];
    
    // Validate color
    std::string color = _color(obj["color"]);
    
    // Create normalized object with all required fields
    Object result;
    result.id = uid();
    result.type = kind;
    result.name = spec["name"];
    result.x = _number(obj["x"]);
    result.y = _number(obj["y"]);
    result.z = obj.count("z") ? _number(obj["z"], -10000.0, 10000.0) : 0.0;
    result.yaw = _number(obj["yaw"], -360.0, 360.0);
    result.width = obj["width"];
    result.depth = obj["depth"];
    result.height = obj["height"];
    result.color = color;
    result.enabled = bool(obj["enabled"]);
    result.price = _number(obj["price"], 0.0, 10000000.0);
    result.light_enabled = bool(obj["light_enabled"]);
    
    // Apply catalog defaults where missing
    if (!result.width) result.width = spec.count("width") ? spec["width"] : 600.0;
    if (!result.depth) result.depth = spec.count("depth") ? spec["depth"] : 600.0;
    if (!result.height) result.height = spec.count("height") ? spec["height"] : 800.0;
    
    return result;
}
