// Zaokrąglone kontrolki (pola, listy rozwijane, przyciski, zakładki) jako elementy obrazkowe ttk.
// Obrazy RGBA są liczone w programie (bez plików); gdy Tk odrzuci któryś element, zostaje płaski wygląd z forma_theme.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "forma_theme.h"
#include "tkcore.h"

namespace forma_theme {

namespace {

struct Color { double r = 0, g = 0, b = 0; };

Color parse(const char* hex) {
    auto pair = [&](int i) { return std::stoi(std::string(hex + 1 + 2 * i, 2), nullptr, 16) / 255.0; };
    return {pair(0), pair(1), pair(2)};
}

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

// Zaokrąglony prostokąt wypełniający cały obraz size × size; fill == nullptr → bez wypełnienia, border == nullptr → bez ramki.
std::vector<std::uint8_t> rounded(int size, double radius, const char* fill, const char* border, double border_width = 1.0) {
    std::vector<std::uint8_t> out((size_t)size * size * 4, 0);
    Color f = fill ? parse(fill) : Color{}, b = border ? parse(border) : Color{};
    double half = size / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double px = std::fabs(x + 0.5 - half) - (half - radius), py = std::fabs(y + 0.5 - half) - (half - radius);
            double d = std::hypot(std::max(px, 0.0), std::max(py, 0.0)) + std::min(std::max(px, py), 0.0) - radius;
            double outer = clamp01(0.5 - d);
            double inner = border ? clamp01(0.5 - (d + border_width)) : outer;
            double stroke = border ? std::max(0.0, outer - inner) : 0.0;
            double body = fill ? inner : 0.0;
            double alpha = stroke + body;
            size_t i = ((size_t)y * size + x) * 4;
            if (alpha > 0.0) {
                out[i] = (std::uint8_t)std::lround(255 * clamp01((b.r * stroke + f.r * body) / alpha));
                out[i + 1] = (std::uint8_t)std::lround(255 * clamp01((b.g * stroke + f.g * body) / alpha));
                out[i + 2] = (std::uint8_t)std::lround(255 * clamp01((b.b * stroke + f.b * body) / alpha));
                out[i + 3] = (std::uint8_t)std::lround(255 * clamp01(alpha));
            }
        }
    return out;
}

// Strzałka "v" listy rozwijanej.
std::vector<std::uint8_t> chevron(int size, const char* color) {
    std::vector<std::uint8_t> out((size_t)size * size * 4, 0);
    Color c = parse(color);
    double cx = size / 2.0, cy = size / 2.0 + 0.5, arm = size * 0.2;
    auto distance = [](double px, double py, double ax, double ay, double bx, double by) {
        double dx = bx - ax, dy = by - ay, t = clamp01(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy));
        return std::hypot(px - ax - t * dx, py - ay - t * dy);
    };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double d = std::min(distance(x + 0.5, y + 0.5, cx - arm, cy - arm / 2, cx, cy + arm / 2), distance(x + 0.5, y + 0.5, cx, cy + arm / 2, cx + arm, cy - arm / 2));
            double alpha = clamp01(1.3 - d);
            size_t i = ((size_t)y * size + x) * 4;
            out[i] = (std::uint8_t)std::lround(255 * c.r); out[i + 1] = (std::uint8_t)std::lround(255 * c.g); out[i + 2] = (std::uint8_t)std::lround(255 * c.b);
            out[i + 3] = (std::uint8_t)std::lround(255 * alpha);
        }
    return out;
}

std::vector<std::unique_ptr<tk::PhotoImage>>& keep() {
    static std::vector<std::unique_ptr<tk::PhotoImage>> images;
    return images;
}

std::string make_image(tk::Widget* root, int size, std::vector<std::uint8_t> pixels) {
    keep().push_back(std::make_unique<tk::PhotoImage>(root, size, size, pixels));
    return keep().back()->name();
}

struct Look {
    const char* fill;
    const char* border;
};

// Obraz 9-częściowy o promieniu `radius`; zwraca nazwę obrazu.
std::string box(tk::Widget* root, int radius, const Look& look) {
    int size = 2 * radius + 3;
    return make_image(root, size, rounded(size, radius, look.fill, look.border));
}

