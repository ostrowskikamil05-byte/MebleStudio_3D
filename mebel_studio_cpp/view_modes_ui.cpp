#include "view_modes_ui.h"

#include <string>
#include <vector>

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
    // Panel boczny układa każdy bezpośredni element w osobnym wierszu, więc etykietę i listę pakujemy razem w jeden wiersz.
    auto labelled_combo = [page](const char* label, tk::StringVar& variable, const std::vector<std::string>& values) {
        auto* row = new ttk::Frame(page, {{"style", "Panel.TFrame"}});
        (new ttk::Label(row, {{"text", label}, {"style", "Panel.TLabel"}, {"width", 15}}))->pack({{"side", "left"}, {"padx", "4 6"}});
        auto* combo = new ttk::Combobox(row, {{"textvariable", variable}, {"values", values}, {"state", "readonly"}});
        combo->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", "0 4"}});
        return combo;
    };
    auto* combo = labelled_combo("X-ray obejmuje:", app->xray_scope, viewport_modes::XRAY_MODES);
    combo->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    auto* mode = labelled_combo("Otwory:", app->drill_mode, viewport_modes::DRILL_MODES);
    mode->bind("<<ComboboxSelected>>", [app](tk::Event&) { app->draw(); return std::string(); });
    side_panels::register_row("engine", quick_studio_ui::_button(page, "Silnik i jakość 3D…", [app] { side_panels::show_engine(app); }));
    app->xray_scope_combo = combo;
    app->drill_mode_combo = mode;
}

}  // namespace view_modes_ui
