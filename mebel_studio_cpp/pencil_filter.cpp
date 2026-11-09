#include "pencil_filter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace pencil_filter {

namespace {

constexpr int BLOCK = 4;           // drżenie obrysu jest stałe w blokach 4×4 px (przesunięcie i tak jest zaokrąglane do piksela)
constexpr size_t PARALLEL_MIN = 160000;  // poniżej tylu pikseli wątki kosztują więcej niż dają
constexpr size_t CACHE_LIMIT = 6;

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

inline uint8_t lum8(uint8_t r, uint8_t g, uint8_t b) { return static_cast<uint8_t>((54 * r + 183 * g + 19 * b + 128) >> 8); }

// Wzór zależny tylko od rozmiaru obrazu, ziarna, odstępu kresek i siły drżenia — nie od zawartości klatki.
struct Pattern {
    int width = 0, height = 0;
    uint32_t seed = 0;
    int spacing_key = 0, wobble_key = 0;
    int bw = 0, bh = 0;                  // liczba bloków 4×4
    std::vector<uint32_t> hatch;         // progi trzech rodzin kresek (po 8 bitów; 255 = brak kreski w tym pikselu)
    std::vector<int8_t> jitter;          // na blok: dx1, dy1, dx2, dy2 (obrys: dwa pociągnięcia)
    std::vector<uint8_t> pressure;       // na blok: nacisk ołówka 0…255
    std::vector<uint8_t> paper;          // ziarno papieru 256 × 256
};

std::mutex cache_mutex;
std::vector<std::shared_ptr<Pattern>>& cache() {
    static std::vector<std::shared_ptr<Pattern>> list;
    return list;
}

template <class F>
void parallel_rows(int height, int threads, F&& work) {  // work(y0, y1, indeks_wątku)
    if (threads <= 1 || height < 2) { work(0, height, 0); return; }
    int band = (height + threads - 1) / threads;
    band = (band + BLOCK - 1) / BLOCK * BLOCK;  // pasy zaczynają się na granicy bloków 4×4 (każdy blok ma jednego właściciela)
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) {
        const int y0 = t * band, y1 = std::min(height, y0 + band);
        if (y0 >= y1) break;
        pool.emplace_back([&work, y0, y1, t] { work(y0, y1, t); });
    }
    work(0, std::min(height, band), 0);
    for (auto& thread : pool) thread.join();
}

