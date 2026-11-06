#pragma once
#include "core.hpp"
#include <string>
#include <vector>

// Mechanizmy otwierania (hinges, handles, etc.)
// Forward declarations needed for mechanisms.cpp implementation

void set_motion(Project& project, const std::string& part_id, 
                const std::string& type, double min_angle, double max_angle);

std::string effective_motion(const std::string& source, const std::string& part_type);

enum class MotionMode {
    Standard,
    SoftClose,
    Damping,
    SpringReturn,
    CargoSlide,
    LiftAndLower,
    Foldable,
    PivotHinge,
    SwingLift
};

void set_motion(Project& project, const std::string& part_id, 
                MotionMode mode, double min_angle, double max_angle);

std::vector<MotionMode> get_available_modes();
