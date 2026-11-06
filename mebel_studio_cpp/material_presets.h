// Documented product sizes, plus editable workshop thickness choices.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace material_presets {

using json = core::json;

extern const std::string EGGER;
extern const std::string PERFECTSENSE_PL;

// Jeden wpis słownika PRESETS. W Pythonie klucz 'note' bywa nieobecny
// (wtedy spec.get('note', ...) daje wartość domyślną) — tu: has_note.
struct Preset {
    std::string kind;
    std::pair<double, double> sheet;
    std::vector<int> thicknesses;
    std::vector<int> stock_thicknesses;  // pusty, gdy klucz nie istnieje w Pythonie
    std::string source;
    std::string color;
    bool has_note = false;
    std::string note;
};

// dict nazwa -> wpis (kolejność jak w Pythonie)
extern const std::vector<std::pair<std::string, Preset>> PRESETS;

// PRESETS[name]; KeyError (std::out_of_range) dla nieznanej nazwy
const Preset& preset(const std::string& name);

core::ProjectPtr apply_material_preset(const core::Project& project, const std::string& part_id, const std::string& name, const json& thickness);

}  // namespace material_presets
