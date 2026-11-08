// Tryby grafiki widoku 3D (klasyczny / realistyczny): cieniowanie ścianek, miękkie cienie rzucane na podłogę, płytki podłogi.
//
// Czysta matematyka bez Tk, VTK i OpenCASCADE — dzięki temu da się ją przetestować poza aplikacją.
// Sam wybór trybu (stan, zapis do pliku, panel) jest w view_style.h; tu są tylko obliczenia.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace render_style {

using Point2 = std::array<double, 2>;
using Point3 = std::array<double, 3>;

enum class Mode { Classic, Realistic, Sketch };  // Sketch = „szkic ołówkiem”: papier, grafitowy obrys, kreskowanie

struct ModeInfo {
    Mode mode;
    std::string id;           // zapis w pliku ustawień
    std::string name;         // nazwa w interfejsie
    std::string description;  // opis pod przełącznikiem
};
const std::vector<ModeInfo>& modes();
// Tryb po identyfikatorze albo nazwie; nieznany → std::invalid_argument.
Mode mode_from(const std::string& id_or_name);
const ModeInfo& info(Mode mode);

// ------------------------------------------------------------------ cieniowanie ścianek

struct Shading {
    double factor;  // mnożnik jasności koloru
    double warm;    // -1 (chłodny) … +1 (ciepły): przesunięcie czerwień/niebieski
};
// Klasyczne: stałe światło otoczenia + słabe światło kierunkowe (jak dotąd).
// Realistyczne: półkulowe światło otoczenia (niebo/podłoga), światło główne z rozmyciem Lamberta, światło wypełniające
// i delikatna krawędź kontrastu — ścianki o różnej orientacji wyraźnie się od siebie różnią.
Shading shade_face(Mode mode, const Point3& normal, double brightness, double ambient);
std::array<int, 3> apply_shading(const std::array<int, 3>& base, const Shading& shading);
// Szkic ołówkiem: łagodne cieniowanie 0,70 … 1,0 (zamiast mocnego kontrastu) — resztę robi kreskowanie z pencil_filter.
// Kolor materiału zostaje tylko jako delikatny odcień papieru (ok. 22%), więc o ciemności decyduje oświetlenie ścianki.
std::array<int, 3> sketch_tone(const std::array<int, 3>& base, double factor);
// Kolor papieru szkicu (#rrggbb).
const char* paper_color();

// ------------------------------------------------------------------ cienie na podłodze

struct ShadowLayer {
    std::vector<Point2> polygon;  // wielokąt wypukły na podłodze (współrzędne świata, XY)
    double strength;              // 0 … 1: udział koloru cienia po nałożeniu tej warstwy
};
struct ShadowSettings {
    int layers = 6;               // liczba warstw półcienia
    double spread = 90;           // rozmycie [mm] przy kontakcie z podłogą
    double max_strength = 0.62;   // siła cienia przy samym meblu
    double offset_per_height_x = 0.30;  // przesunięcie cienia w X na każdy mm wysokości (kierunek światła głównego)
    double offset_per_height_y = 0.40;
};
// Obwiednia wypukła punktów (Andrew); dla < 3 różnych punktów zwraca je bez zmian.
std::vector<Point2> convex_hull(std::vector<Point2> points);
// Przesunięcie wielokąta wypukłego o `distance` z zaokrąglonymi narożnikami (suma Minkowskiego z kołem).
std::vector<Point2> offset_convex(const std::vector<Point2>& hull, double distance, int arc_steps = 4);
// Cień mebla o stopach na wysokości `bottom_z` i szczycie `top_z` nad podłogą (obie ≥ 0): rzut wzdłuż światła + półcień.
// Warstwy zwracane od najbardziej zewnętrznej (najjaśniejszej) do wewnętrznej (najciemniejszej). Mebel wiszący wysoko ma
// cień słabszy i bardziej rozmyty niż mebel stojący na podłodze.
std::vector<ShadowLayer> cast_shadow(const std::vector<Point2>& footprint, double bottom_z, double top_z, const ShadowSettings& settings = {});
// Kolor po nałożeniu cienia o danej sile: mieszanie koloru podłogi z kolorem cienia (#rrggbb).
std::string blend_hex(const std::string& base, const std::string& over, double amount);

// ------------------------------------------------------------------ płytki podłogi

struct Segment3 {
    Point3 a, b;
};
// Linie fug płytek kwadratowych o boku `tile` mm wokół środka (cx, cy), nie więcej niż `max_lines` na oś.
std::vector<Segment3> floor_tile_lines(double cx, double cy, double extent, double tile, double z, int max_lines = 24);

// ------------------------------------------------------------------ miniatura trybu

struct ThumbPoly {
    std::vector<Point2> points;
    std::string fill;   // #rrggbb (dla linii: kolor linii)
    bool line = false;  // true = łamana (fuga płytek), false = wypełniony wielokąt
    bool body = false;  // true = ścianki bryły mebla (do testów obrysu)
    std::string outline;  // kolor obrysu wielokąta bryły (puste = domyślny brązowy)
};
// Miniatura trybu: sześcienna szafka na podłodze, rysowana tymi samymi funkcjami cieniowania i cieni co widok 3D
// (klasyczny: płaskie ścianki i cień kontaktowy; realistyczny: kontrast ścianek, miękki cień rzucany, płytki).
// Wielokąty w kolejności rysowania (od tyłu), współrzędne płótna (y w dół) w polu width × height.
std::vector<ThumbPoly> mode_thumbnail(Mode mode, double width, double height);
std::string rgb_hex(const std::array<int, 3>& color);

}  // namespace render_style
