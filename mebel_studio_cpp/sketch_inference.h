// Silnik wskazówek szkicu : co kursor „widzi” pod myszą.
//
// Czysta geometria 2D bez Tk i OpenCASCADE — dzięki temu jest testowana osobno.
// Kolejność ważności: punkt końcowy › środek › przecięcie › środek okręgu › na krawędzi ›
// kierunki od ostatniego punktu (oś U/V, prostopadle, równolegle, przedłużenie krawędzi) › „od punktu” (wyrównanie
// do współrzędnej innego wierzchołka) › siatka. Shift blokuje ostatnio wykryty kierunek.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace sketch_inference {

using Point = std::array<double, 2>;
struct Segment { Point a, b; };

enum class Kind {
    None, Grid, Endpoint, Midpoint, Intersection, Center, OnEdge,
    AxisU, AxisV, Perpendicular, Parallel, ExtendEdge, FromPoint, Locked
};

// Linia przerywana pokazywana w widoku (np. wyrównanie do wierzchołka).
struct Guide { Point from, to; std::string color; };

struct Input {
    Point raw{0, 0};                       // pozycja kursora w mm (przed przyciąganiem)
    double tolerance = 5;                  // promień łapania w mm (piksele / skala)
    std::vector<Point> points;             // aktualny kontur
    bool closed = false;
    std::vector<Segment> reference;        // geometria mebla (linie pomocnicze)
    std::vector<Point> centers;            // środki okręgów / wieloboków
    std::optional<Point> anchor;           // ostatni punkt (początek rysowanego odcinka)
    std::optional<Point> lock_direction;   // wektor jednostkowy zablokowanego kierunku od punktu anchor
    bool snap_points = true;               // punkty / środki / krawędzie / kierunki
    bool grid = false;
    double grid_step = 10;
    bool ortho = false;                    // wymuś poziomo/pionowo względem anchor
};

struct Result {
    Point point{0, 0};
    Kind kind = Kind::None;
    std::string tip;                       // podpowiedź przy kursorze
    std::string color;                     // kolor znacznika / gumki
    std::vector<Guide> guides;
    std::optional<Point> direction;        // jednostkowy kierunek od anchor do point (dla blokady Shift)
};

Result infer(const Input& input);

std::string tip_for(Kind kind);
std::string color_for(Kind kind);
// Czy rodzaj wskazówki opisuje kierunek (można go zablokować Shiftem / strzałką).
bool is_direction(Kind kind);
// Punkt wskazany przez wskazówkę (znacznik rysowany jako kwadrat) — nie dotyczy siatki ani kierunków.
bool is_point_inference(Kind kind);

}  // namespace sketch_inference
