#include "ui_settings.h"

#include <format>
#include <memory>
#include <stdexcept>

#include "app.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "tk.h"
#include "ttk.h"
#include "ui_preferences.h"

namespace ui_settings {

using core::json;

namespace {

std::string fmt_g(double v) { return std::format("{:g}", v); }

// Odwrotny słownik wartość -> etykieta (reverse[current[key]]); KeyError gdy brak.
std::string label_of(const json& values, const json& value) {
    for (auto it = values.begin(); it != values.end(); ++it)
        if (it.value() == value) return it.key();
    throw std::out_of_range(py::repr(value));
}

}  // namespace

SettingsDialog* open_settings(App* app) {
    SettingsDialog* existing = app->_interface_settings_document;
    if (existing != nullptr) {
        try {
            if (existing->winfo_exists()) {
                app->workspace->select(existing->form->document);
                return existing;
            }
        } catch (const tk::TclError&) {}
        // Okno zostało zamknięte — zwolnij stary opis i zbuduj nowy.
        app->_interface_settings_document = nullptr;
        delete existing;
    }
    auto* sd = new SettingsDialog();
    form_dialog::Form* d = app->dialog("Ustawienia interfejsu i 3D", "620x740");
    sd->form = d;
    app->_interface_settings_document = sd;
    responsive_ui::UiLayout* controller = app->ui_layout;
    json current = controller->preferences();

    (new ttk::Label(d, {{"text", "Okno, panele i jakość widoku"}, {"style", "Title.TLabel"}, {"padding", 12}}))->pack({{"fill", "x"}});
    (new ttk::Label(d, {{"text", "Te ustawienia dotyczą programu i zapisują się na tym komputerze. Rozmiar okna nie zmienia rozdzielczości Windows."},
                       {"wraplength", 520}, {"padding", "12 0 12 8"}}))->pack({{"fill", "x"}});
    json info = responsive_ui::monitor_info(app);
    long long l = info.at("workarea").at(0).get<long long>(), t = info.at("workarea").at(1).get<long long>();
    long long r = info.at("workarea").at(2).get<long long>(), b = info.at("workarea").at(3).get<long long>();
    (new ttk::Label(d, {{"text", "Obszar roboczy monitora: " + std::to_string(r - l) + " × " + std::to_string(b - t) + " px · DPI: " + py::str(info.at("dpi"))},
                       {"padding", "12 0 12 8"}}))->pack({{"fill", "x"}});

    auto combo = [&](const std::string& key, const std::string& label, json values) {
        auto* frame = new ttk::Frame(d, {{"padding", "12 4"}});
        frame->pack({{"fill", "x"}});
        (new ttk::Label(frame, {{"text", label}}))->pack({{"anchor", "w"}});
        if (key == "ui_scale") {
            bool known = false;
            for (auto it = values.begin(); it != values.end(); ++it) known = known || it.value() == current.at(key);
            if (!known) values[fmt_g(current.at(key).get<double>() * 100) + "% (zapisana skala)"] = current.at(key);
        }
        tk::StringVar var(label_of(values, current.at(key)));
        std::vector<std::string> names;
        for (auto it = values.begin(); it != values.end(); ++it) names.push_back(it.key());
        (new ttk::Combobox(frame, {{"textvariable", var}, {"values", names}, {"state", "readonly"}}))->pack({{"fill", "x"}});
        sd->interface_vars.insert_or_assign(key, var);
        sd->key_order.push_back(key);
        sd->interface_choices[key] = values;
    };
    auto number = [&](const std::string& key, const std::string& label) {
        tk::StringVar var(py::str(current.at(key)));
        auto* row = new responsive_ui::FieldRow(d, label, {{"textvariable", var}, {"width", 15}});
        row->pack({{"fill", "x"}, {"padx", 12}, {"pady", 4}});
        sd->interface_vars.insert_or_assign(key, var);
        sd->key_order.push_back(key);
    };

    json window_modes = json::object();
    window_modes["Automatycznie do monitora"] = "auto";
    window_modes["Własny / wybrany preset"] = "custom";
    combo("window_mode", "Rozmiar okna", window_modes);
    auto* presets = new ttk::Frame(d, {{"padding", "12 2"}});
    presets->pack({{"fill", "x"}});
    (new ttk::Label(presets, {{"text", "Presety:"}}))->pack({{"side", "left"}});
    auto preset = [sd](int w, int h) {
        sd->interface_vars.at("window_mode").set("Własny / wybrany preset");
        sd->interface_vars.at("window_width").set(std::to_string(w));
        sd->interface_vars.at("window_height").set(std::to_string(h));
    };
    auto* buttons = new responsive_ui::WrapToolbar(d);
    buttons->pack({{"fill", "x"}, {"padx", 12}});
    for (auto [w, h] : std::vector<std::pair<int, int>>{{900, 600}, {1280, 720}, {1366, 768}, {1920, 1080}})
        (new ttk::Button(buttons, {{"text", std::to_string(w) + " × " + std::to_string(h)},
                                   {"command", std::function<void()>([preset, w = w, h = h] { preset(w, h); })}}))->pack({{"side", "left"}});
    number("window_width", "Szerokość okna [px]");
    number("window_height", "Wysokość okna [px]");
    json scales = json::object();
    scales["75%"] = .75; scales["90%"] = .9; scales["100%"] = 1.0; scales["110%"] = 1.1;
    scales["125%"] = 1.25; scales["150%"] = 1.5; scales["175%"] = 1.75; scales["200%"] = 2.0;
    combo("ui_scale", "Skala interfejsu", scales);
    number("inspector_width", "Szerokość bocznego panelu [px]");
    number("ribbon_height", "Wysokość wstążki [px]");
    (new ttk::Label(d, {{"text", "Przeciągnij separator obok inspektora lub pod wstążką. Limity zachowują miejsce na widok 3D."},
                       {"wraplength", 520}, {"padding", 12}}))->pack({{"fill", "x"}});
    for (auto [key, label] : std::vector<std::pair<std::string, std::string>>{
             {"show_inspector", "Pokaż boczny inspektor"}, {"show_ribbon", "Pokaż górną wstążkę"}, {"show_toolbars", "Pokaż paski skrótów nad 3D"}}) {
        auto inserted = sd->interface_flags.emplace(key, tk::BooleanVar(current.at(key).get<bool>()));
        sd->key_order.push_back(key);
        (new ttk::Checkbutton(d, {{"text", label}, {"variable", inserted.first->second}}))->pack({{"anchor", "w"}, {"padx", 12}, {"pady", 3}});
    }
    json engines = json::object();
    for (const auto& [name, value] : ui_preferences::ENGINE_LABELS) engines[name] = value;
    combo("engine", "Silnik widoku 3D", engines);
    json qualities = json::object();
    qualities["Szybki podgląd"] = "draft"; qualities["Zrównoważona"] = "balanced"; qualities["Wysoka"] = "high";
    combo("quality", "Jakość 3D", qualities);
    (new ttk::Label(d, {{"text", "Automatyczny dobór uruchamia natywny silnik C++, a gdy jest niedostępny, wybiera OpenGL lub CPU. Przełączenie na niedostępny silnik zachowuje działający widok. OpenGL zachowuje dotychczasowy tryb wysokiej jakości. Jakość widoku nie zmienia wymiarów produkcyjnych."},
                       {"wraplength", 520}, {"padding", 12}}))->pack({{"fill", "x"}});
    tk::StringVar note;
    (new ttk::Label(d, {{"textvariable", note}, {"wraplength", 520}, {"padding", "12 2"}}))->pack({{"fill", "x"}});

    auto apply = [sd, controller, note, d]() mutable {
        try {
            json result = controller->preferences();
            for (const auto& key : sd->key_order) {
                auto choice = sd->interface_choices.find(key);
                if (choice != sd->interface_choices.end()) {
                    std::string label = sd->interface_vars.at(key).get();
                    if (!choice->second.contains(label)) throw std::out_of_range(py::repr(json(label)));
                    result[key] = choice->second.at(label);
                } else if (key.rfind("show_", 0) == 0) {
                    result[key] = sd->interface_flags.at(key).get();
                } else {
                    result[key] = py::to_int(sd->interface_vars.at(key).get());
                }
            }
            controller->apply(ui_preferences::validate_preferences(result), true);
            if (controller->persist()) note.set("Zapisano ustawienia interfejsu. Projekt mebla nie został zmieniony.");
            else note.set("Ustawienia zastosowano, ale nie udało się zapisać ich na dysku.");
            d->mark_clean();
        } catch (const std::invalid_argument& error) {  // ValueError
            note.set(error.what());
        } catch (const std::out_of_range& error) {      // KeyError
            note.set(error.what());
        } catch (const std::system_error& error) {      // OSError
            note.set(error.what());
        }
    };
    auto reset = [sd, note]() mutable {
        for (const auto& key : sd->key_order) {
            const json& value = ui_preferences::DEFAULTS.at(key);
            auto choice = sd->interface_choices.find(key);
            if (choice != sd->interface_choices.end()) {
                std::string name;
                bool found = false;
                for (auto it = choice->second.begin(); it != choice->second.end() && !found; ++it)
                    if (it.value() == value) { name = it.key(); found = true; }
                if (!found) throw py::StopIteration();
                sd->interface_vars.at(key).set(name);
            } else if (key.rfind("show_", 0) == 0) {
                sd->interface_flags.at(key).set(value.get<bool>());
            } else {
                sd->interface_vars.at(key).set(py::str(value));
            }
        }
        note.set("Przywrócono wartości domyślne w formularzu. Naciśnij Zastosuj, aby je zapisać.");
    };
    (new ttk::Button(d, {{"text", "Wartości domyślne"}, {"command", std::function<void()>(reset)}}))->pack({{"fill", "x"}, {"padx", 12}, {"pady", 4}});
    (new ttk::Button(d, {{"text", "Zastosuj i zapisz ustawienia"}, {"command", std::function<void()>(apply)}, {"style", "Accent.TButton"}}))
        ->pack({{"fill", "x"}, {"padx", 12}, {"pady", 8}});
    sd->apply_interface_preferences = apply;
    return sd;
}

}  // namespace ui_settings
