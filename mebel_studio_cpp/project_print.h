// Self-contained A4 project book: layout, assemblies, parts and sheet plan.
//
// Presentation uses nominal polygon prisms, never CAD triangulation. All model
// dimensions come from the closed source model; exploding only moves drawings.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "core.h"

namespace project_print {

using json = core::json;

// Argumenty nazwane Pythona: group=None, include_cut_plan=True, include_parts=True, explosion=.35
struct Options {
    std::optional<std::string> group = std::nullopt;
    bool include_cut_plan = true;
    bool include_parts = true;
    double explosion = .35;
};

std::string _num(double value);

// Export printable HTML and return path/counts; never alter source project.
//
// group is an exact group name or None for the whole set. Display visibility
// and animation are ignored: every manufactured part prints in closed state.
// Each source Part represents one item; numbers follow source part order.
// Zwraca słownik: path, groups, parts, sheets, unplaced, sections.
json export_project_print(const core::Project& project, const std::string& path, const Options& options = {});
inline json export_project_print(const core::ProjectPtr& project, const std::string& path, const Options& options = {}) {
    return export_project_print(*project, path, options);
}

}  // namespace project_print
