// Frozen desktop entry point and offline diagnostic mode.
#include "launcher.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <typeinfo>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

#include "app.h"
#include "cad_geometry.h"
#include "configurator_accessories.h"
#include "corner_furniture.h"
#include "depth_raster.h"
#include "drag_hardware.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "furniture_library.h"
#include "mechanisms.h"
#include "opening_options.h"
#include "project_print.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "responsive_ui.h"
#include "selftest.h"
#include "room_architecture.h"
#include "room_objects.h"
#include "runner_installation.h"
#include "runners_catalog.h"
#include "shaped_furniture.h"
#include "sketch.h"
#include "ui_preferences.h"
#include "viewport_modes.h"
#include "viewport_render.h"
#include "viewport_scene.h"
#include "visual_catalogue.h"
#include "workspace_worktops.h"

namespace launcher {

namespace fs = std::filesystem;
using core::json;
using core::Part;
using core::Project;
using core::ProjectPtr;

namespace {

struct AssertionError : std::runtime_error { using std::runtime_error::runtime_error; };

#define ASSERT(condition) \
    do { if (!(condition)) throw AssertionError(std::string("assert ") + #condition + " (launcher.cpp:" + std::to_string(__LINE__) + ")"); } while (0)

// tempfile.TemporaryDirectory(prefix=...)
class TempDir {
public:
    explicit TempDir(const std::string& prefix = "tmp") {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        for (int attempt = 0; attempt < 100; ++attempt) {
            fs::path candidate = fs::temp_directory_path() / (prefix + std::to_string(gen() % 100000000));
            if (fs::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("Nie udało się utworzyć katalogu tymczasowego.");
    }
    ~TempDir() { cleanup(); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    void cleanup() {
        std::error_code ignored;
        if (!path.empty()) fs::remove_all(path, ignored);
        path.clear();
    }
    fs::path path;
};

// traceback.format_exc() — C++ nie ma śladu stosu: nazwa typu wyjątku i komunikat
std::string describe(const std::exception& error) {
    std::string name = typeid(error).name();
#ifdef __GNUC__
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled) name = demangled;
    std::free(demangled);
#endif
    return name + ": " + error.what() + "\n";
}

// next(x for x in items if predicate) — StopIteration, gdy brak
template <class T, class F>
T& first_where(std::vector<T>& items, F predicate) {
    for (auto& item : items) if (predicate(item)) return item;
    throw py::StopIteration();
}

double volume(const TopoDS_Shape& shape) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

std::map<std::string, double> amounts(const Project& project, double value) {
    std::map<std::string, double> out;
    for (const auto& p : project.parts) out[p.id] = value;
    return out;
}

constexpr double PI = 3.14159265358979323846;

}  // namespace

int self_test(const std::string& path) {
    json result = {{"version", "0.15"}, {"frozen", true}, {"checks", json::array()}};
    auto note = [&](const std::string& text) { result["checks"].push_back(text); };
    App* app = nullptr;
    std::unique_ptr<TempDir> diagnostic_settings;
    std::optional<std::function<fs::path()>> original_preference_path;
    try {
        Project p;
        Part part;
        part.u = 200; part.v = 120; part.t = 20; part.material = p.materials[0].id;
        part.chamfers = json::array({json{{"edge", "top:0"}, {"distance", 2}}});
        part.machining = json::array({json{{"type", "pocket"}, {"shape", "rect"}, {"u", 100}, {"v", 60}, {"length", 40}, {"width", 20}, {"depth", 5}, {"face", "top"}}});
        p.parts = {part};
        p.validate();
        ASSERT(volume(cad_geometry::local_shape(p.parts[0])) < 480000);
        note("CAD built in: chamfer and pocket");
        {
            TempDir d;
            cad_geometry::step_export(p, py::path_str(d.path / "test.step"));
            ASSERT(fs::file_size(d.path / "test.step") > 100);
            note("STEP export");
        }
        auto [cabinet_created, cabinet_group] = furniture_library::create_furniture(Project(), 0);
        ProjectPtr cabinet = fittings::cabinet_connectors(*cabinet_created, cabinet_group);
        cabinet->validate();
        note("Mating confirmat drills and countersink");
        {
            std::string side_id = first_where(cabinet->parts, [](const Part& q) { return q.name == "Bok lewy"; }).id;
            cabinet = fittings::system32(*cabinet, side_id);
            note("System 32");
        }
        for (size_t i = 0; i < furniture_library::TEMPLATES.size(); ++i) {
            auto [project, group] = furniture_library::create_furniture(Project(), (long long)i);
            ProjectPtr posed = mechanisms::posed_project(project, amounts(*project, .75));
            ASSERT(posed->parts.size() == project->parts.size());
        }
        note("All " + std::to_string(furniture_library::TEMPLATES.size()) + " furniture templates and opening transforms");
        {
            auto [drawers_created, group] = furniture_library::create_furniture(Project(), 3);
            std::string front_id = first_where(drawers_created->parts, [](const Part& q) { return q.role == "drawer"; }).id;
            ProjectPtr drawers = runner_installation::apply_runner(*drawers_created, front_id, "blum_movento_760h", 400);
            json plan = first_where(drawers->hardware.get_ref<json::array_t&>(), [](const json& h) { return py::get(h, "runner_profile") == "blum_movento_760h"; }).at("dimensions");
            ASSERT(plan.at("external_clearance_per_side") == 5 && plan.at("drawer_outer_width") == 554);
            note("Runner catalogue: " + std::to_string(runners_catalog::PROFILES.size()) + " profiles; synchronized MOVENTO drawer");
        }
        {
            std::string door_id = first_where(cabinet->parts, [](const Part& q) { return q.role == "door"; }).id;
            ProjectPtr opened = opening_options::install(*cabinet, door_id, "tip_bumper", json{{"hinge_spring", "sprung"}});
            bool found = false;
            for (const auto& q : opened->parts)
                for (const auto& o : q.operations)
                    if (py::get(o, "opening_owner") == door_id && o.at("diameter") == 10) found = true;
            ASSERT(found);
            note("Handle alternatives and real ejector drilling");
        }
        {
            for (size_t i = 0; i < shaped_furniture::TEMPLATES.size(); ++i) {
                auto [model, group] = shaped_furniture::create_shaped_furniture(Project(), (long long)i);
                model->validate();
            }
            ProjectPtr disc = std::make_shared<Project>();
            Part circle;
            circle.u = 400; circle.v = 400; circle.t = 28; circle.material = disc->materials[0].id;
            disc->parts = {circle};
            std::string disc_id = disc->parts[0].id;
            disc = shaped_furniture::apply_shape(*disc, disc_id, "circle");
            ASSERT(std::abs(volume(cad_geometry::local_shape(disc->parts[0])) - PI * 200 * 200 * 28) < .01);
            note(std::to_string(shaped_furniture::TEMPLATES.size()) + " shaped furniture templates and exact circular CAD geometry");
            disc->parts[0].fillets = json::array({json{{"edge", "top:0"}, {"radius", 3}}});
            disc->parts[0].chamfers = json::array({json{{"edge", "bottom:0"}, {"distance", 2}}});
            disc->validate();
            ASSERT(volume(cad_geometry::local_shape(disc->parts[0])) < PI * 200 * 200 * 28);
            note("Exact circular rim fillet and chamfer");
        }
        ProjectPtr loaded;
        {
            auto [first_project, first] = furniture_library::create_furniture(Project(), 0, std::nullopt, std::nullopt, std::nullopt, 18, {0, 0, 0}, std::string("A"));
            auto [second_project, second] = furniture_library::create_furniture(*first_project, 0, std::nullopt, std::nullopt, std::nullopt, 18, {900, 0, 0}, std::string("B"));
            ProjectPtr placed = furniture_layout::rotate_group(*second_project, second, 90);
            placed = furniture_layout::snap_group(*placed, second, first, "x+");
            ASSERT(std::abs(furniture_layout::group_bounds(*placed, first).second[0] - furniture_layout::group_bounds(*placed, second).first[0]) < .001);
            loaded = Project::from_data(placed->data());
            ASSERT(loaded->data() == placed->data());
            ProjectPtr posed = mechanisms::posed_project(loaded, amounts(*loaded, .75));
            ASSERT(posed->parts.back().layout == loaded->parts.back().layout);
            note("Rigid layout, rotation, six-direction magnetic placement and roundtrip");
        }
        {
            TempDir d;
            fs::path book = d.path / "complete.html";
            json info = project_print::export_project_print(loaded, py::path_str(book));
            ASSERT(info.at("groups").size() == 2 && info.at("parts") == loaded->parts.size());
            ASSERT(py::read_text(py::path_str(book)).find("window.print()") != std::string::npos);
            note("Dimensioned set and exploded cabinet print book with cutting plans");
        }
        diagnostic_settings = std::make_unique<TempDir>("MebelStudio3D-diagnostic-");
        original_preference_path = ui_preferences::preference_path;
        {
            fs::path settings_file = diagnostic_settings->path / "ui_preferences.json";
            ui_preferences::preference_path = [settings_file] { return settings_file; };
        }
        ui_preferences::save_preferences(py::merged(ui_preferences::DEFAULTS, json{{"engine", "cpu"}}));
        app = new App();
        app->withdraw(); app->update();
        ASSERT(app->canvas->winfo_exists());
        {
            auto* editor = new sketch::Sketch(app, nullptr);
            editor->fields["name"].set("Offline unfinished sketch");
            app->update();
            tk::Canvas* canvas = editor->canvas;
            app->workspace->detach(editor); app->update(); app->workspace->dock(editor); app->update();
            ASSERT(editor->canvas == canvas && editor->fields["name"].get() == "Offline unfinished sketch");
            tk::Widget* main_page = app->design_page;
            app->workspace->detach(main_page); app->update();
            std::string callback = app->call({"wm", "protocol", main_page->path(), "WM_DELETE_WINDOW"});
            app->call({callback});
            app->update();
            ASSERT(!app->workspace->detached(main_page) && app->grab_current() == nullptr);
            form_dialog::Form* form = app->dialog("Offline form", "520x600");
            app->field(form, "Value", 1);
            app->update();
            ASSERT(app->grab_current() == nullptr);
            form->destroy();
            app->update();
            note("Detached 3D native Close and non-modal clipped form tabs");
        }
        ProjectPtr model = std::make_shared<Project>();
        std::string group;
        {
            json cfg = furniture_configurator::default_config(3, *model);
            cfg["leg_count"] = 6;
            std::tie(model, group) = furniture_configurator::configure(*model, cfg);
            long long drawers = 0;
            for (const auto& q : model->parts) if (q.role == "drawer") ++drawers;
            ASSERT(drawers == 3);
            ASSERT(first_where(model->hardware.get_ref<json::array_t&>(), [](const json& h) { return py::get(h, "kind") == "leg"; }).at("qty") == 6);
            auto vertices = configurator_accessories::accessory_vertices(*model);
            if (vertices.empty()) throw py::ValueError("min() iterable argument is empty");
            double lowest = vertices[0][2];
            for (const auto& v : vertices) lowest = std::min(lowest, v[2]);
            ASSERT(lowest == 0);
            cfg["width"] = 800;
            std::tie(model, group) = furniture_configurator::configure(*model, cfg, group);
            ASSERT(py::truthy(furniture_configurator::configuration_status(*model, group).at("editable")));
            note("Unified configurator: three drawers, six purchased legs and atomic resize");
        }
        {
            app->workspace->detach(app->design_page); app->update();
            quick_studio_ui::focus_section(app, "fronts"); app->update();
            ASSERT(app->workspace->detached(app->design_page) && app->grab_current() == nullptr);
            app->workspace->dock(app->design_page); app->update();
            note("Creator settings remain responsive with detached 3D");
        }
        ProjectPtr equipped;
        {
            equipped = room_objects::add_object(*model, "washer", json{{"x", 900}, {"price", 1000}});
            equipped = room_objects::add_object(*equipped, "lamp", json{{"x", 400}, {"z", 1800}});
            equipped = room_objects::set_scene(*equipped, json{{"brightness", .35}, {"led_enabled", true}});
            ASSERT(Project::from_data(equipped->data())->data() == equipped->data());
            ASSERT(equipped->parts.size() == model->parts.size() && equipped->costs().at("hardware") >= 1000);
            note("Purchased room objects, lamp emitter, pricing and persisted dark scene");
        }
        {
            ProjectPtr test = std::make_shared<Project>();
            json options = furniture_configurator::default_config(0, *test);
            options["width"] = 600; options["worktop"] = false;
            std::string body, top, corner_group;
            std::tie(test, body) = furniture_configurator::configure(*test, options);
            std::tie(test, top) = workspace_worktops::install_shared_worktop(*test, {body});
            ASSERT(workspace_worktops::read_shared_worktop(*test, top).at("status") == "ready");
            ASSERT(furniture_configurator::read_config(*test, body).at("top_mode") == "rails");
            json corner = corner_furniture::default_corner_config(test.get());
            corner["side"] = "right"; corner["shape"] = "rounded"; corner["origin"] = json::array({900, 0, 0});
            std::tie(test, corner_group) = corner_furniture::create_corner(*test, corner);
            test->validate();
            bool rounded = false;
            for (const auto& q : test->parts) if (q.group == corner_group && q.contour.size() > 6) rounded = true;
            ASSERT(rounded);
            note("Real reinforcing rails, linked worktop and rounded right corner contour");
        }
        {
            core::Drawn rendered = core::render_faces(*equipped, app->camera, 900, 600, std::nullopt, 0, true);
            bool scene_object = false;
            for (const auto& f : rendered) if (py::truthy(py::get(f, "scene_object"))) scene_object = true;
            ASSERT(scene_object);
            json repeated = json::array();
            size_t times = 800 / std::max<size_t>(1, rendered.size()) + 1;
            for (size_t i = 0; i < times; ++i) for (const auto& f : rendered) repeated.push_back(f);
            viewport_scene::PaintFacesOptions paint_options;
            paint_options.camera = app->camera;
            paint_options.size = {900, 600};
            viewport_scene::paint_faces(app->canvas, repeated, equipped, paint_options);
            ASSERT(!app->canvas->find_withtag("viewport.raster_model").empty());
            note("Offline room geometry, lighting and raster-capable viewport");
        }
        {
            viewport_render::clear_caches();
            for (int index = 0; index < 12; ++index) {
                ProjectPtr altered = room_objects::set_scene(*equipped, json{{"brightness", .3 + index * .08}});
                core::render_faces(*altered, app->camera, 900, 600, std::nullopt, 0, true);
                core::render_faces(*altered, app->camera, 900, 600, std::nullopt, 0, false);
            }
            note("Cold interactive to full geometry survives repeated scene mutations without color-cache mismatch");
        }
        {
            depth_raster::DepthRaster depth({120, 80}, {"front", "rear"});
            depth.polygon({{5, 5}, {115, 5}, {115, 75}, {5, 75}}, {10, 10, 10, 10}, "#e86240", "front");
            depth.polygon({{5, 5}, {115, 5}, {115, 75}, {5, 75}}, {0, 0, 0, 0}, "#4090e8", "rear");
            ASSERT(depth.ids.at(static_cast<size_t>(depth.owner.at(40, 60))) == "front");
            note("Opaque surfaces use per-pixel depth independent of polygon draw order");
        }
        {
            const Part& leaf = first_where(model->parts, [](const Part& q) { return q.role == "drawer"; });
            json hit = {{"u", leaf.u / 2}, {"v", leaf.v / 2}, {"w", 0}, {"face", "bottom"}};
            ProjectPtr fitted = drag_hardware::attach(*model, "led", leaf.id, hit, json{{"width", 12}, {"margin", 30}});
            bool led = false;
            for (const auto& h : fitted->hardware) if (py::truthy(py::get(h, "manual")) && py::get(h, "kind") == "led_strip") led = true;
            ASSERT(led);
            fitted = room_objects::set_scene(*fitted, json{{"architecture", json{{"enabled", true}}}});
            ASSERT(py::truthy(room_architecture::world_faces(*fitted, app->camera)) && Project::from_data(fitted->data())->data() == fitted->data());
            note("Manual LED fitting, automatic length and persisted room walls are available offline");
        }
        {
            auto* doc = app->open_developer_logs();
            app->update();
            ASSERT(doc->winfo_exists());
            doc->destroy();
            app->update();
            ASSERT(fs::is_regular_file(app->developer_log_path));
            note("Developer log tab closes and rotating disk log is available");
        }
        json before;
        {
            app->project = model; app->selected = model->parts[0].id; app->refresh(); app->fit(); app->update();
            before = app->project->data();
            bool dirty = app->dirty;
            json prefs = app->ui_layout->preferences();
            prefs["ui_scale"] = 1.2; prefs["inspector_width"] = 270; prefs["engine"] = "cpu";
            app->ui_layout->apply(prefs, false);
            app->update();
            auto* settings = responsive_ui::interface_settings(app);
            app->update();
            ASSERT(settings->interface_vars.at("ui_scale").get().rfind("120%", 0) == 0);
            settings->destroy();
            app->ui_layout->show_inspector(false);
            app->corner_dialog();
            app->update();
            ASSERT(py::truthy(app->ui_layout->preferences().at("show_inspector")));
            ASSERT(app->project->data() == before && app->dirty == dirty);
            note("Responsive panels and arbitrary saved UI scale preserve the furniture project");
        }
        {
            ASSERT(visual_catalogue::catalogue_items("furniture", *app->project).size() == furniture_library::TEMPLATES.size());
            ASSERT(visual_catalogue::catalogue_items("runner", *app->project).size() >= 10);
            ASSERT(!visual_catalogue::catalogue_items("leg", *app->project).empty() && !visual_catalogue::catalogue_items("appliance", *app->project).empty());
            auto* catalogue = visual_catalogue::show(app, "leg");
            app->update();
            auto* same = visual_catalogue::show(app, "runner");
            app->update();
            ASSERT(catalogue == same && app->grab_current() == nullptr);
            catalogue->form->destroy();
            app->workspace->select(app->design_page);
            app->update();
            note("Graphic furniture, fitting, leg and room catalogues share one non-modal tab");
        }
        {
            std::set<std::string> ids = viewport_modes::xray_part_ids(app->project, true, "Cały zestaw", app->selected);
            std::set<std::string> visible;
            for (const auto& q : app->project->parts) if (q.visible) visible.insert(q.id);
            ASSERT(ids == visible);
            app->xray.set(true); app->xray_scope.set("Cały zestaw"); app->draw(); app->update();
            app->xray.set(false); app->show_drills.set(true); app->drill_mode.set("Także zasłonięte"); app->draw(); app->update();
            ASSERT(app->project->data() == before);
            note("Whole-body X-ray and highlighted mounting-hole overlays preserve exact geometry");
        }
        {
            app->viewport_engine = "gpu"; app->viewport_quality = "balanced"; app->draw(); app->update();
            result["gpu"] = {{"engine", app->canvas->_active_renderer},
                             {"error", app->canvas->_gpu_error ? json(*app->canvas->_gpu_error) : json(nullptr)},
                             {"diagnostics", app->canvas->_render_diagnostics}};
            if (app->canvas->_active_renderer == "gpu") {
                bool any_id = false, any_depth = false;
                for (int32_t v : app->canvas->_id_buffer.value().data) if (v >= 0) { any_id = true; break; }
                for (float v : app->canvas->_depth_buffer.value().data) if (std::isfinite(v)) { any_depth = true; break; }
                ASSERT(any_id && any_depth);
                app->viewport_quality = "high"; app->draw(); app->update();
                ASSERT(app->canvas->_active_renderer == "gpu");
            } else {
                ASSERT(app->canvas->_gpu_error && !app->canvas->_gpu_error->empty());
            }
            app->viewport_engine = "cpu"; app->draw(); app->update();
            ASSERT(app->canvas->_active_renderer == "cpu" && app->project->data() == before);
            note("Bundled OpenGL view, high-quality mode and controlled CPU fallback");
        }
        app->ui_layout->persist();
        ASSERT(fs::is_regular_file(ui_preferences::preference_path()));
        app->destroy(); app = nullptr;
        note("Tkinter startup and retained dockable sketch state");
        result["ok"] = true;
    } catch (const std::exception& error) {
        result["ok"] = false;
        result["error"] = describe(error);
    }
    // finally:
    if (app != nullptr) {
        try { app->destroy(); } catch (...) {}
    }
    if (original_preference_path) ui_preferences::preference_path = *original_preference_path;
    if (diagnostic_settings) diagnostic_settings->cleanup();
    std::string expanded = path;
    if (!expanded.empty() && expanded[0] == '~') expanded = py::path_str(py::home()) + expanded.substr(1);
    fs::path report_path = fs::weakly_canonical(fs::absolute(py::to_path(expanded)));
    fs::create_directories(report_path.parent_path());
    py::write_text(py::path_str(report_path), py::dumps(result, false, 2));
    return py::truthy(result.at("ok")) ? 0 : 1;
}

}  // namespace launcher

#ifdef MEBEL_LAUNCHER
int main(int argc, char** argv) {
    ARGV.assign(argv, argv + argc);
    if (argc > 2 && std::string(argv[1]) == "--self-test") return launcher::self_test(argv[2]);
    if (argc > 1 && std::string(argv[1]) == "--full-test") {
        std::string dir;
        bool pause = false;
        for (int i = 2; i < argc; ++i) { if (std::string(argv[i]) == "--pause") pause = true; else dir = argv[i]; }
        return selftest::run_cli(dir, pause);
    }
    try {
        App().mainloop();
    } catch (const std::exception& exc) {
        if (launcher::native_host) throw;
        std::string error = launcher::describe(exc);
        tk::messagebox::showerror("Błąd uruchomienia", error.substr(error.size() > 2000 ? error.size() - 2000 : 0));
        return 1;
    }
    return 0;
}
#endif
