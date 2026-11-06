// Small persistent tray; root places it beside the actual 3D canvas.
#include "hardware_placement_ui.h"

#include "app.h"
#include "fittings.h"
#include "pyutil.h"
#include "runners_catalog.h"

namespace hardware_placement_ui {

namespace {
// number = lambda var: float(var.get().strip().replace(',', '.'))
double number(tk::StringVar& var) { return py::to_float(py::replace_all(py::strip(var.get()), ",", ".")); }

// next(r['id'] for r in RECIPES if r['name'] == name) — StopIteration, gdy brak.
std::string recipe_id_for(const std::string& name) {
    for (auto& r : fittings::RECIPES) if (r["name"].get<std::string>() == name) return r["id"].get<std::string>();
    throw py::StopIteration();
}
}  // namespace

HardwarePlacementPanel::HardwarePlacementPanel(tk::Widget* parent, App* app_) : ttk::Frame(parent, {{"padding", "8 4"}}), app(app_) {
    note = tk::StringVar("Przeciągnij kafelek na mebel · lub kliknij kafelek, potem element.");
    face = tk::StringVar("Powierzchnia kursora"); width = tk::StringVar("12");
    axis = tk::StringVar("Automatycznie");
    length = tk::StringVar(""); margin = tk::StringVar("30"); price = tk::StringVar("0");
    price_unit = tk::StringVar("Cena [zł/szt.]");
    groove = tk::BooleanVar(false); count = tk::StringVar("2");
    profile = tk::StringVar(runners_catalog::PROFILES["hettich_ka5632"]["label"].get<std::string>());
    std::string hinge_name; bool found = false;
    for (auto& r : fittings::RECIPES) if (r["id"] == "hinge45") { hinge_name = r["name"].get<std::string>(); found = true; break; }
    if (!found) throw py::StopIteration();
    hinge = tk::StringVar(hinge_name);
    controller = new drag_hardware::HardwareDragController(app, this); app->hardware_drag = controller;
    auto* row = new ttk::Frame(this); row->pack({{"fill", "x"}});
    details = new ttk::Frame(this); _details_visible = false;
    details_button = new ttk::Button(row, {{"text", "Parametry ▾"}, {"command", std::function<void()>([this] { toggle_details(); })}});
    details_button->pack({{"side", "right"}, {"padx", "5 0"}});
    tiles.clear();
    for (auto& [kind, label] : std::vector<std::pair<std::string, std::string>>{{"hinge", "◉ Zawiasy"}, {"runner", "⇆ Prowadnice"}, {"led", "▰ LED"},
                                                                                 {"handle", "▱ Uchwyt"}, {"lamp", "☼ Lampa"}, {"bulb", "✧ Żarówka"}}) {
        auto* tile = new ttk::Label(row, {{"text", label}, {"padding", "8 7"}, {"anchor", "center"}, {"relief", "ridge"}, {"cursor", "hand2"}});
        tile->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 2}}); tiles.emplace_back(kind, tile);
        std::string k = kind;
        tile->bind("<ButtonPress-1>", [this, k](tk::Event& event) { return begin(k, &event); });
        tile->bind("<B1-Motion>", [this](tk::Event& event) { return controller->move(event).value_or(std::string()); });
        tile->bind("<ButtonRelease-1>", [this](tk::Event& event) { return controller->end(event).value_or(std::string()); });
    }
    // Long catalogue names live in a single shared row, never a new window.
    row = new ttk::Frame(details); row->pack({{"fill", "x"}, {"pady", "4 2"}});
    (new ttk::Label(row, {{"text", "Zawias:"}}))->pack({{"side", "left"}});
    std::vector<std::string> hinges;
    for (auto& r : fittings::RECIPES) { std::string id = r["id"].get<std::string>(); if (id == "hinge45" || id == "hinge52" || id == "hinge_sensys52") hinges.push_back(r["name"].get<std::string>()); }
    (new ttk::Combobox(row, {{"textvariable", hinge}, {"values", hinges}, {"state", "readonly"}, {"width", 20}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 3}});
    (new ttk::Label(row, {{"text", "Prowadnica:"}}))->pack({{"side", "left"}});
    std::vector<std::string> profiles; for (auto& [key, value] : runners_catalog::PROFILES.items()) profiles.push_back(value["label"].get<std::string>());
    (new ttk::Combobox(row, {{"textvariable", profile}, {"values", profiles}, {"state", "readonly"}, {"width", 20}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 3}});
    row = new ttk::Frame(details); row->pack({{"fill", "x"}, {"pady", 2}});
    std::vector<std::string> values = {"Powierzchnia kursora", "Góra +T", "Dół −T", "Krawędź U=0", "Krawędź U=max", "Krawędź V=0", "Krawędź V=max"};
    (new ttk::Combobox(row, {{"textvariable", face}, {"values", values}, {"state", "readonly"}, {"width", 20}}))->pack({{"side", "left"}, {"padx", 2}});
    for (auto& [label, var, width_] : std::vector<std::tuple<std::string, tk::StringVar, int>>{{"LED szer.", width, 4}, {"Dł. auto", length, 5}, {"Marg.", margin, 4}}) {
        (new ttk::Label(row, {{"text", label}}))->pack({{"side", "left"}, {"padx", "3 1"}}); (new ttk::Entry(row, {{"textvariable", var}, {"width", width_}}))->pack({{"side", "left"}});
    }
    row = new ttk::Frame(details); row->pack({{"fill", "x"}, {"pady", 2}});
    (new ttk::Label(row, {{"text", "Zawiasów"}}))->pack({{"side", "left"}}); (new ttk::Entry(row, {{"textvariable", count}, {"width", 3}}))->pack({{"side", "left"}, {"padx", 3}});
    (new ttk::Label(row, {{"textvariable", price_unit}}))->pack({{"side", "left"}, {"padx", 3}}); (new ttk::Entry(row, {{"textvariable", price}, {"width", 7}}))->pack({{"side", "left"}});
    (new ttk::Checkbutton(row, {{"text", "Rowek LED"}, {"variable", groove}}))->pack({{"side", "left"}, {"padx", 4}});
    (new ttk::Label(row, {{"text", "Kierunek LED:"}}))->pack({{"side", "left"}, {"padx", "4 1"}});
    (new ttk::Combobox(row, {{"textvariable", axis}, {"values", std::vector<std::string>{"Automatycznie", "Oś U powierzchni", "Oś V powierzchni"}}, {"state", "readonly"}, {"width", 17}}))->pack({{"side", "left"}});
    (new ttk::Button(row, {{"text", "Anuluj / Esc"}, {"command", std::function<void()>([this] { controller->cancel(); })}}))->pack({{"side", "right"}});
    note_label = new ttk::Label(this, {{"textvariable", note}, {"foreground", "#176a78"}, {"wraplength", 850}});
    note_label->pack({{"anchor", "w"}, {"pady", 2}});
}

