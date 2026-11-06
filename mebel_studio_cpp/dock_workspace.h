// Tear-off Tk documents. The original frame and its state survive every move.
//
// Tk's ``wm manage``/``wm forget`` support classic Frame widgets. Documents are
// children of the application, so the same frame can also be managed by Notebook.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core.h"
#include "tk.h"
#include "ttk.h"

class App;
namespace quick_studio_ui { class StudioCanvas; }

namespace dock_workspace {

using json = core::json;
using Rect = std::array<long, 4>;  // left, top, right, bottom

// Out-of-context observation of native title moves; no Tcl in callbacks.
//
// WinEvents avoid subclassing Tk's window procedure, whose callbacks require
// _tkinter's own thread state. Geometry changes therefore go through Tcl wm,
// including the absolute "+-120" syntax on left-hand monitors.
class WindowsDockBridge {
public:
    struct Hook { void* hwnd = nullptr; std::optional<Rect> start; std::optional<unsigned> start_dpi; };
    std::map<std::string, Hook> hooks;
    std::vector<std::tuple<std::string, std::string, std::pair<long, long>>> pending;
    void* event_hook = nullptr;

    WindowsDockBridge();
    ~WindowsDockBridge();
    void* hwnd(tk::Widget* page);
    std::optional<Rect> rect(void* hwnd);
    unsigned dpi(void* hwnd);
    std::optional<std::pair<long, long>> cursor();
    bool left_button_down();
    std::optional<Rect> workarea(double x, double y);
    void move(tk::Widget* page, double x, double y, bool fit = false);
    bool install(tk::Widget* page);
    void record_event(unsigned long event, void* hwnd);
    void remove(const std::string& key);
    void close();
    std::optional<std::pair<double, double>> notebook_point(ttk::Notebook* notebook, double x, double y);
};

class DockDocument;

class DockWorkspace {
public:
    struct Entry {
        tk::Widget* page = nullptr; std::string title; bool detached = false;
        std::string geometry = "1100x700+60+60"; std::pair<int, int> minsize{640, 420};
        std::function<void()> close; bool allow_close = false;
        ttk::Frame* header = nullptr; tk::StringVar label; ttk::Button* toggle = nullptr; ttk::Label* grip = nullptr;
        std::optional<int> index; std::string wm_callback; bool native_title_drag = false;
    };
    struct Drag { tk::Widget* page; int x, y; tk::Widget* widget; bool moved; };

    App* app; ttk::Notebook* notebook;
    std::map<std::string, Entry> entries; std::vector<std::string> order;  // dict z kolejnością wstawiania
    std::shared_ptr<Drag> drag;
    tk::Widget* _drag_grab = nullptr;
    bool _closed = false;
    std::unique_ptr<WindowsDockBridge> native;
    std::optional<std::string> _poll_job;

