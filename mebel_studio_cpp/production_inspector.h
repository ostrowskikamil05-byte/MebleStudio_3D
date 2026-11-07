// Inspektor produkcyjny: sprawdza projekt tak, jak zrobiłby to doświadczony technolog przed wysłaniem do cięcia.
//
// Reguły:
//   shelf_sag      — ugięcie półki pod obciążeniem (belka swobodnie podparta, moduł sprężystości zależny od materiału);
//                    półki dzielone przegrodami liczone są na najdłuższym przęśle
//   hole_edge      — otwór zbyt blisko krawędzi lub powierzchni płyty (ryzyko wyrwania), cienka ścianka pod otworem nieprzelotowym
//   hole_clash     — otwory (okucia) przecinające się lub zostawiające < 3 mm materiału; sprawdzane w 3D, także otwory z obu stron płyty
//                    (np. podpórki półek po obu stronach przegrody); głębokość takich otworów można skrócić automatycznie
//   edge_banding   — widoczne krawędzie bez obrzeża (fronty: wszystkie, korpus: krawędź frontowa); naprawa automatyczna
//   part_collision — części tego samego mebla, które wnikają w siebie
//   hinge_count    — za mało zawiasów na wysokość frontu
//   sheet_fit      — formatka większa niż płyta (po odjęciu marginesów)
#pragma once
#include <string>
#include <vector>
#include "core.h"

namespace production_inspector {

using json = core::json;

enum class Severity { Info = 0, Warning = 1, Error = 2 };

struct Settings {
    double shelf_load_kg_m2 = 80;   // obciążenie użytkowe półki
    double sag_ratio = 200;         // dopuszczalne ugięcie = przęsło / sag_ratio
    double rim_small = 5;           // min. odległość brzegu otworu Ø < 20 od krawędzi [mm]
    double rim_large = 3;           // min. odległość brzegu otworu Ø >= 20 (puszki zawiasów) od krawędzi [mm]
    double web = 3;                 // min. grubość materiału między otworami / pod otworem [mm]
    double overlap = 1.5;           // tolerancja wnikania części w siebie [mm]
};

struct Finding {
    std::string rule;
    Severity severity = Severity::Warning;
    std::string part_id, part_name, group;
    std::string other_id, other_name;  // partner kolizji
    std::string message, hint;
    double value = 0, limit = 0;
    std::vector<int> edges;            // edge_banding: krawędzie do oklejenia (indeksy w Part::edges); -2 = obrys nieregularny
    int op_a = -1, op_b = -1;          // hole_clash: indeksy wierceń (Part::operations) do skrócenia
    bool fixable = false;
};

struct Report {
    std::vector<Finding> findings;  // lista do wyświetlenia (po 4 uwagi tej samej reguły na część + zbiorcze podsumowanie)
    std::vector<Finding> hidden;    // uwagi pominięte na liście; liczą się w sumach i są naprawiane razem z pozostałymi
    int errors = 0, warnings = 0, infos = 0;
    int parts_checked = 0;
    Settings settings;

    bool clean() const { return findings.empty(); }
};

Report inspect(const core::Project& project, const Settings& settings = {});

std::string severity_name(Severity severity);
std::string rule_title(const std::string& rule);
// Jedna linia do paska stanu / etykiety: „Inspektor: 2 błędy · 5 ostrzeżeń · 12 uwag” albo „Inspektor: bez uwag”.
std::string summary_line(const Report& report);

// Naprawia uwagi z fixable == true: dokleja brakujące obrzeża i skraca głębokość otworów przebijających płytę od obu stron.
// Zwraca nowy projekt; fixed = liczba poprawek.
core::ProjectPtr apply_fixes(const core::Project& project, const Report& report, int* fixed = nullptr);

// Wydruk HTML (lista uwag pogrupowana według reguł, z częścią i podpowiedzią).
void report_export(const core::Project& project, const Report& report, const std::string& path);

}  // namespace production_inspector
