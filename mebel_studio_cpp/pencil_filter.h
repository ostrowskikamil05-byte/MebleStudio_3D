// Filtr „szkic ołówkiem” dla obrazu widoku 3D: grafitowy obrys z drżeniem ręki, podwójny pociągnięty kontur,
// kreskowanie cieni (od pojedynczych kresek po krzyżowe i gęste) oraz delikatne ziarno papieru.
//
// Czysta matematyka bez Tk, VTK i OpenCASCADE: działa na buforach obrazu (RGBA), identyfikatorów elementów i głębi, które
// renderer CPU i tak ma. Wynik zależy tylko od danych wejściowych i ziarna, więc jest powtarzalny (klatka nie „migocze”).
//
// Wydajność: wzór kresek i drżenie linii są liczone raz dla danego rozmiaru obrazu (pamięć podręczna wzorów), a każda klatka to
// tylko odczyty z tablic. Obraz jest dzielony na pasy wierszy liczone równolegle w kilku wątkach; wynik nie zależy od liczby wątków.
// Kreski pojawiają się tylko w cieniu: ścianki jasne (oświetlone) zostają czystym papierem, a im ciemniej, tym więcej rodzin kresek
// (45°, krzyżowe, pionowe) i tym są ciemniejsze.
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
    double paper = 0.93;      // jasność papieru 0…1 — punkt odniesienia: kreskowane jest tylko to, co ciemniejsze
    int threads = 0;          // liczba wątków (0 = automatycznie, 1 = bez wątków)
};

struct Stats {
    size_t covered = 0;       // piksele bryły
    size_t edge_pixels = 0;   // piksele obrysu po nałożeniu
    size_t hatch_pixels = 0;  // piksele kreskowania
    int threads_used = 1;     // ile wątków policzyło obraz
    bool pattern_cached = false;  // wzór kresek i drżenia wzięty z pamięci podręcznej
    double milliseconds = 0;  // czas całego filtra
};

// Modyfikuje `rgba` (width × height × 4, wiersze od góry). `owner` < 0 oznacza puste tło (zostaje bez zmian, z alfa jak było).
// `depth`: większa wartość = bliżej widza (jak w depth_raster). Rozmiary buforów muszą się zgadzać, inaczej std::invalid_argument.
Stats apply(int width, int height, std::vector<uint8_t>& rgba, const std::vector<int32_t>& owner, const std::vector<float>& depth, const Settings& settings = {});

// Jasność koloru 0…1 (do wyboru gęstości kreskowania).
double luminance(uint8_t r, uint8_t g, uint8_t b);

// Liczba wątków użyta dla obrazu o podanej liczbie pikseli i żądanej liczbie wątków (0 = auto). Małe obrazy liczone są w jednym wątku.
int thread_count(size_t pixels, int requested);

// Pamięć podręczna wzorów: liczba wpisów i czyszczenie (do testów i do zwolnienia pamięci po zmianie rozmiaru okna).
size_t cached_patterns();
void clear_cache();

}  // namespace pencil_filter
