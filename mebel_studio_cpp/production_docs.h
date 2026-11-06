// Rysunki części i etykiety (HTML/SVG).
#pragma once
#include <string>
#include "core.h"

namespace production_docs {

std::string part_svg(const core::Project& project, const core::Part& part);

void drawings_export(core::Project& project, const std::string& path);
void labels_export(core::Project& project, const std::string& path);
// Wygodne przeciążenia dla wywołań z App (project jest ProjectPtr).
inline void drawings_export(const core::ProjectPtr& project, const std::string& path) { drawings_export(*project, path); }
inline void labels_export(const core::ProjectPtr& project, const std::string& path) { labels_export(*project, path); }

}  // namespace production_docs
