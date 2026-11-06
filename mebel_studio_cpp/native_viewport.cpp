// Native C++ DirectX/Vulkan viewport inside the existing CAD workspace.
#include "native_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <tuple>

#include "app.h"
#include "depth_raster.h"
#include "gpu_scene.h"
#include "native_scene_export.h"
#include "pyutil.h"
#include "viewport_render.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace native_viewport {

namespace {

using py::ValueError;

// --- Struktury ABI (ctypes.Structure)
struct NativeCamera {
    float yaw, pitch, scale, panX, panY;
    float center[3];
    float brightness;
    uint32_t xray;
    int32_t selected;
};
static_assert(sizeof(NativeCamera) == 44);

struct NativeHit {
    int32_t owner;
    uint32_t kind, flags;
    float world[3];
    float depth;
};
static_assert(sizeof(NativeHit) == 28);

struct NativeOverlay {
    uint32_t type, outline, fill, flags;
    float width;
    const float* coordinates;
    uint32_t count;
    const wchar_t* text;
    const wchar_t* font;
    int32_t height, bold, anchor;
};

constexpr size_t ERROR_SIZE = 4096;

struct Api {
    void* (*create)(size_t, const char*, wchar_t*, uint32_t) = nullptr;
    int (*upload)(void*, const uint8_t*, uint64_t, wchar_t*, uint32_t) = nullptr;
    int (*frame)(void*, const NativeCamera*, wchar_t*, uint32_t) = nullptr;
    int (*resize)(void*, uint32_t, uint32_t, wchar_t*, uint32_t) = nullptr;
    int (*overlay)(void*, const NativeOverlay*, uint32_t, wchar_t*, uint32_t) = nullptr;
    int (*pick)(void*, float, float, uint32_t, NativeHit*) = nullptr;
    int (*show)(void*, int) = nullptr;
    int (*info)(void*, wchar_t*, uint32_t) = nullptr;
    void (*close)(void*) = nullptr;
};

// --- wchar_t <-> UTF-8
std::string narrow(const wchar_t* w) {
    std::string out;
    for (size_t i = 0; w[i]; ++i) {
        uint32_t c = static_cast<uint32_t>(w[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xD800 && c < 0xDC00 && static_cast<uint32_t>(w[i + 1]) >= 0xDC00 && static_cast<uint32_t>(w[i + 1]) < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<uint32_t>(w[i + 1]) - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::wstring widen(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        uint32_t c = 0xFFFD;
        size_t n = 1;
        if (b < 0x80) c = b;
        else if ((b >> 5) == 6 && i + 1 < s.size()) { c = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((b >> 4) == 14 && i + 2 < s.size()) { c = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else if ((b >> 3) == 30 && i + 3 < s.size()) { c = ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); n = 4; }
        i += n;
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0x10000) { c -= 0x10000; out += static_cast<wchar_t>(0xD800 + (c >> 10)); out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF)); continue; }
        }
        out += static_cast<wchar_t>(c);
    }
    return out;
}

// --- Biblioteka
Api* g_api = nullptr;

Api& library() {
    if (g_api) return *g_api;
#ifndef _WIN32
    throw NativeUnavailable("Silnik natywny wymaga systemu Windows.");
#else
    auto path = py::executable_dir() / "MebelNative3D.dll";
    if (!std::filesystem::is_regular_file(path)) throw NativeUnavailable("Brak biblioteki MebelNative3D.dll w folderze programu.");
    HMODULE module = LoadLibraryExW(path.wstring().c_str(), nullptr, 0x00000100 | 0x00000800);
    if (!module) {
        DWORD code = GetLastError();
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
        std::string message = buffer ? narrow(buffer) : std::string();
        if (buffer) LocalFree(buffer);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) message.pop_back();
        throw NativeUnavailable("Nie udało się wczytać silnika C++: [WinError " + std::to_string(code) + "] " + message);
    }
    auto api = std::make_unique<Api>();
    auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(module, name)); };
    api->create = reinterpret_cast<decltype(api->create)>(symbol("ms3d_create"));
    api->upload = reinterpret_cast<decltype(api->upload)>(symbol("ms3d_upload"));
    api->frame = reinterpret_cast<decltype(api->frame)>(symbol("ms3d_frame"));
    api->resize = reinterpret_cast<decltype(api->resize)>(symbol("ms3d_resize"));
    api->overlay = reinterpret_cast<decltype(api->overlay)>(symbol("ms3d_overlay"));
    api->pick = reinterpret_cast<decltype(api->pick)>(symbol("ms3d_pick"));
    api->show = reinterpret_cast<decltype(api->show)>(symbol("ms3d_show"));
    api->info = reinterpret_cast<decltype(api->info)>(symbol("ms3d_info"));
    api->close = reinterpret_cast<decltype(api->close)>(symbol("ms3d_close"));
    if (!api->create || !api->upload || !api->frame || !api->resize || !api->overlay || !api->pick || !api->show || !api->info || !api->close)
        throw NativeUnavailable("Biblioteka silnika C++ ma niezgodny interfejs.");
    g_api = api.release();
    return *g_api;
