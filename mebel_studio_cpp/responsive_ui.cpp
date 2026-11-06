// Responsive toolbar layout and user controlled panes, independent of projects.
#include "responsive_ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include "app.h"
#include "dock_workspace.h"
#include "pyutil.h"
#include "ui_preferences.h"
#include "sketch.h"

#ifdef _WIN32
#include "win32_compat.h"
#endif

namespace responsive_ui {

using core::json;

std::pair<std::vector<std::array<int, 4>>, int> flow_layout(double width_in, const std::vector<std::pair<double, double>>& sizes, int gap) {
    int width = std::max(1, (int)width_in), x = 0, y = 0, row_height = 0;
    std::vector<std::array<int, 4>> result;
    for (const auto& [requested_w, requested_h] : sizes) {
        int w = std::min(width, std::max(1, (int)requested_w)), h = std::max(1, (int)requested_h);
        if (x && x + w > width) { x = 0; y += row_height + gap; row_height = 0; }
        result.push_back({x, y, w, h});
        x += w + gap;
        row_height = std::max(row_height, h);
    }
    int total = result.empty() ? 1 : y + row_height;
    return {result, total};
}

void update_scrollregion(tk::Canvas* canvas) {
    auto box = canvas->bbox("all");
    std::string region;
    if (box) {
        std::ostringstream out;
        for (size_t i = 0; i < box->size(); ++i) out << (i ? " " : "") << static_cast<long long>((*box)[i]);
        region = out.str();
    }
    canvas->configure({{"scrollregion", region}});
}

WrapToolbar::WrapToolbar(tk::Widget* parent, const tk::Opts& options) : ttk::Frame(parent, options) {
    pack_propagate(false);
    configure({{"height", 1}});
    bind("<Configure>", [this](tk::Event&) { schedule(); return std::string(); }, true);
    bind("<Map>", [this](tk::Event&) { schedule(); return std::string(); }, true);
    bind("<Destroy>", [this](tk::Event& e) { destroyed(e); return std::string(); }, true);
}

void WrapToolbar::destroyed(tk::Event& event) {
    if (event.widget == this && _job) {
        try { after_cancel(*_job); } catch (const tk::TclError&) {}
        _job.reset();
    }
}

void WrapToolbar::schedule() {
    if (!_job) _job = after_idle([this] { reflow(); });
}

void WrapToolbar::reflow() {
    _job.reset();
    if (!winfo_exists()) return;
    std::vector<tk::Widget*> children = winfo_children();
    int width = std::max(1, winfo_width());
    std::vector<std::pair<double, double>> sizes;
    for (auto* w : children) sizes.push_back({(double)w->winfo_reqwidth() + 4, (double)w->winfo_reqheight() + 4});
    std::vector<std::tuple<std::string, int, int>> items;
    for (size_t i = 0; i < children.size(); ++i) items.emplace_back(children[i]->path(), (int)sizes[i].first, (int)sizes[i].second);
    std::pair<int, std::vector<std::tuple<std::string, int, int>>> key{width, items};
    if (_layout_key && *_layout_key == key) return;
    _layout_key = key;
    auto [placements, height] = flow_layout(width, sizes);
    for (size_t i = 0; i < children.size(); ++i) {
        auto [x, y, w, h] = placements[i];
        if (children[i]->winfo_manager() == "pack") children[i]->pack_forget();
        children[i]->place({{"x", x + 2}, {"y", y + 2}, {"width", std::max(1, w - 4)}, {"height", std::max(1, h - 4)}});
    }
    configure({{"height", height}});
}

RibbonPage::RibbonPage(tk::Widget* parent) : ttk::Frame(parent) {
    canvas = new tk::Canvas(this, {{"highlightthickness", 0}, {"bg", "#f6f8fa"}, {"height", 76}});
    auto* scrollbar = new ttk::Scrollbar(this, {{"orient", "vertical"}, {"command", canvas->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    canvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    canvas->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    body = new WrapToolbar(canvas);
    window = canvas->create_window(0, 0, {{"window", body->path()}, {"anchor", "nw"}});
    body->bind("<Configure>", [this](tk::Event&) { update_scrollregion(canvas); return std::string(); }, true);
    canvas->bind("<Configure>", [this](tk::Event& e) { canvas->itemconfigure(window, {{"width", e.width}}); return std::string(); });
    canvas->bind("<MouseWheel>", [this](tk::Event& e) { canvas->yview_scroll(e.delta > 0 ? -1 : 1, "units"); return std::string(); });
}

FieldRow::FieldRow(tk::Widget* parent, const std::string& label_text, const tk::Opts& entry_options) : ttk::Frame(parent) {
    label = new ttk::Label(this, {{"text", label_text}, {"font", "{Segoe UI} 9"}});
    entry = new ttk::Entry(this, entry_options);
    columnconfigure(0, {{"weight", 1}});
    bind("<Configure>", [this](tk::Event&) { arrange(); return std::string(); });
    _idle = after_idle([this] { initial_arrange(); });
    bind("<Destroy>", [this](tk::Event& e) { destroyed(e); return std::string(); }, true);
}

void FieldRow::initial_arrange() {
    _idle.reset();
    arrange();
}

void FieldRow::destroyed(tk::Event& event) {
    if (event.widget == this && _idle) {
        try { after_cancel(*_idle); } catch (const tk::TclError&) {}
        _idle.reset();
    }
}

void FieldRow::arrange() {
    int width = std::max(1, winfo_width());
    int label_width = tk::font::Font(this, label->cget("font")).measure(label->cget("text")) + 4;
    bool stack = width < label_width + entry->winfo_reqwidth() + 8;
    label->configure({{"wraplength", stack ? std::max(80, width - 4) : 0}});
    if (_stacked && stack == *_stacked) return;
    _stacked = stack;
    label->grid_forget();
    entry->grid_forget();
    if (stack) {
        label->grid({{"row", 0}, {"column", 0}, {"sticky", "w"}, {"pady", "0 2"}});
        entry->grid({{"row", 1}, {"column", 0}, {"sticky", "ew"}});
    } else {
        label->configure({{"wraplength", 0}});
        label->grid({{"row", 0}, {"column", 0}, {"sticky", "w"}});
        entry->grid({{"row", 0}, {"column", 1}, {"sticky", "e"}, {"padx", "5 0"}});
    }
}

json monitor_info(tk::Widget* window, const std::optional<std::pair<long long, long long>>& point) {
    try {
#ifdef _WIN32
        HWND hwnd = nullptr;
        if (window != nullptr) hwnd = GetAncestor(reinterpret_cast<HWND>(static_cast<INT_PTR>(window->winfo_id())), 2);
        HMONITOR monitor = point ? MonitorFromPoint(POINT{(LONG)point->first, (LONG)point->second}, 2) : MonitorFromWindow(hwnd, 2);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) throw std::system_error(std::error_code((int)GetLastError(), std::system_category()));
        RECT r = info.rcWork;
        int dpi = 96;
        if (hwnd != nullptr) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto fn = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
            if (fn != nullptr) { UINT value = fn(hwnd); dpi = value ? (int)value : 96; }
        }
        std::array<long long, 2> frame{16, 64};
        if (hwnd != nullptr) {
            RECT outer{}, client{};
            if (win32_compat::GetWindowRect(hwnd, &outer) && win32_compat::GetClientRect(hwnd, &client)) {
                long long fw = (outer.right - outer.left) - (client.right - client.left);
                long long fh = (outer.bottom - outer.top) - (client.bottom - client.top);
                // An unrealized Tk root reports zero-sized decorations. Keep
                // enough room for the caption and native menu until mapped.
                if (0 <= fw && fw <= 200 && 24 <= fh && fh <= 200) frame = {fw, fh};
            }
        }
        return json{{"workarea", json::array({r.left, r.top, r.right, r.bottom})}, {"dpi", dpi}, {"frame", json::array({frame[0], frame[1]})}};
#else
        (void)point;
        throw std::system_error(std::make_error_code(std::errc::not_supported));
#endif
    } catch (const std::system_error&) {
    } catch (const tk::TclError&) {
    }
    return json{{"workarea", json::array({0, 0, window ? window->winfo_screenwidth() : 1920, window ? window->winfo_screenheight() : 1080})}, {"dpi", 96}};
}

std::array<long long, 4> fitted_geometry(const json& preferences, const std::array<long long, 4>& workarea, const std::array<long long, 2>& frame) {
    json p = ui_preferences::validate_preferences(preferences);
    auto [left, top, right, bottom] = workarea;
    long long available_w = std::max(1LL, right - left - frame[0]), available_h = std::max(1LL, bottom - top - frame[1]);
    long long w, h;
    if (p.at("window_mode") == "auto") {
        w = std::min(1440LL, std::max(900LL, (long long)((double)available_w * .94)));
        h = std::min(900LL, std::max(600LL, (long long)((double)available_h * .94)));
    } else {
        w = p.at("window_width").get<long long>();
        h = p.at("window_height").get<long long>();
    }
    w = std::min(w, available_w);
    h = std::min(h, available_h);
    long long x = !p.at("window_x").is_null() ? p.at("window_x").get<long long>() : left + (long long)std::floor((double)(available_w - w) / 2);
    long long y = !p.at("window_y").is_null() ? p.at("window_y").get<long long>() : top + (long long)std::floor((double)(available_h - h) / 2);
    return {w, h, std::max(left, std::min(x, right - w - frame[0])), std::max(top, std::min(y, bottom - h - frame[1]))};
}

long long clamp_inspector(double width, long long total, double scale) {
    long long minimum = std::min(240LL, std::max(1LL, total - 320));
    long long maximum = std::max(minimum, total - std::max(320LL, (long long)(320 * std::min(scale, 1.25))) - 10);
    return std::max(minimum, std::min((long long)width, maximum));
}

UiLayout::UiLayout(App* app_, const json& preferences, const std::optional<std::filesystem::path>& path_) : app(app_), path(path_) {
    prefs = ui_preferences::validate_preferences(!preferences.is_null() && py::truthy(preferences) ? preferences : ui_preferences::load_preferences(path));
    if (app->inspector_tabs != nullptr) {
        panel_var = std::make_unique<tk::StringVar>(app, "");
        panel_selector = new ttk::Combobox(app->inspector_frame, {{"textvariable", *panel_var}, {"state", "readonly"}});
        panel_selector->bind("<<ComboboxSelected>>", [this](tk::Event&) { select_panel(); return std::string(); });
        app->inspector_tabs->bind("<<NotebookTabChanged>>", [this](tk::Event&) { sync_panel(); return std::string(); }, true);
        ttk::Style(app).layout("HiddenInspector.TNotebook.Tab", {});
    }
    app->main_pane->bind("<Configure>", [this](tk::Event&) { schedule(); return std::string(); }, true);
    app->main_pane->bind("<ButtonRelease-1>", [this](tk::Event&) { sash_released(); return std::string(); }, true);
    if (app->workspace_pane != nullptr) {
        app->workspace_pane->bind("<ButtonRelease-1>", [this](tk::Event&) { sash_released(); return std::string(); }, true);
        app->workspace_pane->bind("<Configure>", [this](tk::Event&) { schedule(); return std::string(); }, true);
    }
    app->bind("<Configure>", [this](tk::Event& e) { window_changed(e); return std::string(); }, true);
    app->bind("<Destroy>", [this](tk::Event& e) { if (e.widget == app) close(false); return std::string(); }, true);
    apply(prefs, true);
}

void UiLayout::schedule() {
    if (suspended) return;
    if (job) app->after_cancel(*job);
    job = app->after(35, [this] { reflow(); });
}

void UiLayout::reflow() {
    job.reset();
    auto panes_contain = [](ttk::Panedwindow* pane, tk::Widget* child) {
        auto panes = pane->panes();
        return std::find(panes.begin(), panes.end(), child->path()) != panes.end();
    };
    if (py::truthy(prefs.at("show_inspector")) && panes_contain(app->main_pane, app->inspector_frame)) {
        long long w = clamp_inspector((double)std::min(1200LL, prefs.at("inspector_width").get<long long>()), app->main_pane->winfo_width(), prefs.at("ui_scale").get<double>());
        if (app->main_pane->sashpos(0) != w) app->main_pane->sashpos(0, (int)w);
    }
    if (app->workspace_pane != nullptr && py::truthy(prefs.at("show_ribbon")) && app->workspace_pane->panes().size() > 1) {
        long long h = std::max(64LL, std::min({500LL, prefs.at("ribbon_height").get<long long>(), std::max(64LL, (long long)(app->workspace_pane->winfo_height() * .35))}));
        if (app->canvas->winfo_ismapped()) {
            // Extra fitting tools use space above the viewport. Reduce
            // the effective ribbon first; keep the user's saved height
            // so it returns when that tray closes.
            long long available = app->canvas->winfo_height() + app->workspace_pane->sashpos(0);
            h = std::max(64LL, std::min(h, available - 240));
        }
        if (app->workspace_pane->sashpos(0) != h) app->workspace_pane->sashpos(0, (int)h);
    }
    for (auto* toolbar : app->responsive_toolbars) toolbar->schedule();
    if (panel_selector != nullptr) {
        std::vector<std::string> tabs = app->inspector_tabs->tabs(), labels;
        for (const auto& tab : tabs) labels.push_back(app->inspector_tabs->tab(tab, "text"));
        panel_selector->configure({{"values", labels}});
        std::string spec = ttk::Style(app).lookup("Inspector.TNotebook.Tab", "font");
        tk::font::Font font(app, spec.empty() ? std::string("{Segoe UI} 9") : spec);
        int needed = 0;
        for (const auto& label : labels) needed += font.measure(label) + 24;
        if (needed > app->inspector_frame->winfo_width() - 12) {
            app->inspector_tabs->configure({{"style", "HiddenInspector.TNotebook"}});
            if (panel_selector->winfo_manager().empty()) panel_selector->pack({{"fill", "x"}, {"padx", 5}, {"pady", 4}, {"before", app->inspector_tabs->path()}});
        } else {
            panel_selector->pack_forget();
            app->inspector_tabs->configure({{"style", "Inspector.TNotebook"}});
        }
        sync_panel();
    }
}

void UiLayout::select_panel() {
    int index = panel_selector->current();
    auto tabs = app->inspector_tabs->tabs();
    if (0 <= index && index < (int)tabs.size()) app->inspector_tabs->select(tabs[(size_t)index]);
}

void UiLayout::sync_panel() {
    if (panel_selector != nullptr) {
        std::string selected = app->inspector_tabs->select();
        if (!selected.empty()) panel_var->set(app->inspector_tabs->tab(selected, "text"));
    }
}

void UiLayout::sash_released() {
    if (suspended) return;
    if (py::truthy(prefs.at("show_inspector")) && app->main_pane->panes().size() > 1)
        prefs["inspector_width"] = std::max(240, std::min(1200, app->main_pane->sashpos(0)));
    if (py::truthy(prefs.at("show_ribbon")) && app->workspace_pane != nullptr && app->workspace_pane->panes().size() > 1)
        prefs["ribbon_height"] = std::max(64, std::min(500, app->workspace_pane->sashpos(0)));
    schedule();
    schedule_save();
}

void UiLayout::window_changed(tk::Event& event) {
    if (event.widget == app && !suspended) schedule_save();
}

void UiLayout::schedule_save() {
    if (save_job) app->after_cancel(*save_job);
    save_job = app->after(650, [this] { persist(); });
}

json UiLayout::preferences() { return prefs; }

bool UiLayout::persist() {
    if (save_job) {
        try { app->after_cancel(*save_job); } catch (const tk::TclError&) {}
    }
    save_job.reset();
    try {
        if (app->state() == "normal" && app->winfo_width() >= 900 && app->winfo_height() >= 600) {
            prefs["window_width"] = app->winfo_width(); prefs["window_height"] = app->winfo_height();
            prefs["window_x"] = app->winfo_x(); prefs["window_y"] = app->winfo_y();
        }
        ui_preferences::save_preferences(prefs, path);
        return true;
    } catch (const std::system_error& error) {
        app->status.set(std::string("Nie zapisano ustawień interfejsu: ") + error.what());
        return false;
    } catch (const py::ValueError& error) {
        app->status.set(std::string("Nie zapisano ustawień interfejsu: ") + error.what());
        return false;
    }
}

void UiLayout::close(bool persist_) {
    if (persist_) persist();
    for (std::optional<std::string>* name : {&job, &save_job}) {
        if (*name) {
            try { app->after_cancel(**name); } catch (const tk::TclError&) {}
            name->reset();
        }
    }
}

void UiLayout::apply(const json& preferences_, bool geometry) {
    json p = ui_preferences::validate_preferences(preferences_);
    suspended = true;
    prefs = p;
    try {
        set_scale(p.at("ui_scale").get<double>());
        if (geometry) {
            std::optional<std::pair<long long, long long>> point;
            if (!p.at("window_x").is_null() && !p.at("window_y").is_null()) point = std::make_pair(p.at("window_x").get<long long>(), p.at("window_y").get<long long>());
            json info = monitor_info(app, point);
            const json& work = info.at("workarea");
            json frame_json = py::get(info, "frame", json::array({16, 64}));
            auto [w, h, x, y] = fitted_geometry(p, {work[0].get<long long>(), work[1].get<long long>(), work[2].get<long long>(), work[3].get<long long>()},
                                                {frame_json[0].get<long long>(), frame_json[1].get<long long>()});
            app->minsize((int)std::min(900LL, w), (int)std::min(600LL, h));
            app->geometry(std::format("{}x{}+{}+{}", w, h, x, y));
        }
        show_inspector(py::truthy(p.at("show_inspector")), false);
        show_ribbon(py::truthy(p.at("show_ribbon")), false);
        show_toolbars(py::truthy(p.at("show_toolbars")), false);
        app->apply_viewport_preferences(p);
        // Existing detached and snapshot documents use the same setting;
        // they retain their independent cameras and source projects.
        if (app->workspace != nullptr) {
            std::vector<tk::Widget*> pages;
            for (auto& [name, entry] : app->workspace->entries) pages.push_back(entry.page);
            for (auto* document : pages) {
                if (auto* viz = dynamic_cast<dock_workspace::VisualizationDocument*>(document)) viz->viewport_preferences_changed();
                else if (auto* sk = dynamic_cast<sketch::Sketch*>(document)) { if (sk->viewport_preferences_changed) sk->viewport_preferences_changed(); }
            }
        }
    } catch (...) {
        suspended = false;
        throw;
    }
    suspended = false;
    schedule();
    schedule_save();
}

void UiLayout::set_scale(double scale) {
    ttk::Style style(app);
    auto spec = [](long long size, const std::string& weight = "") { return std::format("{{Segoe UI}} {}{}", size, weight.empty() ? "" : " " + weight); };
    style.configure(".", {{"font", spec((long long)py::round_half_even(10 * scale))}});
    for (auto [name, size, bold] : std::vector<std::tuple<std::string, int, bool>>{{"TNotebook.Tab", 9, true}, {"Inspector.TNotebook.Tab", 9, true}, {"Section.TButton", 9, true}, {"Treeview.Heading", 10, true}})
        style.configure(name, {{"font", spec((long long)py::round_half_even(size * scale), bold ? "bold" : "normal")}});
    style.configure("Treeview", {{"rowheight", (long long)py::round_half_even(29 * scale)}});
    style.configure("TPanedwindow", {{"sashwidth", 9}});
    style.configure("Sash", {{"sashthickness", 9}, {"gripcount", 1}});
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        try {
            std::string specification = widget->cget("font");
            if (!specification.empty()) {
                std::string key = widget->path();
                if (!base_fonts.count(key)) base_fonts[key] = tk::font::Font(app, specification).actual();
                tk::font::Actual actual = base_fonts[key];
                actual.size = (int)py::round_half_even(actual.size * scale);
                auto font = std::make_unique<tk::font::Font>(app, tk::Opts{{"family", actual.family}, {"size", actual.size}, {"weight", actual.weight},
                                                                           {"slant", actual.slant}, {"underline", actual.underline}, {"overstrike", actual.overstrike}});
                widget->configure({{"font", font->name()}});
                font_refs[key] = std::move(font);
            }
        } catch (const tk::TclError&) {
        }
        for (auto* child : widget->winfo_children()) visit(child);
    };
    visit(app);
}

void UiLayout::show_inspector(bool visible, bool save) {
    ttk::Panedwindow* pane = app->main_pane;
    ttk::Frame* child = app->inspector_frame;
    auto panes = pane->panes();
    bool present = std::find(panes.begin(), panes.end(), child->path()) != panes.end();
    if (visible && !present) pane->insert(0, child, {{"weight", 0}});
    else if (!visible && present) pane->forget(child);
    prefs["show_inspector"] = visible;
    if (save) { schedule(); schedule_save(); }
}

void UiLayout::show_ribbon(bool visible, bool save) {
    if (app->workspace_pane != nullptr) {
        ttk::Panedwindow* pane = app->workspace_pane;
        ttk::Notebook* child = app->ribbon;
        auto panes = pane->panes();
        bool present = std::find(panes.begin(), panes.end(), child->path()) != panes.end();
        if (visible && !present) pane->insert(0, child, {{"weight", 0}});
        else if (!visible && present) pane->forget(child);
    }
    prefs["show_ribbon"] = visible;
    if (save) { schedule(); schedule_save(); }
}

void UiLayout::show_toolbars(bool visible, bool save) {
    for (auto* toolbar : {app->view_toolbar, app->detail_toolbar}) {
        if (visible && toolbar->winfo_manager().empty()) toolbar->pack({{"fill", "x"}, {"before", app->canvas->path()}});
        else if (!visible && !toolbar->winfo_manager().empty()) toolbar->pack_forget();
    }
    prefs["show_toolbars"] = visible;
    if (save) { schedule(); schedule_save(); }
}

void UiLayout::fit_view() {
    app->workspace->select(app->design_page);
    app->fit();
}

void UiLayout::restore_layout() {
    json p = preferences();
    p["inspector_width"] = 330; p["ribbon_height"] = 110; p["show_inspector"] = true; p["show_ribbon"] = true; p["show_toolbars"] = true;
    apply(p, false);
}

SettingsDialog* interface_settings(App* app) { return ui_settings::open_settings(app); }

UiLayout* install(App* app, const json& preferences, const std::optional<std::filesystem::path>& path) {
    app->ui_layout = new UiLayout(app, preferences, path);
    return app->ui_layout;
}

}  // namespace responsive_ui
