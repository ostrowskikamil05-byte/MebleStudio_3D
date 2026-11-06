// Presentation-only snapshots for the integrated native C++ viewport.
#pragma once
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include "core.h"
#include "viewport_render.h"

namespace native_scene_export {

using json = core::json;

inline constexpr char MAGIC[] = "MS3DNAT1";  // 8 bajtów + '\0'
inline constexpr unsigned VERSION = 1;
// struct.Struct: "<III6ff" (40 B), "<IIII6f" (40 B), "<10fII" (48 B)
inline constexpr size_t HEADER_SIZE = 40, OWNER_SIZE = 40, VERTEX_SIZE = 48;
inline constexpr unsigned EMISSIVE = 1, WOOD = 2, SCENE = 4, ARCHITECTURE = 8, FITTING = 16, GHOST = 32, SELECTED = 64;
inline constexpr std::array<const char*, 4> KIND_NAMES = {"part", "fitting", "scene", "architecture"};

// Argumenty nazwane build_snapshot(project, *, context=None, interactive=False,
// explode=0, camera=None, selected_ids=(), selected=None, xray_ids=())
struct Options {
    viewport_render::Context* context = nullptr;
    bool interactive = false;
    double explode = 0;
    const core::Camera* camera = nullptr;
    std::set<std::string> selected_ids;
    std::optional<std::string> selected;
    std::set<std::string> xray_ids;
    bool ground = true;  // podłoga + siatka pod modelem (silniki natywne ich nie rysują)
};

// Zwraca (binarny snapshot, diagnostyka); nigdy nie zmienia projektu ani danych CNC.
std::pair<std::string, json> build_snapshot(const core::Project& project, const Options& options = {});

}  // namespace native_scene_export
