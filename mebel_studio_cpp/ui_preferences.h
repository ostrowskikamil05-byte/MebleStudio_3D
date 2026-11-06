// Validated local interface preferences; never stored in a furniture project.
#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace ui_preferences {

using json = core::json;

namespace detail {
inline json make_defaults() {
    json d = json::object();
    d["schema"] = 1;
    d["window_mode"] = "auto";
    d["window_width"] = 1366;
    d["window_height"] = 768;
    d["window_x"] = nullptr;
    d["window_y"] = nullptr;
    d["ui_scale"] = 1.0;
    d["inspector_width"] = 330;
    d["ribbon_height"] = 110;
    d["show_inspector"] = true;
    d["show_ribbon"] = true;
    d["show_toolbars"] = true;
    d["engine"] = "auto";
    d["quality"] = "balanced";
    return d;
}
}  // namespace detail

inline const json DEFAULTS = detail::make_defaults();

// Keep the original values valid so existing local preferences migrate intact.
inline const std::vector<std::string> ENGINES = {"auto", "cpu", "gpu", "d3d11", "d3d12", "vulkan"};
// Uporządkowany słownik etykieta -> wartość.
inline const std::vector<std::pair<std::string, std::string>> ENGINE_LABELS = {
    {"Automatyczny dobór", "auto"},
    {"DirectX 11 · silnik C++", "d3d11"},
    {"DirectX 12 · silnik C++", "d3d12"},
    {"Vulkan · silnik C++", "vulkan"},
    {"OpenGL · GPU i wysoka jakość", "gpu"},
    {"CPU", "cpu"},
};

// Domyślna implementacja preference_path(): LOCALAPPDATA/MebelStudio3D/ui_preferences.json.
std::filesystem::path default_preference_path();

// W Pythonie to zwykła funkcja modułu, którą testy podmieniają — tu wymienialny std::function.
inline std::function<std::filesystem::path()> preference_path = default_preference_path;

json validate_preferences(const json& data);
json load_preferences(const std::optional<std::filesystem::path>& path = std::nullopt);
json save_preferences(const json& preferences, const std::optional<std::filesystem::path>& path = std::nullopt);

}  // namespace ui_preferences