#endif
}

// --- Widok natywny
class NativeViewport {
public:
    tk::Canvas* canvas;
    Api* api;
    std::string backend;
    std::vector<wchar_t> error = std::vector<wchar_t>(ERROR_SIZE, 0);
    void* handle = nullptr;
    bool closed = false;
    using CacheKey = std::tuple<const void*, bool, double, std::vector<double>, std::vector<std::string>, std::vector<std::string>, std::string>;
    std::optional<CacheKey> cache_key;
    std::string payload;
    std::shared_ptr<viewport_render::Context> context;
    std::vector<std::pair<std::string, std::string>> sources;
    std::optional<std::pair<double, double>> size;
    std::string info;
    json drawn = json::array();
    std::optional<std::string> failed_request, warning;
    std::vector<gpu_scene::MeshGroupPtr> meshes;
    json diagnostics = json::object();

    NativeViewport(tk::Canvas* canvas_, const std::string& backend_) : canvas(canvas_), api(&library()), backend(backend_) {
        for (char ch : backend) if (static_cast<unsigned char>(ch) >= 0x80) throw ValueError("'ascii' codec can't encode character in backend name");
        handle = api->create(static_cast<size_t>(canvas->winfo_id()), backend.c_str(), error.data(), ERROR_SIZE);
        if (!handle) {
            std::string text = narrow(error.data());
            throw NativeUnavailable(text.empty() ? "Nie udało się uruchomić silnika C++." : text);
        }
    }

    // if not api.name(handle, *args, error, len(error)): raise NativeUnavailable(...)
    void check(const char* name, const std::function<int()>& invoke) {
        error[0] = 0;
        if (!invoke()) {
            std::string text = narrow(error.data());
            throw NativeUnavailable(text.empty() ? std::string("Błąd silnika C++: ") + name : text);
        }
    }

    void close() {
        if (!closed) {
            closed = true;
            api->close(handle);
            handle = nullptr;
        }
    }

    void show(bool visible) {
        if (!closed) api->show(handle, visible ? 1 : 0);
    }

