// Small local preset library; project data remains in .mebel files.
#include "configurator_presets.h"

namespace configurator_presets {

namespace fs = std::filesystem;
using py::ValueError;

fs::path preset_path() {
    return py::to_path(py::getenv("LOCALAPPDATA").value_or(py::path_str(py::home()))) / "MebelStudio3D" / "kreator_presety.json";
}

json load_presets(const std::optional<std::string>& path_) {
    fs::path path = path_ && !path_->empty() ? py::to_path(*path_) : preset_path();
    if (!fs::exists(path)) return json::object();
    if (fs::file_size(path) > 2'000'000) throw ValueError("Biblioteka presetów jest zbyt duża.");
    json data;
    try { data = json::parse(py::read_text(py::path_str(path))); }
    catch (const json::parse_error& e) { throw ValueError(e.what()); }  // json.JSONDecodeError jest ValueError
    if (!data.is_object() || py::get(data, "schema") != 1 || !py::get(data, "presets").is_object())
        throw ValueError("Nieprawidłowa biblioteka presetów.");
    bool bad = data["presets"].size() > 100;
    for (auto& [name, config] : data["presets"].items()) {
        if (py::strip(name).empty() || py::len(name) > 80 || !config.is_object() || !py::get(config, "_material_names", json::object()).is_object())
            bad = true;
    }
    if (bad)
        throw ValueError("Nieprawidłowy zapis konfiguracji w presetach.");
    return data["presets"];
}

void save_preset(const std::string& name_, const json& config, const std::optional<std::string>& path_) {
    std::string name = py::strip(name_);
    if (name.empty() || py::len(name) > 80) throw ValueError("Nazwa presetu: od 1 do 80 znaków.");
    fs::path path = path_ && !path_->empty() ? py::to_path(*path_) : preset_path();
    json presets = load_presets(py::path_str(path)); presets[name] = config;
    if (presets.size() > 100) throw ValueError("Maksymalnie 100 własnych presetów.");
    std::string payload = py::dumps({{"schema", 1}, {"presets", presets}}, false, 2);
    if (payload.size() > 2'000'000) throw ValueError("Biblioteka presetów jest zbyt duża.");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    fs::path temporary = fs::path(path).replace_extension(".tmp"); py::write_text(py::path_str(temporary), payload); fs::rename(temporary, path);
}

bool delete_preset(const std::string& name, const std::optional<std::string>& path_) {
    fs::path path = path_ && !path_->empty() ? py::to_path(*path_) : preset_path(); json presets = load_presets(py::path_str(path));
    if (!presets.contains(name)) return false;
    presets.erase(name);
    fs::path temporary = fs::path(path).replace_extension(".tmp");
    py::write_text(py::path_str(temporary), py::dumps({{"schema", 1}, {"presets", presets}}, false, 2)); fs::rename(temporary, path);
    return true;
}

}  // namespace configurator_presets