std::shared_ptr<Pattern> build_pattern(int width, int height, const Settings& s, double spacing, int threads) {
    auto p = std::make_shared<Pattern>();
    p->width = width; p->height = height; p->seed = s.seed;
    p->spacing_key = static_cast<int>(std::lround(spacing * 100)); p->wobble_key = static_cast<int>(std::lround(s.wobble * 100));
    p->bw = (width + BLOCK - 1) / BLOCK; p->bh = (height + BLOCK - 1) / BLOCK;
    p->hatch.assign(static_cast<size_t>(width) * height, 0x00ffffffU);
    p->jitter.assign(static_cast<size_t>(p->bw) * p->bh * 4, 0);
    p->pressure.assign(static_cast<size_t>(p->bw) * p->bh, 255);
    p->paper.resize(256 * 256);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) p->paper[static_cast<size_t>(y) * 256 + x] = static_cast<uint8_t>(hash01(x, y, s.seed) * 255.0);
    const double amp = 1.6 * std::max(0.0, s.wobble);
    for (int by = 0; by < p->bh; ++by)
        for (int bx = 0; bx < p->bw; ++bx) {
            const double cx = bx * BLOCK + BLOCK / 2.0, cy = by * BLOCK + BLOCK / 2.0;
            const size_t b = static_cast<size_t>(by) * p->bw + bx;
            auto offset = [&](double scale, uint32_t salt, double gain) { return static_cast<int8_t>(std::lround((noise(cx / scale, cy / scale, s.seed + salt) - .5) * 2 * amp * gain)); };
            p->jitter[b * 4 + 0] = offset(14.0, 1, 1.0); p->jitter[b * 4 + 1] = offset(14.0, 2, 1.0);
            p->jitter[b * 4 + 2] = offset(11.0, 3, 1.3); p->jitter[b * 4 + 3] = offset(11.0, 4, 1.3);
            p->pressure[b] = static_cast<uint8_t>(std::lround(255.0 * (0.72 + 0.28 * noise(cx / 23.0, cy / 23.0, s.seed + 7))));
        }
    // Trzy rodziny kresek. Każda kreska ma własny próg ciemności: im ciemniejsza ścianka, tym więcej kresek się pojawia
    // (stopniowo, jedna po drugiej), a przerwy w kreskach (szum „broken”) dają wygląd ręcznego kreskowania.
    const double sp = std::max(2.5, spacing);
    parallel_rows(height, threads, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < width; ++x) {
                const double drift = noise(x / 38.0, y / 38.0, s.seed + 5) * sp;
                uint32_t a = 255, c2 = 255, c3 = 255;
                {
                    const double u = x + y + drift + 4096 * sp;
                    const double k = std::floor(u / sp);
                    if (u - k * sp < 1.0 && noise(x / 9.0, y / 9.0, s.seed + 11) > .18)
                        a = static_cast<uint32_t>(10 + 24 * hash01(static_cast<int>(k), 31, s.seed));
                }
                {
                    const double sp2 = sp * 1.1;
                    const double u = x - y + drift * 1.3 + 4096 * sp2;
                    const double k = std::floor(u / sp2);
                    if (u - k * sp2 < 1.0 && noise(x / 8.0, y / 8.0, s.seed + 17) > .22)
                        c2 = static_cast<uint32_t>(32 + 24 * hash01(static_cast<int>(k), 37, s.seed));
                }
                {
                    const double sp3 = sp * 1.25;
                    const double u = x + 0.18 * y + drift * 0.7 + 4096 * sp3;
                    const double k = std::floor(u / sp3);
                    if (u - k * sp3 < 1.0 && noise(x / 10.0, y / 10.0, s.seed + 23) > .25)
                        c3 = static_cast<uint32_t>(54 + 24 * hash01(static_cast<int>(k), 41, s.seed));
                }
                p->hatch[static_cast<size_t>(y) * width + x] = a | (c2 << 8) | (c3 << 16);
            }
    });
    return p;
}

std::shared_ptr<Pattern> pattern_for(int width, int height, const Settings& s, double spacing, int threads, bool& cached) {
    const int spacing_key = static_cast<int>(std::lround(spacing * 100)), wobble_key = static_cast<int>(std::lround(s.wobble * 100));
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto& list = cache();
        for (auto it = list.begin(); it != list.end(); ++it)
            if ((*it)->width == width && (*it)->height == height && (*it)->seed == s.seed && (*it)->spacing_key == spacing_key && (*it)->wobble_key == wobble_key) {
                auto found = *it;
                if (it != list.begin()) { list.erase(it); list.insert(list.begin(), found); }  // najświeższy na początku
                cached = true;
                return found;
            }
    }
    cached = false;
    auto built = build_pattern(width, height, s, spacing, threads);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto& list = cache();
    list.insert(list.begin(), built);
    if (list.size() > CACHE_LIMIT) list.resize(CACHE_LIMIT);
    return built;
}

}  // namespace

double luminance(uint8_t r, uint8_t g, uint8_t b) { return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0; }

int thread_count(size_t pixels, int requested) {
    if (pixels < PARALLEL_MIN) return 1;
    int n = requested > 0 ? requested : static_cast<int>(std::thread::hardware_concurrency());
    if (n <= 0) n = 1;
    return std::clamp(n, 1, 8);
}

size_t cached_patterns() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    return cache().size();
}

void clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cache().clear();
}

