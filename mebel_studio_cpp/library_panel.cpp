#include "library_panel.h"

#include <algorithm>
#include <format>
#include <map>
#include <vector>

#include "app.h"
#include "catalogue_previews.h"
#include "forma_theme.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "perf_monitor.h"
#include "responsive_ui.h"
#include "side_panels.h"
#include "visual_catalogue.h"

namespace library_panel {

namespace {

constexpr int COLUMNS = 3;
constexpr int PREVIEW_W = 90, PREVIEW_H = 72;

// Etykiety krótkie, żeby mieściły się w rzędzie przycisków rodzajów.
const std::string HINT = "⌕  Szukaj modułów, frontów, materiałów…";

const std::vector<std::pair<std::string, std::string>> CHIPS = {
    {"furniture", "Meble"}, {"material", "Materiały"}, {"handle", "Uchwyty"}, {"hinge", "Zawiasy"},
    {"runner", "Prowadnice"}, {"leg", "Nogi"}, {"led", "LED"}, {"appliance", "AGD"}};

}  // namespace

LibraryPanel::LibraryPanel(tk::Widget* parent, App* app) : ttk::Frame(parent, {{"style", "Panel.TFrame"}}), app_(app) {
    query_ = tk::StringVar(HINT);
    note_ = tk::StringVar("Kliknij kartę, aby dodać ją do projektu.");
    auto* top = new ttk::Frame(this, {{"style", "Panel.TFrame"}, {"padding", "10 4 10 2"}});
    top->pack({{"fill", "x"}});
    entry_ = new ttk::Entry(top, {{"textvariable", query_}, {"foreground", forma_theme::MUTED}});
    entry_->pack({{"fill", "x"}, {"pady", "0 4"}});
    entry_->bind("<FocusIn>", [this](tk::Event&) {
        if (query_.get() == HINT) { query_.set(""); entry_->configure({{"foreground", forma_theme::TEXT}}); }
        return std::string();
    }, true);
    entry_->bind("<FocusOut>", [this](tk::Event&) {
        if (query_.get().empty()) { query_.set(HINT); entry_->configure({{"foreground", forma_theme::MUTED}}); }
        return std::string();
    }, true);
    auto* chips = new responsive_ui::WrapToolbar(this, {{"style", "Panel.TFrame"}});
    chips->pack({{"fill", "x"}, {"padx", 8}});
    app_->responsive_toolbars.push_back(chips);
    for (const auto& [kind, label] : CHIPS) {
        auto* chip = new ttk::Button(chips, {{"text", label}, {"style", "Chip.TButton"}, {"command", std::function<void()>([this, kind] { show_kind(kind); })}});
        chip->pack({{"side", "left"}});
        chips_[kind] = chip;
    }
    auto* bottom = new ttk::Frame(this, {{"style", "Panel.TFrame"}, {"padding", "10 6"}});
    bottom->pack({{"side", "bottom"}, {"fill", "x"}});
    (new tk::Label(bottom, {{"textvariable", note_}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED}, {"font", "{Segoe UI} 9"},
                            {"wraplength", 270}, {"justify", "left"}, {"anchor", "w"}}))->pack({{"fill", "x"}});
    scroll_ = new quick_studio_ui::ScrollPanel(this);
    scroll_->pack({{"fill", "both"}, {"expand", true}});
    query_.trace_add("write", [this] { schedule(); });
    bind("<Map>", [this](tk::Event&) {
        if (generation_ != app_->project_generation) refresh();
        return std::string();
    }, true);
}

void LibraryPanel::schedule() {
    if (job_) after_cancel(*job_);
    job_ = after(150, [this] { job_.reset(); render(); });
}

// Przebudowa po zakończeniu obsługi zdarzenia: widget, który je wywołał, jest niszczony przy renderowaniu.
void LibraryPanel::render_later() {
    if (job_) after_cancel(*job_);
    job_ = after(1, [this] { job_.reset(); render(); });
}

void LibraryPanel::load_items() {
    generation_ = app_->project_generation;
    try {
        items_ = visual_catalogue::catalogue_items(kind_, app_->project.get());
    } catch (const std::exception& error) {
        items_ = json::array();
        note_.set(error.what());
    }
}

void LibraryPanel::refresh() {
    load_items();
    render();
}

void LibraryPanel::show_kind(const std::string& kind) {
    kind_ = visual_catalogue::canonical_kind(kind);
    expanded_.clear();
    query_.set(HINT);
    entry_->configure({{"foreground", forma_theme::MUTED}});
    selected_id_.clear();
    note_.set("Kliknij kartę, aby dodać ją do projektu.");
    load_items();
    render();
    if (app_->select_tool_page) app_->select_tool_page("Biblioteka");
    scroll_->canvas->yview_moveto(0);
}

