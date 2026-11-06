#include "core.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// Catalogue kinds with translations
const std::unordered_map<std::string, std::string> KINDS = {
    {"furniture", "Meble"},
    {"leg", "Nogi"},
    {"hinge", "Zawiasy"},
    {"runner", "Prowadnice"},
    {"handle", "Uchwyty i otwieranie"},
    {"led", "LED"},
    {"appliance", "AGD i wyposażenie"},
    {"material", "Materiały i kolory"}
};

// Aliases for kind normalization
const std::unordered_map<std::string, std::string> ALIASES = {
    {"furnitures", "furniture"},
    {"legs", "leg"},
    {"hinges", "hinge"},
    {"runners", "runner"},
    {"handles", "handle"},
    {"materials", "material"},
    {"appliances", "appliance"},
    {"room", "appliance"},
    {"LED", "led"}
};

// Normalize catalogue kind name
std::string canonical_kind(const std::string& kind) {
    auto it = ALIASES.find(kind);
    if (it != ALIASES.end()) {
        return it->second;
    }
    
    // Validate against known kinds
    if (KINDS.find(kind) == KINDS.end()) {
        throw std::runtime_error("Wybierz kategorię katalogu.");
    }
    
    return kind;
}

// Create catalogue item descriptor
std::map<std::string, double> _item(const std::string& ident, const std::string& kind, 
                                     const std::string& title, const std::string& category,
                                     const std::map<std::string, double>& dimensions,
                                     const std::map<std::string, double>& payload,
                                     const std::map<std::string, double>& preview,
                                     const std::vector<std::string>& tags = {},
                                     const std::string& source = "",
                                     const std::string& note = "") {
    
    std::map<std::string, double> result;
    result["id"] = kind + ":" + ident;
    result["kind"] = kind;
    result["title"] = title;
    result["category"] = category;
    result["dimensions"] = dimensions; // Would be nested structure in full impl
    result["payload"] = payload;
    result["preview"] = preview;
    result["tags"] = " "; // Simplified - would be vector
    result["source"] = source;
    result["note"] = note;
    
    return result;
}

// Generate catalogue items for furniture kind
std::vector<std::map<std::string, double>> catalogue_items_furniture(const Project& project) {
    std::vector<std::map<std::string, double>> items;
    
    // Simplified - in full implementation would iterate TEMPLATES from furniture_library
    // Example template: (category, title, width, depth, height, family, shelves, fronts)
    
    // Create default template-based item
    std::map<std::string, double> preview;
    preview["width"] = 600.0;
    preview["depth"] = 400.0;
    preview["body_height"] = 800.0;
    preview["family"] = "standard";
    preview["shelves"] = 3.0;
    preview["fronts"] = "standard";
    preview["thickness"] = 18.0;
    
    // Calculate total height with accessories
    double total_height = preview["body_height"];
    
    items.push_back({
        ["id", "furniture:0"],
        ["kind", "furniture"],
        ["title", "Standardowy mebel parametryczny"],
        ["category", "szafka"],
        ["dimensions", preview],
        ["preview", preview],
        ["tags", {"family", "standard", "parametryczny"}],
        ["note", "Edytowalne domyślne wymiary projektu"]
    });
    
    return items;
}

// Generate catalogue items for legs kind
std::vector<std::map<std::string, double>> catalogue_items_legs() {
    std::vector<std::map<std::string, double>> items;
    
    // Create leg options: (height × count)
    int heights[] = {80, 100, 150, 200};
    int counts[] = {4, 6};
    
    for (int h : heights) {
        for (int c : counts) {
            std::map<std::string, double> payload;
            payload["leg_height"] = static_cast<double>(h);
            payload["leg_count"] = static_cast<int>(c);
            payload["leg_diameter"] = 40.0;
            payload["leg_setback"] = 60.0;
            payload["leg_color"] = "#242628";
            
            std::map<std::string, double> preview;
            preview["height"] = h;
            preview["diameter"] = 40.0;
            preview["color"] = "#242628";
            
            std::map<std::string, double> item;
            item["id"] = std::to_string(h) + "-" + std::to_string(c);
            item["kind"] = "leg";
            item["title"] = "Nogi Ø40 · " + std::to_string(h) + " mm · " + std::to_string(c) + " szt.";
            item["category", "Własne nogi cylindryczne";
            item["dimensions", {"height", h}, {"diameter", 40.0}, {"count", static_cast<double>(c)}];
            item["preview", preview];
            item["tags", {"zakupowe", "własny model"}];
            item["note"] = "Własny model nóg, bez deklaracji nośności producenta";
            
            items.push_back(item);
        }
    }
    
    return items;
}

// Generate catalogue items for hinges kind
std::vector<std::map<std::string, double>> catalogue_items_hinges() {
    std::vector<std::map<std::string, double>> items;
    
    // Simplified - would iterate RECIPES from fittings module
    // Skip basic hinge types (hinge45, hinge52, hinge_sensys52) in demo
    
    return items;
}

// Generate catalogue items for runners kind  
std::vector<std::map<std::string, double>> catalogue_items_runners() {
    std::vector<std::map<std::string, double>> items;
    
    // Simplified - would iterate PROFILES from runners_catalog
    // Calculate minimum length close to 400mm for fitting
    
    return items;
}