void element(const std::string& name, const std::string& normal, const std::vector<std::pair<std::string, std::string>>& states, int radius) {
    tkcore::Args spec{normal};
    for (const auto& [state, image] : states) {
        spec.push_back(state);
        spec.push_back(image);
    }
    std::string border = std::to_string(radius + 1);
    tkcore::eval({"ttk::style", "element", "create", name, "image", tkcore::merge(spec), "-border", tkcore::merge({border, border, border, border}), "-sticky", "nswe"});
}

void button_layout(const std::string& style, const std::string& element_name) {
    tkcore::eval({"ttk::style", "layout", style, element_name + " -sticky nswe -children {Button.padding -sticky nswe -children {Button.label -sticky nswe}}"});
}

}  // namespace

void install_round(tk::Widget* root) {
    try {
        constexpr int R = 8;
        // Pola tekstowe, listy rozwijane, pola liczbowe.
        std::string field = box(root, R, {CARD, BORDER}), field_focus = box(root, R, {CARD, ACCENT}), field_off = box(root, R, {BG, BORDER});
        for (const char* name : {"Entry.field", "Combobox.field", "Spinbox.field"})
            element(name, field, {{"disabled", field_off}, {"focus", field_focus}}, R);
        int arrow_size = 22;
        std::string arrow = make_image(root, arrow_size, chevron(arrow_size, MUTED));
        tkcore::eval({"ttk::style", "element", "create", "Combobox.downarrow", "image", arrow, "-sticky", "e"});

        // Przyciski.
        std::string plain = box(root, R, {CARD, BORDER}), plain_hover = box(root, R, {HOVER, BORDER}), plain_press = box(root, R, {ACCENT_SOFT, ACCENT}), plain_off = box(root, R, {BG, BORDER});
        element("Forma.button", plain, {{"disabled", plain_off}, {"pressed", plain_press}, {"active", plain_hover}}, R);
        button_layout("TButton", "Forma.button");
        std::string acc = box(root, R, {ACCENT, ACCENT}), acc_hover = box(root, R, {ACCENT_HOVER, ACCENT_HOVER}), acc_press = box(root, R, {"#3f5a33", "#3f5a33"}), acc_off = box(root, R, {"#a8b79c", "#a8b79c"});
        element("Forma.accent", acc, {{"disabled", acc_off}, {"pressed", acc_press}, {"active", acc_hover}}, R);
        button_layout("Accent.TButton", "Forma.accent");
        constexpr int CR = 12;
        std::string chip = box(root, CR, {CARD, BORDER}), chip_hover = box(root, CR, {HOVER, BORDER}), chip_press = box(root, CR, {ACCENT_SOFT, ACCENT});
        element("Forma.chip", chip, {{"pressed", chip_press}, {"active", chip_hover}}, CR);
        button_layout("Chip.TButton", "Forma.chip");
        std::string chip_on = box(root, CR, {ACCENT, ACCENT}), chip_on_hover = box(root, CR, {ACCENT_HOVER, ACCENT_HOVER});
        element("Forma.chipon", chip_on, {{"active", chip_on_hover}}, CR);
        button_layout("ChipOn.TButton", "Forma.chipon");
        // Wiersze list i przyciski paska: bez ramki, zaokrąglone podświetlenie.
        std::string flat = box(root, R, {nullptr, nullptr}), flat_hover = box(root, R, {HOVER, nullptr}), flat_press = box(root, R, {ACCENT_SOFT, nullptr});
        element("Forma.flat", flat, {{"pressed", flat_press}, {"active", flat_hover}}, R);
        for (const char* style : {"Ribbon.TButton", "Tool.TButton", "Section.TButton"}) button_layout(style, "Forma.flat");
        std::string row_on = box(root, R, {ACCENT_SOFT, nullptr});
        element("Forma.flaton", row_on, {{"pressed", row_on}, {"active", row_on}}, R);
        button_layout("RibbonOn.TButton", "Forma.flaton");

        // Zakładki dokumentów w dialogach: pastylki.
        constexpr int TR = 12;
        std::string tab_none = box(root, TR, {nullptr, nullptr}), tab_on = box(root, TR, {ACCENT, ACCENT}), tab_hover = box(root, TR, {HOVER, nullptr});
        element("Notebook.tab", tab_none, {{"selected", tab_on}, {"active", tab_hover}}, TR);
    } catch (const std::exception&) {
        // Płaski wygląd z forma_theme::apply pozostaje.
    }
}

}  // namespace forma_theme
