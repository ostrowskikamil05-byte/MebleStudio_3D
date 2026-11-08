// Frezy (narzędzia CNC) i operacje frezowania na płycie: dowolnie wiele operacji na jednym elemencie, każda z własnym narzędziem
// (prosty, kulowy, V, zaokrąglający, fazujący), średnicą, głębokością i ścieżką.
//
// Czysta logika bez Tk i OpenCASCADE. Wynik to wpisy w formacie Part::machining (rowek / kieszeń) oznaczone identyfikatorem operacji
// ("op") i opisem narzędzia ("cutter"), więc działają z istniejącym CAD-em, rozkrojem i eksportami; frezy krawędziowe
// (zaokrąglający, fazujący) trafiają do Part::fillets / Part::chamfers. Błędy użytkownika zgłaszane są jako std::invalid_argument.
#pragma once
#include <string>
#include <vector>

#include "core.h"

namespace cutters {

using json = core::json;
using Point = core::Point;

constexpr size_t MAX_CUTS = 200;  // tyle obróbek dopuszcza Part::validate na jeden element

struct CutterInfo {
    std::string id;           // "prosty" | "kulowy" | "v" | "zaokraglajacy" | "fazujacy"
    std::string name;         // nazwa na liście
    std::string description;  // opis pod listą
    bool for_edges;           // frez krawędziowy (zaokrągla / fazuje krawędź), a nie rowkuje powierzchnię
    bool has_angle;           // kąt rozwarcia (frez V)
    double default_diameter;
    double default_angle;
};
const std::vector<CutterInfo>& catalog();
// Znajduje narzędzie po identyfikatorze albo nazwie; nieznane → std::invalid_argument.
const CutterInfo& info(const std::string& id_or_name);

struct Cutter {
    std::string type = "prosty";
    double diameter = 8;  // mm (dla zaokrąglającego i fazującego: rozmiar profilu = promień / szerokość fazy)
    double angle = 90;    // stopnie, tylko frez V
};
Cutter cutter_from_json(const json& data);
json cutter_to_json(const Cutter& cutter);
// Średnica 0,5 … 40 mm, kąt V 20 … 150°; narzędzie krawędziowe nie może rowkować powierzchni (zgłasza błąd z powodem).
void validate(const Cutter& cutter);
// Szerokość rowka na danej głębokości: prosty = średnica, kulowy = cięciwa kuli (do średnicy), V = 2·h·tg(kąt/2) nie więcej niż średnica.
double effective_width(const Cutter& cutter, double depth);

struct Op {
    std::string id;     // unikalny identyfikator operacji (puste = nadany przy dodawaniu)
    std::string name;   // nazwa na liście, np. „Rowek pod plecy”
    std::string kind;   // "rowek" (A→B) | "kieszen" (prostokąt P0–P1) | "kieszen_okragla" (środek P0, średnica z `width`)
    Cutter cutter;
    std::string face = "top";  // "top" | "bottom"
    double depth = 5;
    double width = 0;          // 0 = szerokość z narzędzia na danej głębokości; dla kieszeni okrągłej = średnica
    std::vector<Point> points; // rowek: 2 punkty; kieszeń: 2 przeciwległe narożniki; okrągła: środek
};
// Zamienia operację na wpisy Part::machining dla płyty `part`. Narzędzie krawędziowe, głębokość ≥ grubość, ścieżka poza płytą
// albo zła liczba punktów → std::invalid_argument z powodem.
std::vector<json> cuts_for(const Op& op, const core::Part& part);

struct EdgeOp {
    std::string cutter;  // "zaokraglajacy" | "fazujacy"
    double size = 3;
    std::vector<std::string> edges;
};
// Frez krawędziowy: zwraca nowe tablice fillets / chamfers (zaokrąglający → fillets, fazujący → chamfers; wzajemnie się wykluczają).
struct EdgeResult {
    json fillets = json::array();
    json chamfers = json::array();
    size_t touched = 0;
};
EdgeResult apply_edge_cutter(const core::Part& part, const EdgeOp& op);

// Dopisuje wpisy operacji do obróbki; więcej niż MAX_CUTS łącznie → std::invalid_argument.
json add_cuts(const json& machining, const std::vector<json>& cuts);
// Usuwa wszystkie wpisy operacji `op_id`; ręczne i inne operacje zostają.
json remove_op(const json& machining, const std::string& op_id);

struct OpSummary {
    std::string id, name, cutter_name, kind;
    double depth = 0;
    size_t cuts = 0;
};
// Operacje frezowania w obróbce płyty (wpisy z polem "op"), w kolejności dodania.
std::vector<OpSummary> list_ops(const json& machining);
// Liczba wpisów ręcznych (bez pola "op") — też liczą się do limitu.
size_t count_unowned(const json& machining);
// Opis do dziennika i paska stanu, np. „rowek V 90° Ø6 na 4 mm (1 wpis)”.
std::string describe(const Op& op, size_t cuts);

// Miniaturka narzędzia: przekrój frezu i próbka śladu; [0] = tło, dalej bryły; współrzędne płótna (y w dół) w polu width × height.
std::vector<std::vector<Point>> cutter_thumbnail(const std::string& id, double width, double height, double margin = 6);

}  // namespace cutters
