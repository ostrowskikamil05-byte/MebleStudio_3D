// Editable decorative palette. Approximate screen colours, no vendor colour certification.
#include "palette.h"

#include <algorithm>

namespace palette {

const std::vector<std::pair<std::string, std::string>> PALETTE = [] {
    std::vector<std::pair<std::string, std::string>> p{
        {"Biel śnieżna", "#f5f5f0"}, {"Biel ciepła", "#eee9dd"}, {"Krem", "#e5dcc4"}, {"Beż piaskowy", "#cbb99e"}, {"Kaszmir", "#b8aaa0"}, {"Taupe", "#94867b"},
        {"Szary jasny", "#bec2c3"}, {"Szary kamienny", "#909493"}, {"Grafit", "#4f5661"}, {"Antracyt", "#333a3e"}, {"Czerń mat", "#222425"}, {"Granat", "#243b53"},
        {"Niebieski", "#477d9f"}, {"Błękit", "#a5cad8"}, {"Zieleń szałwia", "#a5b29c"}, {"Oliwka", "#7a8061"}, {"Zieleń butelkowa", "#244b40"}, {"Mięta", "#b5cfc0"},
        {"Bordo", "#673d44"}, {"Ceglany", "#a5604b"}, {"Terakota", "#bb795b"}, {"Pudrowy róż", "#d5b2b0"}, {"Musztarda", "#b29a50"}, {"Żółty", "#d5be6e"},
        {"Pomarańczowy", "#d28343"}, {"Lawenda", "#ada2bc"}, {"Fiolet", "#755f83"}, {"Petrol", "#32676e"}, {"Dąb naturalny", "#c5a06b"}, {"Dąb jasny", "#ddc498"},
        {"Dąb wędzony", "#80664e"}, {"Orzech", "#856044"}, {"Buk", "#cfaa80"}, {"Jesion", "#d9c49d"}, {"Sosna", "#e0c493"}, {"Brzoza", "#e8d5b2"}, {"Wenge", "#483b34"},
        {"Mahoń", "#8b5140"}, {"Beton jasny", "#b5b3ae"}, {"Beton ciemny", "#767975"}, {"Marmur biały", "#e6e4df"}, {"Marmur ciemny", "#3e4143"}};
    // PALETTE += [...]
    for (auto& item : std::vector<std::pair<std::string, std::string>>{
             {"Lazurowy intensywny", "#207be8"}, {"Szmaragdowy", "#17a568"}, {"Turkus intensywny", "#13a8b1"}, {"Pomarańcz soczysta", "#f98629"},
             {"Żółty słoneczny", "#ffd33d"}, {"Koral", "#ed6268"}, {"Fiolet intensywny", "#9865de"}})
        p.push_back(item);
    return p;
}();

core::ProjectPtr install_palette(core::ProjectPtr project) {
    for (const auto& [name, color] : PALETTE) {
        std::string title = "Dekor — " + name;
        bool present = std::any_of(project->materials.begin(), project->materials.end(), [&](const core::Material& m) { return m.name == title; });
        if (!present) project->materials.push_back(core::Material{.name = title, .color = color, .price_m2 = 0, .kind = "mdf"});
    }
    return project;
}

}  // namespace palette
