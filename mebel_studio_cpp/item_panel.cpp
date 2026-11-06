// Prawy panel: właściwości pozycji z biblioteki i drzewo warstw.
#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "catalogue_previews.h"
#include "forma_theme.h"
#include "forma_widgets.h"
#include "pyutil.h"
#include "side_panels.h"
#include "visual_catalogue.h"

namespace side_panels {

namespace {

using json = core::json;

const std::map<std::string, std::string>& labels() {
    static const std::map<std::string, std::string> map = {
        {"width", "Szerokość"}, {"depth", "Głębokość"}, {"height", "Wysokość"}, {"diameter", "Średnica"}, {"pitch", "Rozstaw"},
        {"offset", "Odsunięcie"}, {"spacing", "Rozstaw otworów"}, {"count", "Ilość"}, {"clearance", "Luz na stronę"},
        {"inner_width_offset", "Zmniejszenie szer. wewn."}, {"lengths", "Długości"}, {"mount", "Montaż"}, {"length", "Długość"},
        {"thickness", "Grubość"}, {"price", "Cena"}, {"price_m2", "Cena za m²"}, {"color", "Kolor"}, {"material", "Materiał"},
        {"load", "Nośność"}, {"angle", "Kąt otwarcia"}, {"option", "Wariant"}, {"template", "Szablon"}, {"style", "Styl"},
        {"finish", "Wykończenie"}, {"name", "Nazwa"}, {"setback", "Cofnięcie"}, {"power", "Moc"}, {"voltage", "Napięcie"}};
    return map;
}

std::string label_of(const std::string& key) {
    auto found = labels().find(key);
    return found != labels().end() ? found->second : key;
}

std::string value_text(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "tak" : "nie";
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number()) return std::format("{:g}", value.get<double>());
    if (value.is_array()) {
        std::string text;
        for (const auto& v : value) text += (text.empty() ? "" : ", ") + value_text(v);
        return text;
    }
    if (value.is_null()) return "—";
    return value.dump();
}

bool is_dimension(const std::string& key) {
    return key != "mount" && key != "lengths" && key != "count";
}

void row(tk::Widget* parent, const std::string& name, const std::string& value) {
    auto* line = new ttk::Frame(parent, {{"style", "Panel.TFrame"}});
    line->pack({{"fill", "x"}, {"pady", 2}});
    (new ttk::Label(line, {{"text", name}, {"style", "Panel.TLabel"}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}}))->pack({{"side", "left"}});
    (new ttk::Label(line, {{"text", value}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 9"}, {"wraplength", 170}, {"justify", "right"}}))->pack({{"side", "right"}});
}

std::string lowered(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) out += (char)std::tolower(c);
        else out += text[i];
    }
    return out;
}

bool has(const std::string& haystack, std::initializer_list<const char*> needles) {
    std::string h = lowered(haystack);
    for (const char* n : needles)
        if (h.find(n) != std::string::npos) return true;
    return false;
}

// Typowe przyłącza wynikające z rodzaju pozycji (podpowiedź projektowa, nie dane producenta).
std::vector<std::pair<std::string, std::string>> connections(const json& item) {
    std::string kind = item.at("kind").get<std::string>();
    std::string text = item.at("title").get<std::string>() + " " + item.at("category").get<std::string>();
    for (const auto& tag : item.value("tags", json::array()))
        if (tag.is_string()) text += " " + tag.get<std::string>();
    std::vector<std::pair<std::string, std::string>> out;
    if (kind == "appliance") {
        if (has(text, {"pralk", "zmywark"})) out = {{"Zasilanie", "230 V · gniazdo z uziemieniem"}, {"Woda", "zimna ½\" z zaworem"}, {"Odpływ", "Ø 40 mm · syfon z króćcem"}};
        else if (has(text, {"zlew", "umywal", "bateri", "prysznic", "wann"})) out = {{"Woda", "zimna i ciepła ½\""}, {"Odpływ", "Ø 40–50 mm · syfon"}};
        else if (has(text, {"płyt", "płytę", "indukc", "kuchenk"})) out = {{"Zasilanie", "400 V / 230 V · przewód 3×2,5 mm²"}, {"Wentylacja", "okap lub wyciąg"}};
        else if (has(text, {"piekarnik", "mikrofal"})) out = {{"Zasilanie", "230 V · osobny obwód"}, {"Wentylacja", "szczelina chłodząca w zabudowie"}};
        else if (has(text, {"okap"})) out = {{"Zasilanie", "230 V"}, {"Wentylacja", "kanał Ø 120–150 mm"}};
        else if (has(text, {"lodów", "zamrażar", "chłodz"})) out = {{"Zasilanie", "230 V · gniazdo z uziemieniem"}, {"Wentylacja", "wolne ok. 50 mm z tyłu i góry"}};
        else if (has(text, {"światł", "lamp", "oświetl"})) out = {{"Zasilanie", "230 V lub 12/24 V przez zasilacz"}};
        else out = {{"Zasilanie", "zgodnie z kartą urządzenia"}};
    } else if (kind == "led") {
        out = {{"Zasilanie", "12/24 V DC · zasilacz w pobliżu"}, {"Przewód", "prowadzony w rowku taśmy"}, {"Sterowanie", "włącznik lub czujnik ruchu"}};
    } else if (kind == "hinge") {
        out = {{"Montaż", "puszka w froncie + prowadnik w boku"}, {"Regulacja", "3 osie · wysokość, głębokość, boczna"}};
    } else if (kind == "runner") {
        out = {{"Montaż", "profil w boku lub podwieszany pod dnem"}, {"Połączenie", "zaczep frontu i wkręty w boku"}};
    } else if (kind == "handle") {
        out = {{"Montaż", "otwory przelotowe w froncie"}, {"Wkręty", "M4 · dobrać do grubości frontu"}};
    } else if (kind == "leg") {
        out = {{"Montaż", "wkręty lub gwint w dnie korpusu"}, {"Regulacja", "poziomowanie od góry lub od spodu"}};
    } else if (kind == "furniture") {
        if (has(text, {"zlew", "zlewozmyw"})) out = {{"Woda", "zimna i ciepła"}, {"Odpływ", "syfon Ø 40 mm"}};
        else if (has(text, {"wisząc", "nadstaw"})) out = {{"Montaż", "zawieszki ścienne lub szyna"}};
        else out = {{"Montaż", "podłoga · nóżki lub cokół"}, {"Połączenie", "ściągi do sąsiednich modułów"}};
    }
    return out;
}

void section(tk::Widget* parent, const std::string& title) {
    auto* line = new ttk::Frame(parent, {{"style", "Panel.TFrame"}});
    line->pack({{"fill", "x"}, {"pady", "12 2"}});
    (new ttk::Label(line, {{"text", title}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 10 bold"}}))->pack({{"side", "left"}});
    auto* rule = new tk::Frame(parent, {{"bg", forma_theme::BORDER}, {"height", 1}});
    rule->pack({{"fill", "x"}, {"pady", "0 3"}});
}

std::chrono::steady_clock::time_point g_item_shown;

}  // namespace

bool item_stale() { return std::chrono::steady_clock::now() - g_item_shown > std::chrono::milliseconds(1500); }

void show_item(App* app, const json& item, std::function<void()> add) {
    json copy = item;
    g_item_shown = std::chrono::steady_clock::now();
    show(app, "item", "Właściwości: " + item.at("title").get<std::string>(), [app, copy, add](tk::Widget* body) {
        const json& dims = copy.at("dimensions");
        std::string kind = copy.at("kind").get<std::string>();
        // Karta z miniaturą.
        auto* head = new ttk::Frame(body, {{"style", "Panel.TFrame"}});
        head->pack({{"fill", "x"}});
        auto* image = new tk::Canvas(head, {{"width", 96}, {"height", 74}, {"bg", forma_theme::CARD}, {"highlightthickness", 1}, {"highlightbackground", forma_theme::BORDER}});
        image->pack({{"side", "left"}, {"padx", "0 10"}});
        try {
            catalogue_previews::draw_preview(image, copy, 96, 74);
        } catch (const std::exception&) {
        }
        auto* names = new ttk::Frame(head, {{"style", "Panel.TFrame"}});
        names->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
        (new ttk::Label(names, {{"text", copy.at("title").get<std::string>()}, {"style", "Panel.TLabel"}, {"font", "{Segoe UI} 11 bold"}, {"wraplength", 170}}))->pack({{"anchor", "w"}});
        (new ttk::Label(names, {{"text", copy.at("category").get<std::string>()}, {"style", "Panel.TLabel"}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}}))->pack({{"anchor", "w"}});
        (new ttk::Label(names, {{"text", visual_catalogue::dimension_text(copy)}, {"style", "Panel.TLabel"}, {"foreground", forma_theme::ACCENT}, {"font", "{Segoe UI} 9"}, {"wraplength", 170}}))->pack({{"anchor", "w"}, {"pady", "2 0"}});

        // Wymiary: pola tylko do odczytu z jednostką (wartości z katalogu).
        section(body, "Wymiary");
        bool any = false;
        for (const auto& [key, value] : dims.items()) {
            if (!value.is_number() || !is_dimension(key)) continue;
            tk::StringVar var(value_text(value));
            forma_widgets::field_row(body, label_of(key), var, key == "angle" ? "°" : "mm", true);
            any = true;
        }
        if (!any) (new ttk::Label(body, {{"text", kind == "material" ? "Materiał nie ma wymiarów — ustawia się go elementom." : "Wymiary ustawia się po dodaniu do projektu."},
                                         {"style", "Panel.TLabel"}, {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 9"}, {"wraplength", 280}}))->pack({{"anchor", "w"}});

        // Parametry: pozostałe wartości z katalogu i ładunku.
        section(body, "Parametry");
        bool listed = false;
        for (const auto& [key, value] : dims.items()) {
            if (value.is_number() && is_dimension(key)) continue;
            row(body, label_of(key), value_text(value));
            listed = true;
        }
        if (copy.at("payload").is_object()) {
            for (const auto& [key, value] : copy.at("payload").items()) {
                if (value.is_object() || dims.contains(key) || key == "id") continue;
                if (value.is_array() && value.size() > 12) continue;
                row(body, label_of(key), value_text(value));
                listed = true;
            }
        }
        if (copy.contains("preview") && copy.at("preview").is_object() && copy.at("preview").contains("color")) {
            row(body, "Kolor", value_text(copy.at("preview").at("color")));
            listed = true;
        }
        if (!listed) row(body, "Parametry", "domyślne");

        // Przyłącza.
        section(body, "Przyłącza");
        auto list = connections(copy);
        if (list.empty()) row(body, "Przyłącza", "brak");
        for (const auto& [name, value] : list) row(body, name, value);

        // Informacje.
        section(body, "Informacje");
        row(body, "Rodzaj", kind);
        std::string tags;
        for (const auto& tag : copy.value("tags", json::array()))
            if (tag.is_string()) tags += (tags.empty() ? "" : ", ") + tag.get<std::string>();
        if (!tags.empty()) row(body, "Tagi", tags);
        std::string note = copy.value("note", std::string());
        if (!note.empty()) row(body, "Uwagi", note);
        json source = copy.value("source", json(""));
        if (source.is_string() && !source.get<std::string>().empty()) {
            std::string url = source.get<std::string>();
            row(body, "Źródło", url.starts_with("https://") ? "dane producenta" : url);
            if (url.starts_with("https://"))
                (new ttk::Button(body, {{"text", "Otwórz stronę producenta"}, {"style", "Tool.TButton"},
                                        {"command", std::function<void()>([url] { py::webbrowser_open(url); })}}))->pack({{"fill", "x"}, {"pady", "4 0"}});
        }
        if (add) {
            auto* button = new ttk::Button(body, {{"text", "＋  Dodaj do projektu"}, {"style", "Accent.TButton"}, {"padding", "10 10"}, {"command", std::function<void()>(add)}});
            button->pack({{"fill", "x"}, {"pady", "14 4"}});
        }
        auto* back = new ttk::Button(body, {{"text", "Wróć do właściwości mebla"}, {"style", "Tool.TButton"}, {"command", std::function<void()>([app] { hide(app); })}});
        back->pack({{"fill", "x"}});
    });
}

void show_layers(App* app) {
    show(app, "layers", "Warstwy", [app](tk::Widget* body) {
        auto* box = new ttk::Frame(body, {{"style", "Panel.TFrame"}});
        box->pack({{"fill", "both"}, {"expand", true}});
        auto* tree = new ttk::Treeview(box, {{"show", "tree"}, {"selectmode", "browse"}, {"height", 16}});
        auto* bar = new ttk::Scrollbar(box, {{"orient", "vertical"}, {"command", tree->path() + " yview"}});
        bar->pack({{"side", "right"}, {"fill", "y"}});
        tree->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
        tree->configure({{"yscrollcommand", bar->path() + " set"}});
        auto fill = [app, tree] {
            try {
                tree->delete_(tree->get_children());
                std::map<std::string, std::string> groups;
                for (const auto& p : app->project->parts) {
                    if (!groups.contains(p.group)) {
                        std::string gid = "layer_" + std::to_string(groups.size());
                        groups[p.group] = gid;
                        tree->insert("", "end", {{"iid", gid}, {"text", "▾  " + (p.group.empty() ? std::string("Bez grupy") : p.group)}, {"open", true}});
                    }
                    tree->insert(groups[p.group], "end", {{"iid", p.id}, {"text", std::string(p.visible ? "◉  " : "○  ") + p.name}});
                }
                if (app->selected && tree->exists(*app->selected)) tree->selection_set(*app->selected);
            } catch (const tk::TclError&) {
            }
        };
        fill();
        tree->bind("<<TreeviewSelect>>", [app, tree](tk::Event&) {
            try {
                auto sel = tree->selection();
                if (!sel.empty() && app->tree != nullptr && app->tree->exists(sel[0])) app->tree->selection_set(sel[0]);
            } catch (const tk::TclError&) {
            }
            return std::string();
        });
        // Dwuklik przełącza widoczność elementu (lub całej grupy).
        tree->bind("<Double-1>", [app, tree, fill](tk::Event&) {
            try {
                auto sel = tree->selection();
                if (sel.empty()) return std::string();
                std::string id = sel[0];
                std::string group_name;
                bool is_group = id.starts_with("layer_");
                if (is_group) {
                    std::string text = tree->item(id, "text");
                    group_name = text.size() > 5 ? text.substr(text.find("  ") + 2) : text;
                }
                app->snapshot();
                bool target = true;
                bool found = false;
                for (auto& p : app->project->parts) {
                    bool match = is_group ? (p.group == group_name || (group_name == "Bez grupy" && p.group.empty())) : p.id == id;
                    if (!match) continue;
                    if (!found) { target = !p.visible; found = true; }
                    p.visible = target;
                }
                app->refresh();
                fill();
            } catch (const tk::TclError&) {
            }
            return std::string();
        });
        (new ttk::Label(body, {{"text", "Kliknij, aby zaznaczyć. Dwuklik pokazuje / ukrywa element lub cały mebel."}, {"style", "Panel.TLabel"},
                               {"foreground", forma_theme::MUTED}, {"font", "{Segoe UI} 8"}, {"wraplength", 280}}))->pack({{"anchor", "w"}, {"pady", "6 0"}});
        auto* back = new ttk::Button(body, {{"text", "Wróć do właściwości"}, {"style", "Tool.TButton"}, {"command", std::function<void()>([app] { hide(app); })}});
        back->pack({{"fill", "x"}, {"pady", "6 0"}});
    });
}

}  // namespace side_panels
