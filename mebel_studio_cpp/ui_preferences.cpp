#include "ui_preferences.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include "pyutil.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ui_preferences {

using core::ValueError;

fs::path default_preference_path() {
    // Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'MebelStudio3D' / 'ui_preferences.json'
    fs::path base;
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        base = py::to_path(local);
    } else {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        base = home ? py::to_path(home) : fs::path(".");
    }
    return base / "MebelStudio3D" / "ui_preferences.json";
}

namespace {

bool contains(const std::vector<std::string>& choices, const json& value) {
    if (!value.is_string()) return false;
    return std::find(choices.begin(), choices.end(), value.get<std::string>()) != choices.end();
}

// Czy wartość JSON jest liczbą Pythona (int/float, ale nie bool)?
bool is_py_number(const json& v) { return v.is_number(); }

double as_double(const json& v) { return v.get<double>(); }

// fsync(): zapisane dane mają trafić na dysk przed os.replace.
void sync_file(const fs::path& path) {
#ifdef _WIN32
    int fd = _wopen(path.c_str(), _O_RDWR | _O_BINARY);
    if (fd >= 0) {
        _commit(fd);
        _close(fd);
    }
#else
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

}  // namespace

json validate_preferences(const json& data) {
    bool schema_ok = true;
    if (data.is_object() && data.contains("schema")) {
        const json& schema = data.at("schema");
        if (schema.is_boolean()) schema_ok = schema.get<bool>();  // True == 1
        else if (schema.is_number()) schema_ok = as_double(schema) == 1.0;
        else schema_ok = false;
    }
    if (!data.is_object() || !schema_ok) throw ValueError("Nieobsługiwana wersja ustawień interfejsu.");
    json result = DEFAULTS;
    const std::vector<std::pair<std::string, std::vector<std::string>>> enums = {
        {"window_mode", {"auto", "custom"}},
        {"engine", ENGINES},
        {"quality", {"draft", "balanced", "high"}},
    };
    for (const auto& [key, choices] : enums) {
        if (data.contains(key)) {
            if (!contains(choices, data.at(key))) throw ValueError("Nieprawidłowe ustawienie: " + key);
            result[key] = data.at(key);
        }
    }
    struct Range { const char* key; double low, high; };
    const Range ranges[] = {{"window_width", 900, 7680}, {"window_height", 600, 4320},
                            {"inspector_width", 240, 1200}, {"ribbon_height", 64, 500},
                            {"ui_scale", .75, 2.0}};
    for (const auto& r : ranges) {
        std::string key = r.key;
        if (!data.contains(key)) continue;
        const json& value = data.at(key);
        if (!is_py_number(value) || !std::isfinite(as_double(value)) || !(r.low <= as_double(value) && as_double(value) <= r.high))
            throw ValueError("Nieprawidłowy zakres ustawienia: " + key);
        if (key != "ui_scale" && !value.is_number_integer() && std::trunc(as_double(value)) != as_double(value))
            throw ValueError("Wpisz całkowity rozmiar: " + key);
        if (key == "ui_scale") result[key] = as_double(value);
        else if (value.is_number_integer()) result[key] = value.get<long long>();
        else result[key] = static_cast<long long>(as_double(value));
    }
    for (const char* name : {"show_inspector", "show_ribbon", "show_toolbars"}) {
        std::string key = name;
        if (data.contains(key)) {
            if (!data.at(key).is_boolean()) throw ValueError("Nieprawidłowe ustawienie: " + key);
            result[key] = data.at(key);
        }
    }
    for (const char* name : {"window_x", "window_y"}) {
        std::string key = name;
        if (data.contains(key)) {
            const json& value = data.at(key);
            if (!value.is_null() && (!value.is_number_integer() || std::fabs(as_double(value)) > 100000))
                throw ValueError("Nieprawidłowa pozycja okna.");
            result[key] = value;
        }
    }
    return result;
}

json load_preferences(const std::optional<fs::path>& path_) {
    fs::path path = path_ ? *path_ : preference_path();
    try {
        if (fs::file_size(path) > 65536) return DEFAULTS;
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::system_error(std::error_code(errno, std::generic_category()), path.string());
        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string text = buffer.str();
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // utf-8-sig
        json parsed;
        try {
            parsed = json::parse(text);
        } catch (const json::parse_error& e) {
            throw ValueError(e.what());  // json.JSONDecodeError jest ValueError
        }
        return validate_preferences(parsed);
    } catch (const std::system_error&) {      // OSError
        return DEFAULTS;
    } catch (const std::invalid_argument&) {  // ValueError
        return DEFAULTS;
    } catch (const json::exception&) {        // TypeError
        return DEFAULTS;
    }
}

json save_preferences(const json& preferences, const std::optional<fs::path>& path_) {
    json data = validate_preferences(preferences);
    fs::path path = path_ ? *path_ : preference_path();
    fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    fs::create_directories(parent);
    // NamedTemporaryFile(prefix=path.name + '.', suffix='.tmp', dir=path.parent, delete=False)
    static thread_local std::random_device device;
    fs::path temporary;
    for (int attempt = 0;; ++attempt) {
        std::string middle;
        for (int i = 0; i < 8; ++i) middle += "abcdefghijklmnopqrstuvwxyz0123456789_"[device() % 37];
        std::string tail = "." + middle + ".tmp";
        temporary = parent / (path.filename().u8string() + std::u8string(tail.begin(), tail.end()));
        if (!fs::exists(temporary)) break;
        if (attempt > 10000) throw std::system_error(std::make_error_code(std::errc::file_exists), "No usable temporary file name found");
    }
    try {
        {
            std::ofstream output(temporary);  // tryb tekstowy, jak mode='w'
            if (!output) throw std::system_error(std::error_code(errno, std::generic_category()), temporary.string());
            output << py::dumps(data, false, 2);  // ensure_ascii=False, indent=2
            output.flush();
            output.close();
            if (!output) throw std::system_error(std::error_code(errno, std::generic_category()), temporary.string());
            sync_file(temporary);
        }
        fs::rename(temporary, path);  // os.replace
    } catch (...) {
        std::error_code ec;
        if (fs::exists(temporary, ec)) fs::remove(temporary, ec);
        throw;
    }
    std::error_code ec;
    if (fs::exists(temporary, ec)) fs::remove(temporary, ec);
    return data;
}

}  // namespace ui_preferences
