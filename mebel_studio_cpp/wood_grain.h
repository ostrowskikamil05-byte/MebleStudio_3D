// Usłojenie drewna: parametry losowe na każdą płytę (ziarno z identyfikatora elementu), kod shadera GPU
// i rozkład linii słojów dla rendera CPU.
//
// Czysta matematyka bez Tk, VTK i OpenCASCADE — da się przetestować poza aplikacją.
// Dwie płyty z tego samego materiału mają inne słoje: inne położenie „osi pnia”, inny odcień, gęstość i pochylenie słojów,
// a słoje biegną wzdłuż włókien elementu (Part::grain / grain_v), nie wzdłuż osi świata.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core.h"

namespace wood_grain {

// Parametry jednej płyty. Wszystkie wartości mieszczą się w stałych, sprawdzanych testem granicach.
struct Params {
    double axis_across;   // położenie osi pnia względem płyty, w poprzek włókien [mm], −260 … 260
    double axis_depth;    // głębokość osi pnia pod powierzchnią [mm], 14 … 140 (mała = wyraźne „katedry”)
    double tilt;          // pochylenie osi pnia względem włókien (tangens), −0.09 … 0.09
    double ring_width;    // średnia szerokość rocznego przyrostu [mm], 7 … 18
    double tone;          // jasność deski, −1 … 1 (mnożnik ±9%)
    double warm;          // przesunięcie odcienia chłodny … ciepły, −1 … 1
    double contrast;      // siła ciemnych słojów (drewno późne), 0.55 … 1.35
    double fibre;         // siła drobnych włókien i porów, 0.5 … 1.4
};

uint64_t hash(const std::string& text);
Params params_for(const std::string& id);

// Dane przekazywane do shadera GPU jako cztery atrybuty wierzchołka (po 4 liczby).
struct VertexData {
    std::array<float, 4> seed;   // 0…1: oś (w poprzek), głębokość osi, pochylenie, szerokość słoja
    std::array<float, 4> dir;    // jednostkowy kierunek włókien w świecie + ton deski (−1…1)
    std::array<float, 4> origin; // początek elementu w świecie + ciepło (−1…1)
    std::array<float, 4> extra;  // kontrast ciemnych słojów, siła włókien, odmiana (0…1), zapas
};
// part == nullptr (np. podłoga z desek): kierunek wzdłuż X, początek w (0,0,0).
VertexData vertex_data(const core::Part* part, const std::string& id);
// Kierunek włókien elementu w świecie (wektor jednostkowy).
std::array<double, 3> grain_direction(const core::Part& part);

// Kod GLSL wstawiany w shader VTK (deklaracje i obliczenia). Wynik mnoży kolor wyliczony przez oświetlenie.
struct Shader {
    std::string vertex_declarations, vertex_code, fragment_declarations, fragment_code;
};
Shader shader_code();

// Rendering CPU: ułamki (0,1) położenia kolejnych linii słojów w poprzek włókien — nierównomierne i różne dla każdej płyty.
std::vector<double> line_fractions(const std::string& id, int count);
// Udział koloru linii słoja w kolorze płyty (0.08 … 0.19), inny dla każdej płyty.
double line_mix(const std::string& id);
// Liczba linii słojów dla płyty (4 … 8).
int line_count(const std::string& id);

}  // namespace wood_grain
