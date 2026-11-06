#include "studio_interaction.h"

#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <tuple>

#include "app.h"
#include "attachment_picker.h"
#include "corner_furniture.h"
#include "depth_raster.h"
#include "furniture_configurator.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "room_scene_drag.h"
#include "room_scene_ui.h"
#include "workspace_worktops.h"

namespace studio_interaction {

using core::ValueError;
using Cmd = std::function<void()>;

namespace {

bool starts_with(const std::optional<std::string>& value, const char* prefix) {
    return value && value->rfind(prefix, 0) == 0;
}

}  // namespace

std::optional<std::string> pick(App* app, int x, int y) {
    auto* canvas = app->canvas;
    if (canvas->_native_pick) return depth_raster::pick_visible(canvas, x, y);
    if (canvas->_id_buffer) return depth_raster::pick_visible(canvas, x, y);
    const json& drawn = app->drawn;
    for (auto face = drawn.rbegin(); face != drawn.rend(); ++face) {
        std::vector<core::Point> points;
        for (const auto& p : face->at("points")) points.push_back({p.at(0).get<double>(), p.at(1).get<double>()});
        if (core::inside({static_cast<double>(x), static_cast<double>(y)}, points)) return face->at("id").get<std::string>();
    }
    return std::nullopt;
}

void select(App* app, const std::optional<std::string>& identity) {
    if (!app->flush_parameter_drafts()) return;
    if (identity && !identity->empty() && starts_with(identity, "scene:room:")) return;
    if (identity && !identity->empty() && starts_with(identity, "scene:")) {
        app->selected.reset();
        app->selected_scene = identity->substr(6);
        app->inspector_tabs->select(app->room_scene_panel);
        app->room_scene_panel->refresh();
        app->draw();
        return;
    }
    app->selected_scene.reset();
    app->selected = identity;
    if (identity && !identity->empty() && app->tree->exists(*identity)) {
        app->tree->selection_set(*identity);
        app->tree->see(*identity);
    } else {
        app->tree->selection_remove(app->tree->selection());
    }
    app->load_form();
    app->draw();
}

void configure_change(App* app, const json& changes) {
    core::Part* part = app->selected_part();
    if (!part) throw ValueError("Wybierz mebel w widoku 3D.");
    std::string group = part->group;
    json cfg = furniture_configurator::read_config(*app->project, group);
    if (!py::truthy(cfg)) throw ValueError("Otwórz właściwości elementu lub edytor narożnika dla tego mebla.");
    for (auto& [key, value] : changes.items()) cfg[key] = value;
    auto result = furniture_configurator::configure(*app->project, cfg, group);
    app->snapshot();
    app->project = result.first;
    app->refresh();
}

void set_mode(App* app, const std::string& mode, const std::optional<std::pair<double, double>>& origin) {
    app->edit_mode.set(mode);
    if (origin) app->gizmo_origin = origin;
    app->draw();
}

tk::Menu* context_menu(App* app, const tk::Event& event, bool show) {
    auto identity = pick(app, event.x, event.y);
    if (identity) select(app, identity);
    auto* menu = new tk::Menu(app->canvas, {{"tearoff", false}});
    app->context_menu = menu;
    auto action = [app, menu](const std::string& label, Cmd fn, tk::Menu* parent = nullptr) {
        auto run = [app, menu, label, fn = std::move(fn)] {
            menu->unpost();
            try {
                fn();
            } catch (const std::invalid_argument& error) {  // ValueError
                app->log->warning(label + ": " + error.what());
                app->status.set(error.what());
            } catch (const std::system_error& error) {      // OSError
                app->log->warning(label + ": " + error.what());
                app->status.set(error.what());
            }
        };
        (parent ? parent : menu)->add_command({{"label", label}, {"command", Cmd(run)}});
    };
    if (app->selected_scene) {
        action("Ustawienia wyposażenia", [app] { app->inspector_tabs->select(app->room_scene_panel); });
        action("Obróć wyposażenie +90°", [app] { app->room_scene_panel->rotate_selected(90); });
        action("Usuń wyposażenie", [app] { app->room_scene_panel->delete_selected(); });
    } else {
        core::Part* part = app->selected_part();
        std::string group = part ? part->group : "";
        json corner = part ? corner_furniture::read_corner_config(*app->project, group) : json(nullptr);
        json shared = part ? workspace_worktops::read_shared_worktop(*app->project, group) : json(nullptr);
        bool is_corner = py::truthy(corner);
        if (py::truthy(shared)) {
            action("Dopasuj wspólny blat do szafek", [app, group] {
                auto result = workspace_worktops::fit_shared_worktop(*app->project, group);
                app->snapshot();
                app->project = result;
                app->refresh();
            });
            action("Wymiary i kontur blatu", [app] { app->inspector_tabs->select("1"); });
        }
        action("Wymiary i typ mebla", [app, is_corner] {
            if (is_corner) app->inspector_tabs->select(app->corner_panel);
            else quick_studio_ui::focus_section(app, "type");
        });
        auto* styles = new tk::Menu(menu, {{"tearoff", false}});
        menu->add_cascade({{"label", "Styl frontów"}, {"menu", styles}});
        for (auto [label, style] : std::vector<std::pair<std::string, std::string>>{
                 {"Gładki", "smooth"}, {"Ramka frezowana", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}})
            action(label, [app, style = style] { configure_change(app, json{{"front_style", style}}); }, styles);
        auto* fronts = new tk::Menu(menu, {{"tearoff", false}});
        menu->add_cascade({{"label", "Układ frontów"}, {"menu", fronts}});
        struct Layout { const char* label; const char* mode; int count; };
        for (const Layout& item : {Layout{"Otwarty", "open", 0}, Layout{"Drzwi", "doors", 1}, Layout{"2 szuflady", "drawers", 2},
                                   Layout{"3 szuflady", "drawers", 3}, Layout{"4 szuflady", "drawers", 4}})
            action(item.label, [app, mode = std::string(item.mode), count = item.count] { app->quick_inspector->quick_front(mode, count); }, fronts);
        action("Zawiasy / prowadnice", [app] { quick_studio_ui::focus_section(app, "fittings"); });
        action("Nóżki / blat / wzmocnienia", [app] { quick_studio_ui::focus_section(app, "legs"); });
        menu->add_separator();
        action("Rozciągaj cały mebel · osie przy kursorze", [app, x = event.x, y = event.y] { set_mode(app, "furniture", std::make_pair<double, double>(x, y)); });
        action("Przesuwaj mebel · magnes", [app] { set_mode(app, "layout"); });
        action("Obróć mebel −90°", [app] { app->rotate_furniture(-90); });
        action("Obróć mebel +90°", [app] { app->rotate_furniture(90); });
        action("Połącz / ustaw układ L lub U…", [app] { app->layout_dialog(); });
        action("Narożnik L / ścięty / zaokrąglony…", [app] { app->corner_dialog(); });
        if (part && !is_corner) {
            auto convert = [app, group](const std::string& shape, const std::string& side) {
                auto result = corner_furniture::convert_to_corner(*app->project, group, shape, side);
                app->snapshot();
                app->project = result.first;
                auto chosen = std::find_if(result.first->parts.begin(), result.first->parts.end(), [&](const core::Part& p) { return p.group == result.second; });
                if (chosen == result.first->parts.end()) throw py::StopIteration();
                app->selected = chosen->id;
                app->refresh();
                app->inspector_tabs->select(app->corner_panel);
                app->fit();
            };
            auto* corners = new tk::Menu(menu, {{"tearoff", false}});
            menu->add_cascade({{"label", "Zmień ten mebel na narożny"}, {"menu", corners}});
            for (auto [shape, label] : std::vector<std::pair<std::string, std::string>>{{"L", "L"}, {"chamfer", "Ścięty"}, {"rounded", "Zaokrąglony"}})
                for (auto [side, sl] : std::vector<std::pair<std::string, std::string>>{{"left", "lewy"}, {"right", "prawy"}})
                    action(label + " · " + sl, [convert, shape = shape, side = side] { convert(shape, side); }, corners);
        }
        action("Wspólny blat dla szafek…", [app] { app->shared_worktop_dialog(); });
        action("Kopia całego mebla", [app] { app->quick_inspector->duplicate_group(); });
    }
    menu->add_separator();
    action("Dopasuj widok", [app] { app->fit(); });
    action("Logi developerskie", [app] { app->open_developer_logs(); });
    if (show) {
        try {
            menu->tk_popup(event.x_root, event.y_root);
        } catch (...) {
            menu->grab_release();
            throw;
        }
        menu->grab_release();
    }
    return menu;
}

void paint_gizmo(App* app) {
    if (app->edit_mode.get() != "furniture" || app->handles.empty()) return;
    tk::Canvas* c = app->canvas;
    double w = c->winfo_width(), h = c->winfo_height();
    auto [x0, y0] = app->gizmo_origin ? *app->gizmo_origin : std::make_pair(w * .56, h * .53);
    double x = std::max(86.0, std::min(w - 86, x0)), y = std::max(86.0, std::min(h - 86, y0));
    c->create_oval(x - 64, y - 64, x + 64, y + 64, {{"fill", "#e7edf2"}, {"outline", "#889baa"}, {"width", 2}, {"stipple", "gray50"}, {"tags", "axis_gizmo"}});
    // Move the actual draggable controls to the wheel; their world-to-screen
    // derivative still determines the correct dimensional change in millimetres.
    for (auto& handle : app->handles) {
        char axis = handle.face.at(0), end = handle.face.at(1);
        double vx = handle.vector[0], vy = handle.vector[1], length = std::hypot(vx, vy);
        if (length == 0) throw std::domain_error("float division by zero");  // ZeroDivisionError
        double sign = end == '0' ? -1 : 1;
        double hx = x + vx / length * sign * 51, hy = y + vy / length * sign * 51;
        const char* color;
        switch (axis) {
            case 'x': color = "#c95048"; break;
            case 'y': color = "#288563"; break;
            case 'z': color = "#3e77bd"; break;
            default: throw std::out_of_range(std::string("'") + axis + "'");
        }
        handle.x = hx;
        handle.y = hy;
        c->create_line({x, y, hx, hy}, {{"fill", color}, {"width", 3}, {"tags", "axis_gizmo"}});
        c->create_oval(hx - 8, hy - 8, hx + 8, hy + 8, {{"fill", color}, {"outline", "white"}, {"width", 2}, {"tags", "axis_gizmo"}});
        c->create_text(hx, hy - 15, {{"text", std::string(1, static_cast<char>(std::toupper(axis))) + (end == '0' ? "−" : "+")}, {"fill", color},
                                     {"font", "{Segoe UI} 9 bold"}, {"tags", "axis_gizmo"}});
    }
    c->create_text(x, y + 80, {{"text", "Przeciągnij oś · PPM: opcje"}, {"fill", "#243144"}, {"font", "{Segoe UI} 9"}, {"tags", "axis_gizmo"}});
}

namespace {

// Stan zamknięć z install(): odpowiada atrybutom app._view_last, _view_finish_job, _camera_gesture itd.
struct InstallState {
    double view_last = 0;
    std::optional<std::string> finish_job;
    std::optional<std::tuple<int, int, bool>> camera_gesture;
    bool hardware_click = false;
    std::pair<int, int> right_start{0, 0};
    bool right_dragged = false;
    double last_warning = 0;
};

}  // namespace

void install(App* app) {
    app->selected_scene.reset();
    app->_view_interactive = false;
    auto st = std::make_shared<InstallState>();
    app->pick_attachment_target = [app](double x, double y) { return attachment_picker::pick(app, static_cast<int>(x), static_cast<int>(y)); };
    app->viewport_stats = json{{"last_ms", 0}, {"frames", 0}};
    app->draw_wrapper = [app, st] {
        auto start = std::chrono::steady_clock::now();
        app->draw_original();
        paint_gizmo(app);
        if (app->hardware_drag) app->hardware_drag->draw_ghost();
        double duration = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        app->viewport_stats["last_ms"] = duration;
        app->viewport_stats["frames"] = app->viewport_stats["frames"].get<long long>() + 1;
        if (duration > 700 && py::monotonic() - st->last_warning > 15) {
            st->last_warning = py::monotonic();
            app->log->warning(std::format("Wolna klatka 3D: {:.0f} ms, {} części, {} ścian, tryb ruchu {}", duration, app->project->parts.size(),
                                          app->drawn.size(), app->_view_interactive ? "True" : "False"));
        }
    };
    auto finish = [app, st]() -> std::string {
        if (st->finish_job) {
            app->after_cancel(*st->finish_job);
            st->finish_job.reset();
        }
        app->_view_interactive = false;
        app->draw();
        return "break";
    };
    auto request_finish = [app, st, finish] {
        if (st->finish_job) app->after_cancel(*st->finish_job);
        st->finish_job = app->after(160, [finish] { finish(); });
    };
    auto begin = [app, st](tk::Event& event) -> std::string {
        app->canvas->focus_set();
        st->camera_gesture = std::make_tuple(event.x, event.y, (event.state & 1) != 0);
        return "break";
    };
    auto move = [app, st](tk::Event& event) -> std::string {
        if (!st->camera_gesture) return "break";
        auto [x, y, pan] = *st->camera_gesture;
        int dx = event.x - x, dy = event.y - y;
        st->camera_gesture = std::make_tuple(event.x, event.y, pan);
        if (pan) {
            app->camera.pan = {app->camera.pan[0] + dx, app->camera.pan[1] + dy};
        } else {
            app->camera.yaw += dx * .45;
            app->camera.pitch = std::max(-89.0, std::min(89.0, app->camera.pitch + dy * .4));
        }
        app->_view_interactive = true;
        if (py::monotonic() - st->view_last > .025) {
            st->view_last = py::monotonic();
            app->draw();
        }
        return "break";
    };
    auto release = [st, finish](tk::Event&) -> std::string {
        st->camera_gesture.reset();
        return finish();
    };
    app->canvas->bind("<ButtonPress-2>", begin);
    app->canvas->bind("<B2-Motion>", move);
    app->canvas->bind("<ButtonRelease-2>", release);
    app->canvas->bind("<Alt-ButtonPress-1>", begin);
    app->canvas->bind("<Alt-B1-Motion>", move);
    app->canvas->bind("<Alt-ButtonRelease-1>", release);

    app->canvas->bind("<B1-Motion>", [app, request_finish](tk::Event& event) -> std::string {
        if (app->hardware_drag && app->hardware_drag->active()) return app->hardware_drag->move(event).value_or("");
        if (app->scene_drag) {
            room_scene_drag::move(app, event);
            return "break";
        }
        app->_view_interactive = true;
        app->orbit(event);
        request_finish();
        return "";
    });
    app->canvas->bind("<ButtonPress-1>", [app, st](tk::Event& event) -> std::string {
        if (!app->flush_parameter_drafts()) return "break";
        if (app->hardware_drag && app->hardware_drag->active()) {
            st->hardware_click = true;
            return app->hardware_drag->target_click(event).value_or("");
        }
        auto identity = pick(app, event.x, event.y);
        if (starts_with(identity, "scene:")) {
            app->start = app->last = {event.x, event.y};
            app->dragged = false;
            select(app, identity);
            if (app->edit_mode.get() == "layout") room_scene_drag::begin(app, event, *identity);
            return "break";
        }
        app->mouse_down(event);
        return "";
    });
    app->canvas->bind("<ButtonRelease-1>", [app, st, finish](tk::Event& event) -> std::string {
        if (st->hardware_click) {
            st->hardware_click = false;
            return "break";
        }
        if (app->scene_drag) {
            room_scene_drag::end(app, event);
            return finish();
        }
        auto identity = pick(app, event.x, event.y);
        if (!app->dragged && !app->layout_drag && !app->drag_edit) {
            app->gizmo_origin = std::make_pair<double, double>(event.x, event.y);
            if (starts_with(identity, "scene:")) {
                select(app, identity);
                finish();
                return "break";
            }
            app->selected_scene.reset();
        }
        app->mouse_up(event);
        finish();
        return "";
    });
    app->canvas->bind("<Motion>", [app](tk::Event& event) -> std::string {
        if (app->hardware_drag && app->hardware_drag->active()) return app->hardware_drag->move(event).value_or("");
        for (const auto& d : app->visible_drills) {
            if (std::hypot(event.x - d.x, event.y - d.y) < 8) {
                const json& op = d.operation;
                app->status.set(std::format("{} · Ø {:g} · głębokość {:g} mm · {}", py::get(op, "purpose", "Wiercenie").get<std::string>(),
                                            op.at("diameter").get<double>(), op.at("depth").get<double>(), op.at("face").get<std::string>()));
                break;
            }
        }
        return "";
    });
    app->canvas->bind("<ButtonPress-3>", [app, st](tk::Event& event) -> std::string {
        st->right_start = {event.x, event.y};
        app->pan_start(event);
        st->right_dragged = false;
        return "";
    });
    app->canvas->bind("<B3-Motion>", [app, st](tk::Event& event) -> std::string {
        if (std::hypot(event.x - st->right_start.first, event.y - st->right_start.second) > 4) st->right_dragged = true;
        if (st->right_dragged) {
            app->_view_interactive = true;
            app->pan(event);
        }
        return "";
    });
    app->canvas->bind("<ButtonRelease-3>", [app, st, finish](tk::Event& event) -> std::string {
        finish();
        if (!st->right_dragged) context_menu(app, event);
        return "";
    });
    app->canvas->bind("<MouseWheel>", [app, request_finish](tk::Event& event) -> std::string {
        app->_view_interactive = true;
        app->wheel(event);
        request_finish();
        return "break";
    });
    app->canvas->bind("<Escape>", [app, finish](tk::Event&) -> std::string {
        app->cancel_active_edits();
        finish();
        return "break";
    });
    app->finish_view_interaction = finish;
}

}  // namespace studio_interaction
