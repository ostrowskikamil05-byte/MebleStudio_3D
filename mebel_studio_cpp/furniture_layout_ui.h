// Whole-furniture arrangement controls and screen-plane dragging.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "form_dialog.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace furniture_layout_ui {

// SIDES = {'Po prawej (+X)': 'x+', ...}, ALIGN = {'Początek': 'start', ...} (kolejność jak w Pythonie)
extern const std::vector<std::pair<std::string, std::string>> SIDES;
extern const std::vector<std::pair<std::string, std::string>> ALIGN;

// app.layout_drag = dict(base=..., group=..., start=(x, y), candidate=None, last=0)
struct LayoutDrag {
    core::ProjectPtr base;
    std::string group;
    std::pair<int, int> start{0, 0};
    core::ProjectPtr candidate;  // None == nullptr
    double last = 0;
};

// Invert an orthographic screen delta on XY or Z, in world millimetres.
std::array<double, 3> screen_move(const core::Camera& camera, double dx, double dy, bool vertical = false);
std::string selected_group(App* app);
void commit(App* app, core::ProjectPtr result, const std::string& note = "Zmieniono układ kompletu");
// guarded(app, fn): ValueError/KeyError/StopIteration/OSError -> okno błędu.
std::function<void()> guarded(App* app, std::function<void()> fn);
std::function<std::string(tk::Event&)> guarded(App* app, std::function<void(tk::Event&)> fn);

class LayoutDocument : public form_dialog::FormDocument {
public:
    ttk::Combobox* groups = nullptr;
    tk::StringVar info;
    std::vector<std::pair<std::string, tk::StringVar>> vars;
    ttk::Combobox* target = nullptr;
    tk::StringVar side, align, gap, contact;

    explicit LayoutDocument(App* app);
    std::string current();
    void read();
    void apply();
    void turn(double angle);
    void join();
    void show_design();
};

void begin_drag(App* app, tk::Event& event);
void drag(App* app, tk::Event& event, bool force = false);
bool end_drag(App* app, tk::Event& event);
void cancel_drag(App* app);
void print_project(App* app);
void install(App* app);

}  // namespace furniture_layout_ui
