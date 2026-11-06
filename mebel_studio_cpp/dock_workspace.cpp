// Tear-off Tk documents. The original frame and its state survive every move.
//
// Tk's ``wm manage``/``wm forget`` support classic Frame widgets. Documents are
// children of the application, so the same frame can also be managed by Notebook.
#include "dock_workspace.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "app.h"
#include "gpu_viewport.h"
#include "native_viewport.h"
#include "quick_studio_ui.h"
#include "viewport_modes.h"
#include "viewport_scene.h"

// windows.h dopiero po nagłówkach projektu: definiuje makra (near, far, ERROR), które psują ich identyfikatory.
#ifdef _WIN32
#include "win32_compat.h"
#endif

namespace dock_workspace {

using core::Point3;

// ---------------------------------------------------------------- WindowsDockBridge

#ifdef _WIN32
namespace {
std::set<WindowsDockBridge*>& bridges() { static std::set<WindowsDockBridge*> all; return all; }
void CALLBACK on_event(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    // Only Win32 reads and plain queue writes. No Tcl here.
    for (WindowsDockBridge* bridge : bridges()) {
        try {
            bridge->record_event(event, hwnd);
        } catch (...) {
        }
    }
}
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
GetDpiForWindowFn dpi_api() {
    static GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32"), "GetDpiForWindow");
    return fn;
}
}  // namespace
#endif

WindowsDockBridge::WindowsDockBridge() {
#ifdef _WIN32
    bridges().insert(this);  // strong reference until shutdown
    event_hook = SetWinEventHook(0x000A, 0x000B, nullptr, on_event, GetCurrentProcessId(), 0, 0);  // OUTOFCONTEXT
#endif
}

WindowsDockBridge::~WindowsDockBridge() {
    close();
#ifdef _WIN32
    bridges().erase(this);
#endif
}

void* WindowsDockBridge::hwnd(tk::Widget* page) {
#ifdef _WIN32
    return GetAncestor((HWND)page->winfo_id(), 2);  // GA_ROOT: decorated wrapper
#else
    (void)page; return nullptr;
#endif
}

std::optional<Rect> WindowsDockBridge::rect(void* hwnd) {
#ifdef _WIN32
    RECT result;
    if (!win32_compat::GetWindowRect((HWND)hwnd, &result)) {
        return std::nullopt;
    }
    return Rect{result.left, result.top, result.right, result.bottom};
#else
    (void)hwnd; return std::nullopt;
#endif
}

unsigned WindowsDockBridge::dpi(void* hwnd) {
#ifdef _WIN32
    if (auto fn = dpi_api()) { UINT value = fn((HWND)hwnd); return value ? value : 96; }
#else
    (void)hwnd;
#endif
    return 96;
}

std::optional<std::pair<long, long>> WindowsDockBridge::cursor() {
#ifdef _WIN32
    POINT point;
    if (win32_compat::GetCursorPos(&point)) {
        return std::make_pair((long)point.x, (long)point.y);
    }
#endif
    return std::nullopt;
}

bool WindowsDockBridge::left_button_down() {
    // A native window transition can lose Tk's ButtonRelease event. Read
    // only the high bit: the low bit reports a historical press instead.
#ifdef _WIN32
    return (win32_compat::GetAsyncKeyState(0x01) & 0x8000) != 0;
#else
    return false;
#endif
}

std::optional<Rect> WindowsDockBridge::workarea(double x, double y) {
#ifdef _WIN32
    HMONITOR monitor = MonitorFromPoint(POINT{(LONG)(int)x, (LONG)(int)y}, 2);
    MONITORINFO info; info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        RECT r = info.rcWork;
        return Rect{r.left, r.top, r.right, r.bottom};
    }
#else
    (void)x; (void)y;
#endif
    return std::nullopt;
}

void WindowsDockBridge::move(tk::Widget* page, double x, double y, bool fit) {
    auto r = rect(hwnd(page));
    if (!r) {
        return;
    }
    long width = (*r)[2] - (*r)[0], height = (*r)[3] - (*r)[1];
    auto work = workarea(x, y);
    std::string geometry;
    if (work && fit) {
        auto [left, top, right, bottom] = *work;
        long frame_x = width - page->winfo_width(), frame_y = height - page->winfo_height();
        width = std::min(width, right - left); height = std::min(height, bottom - top);
        x = std::max((double)left, std::min(x, (double)(right - width))); y = std::max((double)top, std::min(y, (double)(bottom - height)));
        geometry = std::format("{}x{}+{}+{}", std::max(1L, width - frame_x), std::max(1L, height - frame_y), (long)x, (long)y);
    } else {
        geometry = std::format("+{}+{}", (long)x, (long)y);
    }
    // Do not resize Tk windows directly through Win32: Configure callbacks
    // must run inside Tcl's own event processing rather than a foreign call.
    page->call({"wm", "geometry", page->path(), geometry});
}

bool WindowsDockBridge::install(tk::Widget* page) {
    if (!event_hook) {
        return false;
    }
    hooks[page->path()] = Hook{hwnd(page), std::nullopt, std::nullopt};
    return true;
}

