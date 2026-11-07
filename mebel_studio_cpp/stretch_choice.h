// Okienko wyboru po rozciągnięciu mebla: pociągnięcie frontu do siebie (głębokość) pozwala zostawić pogłębienie albo zrobić
// narożnik L; pociągnięcie w górę (wysokość) pozwala wybrać, czym ma być wyższy mebel (szafka, szafka z pawlaczem, szafa, regał).
#pragma once
#include <string>
#include "core.h"

class App;

namespace stretch_choice {

// Wywoływane po zatwierdzeniu rozciągnięcia ściany `face` (np. "y0", "z1") mebla `group`; `base` to projekt sprzed rozciągnięcia.
// Gdy zmiana jest zbyt mała albo mebel nie pochodzi z kreatora, nic nie pokazuje.
void offer(App* app, const core::Project& base, const std::string& group, const std::string& face);

}  // namespace stretch_choice
