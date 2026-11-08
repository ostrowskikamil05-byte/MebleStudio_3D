// Geometria „Pracowni 3D” — własnego modelera mebli w stylu bezpośredniego modelowania (rysuj prostokąt na ścianie lub podłodze,
// wyciągaj, przesuwaj, kopiuj). Cała logika jest czysta (bez Tk i OpenCASCADE), więc da się ją sprawdzić poza aplikacją.
//
// Każda bryła zrysowana w pracowni od razu staje się płytą (core::Part): najcieńszy wymiar bryły to grubość płyty, a płaszczyzna
// płyty (XY / XZ / YZ) wynika z kierunku grubości. Kamera programu jest rzutem równoległym, więc rzut ekranu → promień jest
// prosty i jednoznaczny.
//
// Błędy użytkownika zgłaszane są jako std::invalid_argument (= py::ValueError), z powodem.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core.h"

namespace modeler_geometry {

using Vec3 = core::Point3;
using Point = core::Point;

// ------------------------------------------------------------------ wektory

Vec3 add(const Vec3& a, const Vec3& b);
Vec3 sub(const Vec3& a, const Vec3& b);
Vec3 scale(const Vec3& a, double k);
double dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
double length(const Vec3& a);
Vec3 unit(const Vec3& a);  // wektor zerowy → std::invalid_argument

// ------------------------------------------------------------------ promień z ekranu

struct Ray {
    Vec3 origin;  // punkt daleko za sceną (najmniejsza głębokość)
    Vec3 dir;     // jednostkowy, w stronę patrzącego
};
// Odwrotność core::Camera::project dla rzutu równoległego. Punkt o większym `t` jest bliżej patrzącego.
Ray unproject(const core::Camera& camera, double sx, double sy, double width, double height);
Vec3 at(const Ray& ray, double t);

// ------------------------------------------------------------------ płaszczyzny robocze

struct Plane {
    Vec3 origin{0, 0, 0};
    Vec3 u{1, 0, 0}, v{0, 1, 0}, n{0, 0, 1};  // osie w płaszczyźnie i normalna (jednostkowe, wzajemnie prostopadłe)
    std::string label;                         // „Podłoga”, „Przód”, „Bok”, „Ściana: Bok lewy / góra” …
};
Plane ground_plane(double z = 0);
Plane front_plane(double y = 0);  // normalna −Y (patrzący z przodu)
Plane side_plane(double x = 0);   // normalna +X
// Punkt przecięcia promienia z płaszczyzną: parametr t albo nullopt, gdy promień jest (prawie) równoległy.
std::optional<double> ray_plane(const Ray& ray, const Plane& plane);
// Współrzędne punktu w płaszczyźnie (a wzdłuż u, b wzdłuż v) i odwrotnie.
Point to_plane(const Plane& plane, const Vec3& point);
Vec3 from_plane(const Plane& plane, double a, double b);

// ------------------------------------------------------------------ płyty w świecie

bool axis_aligned(const core::Part& part);  // brak obrotów i brak obrotu układu mebla (przesunięcie dozwolone)
struct Box {
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
};
Box part_box(const core::Part& part);       // obwiednia płyty w świecie (dla płyt wyrównanych do osi)
Vec3 box_size(const Box& box);
// Płyta z bryły: najcieńszy wymiar = grubość (przy remisie Z, potem Y, potem X). Wymiary < 0,5 mm albo > 6000 mm → błąd.
core::Part board_from_box(const Box& box, const std::string& name, const std::string& group, const std::string& material);
// Płyta z wielokąta narysowanego na płaszczyźnie osiowej i wyciągniętego o `thickness` (znak = kierunek względem normalnej).
core::Part board_from_polygon(const Plane& plane, const std::vector<Vec3>& points, double thickness, const std::string& name,
                              const std::string& group, const std::string& material);
// Bryła z prostokąta na płaszczyźnie (rogi P0, P1 leżą w płaszczyźnie) wyciągniętego o `depth` wzdłuż normalnej (znak = kierunek).
Box box_from_rectangle(const Plane& plane, const Vec3& p0, const Vec3& p1, double depth);

// ------------------------------------------------------------------ ściany do rysowania i wyciągania

struct FaceHit {
    size_t part = 0;        // indeks w Project::parts
    std::string face;       // „top” | „bottom” | „u0” | „u1” | „v0” | „v1” (płyty prostokątne) albo „edge:N” (wielokąt)
    Plane plane;            // płaszczyzna ściany z normalną skierowaną na zewnątrz
    Vec3 point{0, 0, 0};    // punkt trafienia
    double t = 0;           // parametr na promieniu
};
// Ściany płyty w świecie jako wielokąty z normalną na zewnątrz.
struct Face {
    std::string id;
    Plane plane;
    std::vector<Vec3> polygon;
};
std::vector<Face> part_faces(const core::Part& part);
// Najbliższa ściana pod kursorem; płyty z `skip` (np. przeciągana) i niewidoczne są pomijane.
std::optional<FaceHit> pick_face(const core::Project& project, const core::Camera& camera, double sx, double sy, double width, double height,
                                 const std::set<std::string>& skip = {});

// ------------------------------------------------------------------ edycja płyt

// Przesuwa ścianę płyty prostokątnej o `distance` wzdłuż jej normalnej (dodatnia = na zewnątrz). Dla „top”/„bottom” zmienia grubość,
// dla „u0/u1/v0/v1” wymiar U lub V. Płyta obrócona, z konturem niestandardowym, otworami albo obróbką (przy zmianie U/V) → błąd z powodem.
core::Part pull_face(const core::Part& part, const std::string& face, double distance);
core::Part moved(const core::Part& part, const Vec3& delta);
// Kopie przesunięte o delta, 2·delta, … (count ≥ 1); każda ma nowy identyfikator i własną nazwę.
std::vector<core::Part> array_copies(const core::Part& part, const Vec3& delta, int count);

// ------------------------------------------------------------------ przyciąganie

struct Snap {
    Vec3 point{0, 0, 0};
    std::string label;  // „koniec”, „środek krawędzi”, „środek ściany”, „oś X” …; puste = brak przyciągnięcia
    bool snapped = false;
};
// Punkty charakterystyczne płyt (narożniki, środki krawędzi, środki ścian) najbliższe kursorowi w promieniu `px` pikseli ekranu.
std::optional<Snap> snap_to_points(const core::Project& project, const core::Camera& camera, double sx, double sy, double width, double height,
                                   double px = 10);
// Przyciągnięcie do osi od punktu `anchor` (kierunek prawie równoległy do osi X/Y/Z, tolerancja ok. 2°) oraz do siatki `grid` mm.
Snap snap_to_axes(const Vec3& anchor, const Vec3& point, double grid = 1);
// Wyciąganie: odległość wyrównywana do ścian innych płyt leżących na tej samej osi (w granicach `tolerance` mm).
double snap_pull(const core::Project& project, size_t part, const std::string& face, double distance, double tolerance, std::string* label = nullptr);
// Wyrównanie współrzędnej `proposed` na osi `axis` do ścian płyt (i podłogi 0) w granicach `tolerance` mm; `skip` = pomijana płyta.
// Zwraca wartość po wyrównaniu (bez wyrównania: zaokrąglenie do 1 mm).
double snap_coordinate(const core::Project& project, int axis, double proposed, double tolerance, std::optional<size_t> skip = std::nullopt,
                       std::string* label = nullptr);
// Ile mm odpowiada przesunięciu kursora o (dx, dy) px wzdłuż normalnej `n` w punkcie `p` (rzut równoległy).
double pull_distance(const core::Camera& camera, const Vec3& p, const Vec3& n, double dx_px, double dy_px);

// ------------------------------------------------------------------ wpisywanie wymiarów

// „600x400”, „600;400”, „600 400” → {600, 400}; przecinek to przecinek dziesiętny. Pusty tekst lub nie-liczba → błąd.
std::vector<double> parse_numbers(const std::string& text);

// Nazwa nowej płyty wynikająca z jej położenia: grubość wzdłuż X → „Bok”, wzdłuż Z → „Poziom”, wzdłuż Y → „Pion”;
// kolejne dostają numer, jeśli nazwa już istnieje.
std::string suggest_name(const core::Part& part, const std::vector<std::string>& existing);

// Opis do dziennika, np. „płyta 18 × 560 × 720 mm (YZ) w (0, 0, 0)”.
std::string describe(const core::Part& part);

// ------------------------------------------------------------------ profile 2D (CAD) → płyty
// Wycięcie przelotowe w płycie opisane we współrzędnych płaszczyzny rysowania (a wzdłuż u, b wzdłuż v).
struct Cutout {
    std::string shape;   // „circle” (width = średnica), „rect” (length wzdłuż u, width wzdłuż v), „slot” (length = całkowita długość, kąt w płaszczyźnie)
    Point center{0, 0};
    double length = 0, width = 0, angle = 0;   // kąt w stopniach (tylko szczelina)
};
// Płyta o dowolnym konturze (np. z łukami przybliżonymi odcinkami) z wycięciami przelotowymi (otwory, prostokąty, szczeliny).
core::Part board_from_profile(const Plane& plane, const std::vector<Vec3>& outer, const std::vector<Cutout>& cutouts, double thickness, const std::string& name,
                              const std::string& group, const std::string& material);
// Płyta po zmianie obrysu i wycięć: zachowuje wszystko poza konturem (nazwa, materiał, grupa, okucia, pozostała obróbka); początek płyty jest
// przesuwany, a otwory, obróbka i operacje przeliczane. Kanty (zaokrąglenia/fazy) są zerowane, bo numery krawędzi przestają pasować.
// `outer` leży na górnej ścianie płyty (płaszczyzna `top_plane` z part_faces, id „top”).
core::Part reshape_part(const core::Part& part, const Plane& top_plane, const std::vector<Vec3>& outer, const std::vector<Cutout>& cutouts);
// Wycięcia przelotowe płyty (obróbka typu „cutout”) jako kształty we współrzędnych `top_plane` — do wczytania w edytorze 2D.
std::vector<Cutout> cutouts_of(const core::Part& part, const Plane& top_plane);
// Kąty kamery (yaw, pitch) dla widoku wprost na płaszczyznę z tej strony, z której oś u biegnie w prawo, a v w górę ekranu (bez odbicia lustrzanego).
std::pair<double, double> camera_angles_for_plane(const Plane& plane);
// Płaszczyzna górnej lub dolnej ściany płyty (id „top” / „bottom”).
Plane face_plane(const core::Part& part, const std::string& face);

}  // namespace modeler_geometry
