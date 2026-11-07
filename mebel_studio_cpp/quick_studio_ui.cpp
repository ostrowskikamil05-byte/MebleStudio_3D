// One persistent inspector and a compact ribbon for cabinet configuration.
#include "forma_theme.h"
#include "forma_widgets.h"
#include "perf_monitor.h"
#include "quick_studio_ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <format>
#include <set>

#include "app.h"
#include "configurator_presets.h"
#include "corner_furniture.h"
#include "corner_quick_ui.h"
#include "dock_workspace.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "furniture_layout_ui.h"
#include "furniture_library.h"
#include "hardware_placement_ui.h"
#include "hardware_ui.h"
#include "migration_status.h"
#include "opening_options.h"
#include "productivity.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "runners_catalog.h"
#include "ui_settings.h"
#include "viewport_annotations.h"
#include "workspace_worktops.h"
// Moduły jeszcze nieprzetłumaczone (założony interfejs):
#include "advanced_ui.h"
#include "sketch.h"
#include "studio_ui.h"
#include "style_ui.h"

namespace quick_studio_ui {

using Cmd = std::function<void()>;
using Handler = std::function<std::string(tk::Event&)>;

namespace {

// dict.get(key, default)
json jget(const json& d, const std::string& key, const json& fallback = nullptr) {
    return d.is_object() && d.contains(key) ? d[key] : fallback;
}

// dict[key] — KeyError z komunikatem jak str(KeyError(key)).
const json& at(const json& d, const std::string& key) {
    if (!d.is_object() || !d.contains(key)) throw py::KeyError("'" + key + "'");
    return d[key];
}

// len(str) liczone w znakach Unicode.
int utf8_len(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string fmt_g(double v) { return std::format("{:g}", v); }

// f'{value:g}' if isinstance(value,(int,float)) else str(value)   (bool jest podklasą int)
std::string show_value(const json& value) {
    if (value.is_number() || value.is_boolean()) return fmt_g(py::to_float(value));
    return py::str(value);
}

// Kolejność wstawiania, bez duplikatów: list(dict.fromkeys(...)).
std::vector<std::string> unique_groups(const core::Project& project) {
    std::vector<std::string> groups;
    for (auto& p : project.parts) if (std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
    return groups;
}

std::vector<std::string> keys_of(const json& choices) {
    std::vector<std::string> out;
    for (auto& [k, v] : choices.items()) out.push_back(k);
    return out;
}

// next(label for label, item in choices.items() if item == value, None)
std::optional<std::string> label_of(const json& choices, const json& value) {
    for (auto& [k, v] : choices.items()) if (v == value) return k;
    return std::nullopt;
}

// Ikona z lewej strony wiersza listy poleceń (jak na makiecie); przyciski akcentowane jej nie mają.
std::string row_label(const std::string& label) {
    static const std::map<std::string, std::string> glyphs = {
        {"Rysuj", "✚"}, {"Edytuj szkic", "✎"}, {"Dopasuj element", "⚙"}, {"Zaokrąglenia", "◠"}, {"Dopasuj 3D", "⛶"},
        {"Biblioteka wszystkich mebli", "▦"}, {"Kształty i style", "◇"}, {"Kolory", "◐"}, {"Fronty / uchwyty", "▭"}, {"Zawiasy / prowadnice", "⚙"},
        {"Nóżki / blat", "⊓"}, {"Materiały", "▤"}, {"LED", "✦"}, {"Okucia elementu…", "⚙"}, {"−90°", "↶"}, {"+90°", "↷"},
        {"Łączenie i pozycja…", "⊞"}, {"Narożnik…", "⌐"}, {"Wspólny blat…", "▬"}, {"Drukuj komplet…", "⎙"}, {"Eksport CAD / CNC", "⇪"},
        {"Logi", "≡"}, {"Wyposażenie i oświetlenie", "☼"}, {"Jasne pomieszczenie", "☀"}, {"Dzienne", "◑"}, {"Ciemne", "●"},
        {"LED wł. / wył.", "✦"}, {"Lampy wł. / wył.", "☼"}, {"Katalog mebli", "▦"}, {"Katalog okuć", "⚙"}, {"Katalog materiałów", "▤"},
        {"Katalog AGD", "▣"}, {"Silnik i jakość 3D…", "⚙"}};
    auto found = glyphs.find(label);
    return (found != glyphs.end() ? found->second : std::string("›")) + "   " + label;
}

ttk::Button* make_button(tk::Widget* parent, const std::string& label, Cmd command, bool accent = false) {
    auto* toolbar = dynamic_cast<responsive_ui::WrapToolbar*>(parent);
    bool row = !accent && toolbar != nullptr && toolbar->vertical;  // wiersz listy poleceń w panelu bocznym
    auto* button = new ttk::Button(parent, {{"text", row ? row_label(label) : label}, {"command", command}, {"width", std::max(3, utf8_len(label))},
                                            {"style", accent ? "Accent.TButton" : row ? "Ribbon.TButton" : "TButton"}, {"padding", row ? "10 9" : accent ? "7 6" : "7 4"}});
    button->pack({{"side", "left"}, {"padx", 2}, {"pady", row ? 0 : 3}});
    return button;
}

// Podpisy grup wyłączone (wyglądały jak niedziałające przyciski); funkcja zostaje, bo używają jej inne moduły.
ttk::Label* make_group(tk::Widget*, const std::string&) { return nullptr; }

Handler plain(std::function<void(tk::Event&)> fn) {
    return [fn](tk::Event& e) { fn(e); return std::string(); };
}

}  // namespace

// ---------------------------------------------------------------------------
// ScrollPanel
// ---------------------------------------------------------------------------

ScrollPanel::ScrollPanel(tk::Widget* parent) : ttk::Frame(parent) {
    canvas = new tk::Canvas(this, {{"highlightthickness", 0}, {"bg", "#f6f3ee"}, {"width", 320}});
    auto* sb = new ttk::Scrollbar(this, {{"orient", "vertical"}, {"command", canvas->path() + " yview"}});
    sb->pack({{"side", "right"}, {"fill", "y"}});
    canvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    canvas->configure({{"yscrollcommand", sb->path() + " set"}});
    body = new ttk::Frame(canvas, {{"padding", "10 4"}});
    window = canvas->create_window(0, 0, {{"window", body->path()}, {"anchor", "nw"}});
    body->bind("<Configure>", [this](tk::Event&) { responsive_ui::update_scrollregion(canvas); return std::string(); }, false);
    canvas->bind("<Configure>", [this](tk::Event& e) { resize(e); return std::string(); });
    body->bind("<Map>", [this](tk::Event&) { install_wheel(body); return std::string(); });
}

void ScrollPanel::install_wheel(tk::Widget* parent) {
    parent->bind("<MouseWheel>", [this](tk::Event& e) { return wheel(e); }, true);
    for (auto* child : parent->winfo_children()) install_wheel(child);
}

void ScrollPanel::resize(tk::Event& event) {
    canvas->itemconfigure(window, {{"width", event.width}});
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        try {
            if (widget->winfo_class() == "TLabel" && py::to_int(widget->cget("wraplength")))
                widget->configure({{"wraplength", std::max(100, event.width - 28)}});
        } catch (const tk::TclError&) {
        } catch (const py::ValueError&) {
        }
        for (auto* child : widget->winfo_children()) visit(child);
    };
    visit(body);
}

std::string ScrollPanel::wheel(tk::Event& event) {
    canvas->yview_scroll(event.delta > 0 ? -1 : 1, "units");
    return "break";
}

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------

// Miniatura bryły mebla (rzut izometryczny) o proporcjach szerokość × wysokość × głębokość.
void draw_card_icon(tk::Canvas* c, double w, double h, double d) {
    if (c == nullptr) return;
    try {
        c->delete_("all");
        if (w <= 0 || h <= 0 || d <= 0) { w = 3; h = 2; d = 2; }
        double k = 38.0 / std::max({w, h, d});
        double W = w * k, H = h * k, D = d * k;
        double dx = D * 0.8, dy = D * 0.5, x0 = 4, y0 = 60;
        c->create_polygon({x0, y0, x0 + W, y0, x0 + W, y0 - H, x0, y0 - H}, {{"fill", "#d9b98a"}, {"outline", "#7a6544"}});
        c->create_polygon({x0 + W, y0, x0 + W + dx, y0 - dy, x0 + W + dx, y0 - H - dy, x0 + W, y0 - H}, {{"fill", "#b99668"}, {"outline", "#7a6544"}});
        c->create_polygon({x0, y0 - H, x0 + W, y0 - H, x0 + W + dx, y0 - H - dy, x0 + dx, y0 - H - dy}, {{"fill", "#e8d2ab"}, {"outline", "#7a6544"}});
    } catch (const tk::TclError&) {
    }
}

Section::Section(tk::Widget* parent, const std::string& title_, bool opened_) : ttk::Frame(parent), opened(opened_), title(title_) {
    pack({{"fill", "x"}, {"pady", "3 2"}});
    toggle = new ttk::Button(this, {{"command", Cmd([this] { flip(); })}, {"style", "Section.TButton"}});
    toggle->pack({{"fill", "x"}});
    body = new ttk::Frame(this, {{"padding", "2 5"}});
    show();
}

void Section::show() {
    toggle->configure({{"text", std::string(opened ? "▾  " : "▸  ") + title}});
    if (opened) body->pack({{"fill", "x"}});
    else body->pack_forget();
}

void Section::flip() {
    opened = !opened;
    show();
}

// ---------------------------------------------------------------------------
// QuickInspector
// ---------------------------------------------------------------------------

const QuickInspector::Field& QuickInspector::find(const std::string& key) const {
    for (auto& f : fields) if (f.key == key) return f;
    throw py::KeyError("'" + key + "'");
}

QuickInspector::QuickInspector(tk::Widget* parent, App* app_) : ttk::Frame(parent), app(app_), generation(app_->project_generation) {
    note.set("Wybierz typ i dodaj mebel. Wymiary w mm.");
    auto* top = new ttk::Frame(this, {{"padding", "10 6"}});
    top->pack({{"fill", "x"}});
    card_title.set("Mebel");
    card_sub.set("Wybierz mebel albo dodaj nowy");
    auto* card = new ttk::Frame(top, {{"style", "Card.TFrame"}, {"padding", "10 8"}});
    card->pack({{"fill", "x"}, {"pady", "0 6"}});
    card_icon = new tk::Canvas(card, {{"width", 64}, {"height", 64}, {"bg", forma_theme::CARD}, {"highlightthickness", 0}});
    card_icon->pack({{"side", "left"}, {"padx", "0 10"}});
    draw_card_icon(card_icon, 0, 0, 0);
    auto* card_text = new tk::Frame(card, {{"bg", forma_theme::CARD}});
    card_text->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Label(card_text, {{"textvariable", card_title}, {"font", "{Segoe UI} 12 bold"}, {"background", forma_theme::CARD}}))->pack({{"anchor", "w"}});
    (new ttk::Label(card_text, {{"textvariable", card_sub}, {"foreground", forma_theme::MUTED}, {"background", forma_theme::CARD}}))->pack({{"anchor", "w"}});
    groupcombo = new ttk::Combobox(top, {{"textvariable", groupvar}, {"state", "readonly"}});
    groupcombo->pack({{"fill", "x"}, {"pady", 4}});
    groupcombo->bind("<<ComboboxSelected>>", plain([this](tk::Event&) { choose_group(); }));
    auto* actions = new ttk::Frame(top);
    actions->pack({{"fill", "x"}});
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"+ Nowy", [this] { new_furniture(); }}, {"Kopia", [this] { duplicate_group(); }}, {"Usuń", [this] { delete_group(); }}})
        (new ttk::Button(actions, {{"text", label}, {"command", fn}, {"padding", "6 4"}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 1}});
    auto* tabbar = new ttk::Frame(this, {{"padding", "10 2"}});
    tabbar->pack({{"fill", "x"}});
    for (const char* name : {"Korpus", "Fronty", "Okucia", "Dodatki"}) {
        std::string tab_name = name;
        auto* b = new ttk::Button(tabbar, {{"text", tab_name}, {"style", "Chip.TButton"}, {"command", Cmd([this, tab_name] { show_tab(tab_name); })}});
        b->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 1}});
        tab_buttons.emplace_back(tab_name, b);
    }
    auto* bottom = new ttk::Frame(this, {{"padding", "10 6"}});
    bottom->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* actionrow = new ttk::Frame(bottom);
    actionrow->pack({{"fill", "x"}});
    apply_button = new ttk::Button(actionrow, {{"text", "Zastosuj do mebla"}, {"command", Cmd([this] { apply(true); })}, {"style", "Accent.TButton"}});
    apply_button->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Button(actionrow, {{"text", "↺"}, {"width", 3}, {"command", Cmd([this] { reset_fields(); })}}))->pack({{"side", "right"}, {"padx", "4 0"}});
    auto* note_label = new ttk::Label(bottom, {{"textvariable", note}, {"wraplength", 295}, {"font", "{Segoe UI} 9"}});
    note_label->pack({{"fill", "x"}, {"pady", "4 0"}});
    note_label->bind("<Configure>", plain([note_label](tk::Event& e) { note_label->configure({{"wraplength", std::max(100, e.width)}}); }));
    scroll = new ScrollPanel(this);
    scroll->pack({{"fill", "both"}, {"expand", true}});
    tk::Widget* body = scroll->body;
    type_section = new Section(body, "Wymiary", true);
    sections["type"] = type_section;
    for (size_t i = 0; i < furniture_library::TEMPLATES.size(); ++i) {
        auto& kind = furniture_library::TEMPLATES[i].kind;
        if (kind == "cabinet" || kind == "drawers" || kind == "shelf" || kind == "island") template_indices.push_back((int)i);
    }
    std::vector<std::string> names;
    for (int i : template_indices) names.push_back(furniture_library::TEMPLATES[i].category + " · " + furniture_library::TEMPLATES[i].name);
    templatecombo = new ttk::Combobox(type_section->body, {{"textvariable", templatevar}, {"values", names}, {"state", "readonly"}});
    templatecombo->pack({{"fill", "x"}, {"pady", 3}});
    templatecombo->bind("<<ComboboxSelected>>", plain([this](tk::Event&) { change_template(); }));
    field(type_section->body, "name", "Nazwa", false, true);
    field_pair(type_section->body, "width", "Szerokość", "height", "Wysokość");
    field_pair(type_section->body, "depth", "Głębokość", "thickness", "Grubość korpusu");
    (new ttk::Label(type_section->body, {{"text", "Wysokość obejmuje nóżki i blat."}, {"font", "{Segoe UI} 9"}, {"foreground", "#6b7266"}}))->pack({{"anchor", "w"}});
    // A visible shortcut strip changes geometry in one click, like the reference tool.
    auto* quick = new ttk::Frame(top);
    quick->pack({{"fill", "x"}, {"pady", "5 0"}});
    for (auto& [label, mode, count] : std::vector<std::tuple<std::string, std::string, int>>{{"Otwarty", "open", 0}, {"Drzwi", "doors", 1}, {"2 szuf.", "drawers", 2}, {"3 szuf.", "drawers", 3}, {"4 szuf.", "drawers", 4}}) {
        std::string m = mode;
        int n = count;
        (new ttk::Button(quick, {{"text", label}, {"command", Cmd([this, m, n] { quick_front(m, n); })}, {"width", 1}, {"padding", "2 5"}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    }
    auto* materials = new Section(body, "Materiał", true);
    sections["materials"] = materials;
    combo(materials->body, "material", "Korpus", json::object());
    combo(materials->body, "front_material", "Fronty", json::object());
    forma_widgets::caption(materials->body, "Próbki materiałów");
    swatch_row = new tk::Frame(materials->body, {{"bg", forma_theme::PANEL}});
    swatch_row->pack({{"fill", "x"}, {"pady", "2 6"}});
    check(materials->body, "back", "Plecy");
    field(materials->body, "back_thickness", "Grubość pleców");
    auto* main = new Section(body, "Konstrukcja", true);
    sections["main"] = main;
    section_fields(main->body, "main");
    for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"bottom", "Strefa dolna"}, {"upper", "Pawlacz / strefa górna"}}) {
        auto* sec = new Section(body, label);
        sections[key] = sec;
        check(sec->body, key + ".enabled", "Włącz strefę");
        field(sec->body, key + ".height", "Wysokość strefy");
        section_fields(sec->body, key);
    }
    auto* fronts = new Section(body, "Fronty i uchwyty");
    sections["fronts"] = fronts;
    combo(fronts->body, "front_style", "Styl frontu", json{{"Gładki", "smooth"}, {"Ramka frezowana", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}}, true);
    choice_tiles(fronts->body, "front_style", "Styl frontu", {{"Gładki", "Gładki"}, {"Ramka frezowana", "Ramkowy"}, {"Ryflowany", "Ryflowany"}, {"Chwyt frezowany", "Frezowany"}});
    preview = new tk::Canvas(fronts->body, {{"height", 112}, {"bg", "#f1eee8"}, {"highlightthickness", 0}});
    preview->pack({{"fill", "x"}, {"pady", 4}});
    preview->bind("<Configure>", plain([this](tk::Event&) { draw_preview(); }));
    field_pair(fronts->body, "front_thickness", "Grubość frontu", "gap", "Szczelina");
    json openings = {{"Uchwyt / gałka", "handle"}, {"Bez uchwytu", "none"}};
    for (auto& [k, v] : opening_options::OPTIONS.items()) if (k != "none") openings[at(v, "label").get<std::string>()] = k;
    combo(fronts->body, "handle_mode", "Otwieranie / chwyt", openings);
    json spacing = json::object();
    for (int x : {0, 64, 96, 128, 160, 192, 224, 256, 320}) spacing[std::to_string(x) + " mm"] = x;
    combo(fronts->body, "handle_spacing", "Rozstaw uchwytu", spacing);
    combo(fronts->body, "handle_color", "Kolor okucia", json{{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Złoty", "#bb9656"}, {"Biały", "#efefea"}}, true);
    choice_tiles(fronts->body, "handle_color", "Kolor okucia", {{"Czarny", "Czarny"}, {"Stal", "Stal"}, {"Złoty", "Złoty"}, {"Biały", "Biały"}});
    field_pair(fronts->body, "front_border", "Szer. ramki", "front_depth", "Głęb. frezu");
    field_pair(fronts->body, "front_radius", "Promień frezu", "flute_width", "Szer. ryfla");
    field_pair(fronts->body, "flute_spacing", "Rozstaw ryfli", "flute_count", "Ryfle (0 = auto)", false, true);
    field_pair(fronts->body, "finger_length", "Dł. chwytu", "finger_width", "Szer. chwytu");
    field(fronts->body, "finger_depth", "Głębokość chwytu");
    auto* fittings = new Section(body, "Zawiasy i prowadnice");
    sections["fittings"] = fittings;
    combo(fittings->body, "hinge_pattern", "Wiercenie zawiasu", json{{"Puszka 35 / rozstaw 45", "hinge45"}, {"Puszka 35 / rozstaw 52", "hinge52"}, {"Hettich Sensys 9071205", "hinge_sensys52"}}, true);
    choice_tiles(fittings->body, "hinge_pattern", "Wiercenie zawiasu", {{"Puszka 35 / rozstaw 45", "Ø35 · 45"}, {"Puszka 35 / rozstaw 52", "Ø35 · 52"}, {"Hettich Sensys 9071205", "Sensys"}});
    combo(fittings->body, "hinge_spring", "Sprężyna", json{{"Nieokreślona", "unknown"}, {"Ze sprężyną", "sprung"}, {"Bez sprężyny", "unsprung"}}, true);
    choice_tiles(fittings->body, "hinge_spring", "Sprężyna", {{"Nieokreślona", "Nieokreślona"}, {"Ze sprężyną", "Sprężyna"}, {"Bez sprężyny", "Bez"}});
    field_pair(fittings->body, "hinge_count", "Zawiasy na drzwi", "opening_angle", "Kąt otwarcia", true, false);
    json runners = {{"Uniwersalna boczna 12,7", ""}};
    for (auto& [key, profile] : runners_catalog::PROFILES.items()) runners[at(profile, "label").get<std::string>()] = key;
    combo(fittings->body, "runner_profile", "Prowadnica", runners);
    field(fittings->body, "runner_length", "Długość prowadnicy");
    field(fittings->body, "drawer_side_thickness", "Grubość boku skrzynki");
    field(fittings->body, "drawer_bottom_thickness", "Grubość dna skrzynki");
    (new ttk::Label(fittings->body, {{"textvariable", runner_note}, {"wraplength", 280}, {"font", "{Segoe UI} 9"}}))->pack({{"fill", "x"}, {"pady", 4}});
    auto* legs = new Section(body, "Nóżki, cokół i blat");
    sections["legs"] = legs;
    field(legs->body, "leg_height", "Wysokość nóżek (0 = brak)");
    combo(legs->body, "leg_count", "Liczba nóżek", json{{"4 sztuki", 4}, {"6 sztuk", 6}}, true);
    choice_tiles(legs->body, "leg_count", "Liczba nóżek", {{"4 sztuki", "4 szt."}, {"6 sztuk", "6 szt."}});
    field(legs->body, "leg_diameter", "Średnica nóżki");
    field(legs->body, "leg_setback", "Odsunięcie nóżek");
    combo(legs->body, "leg_color", "Kolor nóżek", json{{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Biały", "#efefea"}}, true);
    choice_tiles(legs->body, "leg_color", "Kolor nóżek", {{"Czarny", "Czarny"}, {"Stal", "Stal"}, {"Biały", "Biały"}});
    field(legs->body, "leg_price", "Cena nóżki [zł / szt.]");
    check(legs->body, "plinth", "Dodaj cokół");
    field(legs->body, "plinth_setback", "Cofnięcie cokołu");
    check(legs->body, "worktop", "Dodaj blat");
    field(legs->body, "worktop_thickness", "Grubość blatu");
    field(legs->body, "worktop_overhang", "Nawis blatu");
    combo(legs->body, "top_mode", "Góra korpusu", json{{"Pełna płyta", "panel"}, {"Listwy wzmacniające pod wspólny blat", "rails"}}, true);
    choice_tiles(legs->body, "top_mode", "Góra korpusu", {{"Pełna płyta", "Pełna płyta"}, {"Listwy wzmacniające pod wspólny blat", "Listwy"}});
    auto* led = new Section(body, "LED i rowki");
    sections["led"] = led;
    check(led->body, "led", "Rowek i profil pod półkami");
    combo(led->body, "led_mount", "Umiejscowienie", json{{"Pod półkami", "under_shelf"}, {"W boku korpusu", "side"}}, true);
    choice_tiles(led->body, "led_mount", "Umiejscowienie", {{"Pod półkami", "Pod półką"}, {"W boku korpusu", "W boku"}});
    field(led->body, "led_width", "Szerokość rowka");
    field(led->body, "led_depth", "Głębokość rowka");
    field(led->body, "led_margin", "Cofnięcie od frontu");
    check(led->body, "led_diffuser", "Dyfuzor profilu");
    field(led->body, "led_price_per_m", "Cena taśmy [zł / m]");
    auto* presets = new Section(body, "Presety i kopiowanie ustawień");
    sections["presets"] = presets;
    presetcombo = new ttk::Combobox(presets->body, {{"textvariable", presetvar}});
    presetcombo->pack({{"fill", "x"}, {"pady", 4}});
    auto* row = new ttk::Frame(presets->body);
    row->pack({{"fill", "x"}});
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"Zapisz", [this] { save_preset(); }}, {"Wczytaj", [this] { load_preset(); }}, {"Usuń", [this] { delete_preset(); }}})
        (new ttk::Button(row, {{"text", label}, {"command", fn}, {"padding", 4}}))->pack({{"side", "left"}, {"expand", true}, {"fill", "x"}});
    row = new ttk::Frame(presets->body);
    row->pack({{"fill", "x"}, {"pady", 4}});
    (new ttk::Button(row, {{"text", "Kopiuj ustawienia"}, {"command", Cmd([this] { copy_settings(); })}, {"padding", 4}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Button(row, {{"text", "Wklej do mebla"}, {"command", Cmd([this] { paste_settings(); })}, {"padding", 4}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    for (auto& f : fields) {
        if (f.flag) f.flag->trace_add("write", [this] { draft_changed(); });
        else f.text->trace_add("write", [this] { draft_changed(); });
    }
    reload_presets();
    scroll->install_wheel(body);
    show_tab("Korpus");
}

std::string QuickInspector::tab_of(const std::string& key) {
    if (key == "fronts") return "Fronty";
    if (key == "fittings") return "Okucia";
    if (key == "legs" || key == "led" || key == "presets") return "Dodatki";
    return "Korpus";
}

void QuickInspector::show_tab(const std::string& name) {
    current_tab = name;
    static const char* ORDER[] = {"type", "materials", "main", "bottom", "upper", "fronts", "fittings", "legs", "led", "presets"};
    for (const char* key : ORDER) {
        auto it = sections.find(key);
        if (it != sections.end()) it->second->pack_forget();
    }
    for (const char* key : ORDER) {
        auto it = sections.find(key);
        if (it != sections.end() && tab_of(key) == name) it->second->pack({{"fill", "x"}, {"pady", "3 2"}});
    }
    for (auto& [tab_name, button] : tab_buttons) button->configure({{"style", tab_name == name ? "ChipOn.TButton" : "Chip.TButton"}});
    if (scroll != nullptr && scroll->canvas != nullptr) scroll->canvas->yview_moveto(0);
}

void QuickInspector::draft_changed() {
    if (!syncing) has_draft = true;
}

void QuickInspector::reset_fields() {
    cancel_pending();
    set_config(config);
    note.set("Przywrócono pola do ostatnio zatwierdzonych wartości.");
}

bool QuickInspector::flush_draft() {
    if (has_draft && group && !group->empty() && py::truthy(furniture_configurator::read_config(*app->project, *group))) return apply(true);
    cancel_pending();
    return true;
}

tk::StringVar* QuickInspector::field(tk::Widget* parent, const std::string& key, const std::string& label, bool integer, bool text) {
    auto var = std::make_shared<tk::StringVar>();
    fields.push_back({key, var, nullptr, text ? "text" : integer ? "int" : "number"});
    // Wiersz: etykieta po lewej, pole po prawej, jednostka za polem (mm / ° / zł).
    auto* row = new ttk::Frame(parent);
    row->pack({{"fill", "x"}, {"pady", 2}});
    (new ttk::Label(row, {{"text", label}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "left"}});
    std::string unit;
    if (!text && !integer) {
        if (label.find("Kąt") != std::string::npos) unit = "°";
        else if (label.find("Cena") != std::string::npos) unit = label.find("zł / m") != std::string::npos ? "zł/m" : "zł";
        else unit = "mm";
    }
    if (!unit.empty()) (new ttk::Label(row, {{"text", unit}, {"foreground", forma_theme::MUTED}, {"width", 4}}))->pack({{"side", "right"}, {"padx", "4 0"}});
    auto* entry = new ttk::Entry(row, {{"textvariable", *var}, {"width", text ? 22 : 9}, {"justify", text ? "left" : "right"}});
    entry->pack({{"side", "right"}});
    entry->bind("<Return>", plain([this](tk::Event&) { apply(true); }));
    entry->bind("<FocusOut>", plain([this](tk::Event&) { schedule(); }));
    entry->bind("<Escape>", plain([this](tk::Event&) { reset_fields(); }));
    return var.get();
}

tk::StringVar* QuickInspector::field_cell(tk::Widget* cell, const std::string& key, const std::string& label, bool integer) {
    auto var = std::make_shared<tk::StringVar>();
    fields.push_back({key, var, nullptr, integer ? "int" : "number"});
    std::string unit;
    if (!integer) unit = label.find("Kąt") != std::string::npos ? "°" : "mm";
    std::string caption = unit.empty() ? label : label + " [" + unit + "]";
    (new ttk::Label(cell, {{"text", caption}, {"font", "{Segoe UI} 9"}, {"foreground", forma_theme::MUTED}}))->pack({{"anchor", "w"}});
    auto* entry = new ttk::Entry(cell, {{"textvariable", *var}, {"width", 6}, {"justify", "right"}});
    entry->pack({{"fill", "x"}});
    entry->bind("<Return>", plain([this](tk::Event&) { apply(true); }));
    entry->bind("<FocusOut>", plain([this](tk::Event&) { schedule(); }));
    entry->bind("<Escape>", plain([this](tk::Event&) { reset_fields(); }));
    return var.get();
}

void QuickInspector::field_pair(tk::Widget* parent, const std::string& key_a, const std::string& label_a, const std::string& key_b, const std::string& label_b, bool int_a, bool int_b) {
    auto* row = new ttk::Frame(parent);
    row->pack({{"fill", "x"}, {"pady", 3}});
    row->columnconfigure(0, {{"weight", 1}, {"uniform", "pairfield"}});
    row->columnconfigure(1, {{"weight", 1}, {"uniform", "pairfield"}});
    auto* left = new ttk::Frame(row);
    left->grid({{"row", 0}, {"column", 0}, {"sticky", "ew"}, {"padx", "0 4"}});
    auto* right = new ttk::Frame(row);
    right->grid({{"row", 0}, {"column", 1}, {"sticky", "ew"}, {"padx", "4 0"}});
    field_cell(left, key_a, label_a, int_a);
    field_cell(right, key_b, label_b, int_b);
}

ttk::Combobox* QuickInspector::combo(tk::Widget* parent, const std::string& key, const std::string& label, const json& choices, bool hidden) {
    if (!hidden) (new ttk::Label(parent, {{"text", label}, {"font", "{Segoe UI} 9"}}))->pack({{"anchor", "w"}, {"pady", "4 1"}});
    auto var = std::make_shared<tk::StringVar>();
    auto* widget = new ttk::Combobox(parent, {{"textvariable", *var}, {"values", keys_of(choices)}, {"state", "readonly"}});
    if (!hidden) widget->pack({{"fill", "x"}});
    fields.push_back({key, var, nullptr, "choice"});
    combos[key] = {widget, choices};
    widget->bind("<<ComboboxSelected>>", plain([this, key](tk::Event&) { choice_changed(key); }));
    return widget;
}

void QuickInspector::draw_tile(const Tile& tile) {
    tk::Canvas* c = tile.canvas;
    if (c == nullptr) return;
    try {
        bool on = find(tile.key).text->get() == tile.choice;
        double w = std::max(40, c->winfo_width());
        c->delete_("all");
        c->create_rectangle(1, 1, w - 2, 61, {{"fill", on ? forma_theme::ACCENT_SOFT : forma_theme::CARD}, {"outline", on ? forma_theme::ACCENT : forma_theme::BORDER}, {"width", on ? 2 : 1}});
        const double cx = w / 2, L = cx - 18, R = cx + 18, T = 8, B = 38, M = (T + B) / 2;
        const char* wood = "#e8d2ab";
        const char* edge = "#7a6544";
        const char* dark = "#8c795a";
        auto rect = [&](double x1, double y1, double x2, double y2, const char* fill) { c->create_rectangle(x1, y1, x2, y2, {{"fill", fill}, {"outline", edge}}); };
        auto dot = [&](double x, double y, double r, const std::string& fill) { c->create_oval(x - r, y - r, x + r, y + r, {{"fill", fill}, {"outline", edge}}); };
        auto line = [&](double x1, double y1, double x2, double y2, const char* color, int width) { c->create_line({x1, y1, x2, y2}, {{"fill", color}, {"width", width}}); };
        static const std::map<std::string, std::string> COLORS = {{"Czarny", "#242628"}, {"Stal", "#aab0b6"}, {"Biały", "#efefea"}, {"Złoty", "#bb9656"}};
        const std::string& k = tile.key;
        const std::string& ch = tile.choice;
        if (k == "front_style") {
            rect(cx - 17, T, cx + 17, B, wood);
            if (ch == "Ramka frezowana") rect(cx - 11, T + 6, cx + 11, B - 6, "#d9b98a");
            else if (ch == "Ryflowany") for (double x = cx - 12; x < cx + 15; x += 5) line(x, T + 2, x, B - 2, dark, 2);
            else if (ch == "Chwyt frezowany") c->create_rectangle(cx - 12, T + 2, cx + 12, T + 6, {{"fill", dark}, {"outline", ""}});
        } else if (k.ends_with(".front")) {
            rect(L, T, R, B, "#f4efe6");
            if (ch == "Otwarte") {
                line(L + 3, M, R - 3, M, edge, 2);
            } else if (ch == "Drzwi") {
                rect(L + 2, T + 2, cx - 1, B - 2, wood);
                rect(cx + 1, T + 2, R - 2, B - 2, wood);
                dot(cx - 4, M, 1.5, dark);
                dot(cx + 4, M, 1.5, dark);
            } else {
                for (int i = 0; i < 3; ++i) {
                    double y = T + 2 + i * 9.3;
                    rect(L + 2, y, R - 2, y + 8, wood);
                    line(cx - 5, y + 4, cx + 5, y + 4, dark, 2);
                }
            }
        } else if (k.ends_with(".doors")) {
            rect(L, T, R, B, "#f4efe6");
            if (ch == "1 skrzydło") {
                rect(L + 2, T + 2, R - 2, B - 2, wood);
                dot(R - 6, M, 1.5, dark);
            } else {
                rect(L + 2, T + 2, cx - 1, B - 2, wood);
                rect(cx + 1, T + 2, R - 2, B - 2, wood);
                dot(cx - 4, M, 1.5, dark);
                dot(cx + 4, M, 1.5, dark);
            }
        } else if (k == "hinge_pattern") {
            double s = ch.find("45") != std::string::npos ? 9 : 11;
            rect(L + 2, T, L + 10, B, wood);
            dot(L + 20, M, 8, "#cfd3d6");
            dot(L + 20, M, 3, "#9aa0a6");
            dot(R - 3, M - s, 2, dark);
            dot(R - 3, M + s, 2, dark);
            if (ch.find("Sensys") != std::string::npos) line(L + 10, M - 5, R - 3, M - s, dark, 1);
        } else if (k == "hinge_spring") {
            rect(L + 2, T + 2, L + 7, B - 2, wood);
            if (ch == "Ze sprężyną") {
                c->create_line({L + 8, M, L + 14, M - 7, L + 20, M + 7, L + 26, M - 7, L + 32, M + 7, R - 2, M}, {{"fill", edge}, {"width", 2}});
            } else if (ch == "Bez sprężyny") {
                line(L + 8, M, R - 2, M, edge, 2);
            } else {
                c->create_text(cx + 5, M, {{"text", "?"}, {"fill", dark}, {"font", "{Segoe UI} 16 bold"}});
            }
        } else if (k == "leg_count") {
            rect(L, T + 4, R, B - 8, wood);
            std::vector<double> xs = {L + 3, R - 3};
            if (ch == "6 sztuk") xs.insert(xs.begin() + 1, cx);
            for (double x : xs) dot(x, B - 2, 2.5, "#242628");
        } else if (k == "leg_color" || k == "handle_color") {
            auto it = COLORS.find(ch);
            dot(cx, M, 12, it != COLORS.end() ? it->second : "#cccccc");
        } else if (k == "top_mode") {
            if (ch == "Pełna płyta") {
                rect(L, M - 5, R, M + 5, wood);
            } else {
                rect(L, M - 8, R, M - 3, wood);
                rect(L, M + 3, R, M + 8, wood);
            }
        } else if (k == "led_mount") {
            if (ch == "Pod półkami") {
                rect(L, T, R, T + 3, wood);
                c->create_rectangle(L + 4, T + 4, R - 4, T + 7, {{"fill", "#f0c040"}, {"outline", ""}});
                for (double x = L + 6; x < R - 4; x += 8) line(x, T + 9, x - 2, T + 18, "#f6e3a0", 1);
            } else {
                rect(L, T, L + 5, B, wood);
                c->create_rectangle(L + 6, T + 3, L + 9, B - 3, {{"fill", "#f0c040"}, {"outline", ""}});
                for (double y = T + 6; y < B - 4; y += 8) line(L + 11, y, L + 20, y + 2, "#f6e3a0", 1);
            }
        }
        c->create_text(w / 2, 50, {{"text", tile.label}, {"fill", on ? forma_theme::ACCENT : forma_theme::TEXT}, {"font", on ? "{Segoe UI} 9 bold" : "{Segoe UI} 9"}});
    } catch (const tk::TclError&) {
    }
}

void QuickInspector::choice_tiles(tk::Widget* parent, const std::string& key, const std::string& caption, const std::vector<std::pair<std::string, std::string>>& items) {
    forma_widgets::caption(parent, caption);
    auto* row = new tk::Frame(parent, {{"bg", forma_theme::PANEL}});
    row->pack({{"fill", "x"}, {"pady", "2 4"}});
    int column = 0;
    for (const auto& [choice, label] : items) {
        auto* canvas = new tk::Canvas(row, {{"height", 62}, {"width", 40}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
        row->columnconfigure(column, {{"weight", 1}, {"uniform", "tile" + key}});
        canvas->grid({{"row", 0}, {"column", column++}, {"padx", 2}, {"sticky", "ew"}});
        tiles.push_back({key, choice, label, canvas});
        size_t index = tiles.size() - 1;
        canvas->bind("<Configure>", plain([this, index](tk::Event&) { draw_tile(tiles[index]); }));
        canvas->bind("<Button-1>", [this, key, choice](tk::Event&) {
            try {
                find(key).text->set(choice);
                choice_changed(key);
            } catch (const tk::TclError&) {
            }
            return std::string();
        });
    }
    find(key).text->trace_add("write", [this, key] {
        for (const auto& tile : tiles) if (tile.key == key) draw_tile(tile);
    });
}

void QuickInspector::rebuild_swatches() {
    if (swatch_row == nullptr || swatches_pending) return;
    swatches_pending = true;
    app->after(1, [this] {
        swatches_pending = false;
        build_swatches_now();
    });
}

void QuickInspector::build_swatches_now() {
    if (swatch_row == nullptr) return;
    try {
        std::string current = find("material").text->get();
        std::string signature = current;
        for (const auto& m : app->project->materials) signature += "|" + m.name + m.color;
        if (signature == swatch_signature) return;
        swatch_signature = signature;
        for (tk::Widget* child : swatch_row->winfo_children()) child->destroy();
        int index = 0;
        for (const auto& m : app->project->materials) {
            bool on = m.name == current;
            auto* cell = new tk::Canvas(swatch_row, {{"width", 34}, {"height", 34}, {"bg", forma_theme::PANEL}, {"highlightthickness", 0}, {"cursor", "hand2"}});
            cell->grid({{"row", index / 7}, {"column", index % 7}, {"padx", 2}, {"pady", 2}});
            cell->create_rectangle(3, 3, 31, 31, {{"fill", m.color}, {"outline", on ? forma_theme::ACCENT : forma_theme::BORDER}, {"width", on ? 3 : 1}});
            if (on) cell->create_text(17, 17, {{"text", "✓"}, {"fill", "white"}, {"font", "{Segoe UI} 10 bold"}});
            std::string name = m.name;
            cell->bind("<Button-1>", [this, name](tk::Event&) {
                try {
                    find("material").text->set(name);
                    choice_changed("material");
                } catch (const tk::TclError&) {
                }
                return std::string();
            });
            ++index;
        }
    } catch (const tk::TclError&) {
    }
}

void QuickInspector::choice_changed(const std::string& key) {
    if (key.ends_with(".front") && jget(combos.at(key).second, find(key).text->get()) == json("drawers"))
        find(key.substr(0, key.find('.')) + ".shelves").text->set("0");
    apply(false);
}

void QuickInspector::check(tk::Widget* parent, const std::string& key, const std::string& label) {
    auto var = std::make_shared<tk::BooleanVar>();
    fields.push_back({key, nullptr, var, "bool"});
    (new ttk::Checkbutton(parent, {{"text", label}, {"variable", *var}, {"command", Cmd([this] { apply(false); })}}))->pack({{"anchor", "w"}, {"pady", 3}});
}

void QuickInspector::section_fields(tk::Widget* parent, const std::string& key) {
    field_pair(parent, key + ".columns", "Komory pionowe", key + ".shelves", "Półki na komorę", true, true);
    combo(parent, key + ".front", "Wypełnienie", json{{"Otwarte", "open"}, {"Drzwi", "doors"}, {"Szuflady", "drawers"}}, true);
    choice_tiles(parent, key + ".front", "Wypełnienie", {{"Otwarte", "Otwarte"}, {"Drzwi", "Drzwi"}, {"Szuflady", "Szuflady"}});
    combo(parent, key + ".doors", "Drzwi na komorę", json{{"1 skrzydło", 1}, {"2 skrzydła", 2}}, true);
    choice_tiles(parent, key + ".doors", "Drzwi na komorę", {{"1 skrzydło", "1 skrzydło"}, {"2 skrzydła", "2 skrzydła"}});
    field(parent, key + ".drawers", "Szuflady na komorę", true);
    combo(parent, key + ".front_style", "Styl tej strefy",
          json{{"Wspólny styl frontów", nullptr}, {"Gładki", "smooth"}, {"Ramka frezowana", "frame"}, {"Ryflowany", "fluted"}, {"Chwyt frezowany", "finger"}});
}

void QuickInspector::cancel_pending() {
    if (job) {
        after_cancel(*job);
        job.reset();
    }
}

void QuickInspector::schedule() {
    if (syncing || !group || group->empty()) return;
    cancel_pending();
    int gen = app->project_generation;
    std::string grp = *group;
    job = after(250, [this, gen, grp] {
        if (gen == app->project_generation && group && grp == *group) apply(false);
    });
}

void QuickInspector::choose_group() {
    std::string grp = groupvar.get();
    if (!flush_draft()) {
        groupvar.set(group ? *group : "None");
        return;
    }
    cancel_pending();
    core::Part* p = nullptr;
    for (auto& q : app->project->parts) if (q.group == grp) { p = &q; break; }
    if (p) {
        app->selected = p->id;
        app->tree->selection_set(p->id);
        auto it = app->open_amounts.find(p->id);
        app->motion_value.set((it == app->open_amounts.end() ? 0.0 : it->second) * 100);
        app->load_form();
        app->draw();
    }
}

void QuickInspector::change_template() {
    int i = template_indices.at(templatecombo->current());
    json defaults = furniture_configurator::default_config(i, *app->project);
    if (group && !group->empty()) {
        // Explicit type choice retains dimensions, materials and layout.
        json current = draft();
        for (const char* key : {"name", "width", "height", "depth", "thickness", "front_thickness", "material", "front_material", "leg_height", "leg_count",
                                "leg_diameter", "leg_setback", "plinth", "worktop", "worktop_thickness", "worktop_overhang"})
            if (current.contains(key)) defaults[key] = current[key];
    }
    set_config(defaults);
    apply(false);
}

json QuickInspector::draft() {
    json cfg = config;
    // sections = {s['key']: s for s in cfg.get('sections', [])}
    std::vector<std::pair<std::string, json>> secs;
    for (auto& s : jget(cfg, "sections", json::array())) {
        std::string skey = py::str(at(s, "key"));
        auto it = std::find_if(secs.begin(), secs.end(), [&](auto& e) { return e.first == skey; });
        if (it == secs.end()) secs.emplace_back(skey, s);
        else it->second = s;
    }
    for (auto& f : fields) {
        const std::string& key = f.key;
        json value;
        if (f.kind == "bool") {
            value = f.flag->get();
        } else {
            std::string raw = f.text->get();
            if (f.kind == "choice") {
                const json& choices = combos.at(key).second;
                if (!choices.contains(raw)) throw py::ValueError("Wybierz ustawienie: " + key);
                value = choices[raw];
            } else if (f.kind == "text") {
                value = py::strip(raw);
            } else {
                double number;
                try {
                    number = py::to_float(py::replace_all(raw, ",", "."));
                } catch (const py::ValueError&) {
                    throw py::ValueError("Wpisz liczbę w polu " + key);
                }
                if (f.kind == "int") {
                    if (std::isnan(number)) throw py::ValueError("cannot convert float NaN to integer");
                    if (std::isinf(number)) throw py::ValueError("cannot convert float infinity to integer");
                    if (std::trunc(number) != number) throw py::ValueError("Wpisz całkowitą liczbę: " + key);
                    value = std::fabs(number) < 9e18 ? json((long long)number) : json(number);
                } else {
                    value = number;
                }
            }
        }
        auto dot = key.find('.');
        if (dot != std::string::npos) {
            std::string section = key.substr(0, dot), fld = key.substr(dot + 1);
            auto it = std::find_if(secs.begin(), secs.end(), [&](auto& e) { return e.first == section; });
            if (it == secs.end()) throw py::KeyError("'" + section + "'");
            if (fld == "front_style" && value.is_null()) it->second.erase(fld);
            else it->second[fld] = value;
        } else {
            cfg[key] = value;
        }
    }
    json list = json::array();
    for (auto& [k, s] : secs) list.push_back(s);
    cfg["sections"] = list;
    return cfg;
}

void QuickInspector::set_config(const json& cfg_in) {
    perf::Scope perf_scope("QuickInspector::set_config");
    json cfg = cfg_in;
    syncing = true;
    config = cfg;
    json sections_ = json::object();
    for (auto& s : jget(cfg, "sections", json::array())) sections_[py::str(at(s, "key"))] = s;
    const json defaults = {{"front_border", 60}, {"front_depth", 4}, {"flute_spacing", 24}, {"hinge_spring", "unknown"}, {"leg_setback", 55}, {"worktop", false},
                           {"worktop_thickness", 38}, {"worktop_overhang", 20}, {"led", false}, {"led_width", 15}, {"led_depth", 5}, {"led_margin", 50},
                           {"led_diffuser", true}, {"handle_color", "#242628"}};
    for (auto& f : fields) {
        const std::string& key = f.key;
        json value;
        auto dot = key.find('.');
        if (dot != std::string::npos) value = jget(jget(sections_, key.substr(0, dot), json::object()), key.substr(dot + 1), 0);
        else value = jget(cfg, key, jget(defaults, key, ""));
        if (f.kind == "choice") {
            const json& choices = combos.at(key).second;
            auto label = label_of(choices, value);
            if (!label) label = choices.empty() ? std::string() : choices.items().begin().key();
            f.text->set(*label);
        } else if (f.kind == "bool") {
            f.flag->set(py::truthy(value));
        } else {
            f.text->set(show_value(value));
        }
    }
    json idx = jget(cfg, "template_index", 0);
    if (idx.is_number_integer()) {
        auto it = std::find(template_indices.begin(), template_indices.end(), idx.get<long long>());
        if (it != template_indices.end()) templatecombo->current((int)(it - template_indices.begin()));
    }
    syncing = false;
    has_draft = false;
    {
        // Karta mebla u góry panelu: nazwa i gabaryty.
        auto number_of = [&](const char* key) -> std::string {
            json v = jget(cfg, key, json());
            return v.is_number() ? std::format("{:g}", v.get<double>()) : std::string("—");
        };
        auto number_value = [&](const char* key) -> double {
            json v = jget(cfg, key, json());
            return v.is_number() ? v.get<double>() : 0.0;
        };
        json name = jget(cfg, "name", json());
        card_title.set(name.is_string() && !name.get<std::string>().empty() ? name.get<std::string>() : std::string("Mebel"));
        card_sub.set(number_of("width") + " × " + number_of("height") + " × " + number_of("depth") + " mm");
        draw_card_icon(card_icon, number_value("width"), number_value("height"), number_value("depth"));
        app->props_title.set(card_title.get() == "Mebel" ? std::string("Właściwości") : "Właściwości: " + card_title.get());
        if (side_panels::is_open("item") && side_panels::item_stale()) side_panels::hide(app);  // zaznaczenie w scenie wraca do właściwości mebla
    }
    rebuild_swatches();
    draw_preview();
}

void QuickInspector::refresh() {
    perf::Scope perf_scope("QuickInspector::refresh");
    using namespace furniture_configurator;
    cancel_pending();
    if (generation != app->project_generation) clipboard.reset();
    generation = app->project_generation;
    auto groups = unique_groups(*app->project);
    groupcombo->configure({{"values", groups}});
    core::Part* p = app->selected_part();
    group.reset();
    if (p) group = p->group;
    bool has_group = group && !group->empty();
    groupvar.set(has_group ? *group : "Nowy mebel — wybierz typ");
    json materials = json::object();
    for (auto& m : app->project->materials) materials[m.name] = m.id;
    for (const char* key : {"material", "front_material"}) {
        auto* widget = combos.at(key).first;
        combos[key] = {widget, materials};
        widget->configure({{"values", keys_of(materials)}});
    }
    json cfg = has_group ? read_config(*app->project, *group) : json(nullptr);
    if (py::truthy(cfg)) {
        set_config(cfg);
        json status = configuration_status(*app->project, *group);
        bool editable = py::truthy(jget(status, "editable", false));
        note.set(editable ? "Zmiana listy: od razu. Liczby: Enter lub wyjście z pola." : py::str(jget(status, "reason", "Mebel zmieniono ręcznie.")));
        apply_button->configure({{"text", "Zastosuj do mebla"}, {"state", editable ? "normal" : "disabled"}});
    } else if (has_group) {
        json corner = corner_furniture::read_corner_config(*app->project, group);
        json shared = corner.is_null() ? workspace_worktops::read_shared_worktop(*app->project, *group) : json(nullptr);
        json draft_ = default_config(0, *app->project);
        draft_["name"] = *group;
        if (!corner.is_null()) {
            // Keep this regular-cabinet inspector informational for a
            // corner, but display the actual selected module parameters.
            for (const char* key : {"width", "depth", "height", "thickness", "material", "front_material", "front_thickness", "leg_height", "leg_count",
                                    "leg_diameter", "leg_setback", "leg_color", "leg_price", "top_mode", "rail_width", "gap", "front_style", "handle_mode",
                                    "handle_spacing", "hinge_pattern", "hinge_count", "opening_angle"})
                if (corner.contains(key)) draft_[key] = corner[key];
            json& zone = draft_["sections"][1];
            zone["shelves"] = jget(corner, "shelves", 0);
            zone["front"] = jget(corner, "fronts", "open") == json("open") ? "open" : "doors";
            zone["doors"] = jget(corner, "fronts") == json("orthogonal") ? 2 : 1;
        } else if (py::truthy(shared)) {
            const core::Part* top = nullptr;
            for (auto& q : app->project->parts)
                if (q.group == *group && py::truthy(py::get(q.assembly, "shared_worktop"))) { top = &q; break; }
            if (!top) throw py::StopIteration();
            draft_["width"] = top->u; draft_["depth"] = top->v; draft_["height"] = top->t; draft_["thickness"] = top->t; draft_["front_thickness"] = top->t;
            draft_["material"] = top->material; draft_["front_material"] = top->material; draft_["leg_height"] = 0; draft_["back"] = false;
            json& zone = draft_["sections"][1];
            zone["shelves"] = 0;
            zone["front"] = "open";
        }
        set_config(draft_);
        if (!corner.is_null()) {
            templatevar.set(std::string("Narożnik · ") + (jget(corner, "furniture_type") == json("wardrobe") ? "szafa" : "szafka"));
            note.set("Narożnik: aktualne wymiary całego mebla. Edytuj ramiona, kierunek, obrys i fronty w zakładce Narożnik.");
            apply_button->configure({{"text", "Edytuj w zakładce Narożnik"}, {"state", "disabled"}});
        } else if (py::truthy(shared)) {
            const core::Part* top = nullptr;
            for (auto& q : app->project->parts)
                if (q.group == *group && py::truthy(py::get(q.assembly, "shared_worktop"))) { top = &q; break; }
            templatevar.set("Wspólny blat");
            note.set(std::format("Wspólny blat: wymiary formatki, wysokość oznacza grubość. Poziom blatu {:g} mm. PPM → Dopasuj blat lub panel Elementy / CAD.", top->z));
            apply_button->configure({{"text", "Edytuj przez menu PPM"}, {"state", "disabled"}});
        } else {
            note.set("Mebel z wcześniejszej biblioteki. Przycisk spróbuje przejąć go do kreatora; szkice pozostają w panelu Elementy / CAD.");
            apply_button->configure({{"text", "Przejmij mebel do kreatora"}, {"state", "normal"}});
        }
    } else {
        set_config(default_config(0, *app->project));
        note.set("Wybierz typ, ustaw wymiary i kliknij + Nowy.");
        apply_button->configure({{"text", "Utwórz mebel"}, {"state", "normal"}});
    }
    std::vector<json> summaries;
    if (p) {
        std::set<std::string> ids;
        for (auto& q : app->project->parts) if (group && q.group == *group) ids.insert(q.id);
        for (auto& h : app->project->hardware) {
            json owner = jget(h, "owner_id");
            if (owner.is_string() && ids.count(owner.get<std::string>()) && jget(h, "kind") == json("runner") && py::truthy(jget(h, "dimensions")))
                summaries.push_back(h["dimensions"]);
        }
    }
    if (!summaries.empty()) {
        const json& d = summaries[0];
        runner_note.set("Skrzynka zewn. " + py::str(jget(d, "drawer_outer_width", "—")) + " mm · wewn. " + py::str(jget(d, "drawer_inner_width", "—")) +
                        " mm\nLuz na stronę " + py::str(jget(d, "external_clearance_per_side", "—")) + " mm");
    } else {
        runner_note.set("Wymiary skrzynki i luz prowadnic przeliczają się po zmianie mebla.");
    }
}

bool QuickInspector::apply(bool explicit_) {
    if (syncing) return false;
    cancel_pending();
    using namespace furniture_configurator;
    try {
        if (generation != app->project_generation) throw py::ValueError("Projekt zmienił się. Wybierz mebel ponownie.");
        bool has_group = group && !group->empty();
        if (has_group && read_config(*app->project, *group).is_null()) {
            if (!explicit_) return false;
            json inferred = infer_config(*app->project, *group);
            set_config(inferred);
        }
        json cfg = draft();
        json previous = has_group ? read_config(*app->project, *group) : json(nullptr);
        if (py::truthy(previous) && normalize_config(cfg, *app->project) == normalize_config(previous, *app->project)) {
            has_draft = false;
            return true;
        }
        auto [result, new_group] = furniture_configurator::configure(*app->project, cfg, group);
        if (result->data() == app->project->data()) return true;
        app->snapshot();
        app->project = result;
        app->open_amounts = {};
        app->motion_value.set(0);
        std::optional<std::string> chosen;
        for (auto& q : result->parts) if (q.group == new_group) { chosen = q.id; break; }
        if (!chosen) throw py::StopIteration();
        app->selected = chosen;
        app->refresh();
        app->status.set("Przeliczono " + new_group + " · model, formatki i okucia zsynchronizowane");
        return true;
    } catch (const py::ValueError& e) {
        note.set(e.what());
        app->status.set(std::string("Kreator: ") + e.what());
        if (explicit_) tk::messagebox::showerror("Kreator mebla", e.what(), app);
    } catch (const py::KeyError& e) {
        note.set(e.what());
        app->status.set(std::string("Kreator: ") + e.what());
        if (explicit_) tk::messagebox::showerror("Kreator mebla", e.what(), app);
    } catch (const py::StopIteration& e) {
        note.set("");
        app->status.set("Kreator: ");
        if (explicit_) tk::messagebox::showerror("Kreator mebla", "", app);
    }
    return false;
}

std::optional<std::string> QuickInspector::new_furniture() {
    try {
        json cfg = draft();
        auto [result, new_group] = furniture_configurator::configure(*app->project, cfg);
        // Place a newly generated module next to the current set.
        if (!app->project->parts.empty()) {
            double right = 0;
            bool first = true;
            for (auto& g : unique_groups(*app->project)) {
                double hi_x = furniture_layout::group_bounds(*app->project, g).second[0];
                if (first || hi_x > right) right = hi_x;
                first = false;
            }
            double lo_x = furniture_layout::group_bounds(*result, new_group).first[0];
            result = furniture_layout::move_group(*result, new_group, right + 50 - lo_x);
        }
        std::optional<std::string> chosen;
        for (auto& q : result->parts) if (q.group == new_group) { chosen = q.id; break; }
        if (!chosen) throw py::StopIteration();
        app->snapshot();
        app->project = result;
        app->selected = chosen;
        app->refresh();
        app->fit();
        return new_group;
    } catch (const py::ValueError& e) {
        note.set(e.what());
        tk::messagebox::showerror("Nowy mebel", e.what(), app);
    } catch (const py::KeyError& e) {
        note.set(e.what());
        tk::messagebox::showerror("Nowy mebel", e.what(), app);
    }
    return std::nullopt;
}

bool QuickInspector::quick_front(const std::string& mode, int count) {
    auto label = label_of(combos.at("main.front").second, mode);
    if (!label) throw py::StopIteration();
    find("main.front").text->set(*label);
    if (count) find("main.drawers").text->set(std::to_string(count));
    if (mode == "drawers") find("main.shelves").text->set("0");
    return apply(false);
}

void QuickInspector::duplicate_group() {
    if (!group || group->empty()) return;
    try {
        auto [lo, hi] = furniture_layout::group_bounds(*app->project, *group);
        std::string name = *group + " — kopia";
        int n = 2;
        std::set<std::string> occupied;
        for (auto& p : app->project->parts) occupied.insert(p.group);
        while (occupied.count(name)) {
            name = *group + std::format(" — kopia {}", n);
            ++n;
        }
        auto extracted = productivity::extract_group(*app->project, *group);
        auto result = productivity::insert_template(*app->project, *extracted, name, json::array({hi[0] - lo[0] + 50, 0, 0}));
        std::optional<std::string> chosen;
        for (auto& q : result->parts) if (q.group == name) { chosen = q.id; break; }
        if (!chosen) throw py::StopIteration();
        app->snapshot();
        app->project = result;
        app->selected = chosen;
        app->refresh();
        app->fit();
    } catch (const py::ValueError& e) {
        note.set(e.what());
    }
}

void QuickInspector::delete_group() {
    if (!group || group->empty()) return;
    std::set<std::string> ids;
    for (auto& p : app->project->parts) if (p.group == *group) ids.insert(p.id);
    app->snapshot();
    std::erase_if(app->project->parts, [&](const core::Part& p) { return ids.count(p.id) > 0; });
    json kept = json::array();
    for (auto& h : app->project->hardware) {
        json owner = jget(h, "owner_id");
        bool owned = owner.is_string() && ids.count(owner.get<std::string>());
        if (!owned && jget(h, "group") != json(*group)) kept.push_back(h);
    }
    app->project->hardware = kept;
    app->selected.reset();
    app->refresh();
    app->fit();
}

void QuickInspector::copy_settings() {
    try {
        clipboard = draft();
        note.set("Skopiowano ustawienia. Wybierz inny mebel i wklej.");
    } catch (const py::ValueError& e) {
        note.set(e.what());
    }
}

void QuickInspector::paste_settings() {
    if (!clipboard || !py::truthy(*clipboard)) {
        note.set("Najpierw skopiuj ustawienia mebla.");
        return;
    }
    json cfg = *clipboard;
    cfg["name"] = (group && !group->empty()) ? json(*group) : at(cfg, "name");
    set_config(cfg);
    apply(true);
}

void QuickInspector::reload_presets() {
    try {
        saved = configurator_presets::load_presets();
    } catch (const py::OSError& e) {
        saved = json::object();
        note.set(e.what());
    } catch (const py::ValueError& e) {
        saved = json::object();
        note.set(e.what());
    }
    presetcombo->configure({{"values", keys_of(saved)}});
}

void QuickInspector::save_preset() {
    try {
        json cfg = draft();
        json names = json::object();
        for (auto& m : app->project->materials) names[m.id] = m.name;
        cfg["_material_names"] = names;
        configurator_presets::save_preset(presetvar.get(), cfg);
        reload_presets();
        note.set("Zapisano własny preset lokalnie.");
    } catch (const py::ValueError& e) {
        note.set(e.what());
    } catch (const py::OSError& e) {
        note.set(e.what());
    }
}

void QuickInspector::load_preset() {
    std::string name = presetvar.get();
    if (!saved.contains(name)) {
        note.set("Wybierz zapisany preset z listy.");
        return;
    }
    json cfg = saved[name];
    cfg["name"] = (group && !group->empty()) ? json(*group) : at(cfg, "name");
    json names = jget(cfg, "_material_names", json::object());
    cfg.erase("_material_names");
    json available = json::object();
    for (auto& m : app->project->materials) available[m.name] = m.id;
    for (const char* key : {"material", "front_material"}) {
        json current = jget(cfg, key);
        if (!current.is_string() || !names.contains(current.get<std::string>())) continue;
        json mapped = names[current.get<std::string>()];
        if (mapped.is_string() && available.contains(mapped.get<std::string>())) cfg[key] = available[mapped.get<std::string>()];
    }
    set_config(cfg);
    apply(true);
}

void QuickInspector::delete_preset() {
    try {
        configurator_presets::delete_preset(presetvar.get());
        reload_presets();
        presetvar.set("");
    } catch (const py::ValueError& e) {
        note.set(e.what());
    } catch (const py::OSError& e) {
        note.set(e.what());
    }
}

void QuickInspector::draw_preview() {
    perf::Scope perf_scope("QuickInspector::draw_preview");
    auto* c = preview;
    c->delete_("all");
    int w = std::max(280, c->winfo_width());
    json style_value = jget(combos.at("front_style").second, find("front_style").text->get(), "smooth");
    std::string style = style_value.is_string() ? style_value.get<std::string>() : "";
    c->create_rectangle(20, 10, 100, 102, {{"fill", "#c7b28c"}, {"outline", "#807151"}, {"width", 2}});
    if (style == "frame") {
        c->create_rectangle(30, 21, 90, 91, {{"fill", "#b49c77"}, {"outline", "#8c795a"}, {"width", 2}});
    } else if (style == "fluted") {
        for (int x = 26; x < 99; x += 7) c->create_line({(double)x, 13, (double)x, 99}, {{"fill", "#8c795a"}, {"width", 2}});
    } else if (style == "finger") {
        c->create_rectangle(35, 14, 85, 22, {{"fill", "#5e5341"}, {"outline", "#5e5341"}});
    }
    c->create_text(117, 16, {{"text", "Podgląd frontu"}, {"anchor", "w"}, {"fill", "#243144"}, {"font", "{Segoe UI} 9 bold"}});
    using P = std::pair<double, double>;
    std::vector<P> profile = {{119, 72}, {w - 18.0, 72}, {w - 18.0, 86}, {119, 86}};
    if (style == "frame") profile = {{119, 72}, {143, 72}, {143, 78}, {w - 43.0, 78}, {w - 43.0, 72}, {w - 18.0, 72}, {w - 18.0, 86}, {119, 86}};
    else if (style == "finger") profile = {{119, 72}, {135, 72}, {135, 80}, {153, 80}, {153, 72}, {w - 18.0, 72}, {w - 18.0, 86}, {119, 86}};
    std::vector<double> flat;
    for (auto& [x, y] : profile) { flat.push_back(x); flat.push_back(y); }
    c->create_polygon(flat, {{"fill", "#c7b28c"}, {"outline", "#807151"}});
    c->create_text(117, 99, {{"text", "Schemat przekroju · frez od frontu"}, {"anchor", "w"}, {"fill", "#6b7266"}, {"font", "{Segoe UI} 8"}});
}

// Zakładka „Ustawienia" (po prawej od Rozkroju): program, dane produkcyjne, informacje.
static void build_settings_page(App* app) {
    auto* page = new ttk::Frame(app->tabs, {{"padding", "40 24"}});
    app->tabs->add(page, {{"text", "Ustawienia"}});
    (new ttk::Label(page, {{"text", "Ustawienia"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}});
    (new ttk::Label(page, {{"text", "Ustawienia programu zapisują się na tym komputerze i nie zmieniają projektu mebla."}, {"style", "Muted.TLabel"}}))->pack({{"anchor", "w"}, {"pady", "2 14"}});
    struct Card { std::string title, text, button; Cmd command; bool accent; };
    std::vector<Card> cards = {
        {"Okno, skala i panele", "Rozmiar okna, skala interfejsu, widoczność paneli. Silnik i jakość 3D ustawisz też w panelu Widok.", "Otwórz ustawienia", [app] { ui_settings::open_settings(app); }, true},
        {"Materiały i ceny produkcji", "Ceny płyt, obrzeży i robocizny, narzut oraz koszty używane w kosztorysie.", "Zmień ceny i materiały", [app] { app->settings(); }, false},
        {"Pomoc", "Skróty myszy, opis trybów pracy i podstawowe procedury.", "Otwórz pomoc", [app] { app->help(); }, false},
        {"Silnik C++ i stan migracji", "Informacje o silniku, module CAD i zgodności z wersją pierwotną.", "Pokaż stan", [app] { migration_status::open_status(app); }, false}};
    for (auto& card : cards) {
        auto* box = new ttk::Frame(page, {{"style", "Card.TFrame"}, {"padding", "16 12"}});
        box->pack({{"anchor", "w"}, {"fill", "x"}, {"pady", 5}});
        (new ttk::Label(box, {{"text", card.title}, {"background", forma_theme::CARD}, {"font", "{Segoe UI} 11 bold"}}))->pack({{"anchor", "w"}});
        (new ttk::Label(box, {{"text", card.text}, {"background", forma_theme::CARD}, {"foreground", forma_theme::MUTED}, {"wraplength", 640}}))->pack({{"anchor", "w"}, {"pady", "2 8"}});
        (new ttk::Button(box, {{"text", card.button}, {"command", card.command}, {"style", card.accent ? "Accent.TButton" : "TButton"}}))->pack({{"anchor", "w"}});
    }
}

// ---------------------------------------------------------------------------
// Okno główne
// ---------------------------------------------------------------------------

void build(App* app) {
    advanced_ui::install_menu(app);
    studio_ui::install(app);
    // Pasek górny: logo, menu (Projekt / Edycja / Widok — dopełnia forma_shell::finish), nazwa projektu, akcje.
    auto* head = new ttk::Frame(app, {{"style", "Header.TFrame"}, {"padding", "10 6"}});
    head->pack({{"fill", "x"}});
    app->header = head;
    (new tk::Label(head, {{"text", "⬡ "}, {"fg", forma_theme::ACCENT}, {"bg", forma_theme::CARD}, {"font", "{Segoe UI Symbol} 17"}}))->pack({{"side", "left"}});
    (new tk::Label(head, {{"text", "MEBEL"}, {"fg", forma_theme::TEXT}, {"bg", forma_theme::CARD}, {"font", "{Segoe UI} 14 bold"}}))->pack({{"side", "left"}});
    (new tk::Label(head, {{"text", " Studio"}, {"fg", forma_theme::TEXT}, {"bg", forma_theme::CARD}, {"font", "{Segoe UI} 14"}}))->pack({{"side", "left"}, {"padx", "0 16"}});
    app->header_menus = new ttk::Frame(head, {{"style", "Header.TFrame"}});
    app->header_menus->pack({{"side", "left"}});
    Cmd interface_settings = [app] { ui_settings::open_settings(app); };
    Cmd do_export = [app] { app->export_(); };
    make_button(head, "⇪  Eksportuj", do_export, true)->pack({{"side", "right"}, {"padx", "6 0"}});
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"?", [app] { app->help(); }}, {"↷", [app] { app->redo(); }}, {"↶", [app] { app->undo(); }}}) {
        auto* b = make_button(head, label, fn);
        b->configure({{"style", "Tool.TButton"}, {"width", 3}});
        b->pack({{"side", "right"}});
    }
    app->header_title = tk::StringVar("▭  " + (app->project ? app->project->name : std::string("Mój projekt")));
    (new tk::Label(head, {{"textvariable", app->header_title}, {"fg", forma_theme::TEXT}, {"bg", forma_theme::CARD}, {"font", "{Segoe UI} 11 bold"}}))
        ->pack({{"side", "left"}, {"expand", true}});
    auto* workspacepane = new ttk::Panedwindow(app, {{"orient", "vertical"}});
    workspacepane->pack({{"fill", "both"}, {"expand", true}, {"padx", 0}, {"pady", 0}});
    app->workspace_pane = workspacepane;
    app->tabs = new ttk::Notebook(workspacepane);
    workspacepane->add(app->tabs, {{"weight", 1}});
    auto* design = new tk::Frame(app, {{"bg", forma_theme::BG}});
    app->design_page = design;
    app->tabs->add(design, {{"text", "Projekt 3D"}});
    auto* pane = new ttk::Panedwindow(design, {{"orient", "horizontal"}});
    pane->pack({{"fill", "both"}, {"expand", true}});
    // Układ: [panel narzędzi: pasek ikon + lista poleceń] [widok 3D] [właściwości]
    auto* library = new ttk::Frame(pane, {{"style", "Panel.TFrame"}, {"width", 352}});
    auto* left = new ttk::Frame(pane, {{"width", 330}, {"style", "Panel.TFrame"}});
    auto* middle = new ttk::Frame(pane);
    pane->add(library, {{"weight", 0}});
    pane->add(middle, {{"weight", 1}});
    pane->add(left, {{"weight", 0}});
    app->library_panel = library;
    app->main_pane = pane;
    app->inspector_frame = left;
    app->view_frame = middle;
    // Prawy panel ma dwie strony: "Właściwości" (domyślna) i kontekstową dla narzędzia (np. Zaokrąglenia, Silnik i jakość 3D).
    app->props_title = tk::StringVar("Właściwości");
    app->context_title = tk::StringVar("");
    auto* props_page = new ttk::Frame(left, {{"style", "Panel.TFrame"}});
    props_page->pack({{"fill", "both"}, {"expand", true}});
    app->props_page = props_page;
    (new ttk::Label(props_page, {{"textvariable", app->props_title}, {"style", "Title.TLabel"}, {"padding", "14 10 8 4"}}))->pack({{"fill", "x"}});
    auto* context_page = new ttk::Frame(left, {{"style", "Panel.TFrame"}});
    app->context_page = context_page;
    auto* context_head = new ttk::Frame(context_page, {{"style", "Panel.TFrame"}});
    context_head->pack({{"fill", "x"}});
    (new ttk::Label(context_head, {{"textvariable", app->context_title}, {"style", "Title.TLabel"}, {"padding", "14 10 8 4"}}))->pack({{"side", "left"}});
    auto* context_close = new tk::Label(context_head, {{"text", "✕"}, {"bg", forma_theme::PANEL}, {"fg", forma_theme::MUTED}, {"font", "{Segoe UI} 12"}, {"cursor", "hand2"}});
    context_close->pack({{"side", "right"}, {"padx", 14}});
    context_close->bind("<Button-1>", plain([app](tk::Event&) { side_panels::hide(app); }));
    app->context_body = new ttk::Frame(context_page, {{"style", "Panel.TFrame"}, {"padding", "14 4"}});
    app->context_body->pack({{"fill", "both"}, {"expand", true}});
    // Dawna wstążka: strony poleceń w panelu po lewej, przełączane ikonami (strony zachowują nazwy dla innych modułów).
    ttk::Style(app).layout("Rail.TNotebook.Tab", {});
    auto* rail = new tk::Frame(library, {{"bg", forma_theme::BG}, {"width", 64}});
    rail->pack({{"side", "left"}, {"fill", "y"}});
    rail->pack_propagate(false);
    auto* panel_title = new ttk::Label(library, {{"text", "Mebel"}, {"style", "Title.TLabel"}, {"padding", "12 10 8 4"}});
    panel_title->pack({{"side", "top"}, {"fill", "x"}});
    auto* ribbon = new ttk::Notebook(library, {{"style", "Rail.TNotebook"}});
    perf::watch_notebook(app, ribbon, "panel boczny");
    ribbon->pack({{"side", "top"}, {"fill", "both"}, {"expand", true}});
    app->ribbon = ribbon;
    auto& pages = app->ribbon_pages;
    std::vector<responsive_ui::WrapToolbar*> unique_pages;
    app->library = new library_panel::LibraryPanel(ribbon, app);
    ribbon->add(app->library, {{"text", "Biblioteka"}});
    for (const char* name : {"Mebel", "CAD", "Układ", "Scena", "Produkcja", "Widok"}) {
        auto* page = new responsive_ui::RibbonPage(ribbon);
        page->body->vertical = true;
        ribbon->add(page, {{"text", name}});
        pages[name] = page->body;
        unique_pages.push_back(page->body);
    }
    pages["Fronty i okucia"] = pages["Mebel"];
    pages["Szkic / CAD"] = pages["CAD"];
    app->responsive_toolbars.insert(app->responsive_toolbars.end(), unique_pages.begin(), unique_pages.end());
    {
        struct RailItem { const char* page; const char* glyph; const char* caption; const char* title; };
        static const std::vector<RailItem> items = {{"Biblioteka", "\u25A6", "Biblioteka", "Biblioteka"}, {"Mebel", "\u270E", "Mebel", "Mebel i wyposażenie"},
                                                    {"CAD", "\u2316", "CAD", "Szkic i element"}, {"Układ", "\u229E", "Układ", "Układ mebli"},
                                                    {"Scena", "\u2302", "Scena", "Pomieszczenie i światło"}, {"Produkcja", "\u2399", "Produkcja", "Dokumenty i eksport"},
                                                    {"Widok", "\u25C9", "Widok", "Widok i silnik 3D"}};
        auto holders = std::make_shared<std::vector<std::vector<tk::Widget*>>>();
        auto select = std::make_shared<std::function<void(size_t)>>();
        *select = [app, ribbon, panel_title, holders](size_t index) {
            ribbon->select(std::to_string(index));
            panel_title->configure({{"text", items.at(index).title}});
            for (size_t i = 0; i < holders->size(); ++i)
                for (auto* w : (*holders)[i]) w->configure({{"bg", i == index ? forma_theme::ACCENT_SOFT : forma_theme::BG}});
        };
        for (size_t i = 0; i < items.size(); ++i) {
            auto* cell = new tk::Frame(rail, {{"bg", forma_theme::BG}, {"cursor", "hand2"}});
            auto* glyph = new tk::Label(cell, {{"text", items[i].glyph}, {"font", "{Segoe UI Symbol} 16"}, {"bg", forma_theme::BG}, {"fg", forma_theme::TEXT}});
            auto* caption = new tk::Label(cell, {{"text", items[i].caption}, {"font", "{Segoe UI} 8"}, {"bg", forma_theme::BG}, {"fg", forma_theme::MUTED}});
            glyph->pack({{"pady", "8 0"}});
            caption->pack({{"pady", "0 6"}});
            (i + 1 < items.size() ? cell->pack({{"fill", "x"}, {"pady", 1}}) : cell->pack({{"side", "bottom"}, {"fill", "x"}, {"pady", 4}}));
            holders->push_back({cell, glyph, caption});
            for (tk::Widget* w : std::vector<tk::Widget*>{cell, glyph, caption})
                w->bind("<Button-1>", plain([select, i](tk::Event&) { (*select)(i); }));
        }
        app->select_tool_page = [select](const std::string& page) {
            for (size_t i = 0; i < items.size(); ++i)
                if (page == items[i].page) { (*select)(i); return; }
        };
        (*select)(0);
    }
    auto* lefttabs = new ttk::Notebook(props_page, {{"style", "Inspector.TNotebook"}});
    lefttabs->pack({{"fill", "both"}, {"expand", true}});
    app->inspector_tabs = lefttabs;
    auto* quick = new QuickInspector(lefttabs, app);
    lefttabs->add(quick, {{"text", "Mebel"}});
    app->quick_inspector = quick;
    auto* elements = new ttk::Frame(lefttabs);
    lefttabs->add(elements, {{"text", "Elementy / CAD"}});
    auto* treebox = new ttk::Frame(elements);
    treebox->pack({{"fill", "x"}, {"padx", 6}, {"pady", 4}});
    app->tree = new ttk::Treeview(treebox, {{"show", "tree"}, {"selectmode", "browse"}, {"height", 5}});
    app->tree->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    auto* sb = new ttk::Scrollbar(treebox, {{"orient", "vertical"}, {"command", app->tree->path() + " yview"}});
    sb->pack({{"side", "right"}, {"fill", "y"}});
    app->tree->configure({{"yscrollcommand", sb->path() + " set"}});
    app->tree->bind("<<TreeviewSelect>>", plain([app](tk::Event& e) { app->tree_select(&e); }));
    app->tree->bind("<Delete>", plain([app](tk::Event&) { app->delete_(); }));
    auto* actions = new responsive_ui::WrapToolbar(elements);
    actions->pack({{"fill", "x"}, {"padx", 4}});
    app->responsive_toolbars.push_back(actions);
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"+ Element", [app] { app->add_part(); }}, {"Kopiuj", [app] { app->duplicate(); }}, {"Usuń", [app] { app->delete_(); }}})
        make_button(actions, label, fn);
    auto* formscroll = new ScrollPanel(elements);
    formscroll->pack({{"fill", "both"}, {"expand", true}});
    tk::Widget* form = formscroll->body;
    app->vars.clear();
    for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"name", "Nazwa"}, {"group", "Grupa / mebel"}, {"u", "U · długość"}, {"v", "V · szerokość"},
                                                                               {"t", "Grubość"}, {"x", "Pozycja X"}, {"y", "Pozycja Y"}, {"z", "Pozycja Z"},
                                                                               {"rx", "Obrót X"}, {"ry", "Obrót Y"}, {"rz", "Obrót Z"}}) {
        tk::StringVar& var = app->vars[key];
        auto* row = new responsive_ui::FieldRow(form, label, {{"textvariable", var}, {"width", 14}});
        row->pack({{"fill", "x"}, {"pady", 2}});
        row->entry->bind("<Return>", plain([app](tk::Event&) { app->apply(); }));
    }
    app->vars["plane"] = tk::StringVar("XY");
    (new ttk::Combobox(form, {{"textvariable", app->vars["plane"]}, {"values", std::vector<std::string>{"XY", "XZ", "YZ"}}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"pady", 4}});
    app->matcombo = new ttk::Combobox(form, {{"textvariable", app->matvar}, {"state", "readonly"}});
    app->matcombo->pack({{"fill", "x"}, {"pady", 3}});
    app->visible.set(true);
    (new ttk::Checkbutton(form, {{"text", "Włókna wzdłuż U"}, {"variable", app->grain}}))->pack({{"anchor", "w"}});
    (new ttk::Checkbutton(form, {{"text", "Widoczny"}, {"variable", app->visible}}))->pack({{"anchor", "w"}});
    app->edgevars.clear();
    app->edgevars.reserve(4);
    for (int i = 0; i < 4; ++i) app->edgevars.emplace_back();
    auto* edges = new ttk::Frame(form);
    edges->pack({{"fill", "x"}});
    const char* edge_labels[4] = {"U=0", "U=max", "V=0", "V=max"};
    for (int i = 0; i < 4; ++i)
        (new ttk::Checkbutton(edges, {{"text", edge_labels[i]}, {"variable", app->edgevars[i]}}))->grid({{"row", i / 2}, {"column", i % 2}, {"sticky", "w"}});
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"Zastosuj zmiany", [app] { app->apply(); }}, {"Edytuj szkic", [app] { app->edit_sketch(); }},
                                                                       {"Kontur i otwory…", [app] { app->contour(); }}, {"Przesuń / powiel…", [app] { app->move(); }},
                                                                       {"Rozciągnij dokładnie…", [app] { app->exact_resize(); }}, {"Zaokrąglij kanty…", [app] { app->round_edges(); }}})
        (new ttk::Button(form, {{"text", label}, {"command", fn}, {"padding", 4}}))->pack({{"fill", "x"}, {"pady", 3}});
    formscroll->install_wheel(form);
    auto* view = new responsive_ui::WrapToolbar(middle);
    view->pack({{"fill", "x"}, {"padx", 4}});
    app->view_toolbar = view;
    app->responsive_toolbars.push_back(view);
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"3D", [app] { app->view(-38, 24); }}, {"Przód", [app] { app->view(0, 0); }}, {"Bok", [app] { app->view(-90, 0); }},
                                                                       {"Góra", [app] { app->view(0, 90); }}, {"Dół", [app] { app->view(0, -90); }}, {"Dopasuj", [app] { app->fit(); }}})
        make_button(view, label, fn);
    make_group(pages["Widok"], "PROJEKT");
    side_panels::register_row("layers", make_button(pages["Widok"], "Warstwy", [app] { side_panels::show_layers(app); }));
    make_group(pages["Widok"], "KAMERA");
    make_button(pages["Widok"], "Dopasuj 3D", [app] { app->fit(); });
    make_group(pages["Widok"], "POKAŻ");
    for (auto& [label, var] : std::vector<std::pair<std::string, tk::BooleanVar*>>{{"Rozstrzelony", &app->explode}, {"Wymiary", &app->dimensions}, {"X-ray", &app->xray}})
        (new ttk::Checkbutton(pages["Widok"], {{"text", label}, {"variable", *var}, {"style", "Panel.TCheckbutton"}, {"command", Cmd([app] { app->draw(); })}}))->pack({{"side", "left"}, {"padx", 4}});
    auto* details = new responsive_ui::WrapToolbar(middle);
    details->pack({{"fill", "x"}, {"padx", 5}});
    app->detail_toolbar = details;
    app->responsive_toolbars.push_back(details);
    auto* tools = new ttk::Combobox(details, {{"state", "readonly"}, {"width", 14}, {"values", std::vector<std::string>{"Wybierz / obrót", "Przesuwaj", "Rozciągaj mebel", "Rozciągaj część", "Kanty"}}});
    tools->current(0);
    tools->pack({{"side", "left"}, {"padx", "0 4"}});
    app->view_tool_combo = tools;
    static const std::vector<std::string> tool_modes = {"camera", "layout", "furniture", "resize", "fillet"};
    tools->bind("<<ComboboxSelected>>", plain([app, tools](tk::Event&) { app->edit_mode.set(tool_modes.at(tools->current())); app->draw(); }));
    app->edit_mode.trace_add("write", [app, tools] {
        auto it = std::find(tool_modes.begin(), tool_modes.end(), app->edit_mode.get());
        if (it != tool_modes.end()) tools->current((int)(it - tool_modes.begin()));
    });
    auto* dimrow = new ttk::Frame(pages["Widok"], {{"style", "Panel.TFrame"}});  // panel boczny: jeden wiersz = etykieta + lista
    (new ttk::Label(dimrow, {{"text", "Wymiary:"}, {"style", "Panel.TLabel"}, {"width", 15}}))->pack({{"side", "left"}, {"padx", "4 6"}});
    auto* dimcombo = new ttk::Combobox(dimrow, {{"textvariable", app->dimension_mode}, {"values", viewport_annotations::MODES}, {"state", "readonly"}});
    dimcombo->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", "0 4"}});
    dimcombo->bind("<<ComboboxSelected>>", plain([app](tk::Event&) { app->draw(); }));
    (new ttk::Checkbutton(pages["Widok"], {{"text", "Otwory montażowe"}, {"variable", app->show_drills}, {"style", "Panel.TCheckbutton"}, {"command", Cmd([app] { app->draw(); })}}))->pack({{"side", "left"}, {"padx", 5}});
    auto* trayframe = new ttk::Frame(middle);
    auto toggle_tray = [app, trayframe] {
        if (!trayframe->winfo_manager().empty()) trayframe->pack_forget();
        else trayframe->pack({{"fill", "x"}, {"before", app->canvas->path()}});
        if (app->ui_layout) app->ui_layout->schedule();
    };
    (new ttk::Button(details, {{"text", "Przeciągnij okucie / LED"}, {"command", Cmd(toggle_tray)}, {"padding", "6 3"}}))->pack({{"side", "right"}});
    app->hardware_panel = new hardware_placement_ui::HardwarePlacementPanel(trayframe, app);
    app->hardware_panel->pack({{"fill", "x"}});
    app->toggle_hardware_tray = toggle_tray;

    auto* motion = new ttk::Frame(middle);
    motion->pack({{"side", "bottom"}, {"fill", "x"}, {"padx", 5}, {"pady", 3}});
    app->motion_toolbar = motion;
    (new ttk::Label(motion, {{"text", "Otwieranie"}}))->pack({{"side", "left"}});
    // Tk wywołuje komendę Scale z bieżącą wartością (tekst); open_motion ją odczytuje ze zmiennej.
    app->motion_scale = new ttk::Scale(motion, {{"from", 0}, {"to", 100}, {"variable", app->motion_value},
                                                {"command", std::function<void(const std::string&)>([app](const std::string&) { app->open_motion(); })}});
    app->motion_scale->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 6}});
    (new ttk::Checkbutton(motion, {{"text", "Cały mebel"}, {"variable", app->motion_all}}))->pack({{"side", "left"}});
    make_button(motion, "▶", [app] { app->animate_motion(); });
    make_button(motion, "Zamknij", [app] { app->close_motion(); });
    app->canvas = new StudioCanvas(middle, {{"bg", forma_theme::BG}, {"highlightthickness", 0}, {"takefocus", true}});
    app->canvas->pack({{"fill", "both"}, {"expand", true}});
    for (auto& [event, fn] : std::vector<std::pair<std::string, Handler>>{
             {"<Configure>", plain([app](tk::Event&) { app->draw(); })},
             {"<ButtonPress-1>", plain([app](tk::Event& e) { app->mouse_down(e); })},
             {"<B1-Motion>", plain([app](tk::Event& e) { app->orbit(e); })},
             {"<ButtonRelease-1>", plain([app](tk::Event& e) { app->mouse_up(e); })},
             {"<ButtonPress-3>", plain([app](tk::Event& e) { app->pan_start(e); })},
             {"<B3-Motion>", plain([app](tk::Event& e) { app->pan(e); })},
             {"<MouseWheel>", plain([app](tk::Event& e) { app->wheel(e); })},
             {"<Button-4>", plain([app](tk::Event&) { app->zoom(1.12); })},
             {"<Button-5>", plain([app](tk::Event&) { app->zoom(1 / 1.12); })}})
        app->canvas->bind(event, fn);
    // Wstążka: każda zakładka ma podpisane grupy; każda funkcja jest tylko w jednym miejscu.
    // Zakładka „Mebel": tworzenie, wygląd, wyposażenie (katalogi dopisuje visual_catalogue).
    make_group(pages["Mebel"], "NOWY MEBEL");
    make_button(pages["Mebel"], "+ Mebel", [quick] { quick->new_furniture(); }, true);
    make_button(pages["Mebel"], "Biblioteka wszystkich mebli", [app] { if (app->library_dialog) app->library_dialog(); });
    make_group(pages["Mebel"], "WYGLĄD");
    make_button(pages["Mebel"], "Kształty i style", [app] { if (app->style_library) app->style_library(); });
    make_button(pages["Mebel"], "Kolory", [app] { if (app->palette_dialog) app->palette_dialog(); });
    make_group(pages["Mebel"], "WYPOSAŻENIE");
    for (auto& [label, key] : std::vector<std::pair<std::string, std::string>>{{"Fronty / uchwyty", "fronts"}, {"Zawiasy / prowadnice", "fittings"}, {"Nóżki / blat", "legs"},
                                                                               {"Materiały", "materials"}, {"LED", "led"}})
        make_button(pages["Fronty i okucia"], label, [app, key] { focus_section(app, key); });
    make_button(pages["Fronty i okucia"], "Okucia elementu…", [app] { if (app->fittings_dialog) app->fittings_dialog(); });
    // Zakładka „CAD": szkic, element, tryb pracy myszą.
    make_group(pages["Szkic / CAD"], "SZKIC");
    make_button(pages["Szkic / CAD"], "Rysuj", [app] { new Sketch(app, nullptr); });
    make_button(pages["Szkic / CAD"], "Edytuj szkic", [app] { app->edit_sketch(); });
    make_group(pages["Szkic / CAD"], "ELEMENT");
    make_button(pages["Szkic / CAD"], "Dopasuj element", [app] { if (app->fit_selected) app->fit_selected(); });
    side_panels::register_row("fillet", make_button(pages["Szkic / CAD"], "Zaokrąglenia", [app] { side_panels::show_fillet(app); }));
    make_group(pages["Szkic / CAD"], "TRYB MYSZY");
    auto* modes = new ttk::Combobox(pages["Szkic / CAD"], {{"state", "readonly"}, {"width", 24}, {"values", std::vector<std::string>{"Wybór / obrót", "Rozciągaj część", "Rozciągaj mebel", "Kanty"}}});
    modes->current(0);
    modes->pack({{"side", "left"}, {"padx", 5}});
    static const std::vector<std::string> cad_modes = {"camera", "resize", "furniture", "fillet"};
    modes->bind("<<ComboboxSelected>>", plain([app, modes](tk::Event&) { app->edit_mode.set(cad_modes.at(modes->current())); app->draw(); }));
    (new ttk::Checkbutton(pages["Szkic / CAD"], {{"text", "Krok 32 mm"}, {"variable", app->resize_step}, {"style", "Panel.TCheckbutton"}}))->pack({{"side", "left"}});
    // Zakładka „Układ": tryb, obrót, pozycja (narożniki i blaty dopisuje workspace_controls).
    make_group(pages["Układ"], "TRYB");
    (new ttk::Radiobutton(pages["Układ"], {{"text", "Wybór / obrót"}, {"variable", app->edit_mode}, {"value", "camera"}, {"style", "Panel.TRadiobutton"}, {"command", Cmd([app] { app->draw(); })}}))->pack({{"side", "left"}, {"padx", 6}});
    (new ttk::Radiobutton(pages["Układ"], {{"text", "Przesuwaj meble"}, {"variable", app->edit_mode}, {"value", "layout"}, {"style", "Panel.TRadiobutton"}, {"command", Cmd([app] { app->draw(); })}}))->pack({{"side", "left"}});
    (new ttk::Checkbutton(pages["Układ"], {{"text", "Magnes"}, {"variable", app->layout_magnet}, {"style", "Panel.TCheckbutton"}}))->pack({{"side", "left"}, {"padx", 6}});
    make_group(pages["Układ"], "POZYCJA");
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"−90°", [app] { if (app->rotate_furniture) app->rotate_furniture(-90); }},
                                                                       {"+90°", [app] { if (app->rotate_furniture) app->rotate_furniture(90); }},
                                                                       {"Łączenie i pozycja…", [app] { if (app->layout_dialog) app->layout_dialog(); }}})
        make_button(pages["Układ"], label, fn);
    make_group(pages["Układ"], "NAROŻNIKI I BLATY");
    // Zakładka „Produkcja": dokumenty i eksport (ceny są w Ustawieniach, pomoc też).
    make_group(pages["Produkcja"], "DOKUMENTY");
    for (auto& [label, fn] : std::vector<std::pair<std::string, Cmd>>{{"Drukuj komplet…", [app] { if (app->print_project) app->print_project(); }},
                                                                       {"Eksport CAD / CNC", [app] { app->export_(); }}})
        make_button(pages["Produkcja"], label, fn);
    make_group(pages["Produkcja"], "DIAGNOSTYKA");
    // Zakładka „Widok" jest budowana wyżej (kamera, pokaż) i w view_modes_ui (silnik 3D).
    build_production(app);
    build_settings_page(app);
    auto* statusbar = new ttk::Frame(app, {{"style", "Header.TFrame"}, {"padding", "12 5"}});
    statusbar->pack({{"side", "bottom"}, {"fill", "x"}, {"before", workspacepane->path()}});
    (new ttk::Label(statusbar, {{"textvariable", app->status}, {"background", forma_theme::CARD}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "left"}});
    (new ttk::Label(statusbar, {{"textvariable", app->status_right}, {"background", forma_theme::CARD}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "right"}});
    // Współrzędne kursora na podłodze sceny (rzut ortogonalny odwrócony do płaszczyzny z = 0), kolory osi jak w gizmo.
    app->status_x.set("X  —"); app->status_y.set("Y  —"); app->status_z.set("Z  0");
    for (auto [var, color] : {std::pair{&app->status_z, "#527cae"}, std::pair{&app->status_y, "#4a9275"}, std::pair{&app->status_x, "#bd6559"}})
        (new ttk::Label(statusbar, {{"textvariable", *var}, {"background", forma_theme::CARD}, {"foreground", color}, {"font", "{Segoe UI} 9 bold"}, {"width", 10}}))->pack({{"side", "right"}, {"padx", 4}});
    app->canvas->bind("<Motion>", [app](tk::Event& e) -> std::string {
        try {
            // Ruch myszy bywa bardzo częsty — liczymy współrzędne najwyżej ~25 razy na sekundę i tylko przy zmianie wartości.
            static auto last_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_time < std::chrono::milliseconds(40)) return "";
            last_time = now;
            double w = app->canvas->winfo_width(), h = app->canvas->winfo_height();
            const auto& cam = app->camera;
            double a = cam.yaw * std::numbers::pi / 180, b = cam.pitch * std::numbers::pi / 180;
            double sb = std::sin(b), cb = std::cos(b), ca = std::cos(a), sa = std::sin(a);
            if (std::fabs(sb) < 0.05 || std::fabs(cam.scale) < 1e-9) { app->status_x.set("X  —"); app->status_y.set("Y  —"); return ""; }
            double u = (e.x - w / 2 - cam.pan[0]) / cam.scale, v = (h / 2 + cam.pan[1] - e.y) / cam.scale;
            double zr = -cam.center[2], d = (v - zr * cb) / sb;
            double x = cam.center[0] + u * ca + d * sa, y = cam.center[1] - u * sa + d * ca;
            static std::string last_x, last_y;
            std::string tx = std::format("X  {:.0f}", x), ty = std::format("Y  {:.0f}", y);
            if (tx != last_x) { last_x = tx; app->status_x.set(tx); }
            if (ty != last_y) { last_y = ty; app->status_y.set(ty); }
        } catch (const tk::TclError&) {
        }
        return "";
    }, true);
    app->workspace = new dock_workspace::DockWorkspace(app, app->tabs);
    app->workspace->add_existing(design, "Projekt 3D", false);
    hardware_ui::install(app);
    style_ui::install_menu(app);
    furniture_layout_ui::install(app);
}

