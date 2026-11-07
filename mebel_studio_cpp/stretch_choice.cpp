#include "stretch_choice.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <vector>

#include "app.h"
#include "corner_furniture.h"
#include "forma_theme.h"
#include "furniture_configurator.h"

namespace stretch_choice {

using core::json;

namespace {

constexpr double MIN_CHANGE = 150;  // mm; mniejsze rozciągnięcie to zwykła zmiana wymiaru, bez pytania
constexpr double TILE_W = 118, TILE_H = 104;

struct Option {
    std::string id, label, hint;
    bool enabled = true;
    std::function<core::ProjectPtr(const core::Project&)> build;  // nullptr = zostaw jak jest
};

double num(const json& v, double fallback = 0) { return v.is_number() ? v.get<double>() : fallback; }

json section_of(json& config, const std::string& key) {
    for (auto& s : config["sections"]) if (s.value("key", std::string()) == key) return s;
    return json::object();
}

void set_section(json& config, const std::string& key, const json& changes) {
    for (auto& s : config["sections"]) {
        if (s.value("key", std::string()) != key) continue;
        for (auto& [k, v] : changes.items()) s[k] = v;
    }
}

void draw_glyph(tk::Canvas* c, const std::string& id, double cx, bool enabled) {
    const char* wood = enabled ? "#e8d2ab" : "#e6e3dc";
    const char* edge = enabled ? "#7a6544" : "#a9a69d";
    const char* dark = enabled ? "#8c795a" : "#b7b4ab";
    auto rect = [&](double x1, double y1, double x2, double y2, const char* fill) { c->create_rectangle(x1, y1, x2, y2, {{"fill", fill}, {"outline", edge}}); };
    auto poly = [&](std::vector<double> p) { c->create_polygon(p, {{"fill", wood}, {"outline", edge}, {"width", 1}}); };
    auto line = [&](double x1, double y1, double x2, double y2) { c->create_line({x1, y1, x2, y2}, {{"fill", dark}, {"width", 2}}); };
    const double L = cx - 26, R = cx + 26, T = 10, B = 66;
    if (id == "keep_depth") {
        rect(cx - 22, 30, cx + 22, 56, wood);
        c->create_rectangle(cx - 22, 56, cx + 22, 66, {{"fill", "#d9b98a"}, {"outline", edge}});
    } else if (id == "corner_left") {
        poly({L, T + 14, R, T + 14, R, T + 56, L + 18, T + 56, L + 18, T + 34, L, T + 34});
    } else if (id == "corner_right") {
        poly({L, T + 14, R, T + 14, R, T + 34, R - 18, T + 34, R - 18, T + 56, L, T + 56});
    } else if (id == "corner_u") {
        poly({L, T + 14, L + 16, T + 14, L + 16, T + 40, R - 16, T + 40, R - 16, T + 14, R, T + 14, R, T + 56, L, T + 56});
    } else if (id == "keep_height") {
        rect(cx - 16, T, cx + 16, B, wood);
        rect(cx - 12, T + 4, cx + 12, T + 28, "#f4efe6");
    } else if (id == "with_upper") {
        rect(cx - 18, T, cx + 18, T + 18, wood);
        rect(cx - 18, T + 22, cx + 18, B, wood);
        line(cx - 18, T + 20, cx + 18, T + 20);
    } else if (id == "wardrobe") {
        rect(cx - 22, T, cx + 22, B, wood);
        line(cx, T + 2, cx, B - 2);
        c->create_oval(cx - 6, 36, cx - 3, 39, {{"fill", dark}, {"outline", ""}});
        c->create_oval(cx + 3, 36, cx + 6, 39, {{"fill", dark}, {"outline", ""}});
    } else if (id == "shelving") {
        rect(cx - 20, T, cx + 20, B, "#f4efe6");
        for (int i = 1; i < 5; ++i) line(cx - 20, T + i * 56.0 / 5, cx + 20, T + i * 56.0 / 5);
    }
}

void show_dialog(App* app, const std::string& title, const std::string& lead, std::vector<Option> options, const std::string& group) {
    auto* dialog = new tk::Toplevel(app);
    dialog->title(title);
    dialog->transient(app);
    auto* body = new ttk::Frame(dialog, {{"padding", 14}});
    body->pack({{"fill", "both"}, {"expand", true}});
    (new ttk::Label(body, {{"text", lead}, {"font", "{Segoe UI} 11 bold"}, {"wraplength", (int)(options.size() * (TILE_W + 10))}}))->pack({{"anchor", "w"}, {"pady", "0 8"}});
    auto* row = new ttk::Frame(body);
    row->pack({{"fill", "x"}});
    auto close = [dialog, app] { app->after(1, [dialog] { try { dialog->destroy(); } catch (const tk::TclError&) {} }); };
    int column = 0;
    for (const Option& option : options) {
        auto* cell = new tk::Frame(row, {{"bg", forma_theme::PANEL}});
        cell->grid({{"row", 0}, {"column", column++}, {"padx", 4}, {"pady", 3}});
        auto* canvas = new tk::Canvas(cell, {{"width", (int)TILE_W}, {"height", (int)TILE_H}, {"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER},
                                             {"cursor", option.enabled ? "hand2" : "arrow"}});
        canvas->pack();
        draw_glyph(canvas, option.id, TILE_W / 2, option.enabled);
        canvas->create_text(TILE_W / 2, 82, {{"text", option.label}, {"fill", option.enabled ? forma_theme::TEXT : forma_theme::MUTED}, {"font", "{Segoe UI} 9 bold"}});
        canvas->create_text(TILE_W / 2, 96, {{"text", option.hint}, {"fill", forma_theme::MUTED}, {"font", "{Segoe UI} 8"}});
        if (!option.enabled) continue;
        auto pick = [app, group, option, close](tk::Event&) {
            close();
            if (!option.build) { app->status.set("Zostawiono rozciągnięcie bez zmiany typu mebla"); return std::string(); }
            try {
                core::ProjectPtr result = option.build(*app->project);
                app->snapshot();
                app->project = result;
                for (auto& part : app->project->parts) if (part.group == group) { app->selected = part.id; break; }
                app->refresh();
                app->status.set(option.label + " · Ctrl+Z cofa zmianę");
            } catch (const std::exception& error) {
                app->status.set(error.what());
            }
            return std::string();
        };
        canvas->bind("<Button-1>", pick);
        canvas->bind("<Enter>", [canvas](tk::Event&) { canvas->configure({{"highlightbackground", forma_theme::ACCENT}}); return std::string(); });
        canvas->bind("<Leave>", [canvas](tk::Event&) { canvas->configure({{"highlightbackground", forma_theme::BORDER}}); return std::string(); });
    }
    (new ttk::Button(body, {{"text", "Zostaw jak jest"}, {"command", std::function<void()>(close)}}))->pack({{"anchor", "e"}, {"pady", "10 0"}});
}

}  // namespace

void offer(App* app, const core::Project& base, const std::string& group, const std::string& face) {
    if (face.size() < 2) return;
    json before = furniture_configurator::read_config(base, group);
    json after = furniture_configurator::read_config(*app->project, group);
    if (before.is_null() || after.is_null()) return;
    if (face[0] == 'y') {
        double old_depth = num(before["depth"]), new_depth = num(after["depth"]);
        if (new_depth - old_depth < MIN_CHANGE) return;
        double old_width = num(before["width"]);
        auto corner = [group, old_depth, old_width, new_depth](const std::string& side) {
            return [=](const core::Project& project) {
                return corner_furniture::convert_to_corner(project, group, "L", side, json(old_depth), json(old_width), json(new_depth)).first;
            };
        };
        show_dialog(app, "Rozciągnięto front do siebie", std::format("Głębokość {:g} → {:g} mm. Co ma z tego powstać?", old_depth, new_depth),
                    {{"keep_depth", "Pogłębienie", "tylko głębszy mebel", true, nullptr},
                     {"corner_left", "Narożnik L", "ramię w lewo", true, corner("left")},
                     {"corner_right", "Narożnik L", "ramię w prawo", true, corner("right")},
                     {"corner_u", "Układ U", "wkrótce", false, nullptr}},
                    group);
    } else if (face[0] == 'z') {
        double old_height = num(before["height"]), new_height = num(after["height"]);
        double delta = new_height - old_height;
        if (delta < MIN_CHANGE) return;
        auto reconfigure = [group](std::function<void(json&)> change) {
            return [=](const core::Project& project) {
                json config = furniture_configurator::read_config(project, group);
                if (config.is_null()) throw core::ValueError("Mebel nie pochodzi z kreatora.");
                change(config);
                return furniture_configurator::configure(project, config, group, config["origin"]).first;
            };
        };
        show_dialog(app, "Rozciągnięto mebel do góry", std::format("Wysokość {:g} → {:g} mm. Jakim meblem ma być?", old_height, new_height),
                    {{"keep_height", "Wyższa szafka", "bez zmian typu", true, nullptr},
                     {"with_upper", "Z pawlaczem", "strefa górna", true, reconfigure([delta](json& config) {
                          json main = section_of(config, "main");
                          json upper = {{"enabled", true}, {"height", std::max(200.0, std::round(delta))}, {"columns", main.value("columns", 1)}, {"front", "doors"}, {"shelves", 0}};
                          if (main.contains("column_widths")) upper["column_widths"] = main["column_widths"];
                          set_section(config, "upper", upper);
                      })},
                     {"wardrobe", "Szafa", "drzwi, półki", true, reconfigure([](json& config) {
                          double width = num(config["width"]);
                          set_section(config, "upper", {{"enabled", false}});
                          set_section(config, "main", {{"front", "doors"}, {"doors", width >= 800 ? 2 : 1}, {"shelves", 4}});
                          config["name"] = config.value("name", std::string("Szafa"));
                      })},
                     {"shelving", "Regał", "otwarte półki", true, reconfigure([new_height](json& config) {
                          set_section(config, "upper", {{"enabled", false}});
                          set_section(config, "main", {{"front", "open"}, {"shelves", (long long)std::clamp(std::lround(new_height / 350.0), 2L, 8L)}});
                      })}},
                    group);
    }
}

}  // namespace stretch_choice
