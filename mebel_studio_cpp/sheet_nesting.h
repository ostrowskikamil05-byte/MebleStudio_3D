// Rozkrój płyt: optymalizator (wiele wariantów upakowania gilotynowego, wybierany jest najlepszy), statystyki odpadu,
// numer płyty i pozycja każdej formatki oraz eksport arkuszy rozkroju (HTML do druku, DXF dla maszyn/plotera).
//
// Format wyniku jest taki sam jak w core::cut_plan: lista płyt {material, thickness, u, v, free, pieces} + lista
// identyfikatorów formatek, które się nie mieszczą. Optymalizator dopisuje do każdej płyty pola:
//   used_area (mm²), waste_percent, offcut = [x, y, szerokość, wysokość] największego użytecznego odpadu.
// Tryb „klasyczny” zwraca dokładnie wynik core::cut_plan (zgodny z wersją Python), tryb „optymalny” nigdy nie jest od niego gorszy.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace sheet_nesting {

using json = core::json;

enum class Mode { Classic, Optimized };

// Ustawienie globalne (program ustawia „optymalny” przy starcie; biblioteka domyślnie „klasyczny”, jak Python).
Mode mode();
void set_mode(Mode mode);
std::string mode_name(Mode mode);

// Plan rozkroju według bieżącego trybu (to wywołują raport, kosztorys i widok Rozkrój).
std::pair<json, std::vector<std::string>> plan(core::Project& project);
// Plan optymalny niezależnie od trybu.
std::pair<json, std::vector<std::string>> optimize(core::Project& project);

// Dopisuje used_area / waste_percent / offcut do płyty zbudowanej dowolnym algorytmem (kerf = rzaz projektu).
void annotate(json& sheet, double kerf);

struct Placement {
    int sheet = 0;     // numer płyty od 1; 0 = formatka nie mieści się na płycie
    int position = 0;  // numer formatki na płycie od 1
    bool rotated = false;
};
// id formatki -> płyta i pozycja
std::map<std::string, Placement> placements(const json& sheets);

// Podsumowanie planu: liczba płyt, wykorzystanie, odpad, koszt płyt, podział na materiały i grubości, użyteczne odpady.
json statistics(const core::Project& project, const json& sheets, const std::vector<std::string>& unplaced);
// Jedna linia opisu do paska pod widokiem rozkroju.
std::string summary_line(const core::Project& project, const json& sheets, const std::vector<std::string>& unplaced);

// HTML do druku: podsumowanie, rysunek każdej płyty (numery, wymiary, kierunek włókien, odpad) i tabela formatek.
void sheets_export(core::Project& project, const std::string& path);
// DXF: obrys każdej płyty i formatek (warstwy SHEET_n / PARTS / LABELS), płyty obok siebie. Tylko ASCII.
void sheets_dxf_export(core::Project& project, const std::string& path);

}  // namespace sheet_nesting
