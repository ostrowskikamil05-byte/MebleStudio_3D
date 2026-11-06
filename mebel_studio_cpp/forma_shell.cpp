#include "forma_shell.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "app.h"
#include "ui_settings.h"

namespace forma_shell {

namespace {

using Command = std::function<void()>;

void add_command(tk::Menu* menu, const std::string& label, Command command) {
    menu->add_command({{"label", label}, {"command", std::move(command)}});
}

}  // namespace

void finish(App* app) {
    if (app->header_menus == nullptr) return;
    // Istniejące podmenu z dawnego paska menu: etykieta -> ścieżka.
    std::vector<std::pair<std::string, std::string>> cascades;
    tk::Menu* bar = nullptr;
    try {
        bar = dynamic_cast<tk::Menu*>(app->nametowidget(app->cget("menu")));
    } catch (const tk::TclError&) {
    }
    if (bar != nullptr)
        for (const auto& entry : bar->entries())
            if (entry.type == "cascade") cascades.emplace_back(entry.label, entry.menu);
    std::set<std::string> used;
    auto add_cascades = [&](tk::Menu* menu, const std::vector<std::string>& labels) {
        for (const auto& wanted : labels)
            for (const auto& [label, path] : cascades)
                if (label == wanted && used.insert(label).second) menu->add_cascade({{"label", label}, {"menu", path}});
    };

    auto* project = new tk::Menu(app, {{"tearoff", false}});
    add_command(project, "Nowy projekt", [app] { app->new_(); });
    add_command(project, "Otwórz…", [app] { app->open(); });
    add_command(project, "Zapisz", [app] { app->save(); });
    project->add_separator();
    add_command(project, "Eksportuj…", [app] { app->export_(); });
    project->add_separator();
    add_cascades(project, {"Układ i wydruk", "Konstrukcja i biblioteka", "Kreator"});

    auto* edit = new tk::Menu(app, {{"tearoff", false}});
    add_command(edit, "Cofnij", [app] { app->undo(); });
    add_command(edit, "Ponów", [app] { app->redo(); });
    edit->add_separator();
    add_command(edit, "Duplikuj", [app] { app->duplicate(); });
    add_command(edit, "Usuń", [app] { app->delete_(); });
    edit->add_separator();
    add_cascades(edit, {"CAD i obróbki", "Kształty i style", "Okucia i wyposażenie"});

    auto* view = new tk::Menu(app, {{"tearoff", false}});
    add_command(view, "Dopasuj widok", [app] { app->fit(); });
    for (const auto& [label, yaw, pitch] : std::vector<std::tuple<std::string, double, double>>{{"Perspektywa 3D", -38, 24}, {"Przód", 0, 0}, {"Bok", -90, 0}, {"Góra", 0, 90}, {"Dół", 0, -90}})
        add_command(view, label, [app, yaw = yaw, pitch = pitch] { app->view(yaw, pitch); });
    view->add_separator();
    add_cascades(view, {"Widoczność", "Scena i diagnostyka"});
    view->add_separator();
    add_command(view, "Ustawienia interfejsu i 3D…", [app] { ui_settings::open_settings(app); });

    // Podmenu spoza listy trafiają do menu Projekt, żeby żadne polecenie nie zniknęło.
    for (const auto& [label, path] : cascades)
        if (used.insert(label).second) project->add_cascade({{"label", label}, {"menu", path}});

    for (auto& [title, menu] : std::vector<std::pair<std::string, tk::Menu*>>{{"Projekt", project}, {"Edycja", edit}, {"Widok", view}}) {
        auto* button = new ttk::Button(app->header_menus, {{"text", title + " ▾"}, {"style", "Tool.TButton"}});
        button->configure({{"command", Command([button, menu] { menu->tk_popup(button->winfo_rootx(), button->winfo_rooty() + button->winfo_height()); })}});
        button->pack({{"side", "left"}, {"padx", 1}});
    }
    // Systemowy pasek menu nie jest już potrzebny (jego podmenu żyją dalej w nowych menu).
    app->configure({{"menu", std::string()}});

    // Skróty widoku i narzędzia są teraz w nakładce na widoku 3D (viewport_hud); stare paski nad widokiem chowamy.
    if (app->ui_layout != nullptr) app->ui_layout->show_toolbars(false, false);

    // Nazwa projektu w pasku górnym.
    auto tick = std::make_shared<Command>();
    *tick = [app, tick] {
        try {
            if (app->project) app->header_title.set(app->project->name + (app->dirty ? " •" : ""));
            app->after(500, *tick);
        } catch (const tk::TclError&) {
        }
    };
    (*tick)();
}

}  // namespace forma_shell