    ViewportResult frame(const Request& request) {
        const core::Project& project = *request.project;
        const core::Camera& camera = request.camera;
        auto prepared = viewport_render::prepare_scene(project);
        std::set<std::string> ghosts = gpu_scene::ghost_ids(project, request.xray, request.xray_ids);
        std::set<std::string> selection = request.selected_ids;
        if (request.selected && !request.selected->empty()) selection.insert(*request.selected);
        CacheKey key{prepared.get(), request.interactive, request.explode,
                     request.explode != 0 ? std::vector<double>(camera.center.begin(), camera.center.end()) : std::vector<double>{},
                     std::vector<std::string>(selection.begin(), selection.end()), std::vector<std::string>(ghosts.begin(), ghosts.end()), request.quality};
        if (!size || *size != request.size) {
            uint32_t w = static_cast<uint32_t>(std::max(1, static_cast<int>(request.size.first)));
            uint32_t h = static_cast<uint32_t>(std::max(1, static_cast<int>(request.size.second)));
            check("ms3d_resize", [&] { return api->resize(handle, w, h, error.data(), ERROR_SIZE); });
            size = request.size;
        }
        if (!cache_key || *cache_key != key) {
            native_scene_export::Options options;
            options.context = prepared.get(); options.interactive = request.interactive; options.camera = &camera; options.explode = request.explode;
            options.selected_ids = selection; options.xray_ids = ghosts;
            auto [bytes, report] = native_scene_export::build_snapshot(project, options);
            check("ms3d_upload", [&] { return api->upload(handle, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), error.data(), ERROR_SIZE); });
            payload = std::move(bytes);
            context = prepared;
            sources.clear();
            for (auto& item : report.at("owner_sources")) sources.emplace_back(item.at(0).get<std::string>(), item.at(1).get<std::string>());
            cache_key = key;
            meshes = gpu_scene::mesh_groups(*context, request.interactive);
            diagnostics = report;
        }
        NativeCamera native{};
        native.yaw = static_cast<float>(camera.yaw); native.pitch = static_cast<float>(camera.pitch); native.scale = static_cast<float>(camera.scale);
        native.panX = static_cast<float>(camera.pan[0]); native.panY = static_cast<float>(camera.pan[1]);
        for (int i = 0; i < 3; ++i) native.center[i] = static_cast<float>(camera.center[(size_t)i]);
        native.brightness = static_cast<float>(py::to_float(py::get(prepared->scene, "brightness", 1)));
        native.xray = request.xray ? 2 : 0;
        native.selected = -1;
        check("ms3d_frame", [&] { return api->frame(handle, &native, error.data(), ERROR_SIZE); });
        viewport_render::RenderedFaces cheap = gpu_scene::cheap_drawn(*prepared, meshes, camera, request.size, selection, request.explode,
                                                                      gpu_scene::visible_architecture(project, camera));
        drawn = cheap.faces;
        std::vector<wchar_t> buffer(ERROR_SIZE, 0);
        api->info(handle, buffer.data(), ERROR_SIZE);
        info = narrow(buffer.data());
        json hardware;
        try {
            hardware = json::parse(info);
        } catch (const json::exception&) {
            hardware = json{{"engine", info}};
        }
        if (!hardware.is_object()) py::type_error("'" + std::string(hardware.type_name()) + "' object is not a mapping");
        json merged = diagnostics;
        for (auto& [k, v] : hardware.items()) merged[k] = v;
        merged["native"] = true;
        ViewportResult result;
        result.ok = true; result.engine = "native"; result.drawn = drawn; result.diagnostics = merged; result.warning = warning;
        return result;
    }

    json hit(double x, double y, unsigned mode = 0) {
        if (closed) return nullptr;
        NativeHit result{};
        if (!api->pick(handle, static_cast<float>(x), static_cast<float>(y), mode, &result)) return nullptr;
        if (!(0 <= result.owner && result.owner < static_cast<int32_t>(sources.size()))) return nullptr;
        const auto& [identity, kind] = sources[static_cast<size_t>(result.owner)];
        return json{{"id", identity}, {"kind", kind},
                    {"world", json::array({static_cast<double>(result.world[0]), static_cast<double>(result.world[1]), static_cast<double>(result.world[2])})},
                    {"depth", static_cast<double>(result.depth)}, {"precise_surface", kind == "part"}, {"flags", result.flags}};
    }
};

// --- Atrybuty doklejane do płótna w Pythonie (canvas._native_*)
struct CanvasNative {
    std::shared_ptr<NativeViewport> viewport;
    bool bound = false;
    std::set<int> background_items;
    std::optional<std::string> requested_backend, failed_backend, commit_job;
    std::string failed_reason, error;
    bool pending = false;
    std::function<void()> recover;
};

std::map<tk::Canvas*, CanvasNative>& registry() {
    static std::map<tk::Canvas*, CanvasNative> instance;
    return instance;
}

depth_raster::CanvasState* canvas_state(tk::Canvas* canvas) { return dynamic_cast<depth_raster::CanvasState*>(canvas); }