Stats apply(int width, int height, std::vector<uint8_t>& rgba, const std::vector<int32_t>& owner, const std::vector<float>& depth, const Settings& s) {
    const auto started = std::chrono::steady_clock::now();
    if (width <= 0 || height <= 0) throw std::invalid_argument("Filtr ołówka: rozmiar obrazu musi być dodatni.");
    const size_t count = static_cast<size_t>(width) * height;
    if (rgba.size() != count * 4 || owner.size() != count || depth.size() != count)
        throw std::invalid_argument("Filtr ołówka: bufory obrazu, identyfikatorów i głębi mają różne rozmiary.");
    Stats stats;
    // zakres głębi bryły — próg skoku głębi zależy od wielkości sceny
    float low = std::numeric_limits<float>::infinity(), high = -low;
    for (size_t i = 0; i < count; ++i)
        if (owner[i] >= 0 && std::isfinite(depth[i])) { low = std::min(low, depth[i]); high = std::max(high, depth[i]); ++stats.covered; }
    if (stats.covered == 0) { stats.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); return stats; }
    const float jump = std::max(1e-4f, (high - low) * 0.035f);
    const int threads = thread_count(count, s.threads);
    stats.threads_used = threads;
    const double spacing = std::max(2.5, s.spacing);
    std::shared_ptr<Pattern> pattern = pattern_for(width, height, s, spacing, threads, stats.pattern_cached);
    const Pattern& pat = *pattern;

    std::vector<uint8_t> lum(count);
    parallel_rows(height, threads, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y)
            for (size_t i = static_cast<size_t>(y) * width, end = i + width; i < end; ++i) lum[i] = lum8(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    });

    // 1. mapa obrysu: granica elementów, skok głębi i załamanie między ściankami (różnica jasności).
    // Każdy piksel sam sprawdza czterech sąsiadów (bez zapisów w cudze wiersze), więc pasy wierszy można liczyć równolegle.
    std::vector<uint8_t> edge(count, 0);
    std::vector<uint8_t> has_edge(static_cast<size_t>(pat.bw) * pat.bh, 0);  // blok 4×4 zawiera piksel obrysu
    parallel_rows(height, threads, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(y) * width + x;
                const bool here = owner[i] >= 0;
                uint8_t value = 0;
                for (int n = 0; n < 4 && value < 255; ++n) {
                    const int nx = x + (n == 0 ? -1 : n == 1 ? 1 : 0), ny = y + (n == 2 ? -1 : n == 3 ? 1 : 0);
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const size_t j = static_cast<size_t>(ny) * width + nx;
                    const bool there = owner[j] >= 0;
                    if (!here && !there) continue;
                    if (here != there) { if (here) value = 255; continue; }                              // sylwetka: znaczymy piksel bryły
                    if (owner[i] != owner[j]) { if (depth[i] >= depth[j]) value = 255; continue; }      // granica elementów: bliższy piksel
                    if (std::fabs(depth[i] - depth[j]) > jump) { if (depth[i] > depth[j]) value = std::max<uint8_t>(value, 230); continue; }
                    if (std::abs(static_cast<int>(lum[i]) - static_cast<int>(lum[j])) > 11 && lum[i] < lum[j]) value = std::max<uint8_t>(value, 191);  // załamanie: ciemniejsza strona
                }
                edge[i] = value;
                if (value) has_edge[static_cast<size_t>(y / BLOCK) * pat.bw + (x / BLOCK)] = 1;
            }
    });
    // obrys sąsiedniego bloku może zostać przesunięty drżeniem do tego bloku (przesunięcie ≤ 4 px): sąsiedztwo 3×3 bloki
    std::vector<uint8_t> near_edge(has_edge.size(), 0);
    for (int by = 0; by < pat.bh; ++by)
        for (int bx = 0; bx < pat.bw; ++bx) {
            uint8_t any = 0;
            for (int dy = -1; dy <= 1 && !any; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = bx + dx, ny = by + dy;
                    if (nx >= 0 && ny >= 0 && nx < pat.bw && ny < pat.bh && has_edge[static_cast<size_t>(ny) * pat.bw + nx]) { any = 1; break; }
                }
            near_edge[static_cast<size_t>(by) * pat.bw + bx] = any;
        }

    // 2. wyjście: papier + kreskowanie + obrys z drżeniem (zapis w miejscu: piksel czyta tylko własny kolor i mapę obrysu)
    struct Part { size_t edge = 0, hatch = 0; };
    std::vector<Part> parts(static_cast<size_t>(threads));
    const int paper8 = static_cast<int>(std::lround(std::clamp(s.paper, 0.5, 1.0) * 255));
    const bool hatching = s.hatching;
    const float strength = static_cast<float>(s.strength);
    parallel_rows(height, threads, [&](int y0, int y1, int index) {
        Part part;
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(y) * width + x;
                const size_t block = static_cast<size_t>(y / BLOCK) * pat.bw + (x / BLOCK);
                float r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
                const bool solid = owner[i] >= 0;
                const bool near = near_edge[block] != 0;
                if (!solid && !near) continue;  // puste tło z dala od obrysu zostaje bez zmian
                if (solid) {
                    const float grain = (static_cast<int>(pat.paper[static_cast<size_t>(y & 255) * 256 + static_cast<size_t>((x + (y >> 8) * 37) & 255)]) - 128) * (0.035f / 128.f);
                    r *= 1 + grain; g *= 1 + grain; b *= 1 + grain;
                    if (hatching) {
                        const int tone = paper8 - lum[i];
                        if (tone > 10) {
                            const uint32_t packed = pat.hatch[i];
                            float darkness = 0;
                            const int thresholds[3] = {static_cast<int>(packed & 255), static_cast<int>((packed >> 8) & 255), static_cast<int>((packed >> 16) & 255)};
                            const float base[3] = {0.20f, 0.18f, 0.16f};
                            for (int f = 0; f < 3; ++f)
                                if (thresholds[f] < 255 && tone > thresholds[f]) darkness += base[f] + 0.38f * std::min(1.f, static_cast<float>(tone - thresholds[f]) / 26.f);
                            if (darkness > 0) {
                                darkness = std::min(0.8f, darkness) * strength;
                                r += (60 - r) * darkness; g += (60 - g) * darkness; b += (64 - b) * darkness;
                                ++part.hatch;
                            }
                        }
                    }
                }
                // obrys: dwa pociągnięcia o różnym drżeniu (drugie słabsze) — linia nie jest idealnie prosta ani jednolita
                const int8_t* j = &pat.jitter[block * 4];
                if (near) {
                auto sample = [&](int dx, int dy) -> int {
                    const int sx = x + dx, sy = y + dy;
                    return (sx < 0 || sy < 0 || sx >= width || sy >= height) ? 0 : edge[static_cast<size_t>(sy) * width + sx];
                };
                const int first = sample(j[0], j[1]);
                const int second = (sample(j[2], j[3]) * 141) >> 8;  // 0,55 · wartość
                const int line = std::max(first, second);
                if (line > 0) {
                    const float alpha = std::min(1.f, line / 255.f * (pat.pressure[block] / 255.f) * strength);
                    r += (52 - r) * alpha; g += (52 - g) * alpha; b += (57 - b) * alpha;
                    if (alpha > 0.35f) ++part.edge;
                    if (!solid && rgba[i * 4 + 3] < 255 * alpha) rgba[i * 4 + 3] = static_cast<uint8_t>(std::min(255.f, 255 * alpha));  // linia wystaje poza sylwetkę
                }
                }
                rgba[i * 4] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.f, 255.f)));
                rgba[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.f, 255.f)));
                rgba[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.f, 255.f)));
            }
        parts[static_cast<size_t>(index)] = part;
    });
    for (const auto& part : parts) { stats.edge_pixels += part.edge; stats.hatch_pixels += part.hatch; }
    stats.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return stats;
}

}  // namespace pencil_filter
