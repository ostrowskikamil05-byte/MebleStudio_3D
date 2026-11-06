#include "advanced_ui.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app.h"
#include "cad_geometry.h"
#include "core.h"
#include "edge_editor.h"
#include "machining_editor.h"
#include "productivity.h"
#include "pyutil.h"
#include "tk.h"
#include "ttk.h"

namespace advanced_ui {

using core::json;
using Cmd = std::function<void()>;
using Values = std::vector<std::string>;

namespace {

// float(x.replace(',', '.'))
double comma_float(const std::string& text) { return py::to_float(py::replace_all(text, ",", ".")); }

// tuple(float(x.replace(',', '.')) for x in v[1:])
json offsets(const Values& v) {
    json result = json::array();
    for (size_t i = 1; i < v.size(); ++i) result.push_back(comma_float(v[i]));
    return result;
}

}  // namespace

void install_menu(App* app) {
    auto* bar = new tk::Menu(app);
    app->configure({{"menu", bar}});

    auto guarded = [app](Cmd fn) -> Cmd {
        return [app, fn = std::move(fn)] {
            try {
                fn();
            } catch (const std::invalid_argument& e) {  // ValueError
                tk::messagebox::showerror("Narzędzia CAD", e.what(), app);
            } catch (const std::system_error& e) {      // OSError
                tk::messagebox::showerror("Narzędzia CAD", e.what(), app);
            } catch (const std::out_of_range& e) {      // KeyError
                tk::messagebox::showerror("Narzędzia CAD", e.what(), app);
            } catch (const py::StopIteration& e) {
                tk::messagebox::showerror("Narzędzia CAD", e.what(), app);
            }
        };
    };
    // Kopia elementu: w Pythonie `p` żyje dalej, nawet gdy projekt zostanie podmieniony.
    auto selected = [app]() -> core::Part {
        core::Part* p = app->selected_part();
        if (!p) throw core::ValueError("Wybierz element w drzewie projektu.");
        return *p;
    };
    auto commit = [app](core::ProjectPtr project) {
        app->snapshot();
        app->project = std::move(project);
        app->refresh();
        app->fit();
    };
    auto edge = [app, selected] {
        cad_geometry::kernel();
        new edge_editor::EdgeEditor(app, selected(), std::nullopt, "chamfer");
    };
    auto machining = [app, selected] {
        cad_geometry::kernel();
        new machining_editor::MachiningEditor(app, selected());
    };
    auto form = [app](const std::string& title, const std::vector<std::pair<std::string, json>>& fields,
                      std::function<void(const Values&)> action) {
        auto* d = app->dialog(title, "530x" + std::to_string(150 + static_cast<int>(fields.size()) * 55));
        std::vector<tk::StringVar> variables;
        for (const auto& [label, value] : fields) variables.push_back(app->field(d, label, value));
        auto submit = [d, title, variables, action = std::move(action)] {
            try {
                d->ensure_current_project(true);
                Values values;
                for (const auto& v : variables) values.push_back(v.get());
                action(values);
                d->destroy();
            } catch (const std::exception& e) {
                // (ValueError, OSError, KeyError, StopIteration, IndexError)
                if (dynamic_cast<const std::invalid_argument*>(&e) || dynamic_cast<const std::system_error*>(&e) ||
                    dynamic_cast<const std::out_of_range*>(&e) || dynamic_cast<const py::StopIteration*>(&e)) {
                    std::string text = e.what();
                    tk::messagebox::showerror(title, text.empty() ? "Element został usunięty. Otwórz formularz ponownie." : text, d);
                } else {
                    throw;
                }
            }
        };
        (new ttk::Button(d, {{"text", "Zastosuj"}, {"command", Cmd(submit)}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 15}});
    };
    auto array = [app, selected, form, commit] {
        core::Part p = selected();
        form("Szyk elementów", {{"Liczba nowych kopii", 3}, {"Krok X [mm]", 0}, {"Krok Y [mm]", 0}, {"Krok Z [mm]", 100}},
             [app, p, commit](const Values& v) {
                 commit(productivity::part_array(*app->project, p.id, py::to_float(v[0]), comma_float(v[1]), comma_float(v[2]), comma_float(v[3])));
             });
    };
    auto mirror = [app, selected, commit](const std::string& axis) {
        core::Part p = selected();
        core::Part q = productivity::mirror_part(p, axis);
        q.x += p.u + 50;
        auto result = std::make_shared<core::Project>(*app->project);  // copy.deepcopy
        result->parts.push_back(q);
        result->validate();
        commit(result);
    };
    auto template_ = [app, selected, form, commit](bool save) {
        core::Part p = selected();
        if (save) {
            std::string path = tk::filedialog::asksaveasfilename(
                {{"parent", app}, {"defaultextension", ".mebel"}, {"initialfile", p.group + ".mebel"},
                 {"filetypes", tk::FileTypes{{"Biblioteka mebli", "*.mebel"}}}});
            if (!path.empty()) productivity::extract_group(*app->project, p.group)->save(path);
        } else {
            core::ProjectPtr group = productivity::extract_group(*app->project, p.group);
            form("Kopiuj cały mebel", {{"Nazwa", p.group + " — kopia"}, {"Przesunięcie X [mm]", 1000}, {"Przesunięcie Y [mm]", 0}, {"Przesunięcie Z [mm]", 0}},
                 [app, group, commit](const Values& v) { commit(productivity::insert_template(*app->project, *group, v[0], offsets(v))); });
        }
    };
    auto load = [app, form, commit] {
        std::string path = tk::filedialog::askopenfilename(
            {{"parent", app}, {"filetypes", tk::FileTypes{{"Biblioteka mebli", "*.mebel"}, {"Projekt", "*.json"}}}});
        if (path.empty()) return;
        core::ProjectPtr group = core::Project::load(path);
        form("Wstaw z biblioteki", {{"Nazwa", group->name}, {"Przesunięcie X [mm]", 1000}, {"Przesunięcie Y [mm]", 0}, {"Przesunięcie Z [mm]", 0}},
             [app, group, commit](const Values& v) { commit(productivity::insert_template(*app->project, *group, v[0], offsets(v))); });
    };
    auto shelves = [app, selected, form, commit] {
        core::Part p = selected();
        form("Dodaj równo rozmieszczone półki", {{"Liczba nowych półek", 3}, {"Grubość [mm]", 18}},
             [app, p, commit](const Values& v) {
                 commit(productivity::equal_shelves(*app->project, p.group, py::to_float(v[0]), py::to_float(v[1]), p.material));
             });
    };
    auto visibility = [app, selected](bool isolate) {
        std::optional<core::Part> p;
        if (isolate) p = selected();
        app->snapshot();
        for (auto& q : app->project->parts) q.visible = p ? q.id == p->id : true;
        app->refresh();
        app->fit();
    };

    struct Menu { std::string label; std::vector<std::pair<std::string, Cmd>> entries; };
    const std::vector<Menu> menus = {
        {"CAD i obróbki",
         {{"Zaokrąglenia kantów…", [app] { app->round_edges(); }},
          {"Fazowanie kantów 45°…", edge},
          {"Kieszenie, rowki, wycięcia i wiercenia…", machining},
          {"Szyk elementów…", array},
          {"Odbicie elementu U", [mirror] { mirror("u"); }},
          {"Odbicie elementu V", [mirror] { mirror("v"); }}}},
        {"Konstrukcja i biblioteka",
         {{"Równy podział na półki…", shelves},
          {"Kopiuj cały mebel…", [template_] { template_(false); }},
          {"Zapisz mebel do biblioteki…", [template_] { template_(true); }},
          {"Wstaw mebel z biblioteki…", load}}},
        {"Widoczność",
         {{"Pokaż tylko wybrany element", [visibility] { visibility(true); }},
          {"Pokaż wszystkie elementy", [visibility] { visibility(false); }}}},
    };
    for (const auto& [label, entries] : menus) {
        auto* menu = new tk::Menu(bar, {{"tearoff", false}});
        bar->add_cascade({{"label", label}, {"menu", menu}});
        for (const auto& [text, fn] : entries) menu->add_command({{"label", text}, {"command", guarded(fn)}});
    }
}

}  // namespace advanced_ui
