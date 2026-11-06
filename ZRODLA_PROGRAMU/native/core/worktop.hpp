#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace mebel::model::worktop {
// fixed to_chars rounds the exact binary value directly to decimal, ties-even.
// Parsing that decimal once avoids the double rounding of value * 10^digits.
// This is also the dtoa/strtod model used by CPython round(x, ndigits).
inline double decimal_round(double value, int digits) {
    if (!std::isfinite(value))
        return value;
    std::array<char, 512> buffer{};
    const auto output = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                      std::chars_format::fixed, digits);
    if (output.ec != std::errc())
        throw std::runtime_error("Nie można zaokrąglić współrzędnej blatu.");
    double rounded = 0;
    const auto input =
        std::from_chars(buffer.data(), output.ptr, rounded, std::chars_format::fixed);
    if (input.ec != std::errc() || input.ptr != output.ptr)
        throw std::runtime_error("Nie można odczytać zaokrąglonej współrzędnej blatu.");
    return rounded == 0 ? std::copysign(0., value) : rounded;
}
} // namespace mebel::model::worktop