void WindowsDockBridge::record_event(unsigned long event, void* hwnd_) {
    for (auto& [key, state] : hooks) {
        if (state.hwnd != hwnd_) continue;
        if (event == 0x000A) {  // EVENT_SYSTEM_MOVESIZESTART
            state.start = rect(hwnd_); state.start_dpi = dpi(hwnd_);
        } else if (event == 0x000B) {  // EVENT_SYSTEM_MOVESIZEEND
            auto start = state.start; auto end = rect(hwnd_); state.start.reset();
            auto start_dpi = state.start_dpi; state.start_dpi.reset();
            bool dpi_changed = start_dpi != std::optional<unsigned>(dpi(hwnd_));
            bool same_size = start && end && ((*start)[2] - (*start)[0]) == ((*end)[2] - (*end)[0]) && ((*start)[3] - (*start)[1]) == ((*end)[3] - (*end)[1]);
            if (start && end && ((*start)[0] != (*end)[0] || (*start)[1] != (*end)[1]) && (same_size || dpi_changed)) {
                auto point = cursor();
                if (point) {
                    pending.emplace_back("drop", key, *point);
                }
            }
        }
    }
}

void WindowsDockBridge::remove(const std::string& key) {
    hooks.erase(key);
}

void WindowsDockBridge::close() {
#ifdef _WIN32
    if (event_hook) {
        UnhookWinEvent((HWINEVENTHOOK)event_hook);
        event_hook = nullptr;
    }
#endif
    hooks.clear();
    pending.clear();
}

std::optional<std::pair<double, double>> WindowsDockBridge::notebook_point(ttk::Notebook* notebook, double x, double y) {
#ifdef _WIN32
    POINT point{(LONG)(int)x, (LONG)(int)y}; RECT r;
    HWND handle = (HWND)notebook->winfo_id();
    if (!win32_compat::ScreenToClient(handle, &point)) {
        return std::nullopt;
    }
    if (!win32_compat::GetClientRect(handle, &r)) {
        return std::nullopt;
    }
    if (r.right <= 0 || r.bottom <= 0) {
        return std::nullopt;
    }
    return std::make_pair(notebook->winfo_rootx() + (double)point.x * notebook->winfo_width() / r.right,
                          notebook->winfo_rooty() + (double)point.y * notebook->winfo_height() / r.bottom);
#else
    (void)notebook; (void)x; (void)y; return std::nullopt;
#endif
}

// ---------------------------------------------------------------- DockWorkspace

DockWorkspace::DockWorkspace(App* app, ttk::Notebook* notebook) : app(app), notebook(notebook) {
#ifdef _WIN32
    native = std::make_unique<WindowsDockBridge>();
#endif
    _poll_job = app->after(45, [this] { _poll_native(); });
    app->bind("<Destroy>", [this](tk::Event& e) { _app_destroyed(e); return std::string(); }, true);
    notebook->bind("<ButtonPress-1>", [this](tk::Event& e) { _tab_press(e); return std::string(); }, true);
    notebook->bind("<B1-Motion>", [this](tk::Event& e) { return _drag_motion(e); }, true);
    notebook->bind("<ButtonRelease-1>", [this](tk::Event& e) { return _drag_release(e); }, true);
    notebook->bind("<Double-Button-1>", [this](tk::Event& e) { return _tab_double(e); }, true);
    app->bind("<ButtonRelease-1>", [this](tk::Event& e) { return _drag_release(e); }, true);
    app->bind("<Escape>", [this](tk::Event& e) { return _cancel_drag(&e); }, true);
    app->workspace = this;
}

void DockWorkspace::erase_entry(const std::string& key) {
    entries.erase(key); std::erase(order, key);
}

void DockWorkspace::_app_destroyed(tk::Event& event) {
    if (event.widget == app) {
        cleanup();
    }
}

void DockWorkspace::cleanup() {
    _closed = true;
    _release_grab();
    if (_poll_job) {
        try {
            app->after_cancel(*_poll_job);
        } catch (const tk::TclError&) {
        }
        _poll_job.reset();
    }
    if (native) {
        std::vector<std::string> keys; for (auto& [key, _] : native->hooks) keys.push_back(key);
        for (auto& key : keys) native->remove(key);
        native->close();
    }
}

void DockWorkspace::_poll_native() {
    _poll_job.reset();
    if (_closed) {
        return;
    }
    struct Reschedule {
        DockWorkspace* self;
        ~Reschedule() {
            if (!self->_closed && self->app->winfo_exists()) self->_poll_job = self->app->after(45, [s = self] { s->_poll_native(); });
        }
    } finally{this};
    if (native) {
        if (drag && !native->left_button_down()) {
            _release_grab();
        }
        auto pending = std::move(native->pending); native->pending.clear();
        for (auto& [kind, key, point] : pending) {
            if (kind == "destroy") {
                native->remove(key);
                continue;
            }
            auto found = entries.find(key);
            if (found == entries.end() || !found->second.detached) {
                continue;
            }
            try {
                auto mapped = native->notebook_point(notebook, point.first, point.second);
                if (mapped && over_tab_bar(mapped->first, mapped->second)) {
                    // Deferred until Tcl is back in its normal event loop.
                    dock(found->second.page, _drop_index(mapped->first, mapped->second));
                }
            } catch (const tk::TclError&) {
                // A queued native event can outlive its document.
                native->remove(key);
            }
        }
    }
}

bool DockWorkspace::confirm_project_change() {
    std::vector<std::string> changed;
    for (auto& key : order) {
        auto& entry = entries[key];
        if (!entry.allow_close) continue;
        auto* document = dynamic_cast<DockDocument*>(entry.page);
        if (document && document->has_changes()) changed.push_back("• " + entry.title);
    }
    if (changed.empty()) {
        return true;
    }
    return tk::messagebox::askyesno("Niezapisane dokumenty",
        "Otwarte dokumenty mają niezapisane zmiany:\n" + py::join(changed, "\n") +
        "\n\nOdrzucić te zmiany i kontynuować? Aby je zachować, anuluj i zapisz w dokumentach.",
        app);
}

