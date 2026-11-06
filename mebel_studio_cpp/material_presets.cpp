// Documented product sizes, plus editable workshop thickness choices.
#include "material_presets.h"

#include <memory>
#include <stdexcept>

#include "dimension_model.h"
#include "pyutil.h"

namespace material_presets {

using core::Material;
using core::Project;
using core::ProjectPtr;
using py::StopIteration;
using py::ValueError;

const std::string EGGER = "https://support.egger.com/hc/en-us/articles/360001082138-What-are-the-dimensions-of-EGGER-Eurodekor-Faced-Chipboard";
const std::string PERFECTSENSE_PL = "https://www.egger.com/pl/meble-i-aranzacja-wnetrz/kolekcja-materialow-dekoracyjnych/asortyment-perfectsense?country=PL";

const std::vector<std::pair<std::string, Preset>> PRESETS = {
    {"Eurodekor — płyta wiórowa (oferta UK)",
     {"plyta", {2800, 2070}, {8, 12, 15, 16, 18, 19, 22, 25, 28, 30, 38}, {18}, EGGER, "#ceb28f", true,
      "Karta EGGER z 21.02.2023, zakład Hexham / UK: 18 mm magazynowo, pozostałe grubości na zamówienie. Oferta i dekor w Polsce wymagają sprawdzenia."}},
    {"PerfectSense Premium Matt PM — MDF (PL)",
     {"mdf", {2800, 2070}, {10, 12, 16, 18, 19, 22, 25}, {18}, PERFECTSENSE_PL, "#ba9871", true,
      "EGGER, oferta PL: MDF lakierowany Premium Matt E1E05 TSCA ST, PM/ST9. 18 mm magazynowo; 10, 12, 16, 19, 22, 25 mm jako warianty produkcyjne. Dostępność zależy od dekoru."}},
    {"PerfectSense Premium Gloss PG — MDF (PL)",
     {"mdf", {2800, 2070}, {10, 12, 16, 18, 19, 22, 25}, {18}, PERFECTSENSE_PL, "#d2d1ce", true,
      "EGGER, oferta PL: MDF lakierowany Premium Gloss E1E05 TSCA ST, PG/ST9. 18 mm magazynowo; 10, 12, 16, 19, 22, 25 mm jako warianty produkcyjne. Dostępność zależy od dekoru."}},
    {"Sklejka — ustawienie warsztatowe", {"sklejka", {2500, 1250}, {4, 6, 8, 9, 12, 15, 18, 21, 24, 30}, {}, "", "#d4b58a", false, ""}},
    {"HDF — ustawienie warsztatowe", {"hdf", {2800, 2070}, {3, 4, 6}, {}, "", "#b69d80", false, ""}},
    {"Drewno lite — ustawienie warsztatowe", {"drewno", {2500, 200}, {18, 20, 22, 25, 28, 30, 40, 45, 50, 60}, {}, "", "#b68a58", false, ""}}};

const Preset& preset(const std::string& name) {
    for (const auto& [key, spec] : PRESETS) if (key == name) return spec;
    throw std::out_of_range(name);
}

ProjectPtr apply_material_preset(const Project& project, const std::string& part_id, const std::string& name, const json& thickness) {
    const Preset& spec = preset(name);
    bool listed = false;
    if (thickness.is_number()) for (int t : spec.thicknesses) if (static_cast<double>(t) == thickness.get<double>()) listed = true;
    if (!listed) throw ValueError("Wybierz grubość z listy tego materiału.");
    const core::Part* found = nullptr;
    for (const auto& q : project.parts) if (q.id == part_id) { found = &q; break; }
    if (!found) throw StopIteration();
    ProjectPtr result = dimension_model::resize_dimensions(project, part_id, found->u, found->v, thickness);
    core::Part* p = nullptr;
    for (auto& q : result->parts) if (q.id == part_id) { p = &q; break; }
    if (!p) throw StopIteration();
    Material* material = nullptr;
    for (auto& m : result->materials) if (m.name == name) { material = &m; break; }
    if (!material) {
        Material created;
        created.name = name;
        created.kind = spec.kind;
        created.sheet_u = spec.sheet.first;
        created.sheet_v = spec.sheet.second;
        created.color = spec.color;
        created.price_m2 = 0;
        created.price_m3 = 0;
        result->materials.push_back(created);
        material = &result->materials.back();
    }
    p->material = material->id;
    result->validate();
    return result;
}

}  // namespace material_presets
