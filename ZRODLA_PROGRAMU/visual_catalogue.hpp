#pragma once
#include "core.hpp"
#include <string>

// Visual catalogue for product miniatures and search functionality

struct CatalogItem {
    std::string id;        // Unique identifier
    std::string name;      // Product name
    std::string thumbnail; // Thumbnail image path
    double scale = 100.0;  // Display scale percentage
    
    bool isValid() const {
        return !id.empty();
    }
};

CatalogItem create_catalog_entry(const std::string& id, const std::string& name, 
                                  const std::string& thumb, double s = 100.0);

std::vector<CatalogItem> get_catalog_items();

bool search_catalog(const std::string& query);
