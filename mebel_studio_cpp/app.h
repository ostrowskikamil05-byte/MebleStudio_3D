// Offline desktop application. C++20.
#pragma once
#include <algorithm>
#include <array>
#include <source_location>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>
#include "pyutil.h"
#include "forma_shell.h"
#include "library_panel.h"
#include "perf_monitor.h"
#include "side_panels.h"
#include "sheet_nesting.h"
#include "view_style.h"
#include "viewport_hud.h"
#include "forma_theme.h"
#include "tk.h"            // tk::Tk, tk::StringVar, tk::DoubleVar, tk::BooleanVar, tk::Event, tk::Opts,
                           // tk::messagebox, tk::filedialog, tk::colorchooser
#include "ttk.h"           // ttk::Style, ttk::Button, ttk::Label, ttk::Combobox, ttk::Entry, ttk::Frame, ...
#include "sketch.h"
#include "assembly.h"
#include "direct_edit.h"
#include "core.h"
// Moduły importowane w Pythonie leniwie (wewnątrz metod):
#include "palette.h"
#include "developer_logs.h"
#include "dock_workspace.h"
#include "corner_quick_ui.h"
#include "drag_hardware.h"
#include "workspace_controls.h"
#include "studio_interaction.h"
#include "view_modes_ui.h"
#include "visual_catalogue.h"
namespace hardware_placement_ui { class HardwarePlacementPanel; }
#include "responsive_ui.h"
#include "native_viewport.h"
#include "furniture_configurator.h"
#include "native_document.h"
#include "quick_studio_ui.h"
#include "workspace_worktops.h"
#include "mechanisms.h"
#include "dimension_model.h"
#include "fittings.h"
#include "opening_options.h"
#include "viewport_scene.h"
#include "viewport_modes.h"
#include "gpu_viewport.h"
#include "viewport_annotations.h"
#include "shaped_furniture.h"
#include "stretch_choice.h"
#include "dev_tools.h"
#include "journal.h"
#include "cad_geometry.h"
#include "furniture_layout.h"
#include "furniture_layout_ui.h"
#include "room_scene_drag.h"
#include "room_scene_ui.h"
#include "form_dialog.h"
#include "edge_editor.h"
#include "production_docs.h"
#include "project_print.h"

namespace fs = std::filesystem;
using json = core::json;  // nlohmann::ordered_json — kolejność kluczy jak w dict

using sketch::Sketch;
using assembly::delete_with_connections;
using assembly::rebuild_dependents;
using direct_edit::resize_part;
using direct_edit::resize_furniture;
using core::Project;
using core::ProjectPtr;
using core::Part;
using core::Material;
using core::Camera;
using core::add_template;
using core::render_faces;
using core::world;
using core::csv_export;
using core::dxf_export;
using core::report_export;
using core::svg_view;
using core::cut_plan;
using core::uid;
using core::inside;
using core::manufacturing_export;
using core::explosion_shift;
using native_viewport::ViewportResult;
using native_document::HistoryStack;
using native_document::ListHistory;
using native_document::NativeHistoryView;

// Odpowiedniki wyjątków Pythona
using py::ValueError;
using py::IndexError;
using py::KeyError;
using py::OSError;
using py::TypeError;
using py::StopIteration;

inline std::vector<std::string> ARGV;  // sys.argv


inline const fs::path ROOT = py::executable_dir();

struct Handle {
    std::string face;
    double x = 0, y = 0;
    std::array<double, 2> vector{};
    std::optional<std::string> group;
};
struct EdgeHandle {
    cad_geometry::Edge edge;
    std::array<double, 2> a{}, b{};
};
struct DragEdit {
    Handle handle;
    ProjectPtr base;
    ProjectPtr candidate;
    std::optional<std::string> id;
    double last = 0;
};

class App : public tk::Tk {
public:
    // float(text.replace(',','.'))
    static double to_number(const std::string& text) { return py::to_float(py::replace_all(text, ",", ".")); }

    std::unique_ptr<native_document::NativeDocument> _native_document; bool _native_pending = false;
    int project_generation = 0;
    ProjectPtr project;
    std::map<std::string, double> open_amounts; std::optional<std::string> animation_job;
    tk::DoubleVar motion_value{0}; tk::BooleanVar motion_all{false}; tk::BooleanVar resize_step{false};
    std::optional<std::string> path; bool dirty = false;
    std::optional<std::string> selected;
    std::unique_ptr<HistoryStack> undo_stack = std::make_unique<ListHistory>();
    std::unique_ptr<HistoryStack> redo_stack = std::make_unique<ListHistory>();
    bool loading = false;
    tk::BooleanVar xray{false};
    tk::StringVar xray_scope{"Cały zestaw"}; tk::StringVar drill_mode{"Także zasłonięte"};
    ttk::Combobox* xray_scope_combo = nullptr; ttk::Combobox* drill_mode_combo = nullptr;  // view_modes_ui::install
    ui_settings::SettingsDialog* _interface_settings_document = nullptr;  // ui_settings::open_settings
    std::function<void()> corner_dialog, shared_worktop_dialog;  // workspace_controls::install
    std::string viewport_engine = "cpu"; std::string viewport_quality = "balanced";
    Camera camera; tk::BooleanVar explode{false}; tk::BooleanVar dimensions{true};
    tk::StringVar dimension_mode{"Wybrany mebel"}; tk::BooleanVar show_drills{false}; std::shared_ptr<room_scene_drag::SceneDrag> scene_drag;
    tk::StringVar edit_mode{"camera"}; std::vector<Handle> handles; std::vector<EdgeHandle> edge_handles; std::optional<DragEdit> drag_edit;
    tk::BooleanVar layout_magnet{true}; tk::DoubleVar layout_tolerance{80}; std::shared_ptr<furniture_layout_ui::LayoutDrag> layout_drag;
    tk::StringVar status{"Gotowy · wszystkie wymiary w milimetrach"};

    // Atrybuty tworzone przez quick_studio_ui::build
    quick_studio_ui::StudioCanvas* canvas = nullptr;
    ttk::Treeview* tree = nullptr;
    std::map<std::string, ttk::Treeview*> tables;
    ttk::Combobox* matcombo = nullptr;
    std::map<std::string, tk::StringVar> vars;
    tk::StringVar matvar; tk::BooleanVar grain; tk::BooleanVar grain_v; tk::BooleanVar visible;
    std::vector<tk::BooleanVar> edgevars;
    ttk::Label* costnote = nullptr; ttk::Label* cutlabel = nullptr;
    ttk::Combobox* sheetcombo = nullptr; tk::StringVar sheetvar;
    tk::Canvas* cutcanvas = nullptr;
    ttk::Notebook* inspector_tabs = nullptr;
    tk::Widget* design_page = nullptr;
    // quick_studio_ui::build: układ okna (nagłówek, wstążka, panele, paski narzędzi)
    ttk::Frame* header = nullptr; ttk::Frame* header_menus = nullptr; tk::StringVar header_title; tk::StringVar status_right; tk::StringVar status_x, status_y, status_z; ttk::Frame* library_panel = nullptr;
    ttk::Frame* props_page = nullptr; ttk::Frame* context_page = nullptr; ttk::Frame* context_body = nullptr;  // prawy panel: Właściwości albo panel kontekstowy narzędzia
    tk::StringVar context_title; tk::StringVar props_title;
    ttk::Panedwindow* workspace_pane = nullptr; ttk::Notebook* ribbon = nullptr;
    std::map<std::string, responsive_ui::WrapToolbar*> ribbon_pages; std::vector<responsive_ui::WrapToolbar*> responsive_toolbars;
    ttk::Panedwindow* main_pane = nullptr; ttk::Frame* inspector_frame = nullptr; ttk::Frame* view_frame = nullptr;
    responsive_ui::WrapToolbar* view_toolbar = nullptr; responsive_ui::WrapToolbar* detail_toolbar = nullptr;
    ttk::Combobox* view_tool_combo = nullptr; ttk::Frame* motion_toolbar = nullptr; ttk::Scale* motion_scale = nullptr;
    hardware_placement_ui::HardwarePlacementPanel* hardware_panel = nullptr; std::function<void()> toggle_hardware_tray;
    // Metody podłączane przez moduły studio_ui / advanced_ui / style_ui (jeszcze nieprzetłumaczone)
    std::function<void()> shape_dialog, library_dialog, style_library, palette_dialog, motion_dialog, fittings_dialog, fit_selected;

    // Atrybuty tworzone przez moduły install(...)
    std::shared_ptr<developer_logs::Logger> log;
    std::filesystem::path developer_log_path;
    std::shared_ptr<developer_logs::MemoryHandler> developer_memory;
    developer_logs::LogDocument* developer_log_document = nullptr;
    std::function<developer_logs::LogDocument*()> open_developer_logs;
    form_dialog::Form* _cpp_status_document = nullptr;  // migration_status.open_status
    std::vector<std::function<void()>> changed_observers;  // developer_logs.install owija app.changed
    dock_workspace::DockWorkspace* workspace = nullptr;
    ttk::Notebook* tabs = nullptr;
    responsive_ui::UiLayout* ui_layout = nullptr;
    quick_studio_ui::QuickInspector* quick_inspector = nullptr;
    corner_quick_ui::CornerPanel* corner_panel = nullptr;
    room_scene_ui::RoomScenePanel* room_scene_panel = nullptr;
    drag_hardware::HardwareDragController* hardware_drag = nullptr;
    // visual_catalogue.install
    std::function<visual_catalogue::VisualCatalogue*(const std::string&, visual_catalogue::OnSelect)> visual_catalogue_open;  // app.visual_catalogue(kind, on_select)
    visual_catalogue::VisualCatalogue* _visual_catalogue_panel = nullptr;
    std::map<std::string, ttk::Button*> catalogue_buttons;
    bool _catalogue_buttons_installed = false;
    library_panel::LibraryPanel* library = nullptr; std::function<void(const std::string&)> select_tool_page;  // biblioteka w lewym panelu; przełączanie stron panelu narzędzi
    std::function<std::optional<json>(double, double)> pick_attachment_target;  // hasattr(app, 'pick_attachment_target')
    std::function<void()> layout_dialog;                // furniture_layout_ui.install
    std::function<void()> print_project;                // furniture_layout_ui.install
    std::function<void()> refresh_inspector;            // quick_studio_ui: odświeża listę Inspektora, gdy jest widoczna
    std::function<void()> inspector_check;              // quick_studio_ui: „Sprawdź projekt” (zawsze, także gdy karta jest ukryta)
    std::function<int()> inspector_autofix;             // quick_studio_ui: „Napraw automatycznie” — zwraca liczbę poprawek
    std::function<void(double)> rotate_furniture;       // furniture_layout_ui.install
    std::optional<std::string> selected_scene;
    bool _view_interactive = false;
    // studio_interaction / viewport_annotations
    std::optional<std::pair<double, double>> gizmo_origin;          // getattr(app, 'gizmo_origin', ...)
    tk::Menu* context_menu = nullptr;                               // app.context_menu
    std::function<std::string()> finish_view_interaction;           // app.finish_view_interaction
    json viewport_stats = json{{"last_ms", 0}, {"frames", 0}};
    struct VisibleDrill { std::string part_id; double x = 0, y = 0; json operation; bool hidden = false; };
    std::vector<VisibleDrill> visible_drills;                       // viewport_annotations
    json visible_dimensions = json::array();                        // viewport_annotations

