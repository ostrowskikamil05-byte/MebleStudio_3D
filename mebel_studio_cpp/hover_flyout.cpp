#include "hover_flyout.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <memory>

#include "app.h"
#include "forma_theme.h"
#include "journal.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "visual_catalogue.h"

namespace hover_flyout {
namespace {

constexpr int WIDTH = 330;
constexpr int SHOW_DELAY_MS = 150;
constexpr int POLL_MS = 100;
constexpr int MISSES_TO_HIDE = 3;

tk::Frame* g_box = nullptr;            // nakładka (dziecko widoku 3D)
tk::Frame* g_content = nullptr;        // wnętrze z ramką
tk::Widget* g_trigger = nullptr;       // pozycja menu, która otworzyła okienko
bool g_visible = false;
unsigned g_enter_token = 0;            // unieważnia oczekujące otwarcie, gdy mysz zjechała
unsigned g_poll_token = 0;             // jedna pętla sprawdzania wskaźnika naraz
int g_misses = 0;
std::vector<std::function<void()>> g_runs;  // polecenia kart ostatniego okienka

bool pointer(App* app, int& x, int& y) {
    try {
        std::string text = app->view_frame->call({"winfo", "pointerxy", "."});
        return std::sscanf(text.c_str(), "%d %d", &x, &y) == 2;
    } catch (const tk::TclError&) {
        return false;
    }
}

bool inside(tk::Widget* widget, int x, int y, int margin) {
    if (widget == nullptr || !widget->winfo_exists()) return false;
    try {
        int left = widget->winfo_rootx(), top = widget->winfo_rooty();
        return x >= left - margin && x <= left + widget->winfo_width() + margin && y >= top - margin && y <= top + widget->winfo_height() + margin;
    } catch (const tk::TclError&) {
        return false;
    }
}

// Pas między pozycją menu a okienkiem: pozwala dojechać do okienka bez jego zamknięcia.
bool in_corridor(tk::Widget* trigger, tk::Widget* box, int x, int y) {
    if (trigger == nullptr || box == nullptr || !trigger->winfo_exists() || !box->winfo_exists()) return false;
    try {
        int top = trigger->winfo_rooty() - 3, bottom = trigger->winfo_rooty() + trigger->winfo_height() + 3;
        int left = trigger->winfo_rootx(), right = box->winfo_rootx() + 4;
        return x >= left && x <= right && y >= top && y <= bottom;
    } catch (const tk::TclError&) {
        return false;
    }
}

void clear_content(App* app) {
    if (g_box == nullptr || !g_box->winfo_exists()) return;
    for (tk::Widget* child : g_box->winfo_children()) child->destroy();
    g_content = nullptr;
    (void)app;
}

void poll(App* app, unsigned token) {
    if (token != g_poll_token || !g_visible) return;
    int x = 0, y = 0;
    bool known = pointer(app, x, y);
    bool keep = !known || inside(g_box, x, y, 4) || inside(g_trigger, x, y, 2) || in_corridor(g_trigger, g_box, x, y);
    if (g_trigger != nullptr && g_trigger->winfo_exists() && !g_trigger->winfo_ismapped()) keep = false;  // strona menu została przełączona
    g_misses = keep ? 0 : g_misses + 1;
    if (g_misses >= MISSES_TO_HIDE) {
        hide(app);
        return;
    }
    app->after(POLL_MS, [app, token] { poll(app, token); });
}

void register_item(std::function<void()> run) { g_runs.push_back(std::move(run)); }

void bind_click(tk::Widget* widget, std::function<void()> run) {
    widget->bind("<Button-1>", [run](tk::Event&) {
        run();
        return std::string();
    });
}

void bind_hover(tk::Frame* row, const std::vector<tk::Widget*>& parts) {
    auto paint = [row, parts](bool on) {
        if (!row->winfo_exists()) return;
        const char* color = on ? forma_theme::ACCENT_SOFT : forma_theme::CARD;
        row->configure({{"bg", color}, {"highlightbackground", on ? forma_theme::ACCENT : forma_theme::BORDER}});
        for (tk::Widget* part : parts)
            if (part->winfo_exists()) part->configure({{"bg", color}});
    };
    std::vector<tk::Widget*> all = parts;
    all.push_back(row);
    for (tk::Widget* widget : all) {
        widget->bind("<Enter>", [paint](tk::Event&) {
            paint(true);
            return std::string();
        }, true);
    }
    row->bind("<Leave>", [paint](tk::Event&) {
        paint(false);
        return std::string();
    }, true);
}

// Jedna karta: pasek koloru (materiały), tytuł i opis.
tk::Frame* card(tk::Widget* body, const std::string& title, const std::string& sub, const std::string& swatch, std::function<void()> run) {
    auto* row = new tk::Frame(body, {{"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER}, {"cursor", "hand2"}});
    row->pack({{"fill", "x"}, {"pady", 2}});
    std::vector<tk::Widget*> parts;
    if (!swatch.empty()) {
        auto* color = new tk::Frame(row, {{"bg", swatch}, {"width", 22}, {"height", 22}});
        color->pack({{"side", "left"}, {"padx", "8 2"}, {"pady", 6}});
        bind_click(color, run);
    }
    auto* text = new tk::Frame(row, {{"bg", forma_theme::CARD}});
    text->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 8}, {"pady", 5}});
    auto* head = new tk::Label(text, {{"text", title}, {"bg", forma_theme::CARD}, {"fg", forma_theme::TEXT}, {"font", "{Segoe UI} 9 bold"}, {"anchor", "w"}, {"justify", "left"}, {"wraplength", WIDTH - 90}});
    head->pack({{"anchor", "w"}});
    parts = {text, head};
    if (!sub.empty()) {
        auto* detail = new tk::Label(text, {{"text", sub}, {"bg", forma_theme::CARD}, {"fg", forma_theme::MUTED}, {"font", "{Segoe UI} 8"}, {"anchor", "w"}, {"justify", "left"}, {"wraplength", WIDTH - 90}});
        detail->pack({{"anchor", "w"}});
        parts.push_back(detail);
    }
    for (tk::Widget* part : parts) bind_click(part, run);
    bind_click(row, run);
    bind_hover(row, parts);
    register_item(run);
    return row;
}

void caption_row(tk::Widget* body, const std::string& text) {
    auto* label = new ttk::Label(body, {{"text", text}, {"style", "Panel.TLabel"}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 8 bold"}});
    label->pack({{"anchor", "w"}, {"pady", "8 2"}});
}

bool is_mounting(const json& item) {
    std::string kind = item.at("kind").get<std::string>();
    return (kind == "hinge" || kind == "runner" || kind == "led" || kind == "handle") && !item.at("payload").contains("option");
}

void ensure_box(App* app) {
    if (g_box != nullptr && g_box->winfo_exists()) return;
    g_box = new tk::Frame(app->view_frame, {{"bg", forma_theme::BORDER}});
}

}  // namespace

void hide(App* app) {
    ++g_poll_token;
    ++g_enter_token;
    g_visible = false;
    g_misses = 0;
    g_trigger = nullptr;
    if (g_box == nullptr || !g_box->winfo_exists()) return;
    g_box->place_forget();
    // Zawartość usuwamy po chwili: hide() bywa wołane z kliknięcia elementu, który właśnie niszczymy.
    app->after(1, [app] {
        if (!g_visible) clear_content(app);
    });
}

bool visible() { return g_visible; }

int last_item_count() { return static_cast<int>(g_runs.size()); }

bool click_item(int index) {
    if (index < 0 || index >= static_cast<int>(g_runs.size())) return false;
    g_runs[static_cast<size_t>(index)]();
    return true;
}

void show_now(App* app, const std::string& title, Builder builder, tk::Widget* trigger) {
    if (app->view_frame == nullptr) return;
    ensure_box(app);
    clear_content(app);
    g_runs.clear();
    g_trigger = trigger;
    g_content = new tk::Frame(g_box, {{"bg", forma_theme::PANEL}});
    g_content->pack({{"fill", "both"}, {"expand", true}, {"padx", 1}, {"pady", 1}});
    auto* head = new tk::Frame(g_content, {{"bg", forma_theme::PANEL}});
    head->pack({{"fill", "x"}});
    auto* caption = new tk::Label(head, {{"text", title}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::TEXT}, {"font", "{Segoe UI} 11 bold"}});
    caption->pack({{"side", "left"}, {"padx", 12}, {"pady", "10 6"}});
    auto* close = new tk::Label(head, {{"text", "✕"}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED}, {"font", "{Segoe UI} 11"}, {"cursor", "hand2"}});
    close->pack({{"side", "right"}, {"padx", 12}});
    close->bind("<Button-1>", [app](tk::Event&) {
        hide(app);
        return std::string();
    });
    auto* scroll = new quick_studio_ui::ScrollPanel(g_content);
    scroll->pack({{"fill", "both"}, {"expand", true}});
    builder(scroll->body);
    int width = std::min(WIDTH, std::max(200, app->view_frame->winfo_width() - 24));
    g_box->place({{"x", 0}, {"y", 0}, {"width", width}, {"relheight", 1.0}});
    g_box->lift();
    g_visible = true;
    g_misses = 0;
    unsigned token = ++g_poll_token;
    app->after(POLL_MS * 4, [app, token] { poll(app, token); });
}

void attach(App* app, tk::Widget* trigger, const std::string& title, Builder builder) {
    trigger->bind("<Enter>", [app, trigger, title, builder](tk::Event&) {
        if (g_visible && g_trigger == trigger) {
            g_misses = 0;
            return std::string();
        }
        unsigned token = ++g_enter_token;
        app->after(SHOW_DELAY_MS, [app, trigger, title, builder, token] {
            if (token != g_enter_token || !trigger->winfo_exists()) return;
            int x = 0, y = 0;
            if (pointer(app, x, y) && !inside(trigger, x, y, 1)) return;  // mysz zdążyła już zjechać
            try {
                show_now(app, title, builder, trigger);
            } catch (const std::exception& error) {
                app->status.set(std::string("Okienko wyboru: ") + error.what());
                journal::event(app, "menu", std::string("okienko wyboru — błąd: ") + error.what());
            }
        });
        return std::string();
    }, true);
    trigger->bind("<Leave>", [](tk::Event&) {
        ++g_enter_token;  // nie otwieramy okienka, jeśli mysz zjechała przed upływem opóźnienia
        return std::string();
    }, true);
}

Builder catalogue(App* app, const std::vector<std::string>& kinds) {
    return [app, kinds](tk::Widget* body) {
        auto note = std::make_shared<tk::StringVar>("");
        auto* error = new tk::Label(body, {{"textvariable", *note}, {"bg", forma_theme::PANEL}, {"fg", "#b3261e"}, {"font", "{Segoe UI} 8"}, {"anchor", "w"},
                                           {"justify", "left"}, {"wraplength", WIDTH - 60}});
        error->pack({{"anchor", "w"}});
        for (const std::string& kind : kinds) {
            json items;
            try {
                items = visual_catalogue::catalogue_items(kind, *app->project);
            } catch (const std::exception& problem) {
                note->set(problem.what());
                continue;
            }
            if (kinds.size() > 1) {
                std::string label = kind;
                for (const auto& [key, text] : visual_catalogue::KINDS)
                    if (key == kind) label = text;
                caption_row(body, label);
            }
            std::string category;
            for (const json& item : items) {
                std::string item_category = item.value("category", std::string());
                if (item_category != category && kinds.size() == 1 && !item_category.empty()) caption_row(body, item_category);
                category = item_category;
                std::string swatch;
                if (item.at("kind") == json("material") && item.contains("preview") && item.at("preview").contains("color")) swatch = item.at("preview").at("color").get<std::string>();
                std::string title = item.at("title").get<std::string>();
                std::string sub;
                try {
                    sub = visual_catalogue::dimension_text(item);
                } catch (const std::exception&) {
                }
                json copy = item;
                card(body, title, sub, swatch, [app, copy, note, title] {
                    try {
                        if (!visual_catalogue::apply_item(app, copy)) return;
                        note->set("");
                        bool mounting = is_mounting(copy);
                        std::string action = mounting ? "wskaż miejsce na meblu, aby zamontować." : "zastosowano.";
                        app->status.set("Biblioteka: " + title + " · " + action);
                        journal::event(app, "biblioteka", "okienko wyboru: " + title);
                        if (mounting) hide(app);
                    } catch (const std::exception& problem) {
                        std::string text = problem.what();
                        if (text.empty()) text = "Nie udało się zastosować karty.";
                        note->set(text);
                        app->status.set("Biblioteka: " + text);
                        journal::event(app, "biblioteka", "błąd (okienko wyboru): " + text);
                    }
                });
            }
        }
        if (g_runs.empty()) {
            auto* empty = new tk::Label(body, {{"text", "Brak pozycji w tym katalogu."}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}});
            empty->pack({{"anchor", "w"}, {"pady", 8}});
        }
    };
}

Builder actions(std::vector<Action> items) {
    return [items = std::move(items)](tk::Widget* body) {
        for (const Action& action : items) {
            auto run = action.run;
            card(body, action.label, action.hint, "", [run] {
                if (run) run();
            });
        }
    };
}

}  // namespace hover_flyout
