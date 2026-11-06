// Cached native geometry for catalogue cards; no CAD kernel or image service.
//
// Furniture cards are lightweight construction schematics using the same library
// parameters. Room equipment uses its actual viewport mesh. Fittings show their
// planning dimensions and are explicitly simplified, never manufacturer meshes.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace tk { class Canvas; }

namespace catalogue_previews {

using json = core::json;
using Face = std::vector<core::Point3>;
using ColoredFaces = std::vector<std::pair<Face, std::string>>;

ColoredFaces preview_faces(const json& item);

ColoredFaces _faces(const std::string& key);  // @lru_cache(maxsize=256)

// Draw cached polygons on the card's Canvas, retaining item identity.
void draw_preview(tk::Canvas* canvas, const json& item, int width = 200, int height = 125);

}  // namespace catalogue_previews