void HardwarePlacementPanel::toggle_details() {
    _details_visible = !_details_visible;
    if (_details_visible) details->pack({{"fill", "x"}, {"before", note_label->path()}});
    else details->pack_forget();
    details_button->configure({{"text", _details_visible ? "Parametry ▴" : "Parametry ▾"}});
}

json HardwarePlacementPanel::options_for(const std::string& kind) {
    double price_value = number(price);
    if (kind == "hinge") {
        std::string recipe_id = recipe_id_for(hinge.get());
        return json{{"recipe_id", recipe_id}, {"price", price_value}, {"count", py::to_int(count.get())}};
    }
    if (kind == "runner") {
        std::string profile_id; bool found = false;
        for (auto& [key, value] : runners_catalog::PROFILES.items()) if (value["label"] == profile.get()) { profile_id = key; found = true; break; }
        if (!found) throw py::StopIteration();
        json length_value = py::strip(length.get()).empty() ? json(400) : json(number(length));
        return json{{"profile_id", profile_id}, {"length", length_value}, {"price", price_value}};
    }
    if (kind == "led") {
        static const std::vector<std::pair<std::string, std::string>> faces = {{"Góra +T", "top"}, {"Dół −T", "bottom"}, {"Krawędź U=0", "u0"}, {"Krawędź U=max", "u1"}, {"Krawędź V=0", "v0"}, {"Krawędź V=max", "v1"}};
        json face_value = nullptr; for (auto& [label, code] : faces) if (label == face.get()) face_value = code;
        double width_value = number(width);
        json length_value = py::strip(length.get()).empty() ? json(nullptr) : json(number(length));
        double margin_value = number(margin);
        bool groove_value = groove.get();
        std::string axis_value = axis.get() == "Oś U powierzchni" ? "u" : axis.get() == "Oś V powierzchni" ? "v" : "auto";
        return json{{"face", face_value}, {"width", width_value}, {"length", length_value}, {"margin", margin_value}, {"groove", groove_value}, {"price_per_m", price_value}, {"axis", axis_value}};
    }
    return json{{"price", price_value}};
}

std::string HardwarePlacementPanel::begin(const std::string& kind, tk::Event* event) {
    price_unit.set(kind == "led" ? "Cena [zł/m]" : kind == "runner" ? "Cena [zł/parę]" : kind == "hinge" ? "Cena [zł/zawias]" : "Cena [zł/szt.]");
    auto failed = [&](const std::string& message) { note.set(message.empty() ? std::string("Sprawdź parametry wyposażenia.") : message); return std::string("break"); };
    try {
        return controller->begin(kind, options_for(kind), event);
    } catch (const py::ValueError& error) { return failed(error.what()); }
    catch (const py::TypeError& error) { return failed(error.what()); }
    catch (const py::StopIteration&) { return failed(""); }  // str(StopIteration()) == ''
}

void HardwarePlacementPanel::refresh() {
    // There is no document-bound pending form; previews always use app.project.
    if (controller->active()) controller->draw_ghost();
}

}  // namespace hardware_placement_ui
