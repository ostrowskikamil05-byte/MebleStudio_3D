#pragma once
#include "core.hpp"
#include <string>

// Palette (decorations catalog) validation

struct DecorItem {
    std::string id;        // Unique identifier
    std::string name;      // Display name
    std::string texture;   // Texture file path or URL
    double price = 0.0;    // Price in PLN
    
    bool isValid() const {
        return !id.empty();
    }
};

DecorItem create_decor(const std::string& id, const std::string& name, 
                       const std::string& texture, double price = 0.0);

std::vector<DecorItem> get_decor_items();

bool validate_decor_texture(const DecorItem& decor);