void DockWorkspace::close_project_documents() {
    // Confirmation happens once, before any destructive transition.
    std::vector<tk::Widget*> pages;
    for (auto& key : order) if (entries[key].allow_close) pages.push_back(entries[key].page);
    for (tk::Widget* page : pages) page->destroy();
    _release_grab();
}

tk::Widget* DockWorkspace::add_existing(tk::Widget* page, const std::string& title, bool allow_close) {
    // Register a tk.Frame, including one already displayed in Notebook.
    std::string cls = page->winfo_class();
    if (cls != "Frame" && cls != "Labelframe") {
        throw py::ValueError("Odłączana zakładka wymaga tk.Frame jako ramki głównej.");
    }
    if (page->master() != app) {
        throw py::ValueError("Ramka dokumentu musi mieć aplikację jako bezpośredniego rodzica.");
    }
    std::string key = page->path();
    if (entries.contains(key)) {
        return page;
    }
    Entry entry;
    entry.page = page; entry.title = title; entry.allow_close = allow_close;
    if (allow_close) entry.close = [page] { page->destroy(); };
    else entry.close = [this, page] { dock(page); };
    auto* header = new ttk::Frame(page, {{"padding", "8 3"}});
    auto packed = page->pack_slaves();
    tk::Opts header_pack{{"side", "top"}, {"fill", "x"}};
    if (!packed.empty()) header_pack.push_back({"before", packed[0]});
    header->pack(header_pack);
    tk::StringVar name(page, "⠿  " + title);
    auto* grip = new ttk::Label(header, {{"textvariable", name}, {"cursor", "fleur"}});
    grip->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    auto* toggle = new ttk::Button(header, {{"text", "Odłącz"}, {"width", 10}, {"command", std::function<void()>([this, page] { this->toggle(page); })}});
    toggle->pack({{"side", "right"}, {"padx", 3}});
    if (allow_close) {
        (new ttk::Button(header, {{"text", "×"}, {"width", 3}, {"command", std::function<void()>([this, page] { close(page); })}}))->pack({{"side", "right"}});
    }
    entry.header = header; entry.label = name; entry.toggle = toggle; entry.grip = grip;
    entries[key] = entry; order.push_back(key);
    for (tk::Widget* widget : std::vector<tk::Widget*>{header, grip}) {
        widget->bind("<ButtonPress-1>", [this, page](tk::Event& event) { return _header_press(page, event); });
        widget->bind("<B1-Motion>", [this](tk::Event& event) { return _drag_motion(event); });
        widget->bind("<ButtonRelease-1>", [this](tk::Event& event) { return _drag_release(event); });
    }
    page->bind("<Destroy>", [this, page](tk::Event& event) { _removed(page, event); return std::string(); }, true);
    auto tabs = notebook_tabs(notebook);
    if (std::find(tabs.begin(), tabs.end(), key) == tabs.end()) {
        notebook->add(page, {{"text", title}});
    }
    notebook->select(page);
    app->after_idle([this, page] { _install_document_tag(page); });
    return page;
}

void DockWorkspace::_install_document_tag(tk::Widget* page) {
    if (!page->winfo_exists()) {
        return;
    }
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        for (tk::Widget* child : widget->winfo_children()) {
            auto tags = child->bindtags();
            if (std::find(tags.begin(), tags.end(), page->path()) == tags.end()) {
                std::string root = app->tk::Widget::path();
                auto found = std::find(tags.begin(), tags.end(), root);
                tags.insert(found, page->path());
            }
            if (std::find(tags.begin(), tags.end(), app->tk::Widget::path()) == tags.end()) {
                tags.push_back(app->tk::Widget::path());
            }
            child->bindtags(tags);
            visit(child);
        }
    };
    visit(page);
}

void DockWorkspace::bind_app_shortcut(const std::string& sequence, std::function<void()> command) {
    auto run = [command](tk::Event&) {
        command();
        return std::string("break");
    };
    app->bind(sequence, run);
    for (auto& key : order) {
        if (!entries[key].allow_close) {
            entries[key].page->bind(sequence, run);
        }
    }
}

void DockWorkspace::_removed(tk::Widget* page, tk::Event& event) {
    if (event.widget && event.widget->path() == page->path()) {
        if (drag && drag->page == page) {
            _release_grab();
        }
        if (native) {
            native->remove(page->path());
        }
        erase_entry(page->path());
    }
}

void DockWorkspace::title(tk::Widget* page, const std::string& value) {
    Entry& entry = entries.at(page->path());
    entry.title = value;
    entry.label.set("⠿  " + value);
    if (entry.detached) {
        app->call({"wm", "title", page->path(), value});
    } else {
        auto tabs = notebook_tabs(notebook);
        if (std::find(tabs.begin(), tabs.end(), page->path()) != tabs.end()) notebook->tab(page, {{"text", value}});
    }
}

void DockWorkspace::select(tk::Widget* page) {
    Entry& entry = entries.at(page->path());
    if (entry.detached) {
        app->call({"wm", "deiconify", page->path()});
        page->lift();
    } else {
        notebook->select(page);
    }
}

bool DockWorkspace::detached(tk::Widget* page) {
    return entries.at(page->path()).detached;
}