    // Atrybuty tworzone w trakcie działania
    int viewport_revision = 0;
    json sheets;
    core::Drawn drawn;
    std::optional<std::string> _gpu_warning;
    std::optional<std::array<json, 3>> _native_engine_logged;
    std::optional<std::pair<std::string, bool>> _native_title_state;
    std::optional<std::vector<std::pair<std::string, std::string>>> _reported_stale_tops;
    ProjectPtr _posed_cache;
    std::optional<std::tuple<const Project*, int, std::vector<std::pair<std::string, double>>>> _posed_cache_key;
    std::array<int, 2> start{}, last{};
    bool dragged = false;

    App() : tk::Tk() {
        title("Mebel Studio 3D · projektowanie i produkcja");
        geometry(std::format("{}x{}+10+10", std::min(1440, winfo_screenwidth() - 40), std::min(900, winfo_screenheight() - 100))); minsize(1180, 600);
        configure({{"bg", "#f1eee8"}});
        _native_document = nullptr; _native_pending = false;
        project_generation = 0;
        project = std::make_shared<Project>();
        palette::install_palette(project);
        open_amounts = {}; animation_job.reset(); motion_value = tk::DoubleVar(0); motion_all = tk::BooleanVar(false); resize_step = tk::BooleanVar(false);
        path.reset(); dirty = false;
        selected.reset(); undo_stack = std::make_unique<ListHistory>(); redo_stack = std::make_unique<ListHistory>(); loading = false;
        xray = tk::BooleanVar(false);
        xray_scope = tk::StringVar("Cały zestaw"); drill_mode = tk::StringVar("Także zasłonięte");
        viewport_engine = "cpu"; viewport_quality = "balanced";
        camera = Camera(); explode = tk::BooleanVar(false); dimensions = tk::BooleanVar(true);
        dimension_mode = tk::StringVar("Wybrany mebel"); show_drills = tk::BooleanVar(false); scene_drag = nullptr;
        edit_mode = tk::StringVar("camera"); handles = {}; edge_handles = {}; drag_edit.reset();
        layout_magnet = tk::BooleanVar(true); layout_tolerance = tk::DoubleVar(80); layout_drag = nullptr;
        status = tk::StringVar("Gotowy · wszystkie wymiary w milimetrach");
        view_style::load();
        forma_theme::apply(this);
        ui();
        developer_logs::install(this); perf::install(this); workspace_controls::install(this); studio_interaction::install(this);
        view_modes_ui::install(this);
        visual_catalogue::install(this);
        responsive_ui::install(this);
        native_viewport::install(this);
        forma_shell::finish(this);
        auto [configured, group] = furniture_configurator::configure(*project, furniture_configurator::default_config(0, *project));
        project = configured;
        _native_document = native_document::create_if_available(project);
        if (_native_document != nullptr) {
            undo_stack = std::make_unique<NativeHistoryView>(_native_document.get(), "undo");
            redo_stack = std::make_unique<NativeHistoryView>(_native_document.get(), "redo");
        }
        selected = [&] {
            for (auto& p : project->parts) if (p.group == group) return p.id;
            throw StopIteration();
        }();
        refresh(); after(100, [this] { fit(); });
        protocol("WM_DELETE_WINDOW", [this] { close(); });
        for (auto& [key, fn] : std::vector<std::pair<std::string, std::function<void()>>>{
                 {"<Control-s>", [this] { save(); }}, {"<Control-o>", [this] { open(); }}, {"<Control-z>", [this] { undo(); }},
                 {"<Control-y>", [this] { redo(); }}, {"<Control-n>", [this] { new_(); }}})
            workspace->bind_app_shortcut(key, fn);
        canvas->bind("<KeyPress-f>", [this](tk::Event&) -> std::string { fit(); return "break"; });
        canvas->bind("<KeyPress-F>", [this](tk::Event&) -> std::string { fit(); return "break"; });
        // File association arguments are projects; unittest/build arguments
        // must never create a delayed missing-file modal dialog.
        if (ARGV.size() > 1 && !ARGV[1].starts_with("--") && py::lower(py::path_str(py::to_path(ARGV[1]).extension())) == ".mebel") {
            std::string arg = ARGV[1];
            after(120, [this, arg] { load_path(arg); });
        }
    }

    ttk::Button* button(tk::Widget* parent, const std::string& text, std::function<void()> command, bool accent = false) {
        auto* b = new ttk::Button(parent, {{"text", text}, {"command", command}, {"style", accent ? "Accent.TButton" : "TButton"}}); b->pack({{"side", "left"}, {"padx", 3}, {"pady", 3}}); return b;
    }

    void ui() {
        quick_studio_ui::build(this);
    }

    Part* selected_part() { for (auto& p : project->parts) if (selected && p.id == *selected) return &p; return nullptr; }

    void apply_viewport_preferences(const json& prefs) {
        viewport_engine = prefs.value("engine", "auto");
        viewport_quality = prefs.value("quality", "balanced");
        journal::event(this, "render", "ustawienia widoku: silnik " + viewport_engine + ", jakość " + viewport_quality + ", skala interfejsu " + prefs.value("ui_scale", json(1.0)).dump());
        if (canvas) {
            native_viewport::reset_native_failure(canvas);
        }
        if (canvas) draw();
    }

    Part* require_part(const std::string& part_id) {
        Part* part = nullptr;
        for (auto& p : project->parts) if (p.id == part_id) { part = &p; break; }
        if (part == nullptr) throw ValueError("Element został usunięty. Otwórz formularz ponownie dla istniejącego elementu.");
        return part;
    }

    void edit_sketch() {
        Part* part = selected_part();
        if (part && py::truthy(py::get(part->assembly, "generated_by"))) {
            std::string generator = part->assembly["generated_by"];
            for (auto& p : project->parts) if (p.id == generator) { part = &p; break; }
        }
        if (part) new Sketch(this, part);
        else tk::messagebox::showinfo("Szkic", "Wybierz element w drzewie projektu.", this);
    }

    std::string last_snapshot_site;  // plik:linia ostatniego snapshot() — dziennik opisuje nim, co zmieniło projekt
    void snapshot(std::source_location where = std::source_location::current()) {
        {
            std::string file = where.file_name();
            auto slash = file.find_last_of("/\\");
            last_snapshot_site = (slash == std::string::npos ? file : file.substr(slash + 1)) + ":" + std::to_string(where.line());
        }
        if (_native_document != nullptr) {
            _sync_native_document();
            _native_pending = true; changed(); return;
        }
        undo_stack->append(json(project->data())); undo_stack->keep_last(60);
        redo_stack->clear(); changed();
    }

    void _native_dirty_state() {
        auto* document = _native_document.get();
        if (document == nullptr) return;
        dirty = document->dirty();
        std::pair<std::string, bool> state{project->name, dirty};
        if (state != _native_title_state) {
            _native_title_state = state;
            title("Mebel Studio 3D · " + project->name + (dirty ? " *" : ""));
        }
    }

    void _sync_native_document() {
        auto* document = _native_document.get();
        if (document == nullptr) return;
        if (drag_edit || layout_drag || scene_drag) return;
        try {
            if (!document->matches(project)) {
                ProjectPtr candidate = workspace_worktops::refresh_shared_worktops(project, false);
                document->commit(candidate, _native_pending);
                project = candidate;
            }
            _native_pending = false;
            _native_dirty_state();
        } catch (const std::exception&) {
            project = document->project(); _native_pending = false;
            _native_dirty_state();
            throw;
        }
    }

    // Publish one validated edit; native history owns the complete result.
    ProjectPtr commit_project(ProjectPtr project_, bool fit_ = false) {
        if (_native_document == nullptr) {
            project_->validate(); snapshot(); project = project_;
        } else {
            _sync_native_document();
            ProjectPtr candidate = workspace_worktops::refresh_shared_worktops(project_, false);
            _native_document->commit(candidate, true);
            project = candidate; _native_pending = false; changed();
        }
        refresh();
        if (fit_) fit();
        return project;
    }

    void changed() {
        viewport_revision = viewport_revision + 1;
        dirty = true; title("Mebel Studio 3D · " + project->name + " *");
        for (auto& observer : changed_observers) observer();
    }

    void refresh() {
        perf::Scope perf_scope("App::refresh");
        project = workspace_worktops::refresh_shared_worktops(project, false);
        _sync_native_document();
        std::vector<std::pair<std::string, std::string>> stale;
        for (auto& p : project->parts) {
            json top = p.assembly.is_object() ? p.assembly.value("shared_worktop", json::object()) : json::object();
            if (py::get(top, "status") == "needs_fit") stale.emplace_back(p.id, py::str(p.assembly["shared_worktop"].value("reason", json(""))));
        }
        if (!stale.empty() && stale != _reported_stale_tops && log)
            for (auto& [identity, reason] : stale) log->warning(std::format("Wspólny blat {} wymaga dopasowania: {}", identity, reason));
        _reported_stale_tops = stale;
        std::set<std::string> ids; for (auto& p : project->parts) ids.insert(p.id);
        std::erase_if(open_amounts, [&](auto& item) { return !ids.contains(item.first); });
        loading = true;
        tree->delete_(tree->get_children()); std::map<std::string, std::string> groups;
        for (auto& p : project->parts) {
            if (!groups.contains(p.group)) {
                std::string gid = "group_" + std::to_string(groups.size()); groups[p.group] = gid; tree->insert("", "end", {{"iid", gid}, {"text", p.group}, {"open", true}});
            }
            tree->insert(groups[p.group], "end", {{"iid", p.id}, {"text", std::string(p.visible ? "◉ " : "○ ") + p.name}});
        }
        std::vector<std::string> names; for (auto& m : project->materials) names.push_back(m.name);
        matcombo->configure({{"values", names}});
        if (selected && tree->exists(*selected)) tree->selection_set(*selected);
        else selected.reset();
        loading = false; load_form(); refresh_production(); draw();
        if (room_scene_panel) room_scene_panel->refresh();
        if (corner_panel) corner_panel->refresh();
        journal::project_refreshed(this);
    }

