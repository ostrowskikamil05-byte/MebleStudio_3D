#include "side_panels.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "app.h"
#include "assembly.h"
#include "cad_geometry.h"
#include "forma_theme.h"
#include "forma_widgets.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "ui_preferences.h"
#include "ui_settings.h"
#include "view_style.h"

namespace side_panels {

namespace {

using json = core::json;

std::string g_key;
std::set<std::string> g_edges;
std::map<std::string, ttk::Button*> g_rows;
std::shared_ptr<tk::StringVar> g_edges_text;

void paint_rows() {
    for (auto& [key, row] : g_rows) {
        try {
            row->configure({{"style", key == g_key ? "RibbonOn.TButton" : "Ribbon.TButton"}});
        } catch (const tk::TclError&) {
        }
    }
}

std::vector<std::string> chain_of(const cad_geometry::EdgeSpec& spec) {
    if (spec.geometry.chain) return *spec.geometry.chain;
    return {spec.edge};
}

void update_edges_text() {
    if (g_edges_text) g_edges_text->set(std::format("Zaznaczone krawędzie ({})", g_edges.size()));
}

// Rysunek poglądowy: przekrój elementu z zaokrąglonym narożnikiem i opisem promienia.
void draw_fillet_preview(tk::Canvas* c, const std::string& radius_text) {
    try {
        c->delete_("all");
        double radius = 0;
        try {
            radius = std::max(0.0, std::stod(py::replace_all(radius_text, ",", ".")));
        } catch (const std::exception&) {
        }
        const double x0 = 30, y0 = 20, x1 = 190, y1 = 130;
        double r = std::min(70.0, radius * 2.4);
        std::vector<double> pts = {x0, y0, x1 - r, y0};
        for (int i = 0; i <= 8; ++i) {
            double a = (-90 + i * 11.25) * 3.14159265358979 / 180.0;
            pts.push_back(x1 - r + r * std::cos(a));
            pts.push_back(y0 + r + r * std::sin(a));
        }
        pts.insert(pts.end(), {x1, y1, x0, y1});
        c->create_polygon(pts, {{"fill", "#efe7d7"}, {"outline", "#4a4f48"}, {"width", 2}});
        for (int d = 14; d < 150; d += 14) {  // delikatne kreskowanie przekroju
            c->create_line({x0 + 2, y0 + d, x0 + d, y0 + 2}, {{"fill", "#d9cfba"}});
        }
        c->create_text(x1 + 12, y0 + 8, {{"text", "R" + std::format("{:g}", radius)}, {"fill", forma_theme::WARM}, {"font", "{Segoe UI} 11 bold"}, {"anchor", "w"}});
    } catch (const tk::TclError&) {
    }
}

void apply_fillet(App* app, const std::string& radius_text, const std::string& scope) {
    try {
        cad_geometry::kernel();
    } catch (const std::invalid_argument& e) {
        tk::messagebox::showinfo("Moduł CAD", e.what(), app);
        return;
    }
    try {
        core::Part* selected = app->selected_part();
        if (selected == nullptr) throw py::ValueError("Zaznacz element mebla w widoku 3D albo na liście.");
        double radius = core::finite(py::replace_all(radius_text, ",", "."), 0, 10000);
        if (scope == "edges" && g_edges.empty()) throw py::ValueError("Zaznacz krawędzie w widoku 3D (kliknij je w trybie Kanty).");
        std::vector<std::string> ids;
        if (scope == "furniture") {
            for (const auto& part : app->project->parts)
                if (part.group == selected->group) ids.push_back(part.id);
        } else {
            ids.push_back(selected->id);
        }
        core::Project working = *app->project;
        core::ProjectPtr result;
        for (const auto& id : ids) {
            auto found = std::find_if(working.parts.begin(), working.parts.end(), [&](const core::Part& p) { return p.id == id; });
            if (found == working.parts.end()) continue;
            std::vector<cad_geometry::EdgeSpec> specs = cad_geometry::edge_specs(found->points(), found->t, found->profile, {found->u, found->v});
            json values = json::object();
            if (scope == "edges") {
                for (auto& item : found->get_attr("fillets")) values[item["edge"].get<std::string>()] = item["radius"];
            }
            for (const auto& spec : specs) {
                if (scope == "edges" && !g_edges.contains(spec.edge)) continue;
                for (const auto& key : chain_of(spec)) {
                    if (radius > 0) values[key] = radius;
                    else values.erase(key);
                }
            }
            json items = json::array();
            for (auto& [key, value] : values.items()) items.push_back(json{{"edge", key}, {"radius", value}});
            core::Part candidate = *found;
            candidate.set_attr("fillets", items);
            candidate.validate();
            *found = candidate;
            result = assembly::rebuild_dependents(working, id);
            working = *result;
        }
        if (!result) throw py::ValueError("Nie znaleziono elementu do zaokrąglenia.");
        result->validate();
        app->snapshot();
        app->project = result;
        app->refresh();
        app->status.set("Zaokrąglenia: zapisano · Ctrl+Z cofa zmianę");
    } catch (const std::invalid_argument& e) {
        tk::messagebox::showerror("Zaokrąglenia", e.what(), app);
    }
}

}  // namespace

void register_row(const std::string& key, ttk::Button* row) {
    g_rows[key] = row;
    paint_rows();
}

bool is_open(const std::string& key) { return g_key == key; }

bool fillet_active() { return g_key == "fillet"; }

void show(App* app, const std::string& key, const std::string& title, std::function<void(tk::Widget* body)> builder) {
    if (app->context_page == nullptr || app->props_page == nullptr) return;
    app->props_page->pack_forget();
    for (tk::Widget* child : app->context_body->winfo_children()) child->destroy();
    g_key = key;
    app->context_title.set(title);
    if (app->context_page->winfo_manager().empty()) app->context_page->pack({{"fill", "both"}, {"expand", true}});
    if (app->ui_layout) app->ui_layout->show_inspector(true, false);
    builder(app->context_body);
    paint_rows();
}

void hide(App* app) {
    g_key.clear();
    if (app->context_page == nullptr || app->props_page == nullptr) return;
    app->context_page->pack_forget();
    for (tk::Widget* child : app->context_body->winfo_children()) child->destroy();
    if (app->props_page->winfo_manager().empty()) app->props_page->pack({{"fill", "both"}, {"expand", true}});
    paint_rows();
}

void toggle_edge(App* app, const std::string& edge) {
    if (g_edges.contains(edge)) g_edges.erase(edge);
    else g_edges.insert(edge);
    update_edges_text();
    app->status.set(std::format("Zaokrąglenia: zaznaczone krawędzie ({}) — kliknij kolejną albo naciśnij Zastosuj", g_edges.size()));
}

void show_fillet(App* app) {
    g_edges.clear();
    g_edges_text = std::make_shared<tk::StringVar>();
    update_edges_text();
    show(app, "fillet", "Zaokrąglenia", [app](tk::Widget* body) {
        tk::StringVar radius("20"), thickness, scope("part");
        core::Part* selected = app->selected_part();
        thickness.set(selected ? std::format("{:g}", selected->t) : std::string("—"));
        forma_widgets::field_row(body, "Promień", radius, "mm");
        forma_widgets::field_row(body, "Grubość", thickness, "mm", true);
        forma_widgets::caption(body, "Zastosowanie");
        auto choose = [app, scope] { app->edit_mode.set(scope.get() == "edges" ? "fillet" : app->edit_mode.get()); app->draw(); };
        std::function<void()> on_scope = choose;
        auto* a = new ttk::Radiobutton(body, {{"text", "Wybrany element"}, {"variable", scope}, {"value", "part"}, {"style", "Panel.TRadiobutton"}, {"command", on_scope}});
        a->pack({{"anchor", "w"}});
        auto* b = new ttk::Radiobutton(body, {{"text", "Wszystkie krawędzie mebla"}, {"variable", scope}, {"value", "furniture"}, {"style", "Panel.TRadiobutton"}, {"command", on_scope}});
        b->pack({{"anchor", "w"}});
        auto* c = new ttk::Radiobutton(body, {{"textvariable", *g_edges_text}, {"variable", scope}, {"value", "edges"}, {"style", "Panel.TRadiobutton"}, {"command", on_scope}});
        c->pack({{"anchor", "w"}});
        (new ttk::Label(body, {{"text", "Aby zaznaczyć krawędzie, wybierz trzecią opcję i klikaj kanty w widoku 3D."}, {"style", "Panel.TLabel"},
                               {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 8"}, {"wraplength", 280}}))->pack({{"anchor", "w"}, {"pady", "6 0"}});
        auto* preview = new tk::Canvas(body, {{"width", 270}, {"height", 150}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}});
        preview->pack({{"pady", 10}});
        draw_fillet_preview(preview, radius.get());
        radius.trace_add("write", [preview, radius] { draw_fillet_preview(preview, radius.get()); });
        auto* go = new ttk::Button(body, {{"text", "✓  Zastosuj"}, {"style", "Accent.TButton"}, {"padding", "10 10"},
                                          {"command", std::function<void()>([app, radius, scope] { apply_fillet(app, radius.get(), scope.get()); })}});
        go->pack({{"fill", "x"}, {"pady", "4 6"}});
        auto* advanced = new ttk::Button(body, {{"text", "Edytor każdej krawędzi osobno…"}, {"style", "Tool.TButton"},
                                                {"command", std::function<void()>([app] { app->round_edges(); })}});
        advanced->pack({{"fill", "x"}});
    });
}

void show_engine(App* app) {
    show(app, "engine", "Silnik i jakość 3D", [app](tk::Widget* body) {
        json prefs = app->ui_layout->preferences();
        auto set_pref = [app](const std::string& key, const std::string& value) {
            try {
                json result = app->ui_layout->preferences();
                result[key] = value;
                app->ui_layout->apply(ui_preferences::validate_preferences(result), false);
                app->ui_layout->persist();
                app->draw();
            } catch (const std::exception& error) {
                app->status.set(std::string("Ustawienia 3D: ") + error.what());
            }
        };
        // Silnik renderujący.
        (new ttk::Label(body, {{"text", "Silnik renderujący"}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 9"}}))->pack({{"anchor", "w"}, {"pady", "4 1"}});
        std::vector<std::string> names;
        std::string current_engine;
        for (const auto& [name, value] : ui_preferences::ENGINE_LABELS) {
            names.push_back(name);
            if (value == prefs.value("engine", "auto")) current_engine = name;
        }
        tk::StringVar engine(current_engine);
        auto* engine_box = new ttk::Combobox(body, {{"textvariable", engine}, {"values", names}, {"state", "readonly"}});
        engine_box->pack({{"fill", "x"}});
        engine_box->bind("<<ComboboxSelected>>", [engine, set_pref](tk::Event&) {
            for (const auto& [name, value] : ui_preferences::ENGINE_LABELS)
                if (name == engine.get()) set_pref("engine", value);
            return std::string();
        });
        // Jakość.
        (new ttk::Label(body, {{"text", "Jakość"}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 9"}}))->pack({{"anchor", "w"}, {"pady", "10 1"}});
        static const std::vector<std::pair<std::string, std::string>> qualities = {{"Niska", "draft"}, {"Średnia", "balanced"}, {"Wysoka", "high"}};
        std::string quality_label = "Średnia";
        for (const auto& [label, value] : qualities)
            if (value == prefs.value("quality", "balanced")) quality_label = label;
        tk::StringVar quality(quality_label);
        forma_widgets::segmented(body, {"Niska", "Średnia", "Wysoka"}, quality, [quality, set_pref] {
            for (const auto& [label, value] : qualities)
                if (label == quality.get()) set_pref("quality", value);
        });
        // Przełączniki.
        tk::BooleanVar lighting(view_style::state.lighting), shadows(view_style::state.shadows);
        forma_widgets::switch_row(body, "Oświetlenie", lighting, [app, lighting] { view_style::state.lighting = lighting.get(); view_style::save(); app->draw(); });
        forma_widgets::switch_row(body, "Cienie", shadows, [app, shadows] { view_style::state.shadows = shadows.get(); view_style::save(); app->draw(); });
        // Antyaliasing.
        (new ttk::Label(body, {{"text", "Antyaliasing"}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 10 bold"}}))->pack({{"anchor", "w"}, {"pady", "6 2"}});
        static const std::vector<std::pair<std::string, int>> aa_modes = {{"Wyłączony", 0}, {"2×", 2}, {"4×", 4}, {"8× (wysoka jakość)", 8}};
        std::vector<std::string> aa_names;
        std::string aa_current = "Wyłączony";
        for (const auto& [label, value] : aa_modes) {
            aa_names.push_back(label);
            if (value == view_style::state.aa) aa_current = label;
        }
        tk::StringVar aa(aa_current);
        auto* aa_box = new ttk::Combobox(body, {{"textvariable", aa}, {"values", aa_names}, {"state", "readonly"}});
        aa_box->pack({{"fill", "x"}});
        aa_box->bind("<<ComboboxSelected>>", [app, aa](tk::Event&) {
            for (const auto& [label, value] : aa_modes)
                if (label == aa.get()) view_style::state.aa = value;
            view_style::save();
            app->status.set("Antyaliasing zostanie zastosowany po ponownym uruchomieniu programu.");
            return std::string();
        });
        // Tło.
        forma_widgets::caption(body, "Tło");
        std::vector<std::string> colors;
        for (const auto& palette : view_style::palettes()) colors.push_back(palette.swatch);
        tk::StringVar background(std::to_string(view_style::state.background));
        forma_widgets::swatches(body, colors, background, [app, background] {
            view_style::state.background = std::stoi(background.get());
            view_style::save();
            app->draw();
        });
        auto* more = new ttk::Button(body, {{"text", "Okno, skala i panele…"}, {"style", "Tool.TButton"},
                                            {"command", std::function<void()>([app] { ui_settings::open_settings(app); })}});
        more->pack({{"fill", "x"}, {"pady", "10 0"}});
    });
}

}  // namespace side_panels
