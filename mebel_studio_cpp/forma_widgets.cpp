#include "forma_widgets.h"

#include <memory>

#include "forma_theme.h"

namespace forma_widgets {

namespace {

std::string no_event() { return std::string(); }

}  // namespace

tk::Frame* switch_row(tk::Widget* parent, const std::string& text, tk::BooleanVar var, Command on_change) {
    auto* row = new tk::Frame(parent, {{"bg", forma_theme::PANEL}});
    row->pack({{"fill", "x"}, {"pady", 5}});
    (new tk::Label(row, {{"text", text}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::TEXT}, {"font", "{Segoe UI} 10 bold"}}))->pack({{"side", "left"}});
    auto* sw = new tk::Canvas(row, {{"width", 42}, {"height", 24}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
    sw->pack({{"side", "right"}});
    auto draw = [sw, var]() {
        try {
            sw->delete_("all");
            bool on = var.get();
            const char* fill = on ? forma_theme::ACCENT : "#c9c5bb";
            sw->create_oval(1, 2, 21, 22, {{"fill", fill}, {"outline", fill}});
            sw->create_oval(21, 2, 41, 22, {{"fill", fill}, {"outline", fill}});
            sw->create_rectangle(11, 2, 31, 23, {{"fill", fill}, {"outline", fill}});
            double cx = on ? 31 : 11;
            sw->create_oval(cx - 8, 4, cx + 8, 20, {{"fill", "white"}, {"outline", "white"}});
        } catch (const tk::TclError&) {
        }
    };
    draw();
    tk::BooleanVar watched = var;
    watched.trace_add("write", draw);
    sw->bind("<Button-1>", [var, on_change](tk::Event&) mutable {
        var.set(!var.get());
        if (on_change) on_change();
        return no_event();
    });
    return row;
}

tk::Frame* segmented(tk::Widget* parent, const std::vector<std::string>& labels, tk::StringVar var, Command on_change) {
    auto* box = new tk::Frame(parent, {{"bg", forma_theme::BORDER}, {"padx", 1}, {"pady", 1}});
    box->pack({{"fill", "x"}, {"pady", "2 8"}});
    auto cells = std::make_shared<std::vector<tk::Label*>>();
    auto paint = [cells, labels, var]() {
        try {
            for (size_t i = 0; i < cells->size(); ++i) {
                bool on = labels[i] == var.get();
                (*cells)[i]->configure({{"bg", on ? forma_theme::ACCENT : forma_theme::CARD}, {"fg", on ? "white" : forma_theme::TEXT}});
            }
        } catch (const tk::TclError&) {
        }
    };
    for (const auto& label : labels) {
        auto* cell = new tk::Label(box, {{"text", label}, {"bg", forma_theme::CARD}, {"fg", forma_theme::TEXT}, {"font", "{Segoe UI} 10"}, {"pady", 6}, {"cursor", "hand2"}});
        cell->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", "0 1"}});
        cells->push_back(cell);
        cell->bind("<Button-1>", [var, label, on_change, paint](tk::Event&) mutable {
            var.set(label);
            paint();
            if (on_change) on_change();
            return no_event();
        });
    }
    paint();
    return box;
}

tk::Frame* swatches(tk::Widget* parent, const std::vector<std::string>& colors, tk::StringVar var, Command on_change) {
    auto* row = new tk::Frame(parent, {{"bg", forma_theme::PANEL}});
    row->pack({{"fill", "x"}, {"pady", "4 8"}});
    auto cells = std::make_shared<std::vector<tk::Canvas*>>();
    auto paint = [cells, colors, var]() {
        try {
            std::string selected = var.get();
            for (size_t i = 0; i < cells->size(); ++i) {
                tk::Canvas* c = (*cells)[i];
                c->delete_("all");
                bool on = selected == std::to_string(i);
                c->create_rectangle(4, 4, 50, 46, {{"fill", colors[i]}, {"outline", on ? forma_theme::ACCENT : forma_theme::BORDER}, {"width", on ? 3 : 1}});
                if (on) {
                    c->create_oval(4, 4, 22, 22, {{"fill", forma_theme::ACCENT}, {"outline", "white"}, {"width", 1}});
                    c->create_text(13, 13, {{"text", "✓"}, {"fill", "white"}, {"font", "{Segoe UI} 9 bold"}});
                }
            }
        } catch (const tk::TclError&) {
        }
    };
    for (size_t i = 0; i < colors.size(); ++i) {
        auto* cell = new tk::Canvas(row, {{"width", 56}, {"height", 52}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
        cell->pack({{"side", "left"}, {"padx", "0 4"}});
        cells->push_back(cell);
        cell->bind("<Button-1>", [var, i, on_change, paint](tk::Event&) mutable {
            var.set(std::to_string(i));
            paint();
            if (on_change) on_change();
            return no_event();
        });
    }
    paint();
    return row;
}

ttk::Label* caption(tk::Widget* parent, const std::string& text) {
    auto* label = new ttk::Label(parent, {{"text", text}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 10 bold"}});
    label->pack({{"anchor", "w"}, {"pady", "10 2"}});
    return label;
}

ttk::Entry* field_row(tk::Widget* parent, const std::string& label, tk::StringVar var, const std::string& unit, bool readonly) {
    auto* row = new ttk::Frame(parent, {{"style", "Panel.TFrame"}});
    row->pack({{"fill", "x"}, {"pady", 3}});
    (new ttk::Label(row, {{"text", label}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "left"}});
    if (!unit.empty()) (new ttk::Label(row, {{"text", unit}, {"style", "Panel.TLabel"}, {"foreground", forma_theme::MUTED}, {"width", 3}}))->pack({{"side", "right"}, {"padx", "4 0"}});
    auto* entry = new ttk::Entry(row, {{"textvariable", var}, {"width", 9}, {"justify", "right"}});
    entry->pack({{"side", "right"}});
    if (readonly) entry->configure({{"state", "readonly"}});
    return entry;
}

}  // namespace forma_widgets