void DockWorkspace::detach(tk::Widget* page, std::optional<double> x, std::optional<double> y) {
    _release_grab();
    _detach_page(page, x, y);
}

void DockWorkspace::_detach_page(tk::Widget* page, std::optional<double> x, std::optional<double> y) {
    Entry& entry = entries.at(page->path());
    if (entry.detached) {
        select(page);
        return;
    }
    auto tabs = notebook_tabs(notebook);
    auto position = std::find(tabs.begin(), tabs.end(), page->path());
    if (position == tabs.end()) throw py::ValueError("list.index(x): x not in list");
    entry.index = (int)(position - tabs.begin());
    notebook->forget(page);
    app->call({"wm", "manage", page->path()});
    entry.detached = true;
    app->call({"wm", "title", page->path(), entry.title});
    app->call({"wm", "geometry", page->path(), entry.geometry});
    app->call({"wm", "minsize", page->path(), std::to_string(entry.minsize.first), std::to_string(entry.minsize.second)});
    if (entry.wm_callback.empty()) {
        entry.wm_callback = page->register_command([this, page] { close(page); });
    }
    app->call({"wm", "protocol", page->path(), "WM_DELETE_WINDOW", entry.wm_callback});
    entry.toggle->configure({{"text", "Przypnij"}});
    app->call({"wm", "deiconify", page->path()});
    page->update_idletasks();
    if (native) {
        std::optional<std::pair<double, double>> point;
        if (x && y) point = std::make_pair(*x, *y);
        else if (auto c = native->cursor()) point = std::make_pair((double)c->first, (double)c->second);
        if (point) {
            auto work = native->workarea(point->first, point->second);
            if (work) {
                app->call({"wm", "minsize", page->path(),
                           std::to_string(std::min((long)entry.minsize.first, (*work)[2] - (*work)[0] - 20)),
                           std::to_string(std::min((long)entry.minsize.second, (*work)[3] - (*work)[1] - 60))});
            }
            auto current = native->rect(native->hwnd(page));
            if (current) {
                double wx = x && y ? (long)*x - 85 : (double)(*current)[0], wy = x && y ? (long)*y - 12 : (double)(*current)[1];
                native->move(page, wx, wy, true);
            }
        }
        entry.native_title_drag = native->install(page);
    } else {
        _move_window(page, x, y);
    }
    page->event_generate("<<DocumentDetached>>");
}

void DockWorkspace::dock(tk::Widget* page) {
    Entry& entry = entries.at(page->path());
    dock(page, entry.index);  // entry.get('index', 'end')
}

void DockWorkspace::dock(tk::Widget* page, std::optional<int> index) {
    _release_grab();
    Entry& entry = entries.at(page->path());
    if (entry.detached) {
        entry.geometry = app->call({"wm", "geometry", page->path()});
        if (native) {
            native->remove(page->path());
        }
        app->call({"wm", "forget", page->path()});
        entry.detached = false;
        std::optional<int> wanted = index;
        int count = (int)notebook_tabs(notebook).size();
        if (wanted && *wanted >= count) {
            wanted.reset();
        }
        notebook->insert(wanted ? std::to_string(*wanted) : "end", page, {{"text", entry.title}});
        entry.toggle->configure({{"text", "Odłącz"}});
        page->event_generate("<<DocumentDocked>>");
    }
    notebook->select(page);
}

void DockWorkspace::toggle(tk::Widget* page) {
    if (detached(page)) {
        dock(page);
    } else {
        detach(page);
    }
}

void DockWorkspace::close(tk::Widget* page) {
    _release_grab();
    auto found = entries.find(page->path());
    if (found != entries.end()) {
        auto close_entry = found->second.close;
        close_entry();
    }
}

std::array<int, 4> DockWorkspace::_bar_bounds() {
    ttk::Notebook* n = notebook;
    int height = 42;
    std::string selected = n->select();
    if (!selected.empty()) {
        try {
            tk::Widget* active = app->nametowidget(selected);
            if (active->winfo_ismapped()) {
                height = std::max(28, active->winfo_rooty() - n->winfo_rooty());
            }
        } catch (const tk::TclError&) {
        }
    }
    return {n->winfo_rootx(), n->winfo_rooty(), n->winfo_width(), height};
}

bool DockWorkspace::over_tab_bar(double x, double y) {
    if (!notebook->winfo_ismapped()) {
        return false;
    }
    auto [bx, by, bw, bh] = _bar_bounds();
    return bx <= x && x <= bx + bw && by <= y && y <= by + bh;
}

tk::Widget* DockWorkspace::_tab_at(tk::Event& event) {
    if (!over_tab_bar(event.x_root, event.y_root)) {
        return nullptr;
    }
    try {
        auto tabs = notebook_tabs(notebook);
        int index = notebook->index(std::format("@{},{}", event.x, event.y));
        if (index < 0) index += (int)tabs.size();  // indeks ujemny jak w Pythonie
        if (index < 0 || index >= (int)tabs.size()) return nullptr;  // IndexError
        auto found = entries.find(tabs[index]);
        return found != entries.end() ? found->second.page : nullptr;
    } catch (const tk::TclError&) {
        return nullptr;
    }
}

bool DockWorkspace::_start(tk::Widget* page, tk::Event& event) {
    _release_grab();
    // Never steal a modal dialog's grab. A tab click itself needs no grab:
    // reserve it for an actual movement beyond the drag threshold.
    if (app->grab_current() != nullptr) {
        return false;
    }
    drag = std::make_shared<Drag>(Drag{page, event.x_root, event.y_root, event.widget, false});
    return true;
}