void focus_section(App* app, const std::string& key) {
    if (app->ui_layout) app->ui_layout->show_inspector(true);
    app->workspace->select(app->design_page);
    app->inspector_tabs->select("0");
    Section* section = app->quick_inspector->sections.at(key);
    app->quick_inspector->show_tab(QuickInspector::tab_of(key));
    section->opened = true;
    section->show();
    app->update_idletasks();
    auto* panel = app->quick_inspector->scroll;
    double y = section->winfo_y();
    double height = std::max(1, panel->body->winfo_height());
    panel->canvas->yview_moveto(y / height);
}

void build_production(App* app) {
    app->tables.clear();
    // Wspólny nagłówek karty: tytuł + opis (jasny motyw Forma).
    auto header = [](tk::Widget* page, const std::string& title, const std::string& note) -> ttk::Label* {
        (new ttk::Label(page, {{"text", title}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}});
        auto* label = new ttk::Label(page, {{"text", note}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}, {"wraplength", 900}, {"justify", "left"}});
        label->pack({{"anchor", "w"}, {"pady", "2 10"}});
        return label;
    };
    struct Spec { std::string key, title; std::vector<std::string> cols; std::vector<int> widths; std::vector<std::string> anchors; };
    for (auto& [key, title, cols, widths, anchors] : std::vector<Spec>{
             {"parts", "Formatki", {"Grupa", "Element", "Materiał", "Gotowe U", "Gotowe V", "T", "Cięcie U", "Cięcie V", "Obrzeża m", "Kontur"},
              {150, 200, 190, 90, 90, 60, 90, 90, 90, 80}, {"w", "w", "w", "e", "e", "e", "e", "e", "e", "center"}},
             {"costs", "Kosztorys", {"Pozycja", "Wartość zł"}, {420, 160}, {"w", "e"}}}) {
        auto* page = new ttk::Frame(app->tabs, {{"padding", "18 14"}});
        app->tabs->add(page, {{"text", title}});
        if (key == "parts") {
            header(page, "Formatki", "Wymiary gotowe zawierają obrzeża; wymiary do cięcia odejmują ich grubość.");
        } else {
            app->costnote = header(page, "Kosztorys", "");
            auto* change = new ttk::Button(page, {{"text", "Zmień ceny, robociznę i narzut"}, {"style", "Tool.TButton"}, {"command", Cmd([app] { app->settings(); })}});
            change->pack({{"anchor", "w"}, {"pady", "0 10"}});
        }
        // Tabela w białej karcie z cienką ramką.
        auto* card = new tk::Frame(page, {{"bg", forma_theme::BORDER}});
        card->pack({{"fill", "both"}, {"expand", true}});
        auto* inner = new tk::Frame(card, {{"bg", forma_theme::CARD}});
        inner->pack({{"fill", "both"}, {"expand", true}, {"padx", 1}, {"pady", 1}});
        auto* table = new ttk::Treeview(inner, {{"columns", cols}, {"show", "headings"}});
        auto* vsb = new ttk::Scrollbar(inner, {{"orient", "vertical"}, {"command", table->path() + " yview"}});
        auto* hsb = new ttk::Scrollbar(inner, {{"orient", "horizontal"}, {"command", table->path() + " xview"}});
        vsb->pack({{"side", "right"}, {"fill", "y"}});
        hsb->pack({{"side", "bottom"}, {"fill", "x"}});
        table->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
        table->configure({{"xscrollcommand", hsb->path() + " set"}, {"yscrollcommand", vsb->path() + " set"}});
        for (size_t i = 0; i < cols.size(); ++i) {
            table->heading(cols[i], {{"text", cols[i]}, {"anchor", anchors[i] == "e" ? "e" : "w"}});
            table->column(cols[i], {{"width", widths[i]}, {"anchor", anchors[i]}, {"stretch", i + 1 == cols.size() && key == "costs"}});
        }
        table->tag_configure("odd", {{"background", forma_theme::PANEL}});
        table->tag_configure("even", {{"background", forma_theme::CARD}});
        table->tag_configure("subtotal", {{"background", forma_theme::HOVER}, {"font", "{Segoe UI} 10 bold"}});
        table->tag_configure("total", {{"background", forma_theme::ACCENT_SOFT}, {"foreground", forma_theme::ACCENT}, {"font", "{Segoe UI} 11 bold"}});
        app->tables[key] = table;
    }
    auto* cut = new ttk::Frame(app->tabs, {{"padding", "18 14"}});
    app->tabs->add(cut, {{"text", "Rozkrój"}});
    app->cutlabel = header(cut, "Rozkrój", "");
    app->sheetcombo = new ttk::Combobox(cut, {{"textvariable", app->sheetvar}, {"state", "readonly"}, {"width", 65}});
    app->sheetcombo->pack({{"anchor", "w"}});
    app->sheetcombo->bind("<<ComboboxSelected>>", plain([app](tk::Event&) { app->draw_cut(); }));
    app->cutcanvas = new tk::Canvas(cut, {{"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER}});
    app->cutcanvas->pack({{"fill", "both"}, {"expand", true}, {"pady", 10}});
    app->cutcanvas->bind("<Configure>", plain([app](tk::Event&) { app->draw_cut(); }));
    (new ttk::Label(cut, {{"text", "Rozkrój gilotynowy jest przybliżony; nieregularne kontury używają prostokątnych półfabrykatów."}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}}))->pack({{"anchor", "w"}});
}

ttk::Button* _button(tk::Widget* parent, const std::string& label, std::function<void()> command, bool accent) {
    return make_button(parent, label, std::move(command), accent);
}

ttk::Label* _group(tk::Widget* parent, const std::string& title) { return make_group(parent, title); }

}  // namespace quick_studio_ui
