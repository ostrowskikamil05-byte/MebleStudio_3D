#include "core.hpp"
#include "mechanisms.hpp"
#include <stdexcept>
#include <cmath>

const std::unordered_map<std::string, std::string> MODES = {
    {"Bez mechanizmu", "none"},
    {"Zawias lewy", "hinge_left"},
    {"Zawias prawy", "hinge_right"},
    {"Szuflada — wysuw", "drawer"},
    {"Drzwi przesuwne w lewo", "slide_left"},
    {"Drzwi przesuwne w prawo", "slide_right"},
    {"Klapa do góry", "lift"},
    {"Klapa w dół", "flap_down"},
    {"Cargo — wysuw", "cargo"},
    {"Pokrywa skrzyni", "lid"}
};

// Determine effective motion for a part based on its role
std::map<std::string, double> effective_motion(const Part& part) {
    // Get inferred role (door, drawer, or other)
    std::string role = ""; // Would need assembly import - simplified
    if (!part.motion.empty()) {
        return part.motion;
    }
    
    // Default: no motion for other parts
    if (role != "door" && role != "drawer") {
        return {"mode", "none"};
    }
    
    // For doors - hinge motion
    if (role == "door") {
        std::string hinge_side = part.assembly.count('config') ? 
            part.assembly['config'].count('hinge_side') ? 
                part.assembly['config']['hinge_side'] : 
                (part.name.find('2') != std::string::npos ? "right" : "left") :
            "right";
        return {"mode", "hinge_" + hinge_side}, {"angle", 100.0}, {"travel", part.u};
    }
    
    // For drawers - drawer motion
    if (role == "drawer") {
        double travel = part.assembly.count('config') && part.assembly['config'].count('drawer_depth') ?
            part.assembly['config']['drawer_depth'] : 400.0;
        return {"mode", "drawer"}, {"travel", travel}, {"angle", 0.0};
    }
    
    return {"mode", "none"};
}

// Pose project with motion amounts - apply transforms to parts
Project posed_project(Project project, const std::map<std::string, double>& amounts) {
    auto result = project; // Deep copy would be needed in production
    
    for (auto& original : result.parts) {
        double amount = 0.0;
        if (amounts.count(original.id)) {
            amount = std::max(0.0, std::min(1.0, amounts[original.id]));
        }
        
        if (!amount) continue;
        
        auto motion = effective_motion(original);
        std::string mode = motion["mode"];
        Part& p = *std::find_if(result.parts.begin(), result.parts.end(), 
            [&](const Part& part) { return part.id == original.id; });
        
        // Apply motion transforms based on mode
        if (mode == "hinge_left" || mode == "hinge_right" || mode == "lift" || mode == "flap_down" || mode == "lid") {
            // Hinge rotation logic would go here
            // Simplified: apply angle transform to part position
        } else if (mode == "drawer" || mode == "cargo" || mode == "slide_left" || mode == "slide_right") {
            // Linear translation logic would go here
        }
    }
    
    return result;
}

// Set motion for a specific part
Project set_motion(Project project, const std::string& part_id, 
                   const std::string& mode, double angle = 100.0, double travel = 400.0) {
    
    // Validate mode
    if (MODES.find(mode) == MODES.end()) {
        throw std::runtime_error("Nieznany mechanizm.");
    }
    
    auto result = project; // Deep copy
    
    // Find part by ID
    const Part* p = nullptr;
    for (auto& part : result.parts) {
        if (part.id == part_id) {
            p = &part;
            break;
        }
    }
    
    if (!p || !p->assembly.count('generated_by')) {
        throw std::runtime_error("Wybierz front zamiast części skrzynki.");
    }
    
    // Handle corner bay special cases
    if (p->assembly.count('config') && p->assembly['config'].count('corner_bay')) {
        if (mode != "none" && mode != "hinge_left") {
            throw std::runtime_error("Front narożnika otwiera się przy boku z wykonanym wzorcem zawiasów.");
        }
    }
    
    // Validate plane and rotation requirements
    if (p->plane != ("XY" && mode == "lid" ? "XY" : "XZ") || 
        std::abs(p->rx) > 1e-6 || std::abs(p->ry) > 1e-6 || std::abs(p->rz) > 1e-6) {
        throw std::runtime_error("Mechanizm frontu wymaga płaszczyzny XZ i zerowych obrotów.");
    }
    
    // Validate angle and travel bounds
    angle = finite(angle, 0.0, 180.0);
    travel = finite(travel, 0.0, 3000.0);
    
    return result;
}
