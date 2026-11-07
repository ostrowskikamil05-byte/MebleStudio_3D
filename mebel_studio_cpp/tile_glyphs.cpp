#include "tile_glyphs.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "forma_theme.h"
#include "tk.h"

namespace tile_glyphs {

void draw(tk::Canvas* c, const std::string& key, const std::string& choice, const std::string& label, bool on) {
    if (c == nullptr) return;
    struct Tile { const std::string& key; const std::string& choice; const std::string& label; } tile{key, choice, label};
    double w = std::max(40, c->winfo_width());
    c->delete_("all");
    c->create_rectangle(1, 1, w - 2, 61, {{"fill", on ? forma_theme::ACCENT_SOFT : forma_theme::CARD}, {"outline", on ? forma_theme::ACCENT : forma_theme::BORDER}, {"width", on ? 2 : 1}});
    const double cx = w / 2, L = cx - 18, R = cx + 18, T = 8, B = 38, M = (T + B) / 2;
    const char* wood = "#e8d2ab";
    const char* edge = "#7a6544";
    const char* dark = "#8c795a";
    auto rect = [&](double x1, double y1, double x2, double y2, const char* fill) { c->create_rectangle(x1, y1, x2, y2, {{"fill", fill}, {"outline", edge}}); };
    auto dot = [&](double x, double y, double r, const std::string& fill) { c->create_oval(x - r, y - r, x + r, y + r, {{"fill", fill}, {"outline", edge}}); };
    auto line = [&](double x1, double y1, double x2, double y2, const char* color, int width) { c->create_line({x1, y1, x2, y2}, {{"fill", color}, {"width", width}}); };
    static const std::map<std::string, std::string> COLORS = {{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Biały", "#efefea"}, {"Złoty", "#bb9656"}};
    const std::string& k = tile.key;
    const std::string& ch = tile.choice;
    if (k == "front_style") {
        rect(cx - 17, T, cx + 17, B, wood);
        if (ch == "Ramka frezowana") rect(cx - 11, T + 6, cx + 11, B - 6, "#d9b98a");
        else if (ch == "Ryflowany") for (double x = cx - 12; x < cx + 15; x += 5) line(x, T + 2, x, B - 2, dark, 2);
        else if (ch == "Chwyt frezowany") c->create_rectangle(cx - 12, T + 2, cx + 12, T + 6, {{"fill", dark}, {"outline", ""}});
    } else if (k.ends_with(".front")) {
        rect(L, T, R, B, "#f4efe6");
        if (ch == "Otwarte") {
            line(L + 3, M, R - 3, M, edge, 2);
        } else if (ch == "Drzwi") {
            rect(L + 2, T + 2, cx - 1, B - 2, wood);
            rect(cx + 1, T + 2, R - 2, B - 2, wood);
            dot(cx - 4, M, 1.5, dark);
            dot(cx + 4, M, 1.5, dark);
        } else {
            for (int i = 0; i < 3; ++i) {
                double y = T + 2 + i * 9.3;
                rect(L + 2, y, R - 2, y + 8, wood);
                line(cx - 5, y + 4, cx + 5, y + 4, dark, 2);
            }
        }
    } else if (k.ends_with(".doors")) {
        rect(L, T, R, B, "#f4efe6");
        if (ch == "1 skrzydło") {
            rect(L + 2, T + 2, R - 2, B - 2, wood);
            dot(R - 6, M, 1.5, dark);
        } else {
            rect(L + 2, T + 2, cx - 1, B - 2, wood);
            rect(cx + 1, T + 2, R - 2, B - 2, wood);
            dot(cx - 4, M, 1.5, dark);
            dot(cx + 4, M, 1.5, dark);
        }
    } else if (k == "hinge_pattern") {
        double s = ch.find("45") != std::string::npos ? 9 : 11;
        rect(L + 2, T, L + 10, B, wood);
        dot(L + 20, M, 8, "#cfd3d6");
        dot(L + 20, M, 3, "#9aa0a6");
        dot(R - 3, M - s, 2, dark);
        dot(R - 3, M + s, 2, dark);
        if (ch.find("Sensys") != std::string::npos) line(L + 10, M - 5, R - 3, M - s, dark, 1);
    } else if (k == "hinge_spring") {
        rect(L + 2, T + 2, L + 7, B - 2, wood);
        if (ch == "Ze sprężyną") {
            c->create_line({L + 8, M, L + 14, M - 7, L + 20, M + 7, L + 26, M - 7, L + 32, M + 7, R - 2, M}, {{"fill", edge}, {"width", 2}});
        } else if (ch == "Bez sprężyny") {
            line(L + 8, M, R - 2, M, edge, 2);
        } else {
            c->create_text(cx + 5, M, {{"text", "?"}, {"fill", dark}, {"font", "{Segoe UI} 16 bold"}});
        }
    } else if (k == "leg_count") {
        rect(L, T + 4, R, B - 8, wood);
        std::vector<double> xs = {L + 3, R - 3};
        if (ch == "6 sztuk") xs.insert(xs.begin() + 1, cx);
        for (double x : xs) dot(x, B - 2, 2.5, "#242628");
    } else if (k == "leg_color" || k == "handle_color") {
        auto it = COLORS.find(ch);
        dot(cx, M, 12, it != COLORS.end() ? it->second : "#cccccc");
    } else if (k == "top_mode") {
        if (ch == "Pełna płyta") {
            rect(L, M - 5, R, M + 5, wood);
        } else {
            rect(L, M - 8, R, M - 3, wood);
            rect(L, M + 3, R, M + 8, wood);
        }
    } else if (k == "led_mount") {
        if (ch == "Pod półkami") {
            rect(L, T, R, T + 3, wood);
            c->create_rectangle(L + 4, T + 4, R - 4, T + 7, {{"fill", "#f0c040"}, {"outline", ""}});
            for (double x = L + 6; x < R - 4; x += 8) line(x, T + 9, x - 2, T + 18, "#f6e3a0", 1);
        } else {
            rect(L, T, L + 5, B, wood);
            c->create_rectangle(L + 6, T + 3, L + 9, B - 3, {{"fill", "#f0c040"}, {"outline", ""}});
            for (double y = T + 6; y < B - 4; y += 8) line(L + 11, y, L + 20, y + 2, "#f6e3a0", 1);
        }
    } else if (k == "door_hinge") {
        rect(L, T, R, B, "#f4efe6");
        rect(L + 2, T + 2, R - 2, B - 2, wood);
        if (ch == "Po prawej") {
            dot(R - 4, T + 8, 2, dark);
            dot(R - 4, B - 8, 2, dark);
            dot(L + 6, M, 1.5, dark);
        } else if (ch == "Po lewej") {
            dot(L + 4, T + 8, 2, dark);
            dot(L + 4, B - 8, 2, dark);
            dot(R - 6, M, 1.5, dark);
        } else {
            c->create_text(cx, M, {{"text", "A"}, {"fill", dark}, {"font", "{Segoe UI} 14 bold"}});
        }
    } else if (k == "leg_height") {
        double h = ch.starts_with("Bez") ? 0 : ch.starts_with("60") ? 4 : ch.starts_with("100") ? 8 : 12;
        rect(L, T + 2, R, B - 14, wood);
        if (h > 0) {
            for (double x : {L + 3, R - 3}) c->create_rectangle(x - 1.5, B - 14, x + 1.5, B - 14 + h, {{"fill", "#242628"}, {"outline", ""}});
        }
        line(L - 4, B - 14 + h + 1, R + 4, B - 14 + h + 1, edge, 1);
    } else if (k == "handle_mode") {
        rect(L, T, R, B, wood);
        if (ch.starts_with("Uchwyt")) line(R - 7, T + 6, R - 7, B - 6, "#242628", 3);
        else if (ch.starts_with("Bez")) line(L + 4, M, R - 4, M, edge, 1);
        else dot(R - 7, M, 3, "#242628");
    }
    c->create_text(w / 2, 50, {{"text", tile.label}, {"fill", on ? forma_theme::ACCENT : forma_theme::TEXT}, {"font", on ? "{Segoe UI} 9 bold" : "{Segoe UI} 9"}});
}

}  // namespace tile_glyphs