    DockWorkspace(App* app, ttk::Notebook* notebook);
    void _app_destroyed(tk::Event& event);
    void cleanup();
    void _poll_native();
    bool confirm_project_change();
    void close_project_documents();
    // Register a tk.Frame, including one already displayed in Notebook.
    tk::Widget* add_existing(tk::Widget* page, const std::string& title, bool allow_close = false);
    void _install_document_tag(tk::Widget* page);
    void bind_app_shortcut(const std::string& sequence, std::function<void()> command);
    void _removed(tk::Widget* page, tk::Event& event);
    void title(tk::Widget* page, const std::string& value);
    void select(tk::Widget* page);
    bool detached(tk::Widget* page);
    void detach(tk::Widget* page, std::optional<double> x = std::nullopt, std::optional<double> y = std::nullopt);
    void _detach_page(tk::Widget* page, std::optional<double> x = std::nullopt, std::optional<double> y = std::nullopt);
    void dock(tk::Widget* page);                                  // index=None
    void dock(tk::Widget* page, std::optional<int> index);        // nullopt == 'end'
    void toggle(tk::Widget* page);
    void close(tk::Widget* page);
    std::array<int, 4> _bar_bounds();
    bool over_tab_bar(double x, double y);
    tk::Widget* _tab_at(tk::Event& event);
    bool _start(tk::Widget* page, tk::Event& event);
    void _tab_press(tk::Event& event);
    std::string _tab_double(tk::Event& event);
    std::string _header_press(tk::Widget* page, tk::Event& event);
    void _move_window(tk::Widget* page, std::optional<double> x, std::optional<double> y);
    std::string _drag_motion(tk::Event& event);
    bool _acquire_drag_grab(tk::Widget* widget);
    void _release_owned_grab();
    void _release_grab();
    std::string _cancel_drag(tk::Event* event = nullptr);
    std::string _drag_release(tk::Event& event);
    std::optional<int> _drop_index(double x, double y);

private:
    void erase_entry(const std::string& key);
};

std::vector<std::string> notebook_tabs(ttk::Notebook* notebook);

// Toplevel-compatible editing document, initially displayed as a tab.
//
// W Pythonie __init_subclass__ owija metody save()/apply() podklas sprawdzeniem
// ensure_current_project(). W C++ podklasy wywołują je jawnie na początku tych metod.
class DockDocument : public tk::Frame {
public:
    App* app;
    int project_generation;
    std::set<std::string> _doc_after_jobs;
    std::string _doc_title = "Dokument";
    std::optional<std::pair<std::map<std::string, std::string>, json>> _doc_baseline;
    DockWorkspace* workspace;

    DockDocument(App* app, const tk::Opts& kwargs = {});
    bool ensure_current_project(bool raise_error = false);
    // Protect a dynamically-created button/menu commit closure.
    std::function<void()> guard(std::function<void()> callback);
    // Draft models and typed control values, including uncommitted fields.
    std::pair<std::map<std::string, std::string>, json> _document_state();
    // self.__dict__ dla 'part', 'candidate', 'values', 'config' — dostarczają podklasy
    virtual json _document_model() const { return json::object(); }
    void mark_clean();
    virtual bool has_changes();
    bool request_close();
    void _document_bindtags();
    std::string title() const;
    void title(const std::string& value);
    std::string geometry() const;
    void geometry(const std::string& value);
    std::pair<int, int> minsize() const;
    void minsize(int width, int height);
    void protocol(const std::string& name, std::function<void()> func);
    // Editing tabs and detached documents stay non-modal.
    std::string transient(tk::Widget* master = nullptr);
    void grab_set();
    void grab_release();
    std::string after(int ms, std::function<void()> func) override;
    void after_cancel(const std::string& job) override;
    void destroy() override;
};

// Independent camera; live project or a fixed sketch assembly snapshot.
class VisualizationDocument : public DockDocument {
public:
    core::ProjectPtr snapshot_project; std::optional<std::string> selected;
    core::Camera camera;
    tk::BooleanVar explode, dimensions, xray;
    bool need_fit = true;
    quick_studio_ui::StudioCanvas* canvas = nullptr;
    std::pair<int, int> anchor{0, 0};
    bool _interactive = false;
    std::optional<std::string> _refresh_job, _live_key, _viewport_warning;
    core::Drawn drawn;

    VisualizationDocument(App* app, core::ProjectPtr project = nullptr, std::optional<std::string> selected = std::nullopt,
                          const std::string& title = "Wizualizacja 3D", const std::string& note = "");
    // All views share the engine preference, retaining independent cameras.
    void viewport_preferences_changed();
    core::ProjectPtr project();
    // Camera/visibility settings are presentation state, not a model draft.
    bool has_changes() override;
    void view(double yaw, double pitch);
    void fit();
    void _live_refresh();
    void _press(tk::Event& event);
    void _orbit(tk::Event& event);
    void _pan(tk::Event& event);
    void _zoom(tk::Event& event);
    void _finish_view(tk::Event* event = nullptr);
    void draw(bool force_cpu = false);
    void destroy() override;
};

}  // namespace dock_workspace