void DockWorkspace::_tab_press(tk::Event& event) {
    tk::Widget* page = _tab_at(event);
    if (page) {
        notebook->select(page);
        _start(page, event);
    }
}

std::string DockWorkspace::_tab_double(tk::Event& event) {
    tk::Widget* page = _tab_at(event);
    if (page) {
        _release_grab();
        detach(page);
        return "break";
    }
    return "";
}

std::string DockWorkspace::_header_press(tk::Widget* page, tk::Event& event) {
    if (_start(page, event)) {
        return "break";
    }
    return "";
}

void DockWorkspace::_move_window(tk::Widget* page, std::optional<double> x, std::optional<double> y) {
    if (x && y) {
        if (native) {
            native->move(page, (long)*x - 85, (long)*y - 12);
        } else {
            // Tk minus offsets mean distance from the right/bottom edge.
            app->call({"wm", "geometry", page->path(), std::format("+{}+{}", (long)*x - 85, (long)*y - 12)});
        }
    }
}

std::string DockWorkspace::_drag_motion(tk::Event& event) {
    if (!drag) {
        return "";
    }
    std::shared_ptr<Drag> d = drag;
    if (std::abs(event.x_root - d->x) + std::abs(event.y_root - d->y) < 14 && !d->moved) {
        return "";
    }
    d->moved = true;
    tk::Widget* page = d->page;
    if (!entries.contains(page->path())) {
        _release_grab();
        return "break";
    }
    if (!detached(page) && !over_tab_bar(event.x_root, event.y_root)) {
        // A Notebook grab belongs to the old Toplevel. Release it before
        // wm manage, then capture on the new window's visible drag handle.
        _release_owned_grab();
        _detach_page(page, (double)event.x_root, (double)event.y_root);
        auto found = entries.find(page->path());
        if (found == entries.end()) {
            _release_grab();
            return "break";
        }
        d->widget = found->second.grip;
    }
    if (drag != d) {
        return "break";
    }
    if (!_acquire_drag_grab(d->widget)) {
        _release_grab();
        return "break";
    }
    if (detached(page)) {
        _move_window(page, (double)event.x_root, (double)event.y_root);
    }
    return "break";
}

bool DockWorkspace::_acquire_drag_grab(tk::Widget* widget) {
    if (_drag_grab == widget && app->grab_current() == widget) {
        return true;
    }
    _release_owned_grab();
    if (app->grab_current() != nullptr) {
        return false;
    }
    try {
        // Use Tcl directly: DockDocument.grab_set is deliberately a no-op
        // for non-modal editing documents, while drag capture is temporary.
        app->call({"grab", "set", widget->path()});
        _drag_grab = widget;
        return true;
    } catch (const tk::TclError&) {
        return false;
    }
}

void DockWorkspace::_release_owned_grab() {
    tk::Widget* widget = _drag_grab; _drag_grab = nullptr;
    if (widget != nullptr) {
        try {
            if (app->call({"grab", "current", app->tk::Widget::path()}) == widget->path()) {
                app->call({"grab", "release", widget->path()});
            }
        } catch (const tk::TclError&) {
        }
    }
}

void DockWorkspace::_release_grab() {
    _release_owned_grab();
    drag.reset();
}

std::string DockWorkspace::_cancel_drag(tk::Event*) {
    if (drag) {
        _release_grab();
        return "break";
    }
    return "";
}

std::string DockWorkspace::_drag_release(tk::Event& event) {
    if (!drag) {
        return "";
    }
    std::shared_ptr<Drag> d = drag;
    _release_grab();
    if (!entries.contains(d->page->path())) {
        return "break";
    }
    if (d->moved && over_tab_bar(event.x_root, event.y_root)) {
        // A drop onto a particular tab also changes the document order.
        auto index = _drop_index(event.x_root, event.y_root);
        if (detached(d->page)) {
            dock(d->page, index);
        } else {
            notebook->insert(index ? std::to_string(*index) : "end", d->page, {});
        }
    }
    return "break";
}

std::optional<int> DockWorkspace::_drop_index(double x, double y) {
    auto bounds = _bar_bounds();
    try {
        return notebook->index(std::format("@{},{}", (long)(x - bounds[0]), (long)(y - bounds[1])));
    } catch (const tk::TclError&) {
        return std::nullopt;
    }
}

std::vector<std::string> notebook_tabs(ttk::Notebook* notebook) {
    return notebook->tabs();
}

// ---------------------------------------------------------------- DockDocument

DockDocument::DockDocument(App* app_, const tk::Opts& kwargs) : tk::Frame(app_, kwargs), app(app_) {
    project_generation = app->project_generation;
    DockWorkspace* existing = app->workspace;
    if (existing == nullptr) {
        if (app->tabs == nullptr) {
            app->tabs = new ttk::Notebook(app);
            app->tabs->pack({{"fill", "both"}, {"expand", true}});
        }
        existing = new DockWorkspace(app, app->tabs);
    }
    workspace = existing;
    workspace->add_existing(this, _doc_title, true);
    workspace->entries[path()].close = [this] { request_close(); };
    after_idle([this] { _document_bindtags(); });
    after_idle([this] { if (!_doc_baseline) mark_clean(); });
}