    void load_form() {
        perf::Scope perf_scope("App::load_form");
        Part* p = selected_part();
        if (p) selected_scene.reset();
        for (auto& [key, var] : vars) var.set(p ? py::str(p->get_attr(key)) : "");
        matvar.set(p ? project->mat(p->material).name : "");
        grain.set(p ? p->grain : false); grain_v.set(p ? p->grain_v : false); visible.set(p ? p->visible : true);
        for (size_t i = 0; i < edgevars.size(); ++i) edgevars[i].set(p ? (bool)p->edges[i] : false);
        if (quick_inspector && !drag_edit) quick_inspector->refresh();
        if (corner_panel && !drag_edit) corner_panel->refresh();
    }

    bool flush_parameter_drafts() {
        if (quick_inspector && !quick_inspector->flush_draft()) return false;
        if (corner_panel && !corner_panel->flush_draft()) return false;
        return true;
    }

    void tree_select([[maybe_unused]] tk::Event* event = nullptr) {
        if (loading) return;
        if (!flush_parameter_drafts()) return;
        auto sel = tree->selection();
        selected.reset();
        if (!sel.empty()) for (auto& p : project->parts) if (p.id == sel[0]) { selected = sel[0]; break; }
        if (!sel.empty() && !selected) {
            std::string group = tree->item(sel[0], "text");
            for (auto& p : project->parts) if (p.group == group) { selected = p.id; break; }
        }
        double amount = 0;
        if (selected && open_amounts.contains(*selected)) amount = open_amounts[*selected];
        motion_value.set(amount * 100);
        load_form(); draw();
    }

    ProjectPtr display_project() {
        bool any = false; for (auto& [k, v] : open_amounts) if (v) any = true;
        if (!any) return project;
        std::tuple<const Project*, int, std::vector<std::pair<std::string, double>>> key{
            project.get(), viewport_revision, std::vector<std::pair<std::string, double>>(open_amounts.begin(), open_amounts.end())};
        if (_posed_cache_key != key) {
            _posed_cache = mechanisms::posed_project(project, open_amounts); _posed_cache_key = key;
        }
        return _posed_cache;
    }

    void open_motion([[maybe_unused]] std::optional<double> value = std::nullopt) {
        double amount = motion_value.get() / 100;
        std::vector<Part*> targets;
        if (motion_all.get()) { for (auto& p : project->parts) if (mechanisms::effective_motion(p)["mode"] != "none") targets.push_back(&p); }
        else if (selected_part()) targets.push_back(selected_part());
        Part* selected_ = selected_part();
        if (motion_all.get() && selected_) std::erase_if(targets, [&](Part* p) { return p->group != selected_->group; });
        for (Part* p : targets) open_amounts[p->id] = amount;
        draw();
        if (!targets.empty()) status.set(std::format("Otwarcie {:.0f}% · podgląd ruchu; wymiary produkcyjne w pozycji zamkniętej", amount * 100));
    }

    void close_motion() {
        if (animation_job) { after_cancel(*animation_job); animation_job.reset(); }
        open_amounts = {}; motion_value.set(0); draw();
    }

    void animate_motion() {
        if (animation_job) { after_cancel(*animation_job); animation_job.reset(); return; }
        double target = motion_value.get() > 50 ? 0 : 100;
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [this, target, weak = std::weak_ptr<std::function<void()>>(tick)] {
            double current = motion_value.get(); double value = target > current ? current + 5 : current - 5;
            motion_value.set(std::max(0.0, std::min(100.0, value))); open_motion();
            if (std::abs(motion_value.get() - target) > 1) { auto t = weak.lock(); animation_job = after(45, [t] { (*t)(); }); }
            else animation_job.reset();
        };
        (*tick)();
    }

    void apply() {
        Part* p = selected_part();
        if (!p) return;
        auto on_error = [&](const std::exception& e) { tk::messagebox::showerror("Sprawdź dane", e.what(), this); };
        try {
            if (py::truthy(p->assembly)) {
                if (py::truthy(py::get(p->assembly, "generated_by"))) throw ValueError("Edytuj front szuflady zamiast części skrzynki.");
                bool moved = false;
                for (std::string k : {"x", "y", "z", "rx", "ry", "rz"}) if (to_number(vars[k].get()) != p->get_attr(k).get<double>()) { moved = true; break; }
                if (moved || vars["plane"].get() != p->plane) throw ValueError("Położenie elementu powiązanego zmień przez Edytuj szkic i parametry montażu.");
                ProjectPtr temp = dimension_model::resize_dimensions(*project, p->id, to_number(vars["u"].get()), to_number(vars["v"].get()), to_number(vars["t"].get()));
                Part* q = nullptr; for (auto& part : temp->parts) if (part.id == p->id) { q = &part; break; }
                if (!q) throw StopIteration();
                q->name = vars["name"].get(); q->material = py::index(temp->materials, matcombo->current()).id; q->visible = visible.get(); q->grain = grain.get(); q->grain_v = grain_v.get();
                q->edges.clear(); for (auto& v : edgevars) q->edges.push_back(v.get()); temp->validate();
                snapshot(); project = temp; refresh(); return;
            }
            Part candidate = *p;
            for (auto& [key, var] : vars) candidate.set_attr(key, (key == "name" || key == "group" || key == "plane") ? json(var.get()) : json(to_number(var.get())));
            if (candidate.group != p->group) {
                Part* peer = nullptr; for (auto& q : project->parts) if (q.group == candidate.group) { peer = &q; break; }
                candidate.layout = peer ? peer->layout : p->layout;
            }
            candidate.material = py::index(project->materials, matcombo->current()).id;
            candidate.edges.clear(); for (auto& v : edgevars) candidate.edges.push_back(v.get()); candidate.grain = grain.get(); candidate.grain_v = grain_v.get(); candidate.visible = visible.get();
            if (!candidate.contour.empty() && (candidate.u != p->u || candidate.v != p->v)) {
                candidate.contour.clear();
                for (auto& pt : p->contour) candidate.contour.push_back({pt[0] * candidate.u / p->u, pt[1] * candidate.v / p->v});
            }
            json operations = json::array();
            for (auto& op : candidate.operations) if (py::get(op, "recipe_id") != "handle") operations.push_back(op);
            candidate.operations = operations;
            candidate.validate();
            auto blank = candidate.blank(project->edge_t);
            if (std::min(blank.first, blank.second) <= 0) throw ValueError("Wymiar do cięcia musi być dodatni.");
            ProjectPtr temp = std::make_shared<Project>(*project); temp->parts[p - project->parts.data()] = candidate;
            std::string part_id = p->id;
            fittings::refresh_handles(*temp, part_id);
            opening_options::refresh_openings(*temp, part_id);
            temp = rebuild_dependents(*temp, part_id);
            snapshot(); project = temp; refresh(); status.set("Zmieniono element · model i formatki przeliczone");
        } catch (const ValueError& e) { on_error(e); } catch (const IndexError& e) { on_error(e); }
    }

    void refresh_production() {
        perf::Scope perf_scope("App::refresh_production");
        auto* table = tables["parts"]; table->delete_(table->get_children());
        int zebra = 0;  // naprzemienne tło wierszy (tagi "odd"/"even" z build_production)
        for (auto& p : project->parts) {
            auto [a, b] = p.blank(project->edge_t);
            table->insert("", "end", {{"values", std::vector<std::string>{p.group, p.name, project->mat(p.material).name, std::format("{:g}", p.u), std::format("{:g}", p.v), std::format("{:g}", p.t), std::format("{:g}", a), std::format("{:g}", b), std::format("{:.2f}", p.edge_m()), p.contour.empty() ? "" : "Tak"}}, {"tags", (zebra++ % 2) ? "odd" : "even"}});
        }
        auto c = project->costs(); table = tables["costs"]; table->delete_(table->get_children()); zebra = 0;
        for (auto& [label, key] : std::vector<std::pair<std::string, std::string>>{{"Materiał · powierzchnia netto", "panels"}, {"Obrzeża", "edges"}, {"Akcesoria", "hardware"}, {"Robocizna", "labor"}, {"Koszt bazowy", "base"}, {std::format("Cena z narzutem {:g}%", project->markup), "total"}})
            table->insert("", "end", {{"values", std::vector<std::string>{label, std::format("{:.2f}", c.at(key))}}, {"tags", key == "total" ? "total" : key == "base" ? "subtotal" : (zebra++ % 2) ? "odd" : "even"}});
        auto [sheets_, bad] = sheet_nesting::plan(*project); sheets = sheets_;
        double stock = 0;
        for (auto& s : sheets) { Part blank; blank.u = s["u"]; blank.v = s["v"]; blank.t = s["thickness"]; stock += project->mat(s["material"].get<std::string>()).cost(blank); }
        costnote->configure({{"text", std::format("Pełne płyty według rozkroju: {:.2f} zł · ceny własne, bez podatku i transportu; materiał netto nie zawiera odpadów.", stock)}});
        cutlabel->configure({{"text", sheet_nesting::summary_line(*project, sheets, bad)}});
        std::vector<std::string> vals;
        for (size_t i = 0; i < sheets.size(); ++i) { auto& s = sheets[i]; vals.push_back(std::format("{}. {} · {:g} mm · {:g} × {:g}", i + 1, project->mat(s["material"].get<std::string>()).name, s["thickness"].get<double>(), s["u"].get<double>(), s["v"].get<double>())); }
        sheetcombo->configure({{"values", vals}});
        if (!vals.empty()) sheetcombo->current(0);
        else sheetvar.set("Brak płyt");
        draw_cut();
        if (refresh_inspector) refresh_inspector();
    }