void attach(tk::Canvas* canvas) {
    CanvasNative& state = registry()[canvas];
    if (state.bound) return;
    state.bound = true;
    canvas->bind("<Destroy>", [canvas](tk::Event& event) {
        if (event.widget == canvas) { close_native(canvas); registry().erase(canvas); }
        return std::string();
    }, true);
    auto visibility = [canvas](bool mapped) {
        return [canvas, mapped](tk::Event&) {
            auto found = registry().find(canvas);
            if (found != registry().end() && found->second.viewport) found->second.viewport->show(mapped);
            return std::string();
        };
    };
    canvas->bind("<Map>", visibility(true), true);
    canvas->bind("<Unmap>", visibility(false), true);
}

// Mirror editor annotations; native GPU already draws model/background.
struct OverlayBatch {
    std::vector<NativeOverlay> commands;
    std::deque<std::vector<float>> storage;
    std::deque<std::wstring> strings;
};

OverlayBatch overlay_commands(tk::Canvas* canvas, const std::set<int>& background) {
    static const std::map<std::string, uint32_t> types = {{"line", 1}, {"rectangle", 2}, {"oval", 3}, {"polygon", 4}, {"text", 5}};
    static const std::map<std::string, int32_t> anchors = {{"nw", 0}, {"n", 1}, {"ne", 2}, {"w", 3}, {"center", 4}, {"e", 5}, {"sw", 6}, {"s", 7}, {"se", 8}};
    OverlayBatch batch;
    std::map<std::string, uint32_t> colors;
    std::map<std::string, tk::font::Actual> fonts;
    auto color = [&](const std::string& value) -> uint32_t {
        if (value.empty()) return NONE;
        auto found = colors.find(value);
        if (found == colors.end()) {
            auto rgb = canvas->winfo_rgb(value);
            found = colors.emplace(value, static_cast<uint32_t>((rgb[0] >> 8) | ((rgb[1] >> 8) << 8) | ((rgb[2] >> 8) << 16))).first;
        }
        return found->second;
    };
    static const char* skipped[] = {"viewport.background", "viewport.floor", "viewport.grid", "viewport.shadow", "viewport.light_pool",
                                    "viewport.raster_model", "viewport.gpu_model", "viewport.grain", "viewport.ghost"};
    for (int item : canvas->find_all()) {
        if (background.contains(item)) continue;
        std::string kind = canvas->type(item);
        auto type = types.find(kind);
        if (type == types.end()) continue;
        auto tags = canvas->gettags(item);
        bool hidden_tag = std::find(tags.begin(), tags.end(), "model") != tags.end();
        for (const auto& tag : tags) for (const char* prefix : skipped) if (tag.rfind(prefix, 0) == 0) hidden_tag = true;
        if (hidden_tag) continue;
        if (canvas->itemcget(item, "state") == "hidden") continue;
        std::vector<double> coords = canvas->coords(item);
        if (coords.size() < 2) continue;
        batch.storage.emplace_back(coords.begin(), coords.end());
        NativeOverlay command{};
        command.type = type->second; command.coordinates = batch.storage.back().data(); command.count = static_cast<uint32_t>(coords.size());
        command.width = 1; command.outline = NONE; command.fill = NONE; command.anchor = 4;
        if (kind == "text") {
            command.fill = color(canvas->itemcget(item, "fill"));
            batch.strings.push_back(widen(canvas->itemcget(item, "text")));
            command.text = batch.strings.back().c_str();
            std::string specification = canvas->itemcget(item, "font");
            if (!fonts.contains(specification)) fonts[specification] = tk::font::actual(canvas, specification);
            const auto& font = fonts[specification];
            batch.strings.push_back(widen(font.family));
            command.font = batch.strings.back().c_str();
            command.width = static_cast<float>(py::to_float(canvas->itemcget(item, "width")));
            double scale = font.size > 0 ? canvas->winfo_fpixels("1i") / 72 : 1;
            command.height = static_cast<int32_t>(std::max(1.0, py::round_half_even(std::abs(font.size) * scale)));
            command.bold = font.weight == "bold" ? 1 : 0;
            auto anchor = anchors.find(canvas->itemcget(item, "anchor"));
            command.anchor = anchor != anchors.end() ? anchor->second : 4;
        } else {
            command.width = static_cast<float>(py::to_float(canvas->itemcget(item, "width")));
            if (kind == "line") {
                command.outline = color(canvas->itemcget(item, "fill"));
                std::string arrow = canvas->itemcget(item, "arrow");
                command.flags |= (arrow == "first" || arrow == "both") ? 2 : 0;
                command.flags |= (arrow == "last" || arrow == "both") ? 4 : 0;
            } else {
                command.outline = color(canvas->itemcget(item, "outline"));
                command.fill = color(canvas->itemcget(item, "fill"));
            }
            if (!canvas->itemcget(item, "dash").empty()) command.flags |= 1;
        }
        batch.commands.push_back(command);
    }
    return batch;
}

}  // namespace

