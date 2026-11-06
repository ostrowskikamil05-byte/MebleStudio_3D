// Native C++ DirectX/Vulkan viewport inside the existing CAD workspace.
//
// The Tk canvas keeps its complete editor bindings and 2D drawing commands.
// Only immutable presentation triangles and annotations cross the C ABI.
#pragma once
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "tk.h"

class App;

namespace native_viewport {

using json = core::json;

inline const std::set<std::string> NATIVE_ENGINES{"auto", "d3d11", "d3d12", "vulkan"};
inline constexpr unsigned NONE = 0xffffffffu;

struct NativeUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };

// Słownik wynikowy paint_native / NativeViewport.frame:
// dict(ok=, engine=, drawn=, diagnostics=, warning=, error=)
struct ViewportResult {
    bool ok = false;
    core::Drawn drawn;
    std::optional<std::string> engine, warning, error;
    json diagnostics;
};

// Argumenty paint_native(canvas, project, *, backend, camera, size, **options)
struct Request {
    core::ProjectPtr project;
    std::string backend = "auto";
    core::Camera camera;
    std::pair<double, double> size;
    bool interactive = false;
    std::set<std::string> selected_ids;
    std::optional<std::string> selected;
    double explode = 0;
    bool xray = false;
    std::set<std::string> xray_ids;
    std::string quality = "balanced";
};

ViewportResult paint_native(tk::Canvas* canvas, const Request& request);

void reset_native_failure(tk::Canvas* canvas);

void close_native(tk::Canvas* canvas);

// Mirror editor annotations; native GPU already draws model/background.
// Zatwierdza nakładki po ostatnim rysowaniu (w Pythonie także commit_native).
void flush_native(tk::Canvas* canvas);

// Commit overlays after the final editor draw wrapper, on the Tk thread.
void install(App* app);

}  // namespace native_viewport
