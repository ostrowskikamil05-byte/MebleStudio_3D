// Kanty i wzorki projektowane w szkicowniku: obróbka krawędzi (zaokrąglenie, fazka) i wzory frezowane na płycie
// (ryfle, ramka, romby, perforacja, szczeliny, paski), a także dobór pasujących elementów mebla.
//
// Czysta logika bez Tk i OpenCASCADE — wynik to zwykłe tablice JSON w formacie Part::fillets / Part::chamfers / Part::machining,
// więc działa z istniejącym CAD-em, rozkrojem i eksportami. Błędy użytkownika zgłaszane jako std::invalid_argument (= py::ValueError).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core.h"

namespace sketch_design {

using json = core::json;
using Point = core::Point;

constexpr size_t MAX_PATTERN_CUTS = 200;  // tyle frezów dopuszcza Part::validate na jeden element

// ------------------------------------------------------------------ kanty (obróbka krawędzi)

struct EdgeStyleInfo {
    std::string id;           // "prosta" | "zaokraglenie" | "fazka"
    std::string name;         // nazwa na liście
    std::string description;  // opis pod listą
    bool has_size;            // czy ma wymiar (promień / odległość fazy)
};
const std::vector<EdgeStyleInfo>& edge_styles();
// Znajduje styl po identyfikatorze albo nazwie; nieznany → std::invalid_argument.
const EdgeStyleInfo& edge_style(const std::string& id_or_name);

// Miniaturka przekroju krawędzi: zamknięty wielokąt we współrzędnych płótna (y w dół) w polu width × height.
std::vector<Point> edge_section_thumbnail(const std::string& style, double width, double height, double margin = 6);

// Krawędzie górna i dolna odcinka `index` konturu (identyfikatory jak w cad_geometry::edge_specs dla zwykłego wielokąta).
std::vector<std::string> edge_ids_for_segment(size_t index);
// Górne i dolne krawędzie wszystkich `count` odcinków konturu.
std::vector<std::string> edge_ids_all(size_t count);

struct EdgeChange {
    json fillets = json::array();
    json chamfers = json::array();
    size_t touched = 0;
};
// Ustawia styl na podanych krawędziach: zaokrąglenie i fazka wzajemnie się wykluczają na tej samej krawędzi,
// „prosta” zdejmuje obróbkę. Wartość musi mieścić się w (0,1 … grubość/2) mm.
EdgeChange apply_edge_style(const json& fillets, const json& chamfers, const std::string& style, double size, const std::vector<std::string>& ids,
                            double thickness);
// Opis do dziennika i paska stanu, np. „zaokrąglenie R5 mm na 8 krawędziach”.
std::string describe_edge_style(const std::string& style, double size, size_t count);

// ------------------------------------------------------------------ wzorki frezowane

struct PatternParam {
    std::string key, label;
    double def, min, max;
};
struct PatternInfo {
    std::string id, name, description;
    std::vector<PatternParam> params;
};
const std::vector<PatternInfo>& pattern_catalog();
const PatternInfo& pattern_info(const std::string& id_or_name);
std::map<std::string, double> default_params(const std::string& id);

struct PatternResult {
    std::vector<json> cuts;  // wpisy w formacie Part::machining, oznaczone generated_decor = "wzorek:<id>"
    size_t skipped = 0;      // frezy, które nie mieszczą się w konturze
};
// Wzorek na płycie o wymiarach u × v i grubości t. `contour` puste = prostokąt. Nieznany klucz parametru, wartość poza zakresem
// albo wzór niemieszczący się w ogóle → std::invalid_argument (brak cichego pomijania).
PatternResult generate_pattern(const std::string& id, const std::vector<Point>& contour, double u, double v, double thickness,
                               const std::map<std::string, double>& params = {}, const std::string& face = "top");

bool is_pattern_cut(const json& cut);
std::string pattern_of(const json& cut);                                  // identyfikator wzorka albo ""
size_t count_patterns(const json& machining, const std::string& id = "");  // liczba frezów danego (albo wszystkich) wzorków
// Zwraca obróbkę bez frezów wzorka `id` (pusty = wszystkich wzorków); ręczne obróbki zostają.
json remove_patterns(const json& machining, const std::string& id = "");
// Zastępuje frezy wzorka `id` nowymi (ręczne i inne wzorki zostają).
json merge_pattern(const json& machining, const std::string& id, const std::vector<json>& cuts);

// Obrys frezu w układzie części (okrąg, podłużny, prostokąt — po obrocie).
std::vector<Point> cut_outline(const json& cut, int segments = 20);
// Miniaturka wzorka na próbnej płycie: [0] = obrys płyty, dalej frezy; współrzędne płótna (y w dół) w polu width × height.
std::vector<std::vector<Point>> pattern_thumbnail(const std::string& id, double width, double height, double margin = 6);

// ------------------------------------------------------------------ pasujące elementy mebla

// Elementy tego samego mebla, o tej samej funkcji (poza „other”), wymiarach, konturze i profilu co `model` — im można
// bezpiecznie przekazać kanty i wzorki. `role_of` zwraca funkcję elementu (assembly::inferred_role).
std::vector<std::string> matching_parts(const core::Project& project, const core::Part& model,
                                        const std::function<std::string(const core::Part&)>& role_of);

}  // namespace sketch_design