    void draw_cut() {
        auto* c = cutcanvas; c->delete_("all"); int index = sheetcombo->current();
        if (!sheets.is_array() || !(0 <= index && index < (int)sheets.size())) return;
        auto& s = sheets[index]; int w = std::max(c->winfo_width(), 200); int h = std::max(c->winfo_height(), 200);
        double su = s["u"], sv = s["v"];
        double scale = std::min((w - 50) / su, (h - 50) / sv); double ox = (w - su * scale) / 2; double oy = (h - sv * scale) / 2;
        c->create_rectangle(ox, oy, ox + su * scale, oy + sv * scale, {{"fill", "#f1eee8"}, {"outline", "#6b7266"}});
        // Wolne pola po rozkroju (odpady): przerywany obrys, a większe z podpisem wymiaru.
        if (s.contains("free") && s["free"].is_array()) {
            for (auto& f : s["free"]) {
                double fw = f[2].get<double>() - project->kerf, fh = f[3].get<double>() - project->kerf;
                if (fw <= 0 || fh <= 0) continue;
                double fx = ox + f[0].get<double>() * scale, fy = oy + f[1].get<double>() * scale;
                c->create_rectangle(fx, fy, fx + fw * scale, fy + fh * scale, {{"fill", "#e4e1da"}, {"outline", "#9aa4ae"}, {"dash", "4 3"}});
                if (fw * scale > 90 && fh * scale > 30)
                    c->create_text(fx + fw * scale / 2, fy + fh * scale / 2, {{"text", std::format("odpad {:.0f} × {:.0f}", fw, fh)}, {"font", "{Segoe UI} 8"}, {"fill", "#6b7684"}});
            }
        }
        for (size_t n = 0; n < s["pieces"].size(); ++n) {
            auto& r = s["pieces"][n];
            Part* p = nullptr; for (auto& q : project->parts) if (json(q.id) == r["id"]) { p = &q; break; }
            if (!p) throw StopIteration();
            double ru = r["u"], rv = r["v"];
            double x = ox + r["x"].get<double>() * scale, y = oy + r["y"].get<double>() * scale;
            c->create_rectangle(x, y, x + ru * scale, y + rv * scale, {{"fill", project->mat(p->material).color}, {"outline", "#45546b"}});
            if (ru * scale > 70 && rv * scale > 32)
                c->create_text(x + ru * scale / 2, y + rv * scale / 2, {{"text", std::format("{}. {}\n{:g} × {:g}{}", n + 1, p->name, ru, rv, r.value("rotated", false) ? " ⟳" : "")}, {"font", "{Segoe UI} 9"}, {"width", std::max(30.0, ru * scale - 8)}, {"fill", "#172332"}});
        }
    }

