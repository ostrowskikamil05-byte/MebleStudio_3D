#include "forma_tabs.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "app.h"
#include "forma_theme.h"

namespace forma_tabs {

namespace {

struct Entry {
    std::string id, text;
    bool closable = false;
    double x0 = 0, x1 = 0;
};

struct State {
    std::vector<Entry> visible;
    std::vector<Entry> overflow;
    double more0 = 0, more1 = 0;  // przycisk "⋯"
    std::string signature;
};

std::vector<double> rounded(double x0, double y0, double x1, double y1, double r) {
    std::vector<double> points;
    auto arc = [&](double cx, double cy, double start) {
        for (int i = 0; i <= 6; ++i) {
            double angle = (start + i * 15.0) * std::numbers::pi / 180.0;
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

// Skraca tytuł do `limit` znaków (UTF-8) i dopisuje wielokropek.
std::string shorten(const std::string& text, size_t limit) {
    size_t count = 0, i = 0;
    while (i < text.size()) {
        size_t step = 1;
        unsigned char c = (unsigned char)text[i];
        if (c >= 0xF0) step = 4; else if (c >= 0xE0) step = 3; else if (c >= 0xC0) step = 2;
        if (count == limit) return text.substr(0, i) + "…";
        i += step;
        ++count;
    }
    return text;
}

}  // namespace

void install(App* app) {
    if (app->tabs == nullptr || app->workspace_pane == nullptr) return;
    ttk::Style style(app);
    style.layout("Doc.TNotebook.Tab", {});
    style.configure("Doc.TNotebook", {{"tabmargins", "0 0 0 0"}, {"borderwidth", 0}, {"background", forma_theme::BG}});
    app->tabs->configure({{"style", "Doc.TNotebook"}});
    // Nagłówek "⠿ Projekt 3D / Odłącz" na stronie głównej jest zbędny (okna dodatkowe zachowują swój nagłówek z przyciskami).
    if (app->workspace != nullptr && app->design_page != nullptr) {
        auto found = app->workspace->entries.find(app->design_page->path());
        if (found != app->workspace->entries.end() && found->second.header != nullptr) found->second.header->pack_forget();
    }

    auto* bar = new tk::Canvas(app, {{"height", 54}, {"bg", forma_theme::BG}, {"highlightthickness", 0}});
    bar->pack({{"fill", "x"}, {"before", app->workspace_pane->path()}});
    auto state = std::make_shared<State>();
    auto font = std::make_shared<tk::font::Font>(app, "{Segoe UI} 10");

    auto paint = [app, bar, state, font](bool force) {
        try {
            double width = bar->winfo_width();
            if (width < 60) return;
            std::vector<Entry> all;
            std::string current = app->tabs->select();
            for (const auto& id : app->tabs->tabs()) {
                Entry entry;
                entry.id = id;
                entry.text = shorten(app->tabs->tab(id, "text"), 24);
                auto found = app->workspace ? app->workspace->entries.find(id) : decltype(app->workspace->entries)::iterator();
                entry.closable = app->workspace && found != app->workspace->entries.end() && found->second.allow_close;
                all.push_back(entry);
            }
            std::string signature = current + "|" + std::to_string((int)width);
            for (const auto& e : all) signature += "|" + e.id + "=" + e.text;
            if (!force && signature == state->signature) return;
            state->signature = signature;
            const double pad = 18, close_w = 20, gap = 2, inner = 5;
            for (auto& e : all) e.x1 = font->measure(e.text) + 2 * pad + (e.closable ? close_w : 0);  // szerokość chwilowo w x1
            double room = width - 90, used = 0;
            std::vector<Entry> visible, overflow;
            for (const auto& e : all) {
                if (used + e.x1 + gap <= room) { visible.push_back(e); used += e.x1 + gap; }
                else overflow.push_back(e);
            }
            // Aktywna zakładka zawsze widoczna: zastępuje ostatnią mieszczącą się.
            auto in_overflow = std::find_if(overflow.begin(), overflow.end(), [&](const Entry& e) { return e.id == current; });
            if (in_overflow != overflow.end()) {
                Entry chosen = *in_overflow;
                overflow.erase(in_overflow);
                while (!visible.empty() && used + chosen.x1 + gap > room) {
                    used -= visible.back().x1 + gap;
                    overflow.insert(overflow.begin(), visible.back());
                    visible.pop_back();
                }
                visible.push_back(chosen);
                used += chosen.x1 + gap;
            }
            double more = overflow.empty() ? 0 : 44;
            double total = used + more + 2 * inner;
            double x = std::max(8.0, (width - total) / 2), y = 9, h = 36;
            bar->delete_("all");
            bar->create_polygon(rounded(x, y, x + total, y + h, h / 2), {{"fill", forma_theme::CARD}, {"outline", forma_theme::BORDER}, {"width", 1}});
            x += inner;
            for (auto& e : visible) {
                double w = e.x1;
                bool on = e.id == current;
                e.x0 = x;
                e.x1 = x + w;
                if (on) bar->create_polygon(rounded(x, y + 3, x + w, y + h - 3, (h - 6) / 2), {{"fill", forma_theme::ACCENT}, {"outline", ""}});
                bar->create_text(x + pad, y + h / 2, {{"text", e.text}, {"anchor", "w"}, {"fill", on ? "white" : forma_theme::TEXT}, {"font", on ? "{Segoe UI} 10 bold" : "{Segoe UI} 10"}});
                if (e.closable) bar->create_text(x + w - pad + 2, y + h / 2, {{"text", "✕"}, {"fill", on ? "white" : forma_theme::MUTED}, {"font", "{Segoe UI} 9"}});
                x += w + gap;
            }
            state->more0 = state->more1 = 0;
            if (!overflow.empty()) {
                state->more0 = x;
                state->more1 = x + more - 4;
                bar->create_text((state->more0 + state->more1) / 2, y + h / 2, {{"text", "⋯"}, {"fill", forma_theme::TEXT}, {"font", "{Segoe UI} 12 bold"}});
            }
            state->visible = visible;
            state->overflow = overflow;
        } catch (const tk::TclError&) {
        }
    };

    bar->bind("<Button-1>", [app, bar, state, paint](tk::Event& e) {
        try {
            if (state->more1 > state->more0 && e.x >= state->more0 && e.x <= state->more1) {
                auto* menu = new tk::Menu(app, {{"tearoff", false}});
                for (const auto& entry : state->overflow) {
                    std::string id = entry.id;
                    menu->add_command({{"label", entry.text}, {"command", std::function<void()>([app, id] { app->tabs->select(id); })}});
                }
                menu->tk_popup(e.x_root, e.y_root);
                return std::string();
            }
            for (const auto& entry : state->visible) {
                if (e.x < entry.x0 || e.x > entry.x1) continue;
                if (entry.closable && e.x > entry.x1 - 26 && app->workspace != nullptr) {
                    auto found = app->workspace->entries.find(entry.id);
                    if (found != app->workspace->entries.end()) app->workspace->close(found->second.page);
                } else {
                    app->tabs->select(entry.id);
                }
                break;
            }
            paint(true);
        } catch (const tk::TclError&) {
        }
        return std::string();
    });
    bar->bind("<Configure>", [paint](tk::Event&) { paint(true); return std::string(); });
    app->tabs->bind("<<NotebookTabChanged>>", [paint](tk::Event&) { paint(false); return std::string(); }, true);

    auto tick = std::make_shared<std::function<void()>>();
    *tick = [app, paint, tick] {
        try {
            paint(false);
            app->after(600, *tick);
        } catch (const tk::TclError&) {
        }
    };
    (*tick)();
}

}  // namespace forma_tabs
