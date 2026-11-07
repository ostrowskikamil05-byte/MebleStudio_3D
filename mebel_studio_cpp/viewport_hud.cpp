#include "viewport_hud.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <format>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include "app.h"
#include "journal.h"
#include "forma_theme.h"

namespace viewport_hud {

namespace {

struct Area {
    double x0, y0, x1, y1;
    std::function<void()> action;
};

std::vector<Area> g_areas;
bool g_captured = false;

std::vector<double> rounded(double x0, double y0, double x1, double y1, double r) {
    std::vector<double> points;
    auto arc = [&](double cx, double cy, double start) {
        for (int i = 0; i <= 5; ++i) {
            double angle = (start + i * 18.0) * std::numbers::pi / 180.0;
            points.push_back(cx + r * std::cos(angle));
            points.push_back(cy + r * std::sin(angle));
        }
    };
    arc(x1 - r, y0 + r, -90);
    arc(x1 - r, y1 - r, 0);
    arc(x0 + r, y1 - r, 90);
    arc(x0 + r, y0 + r, 180);
    return points;
}

// Biała „tabletka" z delikatnym cieniem.
void card(tk::Canvas* c, double x0, double y0, double x1, double y1, double r) {
    c->create_polygon(rounded(x0, y0 + 2, x1, y1 + 3, r), {{"fill", "#e7e3da"}, {"outline", ""}, {"tags", "hud"}});
    c->create_polygon(rounded(x0, y0, x1, y1, r), {{"fill", forma_theme::CARD}, {"outline", forma_theme::BORDER}, {"width", 1}, {"tags", "hud"}});
}

}  // namespace

void activate_tool(App* app, const std::string& target, const std::string& tool_label) {
    journal::event(app, "narzędzie", "kliknięto „" + tool_label + "” (tryb " + target + ", poprzedni " + app->edit_mode.get() + ")");
    // Wybór narzędzia kończy wstawianie okucia (zawias / prowadnica / LED / uchwyt) — inaczej każde kliknięcie w widoku trafiałoby do tego trybu i nic nie dałoby się zaznaczyć.
    if (app->hardware_drag != nullptr && app->hardware_drag->active()) {
        journal::event(app, "okucia", "anulowano wstawianie okucia (wybrano narzędzie)");
        app->hardware_drag->cancel();
        app->draw();
    }
    if (target == "tray") { if (app->toggle_hardware_tray) app->toggle_hardware_tray(); }
    else if (target == "rotate") { if (app->rotate_furniture) app->rotate_furniture(90); }
    else if (target == "dimensions") { app->dimensions.set(!app->dimensions.get()); app->draw(); }
    else {
        app->edit_mode.set(target);
        app->draw();
        if (target == "furniture") app->status.set(app->selected ? "Rozciąganie mebla: przeciągnij zielone uchwyty na jego ściankach (Krok 32 mm w CAD)" : "Rozciąganie mebla: zaznacz mebel, potem przeciągnij uchwyty na jego ściankach");
        else if (target == "configure") app->status.set("Konfiguracja: kliknij mebel, aby otworzyć okno z uchwytami, nóżkami, zawiasami, materiałami i dodatkami");
        else if (target == "resize") app->status.set(app->selected ? "Rozciąganie części: przeciągnij kolorowe uchwyty (U / V / T) zaznaczonego elementu" : "Rozciąganie części: zaznacz element, potem przeciągnij uchwyty");
    }
    journal::event(app, "narzędzie", "stan po kliknięciu „" + tool_label + "”: " + journal::interaction_state(app));
}

void paint(App* app, double width, double height, const core::json& grid_spacing) {
    g_areas.clear();
    tk::Canvas* c = app->canvas;
    double k = 1.0;
    try {
        if (app->ui_layout) k = std::clamp(app->ui_layout->preferences().at("ui_scale").get<double>(), 0.75, 2.0);
    } catch (...) {
    }
    auto size = [&](double v) { return std::round(v * k); };
    const std::string label_font = std::format("{{Segoe UI}} {}", (int)std::round(10 * k));
    const std::string small_font = std::format("{{Segoe UI}} {}", std::max(7, (int)std::round(8 * k)));
    const std::string icon_font = std::format("{{Segoe UI Symbol}} {}", (int)std::round(17 * k));
    // Czcionka do pomiaru jest tworzona raz na rozmiar — odtwarzanie jej przy każdym rysowaniu spowalniało widok.
    static std::map<std::string, std::unique_ptr<tk::font::Font>> fonts;
    auto& cached = fonts[label_font];
    if (!cached) cached = std::make_unique<tk::font::Font>(app, label_font);
    tk::font::Font& measure = *cached;

    // Prawy dolny róg paska stanu.
    std::string spacing_text;
    if (grid_spacing.is_number()) spacing_text = std::format("Siatka {:g} mm   ·   ", grid_spacing.get<double>());
    app->status_right.set(spacing_text + std::format("Zoom {:.0f}%", std::fabs(app->camera.scale) * 100));

    // Podpowiedź, gdy włączony jest tryb wstawiania okucia: klik w widoku nie zaznacza wtedy mebli, tylko wskazuje miejsce okucia.
    if (app->hardware_drag != nullptr && app->hardware_drag->active() && width >= 520) {
        std::string text = "Wstawianie okucia — kliknij element mebla  ·  Esc albo narzędzie „Zaznacz” anuluje";
        double pill_w = measure.measure(text) + size(32), x0 = (width - pill_w) / 2, y0 = size(14), h0 = size(34);
        card(c, x0, y0, x0 + pill_w, y0 + h0, size(17));
        c->create_text(width / 2, y0 + h0 / 2, {{"text", text}, {"fill", forma_theme::ACCENT}, {"font", label_font}, {"tags", "hud"}});
    }

    // Paleta narzędzi po lewej: Zaznacz · Przesuń · Obróć · Wymiary · Okucia (jak na makiecie).
    if (height >= 380) {
        struct Tool { std::string label, glyph, mode; };
        std::vector<Tool> tools = {{"Zaznacz", "↖", "camera"}, {"Przesuń", "✥", "layout"}, {"Obróć", "↻", "rotate"}, {"Wymiary", "↔", "dimensions"}, {"Okucia", "⚙", "tray"},
                                   {"Konfiguruj", "✎", "configure"}};
        // Rozciąganie pod Konfiguruj; im niższe okno, tym mniej narzędzi (najmniej ważne znikają), żeby paleta nie zachodziła na przełącznik widoków.
        tools.push_back({"Rozciągnij", "⤢", "furniture"});
        tools.push_back({"Część", "▭", "resize"});
        double x = size(14), y = size(18), cell_w = size(60);
        double limit = height - size(14 + 34 + 8 + 8);  // górna krawędź przełącznika widoków
        while (tools.size() > 5 && (limit - y - size(10)) / tools.size() < size(44)) tools.pop_back();
        double cell_h = std::min(size(58), (limit - y - size(10)) / tools.size());
        double total = cell_h * tools.size() + size(10);
        card(c, x, y, x + cell_w + size(8), y + total, size(14));
        std::string mode = app->edit_mode.get();
        for (size_t i = 0; i < tools.size(); ++i) {
            double cy = y + size(5) + cell_h * i, cx = x + size(4);
            bool active = (tools[i].mode == mode && tools[i].mode != "rotate" && tools[i].mode != "tray") || (tools[i].mode == "dimensions" && app->dimensions.get());
            if (active) c->create_polygon(rounded(cx, cy + 1, cx + cell_w, cy + cell_h - 1, size(10)), {{"fill", forma_theme::ACCENT_SOFT}, {"outline", forma_theme::ACCENT}, {"width", 1}, {"tags", "hud"}});
            c->create_text(cx + cell_w / 2, cy + cell_h * 0.38, {{"text", tools[i].glyph}, {"fill", active ? forma_theme::ACCENT : forma_theme::TEXT}, {"font", icon_font}, {"tags", "hud"}});
            c->create_text(cx + cell_w / 2, cy + cell_h * 0.80, {{"text", tools[i].label}, {"fill", active ? forma_theme::ACCENT : forma_theme::MUTED}, {"font", small_font}, {"tags", "hud"}});
            std::string target = tools[i].mode;
            std::string tool_label = tools[i].label;
            g_areas.push_back({cx, cy, cx + cell_w, cy + cell_h, [app, target, tool_label] { activate_tool(app, target, tool_label); }});
        }
    }

    // Przełącznik widoków u dołu po lewej: Perspektywa · Przód · Góra (Bok, Dół i Dopasuj są w menu Widok).
    if (width >= 420 && height >= 300) {
        struct View { std::string label; double yaw, pitch; };
        std::vector<View> views = {{"◈  Perspektywa", -38, 24}, {"▭  Przód", 0, 0}, {"▭  Góra", 0, 90}};
        double pad = size(14), gap = size(4), h = size(34), total = size(8);
        std::vector<double> widths;
        for (const auto& v : views) { widths.push_back(measure.measure(v.label) + 2 * pad); total += widths.back() + gap; }
        double x = size(14), y = height - size(14) - h - size(8);
        card(c, x, y, x + total, y + h + size(8), size(18));
        x += size(8);
        for (const auto& v : views) {
            double x1 = x + widths[&v - &views[0]];
            bool active = std::fabs(app->camera.yaw - v.yaw) < 0.5 && std::fabs(app->camera.pitch - v.pitch) < 0.5;
            if (active) c->create_polygon(rounded(x, y + size(4), x1, y + size(4) + h, size(15)), {{"fill", forma_theme::ACCENT}, {"outline", ""}, {"tags", "hud"}});
            c->create_text((x + x1) / 2, y + size(4) + h / 2, {{"text", v.label}, {"fill", active ? "white" : forma_theme::TEXT}, {"font", label_font}, {"tags", "hud"}});
            View copy = v;
            g_areas.push_back({x, y, x1, y + h + size(8), [app, copy] { app->view(copy.yaw, copy.pitch); }});
            x = x1 + gap;
        }
    }

    // Wskaźnik osi X / Y / Z w prawym dolnym rogu (zgodny z bieżącą kamerą).
    if (width >= 420 && height >= 260) {
        try {
            core::Point3 origin = app->camera.project({0, 0, 0}, width, height);
            struct Axis { const char* label; core::Point3 vector; const char* color; double dx = 0, dy = 0, length = 0; };
            std::vector<Axis> axes = {{"X", {1, 0, 0}, "#d9453d"}, {"Y", {0, 1, 0}, "#3f7fd0"}, {"Z", {0, 0, 1}, "#4f9a3f"}};
            double longest = 1e-6;
            for (auto& axis : axes) {
                core::Point3 tip = app->camera.project(axis.vector, width, height);
                axis.dx = tip[0] - origin[0]; axis.dy = tip[1] - origin[1];
                axis.length = std::hypot(axis.dx, axis.dy);
                longest = std::max(longest, axis.length);
            }
            double cx = width - size(64), cy = height - size(52), reach = size(34);
            for (const auto& axis : axes) {
                if (axis.length < 1e-6) continue;
                double scale = reach * std::max(0.35, axis.length / longest) / axis.length;
                double ex = cx + axis.dx * scale, ey = cy + axis.dy * scale;
                double ux = (ex - cx) / std::hypot(ex - cx, ey - cy), uy = (ey - cy) / std::hypot(ex - cx, ey - cy);
                c->create_line({cx, cy, ex, ey}, {{"fill", axis.color}, {"width", 2}, {"tags", "hud"}});
                c->create_polygon({ex, ey, ex - ux * 8 - uy * 4, ey - uy * 8 + ux * 4, ex - ux * 8 + uy * 4, ey - uy * 8 - ux * 4},
                                  {{"fill", axis.color}, {"outline", ""}, {"tags", "hud"}});
                c->create_text(ex + ux * 11, ey + uy * 11, {{"text", axis.label}, {"fill", axis.color}, {"font", label_font + " bold"}, {"tags", "hud"}});
            }
        } catch (const std::exception&) {
        }
    }
}

bool press(App* app, const tk::Event& event) {
    (void)app;
    for (const auto& area : g_areas) {
        if (event.x >= area.x0 && event.x <= area.x1 && event.y >= area.y0 && event.y <= area.y1) {
            g_captured = true;
            std::function<void()> action = area.action;  // draw() czyści listę obszarów
            if (action) action();
            return true;
        }
    }
    g_captured = false;
    return false;
}

bool captured() { return g_captured; }

bool release() {
    bool was = g_captured;
    g_captured = false;
    return was;
}

}  // namespace viewport_hud