bool DockDocument::ensure_current_project(bool raise_error) {
    bool current = project_generation == app->project_generation;
    if (current) {
        return true;
    }
    std::string text = "Ten dokument pochodzi z poprzedniego projektu. Otwórz edytor ponownie w bieżącym projekcie.";
    if (raise_error) {
        throw py::ValueError(text);
    }
    tk::messagebox::showerror("Nieaktualny dokument", text, app);
    return false;
}

std::function<void()> DockDocument::guard(std::function<void()> callback) {
    // Protect a dynamically-created button/menu commit closure.
    return [this, callback] {
        ensure_current_project(true);
        callback();
    };
}

std::pair<std::map<std::string, std::string>, json> DockDocument::_document_state() {
    // Draft models and typed control values, including uncommitted fields.
    std::map<std::string, std::string> controls;
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        for (tk::Widget* child : widget->winfo_children()) {
            std::string cls = child->winfo_class();
            if (cls == "Entry" || cls == "TEntry" || cls == "TCombobox" || cls == "Spinbox" || cls == "TSpinbox") {
                controls[child->path()] = child->call({child->path(), "get"});
            } else if (cls == "Checkbutton" || cls == "TCheckbutton" || cls == "Radiobutton" || cls == "TRadiobutton") {
                std::string variable = child->cget("variable");
                if (!variable.empty()) {
                    controls[variable] = child->getvar(variable);
                }
            }
            visit(child);
        }
    };
    visit(this);
    return {controls, _document_model()};
}

void DockDocument::mark_clean() {
    if (winfo_exists()) {
        _doc_baseline = _document_state();
    }
}

bool DockDocument::has_changes() {
    return _doc_baseline && _document_state() != *_doc_baseline;
}

bool DockDocument::request_close() {
    if (has_changes() && !tk::messagebox::askyesno("Niezapisany dokument",
            "Odrzucić niezapisane zmiany tego dokumentu?", winfo_toplevel())) {
        return false;
    }
    destroy();
    return true;
}

void DockDocument::_document_bindtags() {
    // Docked child widgets ordinarily dispatch shortcuts to root. Include
    // the document frame first, so its own editing shortcut can consume it.
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        for (tk::Widget* child : widget->winfo_children()) {
            auto tags = child->bindtags();
            if (std::find(tags.begin(), tags.end(), path()) == tags.end()) {
                std::string root_tag = app->tk::Widget::path();
                auto found = std::find(tags.begin(), tags.end(), root_tag);
                tags.insert(found, path());
                child->bindtags(tags);
            }
            visit(child);
        }
    };
    if (winfo_exists()) {
        visit(this);
    }
}

std::string DockDocument::title() const {
    return _doc_title;
}

void DockDocument::title(const std::string& value) {
    _doc_title = value;
    workspace->title(this, value);
}

std::string DockDocument::geometry() const {
    return workspace->entries.at(path()).geometry;
}

void DockDocument::geometry(const std::string& value) {
    auto& entry = workspace->entries.at(path());
    entry.geometry = value;
    if (entry.detached) {
        call({"wm", "geometry", path(), value});
    }
}

std::pair<int, int> DockDocument::minsize() const {
    return workspace->entries.at(path()).minsize;
}

void DockDocument::minsize(int width, int height) {
    auto& entry = workspace->entries.at(path());
    entry.minsize = {width, height};
    if (entry.detached) {
        call({"wm", "minsize", path(), std::to_string(width), std::to_string(height)});
    }
}

void DockDocument::protocol(const std::string& name, std::function<void()> func) {
    if (name == "WM_DELETE_WINDOW" && func) {
        workspace->entries.at(path()).close = func;
    }
}

std::string DockDocument::transient(tk::Widget*) {
    // Editing tabs and detached documents stay non-modal.
    return "";
}

void DockDocument::grab_set() {}

void DockDocument::grab_release() {}

std::string DockDocument::after(int ms, std::function<void()> func) {
    auto job = std::make_shared<std::string>();
    *job = tk::Frame::after(ms, [this, job, func] {
        _doc_after_jobs.erase(*job);
        func();
    });
    _doc_after_jobs.insert(*job);
    return *job;
}

void DockDocument::after_cancel(const std::string& job) {
    _doc_after_jobs.erase(job);
    tk::Frame::after_cancel(job);
}

void DockDocument::destroy() {
    // Tk deletes registered commands on destruction but leaves pending
    // timer scripts in its interpreter unless explicitly cancelled.
    for (auto job : std::vector<std::string>(_doc_after_jobs.begin(), _doc_after_jobs.end())) {
        try {
            after_cancel(job);
        } catch (const tk::TclError&) {
        }
    }
    tk::Frame::destroy();
}

// ---------------------------------------------------------------- VisualizationDocument

