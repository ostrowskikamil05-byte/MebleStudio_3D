// Synchronized dimension controls and debounced 3D sketch preview.
#include "sketch_live.h"

#include <algorithm>
#include <format>

#include "app.h"
#include "core.h"
#include "gpu_viewport.h"
#include "graphics_mode_ui.h"
#include "native_viewport.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "shaped_furniture.h"
#include "sketch.h"

namespace sketch_live {

using sketch::Sketch;
using core::ValueError;
using Cmd = std::function<void()>;
using Point = core::Point;

namespace {
std::string g(double x) { return std::format("{:g}", x); }
}  // namespace

void install(Sketch* s) {
    tk::Widget* body = s->canvas->master();
    s->preview_job.reset();
    s->size_vars.clear();
    s->auto_fit = tk::BooleanVar(false);
    auto* row = new ttk::Frame(s, {{"padding", "8 3"}});
    row->pack({{"fill", "x"}, {"before", body->path()}});
    (new ttk::Label(row, {{"text", "Wymiary szkicu = wymiary 3D [mm]"}}))->pack({{"side", "left"}, {"padx", 5}});
    for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"u", "U"}, {"v", "V"}}) {
        (new ttk::Label(row, {{"text", label}}))->pack({{"side", "left"}});
        tk::StringVar var("0");
        s->size_vars.insert_or_assign(key, var);
        auto* entry = new ttk::Entry(row, {{"textvariable", var}, {"width", 10}});
        entry->pack({{"side", "left"}, {"padx", 4}});
        entry->bind("<Return>", [s](tk::Event&) { apply_sizes(s); return std::string(); });
    }
    (new ttk::Button(row, {{"text", "Zmień wymiary 2D i 3D"}, {"command", Cmd([s] { apply_sizes(s); })}}))->pack({{"side", "left"}, {"padx", 5}});
    (new ttk::Checkbutton(row, {{"text", "Dopasuj po wyborze roli"}, {"variable", *s->auto_fit}}))->pack({{"side", "left"}, {"padx", 8}});
    auto* panel = new ttk::Frame(body, {{"width", 255}, {"padding", 8}});
    panel->pack({{"side", "left"}, {"fill", "y"}, {"before", s->canvas->path()}});
    panel->pack_propagate(false);
    (new ttk::Label(panel, {{"text", "Podgląd 3D na żywo"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}});
    graphics_mode_ui::add_compact(panel, s->app, "okno szkicownika");
    graphics_mode_ui::add_listener([s] {
        try {
            if (!s->winfo_exists()) return false;
            schedule(s);  // podgląd 3D na żywo przerysuje się w nowym trybie
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
    s->live_canvas = new quick_studio_ui::StudioCanvas(panel, {{"bg", "#eef1f5"}, {"width", 239}, {"height", 350}, {"highlightthickness", 0}});
    s->live_canvas->pack({{"fill", "both"}, {"expand", true}, {"pady", 8}});
    s->live_note = tk::StringVar();
    (new ttk::Label(panel, {{"textvariable", s->live_note}, {"wraplength", 225}}))->pack({{"fill", "x"}, {"pady", 8}});
    s->live_canvas->bind("<Configure>", [s](tk::Event&) { schedule(s); return std::string(); });
    s->viewport_preferences_changed = [s] {
        native_viewport::reset_native_failure(s->live_canvas);
        schedule(s);
    };
    s->fields.at("t").trace_add("write", [s] { schedule(s); });
    s->level.trace_add("write", [s] { schedule(s); });
    s->hinge_side.trace_add("write", [s] { schedule(s); });
    s->automatic.trace_add("write", [s] { schedule(s); });
    s->target.trace_add("write", [s] { schedule(s); });
    s->role.trace_add("write", [s] { schedule(s); });
    sync(s);
    schedule(s);
}

void sync(Sketch* s) {
    if (s->size_vars.empty()) return;
    if (!s->points.empty()) {
        double umin = s->points[0][0], umax = umin, vmin = s->points[0][1], vmax = vmin;
        for (auto& p : s->points) {
            umin = std::min(umin, p[0]); umax = std::max(umax, p[0]);
            vmin = std::min(vmin, p[1]); vmax = std::max(vmax, p[1]);
        }
        s->size_vars.at("u").set(g(umax - umin));
        s->size_vars.at("v").set(g(vmax - vmin));
    } else {
        for (auto& [key, var] : s->size_vars) var.set("0");
    }
    schedule(s);
}

void apply_sizes(Sketch* s) {
    try {
        if (!s->closed) throw ValueError("Zamknij kontur przed zmianą jego wymiarów.");
        double u = core::finite(s->number(s->size_vars.at("u")), .1, 10000);
        double v = core::finite(s->number(s->size_vars.at("v")), .1, 10000);
        double min_u = s->points[0][0], min_v = s->points[0][1], max_u = min_u, max_v = min_v;
        for (auto& p : s->points) {
            min_u = std::min(min_u, p[0]); max_u = std::max(max_u, p[0]);
            min_v = std::min(min_v, p[1]); max_v = std::max(max_v, p[1]);
        }
        double old_u = max_u - min_u, old_v = max_v - min_v;
        if (py::truthy(s->part.profile) && s->points == s->part.points()) {
            core::Part part = s->part;
            shaped_furniture::resize_profile(part, u, v);
            part.u = u;
            part.v = v;
            part.validate();
            s->checkpoint();
            s->part = part;
            s->points = part.points();
        } else {
            s->checkpoint();
            if (old_u == 0 || old_v == 0) throw std::domain_error("float division by zero");
            for (auto& p : s->points) p = {min_u + (p[0] - min_u) * u / old_u, min_v + (p[1] - min_v) * v / old_v};
        }
        s->update_list();
        s->fit();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Wymiary szkicu", e.what(), s);
    }
}

void schedule(Sketch* s) {
    if (s->live_canvas == nullptr) return;
    if (s->preview_job) {
        try {
            s->after_cancel(*s->preview_job);
        } catch (const tk::TclError&) {
        }
    }
    s->preview_job = s->after(160, [s] { preview(s); });
}

void preview(Sketch* s, bool force_cpu) {
    s->preview_job.reset();
    quick_studio_ui::StudioCanvas* c = s->live_canvas;
    c->delete_("all");
    int w = c->winfo_width(), h = c->winfo_height();
    if (w < 10) return;
    if (!s->closed) {
        native_viewport::close_native(c);
        gpu_viewport::close_gpu(c);
        s->live_note.set("Narysuj prostokąt lub zamknij wielokąt. Podgląd przeliczy położenie, wymiary i okucia.");
        return;
    }
    auto invalid = [&](const std::exception& e) {
        native_viewport::close_native(c);
        gpu_viewport::close_gpu(c);
        s->live_note.set(e.what());
        c->create_text(w / 2.0, h / 2.0, {{"text", "Sprawdź dopasowanie\ni parametry montażu"}, {"fill", "#a2542c"}, {"justify", "center"}});
    };
    try {
        auto [project, id] = s->candidate();
        const core::Part& p = sketch::find_part(*project, id);
        core::Camera camera;
        camera.fit(*project, w, h);
        std::string engine = force_cpu ? "cpu" : s->app->viewport_engine;
        std::string quality = s->app->viewport_quality;
        std::optional<native_viewport::ViewportResult> result;
        if (engine == "auto" || engine == "d3d11" || engine == "d3d12" || engine == "vulkan") {
            result = native_viewport::paint_native(c, {.project = project, .backend = engine, .camera = camera, .size = {w, h}, .interactive = false,
                                                       .selected_ids = {}, .selected = p.id, .explode = 0, .xray = false, .xray_ids = {}, .quality = quality});
            if (result->ok) gpu_viewport::close_gpu(c);
        } else native_viewport::close_native(c);
        if ((engine == "auto" || engine == "gpu") && !(result && result->ok)) {
            auto failed = [&](const std::exception& error) {
                result = native_viewport::ViewportResult{};
                result->error = error.what();
            };
            try {
                result = gpu_viewport::paint_gpu(c, {.project = project, .camera = camera, .size = {w, h}, .interactive = false,
                                                     .selected_ids = {}, .selected = p.id, .explode = 0, .xray = false, .xray_ids = {}, .quality = quality});
            } catch (const std::runtime_error& error) { failed(error); }  // ImportError, RuntimeError, OSError
            catch (const ValueError& error) { failed(error); }
        }
        if (result && result->ok) {
            if (result->engine == std::optional<std::string>("native")) native_viewport::flush_native(c);
            else c->_active_renderer = "gpu";
        } else {
            native_viewport::close_native(c);
            gpu_viewport::close_gpu(c);
            c->_active_renderer = "cpu";
            core::Drawn faces = core::render_faces(*project, camera, w, h, p.id);
            for (auto& face : faces) {
                std::vector<double> flat;
                for (auto& point : face.at("points")) {
                    flat.push_back(point.at(0).get<double>());
                    flat.push_back(point.at(1).get<double>());
                }
                bool mesh = py::truthy(py::get(face, "mesh"));
                std::string color = face.at("color").get<std::string>();
                std::string outline = mesh ? color : py::truthy(face.at("selected")) ? "#ed8a32" : "#66727f";
                c->create_polygon(flat, {{"fill", color}, {"outline", outline}, {"width", mesh ? 0 : 1}});
            }
        }
        std::vector<std::string> hardware;
        for (auto& x : project->hardware)
            if (py::get(x, "owner_id") == p.id) hardware.push_back(g(x.at("qty").get<double>()) + " × " + py::str(x.at("name")));
        std::string text = std::format("{} × {} × {} mm\n", g(p.u), g(p.v), g(p.t));
        if (!hardware.empty()) {
            text += "Okucia: ";
            for (size_t i = 0; i < hardware.size(); ++i) text += (i ? ", " : "") + hardware[i];
        } else text += "Element bez automatycznych okuć.";
        text += "\nZmiany zapiszesz przyciskiem Dodaj / zapisz.";
        s->live_note.set(text);
    } catch (const std::invalid_argument& e) {  // ValueError
        invalid(e);
    } catch (const std::out_of_range& e) {  // KeyError
        invalid(e);
    } catch (const py::StopIteration& e) {
        invalid(e);
    } catch (const std::runtime_error& e) {  // RuntimeError, OSError
        native_viewport::close_native(c);
        if (!force_cpu) {
            if (s->app->log) s->app->log->warning(std::format("Podgląd szkicu C++: {}; podgląd CPU.", e.what()));
            preview(s, true);
        } else s->live_note.set(e.what());
    }
}

}  // namespace sketch_live