void reset_native_failure(tk::Canvas* canvas) {
    auto found = registry().find(canvas);
    if (found == registry().end()) return;
    found->second.failed_backend.reset();
    if (found->second.viewport) found->second.viewport->failed_request.reset();
}

void close_native(tk::Canvas* canvas) {
    CanvasNative& state = registry()[canvas];
    if (state.commit_job) {
        try {
            canvas->after_cancel(*state.commit_job);
        } catch (const tk::TclError&) {
        }
        state.commit_job.reset();
    }
    if (state.viewport) state.viewport->close();
    state.viewport.reset();
    state.pending = false;
    if (auto* s = canvas_state(canvas)) { s->_native_pick = nullptr; s->_native_world = nullptr; s->_native_depth = nullptr; }
}

void flush_native(tk::Canvas* canvas) {
    CanvasNative& state = registry()[canvas];
    if (state.commit_job) {
        try {
            canvas->after_cancel(*state.commit_job);
        } catch (const tk::TclError&) {
        }
        state.commit_job.reset();
    }
    auto viewport = state.viewport;
    if (!viewport || viewport->closed || !state.pending) return;
    try {
        OverlayBatch batch = overlay_commands(canvas, state.background_items);
        static NativeOverlay none{};
        viewport->check("ms3d_overlay", [&] {
            return viewport->api->overlay(viewport->handle, batch.commands.empty() ? &none : batch.commands.data(), static_cast<uint32_t>(batch.commands.size()),
                                          viewport->error.data(), ERROR_SIZE);
        });
        viewport->show(canvas->winfo_ismapped());
        registry()[canvas].pending = false;
    } catch (const std::runtime_error& exc) {  // TclError, NativeUnavailable, OSError
        std::string backend = state.requested_backend.value_or(viewport->backend);
        close_native(canvas);
        CanvasNative& after = registry()[canvas];
        after.failed_backend = backend; after.failed_reason = exc.what();
        throw;
    } catch (const ValueError& exc) {
        std::string backend = state.requested_backend.value_or(viewport->backend);
        close_native(canvas);
        CanvasNative& after = registry()[canvas];
        after.failed_backend = backend; after.failed_reason = exc.what();
        throw;
    }
}

