// Migration adapter for the C++20 furniture calculation library.
#include "native_model.h"

#include <cmath>
#include <cstring>
#include <mutex>

#include "pyutil.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace native_model {

using py::ValueError;

namespace {

struct Library {
    void* handle = nullptr;
    uint32_t (*version)() = nullptr;
    void* (*call)(const char*) = nullptr;
    void (*free)(void*) = nullptr;
    int (*transform)(const PartPose*, const double*, uint64_t, double*) = nullptr;
};

Library g_library;
bool g_loaded = false;
std::mutex g_load_lock;
std::map<std::string, long long> g_calls;

void* symbol(void* handle, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

Library& library() {
    if (g_loaded) return g_library;
    load_library();
    return g_library;
}

// json.dumps(..., allow_nan=False)
void reject_non_finite(const json& value) {
    if (value.is_number_float()) {
        double v = value.get<double>();
        if (!std::isfinite(v)) throw ValueError("Out of range float values are not JSON compliant: " + py::repr(v));
    } else if (value.is_array() || value.is_object()) {
        for (const auto& item : value) reject_non_finite(item);
    }
}

}  // namespace

std::filesystem::path library_path() { return py::executable_dir() / "MebelModel.dll"; }

bool available() {
#ifdef _WIN32
    return std::filesystem::is_regular_file(library_path());
#else
    return false;  // os.name == 'nt'
#endif
}

void load_library() {
    std::lock_guard<std::mutex> guard(g_load_lock);
    if (g_loaded) return;
    auto path = library_path();
    if (!std::filesystem::is_regular_file(path))
        throw RuntimeError("Brakuje biblioteki obliczeniowej C++: " + py::path_str(path) + ". Rozpakuj cały ZIP programu.");
    Library lib;
#ifdef _WIN32
    AddDllDirectory(path.parent_path().wstring().c_str());
    lib.handle = LoadLibraryExW(path.wstring().c_str(), nullptr, 0x00000100 | 0x00001000);
    if (!lib.handle) throw py::OSError(std::error_code((int)GetLastError(), std::system_category()), py::path_str(path));
#else
    lib.handle = dlopen(path.c_str(), RTLD_NOW);
    if (!lib.handle) throw py::OSError(std::error_code(ENOEXEC, std::generic_category()), py::path_str(path));
#endif
    lib.version = reinterpret_cast<uint32_t (*)()>(symbol(lib.handle, "ms3d_model_version"));
    lib.call = reinterpret_cast<void* (*)(const char*)>(symbol(lib.handle, "ms3d_model_call"));
    lib.free = reinterpret_cast<void (*)(void*)>(symbol(lib.handle, "ms3d_model_free"));
    lib.transform = reinterpret_cast<int (*)(const PartPose*, const double*, uint64_t, double*)>(symbol(lib.handle, "ms3d_model_transform"));
    if (!lib.version || !lib.call || !lib.free || !lib.transform) throw py::KeyError("ms3d_model_*");  // AttributeError w ctypes
    if (lib.version() != 1) throw RuntimeError("Niezgodna wersja biblioteki obliczeniowej C++.");
    g_library = lib;
    g_loaded = true;
}

std::string dumps_compact(const json& value) {
    reject_non_finite(value);
    return value.dump();
}

json call(const std::string& module, const std::string& function, const json& args, const json& kwargs) {
    json message = {{"module", module}, {"function", function}, {"args", args}, {"kwargs", kwargs}};
    std::string request = dumps_compact(message);
    if (request.size() > 16 * 1024 * 1024) throw ValueError("Żądanie silnika jest zbyt duże.");
    Library& lib = library();
    void* pointer = lib.call(request.c_str());
    if (!pointer) throw MemoryError("Silnik C++ nie mógł przygotować odpowiedzi.");
    json response;
    try {
        response = json::parse(static_cast<const char*>(pointer));
    } catch (...) {
        lib.free(pointer);
        throw;
    }
    lib.free(pointer);
    ++g_calls[module + "." + function];
    if (!py::truthy(py::get(response, "ok"))) {
        json type = py::get(response, "type");
        std::string text = py::get(response, "error", "Błąd silnika C++.").get<std::string>();
        if (type == "OSError") throw py::OSError(std::error_code((int)py::get(response, "winerror", 1).get<long long>(), std::system_category()));
        if (type == "RuntimeError") throw RuntimeError(text);
        if (type == "ZeroDivisionError") throw ZeroDivisionError(text);
        throw ValueError(text);
    }
    return response.at("result");
}

std::pair<std::vector<core::Point3>, int> transform(const core::Part& part, const std::vector<core::Point3>& vertices) {
    std::vector<double> points;
    points.reserve(vertices.size() * 3);
    for (const auto& v : vertices) points.insert(points.end(), v.begin(), v.end());
    std::vector<double> output(points.size());
    PartPose pose{};
    pose.origin[0] = part.x; pose.origin[1] = part.y; pose.origin[2] = part.z;
    pose.angles[0] = part.rx; pose.angles[1] = part.ry; pose.angles[2] = part.rz;
    const char* keys[4] = {"x", "y", "z", "yaw"};
    for (int i = 0; i < 4; ++i) pose.placement[i] = py::to_float(py::get(part.layout, keys[i], 0));
    if (part.plane == "XY") pose.plane = 0;
    else if (part.plane == "XZ") pose.plane = 1;
    else if (part.plane == "YZ") pose.plane = 2;
    else throw py::KeyError(part.plane);
    int sign = library().transform(&pose, points.data(), points.size() / 3, output.data());
    if (!sign) throw ValueError("Nieprawidłowa geometria lub ustawienie elementu w silniku C++.");
    ++g_calls["viewport_render.transform"];
    std::vector<core::Point3> result;
    result.reserve(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) result.push_back({output[3 * i], output[3 * i + 1], output[3 * i + 2]});
    return {std::move(result), sign};
}

json diagnostics() {
    json calls = json::object();
    for (const auto& [key, count] : g_calls) calls[key] = count;
    return py::merged(call("native", "info"), json{{"library", py::path_str(library_path())}, {"routed_functions", calls}});
}

void save_project(core::Project& project, const std::string& path) {
    project.validate();
    std::string snapshot = py::dumps(project.data(), false, 2);
    call("project_io", "save", json::array({py::path_str(std::filesystem::absolute(py::to_path(path))), snapshot}));
}

core::ProjectPtr load_project(const std::string& path) {
    json text = call("project_io", "read", json::array({py::path_str(std::filesystem::absolute(py::to_path(path)))}), json{{"preserve_order", true}});
    return core::Project::from_data(json::parse(text.get<std::string>()));
}

}  // namespace native_model
