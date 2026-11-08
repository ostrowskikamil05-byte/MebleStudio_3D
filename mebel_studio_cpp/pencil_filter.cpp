#include "pencil_filter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pencil_filter {

namespace {

uint32_t mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

double hash01(int x, int y, uint32_t seed) { return (mix32(static_cast<uint32_t>(x) * 73856093U ^ static_cast<uint32_t>(y) * 19349663U ^ seed) >> 8) / 16777216.0; }

// Gładki szum wartościowy 0…1.
double noise(double x, double y, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    double fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    const double a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed), c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

}  // namespace

double luminance(uint8_t r, uint8_t g, uint8_t b) { return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0; }

Stats apply(int width, int height, std::vector<uint8_t>& rgba, const std::vector<int32_t>& owner, const std::vector<float>& depth, const Settings& s) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("Filtr ołówka: rozmiar obrazu musi być dodatni.");
    const size_t count = static_cast<size_t>(width) * height;
    if (rgba.size() != count * 4 || owner.size() != count || depth.size() != count)
        throw std::invalid_argument("Filtr ołówka: bufory obrazu, identyfikatorów i głębi mają różne rozmiary.");
    Stats stats;
    // zakres głębi bryły — próg skoku głębi zależy od wielkości sceny
    float low = std::numeric_limits<float>::infinity(), high = -low;
    for (size_t i = 0; i < count; ++i)
        if (owner[i] >= 0 && std::isfinite(depth[i])) { low = std::min(low, depth[i]); high = std::max(high, depth[i]); ++stats.covered; }
    if (stats.covered == 0) return stats;
    const float jump = std::max(1e-4f, (high - low) * 0.035f);

    std::vector<float> lum(count, 1.f);
    for (size_t i = 0; i < count; ++i) lum[i] = static_cast<float>(luminance(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]));

    // 1. mapa obrysu: granica elementów, skok głębi i załamanie między ściankami (różnica jasności)
    std::vector<float> edge(count, 0.f);
    auto mark = [&](size_t i, float value) { edge[i] = std::max(edge[i], value); };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            for (int step = 0; step < 2; ++step) {
                const int nx = x + (step == 0), ny = y + (step == 1);
                if (nx >= width || ny >= height) continue;
                const size_t j = static_cast<size_t>(ny) * width + nx;
                const bool here = owner[i] >= 0, there = owner[j] >= 0;
                if (!here && !there) continue;
                if (here != there) { mark(here ? i : j, 1.f); continue; }          // krawędź sylwetki: znaczymy piksel bryły
                if (owner[i] != owner[j]) { mark(depth[i] > depth[j] ? i : j, 1.f); continue; }  // granica elementów: bliższy piksel
                if (std::fabs(depth[i] - depth[j]) > jump) { mark(depth[i] > depth[j] ? i : j, .9f); continue; }
                if (std::fabs(lum[i] - lum[j]) > 0.045f) mark(lum[i] < lum[j] ? i : j, .75f);  // załamanie: ciemniejsza strona
            }
        }

    // 2. wyjście: papier + kreskowanie + obrys z drżeniem
    std::vector<uint8_t> out = rgba;
    const double amp = 1.6 * std::max(0.0, s.wobble);
    const double spacing = std::max(2.5, s.spacing);
    auto sample_edge = [&](int x, int y, double ox, double oy) -> double {
        const int sx = static_cast<int>(std::lround(x + ox)), sy = static_cast<int>(std::lround(y + oy));
        if (sx < 0 || sy < 0 || sx >= width || sy >= height) return 0;
        return edge[static_cast<size_t>(sy) * width + sx];
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            double r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
            const bool solid = owner[i] >= 0;
            if (solid) {
                // ziarno papieru
                const double grain = (hash01(x, y, s.seed) - .5) * 0.035;
                r *= 1 + grain; g *= 1 + grain; b *= 1 + grain;
                if (s.hatching) {
                    const double l = lum[i];
                    // pierwsza warstwa kresek (45°) od jasności < 0,90, druga (krzyżowa) od < 0,82; kreski nierówne i przerywane
                    const double drift = noise(x / 38.0, y / 38.0, s.seed + 5) * spacing;
                    double darkness = 0;
                    if (l < 0.90) {
                        const double phase = std::fmod(x + y + drift + 4096 * spacing, spacing);
                        const double broken = noise(x / 9.0, y / 9.0, s.seed + 11);
                        if (phase < 1.0 && broken > 0.18) darkness += 0.20 + 0.45 * std::min(1.0, (0.90 - l) / 0.16);
                    }
                    if (l < 0.82) {
                        const double phase = std::fmod(x - y + drift * 1.3 + 4096 * spacing, spacing * 1.1);
                        const double broken = noise(x / 8.0, y / 8.0, s.seed + 17);
                        if (phase < 1.0 && broken > 0.22) darkness += 0.18 + 0.4 * std::min(1.0, (0.82 - l) / 0.14);
                    }
                    if (darkness > 0) {
                        darkness = std::min(0.8, darkness) * s.strength;
                        r += (60 - r) * darkness; g += (60 - g) * darkness; b += (64 - b) * darkness;
                        ++stats.hatch_pixels;
                    }
                }
            }
            // obrys: dwa pociągnięcia o różnym drżeniu (drugie słabsze) — linia nie jest idealnie prosta ani jednolita
            const double ox1 = (noise(x / 14.0, y / 14.0, s.seed + 1) - .5) * 2 * amp, oy1 = (noise(x / 14.0, y / 14.0, s.seed + 2) - .5) * 2 * amp;
            const double ox2 = (noise(x / 11.0, y / 11.0, s.seed + 3) - .5) * 2 * amp * 1.3, oy2 = (noise(x / 11.0, y / 11.0, s.seed + 4) - .5) * 2 * amp * 1.3;
            double line = sample_edge(x, y, ox1, oy1);
            line = std::max(line, 0.55 * sample_edge(x, y, ox2, oy2));
            if (line > 0) {
                const double pressure = 0.72 + 0.28 * noise(x / 23.0, y / 23.0, s.seed + 7);
                const double alpha = std::min(1.0, line * pressure * s.strength);
                r += (52 - r) * alpha; g += (52 - g) * alpha; b += (57 - b) * alpha;
                if (alpha > 0.35) ++stats.edge_pixels;
                if (!solid && out[i * 4 + 3] < 255 * alpha) out[i * 4 + 3] = static_cast<uint8_t>(std::min(255.0, 255 * alpha));  // linia wystaje poza sylwetkę
            }
            out[i * 4] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.0, 255.0)));
            out[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.0, 255.0)));
            out[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.0, 255.0)));
        }
    rgba = std::move(out);
    return stats;
}

}  // namespace pencil_filter
