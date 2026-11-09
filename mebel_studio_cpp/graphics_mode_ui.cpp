#include "graphics_mode_ui.h"

#include <memory>
#include <vector>

#include "app.h"
#include "forma_theme.h"
#include "forma_widgets.h"
#include "journal.h"
#include "view_style.h"

namespace graphics_mode_ui {

namespace {

constexpr int THUMB_W = 112, THUMB_H = 70;

struct Tile {
    tk::Canvas* canvas;
    render_style::Mode mode;
};

std::vector<Tile>& tiles() {
    static std::vector<Tile> list;
    return list;
}

std::vector<std::function<bool()>>& listeners() {
    static std::vector<std::function<bool()>> list;
    return list;
}

void highlight_all() {
    auto& list = tiles();
    for (size_t i = 0; i < list.size();) {
        try {
            bool selected = list[i].mode == view_style::state.mode;
            list[i].canvas->configure({{"highlightbackground", selected ? forma_theme::ACCENT : forma_theme::BORDER},
                                       {"highlightcolor", selected ? forma_theme::ACCENT : forma_theme::BORDER}});
            ++i;
        } catch (const std::exception&) {
            list.erase(list.begin() + (long)i);  // okno z tym kafelkiem zostało zamknięte
        }
    }
}

}  // namespace

void add_listener(std::function<bool()> listener) { listeners().push_back(std::move(listener)); }

bool set_mode(App* app, render_style::Mode mode, const std::string& source) {
    const auto& name = render_style::info(mode);
    if (view_style::state.mode == mode) {
        journal::event(app, "grafika", "tryb grafiki „" + name.name + "” jest już ustawiony (" + source + ") — bez zmian");
        return false;
    }
    const auto& before = render_style::info(view_style::state.mode);
    view_style::state.mode = mode;
    view_style::save();
    journal::event(app, "grafika", "tryb grafiki: " + before.name + " → " + name.name + " (" + source + ")");
    highlight_all();
    auto& list = listeners();
    for (size_t i = 0; i < list.size();) {
        bool keep = true;
        try {
            keep = list[i]();
        } catch (const std::exception&) {
            keep = false;
        }
        if (keep) ++i;
        else list.erase(list.begin() + (long)i);
    }
    app->status.set("Tryb grafiki: " + name.name + ". " + name.description);
    app->draw();
    return true;
}

void draw_thumbnail(tk::Canvas* canvas, render_style::Mode mode, int width, int height) {
    canvas->delete_("mode_thumb");
    for (const auto& poly : render_style::mode_thumbnail(mode, width, height)) {
        std::vector<double> coords;
        for (const auto& p : poly.points) { coords.push_back(p[0]); coords.push_back(p[1]); }
        if (poly.line) canvas->create_line(coords, {{"fill", poly.fill}, {"width", 1}, {"tags", "mode_thumb"}});
        else canvas->create_polygon(coords, {{"fill", poly.fill}, {"outline", poly.body ? (poly.outline.empty() ? std::string("#6b5a45") : poly.outline) : std::string()}, {"width", 1}, {"tags", "mode_thumb"}});
    }
}

ttk::Frame* add_switch(tk::Widget* parent, App* app, const std::string& source) {
    auto* holder = new ttk::Frame(parent);
    holder->pack({{"fill", "x"}, {"pady", 4}});
    // po dwa kafelki w rzędzie: cztery tryby z miniaturami nie mieszczą się obok siebie w wąskim panelu bocznym
    std::vector<ttk::Frame*> rows;
    size_t index = 0;
    for (const auto& info : render_style::modes()) {
        if (index % 2 == 0) {
            rows.push_back(new ttk::Frame(holder));
            rows.back()->pack({{"fill", "x"}, {"pady", 2}});
        }
        ++index;
        auto* cell = new ttk::Frame(rows.back());
        cell->pack({{"side", "left"}, {"padx", 3}});
        bool selected = info.mode == view_style::state.mode;
        auto* canvas = new tk::Canvas(cell, {{"width", THUMB_W}, {"height", THUMB_H}, {"bg", forma_theme::CARD}, {"highlightthickness", 2},
                                             {"highlightbackground", selected ? forma_theme::ACCENT : forma_theme::BORDER},
                                             {"highlightcolor", selected ? forma_theme::ACCENT : forma_theme::BORDER}, {"cursor", "hand2"}});
        canvas->pack();
        draw_thumbnail(canvas, info.mode, THUMB_W, THUMB_H);
        auto* label = new ttk::Label(cell, {{"text", info.name}});
        label->pack();
        render_style::Mode mode = info.mode;
        std::string where = source;
        canvas->bind("<Button-1>", [app, mode, where](tk::Event&) { set_mode(app, mode, where); return std::string(); });
        label->bind("<Button-1>", [app, mode, where](tk::Event&) { set_mode(app, mode, where); return std::string(); });
        tiles().push_back({canvas, mode});
    }
    return holder;
}

tk::Frame* add_compact(tk::Widget* parent, App* app, const std::string& source) {
    std::vector<std::string> labels;
    for (const auto& info : render_style::modes()) labels.push_back(info.name);
    auto var = std::make_shared<tk::StringVar>(render_style::info(view_style::state.mode).name);
    auto on_change = [app, var, source] {
        try {
            set_mode(app, render_style::mode_from(var->get()), source);
        } catch (const std::exception& error) {
            journal::event(app, "grafika", std::string("przełączenie trybu odrzucone: ") + error.what());
        }
    };
    const bool tab_bar = source.rfind("zakładka", 0) == 0;  // pasek zakładek układa elementy poziomo i ma miejsce na wszystkie tryby w jednym rzędzie
    tk::Frame* frame = nullptr;
    if (tab_bar || labels.size() <= 3) {
        frame = forma_widgets::segmented(parent, labels, *var, on_change);
    } else {
        // wąski panel boczny: tryby w dwóch rzędach, żeby żadna nazwa nie była ucięta; oba rzędy dzielą jedną zmienną
        frame = new tk::Frame(parent, {{"bg", forma_theme::PANEL}});
        frame->pack({{"fill", "x"}});
        const size_t half = (labels.size() + 1) / 2;
        forma_widgets::segmented(frame, std::vector<std::string>(labels.begin(), labels.begin() + (long)half), *var, on_change);
        forma_widgets::segmented(frame, std::vector<std::string>(labels.begin() + (long)half, labels.end()), *var, on_change);
    }
    if (tab_bar) frame->pack({{"side", "left"}, {"padx", 6}, {"pady", 2}});
    // zmiana trybu w innym miejscu przestawia też ten przełącznik
    add_listener([var, frame] {
        try {
            if (!frame->winfo_exists()) return false;
            var->set(render_style::info(view_style::state.mode).name);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
    return frame;
}

}  // namespace graphics_mode_ui
