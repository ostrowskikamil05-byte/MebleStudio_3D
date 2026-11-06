// Responsive toolbar layout and user controlled panes, independent of projects.
#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core.h"
#include "tk.h"
#include "ttk.h"
#include "ui_settings.h"

class App;

namespace responsive_ui {

using json = core::json;

// Return (x,y,width,height) for every item; keep every action reachable.
std::pair<std::vector<std::array<int, 4>>, int> flow_layout(double width, const std::vector<std::pair<double, double>>& sizes, int gap = 4);

// canvas.configure(scrollregion=canvas.bbox('all')) — wspólny fragment wielu paneli przewijanych.
void update_scrollregion(tk::Canvas* canvas);

// Adopts packed children, including later plugin controls, without grid conflicts.
class WrapToolbar : public ttk::Frame {
public:
    explicit WrapToolbar(tk::Widget* parent, const tk::Opts& options = {});
    void destroyed(tk::Event& event);
    void schedule();
    void reflow();
    bool vertical = false;  // każdy element w osobnym wierszu na pełną szerokość (lista w panelu bocznym)

private:
    std::optional<std::string> _job;
    // (szerokość, ((nazwa widgetu, (w, h)), ...))
    std::optional<std::pair<int, std::vector<std::tuple<std::string, int, int>>>> _layout_key;
};

class RibbonPage : public ttk::Frame {
public:
    explicit RibbonPage(tk::Widget* parent);
    tk::Canvas* canvas = nullptr;
    WrapToolbar* body = nullptr;
    int window = 0;
};

// A label and editor stack when the inspector is narrower than their content.
class FieldRow : public ttk::Frame {
public:
    FieldRow(tk::Widget* parent, const std::string& label, const tk::Opts& entry_options = {});
    ttk::Label* label = nullptr;
    ttk::Entry* entry = nullptr;
    void initial_arrange();
    void destroyed(tk::Event& event);
    void arrange();

private:
    std::optional<std::string> _idle;
    std::optional<bool> _stacked;
};

// Read current monitor work area and DPI; never change Windows settings.
// Zwraca {workarea:[l,t,r,b], dpi, frame:[w,h]} (frame tylko przy odczycie z Windows).
json monitor_info(tk::Widget* window = nullptr, const std::optional<std::pair<long long, long long>>& point = std::nullopt);

// Zwraca (w, h, x, y).
std::array<long long, 4> fitted_geometry(const json& preferences, const std::array<long long, 4>& workarea, const std::array<long long, 2>& frame = {16, 40});

long long clamp_inspector(double width, long long total, double scale = 1);

class UiLayout {
public:
    UiLayout(App* app, const json& preferences = nullptr, const std::optional<std::filesystem::path>& path = std::nullopt);
    void schedule();
    void reflow();
    void select_panel();
    void sync_panel();
    void sash_released();
    void window_changed(tk::Event& event);
    void schedule_save();
    json preferences();
    bool persist();
    void close(bool persist = true);
    void apply(const json& preferences, bool geometry = true);
    void set_scale(double scale);
    void show_inspector(bool visible, bool save = true);
    void show_ribbon(bool visible, bool save = true);
    void show_toolbars(bool visible, bool save = true);
    void fit_view();
    void restore_layout();

    App* app;
    std::optional<std::filesystem::path> path;
    json prefs;
    std::optional<std::string> job, save_job;
    bool suspended = false;
    std::map<std::string, tk::font::Actual> base_fonts;
    std::map<std::string, std::unique_ptr<tk::font::Font>> font_refs;
    ttk::Combobox* panel_selector = nullptr;
    std::unique_ptr<tk::StringVar> panel_var;
};
using UILayout = UiLayout;

using SettingsDialog = ui_settings::SettingsDialog;
// app.interface_settings(): otwiera okno ustawień interfejsu.
SettingsDialog* interface_settings(App* app);

UiLayout* install(App* app, const json& preferences = nullptr, const std::optional<std::filesystem::path>& path = std::nullopt);

}  // namespace responsive_ui
