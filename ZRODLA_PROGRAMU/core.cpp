#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>

namespace mebel {

// Unique ID generator (12-char hex)
inline std::string uid() {
    static unsigned int seed = 0;
    seed ^= (seed << 13) ^ (seed >> 17);
    return std::to_string(seed++).substr(0, 12);
}

// Bounds-checked float conversion
inline double finite(double x, double lo = -100000.0, double hi = 100000.0) {
    if (!std::isfinite(x) || !(lo <= x && x <= hi)) {
        throw std::runtime_error(std::string("Wartość musi mieścić się w zakresie ") + 
                                 std::to_string(lo) + "–" + std::to_string(hi) + ".");
    }
    return x;
}
