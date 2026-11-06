// Migration adapter for the C++20 furniture calculation library.
//
// The editor and exact CAD validation stay in their original modules while the
// algorithms listed in native/core/*_ports.json execute in MebelModel.dll.
// This adapter never turns a native algorithm error into a silent retry.
//
// Różnice względem Pythona: bind/install_core/install_construction/
// install_viewport/install_services/install_shapes (podmiana funkcji w
// przestrzeniach nazw Pythona) nie istnieją — te algorytmy są teraz
// skompilowane bezpośrednio w przetłumaczonych modułach. Bufory wierzchołków
// transform() nie są cache'owane (cache był tylko optymalizacją).
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace native_model {

using json = core::json;

using RuntimeError = std::runtime_error;
struct ZeroDivisionError : std::runtime_error { using std::runtime_error::runtime_error; };
struct MemoryError : std::runtime_error { using std::runtime_error::runtime_error; };

// ctypes.Structure PartPose: 3+3+4 double + uint32 (rozmiar 88 z dopełnieniem)
struct PartPose {
    double origin[3];
    double angles[3];
    double placement[4];
    uint32_t plane;
};
static_assert(sizeof(PartPose) == 88);

std::filesystem::path library_path();

bool available();

// Ładuje bibliotekę (jednokrotnie); RuntimeError, gdy brak pliku lub zła wersja.
void load_library();

// JSON protocol: {"module","function","args","kwargs"} -> {"ok","result"|"error",...}
json call(const std::string& module, const std::string& function, const json& args = json::array(), const json& kwargs = json::object());

// One binary C++ call per part, preserving the original winding sign.
std::pair<std::vector<core::Point3>, int> transform(const core::Part& part, const std::vector<core::Point3>& vertices);

json diagnostics();

// json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
std::string dumps_compact(const json& value);

void save_project(core::Project& project, const std::string& path);

core::ProjectPtr load_project(const std::string& path);

}  // namespace native_model
