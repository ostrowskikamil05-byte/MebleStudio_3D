// Geometria 2D szkicu CAD z prawdziwymi łukami (bez Tk i OpenCASCADE).
//
// Kontur to łamana, w której każdy odcinek może być łukiem: wierzchołek niesie „wybrzuszenie" (bulge = tan(kąt/4), dodatnie = łuk przeciwnie do ruchu
// wskazówek zegara, jak w DXF). Dzięki temu okręgi, szczeliny, zaokrąglone narożniki i łuki styczne są zapisane dokładnie, a do płyty trafia
// dopiero przybliżenie odcinkami (limit konturu w programie: 128 punktów, dobierany tak, by odchyłka łuku była jak najmniejsza).
// Błędy użytkownika zgłaszane są jako std::invalid_argument z powodem po polsku.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace cad2d {

using Point = std::array<double, 2>;
constexpr double PI = 3.14159265358979323846;
constexpr size_t MAX_CONTOUR_POINTS = 128;

struct Vertex {
    Point p{0, 0};
    double bulge = 0;  // odcinek od tego wierzchołka do następnego: 0 = prosty, inaczej tan(kąt łuku / 4)
};

struct Path {
    std::vector<Vertex> v;
    bool closed = false;
    std::string kind = "kontur";  // linia, prostokąt, okrąg, szczelina, wielobok, kontur — tylko opis i rozpoznawanie otworów
};

struct Arc {
    Point center{0, 0};
    double radius = 0;
    double start = 0;  // kąt początku [rad]
    double sweep = 0;  // dodatni = przeciwnie do ruchu wskazówek [rad]
};

struct Segment {
    Point a{0, 0}, b{0, 0};
    double bulge = 0;
};

// --- łuki
Arc arc_of(const Point& a, const Point& b, double bulge);
double bulge_three_points(const Point& a, const Point& through, const Point& c);
double bulge_tangent(const Point& a, const Point& tangent_direction, const Point& b);
double bulge_center(const Point& center, const Point& a, const Point& b, bool counter_clockwise);

// --- odcinki
size_t segment_count(const Path& path);
Segment segment(const Path& path, size_t index);
double segment_length(const Segment& s);                 // długość odcinka albo łuku
Point segment_point(const Segment& s, double t);         // t = 0 … 1 wzdłuż odcinka albo łuku
Point segment_direction(const Segment& s, bool at_end);  // wersor stycznej na początku albo końcu

// --- przybliżenie odcinkami: łuk tnie się tak, by odchyłka od okręgu nie przekraczała `tolerance` mm; gdy punktów byłoby więcej niż `max_points`,
// tolerancja rośnie (do 20 mm); jeśli nadal za dużo — błąd z poleceniem uproszczenia kształtu. Kontur zamknięty nie powtarza pierwszego punktu.
std::vector<Point> tessellate(const Path& path, double tolerance = 0.2, size_t max_points = MAX_CONTOUR_POINTS);

// --- kształty
Path polyline(const std::vector<Point>& points, bool closed);
Path rectangle(const Point& a, const Point& b);
Path rounded_rectangle(const Point& a, const Point& b, double radius);
Path circle(const Point& center, double radius);
Path slot(const Point& a, const Point& b, double width);   // szczelina (owal): dwa półokręgi na końcach odcinka a–b
Path regular_polygon(const Point& center, double radius, int sides, double start_degrees = 0);

// --- edycja
Path fillet_vertex(const Path& path, size_t index, double radius);   // zaokrągla narożnik styczną do obu boków
Path chamfer_vertex(const Path& path, size_t index, double distance);
Path move_vertex(const Path& path, size_t index, const Point& to);
Path remove_vertex(const Path& path, size_t index);
Path set_segment_length(const Path& path, size_t index, double length);   // tylko odcinki proste; przesuwa koniec odcinka
Path translate(const Path& path, const Point& delta);
Path close_path(const Path& path);

// --- pomiary
std::array<Point, 2> bounds(const Path& path);   // {min, max} łącznie z wybrzuszeniem łuków
double area(const Path& path);                    // pole konturu zamkniętego [mm²]
double distance_to_path(const Path& path, const Point& p);
std::optional<size_t> nearest_vertex(const Path& path, const Point& p, double tolerance);
std::optional<size_t> nearest_segment(const Path& path, const Point& p, double tolerance);
std::string describe(const Path& path);

struct CircleInfo { Point center{0, 0}; double radius = 0; };
std::optional<CircleInfo> as_circle(const Path& path);                       // okrąg narysowany narzędziem Okrąg
std::optional<std::array<Point, 2>> as_rectangle(const Path& path);          // prostokąt równoległy do osi {min, max}
struct SlotInfo { Point a{0, 0}, b{0, 0}; double width = 0; };
std::optional<SlotInfo> as_slot(const Path& path);                            // szczelina narysowana narzędziem Szczelina

// --- przyciąganie
struct Snap {
    Point p{0, 0};
    std::string label;  // „koniec", „środek", „środek łuku", „kwadrant"
};
std::vector<Snap> snap_points(const Path& path);
std::optional<Snap> nearest_snap(const std::vector<Path>& paths, const Point& cursor, double tolerance);
Point snap_to_grid(const Point& p, double step);
// Rzut kierunku od `from` do `to` na najbliższą oś, gdy odchyłka jest mniejsza niż `degrees`.
Point snap_to_axis(const Point& from, const Point& to, double degrees = 3);

// --- sprawdzanie i układ kształtów
void validate_profile(const Path& path, const std::string& name = "Kontur");   // zamknięty, bez samoprzecięć, pole ≥ 1 mm²
bool polygon_contains(const std::vector<Point>& outer, const std::vector<Point>& inner);
struct Group {
    size_t outer = 0;
    std::vector<size_t> inner;   // kształty leżące wewnątrz (otwory)
};
// Dzieli kształty na płyty: każdy kształt nie leżący w innym to osobna płyta, kształty w jego wnętrzu to otwory. Kształty, które się przecinają,
// albo zagnieżdżone na dwóch poziomach, są błędem.
std::vector<Group> group_shapes(const std::vector<Path>& shapes);

// Kontur do płyty: przybliżenie odcinkami przesunięte tak, by minimum wypadło w (0,0); `offset` to przesunięcie, które odjęto.
struct Contour {
    std::vector<Point> points;
    Point offset{0, 0};
    double u = 0, v = 0;
};
Contour to_contour(const Path& path, double tolerance = 0.2);

// --- rysowanie odręczne (ołówek)
// Dopasowanie pociągnięcia myszą do czystych odcinków i łuków. `tolerance` [mm] to największa odchyłka dopasowanego kształtu od ręcznie narysowanych punktów.
// Rozpoznaje: okrąg (zamknięte pociągnięcie w kształcie koła), prostokąt równoległy do osi, ciąg odcinków i łuków (otwarty albo zamknięty kontur).
// Pociągnięcie krótsze niż ok. 3 tolerancje albo bez dwóch różnych punktów jest odrzucane (std::invalid_argument).
struct StrokeFit {
    Path path;
    std::string description;   // np. „okrąg R 85”, „2 odcinki + 1 łuk”
    size_t lines = 0, arcs = 0;
};
StrokeFit fit_stroke(const std::vector<Point>& stroke, double tolerance);

}  // namespace cad2d