ViewportResult paint_native(tk::Canvas* canvas, const Request& request) {
    const std::string& backend = request.backend;
    {
        CanvasNative& state = registry()[canvas];
        auto items = canvas->find_all();
        state.background_items = std::set<int>(items.begin(), items.end());
        state.requested_backend = backend;
        if (state.failed_backend && *state.failed_backend == backend) {
            ViewportResult failed;
            failed.ok = false; failed.engine = "cpu"; failed.error = state.failed_reason;
            return failed;
        }
        state.failed_backend.reset();
    }
    std::shared_ptr<NativeViewport> viewport = registry()[canvas].viewport;
    std::shared_ptr<NativeViewport> candidate;
    auto failure = [&](const std::exception& exc) {
        if (candidate) candidate->close();
        close_native(canvas);
        CanvasNative& state = registry()[canvas];
        state.failed_backend = backend; state.failed_reason = exc.what();
        ViewportResult result;
        result.ok = false; result.engine = "cpu"; result.error = exc.what();
        return result;
    };
    try {
        attach(canvas);
        ViewportResult result;
        if (!viewport || viewport->closed || (viewport->backend != backend && viewport->failed_request != std::optional<std::string>(backend))) {
            try {
                candidate = std::make_shared<NativeViewport>(canvas, backend);
                result = candidate->frame(request);
            } catch (const std::runtime_error& exc) {  // NativeUnavailable, OSError, RuntimeError
                if (candidate) candidate->close();
                candidate.reset();
                if (!viewport || viewport->closed) throw;
                viewport->failed_request = backend;
                viewport->warning = backend + ": " + exc.what() + "; zachowano " + viewport->backend + ".";
                result = viewport->frame(request);
            } catch (const ValueError& exc) {
                if (candidate) candidate->close();
                candidate.reset();
                if (!viewport || viewport->closed) throw;
                viewport->failed_request = backend;
                viewport->warning = backend + ": " + exc.what() + "; zachowano " + viewport->backend + ".";
                result = viewport->frame(request);
            }
            if (candidate) {
                if (viewport) viewport->close();
                viewport = candidate;
                registry()[canvas].viewport = viewport;
                candidate.reset();
            }
        } else {
            if (viewport->backend == backend) { viewport->failed_request.reset(); viewport->warning.reset(); }
            result = viewport->frame(request);
        }
        if (auto* s = canvas_state(canvas)) {
            s->_depth_buffer.reset(); s->_id_buffer.reset();
            s->_native_pick = [viewport](double x, double y) -> std::optional<std::string> {
                json hit = viewport->hit(x, y);
                if (hit.is_null()) return std::nullopt;
                return hit.at("id").get<std::string>();
            };
            s->_native_world = [viewport](double x, double y) { return viewport->hit(x, y, 1); };
            s->_native_depth = [viewport](double x, double y) { return viewport->hit(x, y); };
            s->_render_diagnostics = result.diagnostics;
        }
        // canvas._active_renderer = 'native' ustawia App::draw na podstawie result.engine
        CanvasNative& state = registry()[canvas];
        state.pending = true;
        if (!state.commit_job) {
            state.commit_job = canvas->after_idle([canvas] {
                CanvasNative& current = registry()[canvas];
                current.commit_job.reset();
                try {
                    flush_native(canvas);
                } catch (const std::runtime_error& exc) {  // NativeUnavailable, TclError, OSError
                    registry()[canvas].error = exc.what();
                    auto recover = registry()[canvas].recover;
                    if (recover) recover();
                } catch (const ValueError& exc) {
                    registry()[canvas].error = exc.what();
                    auto recover = registry()[canvas].recover;
                    if (recover) recover();
                }
            });
        }
        return result;
    } catch (const std::runtime_error& exc) {
        return failure(exc);
    } catch (const ValueError& exc) {
        return failure(exc);
    }
}

void install(App* app) {
    // Commit overlays after the final editor draw wrapper, on the Tk thread.
    app->draw_wrapper = [app] {
        app->draw_original();
        try {
            flush_native(app->canvas);
        } catch (const std::runtime_error& exc) {  // NativeUnavailable, TclError, OSError
            if (app->log) app->log->warning(std::string("Nakładka silnika C++: ") + exc.what());
            std::string saved = app->viewport_engine; app->viewport_engine = "cpu";
            try { app->draw_original(); } catch (...) { app->viewport_engine = saved; throw; }
            app->viewport_engine = saved;
        } catch (const ValueError& exc) {
            if (app->log) app->log->warning(std::string("Nakładka silnika C++: ") + exc.what());
            std::string saved = app->viewport_engine; app->viewport_engine = "cpu";
            try { app->draw_original(); } catch (...) { app->viewport_engine = saved; throw; }
            app->viewport_engine = saved;
        }
    };
    registry()[app->canvas].recover = [app] {
        std::string saved = app->viewport_engine; app->viewport_engine = "cpu";
        try { app->draw_original(); } catch (...) { app->viewport_engine = saved; throw; }
        app->viewport_engine = saved;
    };
}

}  // namespace native_viewport
