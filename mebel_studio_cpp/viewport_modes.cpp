#include "viewport_modes.h"

#include <algorithm>

#include "assembly.h"

namespace viewport_modes {

std::set<std::string> xray_part_ids(core::ProjectPtr project, bool enabled, const std::string& scope,
                                    const std::optional<std::string>& selected) {
    if (!enabled) return {};
    if (std::find(XRAY_MODES.begin(), XRAY_MODES.end(), scope) == XRAY_MODES.end())
        throw core::ValueError("Wybierz zakres X-ray z listy.");
    std::vector<const core::Part*> parts;
    for (const auto& part : project->parts)
        if (part.visible) parts.push_back(&part);
    if (scope == "Cały zestaw") {
        std::set<std::string> ids;
        for (auto* part : parts) ids.insert(part->id);
        return ids;
    }
    if (scope == "Fronty") {
        std::set<std::string> ids;
        for (auto* part : parts) {
            std::string role = assembly::inferred_role(*part);
            if (role == "door" || role == "drawer") ids.insert(part->id);
        }
        return ids;
    }
    const core::Part* chosen = nullptr;
    for (auto* part : parts)
        if (selected && part->id == *selected) { chosen = part; break; }
    if (chosen == nullptr) return {};
    if (scope == "Wybrany element") return {chosen->id};
    std::set<std::string> ids;
    for (auto* part : parts)
        if (part->group == chosen->group) ids.insert(part->id);
    return ids;
}

}  // namespace viewport_modes
