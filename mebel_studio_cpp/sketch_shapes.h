// Gotowe kształty i operacje na konturze szkicu (okrąg, wielobok, łuk, profile L/U/T, zaokrąglenia, uproszczenie).
//
// Czysta geometria 2D bez Tk i OpenCASCADE. Błędy użytkownika zgłaszane jako std::invalid_argument (= py::ValueError).
// Kontur w programie jest łamaną, więc łuki są przybliżane odcinkami (limit edytora: 128 punktów).
#pragma once
#include <array>
#include <string>
#include <vector>

namespace sketch_shapes {

using Point = std::array<double, 2>;
constexpr size_t MAX_POINTS = 128;

std::vector<Point> circle(const Point& center, double radius, int segments = 48);
// Wielobok foremny wpisany w okrąg o promieniu `radius`; pierwszy wierzchołek pod kątem `start_degrees`.
std::vector<Point> regular_polygon(const Point& center, double radius, int sides, double start_degrees = 0);
// Łuk od `from_radians` do `to_radians` (przeciwnie do ruchu wskazówek, gdy to > from), obie końcówki włącznie.
std::vector<Point> arc(const Point& center, double radius, double from_radians, double to_radians, int segments_per_quarter = 8);
// Łuk przez trzy punkty: początek a, punkt pośredni b, koniec c (włącznie z a i c).
std::vector<Point> arc_three_points(const Point& a, const Point& b, const Point& c, int segments_per_quarter = 8);
// Prostokąt o przeciwległych narożnikach a, b z zaokrąglonymi rogami.
std::vector<Point> rounded_rectangle(const Point& a, const Point& b, double radius, int segments_per_corner = 6);

struct ProfileInfo {
    std::string name;         // nazwa na liście, np. "Kształt L"
    std::string param_label;  // etykieta trzeciego parametru ("" = brak)
    double default_param;     // wartość domyślna parametru
};
const std::vector<ProfileInfo>& profile_catalog();
// Profil z katalogu o szerokości `width` i wysokości `height` (mm), lewy dolny róg w (0,0).
std::vector<Point> profile(const std::string& name, double width, double height, double param);

// Zaokrągla narożnik `index` łukiem o promieniu `radius` (styczny do obu sąsiednich odcinków).
std::vector<Point> fillet_corner(const std::vector<Point>& points, size_t index, double radius, bool closed = true, int segments_per_quarter = 8);
// Ścina narożnik `index` na odległość `distance` od wierzchołka po obu bokach.
std::vector<Point> chamfer_corner(const std::vector<Point>& points, size_t index, double distance, bool closed = true);
// Usuwa punkt `index`; kontur zamknięty musi zachować ≥ 3 punkty, otwarty ≥ 1.
std::vector<Point> remove_point(const std::vector<Point>& points, size_t index, bool closed);
// Upraszcza łamaną (Douglas–Peucker, kontur zamknięty) tak, by miała najwyżej `max_points` punktów.
std::vector<Point> simplify(const std::vector<Point>& points, size_t max_points = MAX_POINTS);

// Wartości wpisane z klawiatury (jak pole VCB w SketchUpie): „250”, „600x400”, „600;400”, „12,5”, „250<45”, „30cm”.
// Separatorem pól jest ; spacja lub x; przecinek jest dziesiętny, chyba że `comma_separates` (prostokąt) i nie ma innego separatora.
// Po „<” stoi kąt w stopniach. Jednostki: mm (domyślnie), cm, m. Śmieci zgłaszane jako std::invalid_argument.
struct VcbValues {
    std::vector<double> numbers;
    bool has_angle = false;
    double angle = 0;
};
VcbValues parse_vcb(const std::string& text, bool comma_separates = false);

double signed_area(const std::vector<Point>& points);
// Czy łamana zamknięta przecina samą siebie (nie licząc sąsiednich odcinków).
bool self_intersects(const std::vector<Point>& points);

}  // namespace sketch_shapes
