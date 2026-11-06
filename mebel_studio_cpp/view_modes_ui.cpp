#include "view_modes_ui.h"

#include <string>

#include "app.h"
#include "quick_studio_ui.h"
#include "tk.h"
#include "ttk.h"
#include "ui_settings.h"
#include "viewport_modes.h"

namespace view_modes_ui {

void install(App* app) {
    auto found = app->ribbon_pages.find("Widok");
    if (found == app->ribbon_pages.end() || found->second == nullptr) return;
    tk::Widget* page = found->second;
    (new ttk::Label(page, {{"text", "X-ray obejmuje:"}}))->pack({{"side", "left"}, {"padx", "3 0"}});
    auto* combo = new ttk::Combobox(page, {{"textvariable", app->xray_scope}, {"values", viewport_modes::XRAY_MODES},
                                          {"state", "readonly"}, {"width", 17}});
    combo->pack({{"side", "left"}, {"padx", 3}});
    combo->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    (new ttk::Label(page, {{"text", "Otwory:"}}))->pack({{"side", "left"}, {"padx", "3 0"}});
    auto* mode = new ttk::Combobox(page, {{"textvariable", app->drill_mode}, {"values", viewport_modes::DRILL_MODES},
                                         {"state", "readonly"}, {"width", 21}});
    mode->pack({{"side", "left"}, {"padx", 3}});
    mode->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    (new ttk::Label(page, {{"text", "● widoczne  ◌ zasłonięte"}, {"foreground", "#c14160"}}))->pack({{"side", "left"}, {"padx", 4}});
    quick_studio_ui::_group(page, "SILNIK 3D");
    quick_studio_ui::_button(page, "Silnik i jakość 3D…", [app] { ui_settings::open_settings(app); });
    app->xray_scope_combo = combo;
    app->drill_mode_combo = mode;
}

}  // namespace view_modes_ui
