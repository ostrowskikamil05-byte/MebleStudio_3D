#include "workspace_controls.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app.h"
#include "corner_furniture.h"
#include "corner_quick_ui.h"
#include "form_dialog.h"
#include "furniture_layout.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "room_scene_ui.h"
#include "tk.h"
#include "ttk.h"
#include "workspace_worktops.h"

namespace workspace_controls {

using core::json;
using Cmd = std::function<void()>;

namespace {

// _guard(app, title, fn): (ValueError, OSError, KeyError) trafiają do logu i paska stanu.
Cmd guard(App* app, const std::string& title, std::function<void()> fn) {
    auto report = [app, title](const char* what) {
        app->log->error(title + ": " + what);
        app->status.set(what);
    };
    return [report, fn = std::move(fn)] {
        try {
            fn();
        } catch (const std::invalid_argument& error) {  // ValueError
            report(error.what());
        } catch (const std::system_error& error) {      // OSError
            report(error.what());
        } catch (const std::out_of_range& error) {      // KeyError
            report(error.what());
        }
    };
}

struct Choices {
    std::vector<std::pair<std::string, json>> items;  // etykieta -> wartość (słownik uporządkowany)
};

}  // namespace

form_dialog::Form* corner_dialog(App* app) {
    core::Part* current = app->selected_part();
    std::optional<std::string> group;
    if (current) group = current->group;
    json cfg = (group && !group->empty()) ? corner_furniture::read_corner_config(*app->project, group) : json(nullptr);
    bool editing = py::truthy(cfg);
    if (!editing) cfg = corner_furniture::default_corner_config(app->project.get());
    if (!editing && !app->project->parts.empty()) {
        double right = 0;
        bool first = true;
        for (const auto& g : furniture_layout::groups(*app->project)) {
            double x = furniture_layout::group_bounds(*app->project, g).second[0];
            if (first || x > right) right = x;
            first = false;
        }
        cfg["origin"] = json::array({right + 100, 0, 0});
    }
    auto* doc = app->dialog("Narożnik — konstrukcja L / ścięta / zaokrąglona", "560x750");
    (new ttk::Label(doc, {{"text", "Korpus narożny · rzeczywisty obrys dna i półek"}, {"padding", 12}, {"font", "{Segoe UI} 11 bold"}}))->pack({{"fill", "x"}});
    struct FieldSpec { const char* key; const char* label; json value; };
    const std::vector<FieldSpec> specs = {{"name", "Nazwa", "Narożnik"}, {"width", "Szerokość X [mm]", 900}, {"depth", "Głębokość Y [mm]", 900},
                                          {"height", "Wysokość [mm]", 720}, {"arm_depth", "Głębokość ramion [mm]", 560},
                                          {"thickness", "Grubość płyty [mm]", 18}, {"radius", "Promień naroża [mm]", 100}, {"shelves", "Liczba półek", 1}};
    auto fields = std::make_shared<std::vector<std::pair<std::string, tk::StringVar>>>();
    for (const auto& spec : specs) fields->push_back({spec.key, app->field(doc, spec.label, py::get(cfg, spec.key, spec.value))});
    struct ComboSpec { const char* key; const char* label; Choices choices; };
    const std::vector<ComboSpec> combo_specs = {
        {"shape", "Kształt", {{{"L", "L"}, {"Ścięty", "chamfer"}, {"Zaokrąglony", "rounded"}}}},
        {"side", "Wariant", {{{"Lewy", "left"}, {"Prawy", "right"}}}},
        {"top_mode", "Góra", {{{"Pełna płyta", "panel"}, {"Listwy pod wspólny blat", "rails"}}}},
    };
    auto combos = std::make_shared<std::vector<std::tuple<std::string, tk::StringVar, Choices>>>();
    for (const auto& spec : combo_specs) {
        auto* row = new ttk::Frame(doc, {{"padding", "15 3"}});
        row->pack({{"fill", "x"}});
        (new ttk::Label(row, {{"text", spec.label}}))->pack({{"side", "left"}});
        // next((k for k, v in choices.items() if v == cfg.get(key)), next(iter(choices)))
        std::string label = spec.choices.items.front().first;
        json wanted = py::get(cfg, spec.key);
        for (const auto& [k, v] : spec.choices.items)
            if (v == wanted) { label = k; break; }
        tk::StringVar var(label);
        std::vector<std::string> names;
        for (const auto& item : spec.choices.items) names.push_back(item.first);
        (new ttk::Combobox(row, {{"textvariable", var}, {"values", names}, {"state", "readonly"}, {"width", 28}}))->pack({{"side", "right"}});
        combos->push_back({spec.key, var, spec.choices});
    }
    (new ttk::Label(doc, {{"text", "Otwarty korpus. Fronty i okucia narożne dobierz do wybranego mechanizmu; ten generator nie przypisuje zawiasów skośnych."},
                         {"wraplength", 480}, {"padding", 15}}))->pack({{"fill", "x"}});
    auto apply = [app, doc, cfg, group, editing, fields, combos] {
        doc->ensure_current_project(true);
        json draft = cfg;
        for (auto& [key, var] : *fields) {
            if (key == "name") draft[key] = var.get();
            else draft[key] = app->number(var);
        }
        for (auto& [key, var, choices] : *combos) {
            std::string label = var.get();
            auto found = std::find_if(choices.items.begin(), choices.items.end(), [&](const auto& item) { return item.first == label; });
            if (found == choices.items.end()) throw std::out_of_range("'" + label + "'");
            draft[key] = found->second;
        }
        auto [model, actual] = corner_furniture::create_corner(*app->project, draft, editing ? group : std::nullopt);
        app->snapshot();
        app->project = model;
        auto chosen = std::find_if(model->parts.begin(), model->parts.end(), [&](const core::Part& p) { return p.group == actual; });
        if (chosen == model->parts.end()) throw py::StopIteration();
        app->selected = chosen->id;
        app->selected_scene.reset();
        app->refresh();
        app->fit();
        doc->destroy();
        app->workspace->select(app->design_page);
    };
    (new ttk::Button(doc, {{"text", editing ? "Zapisz narożnik" : "Dodaj narożnik"}, {"command", guard(app, "Narożnik", apply)}, {"style", "Accent.TButton"}}))
        ->pack({{"fill", "x"}, {"padx", 15}, {"pady", 8}});
    return doc;
}

form_dialog::Form* shared_worktop_dialog(App* app) {
    std::vector<std::string> options;
    for (const auto& g : furniture_layout::groups(*app->project))
        if (!py::truthy(workspace_worktops::read_shared_worktop(*app->project, g))) options.push_back(g);
    if (options.empty()) throw core::ValueError("Najpierw dodaj szafki pod blat.");
    auto* doc = app->dialog("Jeden wspólny blat dla szafek", "580x710");
    (new ttk::Label(doc, {{"text", "Wybierz szafki, które tworzą połączony ciąg lub układ L / U."}, {"padding", 15}, {"wraplength", 500}}))->pack({{"fill", "x"}});
    auto choices = std::make_shared<std::vector<std::pair<std::string, tk::BooleanVar>>>();
    for (const auto& name : options) {
        tk::BooleanVar var(options.size() <= 6);
        choices->push_back({name, var});
        (new ttk::Checkbutton(doc, {{"text", name}, {"variable", var}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 2}});
    }
    tk::StringVar thickness = app->field(doc, "Grubość [mm]", 38), overhang = app->field(doc, "Nawis [mm]", 20);
    tk::StringVar name = app->field(doc, "Nazwa blatu", "Wspólny blat");
    (new ttk::Label(doc, {{"text", "Usuwa indywidualne blaty kreatora i tworzy listwy wzmacniające. Niepołączone szafki lub różne wysokości wymagają najpierw wyrównania."},
                         {"wraplength", 500}, {"padding", 15}}))->pack({{"fill", "x"}});
    auto apply = [app, doc, choices, thickness, overhang, name]() mutable {
        doc->ensure_current_project(true);
        std::vector<std::string> selected;
        for (auto& [g, v] : *choices)
            if (v.get()) selected.push_back(g);
        auto [result, group] = workspace_worktops::install_shared_worktop(*app->project, selected, app->number(thickness), app->number(overhang),
                                                                          std::nullopt, name.get());
        app->snapshot();
        app->project = result;
        auto chosen = std::find_if(result->parts.begin(), result->parts.end(), [&](const core::Part& p) { return p.group == group; });
        if (chosen == result->parts.end()) throw py::StopIteration();
        app->selected = chosen->id;
        app->refresh();
        app->fit();
        doc->destroy();
        app->workspace->select(app->design_page);
    };
    (new ttk::Button(doc, {{"text", "Utwórz wspólny blat"}, {"command", guard(app, "Wspólny blat", apply)}, {"style", "Accent.TButton"}}))
        ->pack({{"fill", "x"}, {"padx", 15}, {"pady", 8}});
    return doc;
}

void install(App* app) {
    auto* panel = new room_scene_ui::RoomScenePanel(app->inspector_tabs, app);
    app->room_scene_panel = panel;
    app->inspector_tabs->add(panel, {{"text", "Pomieszczenie"}});
    app->corner_dialog = guard(app, "Narożnik", [app] { corner_dialog(app); });
    app->shared_worktop_dialog = guard(app, "Wspólny blat", [app] { shared_worktop_dialog(app); });
    app->corner_panel = new corner_quick_ui::CornerPanel(app->inspector_tabs, app);
    app->inspector_tabs->add(app->corner_panel, {{"text", "Narożnik"}});
    app->inspector_tabs->tab("1", {{"text", "CAD"}});
    app->inspector_tabs->tab(panel, {{"text", "Scena"}});
    auto show_corner = [app] {
        if (app->ui_layout) app->ui_layout->show_inspector(true);
        app->workspace->select(app->design_page);
        app->corner_panel->refresh();
        app->inspector_tabs->select(app->corner_panel);
    };
    app->corner_dialog = show_corner;
    quick_studio_ui::_button(app->ribbon_pages.at("Układ"), "Narożnik…", app->corner_dialog);
    quick_studio_ui::_button(app->ribbon_pages.at("Układ"), "Wspólny blat…", app->shared_worktop_dialog);
    auto* tolerance_row = new ttk::Frame(app->ribbon_pages.at("Układ"), {{"style", "Panel.TFrame"}, {"padding", "10 4"}});
    tolerance_row->pack({{"side", "left"}});
    (new ttk::Label(tolerance_row, {{"text", "Tolerancja łączenia"}, {"style", "Panel.TLabel"}}))->pack({{"side", "left"}});
    (new ttk::Label(tolerance_row, {{"text", "mm"}, {"style", "Panel.TLabel"}}))->pack({{"side", "right"}, {"padx", "4 0"}});
    auto* tolerance = new ttk::Spinbox(tolerance_row, {{"from", 5}, {"to", 200}, {"increment", 5}, {"width", 5}, {"textvariable", app->layout_tolerance}});
    tolerance->pack({{"side", "right"}, {"padx", 3}});
    Cmd show_scene = [app, panel] {
        if (app->ui_layout) app->ui_layout->show_inspector(true);
        app->workspace->select(app->design_page);
        app->inspector_tabs->select(panel);
    };
    quick_studio_ui::_group(app->ribbon_pages.at("Scena"), "POMIESZCZENIE");
    quick_studio_ui::_button(app->ribbon_pages.at("Scena"), "Wyposażenie i oświetlenie", show_scene);
    auto set_brightness = [app, panel](double value) {
        if (py::get(app->project->scene, "brightness", 1) == json(value)) return;
        app->snapshot();
        app->project->scene["brightness"] = value;
        panel->refresh();
        app->draw();
    };
    quick_studio_ui::_group(app->ribbon_pages.at("Scena"), "ŚWIATŁO");
    for (auto [label, value] : std::vector<std::pair<std::string, double>>{{"Jasne pomieszczenie", 1.25}, {"Dzienne", 1.0}, {"Ciemne", .35}})
        quick_studio_ui::_button(app->ribbon_pages.at("Scena"), label, [set_brightness, v = value] { set_brightness(v); });
    auto toggle = [app, panel](const std::string& key) {
        app->snapshot();
        app->project->scene[key] = !py::truthy(py::get(app->project->scene, key, true));
        panel->refresh();
        app->draw();
    };
    quick_studio_ui::_group(app->ribbon_pages.at("Scena"), "LED I LAMPY");
    quick_studio_ui::_button(app->ribbon_pages.at("Scena"), "LED wł. / wył.", [toggle] { toggle("led_enabled"); });
    quick_studio_ui::_button(app->ribbon_pages.at("Scena"), "Lampy wł. / wył.", [toggle] { toggle("lights_enabled"); });
    Cmd logs = [app] { app->open_developer_logs(); };
    if (dev_tools::enabled()) quick_studio_ui::_button(app->ribbon_pages.at("Produkcja"), "Logi", logs);
    auto* menu = dynamic_cast<tk::Menu*>(app->nametowidget(app->cget("menu")));
    if (menu == nullptr) throw tk::TclError("bad menu entry: " + app->cget("menu"));
    auto* tools = new tk::Menu(menu, {{"tearoff", false}});
    menu->add_cascade({{"label", "Scena i diagnostyka"}, {"menu", tools}});
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"Wyposażenie pomieszczenia", show_scene}, {"Narożnik…", app->corner_dialog},
                                                                         {"Wspólny blat…", app->shared_worktop_dialog}, {"Otwórz / pokaż logi", logs}}) {
        if (label == "Otwórz / pokaż logi" && !dev_tools::enabled()) continue;
        tools->add_command({{"label", label}, {"command", fn}});
    }
    app->status.set("ŚPM / Alt+LPM: obrót · Shift+ŚPM: przesuń widok · PPM: szybkie menu · rolka: przybliżenie");
}

}  // namespace workspace_controls