void LibraryPanel::render() {
    perf::Scope perf_scope("LibraryPanel::render");
    for (const auto& [kind, chip] : chips_) chip->configure({{"style", kind == kind_ ? "ChipOn.TButton" : "Chip.TButton"}});
    tk::Widget* body = scroll_->body;
    cards_.clear();
    for (tk::Widget* child : body->winfo_children()) child->destroy();
    std::string search = query_.get();
    if (search == HINT) search.clear();
    json filtered = visual_catalogue::filter_items(items_, search, "Wszystkie");
    bool searching = !search.empty();

    std::vector<std::string> order;
    std::map<std::string, std::vector<json>> groups;
    for (const auto& item : filtered) {
        std::string category = item.at("category").get<std::string>();
        if (!groups.contains(category)) order.push_back(category);
        groups[category].push_back(item);
    }
    if (filtered.empty()) {
        (new tk::Label(body, {{"text", "Brak pozycji."}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED}}))->pack({{"anchor", "w"}, {"pady", 8}});
    }
    for (const auto& category : order) {
        const auto& list = groups[category];
        bool all = searching || expanded_.contains(category) || list.size() <= (size_t)COLUMNS;
        auto* head = new ttk::Frame(body, {{"style", "Panel.TFrame"}});
        head->pack({{"fill", "x"}, {"pady", "10 4"}});
        (new tk::Label(head, {{"text", category}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::TEXT}, {"font", "{Segoe UI} 11 bold"}}))->pack({{"side", "left"}});
        if (!searching && list.size() > (size_t)COLUMNS) {
            bool open = expanded_.contains(category);
            auto* more = new tk::Label(head, {{"text", open ? "Pokaż mniej ‹" : "Zobacz wszystkie ›"}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::ACCENT},
                                              {"font", "{Segoe UI} 9"}, {"cursor", "hand2"}});
            more->pack({{"side", "right"}});
            more->bind("<Button-1>", [this, category, open](tk::Event&) {
                if (open) expanded_.erase(category); else expanded_.insert(category);
                render_later();
                return std::string();
            });
        }
        auto* grid = new ttk::Frame(body, {{"style", "Panel.TFrame"}});
        grid->pack({{"fill", "x"}});
        for (int c = 0; c < COLUMNS; ++c) grid->columnconfigure(c, {{"weight", 1}, {"uniform", "cards"}});
        size_t shown = all ? list.size() : (size_t)COLUMNS;
        for (size_t i = 0; i < shown; ++i) {
            const json& item = list[i];
            bool selected = item.at("id").get<std::string>() == selected_id_;
            auto* card = new tk::Frame(grid, {{"bg", forma_theme::PANEL}});
            card->grid({{"row", (int)(i / COLUMNS)}, {"column", (int)(i % COLUMNS)}, {"padx", 3}, {"pady", 3}, {"sticky", "n"}});
            auto* image = new tk::Canvas(card, {{"width", PREVIEW_W}, {"height", PREVIEW_H}, {"bg", selected ? forma_theme::ACCENT_SOFT : forma_theme::CARD},
                                                {"highlightthickness", 1}, {"highlightbackground", selected ? forma_theme::ACCENT : forma_theme::BORDER}, {"cursor", "hand2"}});
            image->pack();
            cards_[item.at("id").get<std::string>()] = image;
            try {
                catalogue_previews::draw_preview(image, item, PREVIEW_W, PREVIEW_H);
            } catch (const std::exception&) {
            }
            auto* title = new tk::Label(card, {{"text", item.at("title").get<std::string>()}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::TEXT},
                                               {"font", "{Segoe UI} 9 bold"}, {"wraplength", PREVIEW_W + 10}, {"justify", "center"}, {"cursor", "hand2"}});
            title->pack({{"pady", "3 0"}});
            auto* dims = new tk::Label(card, {{"text", visual_catalogue::dimension_text(item)}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED},
                                              {"font", "{Segoe UI} 8"}, {"wraplength", PREVIEW_W + 10}, {"justify", "center"}});
            dims->pack();
            json copy = item;
            for (tk::Widget* widget : std::vector<tk::Widget*>{card, image, title, dims})
                widget->bind("<Button-1>", [this, copy](tk::Event&) { activate(copy); return std::string(); });
        }
    }
    scroll_->install_wheel(body);
}

void LibraryPanel::highlight(const std::string& id, bool on) {
    auto found = cards_.find(id);
    if (found == cards_.end()) return;
    try {
        found->second->configure({{"bg", on ? forma_theme::ACCENT_SOFT : forma_theme::CARD}, {"highlightbackground", on ? forma_theme::ACCENT : forma_theme::BORDER}});
    } catch (const tk::TclError&) {
    }
}

void LibraryPanel::activate(const json& item) {
    try {
        if (generation_ != app_->project_generation) {
            load_items();
            throw py::ValueError("Projekt zmienił się. Lista została odświeżona — kliknij kartę jeszcze raz.");
        }
        if (!visual_catalogue::apply_item(app_, item)) return;
        std::string previous_id = selected_id_;
        selected_id_ = item.at("id").get<std::string>();
        std::string item_kind = item.at("kind").get<std::string>();
        bool mounting = (item_kind == "hinge" || item_kind == "runner" || item_kind == "led" || item_kind == "handle") && !item.at("payload").contains("option");
        std::string action = mounting ? "Wskaż miejsce na meblu, aby zamontować." : "Dodano do projektu.";
        note_.set(item.at("title").get<std::string>() + " · " + action);
        app_->status.set("Biblioteka: " + item.at("title").get<std::string>() + " · " + action);
        generation_ = app_->project_generation;
        json shown = item;
        side_panels::show_item(app_, shown, [this, shown] { activate(shown); });
        // Zamiast przebudowywać całą listę (ok. 120 ms) przestawiamy tylko podświetlenie.
        if (!previous_id.empty()) highlight(previous_id, false);
        highlight(selected_id_, true);
    } catch (const std::invalid_argument& error) {
        note_.set(error.what());
    } catch (const std::out_of_range& error) {
        note_.set(error.what());
    } catch (const core::json::exception& error) {
        note_.set(error.what());
    } catch (const py::StopIteration& error) {
        note_.set(error.what());
    }
}

}  // namespace library_panel