    // native_viewport.install owija draw: po pełnym rysowaniu zatwierdza nakładki silnika C++.
    std::function<void()> draw_wrapper;
    void draw() {
        perf::Scope perf_scope("App::draw");
        auto t0 = std::chrono::steady_clock::now();
        if (draw_wrapper) draw_wrapper(); else draw_original();
        if (dev_tools::enabled()) journal::draw_done(this, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }

    void draw_original() {
        if (_native_document != nullptr) _sync_native_document();
        auto* c = canvas; c->delete_("all"); int w = c->winfo_width(), h = c->winfo_height();
        if (w < 10 || h < 10) return;
        ProjectPtr display = display_project();
        auto xray_ids = viewport_modes::xray_part_ids(display, xray.get(), xray_scope.get(), selected);
        bool interactive = _view_interactive;
        json background = viewport_scene::paint_background(c, camera, display, {w, h}, interactive);
        std::optional<std::string> identity = selected_scene ? std::optional<std::string>("scene:" + *selected_scene) : selected;
        Part* selected_part_ = selected_part();
        std::set<std::string> layout_ids;
        if (edit_mode.get() == "layout" || edit_mode.get() == "furniture" || inspector_tabs->index(inspector_tabs->select()) == 0)
            for (auto& q : project->parts) if (selected_part_ && q.group == selected_part_->group) layout_ids.insert(q.id);
        std::optional<ViewportResult> gpu_result;
        if (native_viewport::NATIVE_ENGINES.contains(viewport_engine)) {
            gpu_result = native_viewport::paint_native(c, {.project = display, .backend = viewport_engine, .camera = camera, .size = {w, h},
                .interactive = interactive, .selected_ids = layout_ids, .selected = identity,
                .explode = explode.get() ? .22 : 0, .xray = xray.get(), .xray_ids = xray_ids, .quality = viewport_quality});
            if (gpu_result->ok || viewport_engine != "auto") {
                gpu_viewport::close_gpu(c);
            }
            std::optional<std::string> warning = (gpu_result->warning && !gpu_result->warning->empty()) ? gpu_result->warning : gpu_result->error;
            if (warning && !warning->empty() && _gpu_warning != warning && log) {
                _gpu_warning = warning; log->warning(std::format("Silnik C++: {}", *warning));
            }
        } else {
            native_viewport::close_native(c);
        }
        if (viewport_engine == "gpu" || (viewport_engine == "auto" && !(gpu_result && gpu_result->ok))) {
            auto on_error = [&](const std::exception& error) {
                if (_gpu_warning != std::string(error.what()) && log) {
                    _gpu_warning = error.what(); log->warning(std::format("Silnik 3D: użyto renderera CPU: {}", error.what()));
                }
            };
            try {
                gpu_result = gpu_viewport::paint_gpu(c, {.project = display, .camera = camera, .size = {w, h}, .interactive = interactive, .selected_ids = layout_ids,
                    .selected = identity, .explode = explode.get() ? .22 : 0, .xray = xray.get(), .xray_ids = xray_ids, .quality = viewport_quality});
                if (!gpu_result->ok && log) {
                    std::string error = gpu_result->error.value_or("Silnik GPU jest niedostępny.");
                    if (_gpu_warning != error) {
                        _gpu_warning = error; log->warning(std::format("Silnik 3D: użyto renderera CPU: {}", error));
                    }
                }
            } catch (const std::runtime_error& error) { on_error(error); }  // RuntimeError, OSError
            catch (const ValueError& error) { on_error(error); }
        }
        if (gpu_result && gpu_result->ok) {
            drawn = gpu_result->drawn; c->_active_renderer = gpu_result->engine == std::optional<std::string>("native") ? "native" : "gpu";
            if (c->_active_renderer == "native" && log) {
                json diagnostics = gpu_result->diagnostics.is_object() ? gpu_result->diagnostics : json::object();
                std::array<json, 3> identity_engine{py::get(diagnostics, "engine"), py::get(diagnostics, "adapter"), py::get(diagnostics, "software")};
                if (identity_engine != _native_engine_logged) {
                    _native_engine_logged = identity_engine;
                    log->info(std::format("Widok 3D C++: {} · {} · {}", py::str(identity_engine[0]), py::str(identity_engine[1]),
                                          py::truthy(identity_engine[2]) ? "renderer programowy" : "renderowanie sprzętowe"));
                }
            }
        } else {
            c->_active_renderer = "cpu";
            drawn = render_faces(*display, camera, w, h, identity, explode.get() ? 0.22 : 0, interactive);
            viewport_scene::paint_faces(c, drawn, display, {.selected_ids = layout_ids, .camera = camera, .size = {w, h}, .xray = xray.get(), .explode = explode.get() ? .22 : 0, .interactive = interactive, .xray_ids = xray_ids, .stats = viewport_render::last_stats()});
        }
        Part* p = nullptr; for (auto& q : display->parts) if (selected && q.id == *selected) { p = &q; break; }
        double factor = explode.get() ? .22 : 0;
        auto project_local = [&](const Part& part, const std::array<double, 3>& point) {
            auto shift = explosion_shift(part, camera, factor); auto position = world(part, point[0], point[1], point[2]);
            return camera.project({position[0] + shift[0], position[1] + shift[1], position[2] + shift[2]}, w, h);
        };
        viewport_annotations::paint(this, display);
        viewport_hud::paint(this, w, h, background["spacing"]);
        handles = {}; edge_handles = {};
        bool any_open = false; for (auto& [k, v] : open_amounts) if (v) any_open = true;
        // Rozciąganie mebla działa na całej grupie, więc zaznaczona część skrzynki szuflady / kosza (generated_by) nie może blokować uchwytów:
        // bierzemy jej właściciela (front), a w razie braku — dowolną część tej samej grupy.
        Part* furniture_part = p;
        if (edit_mode.get() == "furniture" && p && py::truthy(py::get(p->assembly, "generated_by"))) {
            furniture_part = nullptr;
            std::string owner = py::get(p->assembly, "generated_by").is_string() ? py::get(p->assembly, "generated_by").get<std::string>() : "";
            for (auto& q : display->parts) if (q.id == owner) { furniture_part = &q; break; }
            if (!furniture_part) for (auto& q : display->parts) if (q.group == p->group && !py::truthy(py::get(q.assembly, "generated_by"))) { furniture_part = &q; break; }
        }
        {
            // Dziennik: dlaczego w trybie rozciągania nie ma uchwytów (raz na zmianę przyczyny) — wcześniej brak uchwytów był niemy.
            static std::string last_reason;
            std::string reason;
            if (edit_mode.get() == "furniture") {
                if (!p) reason = "brak zaznaczonej części";
                else if (any_open) reason = "jest otwarta animacja drzwi/szuflad";
                else if (!furniture_part) reason = "zaznaczona część skrzynki bez właściciela";
                else if (furniture_part->group.empty()) reason = "zaznaczona część nie należy do żadnego mebla";
            }
            if (reason != last_reason) {
                last_reason = reason;
                if (!reason.empty()) journal::event(this, "rozciąganie", "brak uchwytów rozciągania: " + reason + (p ? " (" + p->name + ")" : std::string()));
            }
        }
        if (furniture_part && !any_open && (edit_mode.get() == "resize" || edit_mode.get() == "furniture") && (edit_mode.get() == "furniture" || !py::truthy(py::get(p->assembly, "generated_by")))) {
            if (edit_mode.get() == "furniture") {
                Part* p = furniture_part;
                try {
                    auto styled = shaped_furniture::styled_bounds(*project, p->group);
                    auto f = styled ? *styled : assembly::frame(*project, p->group); auto lo = f.lower, hi = f.upper;
                    std::array<double, 3> center{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
                    for (int index = 0; index < 3; ++index) {
                        char axis = "xyz"[index];
                        for (char end : {'0', '1'}) {
                            auto point = center; point[index] = end == '0' ? lo[index] : hi[index]; auto pr = camera.project(core::layout_point(*p, point), w, h); double x = pr[0], y = pr[1];
                            auto unit = point; unit[index] += 1; auto a = camera.project(core::layout_point(*p, unit), w, h); std::array<double, 2> vector{a[0] - x, a[1] - y};
                            if (std::hypot(vector[0], vector[1]) < .01) continue;
                            handles.push_back(Handle{std::string{axis, end}, x, y, vector, p->group});
                            c->create_rectangle(x - 6, y - 6, x + 6, y + 6, {{"fill", "#4e6b3f"}, {"outline", "white"}, {"width", 2}});
                            c->create_text(x + 10, y - 12, {{"text", std::string{(char)std::toupper(axis), end}}, {"fill", "#4e6b3f"}, {"anchor", "w"}});
                        }
                    }
                    static std::string last_group;
                    if (last_group != p->group) {
                        last_group = p->group;
                        journal::event(this, "rozciąganie", std::format("uchwyty rozciągania: {} dla „{}”", handles.size(), p->group));
                    }
                } catch (const ValueError& e) {
                    status.set(e.what());
                    journal::event(this, "rozciąganie", std::string("uchwyty rozciągania niedostępne: ") + e.what() + " (" + p->group + ")");
                }
            } else {
                std::array<double, 3> center{p->u / 2, p->v / 2, p->t / 2};
                for (char axis : {'u', 'v', 't'}) {
                    int index = std::map<char, int>{{'u', 0}, {'v', 1}, {'t', 2}}[axis];
                    for (char end : {'0', '1'}) {
                        auto point = center; point[index] = end == '0' ? 0 : p->get_attr(std::string(1, axis)).get<double>();
                        auto pr = project_local(*p, point); double x = pr[0], y = pr[1];
                        auto unit = point; unit[index] += 1;
                        auto a = project_local(*p, unit); std::array<double, 2> vector{a[0] - x, a[1] - y};
                        if (std::hypot(vector[0], vector[1]) < .01) continue;
                        std::string color = std::map<char, std::string>{{'u', "#c95048"}, {'v', "#288563"}, {'t', "#3e77bd"}}[axis];
                        c->create_rectangle(x - 6, y - 6, x + 6, y + 6, {{"fill", color}, {"outline", "white"}, {"width", 2}, {"tags", "resize_handle"}});
                        c->create_text(x + 10, y - 10, {{"text", std::string(1, (char)std::toupper(axis)) + (end == '0' ? "−" : "+")}, {"fill", color}, {"font", "{Segoe UI} 10 bold"}});
                        handles.push_back(Handle{std::string{axis, end}, x, y, vector, std::nullopt});
                    }
                }
            }
        }
        if (p && edit_mode.get() == "fillet") {
            int number = 1;
            for (auto& spec : cad_geometry::edge_specs(p->points(), p->t, p->profile, {p->u, p->v})) {
                auto edge = spec.edge; std::vector<std::array<double, 2>> path_;
                for (auto& point : cad_geometry::edge_path(spec)) { auto pr = project_local(*p, point); path_.push_back({pr[0], pr[1]}); }
                std::vector<double> coords; for (auto& point : path_) { coords.push_back(point[0]); coords.push_back(point[1]); }
                c->create_line(coords, {{"fill", "#6c40a3"}, {"width", 2}});
                for (size_t i = 0; i + 1 < path_.size(); ++i) edge_handles.push_back(EdgeHandle{edge, path_[i], path_[i + 1]});
                auto label_point = path_[path_.size() / 2];
                c->create_text(label_point[0], label_point[1], {{"text", std::to_string(number)}, {"fill", "#5a2485"}, {"font", "{Segoe UI} 9 bold"}});
                ++number;
            }
        }
    }

    void fit() {
        viewport_scene::fit_scene(camera, *display_project(), {std::max(canvas->winfo_width(), 100), std::max(canvas->winfo_height(), 100)});
        if (explode.get()) camera.scale /= 1.22;
        draw();
    }
    void view(double yaw, double pitch) { journal::event(this, "render", std::format("widok: yaw {:.0f} pitch {:.0f}", yaw, pitch)); camera.yaw = yaw; camera.pitch = pitch; fit(); }
    void zoom(double factor) { camera.scale = std::max(.003, std::min(15.0, camera.scale * factor)); draw(); }
    void wheel(tk::Event& e) { zoom(e.delta > 0 ? 1.12 : 1 / 1.12); }
    void mouse_down(tk::Event& e) {
        if (!flush_parameter_drafts()) {
            start = last = {e.x, e.y}; dragged = true; return;
        }
        canvas->focus_set(); start = {e.x, e.y}; last = {e.x, e.y}; dragged = false;
        if (edit_mode.get() == "layout") {
            furniture_layout_ui::begin_drag(this, e); return;
        }
        if (edit_mode.get() == "resize" || edit_mode.get() == "furniture") {
            const Handle* nearest = nullptr;
            for (auto& hd : handles) if (!nearest || std::hypot(e.x - hd.x, e.y - hd.y) < std::hypot(e.x - nearest->x, e.y - nearest->y)) nearest = &hd;
            if (nearest && std::hypot(e.x - nearest->x, e.y - nearest->y) < 12) {
                drag_edit = DragEdit{*nearest, std::make_shared<Project>(*project), nullptr, selected, 0};
                journal::event(this, "rozciąganie", std::format("chwycono uchwyt {} ({}) w trybie {}", nearest->face, nearest->group ? *nearest->group : std::string("(część)"), edit_mode.get()));
            }
            else if (edit_mode.get() == "furniture")
                journal::event(this, "rozciąganie", nearest ? std::format("kliknięcie poza uchwytami (najbliższy {} w odległości {:.0f} px)", nearest->face, std::hypot(e.x - nearest->x, e.y - nearest->y))
                                                           : std::string("kliknięcie w trybie rozciągania, ale nie ma żadnych uchwytów"));
        }
    }
    void direct_drag(tk::Event& e) {
        DragEdit& drag = *drag_edit; Handle& handle = drag.handle; double vx = handle.vector[0], vy = handle.vector[1];
        double distance = std::nearbyint(((e.x - start[0]) * vx + (e.y - start[1]) * vy) / (vx * vx + vy * vy));
        if (resize_step.get()) distance = std::nearbyint(distance / 32) * 32;
        if (py::monotonic() - drag.last < .04) return;
        drag.last = py::monotonic();
        try {
            ProjectPtr result;
            if (handle.group && !handle.group->empty()) {
                result = direct_edit::resize_furniture_face(*drag.base, *handle.group, handle.face, distance);
            } else result = resize_part(*drag.base, *drag.id, handle.face, distance);
            drag.candidate = result; project = result; load_form(); draw();
            std::string row_note;
            if (handle.group && !handle.group->empty() && handle.face[0] == 'x') {
                try {
                    json row = furniture_layout::row_extent(*result, *handle.group);
                    if (row.is_object() && row.at("count").get<long long>() > 1)
                        row_note = std::format(" · rząd {} szafek: {:g} mm razem", row.at("count").get<long long>(), std::round(row.at("length").get<double>() * 10) / 10);
                } catch (const std::exception&) {
                }
            }
            status.set(std::format("Rozciąganie {}: {:+g} mm{} · puść przycisk, aby zatwierdzić", handle.face, distance, row_note));
        } catch (const ValueError& error) {
            drag.candidate = nullptr; project = drag.base; draw(); status.set(error.what());
        }
    }
    void orbit(tk::Event& e) {
        if (edit_mode.get() == "layout") {
            furniture_layout_ui::drag(this, e); return;
        }
        if (drag_edit) { direct_drag(e); return; }
        if (edit_mode.get() != "camera" && edit_mode.get() != "configure") return;
        int dx = e.x - last[0], dy = e.y - last[1];
        if (std::hypot(e.x - start[0], e.y - start[1]) > 3) dragged = true;
        if (dragged) { camera.yaw += dx * .45; camera.pitch = std::max(-89.0, std::min(89.0, camera.pitch + dy * .4)); draw(); }
        last = {e.x, e.y};
    }
    void mouse_up(tk::Event& e) {
        if (layout_drag) {
            if (furniture_layout_ui::end_drag(this, e)) return;
        }
        if (drag_edit) {
            drag_edit->last = 0; direct_drag(e);
            DragEdit drag = *drag_edit; project = drag.base; drag_edit.reset();
            if (drag.candidate != nullptr) { snapshot(); project = drag.candidate; }
            refresh(); status.set("Zapisano rozciągnięcie mebla / elementu · Ctrl+Z cofa zmianę");
            journal::event(this, "rozciąganie", std::format("zatwierdzono {} {} ({})", drag.handle.group ? *drag.handle.group : std::string("(część)"), drag.handle.face, drag.candidate != nullptr ? "zapisano" : "bez zmian"));
            // Po rozciągnięciu mebla w głąb albo w górę pytamy, czym ma się stać (narożnik L, pawlacz, szafa, regał).
            if (drag.candidate != nullptr && drag.handle.group && !drag.handle.group->empty() && drag.base)
                stretch_choice::offer(this, *drag.base, *drag.handle.group, drag.handle.face);
            return;
        }
        if (edit_mode.get() == "fillet" && !edge_handles.empty()) {
            auto distance = [&](const EdgeHandle& item) {
                double ax = item.a[0], ay = item.a[1]; double bx = item.b[0], by = item.b[1]; double dx = bx - ax, dy = by - ay;
                double denom = dx * dx + dy * dy; if (!denom) denom = 1;
                double t = std::max(0.0, std::min(1.0, ((e.x - ax) * dx + (e.y - ay) * dy) / denom));
                return std::hypot(e.x - ax - t * dx, e.y - ay - t * dy);
            };
            const EdgeHandle* nearest = &edge_handles[0];
            for (auto& item : edge_handles) if (distance(item) < distance(*nearest)) nearest = &item;
            if (distance(*nearest) < 10) { round_edges(nearest->edge); return; }
        }
        if (!dragged) {
            selected.reset();
            selected = studio_interaction::pick(this, e.x, e.y);
            if (selected && selected->starts_with("scene:")) selected.reset();
            if (selected) { tree->selection_set(*selected); tree->see(*selected); }
            else tree->selection_remove(tree->selection());
            if (Part* chosen = selected_part()) journal::event(this, "zaznaczenie", std::format("kliknięcie w widoku ({}, {}) w trybie {}: {} [{}] grupa {}", e.x, e.y, edit_mode.get(), chosen->name, chosen->role, chosen->group));
            else journal::event(this, "zaznaczenie", std::format("kliknięcie w widoku ({}, {}) w trybie {}: nic nie zaznaczono", e.x, e.y, edit_mode.get()));
            load_form(); draw();
            if (edit_mode.get() == "configure") {
                if (Part* part = selected_part(); part != nullptr && !part->group.empty()) stretch_choice::open_configuration(this, part->group);
                else status.set("Konfiguracja: kliknij mebel, który chcesz skonfigurować");
            }
        }
    }
    void pan_start(tk::Event& e) { last = {e.x, e.y}; }
    void pan(tk::Event& e) { camera.pan = {camera.pan[0] + e.x - last[0], camera.pan[1] + e.y - last[1]}; last = {e.x, e.y}; draw(); }

    void exact_resize() {
        Part* p = selected_part();
        if (!p) return;
        std::string part_id = p->id;
        auto* dialog_ = dialog("Rozciągnij wybraną ścianę", "500x320");
        (new ttk::Label(dialog_, {{"text", "Przesunięcie ściany w osi lokalnej.\nWartość dodatnia zwiększa współrzędną ściany."}, {"padding", 12}}))->pack({{"fill", "x"}});
        std::vector<std::pair<std::string, std::string>> labels{{"U max", "u1"}, {"U min", "u0"}, {"V max", "v1"}, {"V min", "v0"}, {"Grubość max", "t1"}, {"Grubość min", "t0"}};
        std::vector<std::string> label_names; for (auto& [k, v] : labels) label_names.push_back(k);
        tk::StringVar face("U max"); (new ttk::Combobox(dialog_, {{"textvariable", face}, {"values", label_names}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"padx", 15}});
        tk::StringVar distance = field(dialog_, "Przesunięcie [mm]", 100);
        auto submit = [this, dialog_, labels, face, distance, part_id]() mutable {
            try {
                dialog_->ensure_current_project(true); require_part(part_id);
                std::string face_key; for (auto& [k, v] : labels) if (k == face.get()) face_key = v;
                ProjectPtr result = resize_part(*project, part_id, face_key, number(distance));
                snapshot(); project = result; refresh(); fit(); dialog_->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Rozciąganie", e.what(), dialog_); }
        };
        (new ttk::Button(dialog_, {{"text", "Zastosuj"}, {"command", std::function<void()>(submit)}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 12}});
    }

    void furniture_dimensions() {
        Part* p = selected_part();
        auto selection = tree->selection();
        std::optional<std::string> group = p ? std::optional<std::string>(p->group) : !selection.empty() ? std::optional<std::string>(tree->item(selection[0], "text")) : std::nullopt;
        if (!group || group->empty()) {
            tk::messagebox::showinfo("Mebel", "Wybierz mebel lub jego element w drzewie projektu.", this); return;
        }
        assembly::Frame f;
        try { auto styled = shaped_furniture::styled_bounds(*project, *group); f = styled ? *styled : assembly::frame(*project, *group); }
        catch (const ValueError& e) { tk::messagebox::showerror("Mebel", e.what(), this); return; }
        auto lo = f.lower, hi = f.upper;
        auto* dialog_ = dialog("Wymiary całego mebla — " + *group, "530x390");
        std::vector<tk::StringVar> fields;
        for (auto& [label, value] : std::vector<std::pair<std::string, double>>{{"Szerokość X [mm]", hi[0] - lo[0]}, {"Wysokość Z [mm]", hi[2] - lo[2]}, {"Głębokość Y [mm]", hi[1] - lo[1]}})
            fields.push_back(field(dialog_, label, value));
        (new ttk::Label(dialog_, {{"text", "Grubości części i promienie pozostają stałe.\nSystem przelicza położenie boków, górę, dół i elementy zależne.\nParametry prowadnic i skrzynki szuflady pozostają zadane."}, {"padding", 15}, {"wraplength", 490}}))->pack({{"fill", "x"}});
        std::string group_ = *group;
        auto submit = [this, dialog_, fields, group_]() mutable {
            try {
                ProjectPtr result = resize_furniture(*project, group_, number(fields[0]), number(fields[1]), number(fields[2]));
                snapshot(); project = result; refresh(); fit(); dialog_->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Wymiary mebla", e.what(), dialog_); }
        };
        (new ttk::Button(dialog_, {{"text", "Przelicz cały mebel"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 12}});
    }

    void round_edges(std::optional<cad_geometry::Edge> edge = std::nullopt) {
        if (edge && !edge->empty() && side_panels::fillet_active()) { side_panels::toggle_edge(this, *edge); return; }
        Part* p = selected_part();
        if (!p) return;
        try {
            cad_geometry::kernel();
        } catch (const ValueError& e) { tk::messagebox::showinfo("Moduł CAD", e.what(), this); return; }
        new edge_editor::EdgeEditor(this, *p, edge);
    }

    void add_part() {
        snapshot(); Part p; p.material = project->materials[0].id; project->parts.push_back(p); selected = p.id; refresh(); fit();
    }
    void delete_() {
        Part* p = selected_part();
        if (p) {
            if (py::truthy(py::get(p->assembly, "generated_by"))) {
                tk::messagebox::showinfo("Szuflada", "Część należy do wygenerowanej szuflady. Usuń front szuflady, aby usunąć cały zespół.", this); return;
            }
            std::string part_id = p->id;
            snapshot(); delete_with_connections(*project, part_id); selected.reset(); refresh();
        }
    }
    void duplicate() {
        Part* p = selected_part();
        if (p) {
            Part source = *p;
            snapshot(); Part q = source; q.id = uid(); q.name += " kopia"; q.x += 50;
            q.assembly = json::object(); q.operations = json::array(); q.role = "other"; q.configurator_source = json::object();
            for (auto& cut : q.machining) cut.erase("attachment_id");
            project->parts.push_back(q); selected = q.id; refresh();
        }
    }
    bool cancel_active_edits() {
        if (quick_inspector) quick_inspector->cancel_pending();
        if (corner_panel) corner_panel->cancel_pending();
        if (hardware_drag && hardware_drag->active()) { hardware_drag->cancel(); return true; }
        if (scene_drag) {
            return room_scene_drag::cancel(this);
        }
        if (layout_drag) {
            furniture_layout_ui::cancel_drag(this); return true;
        }
        if (drag_edit) {
            project = drag_edit->base; drag_edit.reset(); refresh(); return true;
        }
        return false;
    }
    void undo() {
        if (cancel_active_edits()) return;
        journal::event(this, "cofanie", std::format("cofnij (Ctrl+Z) · stos cofania: {}", undo_stack->empty() ? 0 : 1));
        if (_native_document != nullptr) {
            _sync_native_document(); ProjectPtr project_ = _native_document->undo();
            if (project_ != nullptr) { project = project_; changed(); refresh(); }
            return;
        }
        if (!undo_stack->empty()) { redo_stack->append(project->data()); project = Project::from_data(undo_stack->pop()); changed(); refresh(); }
    }
    void redo() {
        journal::event(this, "cofanie", "ponów (Ctrl+Y)");
        if (cancel_active_edits()) return;
        if (_native_document != nullptr) {
            _sync_native_document(); ProjectPtr project_ = _native_document->redo();
            if (project_ != nullptr) { project = project_; changed(); refresh(); }
            return;
        }
        if (!redo_stack->empty()) { undo_stack->append(project->data()); project = Project::from_data(redo_stack->pop()); changed(); refresh(); }
    }

    form_dialog::Form* dialog(const std::string& title_, const std::string& size = "520x600") {
        return (new form_dialog::FormDocument(this, title_, size))->form;
    }
    tk::StringVar field(tk::Widget* parent, const std::string& label, const json& default_) {
        auto* frame = new ttk::Frame(parent); frame->pack({{"fill", "x"}, {"padx", 15}, {"pady", 5}});
        (new ttk::Label(frame, {{"text", label}, {"width", 26}}))->pack({{"side", "left"}});
        tk::StringVar var(py::str(default_)); (new ttk::Entry(frame, {{"textvariable", var}}))->pack({{"side", "right"}, {"fill", "x"}, {"expand", true}}); return var;
    }
    double number(tk::StringVar& v) { return to_number(v.get()); }

    void template_() {
        auto* d = dialog("Dodaj mebel parametryczny", "520x610");
        (new ttk::Label(d, {{"text", "Konstrukcja"}, {"style", "Title.TLabel"}, {"padding", 15}}))->pack({{"anchor", "w"}});
        tk::StringVar kind("Szafka"); (new ttk::Combobox(d, {{"textvariable", kind}, {"values", std::vector<std::string>{"Szafka", "Szafa", "Regał", "Stół"}}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 5}});
        std::map<std::string, tk::StringVar> fields;
        for (auto& [k, label, v] : std::vector<std::tuple<std::string, std::string, json>>{{"group", "Nazwa mebla", "Mebel 1"}, {"w", "Szerokość X [mm]", 800}, {"h", "Wysokość Z [mm]", 1200}, {"d", "Głębokość Y [mm]", 480}, {"t", "Grubość płyty [mm]", 18}, {"s", "Liczba półek", 2}, {"x", "Pozycja X [mm]", 0}, {"y", "Pozycja Y [mm]", 0}, {"z", "Pozycja Z [mm]", 0}})
            fields.emplace(k, field(d, label, v));
        tk::BooleanVar front(true); (new ttk::Checkbutton(d, {{"text", "Dodaj dwa fronty (szafka / szafa)"}, {"variable", front}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 10}});
        (new ttk::Label(d, {{"text", "Plecy nakładane 3 mm. Fronty: szczeliny po 2 mm.\nKonstrukcja robocza; po dodaniu edytuj każdą część."}, {"padding", 15}}))->pack({{"anchor", "w"}});
        auto submit = [this, d, kind, fields, front]() mutable {
            try {
                ProjectPtr temp = std::make_shared<Project>(*project);
                double s = number(fields["s"]);
                if (s != std::trunc(s)) throw ValueError("Liczba półek musi być całkowita.");
                std::string kind_key = std::map<std::string, std::string>{{"Szafka", "cabinet"}, {"Szafa", "wardrobe"}, {"Regał", "shelf"}, {"Stół", "table"}}.at(kind.get());
                auto parts = add_template(*temp, kind_key, number(fields["w"]), number(fields["h"]), number(fields["d"]), number(fields["t"]), (int)s, front.get(), std::array<double, 3>{number(fields["x"]), number(fields["y"]), number(fields["z"])}, fields["group"].get());
                temp->validate(); snapshot(); project = temp; selected = parts.at(0).id; refresh(); fit(); d->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Sprawdź dane", e.what(), d); }
        };
        (new ttk::Button(d, {{"text", "Dodaj do projektu"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 10}});
    }

    void contour() {
        Part* p = selected_part();
        if (!p) return;
        if (py::truthy(p->assembly)) {
            edit_sketch(); return;
        }
        std::string part_id = p->id;
        auto* d = dialog("Kontur i otwory · " + p->name, "650x650");
        (new ttk::Label(d, {{"text", "Kontur lokalny U;V · jeden punkt na wiersz"}, {"padding", 12}}))->pack({{"anchor", "w"}});
        auto* points = new tk::Text(d, {{"height", 9}, {"font", "Consolas 11"}}); points->pack({{"fill", "both"}, {"expand", true}, {"padx", 12}});
        if (!p->contour.empty()) {
            std::vector<std::string> lines; for (auto& pt : p->contour) lines.push_back(std::format("{:g};{:g}", pt[0], pt[1]));
            points->insert("1.0", py::join(lines, "\n"));
        }
        (new ttk::Label(d, {{"text", "Puste pole = prostokąt. Minimum U i V musi wynosić 0.\nKontur zamykany automatycznie. Nieregularne obrzeża nie są obsługiwane."}, {"padding", 12}}))->pack({{"anchor", "w"}});
        (new ttk::Label(d, {{"text", "Otwory od strony +T: U;V;średnica;głębokość [mm]"}, {"padding", 12}}))->pack({{"anchor", "w"}});
        auto* holes = new tk::Text(d, {{"height", 7}, {"font", "Consolas 11"}}); holes->pack({{"fill", "both"}, {"expand", true}, {"padx", 12}});
        {
            std::vector<std::string> lines;
            for (auto& hole : p->holes) { std::vector<std::string> cells; for (double x : hole) cells.push_back(std::format("{:g}", x)); lines.push_back(py::join(cells, ";")); }
            holes->insert("1.0", py::join(lines, "\n"));
        }
        (new ttk::Label(d, {{"text", "Otwory zapisują się w CSV i raporcie; DXF zawiera tylko koła.\nW widoku 3D włącz „Otwory montażowe”, aby zobaczyć pozycje wierceń."}, {"padding", 12}}))->pack({{"anchor", "w"}});
        auto parse = [](tk::Text* widget, size_t n) {
            std::vector<std::vector<double>> result;
            for (auto& line : py::splitlines(py::strip(widget->get("1.0", "end")))) {
                std::vector<double> values; for (auto& i : py::split(line, ';')) values.push_back(py::to_float(py::replace_all(py::strip(i), ",", ".")));
                if (values.size() != n) throw ValueError(std::format("Każdy wiersz wymaga {} wartości rozdzielonych średnikiem.", n));
                result.push_back(values);
            }
            return result;
        };
        auto submit = [this, d, points, holes, parse, part_id] {
            try {
                d->ensure_current_project(true);
                Part* current = require_part(part_id); Part q = *current; q.contour = parse(points, 2); q.holes = parse(holes, 4);
                if (q.contour != current->contour && (py::truthy(current->profile) || py::truthy(current->fillets) || py::truthy(current->chamfers)))
                    throw ValueError("Kontur z profilem lub obróbką kantów zmień w edytorze szkicu / CAD.");
                if (!q.contour.empty()) q.edges = std::vector<bool>(4, false);
                size_t index = current - project->parts.data();
                q.validate(); snapshot(); project->parts[index] = q; refresh(); d->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Sprawdź kontur", e.what(), d); }
        };
        (new ttk::Button(d, {{"text", "Zapisz kontur i otwory"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 12}, {"pady", 12}});
    }

    void move() {
        Part* p = selected_part();
        if (!p) return;
        if (py::truthy(p->assembly) || py::truthy(p->operations)) {
            tk::messagebox::showinfo("Montaż", "Użyj „Edytuj szkic” dla elementów powiązanych i podpór okuć.", this); return;
        }
        std::string part_id = p->id;
        auto* d = dialog("Przesunięcie elementu", "450x280"); std::vector<tk::StringVar> fields; for (std::string axis : {"X", "Y", "Z"}) fields.push_back(field(d, "Przesunięcie " + axis + " [mm]", 0));
        tk::BooleanVar duplicate_; (new ttk::Checkbutton(d, {{"text", "Utwórz kopię"}, {"variable", duplicate_}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 10}});
        auto submit = [this, d, fields, duplicate_, part_id]() mutable {
            try {
                d->ensure_current_project(true);
                Part* current = require_part(part_id); Part q = *current;
                std::vector<std::string> keys{"x", "y", "z"};
                for (size_t i = 0; i < keys.size(); ++i) q.set_attr(keys[i], q.get_attr(keys[i]).get<double>() + number(fields[i]));
                size_t index = current - project->parts.data();
                q.validate(); snapshot();
                if (duplicate_.get()) { q.id = uid(); q.name += " kopia"; project->parts.push_back(q); }
                else project->parts[index] = q;
                selected = q.id; refresh(); d->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Sprawdź dane", e.what(), d); }
        };
        (new ttk::Button(d, {{"text", "Zastosuj"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 10}});
    }

    void settings() {
        auto* d = dialog("Materiały i ustawienia produkcji", "680x850");
        (new ttk::Label(d, {{"text", "Materiały · ceny własne"}, {"style", "Title.TLabel"}, {"padding", 15}}))->pack({{"anchor", "w"}});
        std::vector<std::string> names; for (auto& m : project->materials) names.push_back(m.name);
        tk::StringVar mv(project->materials[0].name); auto* combo = new ttk::Combobox(d, {{"textvariable", mv}, {"values", names}, {"state", "readonly"}}); combo->pack({{"fill", "x"}, {"padx", 15}});
        std::map<std::string, tk::StringVar> vals;
        for (auto& [k, label] : std::vector<std::pair<std::string, std::string>>{{"name", "Nazwa materiału"}, {"kind", "Rodzaj: plyta/mdf/sklejka/drewno/hdf/inne"}, {"color", "Kolor #RRGGBB"}, {"price_m2", "Cena płyt [zł/m²]"}, {"price_m3", "Cena drewna litego [zł/m³]"}, {"sheet_u", "Długość surowca U [mm]"}, {"sheet_v", "Szerokość surowca V [mm]"}})
            vals.emplace(k, field(d, label, project->materials[0].get_attr(k)));
        std::function<void()> load = [this, combo, vals]() mutable {
            Material& m = py::index(project->materials, combo->current());
            for (auto& [k, v] : vals) v.set(py::str(m.get_attr(k)));
        };
        combo->bind("<<ComboboxSelected>>", [load](tk::Event&) -> std::string { load(); return ""; });
        auto choose = [d, vals]() mutable {
            auto col = tk::colorchooser::askcolor(vals["color"].get(), d).second;
            if (col && !col->empty()) vals["color"].set(*col);
        };
        (new ttk::Button(d, {{"text", "Wybierz kolor"}, {"command", std::function<void()>(choose)}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 8}});
        (new ttk::Separator(d))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 8}});
        std::map<std::string, tk::StringVar> setting;
        for (auto& [k, label] : std::vector<std::pair<std::string, std::string>>{{"kerf", "Rzaz piły [mm]"}, {"margin", "Margines płyty [mm]"}, {"edge_t", "Grubość obrzeża [mm]"}, {"edge_price", "Obrzeże z usługą [zł/m]"}, {"labor", "Robocizna projektu [zł]"}, {"markup", "Narzut na koszt [%]"}})
            setting.emplace(k, field(d, label, project->get_attr(k)));
        (new ttk::Label(d, {{"text", "Włókna biegną wzdłuż U płyty. Narzut nie jest marżą.\nCeny są przykładowe; wpisz ceny swojego dostawcy.\nZapisz wybrany materiał przed przejściem do kolejnego."}, {"padding", 15}}))->pack({{"anchor", "w"}});
        auto submit = [this, d, combo, vals, setting, mv]() mutable {
            try {
                ProjectPtr temp = std::make_shared<Project>(*project); Material& m = py::index(temp->materials, combo->current());
                for (auto& [k, v] : vals) m.set_attr(k, (k == "name" || k == "color" || k == "kind") ? json(v.get()) : json(number(v)));
                for (auto& [k, v] : setting) temp->set_attr(k, number(v));
                temp->validate(); snapshot(); project = temp; refresh();
                std::vector<std::string> names_; for (auto& mat : temp->materials) names_.push_back(mat.name);
                combo->configure({{"values", names_}}); mv.set(m.name); status.set("Zapisano materiał i ceny");
            } catch (const ValueError& e) { tk::messagebox::showerror("Sprawdź dane", e.what(), d); }
        };
        (new ttk::Button(d, {{"text", "Zapisz materiał i ustawienia"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 8}});
        auto add = [this, combo, load] {
            snapshot(); Material m; m.name = "Nowy materiał"; project->materials.push_back(m);
            std::vector<std::string> names_; for (auto& mat : project->materials) names_.push_back(mat.name);
            combo->configure({{"values", names_}}); combo->current((int)project->materials.size() - 1); load(); refresh();
        };
        (new ttk::Button(d, {{"text", "+ Nowy materiał"}, {"command", std::function<void()>(add)}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 4}});
        (new ttk::Button(d, {{"text", "Zamknij"}, {"command", std::function<void()>([d] { d->destroy(); })}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 4}});
    }

    void hardware() {
        auto* d = dialog("Akcesoria · ręczne zestawienie", "650x480");
        (new ttk::Label(d, {{"text", "Nazwa; ilość; cena jednostkowa [zł] · jeden wpis na wiersz"}, {"padding", 15}}))->pack({{"anchor", "w"}});
        auto* txt = new tk::Text(d, {{"font", "Consolas 11"}}); txt->pack({{"fill", "both"}, {"expand", true}, {"padx", 15}});
        std::vector<std::string> lines;
        for (auto& h : project->hardware) if (!py::truthy(py::get(h, "owner_id"))) lines.push_back(py::str(h["name"]) + ";" + py::str(h["qty"]) + ";" + py::str(h["price"]));
        txt->insert("1.0", py::join(lines, "\n"));
        std::vector<std::string> automatic;
        for (auto& h : project->hardware) if (py::truthy(py::get(h, "owner_id"))) automatic.push_back(std::format("{:g} × {}", h["qty"].get<double>(), py::str(h["name"])));
        (new ttk::Label(d, {{"text", "Ręczne akcesoria: nazwa;ilość;cena.\nAutomatyczne okucia zmienisz przez „Edytuj szkic” i parametry okuć.\n" + py::join(automatic, ", ")}, {"padding", 15}, {"wraplength", 600}}))->pack({{"anchor", "w"}});
        auto submit = [this, d, txt] {
            try {
                json result = json::array();
                for (auto& line : py::splitlines(py::strip(txt->get("1.0", "end")))) {
                    auto cells = py::split(line, ';');
                    if (cells.size() != 3) throw ValueError(cells.size() < 3 ? std::format("not enough values to unpack (expected 3, got {})", cells.size()) : "too many values to unpack (expected 3)");
                    result.push_back({{"name", py::strip(cells[0])}, {"qty", to_number(cells[1])}, {"price", to_number(cells[2])}});
                }
                d->ensure_current_project(true);
                json automatic_current = json::array();
                for (auto& h : project->hardware) if (py::truthy(py::get(h, "owner_id"))) automatic_current.push_back(h);
                ProjectPtr temp = std::make_shared<Project>(*project);
                for (auto& h : automatic_current) result.push_back(h);
                temp->hardware = result; temp->validate(); snapshot(); project = temp; refresh(); d->destroy();
            } catch (const ValueError& e) { tk::messagebox::showerror("Sprawdź dane", e.what(), d); }
        };
        (new ttk::Button(d, {{"text", "Zapisz zestawienie"}, {"command", std::function<void()>(submit)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 10}});
    }

    bool confirm_save() {
        if (!dirty) return true;
        std::optional<bool> ans = tk::messagebox::askyesnocancel("Niezapisany projekt", "Zapisać zmiany przed kontynuowaniem?", this);
        if (!ans) return false;
        return *ans ? save() : true;
    }
    bool confirm_project_transition() {
        if (!flush_parameter_drafts()) return false;
        return workspace->confirm_project_change() && confirm_save();
    }
    void end_project_documents() {
        selected_scene.reset();
        cancel_active_edits();
        workspace->close_project_documents();
        project_generation += 1;
        if (animation_job) {
            after_cancel(*animation_job);
            animation_job.reset();
        }
        open_amounts = {}; motion_value.set(0);
    }
    bool new_() {
        if (!confirm_project_transition()) return false;
        end_project_documents();
        project = std::make_shared<Project>();
        palette::install_palette(project);
        if (_native_document != nullptr) _native_document->reset(project);
        open_amounts = {}; animation_job.reset(); motion_value.set(0); motion_all.set(false); resize_step.set(false);
        path.reset(); selected.reset(); undo_stack->clear(); redo_stack->clear(); dirty = false; title("Mebel Studio 3D · Mój projekt"); refresh(); fit(); workspace->select(design_page); journal::file_event(this, "nowy projekt", "", 0, "OK"); return true;
    }
    bool save(bool save_as = false) {
        if (!flush_parameter_drafts()) return false;
        cancel_active_edits();
        _sync_native_document();
        std::string path_ = path.value_or("");
        if (save_as || path_.empty()) path_ = tk::filedialog::asksaveasfilename({{"parent", this}, {"title", "Zapisz projekt"}, {"defaultextension", ".mebel"}, {"filetypes", tk::FileTypes{{"Projekt Mebel Studio", "*.mebel"}}}, {"initialfile", project->name + ".mebel"}});
        if (path_.empty()) return false;
        std::string previous_name = project->name;
        auto save_started = std::chrono::steady_clock::now();
        auto save_ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - save_started).count(); };
        auto on_error = [&](const std::exception& e) {
            journal::file_event(this, "zapis", path_, save_ms(), std::string("BŁĄD: ") + e.what());
            project->name = previous_name;
            if (_native_document != nullptr) {
                std::string authoritative = _native_document->read();
                if (native_document::serialize(project) != authoritative) project = Project::from_data(json::parse(authoritative));
                _native_dirty_state();
            }
            tk::messagebox::showerror("Nie udało się zapisać", e.what(), this); return false;
        };
        try {
            project->name = py::path_str(py::to_path(path_).stem());
            if (_native_document != nullptr) _native_document->save(project, path_);
            else project->save(path_);
            path = path_; dirty = false; title("Mebel Studio 3D · " + project->name); status.set("Zapisano: " + path_); log->info("Zapisano projekt: " + path_);
            journal::file_event(this, "zapis", path_, save_ms(), std::format("OK · {} części", project->parts.size())); return true;
        } catch (const OSError& e) { return on_error(e); } catch (const ValueError& e) { return on_error(e); }
    }
    bool open() {
        std::string path_ = tk::filedialog::askopenfilename({{"parent", this}, {"filetypes", tk::FileTypes{{"Projekt Mebel Studio", "*.mebel"}}}});
        return !path_.empty() ? load_path(path_) : false;
    }
    bool load_path(const std::string& path_) {
        ProjectPtr project_;
        auto load_started = std::chrono::steady_clock::now();
        auto load_ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - load_started).count(); };
        auto on_error = [&](const std::exception& e) { journal::file_event(this, "otwarcie", path_, load_ms(), std::string("BŁĄD: ") + e.what()); tk::messagebox::showerror("Nie udało się otworzyć projektu", e.what(), this); return false; };
        try {
            project_ = Project::load(path_);
        } catch (const OSError& e) { return on_error(e); } catch (const ValueError& e) { return on_error(e); }
        catch (const KeyError& e) { return on_error(e); } catch (const TypeError& e) { return on_error(e); } catch (const StopIteration& e) { return on_error(e); }
        if (!confirm_project_transition()) return false;
        end_project_documents();
        if (_native_document != nullptr) _native_document->reset(project_);
        project = project_; path = path_; dirty = false; undo_stack->clear(); redo_stack->clear(); selected.reset(); refresh(); fit();
        journal::file_event(this, "otwarcie", path_, load_ms(), std::format("OK · {} części, {} okuć", project_->parts.size(), project_->hardware.size())); workspace->select(design_page); title("Mebel Studio 3D · " + project_->name); status.set("Otwarto: " + path_); log->info("Otwarto projekt: " + path_); return true;
    }
    bool close() {
        if (!confirm_project_transition()) return false;
        if (ui_layout) {
            try {
                ui_layout->persist();
            } catch (const OSError& error) { log->warning(std::format("Nie zapisano układu interfejsu: {}", error.what())); }
            catch (const ValueError& error) { log->warning(std::format("Nie zapisano układu interfejsu: {}", error.what())); }
        }
        end_project_documents(); destroy(); return true;
    }

    void destroy() override {
        if (_native_document != nullptr) _native_document->close();
        if (canvas) {
            try {
                native_viewport::close_native(canvas);
                gpu_viewport::close_gpu(canvas);
            } catch (const std::runtime_error&) {}  // RuntimeError, OSError
        }
        if (log) {
            log->info("Zamknięto program"); developer_logs::dispose(this);
        }
        tk::Tk::destroy();
    }

    bool export_() {
        if (!flush_parameter_drafts()) return false;
        cancel_active_edits(); _sync_native_document();
        auto* d = dialog("Eksport dokumentacji", "610x760");
        (new ttk::Label(d, {{"text", "Dokumentacja projektu"}, {"style", "Title.TLabel"}, {"padding", 15}}))->pack({{"anchor", "w"}});
        auto run = [this, d](const std::string& kind) {
            std::string ext = std::map<std::string, std::string>{{"csv", ".csv"}, {"dxf", ".dxf"}, {"html", ".html"}, {"svg", ".svg"}, {"json", ".json"}, {"step", ".step"}, {"stl", ".stl"}, {"drawings", ".html"}, {"labels", ".html"}, {"project_print", ".html"}, {"cutting", ".html"}, {"cutting_dxf", ".dxf"}}.at(kind);
            std::string path_ = tk::filedialog::asksaveasfilename({{"parent", d}, {"defaultextension", ext}, {"initialfile", project->name + ext}, {"filetypes", tk::FileTypes{{"Eksport", "*" + ext}}}});
            if (path_.empty()) return;
            auto export_started = std::chrono::steady_clock::now();
            auto export_ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - export_started).count(); };
            auto on_error = [&](const std::exception& e) { journal::file_event(this, "eksport " + kind, path_, export_ms(), std::string("BŁĄD: ") + e.what()); tk::messagebox::showerror("Błąd eksportu", e.what(), d); };
            try {
                project->validate();
                if (kind == "csv") csv_export(*project, path_);
                else if (kind == "dxf") dxf_export(*project, path_);
                else if (kind == "html") report_export(*project, path_);
                else if (kind == "json") manufacturing_export(*project, path_);
                else if (kind == "drawings" || kind == "labels") {
                    if (kind == "drawings") production_docs::drawings_export(project, path_); else production_docs::labels_export(project, path_);
                } else if (kind == "cutting") {
                    sheet_nesting::sheets_export(*project, path_);
                } else if (kind == "cutting_dxf") {
                    sheet_nesting::sheets_dxf_export(*project, path_);
                } else if (kind == "project_print") {
                    project_print::export_project_print(project, path_);
                } else if (kind == "stl") {
                    cad_geometry::stl_export(*project, path_);
                } else if (kind == "step") {
                    cad_geometry::step_export(*project, path_);
                } else {
                    Camera cam = camera; cam.fit(*project, 1400, 900); py::write_text(path_, svg_view(*project, &cam, 1400, 900));
                }
                status.set("Wyeksportowano: " + path_);
                journal::file_event(this, "eksport " + kind, path_, export_ms(), "OK");
                if (kind == "html" || kind == "svg" || kind == "drawings" || kind == "labels" || kind == "project_print" || kind == "cutting") py::webbrowser_open(py::as_uri(fs::weakly_canonical(py::to_path(path_))));
                else tk::messagebox::showinfo("Eksport gotowy", "Zapisano plik:\n" + path_, d);
            } catch (const OSError& e) { on_error(e); } catch (const ValueError& e) { on_error(e); }
        };
        for (auto& [label, kind] : std::vector<std::pair<std::string, std::string>>{{"Komplet + rozstrzelone szafki + rozkrój · druk / PDF", "project_print"}, {"Rozkrój płyt · arkusze z numerami · druk", "cutting"}, {"Rozkrój płyt · DXF (płyty i formatki)", "cutting_dxf"}, {"Formatki CSV · Excel / rozkrój", "csv"}, {"Kontury i otwory DXF · dokumentacja 2D", "dxf"}, {"Bryły STEP · dokładna geometria obróbek", "step"}, {"Model STL · siatka 3D", "stl"}, {"Rysunki części · wymiary i obróbki", "drawings"}, {"Etykiety części · druk A4", "labels"}, {"Dane produkcyjne JSON · geometria i wiercenia", "json"}, {"Raport HTML · wydruk / zapis PDF", "html"}, {"Widok SVG · prezentacja klientowi", "svg"}})
            (new ttk::Button(d, {{"text", label}, {"command", std::function<void()>([run, k = kind] { run(k); })}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 6}});
        (new ttk::Label(d, {{"text", "DXF nie jest programem maszyny CNC.\nRaport zawiera model, rzuty, formatki, rozkrój i kosztorys.\nPDF zapiszesz z raportu przez „Drukuj”."}, {"padding", 15}}))->pack({{"anchor", "w"}});
        return true;
    }

    // Z zakładki Rozkrój: zapis arkuszy rozkroju (HTML do druku albo DXF).
    void export_cut_plan(const std::string& kind) {
        if (!flush_parameter_drafts()) return;
        std::string ext = kind == "cutting_dxf" ? ".dxf" : ".html";
        std::string path_ = tk::filedialog::asksaveasfilename({{"parent", this}, {"defaultextension", ext}, {"initialfile", project->name + " — rozkrój" + ext}, {"filetypes", tk::FileTypes{{"Rozkrój", "*" + ext}}}});
        if (path_.empty()) return;
        auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
        try {
            project->validate();
            if (kind == "cutting_dxf") sheet_nesting::sheets_dxf_export(*project, path_); else sheet_nesting::sheets_export(*project, path_);
            status.set("Wyeksportowano rozkrój: " + path_);
            journal::file_event(this, "eksport " + kind, path_, elapsed(), "OK");
            if (kind == "cutting") py::webbrowser_open(py::as_uri(fs::weakly_canonical(py::to_path(path_))));
        } catch (const std::exception& e) {
            journal::file_event(this, "eksport " + kind, path_, elapsed(), std::string("BŁĄD: ") + e.what());
            tk::messagebox::showerror("Błąd eksportu rozkroju", e.what(), this);
        }
    }

    void help() {
        fs::path path_ = ROOT / "INSTRUKCJA.html";
        if (fs::exists(path_)) py::webbrowser_open(py::as_uri(path_));
        else tk::messagebox::showinfo("Sterowanie", "Dodaj mebel lub element. Zaznacz część i zmień właściwości.\nLPM obraca widok; PPM przesuwa; rolka powiększa.\nCtrl+S zapisuje; Ctrl+Z cofa; Ctrl+Y ponawia.", this);
    }
};
