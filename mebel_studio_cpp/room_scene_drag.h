// Direct purchased-object placement, one undo step and cheap move previews.
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include "core.h"
#include "tk.h"

class App;

namespace room_scene_drag {

using json = core::json;

// Stan przeciągania (słownik `scene_drag` w Pythonie): base, obj, start, candidate (None == null), last.
struct SceneDrag {
    core::ProjectPtr base;
    json obj;
    std::pair<int, int> start{0, 0};
    json candidate = nullptr;
    double last = 0;
};

// Zwraca (przesunięty obiekt, {oś: korekta} lub None gdy brak przyciągnięcia).
std::pair<json, std::optional<std::map<int, double>>> snap_object(const core::Project& project, const json& obj, double tolerance = 80, bool vertical = false);

bool begin(App* app, tk::Event& event, const std::string& identity);
bool move(App* app, tk::Event& event, bool force = false);
bool end(App* app, tk::Event& event);
bool cancel(App* app);

}  // namespace room_scene_drag