VisualizationDocument::VisualizationDocument(App* app_, core::ProjectPtr project_, std::optional<std::string> selected_, const std::string& title_, const std::string& note)
    : DockDocument(app_), explode(this, false), dimensions(this, true), xray(this, false) {
    title(title_);
    geometry("1100x760+60+60");
    snapshot_project = project_; selected = selected_;
    camera = core::Camera();
    need_fit = true;
    auto* toolbar = new ttk::Frame(this, {{"padding", 6}});
    toolbar->pack({{"fill", "x"}});
    for (auto& [label, angles] : std::vector<std::pair<std::string, std::pair<double, double>>>{{"3D", {-38, 24}}, {"Przód", {0, 0}}, {"Bok", {-90, 0}}, {"Góra", {0, 90}}}) {
        auto a = angles;
        (new ttk::Button(toolbar, {{"text", label}, {"command", std::function<void()>([this, a] { view(a.first, a.second); })}}))->pack({{"side", "left"}, {"padx", 3}});
    }
    (new ttk::Button(toolbar, {{"text", "Dopasuj"}, {"command", std::function<void()>([this] { fit(); })}}))->pack({{"side", "left"}, {"padx", 3}});
    (new ttk::Checkbutton(toolbar, {{"text", "Rozstrzelony"}, {"variable", explode}, {"command", std::function<void()>([this] { draw(); })}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Checkbutton(toolbar, {{"text", "Wymiary"}, {"variable", dimensions}, {"command", std::function<void()>([this] { draw(); })}}))->pack({{"side", "left"}});
    (new ttk::Checkbutton(toolbar, {{"text", "X-ray"}, {"variable", xray}, {"command", std::function<void()>([this] { draw(); })}}))->pack({{"side", "left"}});
    if (!note.empty()) {
        (new ttk::Label(this, {{"text", note}, {"padding", 8}, {"wraplength", 1100}}))->pack({{"fill", "x"}});
    }
    canvas = new quick_studio_ui::StudioCanvas(this, {{"background", "#eaf0f4"}, {"highlightthickness", 0}});
    canvas->pack({{"fill", "both"}, {"expand", true}});
    auto handler = [](auto fn) { return [fn](tk::Event& e) { fn(e); return std::string(); }; };
    canvas->bind("<Configure>", handler([this](tk::Event&) { draw(); }));
    canvas->bind("<ButtonPress-1>", handler([this](tk::Event& e) { _press(e); }));
    canvas->bind("<B1-Motion>", handler([this](tk::Event& e) { _orbit(e); }));
    canvas->bind("<ButtonPress-3>", handler([this](tk::Event& e) { _press(e); }));
    canvas->bind("<B3-Motion>", handler([this](tk::Event& e) { _pan(e); }));
    canvas->bind("<MouseWheel>", handler([this](tk::Event& e) { _zoom(e); }));
    canvas->bind("<ButtonPress-2>", handler([this](tk::Event& e) { _press(e); }));
    canvas->bind("<B2-Motion>", handler([this](tk::Event& e) { _orbit(e); }));
    canvas->bind("<ButtonRelease-1>", handler([this](tk::Event& e) { _finish_view(&e); }));
    canvas->bind("<ButtonRelease-2>", handler([this](tk::Event& e) { _finish_view(&e); }));
    canvas->bind("<ButtonRelease-3>", handler([this](tk::Event& e) { _finish_view(&e); }));
    _interactive = false;
    (new ttk::Label(this, {{"text", "LPM: obrót · PPM: przesuwanie · Rolka: przybliżenie. Kamera tej zakładki jest niezależna."}, {"padding", 7}}))->pack({{"fill", "x"}});
    _refresh_job = after(400, [this] { _live_refresh(); });
}

void VisualizationDocument::viewport_preferences_changed() {
    // All views share the engine preference, retaining independent cameras.
    if (winfo_exists()) {
        native_viewport::reset_native_failure(canvas); draw();
    }
}

core::ProjectPtr VisualizationDocument::project() {
    if (snapshot_project != nullptr) {
        return snapshot_project;
    }
    return app->display_project();
}

bool VisualizationDocument::has_changes() {
    // Camera/visibility settings are presentation state, not a model draft.
    return false;
}

void VisualizationDocument::view(double yaw, double pitch) {
    camera.yaw = yaw; camera.pitch = pitch;
    fit();
}

void VisualizationDocument::fit() {
    need_fit = true;
    draw();
}

void VisualizationDocument::_live_refresh() {
    if (!winfo_exists()) {
        return;
    }
    if (winfo_ismapped()) {
        json amounts = json::array(); for (auto& [k, v] : app->open_amounts) amounts.push_back(json::array({k, v}));
        std::string key = py::dumps(json::array({std::format("{}", (const void*)project().get()), app->viewport_revision, amounts, app->viewport_engine, app->viewport_quality}));
        if (key != _live_key) { _live_key = key; draw(); }
    }
    _refresh_job = after(400, [this] { _live_refresh(); });
}

void VisualizationDocument::_press(tk::Event& event) {
    anchor = {event.x, event.y};
}

void VisualizationDocument::_orbit(tk::Event& event) {
    _interactive = true;
    camera.yaw += (event.x - anchor.first) * .5;
    camera.pitch = std::max(-89.0, std::min(89.0, camera.pitch + (event.y - anchor.second) * .4));
    anchor = {event.x, event.y};
    draw();
}

void VisualizationDocument::_pan(tk::Event& event) {
    _interactive = true;
    camera.pan = {camera.pan[0] + event.x - anchor.first, camera.pan[1] + event.y - anchor.second};
    anchor = {event.x, event.y};
    draw();
}

void VisualizationDocument::_zoom(tk::Event& event) {
    camera.scale = std::min(40.0, std::max(.01, camera.scale * (event.delta > 0 ? 1.12 : 1 / 1.12)));
    draw();
}

void VisualizationDocument::_finish_view(tk::Event*) {
    _interactive = false; draw();
}

void VisualizationDocument::draw(bool force_cpu) {
    if (canvas == nullptr) {
        return;
    }
    auto* c = canvas;
    c->delete_("all");
    int w = c->winfo_width(), h = c->winfo_height();
    if (w < 10 || h < 10) {
        return;
    }
    core::ProjectPtr project_ = project();
    if (need_fit) {
        camera.fit(*project_, w, h);
        need_fit = false;
    }
    std::optional<std::string> selected_ = snapshot_project != nullptr ? selected : app->selected;
    double factor = explode.get() ? .22 : 0;
    bool interactive = _interactive;
    viewport_scene::paint_background(c, camera, project_, {w, h}, interactive);
    std::string engine = force_cpu ? "cpu" : app->viewport_engine;
    std::string quality = app->viewport_quality;
    auto xray_ids = viewport_modes::xray_part_ids(project_, xray.get(), app->xray_scope.get(), selected_);
    std::optional<native_viewport::ViewportResult> result;
    if (engine == "auto" || engine == "d3d11" || engine == "d3d12" || engine == "vulkan") {
        result = native_viewport::paint_native(c, {.project = project_, .backend = engine, .camera = camera, .size = {w, h}, .interactive = interactive,
            .selected_ids = {}, .selected = selected_, .explode = factor, .xray = xray.get(), .xray_ids = xray_ids, .quality = quality});
        if (result->ok) {
            gpu_viewport::close_gpu(c);
        }
    } else native_viewport::close_native(c);
    if ((engine == "auto" || engine == "gpu") && !(result && result->ok)) {
        auto failed = [&](const std::exception& error) { result = native_viewport::ViewportResult{}; result->error = error.what(); };
        try {
            result = gpu_viewport::paint_gpu(c, {.project = project_, .camera = camera, .size = {w, h}, .interactive = interactive,
                .selected_ids = {}, .selected = selected_, .explode = factor, .xray = xray.get(), .xray_ids = xray_ids, .quality = quality});
        } catch (const std::runtime_error& error) { failed(error); }  // ImportError, RuntimeError, OSError
        catch (const py::ValueError& error) { failed(error); }
    }
    core::Drawn rendered;
    if (result && result->ok) {
        rendered = result->drawn;
        if (result->engine != std::optional<std::string>("native")) c->_active_renderer = "gpu";
    } else {
        gpu_viewport::close_gpu(c); native_viewport::close_native(c); c->_active_renderer = "cpu";
        rendered = core::render_faces(*project_, camera, w, h, selected_, factor, interactive);
        viewport_scene::paint_faces(c, rendered, project_, {.selected_ids = {}, .camera = camera, .size = {w, h}, .xray = xray.get(), .explode = factor, .interactive = interactive, .xray_ids = xray_ids, .stats = viewport_render::last_stats()});
        std::optional<std::string> error = result ? result->error : std::nullopt;
        if (error && !error->empty() && error != _viewport_warning) {
            _viewport_warning = error;
            if (app->log) app->log->warning(std::format("Wizualizacja 3D: {}; podgląd CPU.", *error));
        }
    }
    drawn = rendered;
    if (dimensions.get()) {
        for (auto& part : project_->parts) {
            if (!(part.visible && (explode.get() || (selected_ && part.id == *selected_)))) continue;
            Point3 shift = core::explosion_shift(part, camera, factor);
            auto position = [&](const Point3& point) {
                Point3 xyz = core::world(part, point[0], point[1], point[2]);
                return camera.project({xyz[0] + shift[0], xyz[1] + shift[1], xyz[2] + shift[2]}, w, h);
            };
            if (explode.get()) {
                auto [x, y, _] = position({part.u / 2, part.v / 2, part.t});
                c->create_text(x, y - 12, {{"text", std::format("{}\n{:g} × {:g} × {:g} mm", part.name, part.u, part.v, part.t)}, {"fill", "#123c49"}, {"font", "{Segoe UI} 9 bold"}, {"tags", "dimensions"}});
            } else {
                for (auto& [a, b, text] : std::vector<std::tuple<Point3, Point3, std::string>>{{{0, 0, 0}, {part.u, 0, 0}, std::format("U {:g}", part.u)}, {{part.u, 0, 0}, {part.u, part.v, 0}, std::format("V {:g}", part.v)}, {{part.u, part.v, 0}, {part.u, part.v, part.t}, std::format("T {:g}", part.t)}}) {
                    Point3 qa = position(a), qb = position(b);
                    c->create_line(std::vector<double>{qa[0], qa[1], qb[0], qb[1]}, {{"fill", "#176a78"}, {"width", 2}, {"tags", "dimensions"}});
                    c->create_text((qa[0] + qb[0]) / 2, (qa[1] + qb[1]) / 2 - 12, {{"text", text + " mm"}, {"fill", "#123c49"}, {"font", "{Segoe UI} 9 bold"}, {"tags", "dimensions"}});
                }
            }
        }
    }
    if (c->_active_renderer == "native") {
        auto failed = [&](const std::exception& error) {
            if (app->log) app->log->warning(std::format("Nakładka wizualizacji C++: {}; podgląd CPU.", error.what()));
            draw(true);
        };
        try { native_viewport::flush_native(c); }
        catch (const std::runtime_error& error) { failed(error); }  // RuntimeError, OSError, TclError
        catch (const py::ValueError& error) { failed(error); }
    }
}

void VisualizationDocument::destroy() {
    if (canvas != nullptr) {
        native_viewport::close_native(canvas); gpu_viewport::close_gpu(canvas);
    }
    if (_refresh_job) {
        try {
            after_cancel(*_refresh_job);
        } catch (const tk::TclError&) {
        }
    }
    DockDocument::destroy();
}

}  // namespace dock_workspace
