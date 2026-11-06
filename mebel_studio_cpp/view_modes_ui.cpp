#include "view_modes_ui.h"

#include <string>

#include "app.h"
#include "quick_studio_ui.h"
#include "side_panels.h"
#include "tk.h"
#include "ttk.h"
#include "viewport_modes.h"

namespace view_modes_ui {

// Strona "Widok" w lewym panelu: zasięg X-ray, tryb otworów i przycisk panelu "Silnik i jakość 3D".
void install(App* app) {
    auto found = app->ribbon_pages.find("Widok");
    if (found == app->ribbon_pages.end() || found->second == nullptr) return;
    tk::Widget* page = found->second;
    (new ttk::Label(page, {{"text", "X-ray obejmuje:"}, {"style", "Panel.TLabel"}}))->pack({{"side", "left"}, {"padx", 4}});
    auto* combo = new ttk::Combobox(page, {{"textvariable", app->xray_scope}, {"values", viewport_modes::XRAY_MODES}, {"state", "readonly"}});
    combo->pack({{"side", "left"}, {"padx", 4}});
    combo->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    (new ttk::Label(page, {{"text", "Otwory:"}, {"style", "Panel.TLabel"}}))->pack({{"side", "left"}, {"padx", 4}});
    auto* mode = new ttk::Combobox(page, {{"textvariable", app->drill_mode}, {"values", viewport_modes::DRILL_MODES}, {"state", "readonly"}});
    mode->pack({{"side", "left"}, {"padx", 4}});
    mode->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    side_panels::register_row("engine", quick_studio_ui::_button(page, "Silnik i jakość 3D…", [app] { side_panels::show_engine(app); }));
    app->xray_scope_combo = combo;
    app->drill_mode_combo = mode;
}

}  // namespace view_modes_ui
