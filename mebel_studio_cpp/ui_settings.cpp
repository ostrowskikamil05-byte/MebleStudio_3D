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

    // Układ: pasek akcji przypięty do dołu (zawsze widoczny), nad nim sekcje tematyczne.
    tk::StringVar note;
    auto* actions = new ttk::Frame(d, {{"padding", "12 6"}});
    actions->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* page = new ttk::Frame(d, {{"padding", "12 8"}});
    page->pack({{"side", "top"}, {"fill", "both"}, {"expand", true}});

    (new ttk::Label(page, {{"text", "Okno, panele i jakość widoku"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}});
    json info = responsive_ui::monitor_info(app);
    long long l = info.at("workarea").at(0).get<long long>(), t = info.at("workarea").at(1).get<long long>();
    long long r = info.at("workarea").at(2).get<long long>(), b = info.at("workarea").at(3).get<long long>();
    (new ttk::Label(page, {{"text", "Zapisują się na tym komputerze i nie zmieniają projektu mebla. Monitor: " + std::to_string(r - l) + " × " + std::to_string(b - t) +
                                    " px · DPI " + py::str(info.at("dpi"))},
                          {"wraplength", 560}, {"padding", "0 0 6 0"}}))->pack({{"anchor", "w"}, {"fill", "x"}});

    auto section = [&](const std::string& title) {
        auto* box = new ttk::LabelFrame(page, {{"text", title}, {"padding", "10 6"}});
        box->pack({{"fill", "x"}, {"pady", "0 8"}});
        return box;
    };
    // Wiersz: etykieta po lewej, kontrolka po prawej (stała, przewidywalna kolumna).
    auto row = [&](tk::Widget* parent, const std::string& label) {
        auto* line = new ttk::Frame(parent);
        line->pack({{"fill", "x"}, {"pady", 3}});
        auto* text = new ttk::Label(line, {{"text", label}, {"width", 26}});
        text->pack({{"side", "left"}});
        return line;
    };
    auto combo = [&](tk::Widget* parent, const std::string& key, const std::string& label, json values) {
        auto* line = row(parent, label);
        if (key == "ui_scale") {
            bool known = false;
            for (auto it = values.begin(); it != values.end(); ++it) known = known || it.value() == current.at(key);
            if (!known) values[fmt_g(current.at(key).get<double>() * 100) + "% (zapisana skala)"] = current.at(key);
        }
        tk::StringVar var(label_of(values, current.at(key)));
        std::vector<std::string> names;
        for (auto it = values.begin(); it != values.end(); ++it) names.push_back(it.key());
        (new ttk::Combobox(line, {{"textvariable", var}, {"values", names}, {"state", "readonly"}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
        sd->interface_vars.insert_or_assign(key, var);
        sd->key_order.push_back(key);
        sd->interface_choices[key] = values;
    };
    auto number = [&](tk::Widget* parent, const std::string& key, const std::string& label) {
        auto* line = row(parent, label);
        tk::StringVar var(py::str(current.at(key)));
        (new ttk::Entry(line, {{"textvariable", var}, {"width", 10}}))->pack({{"side", "left"}});
        sd->interface_vars.insert_or_assign(key, var);
        sd->key_order.push_back(key);
    };

    // 1. Okno i skala
    auto* window_box = section("Okno i skala");
    json window_modes = json::object();
    window_modes["Automatycznie do monitora"] = "auto";
    window_modes["Własny / wybrany preset"] = "custom";
    combo(window_box, "window_mode", "Rozmiar okna", window_modes);
    number(window_box, "window_width", "Szerokość okna [px]");
    number(window_box, "window_height", "Wysokość okna [px]");
    auto preset = [sd](int w, int h) {
        sd->interface_vars.at("window_mode").set("Własny / wybrany preset");
        sd->interface_vars.at("window_width").set(std::to_string(w));
        sd->interface_vars.at("window_height").set(std::to_string(h));
    };
    auto* presets = row(window_box, "Presety");
    for (auto [w, h] : std::vector<std::pair<int, int>>{{900, 600}, {1280, 720}, {1366, 768}, {1920, 1080}})
        (new ttk::Button(presets, {{"text", std::to_string(w) + " × " + std::to_string(h)},
                                   {"command", std::function<void()>([preset, w = w, h = h] { preset(w, h); })}}))->pack({{"side", "left"}, {"padx", "0 4"}});
    json scales = json::object();
    scales["75%"] = .75; scales["90%"] = .9; scales["100%"] = 1.0; scales["110%"] = 1.1;
    scales["125%"] = 1.25; scales["150%"] = 1.5; scales["175%"] = 1.75; scales["200%"] = 2.0;
    combo(window_box, "ui_scale", "Skala interfejsu", scales);

    // 2. Panele
    auto* panels_box = section("Panele i paski");
    for (auto [key, label] : std::vector<std::pair<std::string, std::string>>{
             {"show_inspector", "Pokaż boczny inspektor"}, {"show_ribbon", "Pokaż górną wstążkę"}, {"show_toolbars", "Pokaż paski skrótów nad 3D"}}) {
        auto inserted = sd->interface_flags.emplace(key, tk::BooleanVar(current.at(key).get<bool>()));
        sd->key_order.push_back(key);
        (new ttk::Checkbutton(panels_box, {{"text", label}, {"variable", inserted.first->second}}))->pack({{"anchor", "w"}, {"pady", 2}});
    }
    number(panels_box, "inspector_width", "Szerokość bocznego panelu [px]");
    number(panels_box, "ribbon_height", "Wysokość wstążki [px]");
    (new ttk::Label(panels_box, {{"text", "Szerokość i wysokość możesz też zmienić, przeciągając separator obok inspektora lub pod wstążką."},
                                {"wraplength", 540}, {"padding", "0 4 0 0"}}))->pack({{"fill", "x"}});

    // 3. Widok 3D
    auto* view_box = section("Widok 3D");
    json engines = json::object();
    for (const auto& [name, value] : ui_preferences::ENGINE_LABELS) engines[name] = value;
    combo(view_box, "engine", "Silnik widoku 3D", engines);
    json qualities = json::object();
    qualities["Szybki podgląd"] = "draft"; qualities["Zrównoważona"] = "balanced"; qualities["Wysoka"] = "high";
    combo(view_box, "quality", "Jakość 3D", qualities);
    (new ttk::Label(view_box, {{"text", "Automatyczny dobór wybiera natywny silnik C++ (DirectX/Vulkan), a gdy jest niedostępny — OpenGL lub CPU. Jakość widoku nie zmienia wymiarów produkcyjnych."},
                              {"wraplength", 540}, {"padding", "0 4 0 0"}}))->pack({{"fill", "x"}});

    (new ttk::Label(page, {{"textvariable", note}, {"wraplength", 560}, {"foreground", "#0f766e"}}))->pack({{"anchor", "w"}, {"fill", "x"}});

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
    (new ttk::Button(actions, {{"text", "Wartości domyślne"}, {"command", std::function<void()>(reset)}}))->pack({{"side", "left"}});
    (new ttk::Button(actions, {{"text", "Zastosuj i zapisz ustawienia"}, {"command", std::function<void()>(apply)}, {"style", "Accent.TButton"}}))
        ->pack({{"side", "right"}});
    sd->apply_interface_preferences = apply;
    return sd;
}

}  // namespace ui_settings
