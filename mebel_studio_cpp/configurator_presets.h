// Small local preset library; project data remains in .mebel files.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include "pyutil.h"

namespace configurator_presets {

using json = py::json;

std::filesystem::path preset_path();

json load_presets(const std::optional<std::string>& path = std::nullopt);

void save_preset(const std::string& name, const json& config, const std::optional<std::string>& path = std::nullopt);

bool delete_preset(const std::string& name, const std::optional<std::string>& path = std::nullopt);

}  // namespace configurator_presets
