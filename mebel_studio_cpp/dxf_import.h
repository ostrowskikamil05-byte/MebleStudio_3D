// Import konturów z plików DXF (tekstowych): LINE, LWPOLYLINE, POLYLINE, ARC, CIRCLE.
//
// Czysta logika bez Tk i OpenCASCADE. Łuki i okręgi są zamieniane na łamane, rozłączne odcinki łączone w kontury,
// jednostki przeliczane na mm ($INSUNITS). Niewspierane obiekty (SPLINE, ELLIPSE, TEXT, HATCH, INSERT …) są
// pomijane z ostrzeżeniem — nic nie znika po cichu.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace dxf_import {

using Point = std::array<double, 2>;

struct Contour {
    std::vector<Point> points;
    bool closed = false;
    bool is_circle = false;      // pełny okrąg (CIRCLE) — kandydat na otwór
    Point center{0, 0};
    double radius = 0;
    double area = 0;             // |pole| dla konturów zamkniętych
};

struct Result {
    std::vector<Contour> contours;
    std::vector<std::string> warnings;
    double unit_scale = 1;       // mnożnik do mm
    std::string unit_name = "mm";
    size_t entities = 0;         // wczytane obiekty rysunkowe (obsługiwane)
    size_t skipped = 0;          // pominięte obiekty
};

// Wczytuje tekst DXF. Zgłasza std::invalid_argument, gdy plik jest binarny, uszkodzony albo bez obsługiwanych obiektów.
Result parse(const std::string& text);

struct Outline {
    std::vector<Point> points;   // przesunięte tak, by min = (0, 0), orientacja przeciwna do ruchu wskazówek
    size_t original_points = 0;  // przed uproszczeniem
    bool simplified = false;
    double width = 0, height = 0;
    size_t inner_contours = 0;   // zamknięte kontury wewnątrz obrysu (otwory) — niewczytane do szkicu
    size_t inner_circles = 0;    // w tym okręgi
    size_t outer_candidates = 0; // zamknięte kontury poza obrysem (ignorowane)
    size_t open_paths = 0;       // niezamknięte łamane (ignorowane)
};

// Wybiera obrys: największy zamknięty kontur. Zgłasza std::invalid_argument, gdy nie ma żadnego zamkniętego.
Outline pick_outline(const Result& result, size_t max_points = 128);

}  // namespace dxf_import
