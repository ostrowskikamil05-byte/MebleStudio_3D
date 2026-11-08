// Filtr „szkic ołówkiem” dla obrazu widoku 3D: grafitowy obrys z drżeniem ręki, podwójny pociągnięty kontur,
// kreskowanie cieni (pojedyncze i krzyżowe) oraz delikatne ziarno papieru.
//
// Czysta matematyka bez Tk, VTK i OpenCASCADE: działa na buforach obrazu (RGBA), identyfikatorów elementów i głębi, które
// renderer CPU i tak ma. Wynik zależy tylko od danych wejściowych i ziarna, więc jest powtarzalny (klatka nie „migocze”).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pencil_filter {

struct Settings {
    bool hatching = true;     // kreskowanie cieni (wyłączane w klatkach interaktywnych — szybciej)
    double wobble = 1.0;      // siła drżenia linii (0 = linijka)
    double strength = 1.0;    // ciemność ołówka
    uint32_t seed = 20261108; // ziarno drżenia i papieru
    double spacing = 6.0;     // odstęp kresek kreskowania [px obrazu roboczego]
};

struct Stats {
    size_t covered = 0;       // piksele bryły
    size_t edge_pixels = 0;   // piksele obrysu po nałożeniu
    size_t hatch_pixels = 0;  // piksele kreskowania
};

// Modyfikuje `rgba` (width × height × 4, wiersze od góry). `owner` < 0 oznacza puste tło (zostaje bez zmian, z alfa jak było).
// `depth`: większa wartość = bliżej widza (jak w depth_raster). Rozmiary buforów muszą się zgadzać, inaczej std::invalid_argument.
Stats apply(int width, int height, std::vector<uint8_t>& rgba, const std::vector<int32_t>& owner, const std::vector<float>& depth, const Settings& settings = {});

// Jasność koloru 0…1 (do wyboru gęstości kreskowania).
double luminance(uint8_t r, uint8_t g, uint8_t b);

}  // namespace pencil_filter
