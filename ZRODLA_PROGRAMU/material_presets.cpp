#include "core.hpp"

// Material presets with editable workshop thickness choices
const std::unordered_map<std::string, std::map<std::string, double>> MATERIAL_PRESETS = {
    {"Eurodekor — płyta wiórowa (oferta UK)", {
        {"kind", "plyta"},
        {"sheet_u", 2800.0},
        {"sheet_v", 2070.0},
        {"color", "#ceb28f"},
        {"note", "Karta EGGER z 21.02.2023, zakład Hexham / UK"}
    },
     {"thicknesses", {8.0, 12.0, 15.0, 16.0, 18.0, 19.0, 22.0, 25.0, 28.0, 30.0, 38.0}},
     {"stock_thicknesses", {18.0}}},
    
    {"PerfectSense Premium Matt PM — MDF (PL)", {
        {"kind", "mdf"},
        {"sheet_u", 2800.0},
        {"sheet_v", 2070.0},
        {"color", "#ba9871"},
        {"note", "EGGER, oferta PL: MDF lakierowany Premium Matt E1E05 TSCA ST"}
    },
     {"thicknesses", {10.0, 12.0, 16.0, 18.0, 19.0, 22.0, 25.0}},
     {"stock_thicknesses", {18.0}}},
    
    {"PerfectSense Premium Gloss PG — MDF (PL)", {
        {"kind", "mdf"},
        {"sheet_u", 2800.0},
        {"sheet_v", 2070.0},
        {"color", "#d2d1ce"},
        {"note", "EGGER, oferta PL: MDF lakierowany Premium Gloss E1E05 TSCA ST"}
    },
     {"thicknesses", {10.0, 12.0, 16.0, 18.0, 19.0, 22.0, 25.0}},
     {"stock_thicknesses", {18.0}}},
    
    {"Sklejka — ustawienie warsztatowe", {
        {"kind", "sklejka"},
        {"sheet_u", 2500.0},
        {"sheet_v", 1250.0},
        {"color", "#d4b58a"}
    },
     {"thicknesses", {4.0, 6.0, 8.0, 9.0, 12.0, 15.0, 18.0, 21.0, 24.0, 30.0}}},
    
    {"HDF — ustawienie warsztatowe", {
        {"kind", "hdf"},
        {"sheet_u", 2800.0},
        {"sheet_v", 2070.0},
        {"color", "#b69d80"}
    },
     {"thicknesses", {3.0, 4.0, 6.0}}},
    
    {"Drewno lite — ustawienie warsztatowe", {
        {"kind", "drewno"},
        {"sheet_u", 2500.0},
        {"sheet_v", 200.0},
        {"color", "#b68a58"}
    },
     {"thicknesses", {18.0, 20.0, 22.0, 25.0, 28.0, 30.0, 40.0, 45.0, 50.0, 60.0}}}
};

// Apply material preset to part with resize dimensions
Project apply_material_preset(Project project, const std::string& part_id, 
                              const std::string& name, double thickness) {
    auto spec = MATERIAL_PRESETS.find(name);
    
    if (spec == MATERIAL_PRESETS.end()) {
        throw std::runtime_error("Nieznany preset materiału.");
    }
    
    // Get material specifications
    const auto& material_spec = spec->second;
    std::string kind = material_spec["kind"];
    double sheet_u = material_spec["sheet_u"];
    double sheet_v = material_spec["sheet_v"];
    std::string color = material_spec["color"];
    
    // Validate thickness choice
    bool found_thickness = false;
    for (const auto& t : material_spec["thicknesses"]) {
        if (std::abs(t - thickness) < 1e-9) {
            found_thickness = true;
            break;
        }
    }
    
    if (!found_thickness) {
        throw std::runtime_error("Wybierz grubość z listy tego materiału.");
    }
    
    // Find and update part (simplified - would need proper rebuild)
    const Part* part = nullptr;
    for (auto& p : project.parts) {
        if (p.id == part_id) {
            part = &p;
            break;
        }
    }
    
    if (!part) {
        throw std::runtime_error("Część nie istnieje.");
    }
    
    // Resize dimensions would happen here with proper dependency rebuild
    
    // Find or create material
    const Material* material = nullptr;
    for (auto& m : project.materials) {
        if (m.name == name) {
            material = &m;
            break;
        }
    }
    
    if (!material) {
        Material mat(name, kind, color);
        project.materials.push_back(mat);
        material = &project.materials.back();
    }
    
    part->material = material->id;
    
    return project;
}
