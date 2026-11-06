#include "viewport_hud.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include "app.h"
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
    tk::font::Font measure(app, label_font);

    // Prawy dolny róg paska stanu.
    std::string spacing_text;
    if (grid_spacing.is_number()) spacing_text = std::format("Siatka {:g} mm   ·   ", grid_spacing.get<double>());
    app->status_right.set(spacing_text + std::format("Zoom {:.0f}%", std::fabs(app->camera.scale) * 100));

    // Przełączniki u góry (X-ray / rozstrzelony / wymiary).
    if (width >= 520) {
        struct Toggle { std::string label; tk::BooleanVar* var; };
        std::vector<Toggle> toggles = {{"X-ray", &app->xray}, {"Rozstrzelony", &app->explode}, {"Wymiary", &app->dimensions}};
        double pad = size(16), gap = size(4), h = size(34), total = size(8);
        std::vector<double> widths;
        for (const auto& t : toggles) { widths.push_back(measure.measure(t.label) + 2 * pad); total += widths.back() + gap; }
        double x = (width - total) / 2, y = size(14);
        card(c, x, y, x + total, y + h + size(8), size(18));
        x += size(8);
        for (size_t i = 0; i < toggles.size(); ++i) {
            bool on = toggles[i].var->get();
            double x1 = x + widths[i];
            if (on) c->create_polygon(rounded(x, y + size(4), x1, y + size(4) + h, size(15)), {{"fill", forma_theme::ACCENT}, {"outline", ""}, {"tags", "hud"}});
            c->create_text((x + x1) / 2, y + size(4) + h / 2, {{"text", toggles[i].label}, {"fill", on ? "white" : forma_theme::TEXT}, {"font", label_font}, {"tags", "hud"}});
            tk::BooleanVar* var = toggles[i].var;
            g_areas.push_back({x, y, x1, y + h + size(8), [app, var] { var->set(!var->get()); app->draw(); }});
            x = x1 + gap;
        }
    }

    // Paleta narzędzi po lewej.
    if (height >= 380) {
        struct Tool { std::string label, glyph, mode; };
        std::vector<Tool> tools = {{"Zaznacz", "↖", "camera"}, {"Przesuń", "✥", "layout"}, {"Mebel", "⇔", "furniture"},
                                   {"Część", "⇱", "resize"}, {"Kanty", "◠", "fillet"}, {"Okucia", "⚙", "tray"}};
        double cell_w = size(60), cell_h = size(58), x = size(14), y = size(64);
        double total = cell_h * tools.size() + size(10);
        card(c, x, y, x + cell_w + size(8), y + total, size(14));
        std::string mode = app->edit_mode.get();
        for (size_t i = 0; i < tools.size(); ++i) {
            double cy = y + size(5) + cell_h * i, cx = x + size(4);
            bool active = tools[i].mode == mode;
            if (active) c->create_polygon(rounded(cx, cy + 1, cx + cell_w, cy + cell_h - 1, size(10)), {{"fill", forma_theme::ACCENT_SOFT}, {"outline", forma_theme::ACCENT}, {"width", 1}, {"tags", "hud"}});
            c->create_text(cx + cell_w / 2, cy + cell_h * 0.38, {{"text", tools[i].glyph}, {"fill", active ? forma_theme::ACCENT : forma_theme::TEXT}, {"font", icon_font}, {"tags", "hud"}});
            c->create_text(cx + cell_w / 2, cy + cell_h * 0.80, {{"text", tools[i].label}, {"fill", active ? forma_theme::ACCENT : forma_theme::MUTED}, {"font", small_font}, {"tags", "hud"}});
            std::string target = tools[i].mode;
            g_areas.push_back({cx, cy, cx + cell_w, cy + cell_h, [app, target] {
                if (target == "tray") { if (app->toggle_hardware_tray) app->toggle_hardware_tray(); }
                else { app->edit_mode.set(target); app->draw(); }
            }});
        }
    }

    // Przełącznik widoków na dole.
    if (width >= 560 && height >= 300) {
        struct View { std::string label; double yaw, pitch; bool fit; };
        std::vector<View> views = {{"Perspektywa", -38, 24, false}, {"Przód", 0, 0, false}, {"Bok", -90, 0, false}, {"Góra", 0, 90, false}, {"Dół", 0, -90, false}, {"Dopasuj", 0, 0, true}};
        double pad = size(14), gap = size(4), h = size(34), total = size(8);
        std::vector<double> widths;
        for (const auto& v : views) { widths.push_back(measure.measure(v.label) + 2 * pad); total += widths.back() + gap; }
        double x = (width - total) / 2, y = height - size(14) - h - size(8);
        card(c, x, y, x + total, y + h + size(8), size(18));
        x += size(8);
        for (const auto& v : views) {
            double x1 = x + widths[&v - &views[0]];
            bool active = !v.fit && std::fabs(app->camera.yaw - v.yaw) < 0.5 && std::fabs(app->camera.pitch - v.pitch) < 0.5;
            if (active) c->create_polygon(rounded(x, y + size(4), x1, y + size(4) + h, size(15)), {{"fill", forma_theme::ACCENT}, {"outline", ""}, {"tags", "hud"}});
            c->create_text((x + x1) / 2, y + size(4) + h / 2, {{"text", v.label}, {"fill", active ? "white" : forma_theme::TEXT}, {"font", label_font}, {"tags", "hud"}});
            View copy = v;
            g_areas.push_back({x, y, x1, y + h + size(8), [app, copy] { if (copy.fit) app->fit(); else app->view(copy.yaw, copy.pitch); }});
            x = x1 + gap;
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
