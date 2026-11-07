// Okienko wyboru po rozciągnięciu mebla: pociągnięcie frontu do siebie (głębokość) pozwala zostawić pogłębienie albo zrobić
// narożnik L; pociągnięcie w górę (wysokość) pozwala wybrać, czym ma być wyższy mebel (szafka, szafka z pawlaczem, szafa, regał).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core.h"
#include "tk.h"

class App;

namespace stretch_choice {

// Wywoływane po zatwierdzeniu rozciągnięcia ściany `face` (np. "y0", "z1") mebla `group`; `base` to projekt sprzed rozciągnięcia.
// Gdy zmiana jest zbyt mała albo mebel nie pochodzi z kreatora, nic nie pokazuje.
void offer(App* app, const core::Project& base, const std::string& group, const std::string& face);

// Okno konfiguracji zaznaczonego mebla z kreatora (uchwyty, nóżki, zawiasy, materiały, fronty, LED, cokół, blat);
// opcjonalnie dla całego rzędu szafek. Narzędzie „Konfiguruj” w palecie CAD.
void open_configuration(App* app, const std::string& group, const std::string& notice = "");

// Nakłada zmienione pola na konfigurację mebla tak, jak okno Konfiguruj (łącznie z zależnościami: szuflady bez półek, TIP-ON bez sprężyny).
void apply_overrides(core::json& config, const core::json& values);

// Otwarte okno Konfiguruj (jedno naraz) albo nullptr.
tk::Widget* configuration_dialog();

// Do testów: warianty, jakie dałoby okno wyboru po rozciągnięciu ściany `face` (build == nullptr oznacza „zostaw jak jest”).
struct Variant {
    std::string id, label;
    std::function<core::ProjectPtr(const core::Project&)> build;
    bool enabled = true;  // false = kafelek wyłączony w oknie (wariant niemożliwy dla tego mebla)
};
std::vector<Variant> variants(const core::Project& base, const core::Project& stretched, const std::string& group, const std::string& face);

}  // namespace stretch_choice
