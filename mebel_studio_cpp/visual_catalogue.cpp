// Persistent, searchable graphic catalogue and explicit backend action routing.
#include "visual_catalogue.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

#include "app.h"
#include "assembly.h"
#include "library_panel.h"
#include "quick_studio_ui.h"
#include "catalogue_previews.h"
#include "corner_furniture.h"
#include "drag_hardware.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "furniture_library.h"
#include "journal.h"
#include "opening_options.h"
#include "palette.h"
#include "pyutil.h"
#include "room_objects.h"
#include "shaped_furniture.h"
#include "runners_catalog.h"

namespace visual_catalogue {

using core::ValueError;

const std::vector<std::pair<std::string, std::string>> KINDS{
    {"furniture", "Meble"}, {"shaped", "Kształty i style"}, {"leg", "Nogi"}, {"hinge", "Zawiasy"}, {"runner", "Prowadnice"}, {"handle", "Uchwyty i otwieranie"},
    {"led", "LED"}, {"appliance", "AGD i wyposażenie"}, {"material", "Materiały i kolory"}};
const std::vector<std::pair<std::string, std::string>> ALIASES{
    {"furnitures", "furniture"}, {"legs", "leg"}, {"hinges", "hinge"}, {"runners", "runner"}, {"handles", "handle"},
    {"materials", "material"}, {"appliances", "appliance"}, {"room", "appliance"}, {"LED", "led"}, {"shapes", "shaped"}, {"styles", "shaped"}};

namespace {

std::string g(double x) { return std::format("{:g}", x); }
double num(const json& value) { return value.get<double>(); }

const std::string& kind_label(const std::string& kind) {
    for (auto& [key, label] : KINDS)
        if (key == kind) return label;
    throw std::out_of_range("'" + kind + "'");
}

// --- tekst UTF-8: str.casefold() / unicodedata.normalize('NFKD') bez znaków łączących

std::u32string decode(const std::string& text) {
    std::u32string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        char32_t cp = length == 1 ? c : length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k < length && i + k < text.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += length;
        out += cp;
    }
    return out;
}

std::string encode(const std::u32string& text) {
    std::string out;
    for (char32_t cp : text) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// Mała litera dla liter łacińskich (Latin-1 i Latin Extended-A), ASCII.
char32_t lower_cp(char32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if (cp >= 0x100 && cp <= 0x137) return cp % 2 == 0 ? cp + 1 : cp;
    if (cp >= 0x139 && cp <= 0x148) return cp % 2 == 1 ? cp + 1 : cp;
    if (cp >= 0x14A && cp <= 0x177) return cp % 2 == 0 ? cp + 1 : cp;
    if (cp == 0x178) return 0xFF;
    if (cp >= 0x179 && cp <= 0x17E) return cp % 2 == 1 ? cp + 1 : cp;
    return cp;
}

// Litera bazowa po dekompozycji NFKD (małe litery łacińskie z akcentami); 0 = brak.
char32_t base_letter(char32_t cp) {
    struct Range { char32_t from, to; char base; };
    static const Range ranges[] = {
        {0xE0, 0xE5, 'a'}, {0xE7, 0xE7, 'c'}, {0xE8, 0xEB, 'e'}, {0xEC, 0xEF, 'i'}, {0xF1, 0xF1, 'n'}, {0xF2, 0xF6, 'o'},
        {0xF9, 0xFC, 'u'}, {0xFD, 0xFD, 'y'}, {0xFF, 0xFF, 'y'},
        {0x100, 0x105, 'a'}, {0x106, 0x10D, 'c'}, {0x10E, 0x10F, 'd'}, {0x112, 0x11B, 'e'}, {0x11C, 0x123, 'g'}, {0x124, 0x125, 'h'},
        {0x128, 0x12F, 'i'}, {0x134, 0x135, 'j'}, {0x136, 0x137, 'k'}, {0x139, 0x13E, 'l'}, {0x143, 0x148, 'n'}, {0x14C, 0x151, 'o'},
        {0x154, 0x159, 'r'}, {0x15A, 0x161, 's'}, {0x162, 0x165, 't'}, {0x168, 0x173, 'u'}, {0x174, 0x175, 'w'}, {0x176, 0x177, 'y'},
        {0x179, 0x17E, 'z'}};
    for (auto& r : ranges)
        if (cp >= r.from && cp <= r.to) return r.base;
    return 0;
}

std::string search_key(const std::string& text) {
    std::u32string out;
    for (char32_t cp : decode(text)) {
        if (cp == 0xDF) { out += U"ss"; continue; }          // casefold
        cp = lower_cp(cp);
        if (cp == 0x142) cp = 'l';                           // .replace('ł', 'l')
        if (cp >= 0x300 && cp <= 0x36F) continue;            // znaki łączące
        char32_t base = base_letter(cp);
        out += base ? base : cp;
    }
    return encode(out);
}

std::vector<std::string> split_whitespace(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            if (!current.empty()) { out.push_back(current); current.clear(); }
        } else current += c;
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// name.lower() — dla liter łacińskich
std::string lower_text(const std::string& text) {
    std::u32string out;
    for (char32_t cp : decode(text)) out += lower_cp(cp);
    return encode(out);
}

bool wood_name(const std::string& name) {
    std::string lowered = lower_text(name);
    for (const char* word : {"dąb", "orzech", "buk", "jesion", "sosna", "brzoza", "wenge", "mahoń"})
        if (lowered.find(word) != std::string::npos) return true;
    return false;
}

json make_item(const std::string& ident, const std::string& kind, const json& title, const json& category, const json& dimensions, const json& payload,
               const json& preview, const json& tags = json::array(), const json& source = "", const std::string& note = "") {
    json item = json::object();
    item["id"] = kind + ":" + ident;
    item["kind"] = kind;
    item["title"] = title;
    item["category"] = category;
    item["dimensions"] = dimensions;
    item["payload"] = payload;
    item["preview"] = preview;
    item["tags"] = tags;
    item["source"] = source;
    item["note"] = note;
    return item;
}

// a + b z zachowaniem typu całkowitego, gdy oba argumenty są całkowite
json add_numbers(const json& a, double b) {
    if (a.is_number_integer() && b == std::floor(b)) return json(a.get<long long>() + (long long)b);
    return json(a.get<double>() + b);
}

bool commit(App* app, const core::ProjectPtr& candidate, std::optional<std::string> selected = std::nullopt,
            std::optional<std::string> scene_selected = std::nullopt, bool fit = false) {
    candidate->validate();
    if (candidate->data() == app->project->data()) return true;
    app->snapshot();
    app->project = candidate;
    if (selected) { app->selected = selected; app->selected_scene.reset(); }
    if (scene_selected) { app->selected_scene = scene_selected; app->selected.reset(); }
    app->refresh();
    if (fit) app->fit();
    return true;
}

double groups_right(const core::Project& project) {
    std::set<std::string> groups;
    for (auto& p : project.parts) groups.insert(p.group);
    double right = furniture_layout::group_bounds(project, *groups.begin()).second[0];
    for (auto& group : groups) right = std::max(right, furniture_layout::group_bounds(project, group).second[0]);
    return right;
}

}  // namespace

std::string canonical_kind(const std::string& kind_in) {
    std::string kind = kind_in;
    for (auto& [alias, target] : ALIASES)
        if (alias == kind_in) { kind = target; break; }
    bool known = std::any_of(KINDS.begin(), KINDS.end(), [&](const auto& entry) { return entry.first == kind; });
    if (!known) throw ValueError("Wybierz kategorię katalogu.");
    return kind;
}

json catalogue_items(const std::string& kind_in, const core::Project* project) {
    std::string kind = canonical_kind(kind_in);
    json items = json::array();
    if (kind == "furniture") {
        const auto& templates = furniture_library::TEMPLATES;
        for (size_t i = 0; i < templates.size(); ++i) {
            const auto& t = templates[i];
            long long index = (long long)i;
            json preview = json{{"width", t.w}, {"depth", t.d}, {"body_height", t.h}, {"family", t.kind}, {"shelves", t.shelves}, {"fronts", t.fronts}, {"thickness", 18}};
            json total = t.h;
            if (py::truthy(furniture_library::template_options(index))) {
                json cfg = furniture_configurator::default_config(json(index), project);
                preview["leg_height"] = cfg.at("leg_height");
                preview["top_mode"] = cfg.at("top_mode");
                preview["back"] = cfg.at("back");
                json sections = json::array();
                for (auto& s : cfg.at("sections"))
                    if (py::truthy(s.at("enabled"))) sections.push_back(s);
                preview["sections"] = sections;
                total = cfg.at("height");
            }
            if (t.kind == "island") total = add_numbers(total, 38);
            if (t.kind == "bed") total = add_numbers(total, 400);
            if (t.kind == "chest") total = add_numbers(total, 18);
            items.push_back(make_item(std::to_string(index), kind, t.name, t.category, json{{"width", t.w}, {"depth", t.d}, {"height", total}}, json{{"template_index", index}},
                                      preview, json::array({t.kind, t.category, "parametryczny"}), "",
                                      "Edytowalne domyślne wymiary projektu. Podgląd konstrukcji, bez doboru modelu AGD."));
        }
    } else if (kind == "shaped") {
        const auto& templates = shaped_furniture::TEMPLATES;
        for (size_t i = 0; i < templates.size(); ++i) {
            const auto& t = templates[i];
            long long index = (long long)i;
            json preview = json{{"width", t.width}, {"depth", t.depth}, {"body_height", t.height}, {"family", t.kind}};
            items.push_back(make_item(std::to_string(index), kind, t.name, t.style, json{{"width", t.width}, {"depth", t.depth}, {"height", t.height}}, json{{"template_index", index}},
                                      preview, json::array({t.kind, t.category, t.style, "obły"}), "", t.description));
        }
    } else if (kind == "leg") {
        for (long long height : {80, 100, 150, 200})
            for (long long count : {4, 6}) {
                json payload = json{{"leg_height", height}, {"leg_count", count}, {"leg_diameter", 40}, {"leg_setback", 60}, {"leg_color", "#242628"}};
                items.push_back(make_item(std::format("{}-{}", height, count), kind, std::format("Nogi Ø40 · {} mm · {} szt.", height, count), "Własne nogi cylindryczne",
                                          json{{"height", height}, {"diameter", 40}, {"count", count}}, payload, json{{"height", height}, {"diameter", 40}, {"color", "#242628"}, {"count", count}},
                                          json::array({"zakupowe", "własny model"}), "",
                                          "Własny model nóg, bez deklaracji nośności producenta. Zmiana zachowuje wysokość korpusu."));
            }
    } else if (kind == "hinge") {
        for (auto& recipe : fittings::RECIPES) {
            std::string id = recipe.at("id").get<std::string>();
            if (id != "hinge45" && id != "hinge52" && id != "hinge_sensys52") continue;
            json dims = json::object();
            for (const char* key : {"diameter", "depth", "pitch", "offset"}) dims[key] = recipe.at(key);
            json payload = json{{"recipe_id", recipe.at("id")}, {"count", 2}, {"price", 0}};
            items.push_back(make_item(id, kind, recipe.at("name"), py::get(recipe, "manufacturer", "Wzorce puszki"), dims, payload, dims,
                                      json::array({recipe.at("verified"), "wymaga sprawdzenia"}), recipe.at("source"),
                                      "Wiercenia z wybranego wzorca. Prowadnik roboczy i pilot wymagają sprawdzenia z rzeczywistym okuciem."));
        }
    } else if (kind == "runner") {
        for (auto& [ident, profile] : runners_catalog::PROFILES.items()) {
            const json& lengths = profile.at("lengths");
            json length = lengths.at(0);
            for (auto& value : lengths)
                if (std::abs(num(value) - 400) < std::abs(num(length) - 400)) length = value;
            json clearance = py::get(profile, "external_clearance");
            if (clearance.is_null()) {
                json offset = py::get(profile, "inner_width_offset");
                clearance = (py::truthy(offset) ? num(offset) : 42.0) / 2;
            }
            json payload = json{{"profile_id", ident}, {"length", length}, {"price", 0}};
            json dims = json{{"lengths", lengths}, {"clearance", py::get(profile, "external_clearance")}, {"inner_width_offset", py::get(profile, "inner_width_offset")},
                             {"max_side_thickness", py::get(profile, "max_side_thickness")}, {"load_kg", py::get(profile, "load_kg")}, {"mount", profile.at("mount")}};
            // Miniaturka różni profile: udźwig (grubość szyny), wysuw pełny/częściowy oraz tłumik / push-to-open (z nazwy profilu).
            std::string label_lower = lower_text(profile.at("label").get<std::string>());
            std::string feature = label_lower.find("push") != std::string::npos ? "push" : (label_lower.find("blumotion") != std::string::npos || label_lower.find("silent") != std::string::npos || label_lower.find("soft") != std::string::npos) ? "soft" : "";
            json load = py::get(profile, "load_kg");
            json preview = json{{"length", length}, {"clearance", clearance}, {"height", 46}, {"mount", profile.at("mount")},
                                {"load_kg", load.is_number() ? load : json(30)}, {"extension", py::get(profile, "extension", "full")}, {"feature", feature}};
            std::string note = profile.at("status").get<std::string>() + "; bez kompletnych wzorców CNC.";
            items.push_back(make_item(ident, kind, profile.at("label"), profile.at("manufacturer"), dims, payload, preview,
                                      json::array({profile.at("family"), profile.at("mount") == "undermount" ? "podwieszana" : "boczna", profile.at("extension")}),
                                      profile.at("source"), note));
        }
    } else if (kind == "handle") {
        for (auto& [title, spacing] : fittings::HANDLES)
            items.push_back(make_item(std::format("spacing-{}", spacing), kind, title, "Uchwyty i gałki", json{{"spacing", spacing}, {"projection", 28}},
                                      json{{"spacing", spacing}, {"color", "#242628"}, {"projection", 28}, {"price", 0}}, json::object(),
                                      json::array({"zakupowy", "własny model"}), "", "Rozstaw otworów w mm; przelot M4. Kolor i wysunięcie edytowalne."));
        for (auto& [option, spec] : opening_options::OPTIONS.items()) {
            json dims = json::object();
            for (const char* key : {"diameter", "depth", "gap"})
                if (spec.contains(key)) dims[key] = spec.at(key);
            json payload = json{{"option", option}, {"handle_mode", option}};
            items.push_back(make_item(option, kind, spec.at("label"), "Chwyt i push-to-open", dims, payload, dims,
                                      json::array({option.starts_with("finger") ? "frezowany" : "bez uchwytu", py::get(spec, "hinge_spring", "")}),
                                      spec.at("source"), spec.at("note").get<std::string>()));
        }
    } else if (kind == "led") {
        for (long long width : {8, 12, 16})
            for (bool groove : {false, true}) {
                json payload = json{{"width", width}, {"length", nullptr}, {"margin", 30}, {"groove", groove}, {"depth", 3}, {"price_per_m", 0}, {"axis", "auto"}};
                items.push_back(make_item(std::format("{}-{}", width, (int)groove), kind, std::format("LED {} mm · ", width) + (groove ? "z rowkiem" : "nakładany"),
                                          "Taśma i profil LED", json{{"width", width}, {"depth", 3}, {"margin", 30}}, payload, json::object(),
                                          json::array({"własny profil", groove ? "rowek" : "nakładany"}), "",
                                          "Własna geometria taśmy i opcjonalnego rowka. Bez doboru zasilacza i parametrów elektrycznych."));
            }
    } else if (kind == "appliance") {
        for (auto& [ident, spec] : room_objects::CATALOG.items()) {
            bool lighting = ident == "lamp" || ident == "bulb";
            bool bath = ident == "washbasin" || ident == "sink" || ident == "faucet" || ident == "shower" || ident == "mirror";
            bool accessory = ident == "hanger" || ident == "shelf";
            std::string category = lighting ? "Oświetlenie" : bath ? "Łazienka i zlew" : accessory ? "Akcesoria pomieszczenia" : "AGD";
            json dims = json{{"width", spec.at("width")}, {"depth", spec.at("depth")}, {"height", spec.at("height")}};
            json preview = json{{"type", ident}, {"color", spec.at("color")}, {"width", spec.at("width")}, {"depth", spec.at("depth")}, {"height", spec.at("height")}};
            items.push_back(make_item(ident, kind, spec.at("name"), category, dims, json{{"object_type", ident}}, preview, json::array({"zakupowe", "własne wymiary"}), "",
                                      "Edytowalny model sceny. Wymiary ilustracyjne, bez deklaracji gabarytów/odstępów konkretnego producenta."));
        }
    } else if (kind == "material") {
        const std::map<std::string, std::string> material_types{{"plyta", "Płyta"}, {"mdf", "MDF"}, {"hdf", "HDF"}, {"drewno", "Drewno"}};
        std::set<std::string> existing;
        if (project) {
            for (auto& material : project->materials) {
                existing.insert(material.name);
                bool wood = material.kind == "drewno" || wood_name(material.name);
                auto type = material_types.find(material.kind);
                items.push_back(make_item(material.id, kind, material.name, type != material_types.end() ? type->second : material.kind,
                                          json{{"sheet_width", material.sheet_u}, {"sheet_height", material.sheet_v}}, json{{"material_id", material.id}},
                                          json{{"color", material.color}, {"wood", wood}}, json::array({material.color, material.kind}), "",
                                          "Materiał projektu. Barwa ekranowa jest przybliżona."));
            }
        }
        for (size_t index = 0; index < palette::PALETTE.size(); ++index) {
            const auto& [name, color] = palette::PALETTE[index];
            std::string title = "Dekor — " + name;
            if (existing.count(title)) continue;
            bool wood = wood_name(name);
            items.push_back(make_item("palette-" + std::to_string(index), kind, title, wood ? "Dekory drewna" : "Kolory i dekory", json::object(),
                                      json{{"name", title}, {"color", color}, {"kind", "mdf"}}, json{{"color", color}, {"wood", wood}, {"decor", true}}, json::array({color, "dekor"}), "",
                                      "Przybliżona barwa ekranu; własny dekor MDF, bez certyfikacji koloru producenta."));
        }
    }
    return items;
}

std::string search_text(const std::string& text) { return search_key(text); }

json filter_items(const json& items, const std::string& query, const std::string& category) {
    std::vector<std::string> tokens = split_whitespace(search_key(query));
    json out = json::array();
    for (auto& item : items) {
        if (!(category == "Wszystkie" || item.at("category") == json(category))) continue;
        std::string joined = item.at("title").get<std::string>() + " " + item.at("category").get<std::string>() + " " + py::str(item.at("dimensions"));
        for (auto& tag : item.at("tags")) joined += " " + tag.get<std::string>();
        std::string haystack = search_key(joined);
        bool all = std::all_of(tokens.begin(), tokens.end(), [&](const std::string& token) { return haystack.find(token) != std::string::npos; });
        if (all) out.push_back(item);
    }
    return out;
}

std::string dimension_text(const json& item) {
    const json& d = item.at("dimensions");
    std::string kind = item.at("kind").get<std::string>();
    if (d.contains("width") && d.contains("depth") && d.contains("height"))
        return std::format("{} × {} × {} mm", g(num(d.at("width"))), g(num(d.at("depth"))), g(num(d.at("height"))));
    if (kind == "leg") return std::format("H {} · Ø{} · {} szt.", g(num(d.at("height"))), g(num(d.at("diameter"))), py::str(d.at("count")));
    if (kind == "hinge") return std::format("Ø{} × {} · {} × {} mm", g(num(d.at("diameter"))), g(num(d.at("depth"))), g(num(d.at("pitch"))), g(num(d.at("offset"))));
    if (kind == "runner") {
        bool undermount = d.at("mount") == "undermount";
        std::string detail = undermount ? "podwieszana" : !d.at("clearance").is_null() ? std::format("luz {} mm / stronę", g(num(d.at("clearance")))) : "montaż boczny";
        if (undermount && !d.at("inner_width_offset").is_null()) detail += std::format(" · wewn. −{} mm", g(num(d.at("inner_width_offset"))));
        double lo = num(d.at("lengths").at(0)), hi = lo;
        for (auto& value : d.at("lengths")) { lo = std::min(lo, num(value)); hi = std::max(hi, num(value)); }
        return std::format("{}–{} mm · ", g(lo), g(hi)) + detail;
    }
    if (kind == "handle" && d.contains("spacing")) return !py::truthy(d.at("spacing")) ? "Gałka · 1 otwór" : std::format("Rozstaw {} mm", g(num(d.at("spacing"))));
    if (kind == "led") return std::format("Szer. {} mm · rowek {} mm", g(num(d.at("width"))), g(num(d.at("depth"))));
    if (kind == "material") return item.at("preview").at("color").get<std::string>();
    std::string text;
    for (auto& [key, value] : d.items())
        if (value.is_number()) text += (text.empty() ? "" : " · ") + std::format("{}: {} mm", key, g(num(value)));
    return text.empty() ? "Własne edytowalne wymiary" : text;
}

void select_design(App* app) {
    if (app->workspace != nullptr && app->design_page != nullptr) app->workspace->select(app->design_page);
    if (app->canvas != nullptr) app->canvas->focus_set();
}

bool update_object(App* app, const std::string& object_id, const json& changes) {
    if (!app->flush_parameter_drafts()) return false;
    return commit(app, room_objects::update_object(*app->project, object_id, changes));  // bez zmiany zaznaczenia: panel pozycji zostaje otwarty
}

// Wysoki mebel (np. słupek 2100 mm) nie mieści formatki boku na płycie, gdy włókna biegną wzdłuż U.
// Zostawiamy blokadę usłojenia, ale przestawiamy ją na kierunek V (pionowo na boku/froncie) i zostawiamy ślad w dzienniku.
static void relax_grain_for_sheet(App* app, core::Project& project, const std::string& group) {
    for (auto& part : project.parts) {
        if (part.group != group || !part.grain || part.grain_v) continue;
        const core::Material* material = nullptr;
        for (const auto& m : project.materials) if (m.id == part.material) { material = &m; break; }
        if (!material) continue;
        auto [a, b] = part.blank(project.edge_t);
        double sw = material->sheet_u - 2 * project.margin, sh = material->sheet_v - 2 * project.margin;
        core::Part turned = part;
        turned.grain_v = true;
        if (part.fits_sheet(a, b, sw, sh) || !turned.fits_sheet(a, b, sw, sh)) continue;
        part.grain_v = true;
        journal::event(app, "biblioteka", std::format("{}: formatka {:g}×{:g} mm nie mieści się na płycie {:g}×{:g} mm z włóknami wzdłuż U — włókna ustawione wzdłuż V (pionowo), formatka leży obrócona",
                                                       part.name, a, b, material->sheet_u, material->sheet_v));
    }
}

bool apply_item(App* app, const json& item) {
    if (!app->flush_parameter_drafts()) return false;
    std::string kind = canonical_kind(item.at("kind").get<std::string>());
    json payload = item.at("payload");
    {
        std::string label = "?";
        for (const char* key : {"name", "title", "label"})
            if (item.contains(key) && item.at(key).is_string()) { label = item.at(key).get<std::string>(); break; }
        journal::event(app, "biblioteka", std::format("dodaję: rodzaj {} · {} · parametry {}", kind, label, payload.dump(-1, ' ', false, json::error_handler_t::replace).substr(0, 160)));
    }
    if (kind == "furniture") {
        auto [candidate, group] = furniture_library::create_furniture(*app->project, payload.at("template_index").get<long long>());
        if (!app->project->parts.empty()) {
            double right = groups_right(*app->project);
            double lo = furniture_layout::group_bounds(*candidate, group).first[0];
            candidate = furniture_layout::move_group(*candidate, group, right + 50 - lo);
        }
        // Mebel z biblioteki od razu trafia do kreatora, dzięki czemu panel „Mebel” (materiały, LED, nóżki,
        // fronty) i karty biblioteki mogą go zmieniać. Gdy nie da się go rozpoznać, zostaje zwykły mebel.
        try {
            json inferred = furniture_configurator::infer_config(*candidate, group);
            auto taken = furniture_configurator::configure(*candidate, inferred, group);
            if (taken.second == group) candidate = taken.first;
        } catch (const std::exception& error) {
            journal::event(app, "biblioteka", std::string("mebel zostaje bez kreatora: ") + error.what());
        }
        relax_grain_for_sheet(app, *candidate, group);
        auto chosen = std::find_if(candidate->parts.begin(), candidate->parts.end(), [&](const core::Part& p) { return p.group == group; });
        if (chosen == candidate->parts.end()) throw py::StopIteration();
        return commit(app, candidate, chosen->id, std::nullopt, true);
    }
    if (kind == "shaped") {
        auto [candidate, group] = shaped_furniture::create_shaped_furniture(*app->project, payload.at("template_index").get<long long>());
        if (!app->project->parts.empty()) {
            double right = groups_right(*app->project);
            double lo = furniture_layout::group_bounds(*candidate, group).first[0];
            candidate = furniture_layout::move_group(*candidate, group, right + 50 - lo);
        }
        auto chosen = std::find_if(candidate->parts.begin(), candidate->parts.end(), [&](const core::Part& p) { return p.group == group; });
        if (chosen == candidate->parts.end()) throw py::StopIteration();
        return commit(app, candidate, chosen->id, std::nullopt, true);
    }
    if (kind == "hinge" || kind == "runner" || kind == "led" || (kind == "handle" && !payload.contains("option"))) {
        if (app->hardware_drag == nullptr) throw ValueError("Panel przeciągania okuć nie jest dostępny. Otwórz panel wyposażenia mebla.");
        app->hardware_drag->begin(kind, payload);
        select_design(app);
        return true;
    }
    if (kind == "appliance") {
        std::vector<core::Point3> points = room_objects::object_vertices(*app->project);
        double right = -50;
        if (!points.empty()) {
            right = points[0][0];
            for (auto& p : points) right = std::max(right, p[0]);
        }
        if (!app->project->parts.empty()) right = std::max(right, groups_right(*app->project));
        core::ProjectPtr candidate = room_objects::add_object(*app->project, payload.at("object_type").get<std::string>(), json{{"x", right + 50}});
        std::string object_id = candidate->scene.at("objects").back().at("id").get<std::string>();
        return commit(app, candidate, std::nullopt, object_id, true);
    }
    core::Part* part = app->selected_part();
    if (part == nullptr) {
        // Brak zaznaczenia: jeżeli w projekcie jest dokładnie jeden mebel, to jego dotyczy karta.
        std::set<std::string> groups;
        for (auto& p : app->project->parts) groups.insert(p.group);
        if (groups.size() == 1) {
            part = &app->project->parts.front();
            app->selected = part->id;
            app->selected_scene.reset();
        }
    }
    if (part == nullptr) throw ValueError("Najpierw zaznacz mebel lub jego część w projekcie.");
    std::string part_id = part->id, part_group = part->group;
    if (kind == "leg") {
        json corner = corner_furniture::read_corner_config(*app->project, part_group);
        json cfg = py::truthy(corner) ? corner : furniture_configurator::read_config(*app->project, part_group);
        if (cfg.is_null()) {
            try {
                cfg = furniture_configurator::infer_config(*app->project, part_group);
            } catch (const ValueError& error) {
                throw ValueError(std::string("Nogi z katalogu wymagają parametrycznego mebla lub narożnika. ") + error.what());
            }
        }
        double delta = num(payload.at("leg_height")) - num(py::get(cfg, "leg_height", 0));
        cfg["height"] = add_numbers(cfg.at("height"), delta);
        for (auto& [key, value] : payload.items()) cfg[key] = value;
        core::ProjectPtr candidate = py::truthy(corner) ? corner_furniture::create_corner(*app->project, cfg, part_group).first
                                                        : furniture_configurator::configure(*app->project, cfg, part_group).first;
        return commit(app, candidate, part_id);
    }
    if (kind == "handle") {
        core::ProjectPtr candidate = opening_options::install(*app->project, part_id, payload.at("option").get<std::string>());
        return commit(app, candidate, part_id);
    }
    if (kind == "material") {
        auto candidate = std::make_shared<core::Project>(*app->project);
        std::string ident;
        if (payload.contains("material_id")) {
            ident = payload.at("material_id").get<std::string>();
            bool known = std::any_of(candidate->materials.begin(), candidate->materials.end(), [&](const core::Material& m) { return m.id == ident; });
            if (!known) throw ValueError("Materiał pochodzi z poprzedniego projektu. Otwórz katalog ponownie.");
        } else {
            std::string name = payload.at("name").get<std::string>(), color = payload.at("color").get<std::string>();
            auto match = std::find_if(candidate->materials.begin(), candidate->materials.end(), [&](const core::Material& m) { return m.name == name && m.color == color; });
            if (match == candidate->materials.end()) {
                core::Material material;
                material.name = name;
                material.color = color;
                material.kind = payload.at("kind").get<std::string>();
                material.price_m2 = 0;
                candidate->materials.push_back(material);
                ident = material.id;
            } else ident = match->id;
        }
        auto target = std::find_if(candidate->parts.begin(), candidate->parts.end(), [&](const core::Part& p) { return p.id == part_id; });
        if (target == candidate->parts.end()) throw py::StopIteration();
        {
            // Mebel z kreatora lub narożnik: materiał dotyczy całego korpusu (albo frontów, gdy zaznaczono front).
            std::string role = assembly::inferred_role(*target);
            const char* key = (role == "door" || role == "drawer") ? "front_material" : "material";
            json corner = corner_furniture::read_corner_config(*candidate, part_group);
            json cfg = py::truthy(corner) ? corner : furniture_configurator::read_config(*candidate, part_group);
            if (cfg.is_null()) {
                try {
                    cfg = furniture_configurator::infer_config(*candidate, part_group);
                } catch (const std::exception&) {
                }
            }
            if (cfg.is_object()) {
                cfg[key] = ident;
                try {
                    core::ProjectPtr grouped = py::truthy(corner) ? corner_furniture::create_corner(*candidate, cfg, part_group).first
                                                                  : furniture_configurator::configure(*candidate, cfg, part_group).first;
                    journal::event(app, "biblioteka", std::format("materiał całego mebla {} → {} ({})", part_group, ident, key));
                    return commit(app, grouped, part_id);
                } catch (const std::exception& error) {
                    journal::event(app, "biblioteka", std::string("materiał tylko dla części (kreator odmówił): ") + error.what());
                }
            }
        }
        target->material = ident;
        return commit(app, candidate, part_id);
    }
    throw ValueError("Brak obsługi wybranej karty katalogu.");
}

// ---------------------------------------------------------------- VisualCatalogue

VisualCatalogue::VisualCatalogue(form_dialog::Form* parent, App* app_, const std::string& kind_, OnSelect on_select_)
    : ttk::Frame(parent, {{"padding", 10}}), app(app_), form(parent), kind(canonical_kind(kind_)), on_select(on_select_), generation(app_->project_generation) {
    kindvar = tk::StringVar(kind_label(kind));
    query = tk::StringVar();
    category = tk::StringVar("Wszystkie");
    note = tk::StringVar("Kliknij obraz lub Wybierz. Wymiary są w mm.");
    count = tk::StringVar();
    auto* head = new ttk::Frame(this);
    head->pack({{"fill", "x"}, {"pady", "0 7"}});
    (new ttk::Label(head, {{"text", "KATALOG GRAFICZNY"}, {"font", "{Segoe UI} 12 bold"}, {"foreground", "#4e6b3f"}}))->pack({{"side", "left"}});
    std::vector<std::string> labels;
    for (auto& [key, label] : KINDS) labels.push_back(label);
    kindcombo = new ttk::Combobox(head, {{"textvariable", kindvar}, {"values", labels}, {"state", "readonly"}, {"width", 22}});
    kindcombo->pack({{"side", "right"}});
    kindcombo->bind("<<ComboboxSelected>>", [this](tk::Event&) {
        for (auto& [key, label] : KINDS)
            if (label == kindvar.get()) { set_kind(key); return std::string(); }
        throw py::StopIteration();
    });
    auto* row = new ttk::Frame(this);
    row->pack({{"fill", "x"}, {"pady", "0 7"}});
    (new ttk::Label(row, {{"text", "Szukaj"}}))->pack({{"side", "left"}});
    search = new ttk::Entry(row, {{"textvariable", query}});
    search->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 7}});
    categorycombo = new ttk::Combobox(row, {{"textvariable", category}, {"state", "readonly"}, {"width", 25}});
    categorycombo->pack({{"side", "right"}});
    categorycombo->bind("<<ComboboxSelected>>", [this](tk::Event&) { render(); return std::string(); });
    query.trace_add("write", [this] { schedule(); });
    (new ttk::Label(this, {{"textvariable", count}, {"foreground", "#6b7266"}}))->pack({{"anchor", "w"}});
    auto* bottom = new ttk::Frame(this);
    bottom->pack({{"side", "bottom"}, {"fill", "x"}, {"pady", "7 0"}});
    choosebutton = new ttk::Button(bottom, {{"text", "Zastosuj wybrane"}, {"command", std::function<void()>([this] { activate_selected(); })}, {"state", "disabled"}});
    choosebutton->pack({{"side", "right"}, {"padx", "8 0"}});
    sourcebutton = new ttk::Button(bottom, {{"text", "Źródło"}, {"command", std::function<void()>([this] { open_source(); })}, {"state", "disabled"}});
    sourcebutton->pack({{"side", "right"}});
    (new ttk::Label(bottom, {{"textvariable", note}, {"wraplength", 650}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    auto* content = new ttk::Frame(this);
    content->pack({{"fill", "both"}, {"expand", true}});
    canvas = new tk::Canvas(content, {{"height", 410}, {"highlightthickness", 0}, {"bg", "#f3f0ea"}});
    canvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* sb = new ttk::Scrollbar(content, {{"orient", "vertical"}, {"command", canvas->path() + " yview"}});
    sb->pack({{"side", "right"}, {"fill", "y"}});
    canvas->configure({{"yscrollcommand", sb->path() + " set"}});
    gridbody = new ttk::Frame(canvas);
    window = canvas->create_window(0, 0, {{"window", gridbody->path()}, {"anchor", "nw"}});
    gridbody->bind("<Configure>", [this](tk::Event&) { responsive_ui::update_scrollregion(canvas); return std::string(); });
    canvas->bind("<Configure>", [this](tk::Event& e) { resize(e); return std::string(); });
    canvas->bind("<MouseWheel>", [this](tk::Event& e) { return wheel(e); });
    bind("<Destroy>", [this](tk::Event& e) { destroyed(e); return std::string(); }, true);
    set_kind(kind, on_select);
}

std::string VisualCatalogue::wheel(tk::Event& event) {
    canvas->yview_scroll(event.delta > 0 ? -1 : 1, "units");
    return "break";
}

void VisualCatalogue::destroyed(tk::Event& event) {
    if (event.widget == this) {
        if (job) {
            after_cancel(*job);
            job.reset();
        }
        if (app->_visual_catalogue_panel == this) app->_visual_catalogue_panel = nullptr;
    }
}

void VisualCatalogue::schedule() {
    if (job) after_cancel(*job);
    job = after(130, [this] { render(); });
}

void VisualCatalogue::resize(tk::Event& event) {
    canvas->itemconfigure(window, {{"width", event.width}});
    int new_columns = std::max(1, event.width / 215);
    if (new_columns != columns) {
        columns = new_columns;
        schedule();
    }
}

void VisualCatalogue::set_kind(const std::string& kind_in, OnSelect on_select_) {
    kind = canonical_kind(kind_in);
    on_select = on_select_;
    kindvar.set(kind_label(kind));
    generation = app->project_generation;
    items = catalogue_items(kind, app->project.get());
    category.set("Wszystkie");
    query.set("");
    selected.reset();
    kindcombo->configure({{"state", on_select ? "disabled" : "readonly"}});
    sourcebutton->configure({{"state", "disabled"}});
    std::set<std::string> categories;
    for (auto& item : items) categories.insert(item.at("category").get<std::string>());
    std::vector<std::string> values{"Wszystkie"};
    values.insert(values.end(), categories.begin(), categories.end());
    categorycombo->configure({{"values", values}});
    render();
    canvas->yview_moveto(0);
}

void VisualCatalogue::render() {
    if (job) {
        after_cancel(*job);
        job.reset();
    }
    for (tk::Widget* child : gridbody->winfo_children()) child->destroy();
    cards.clear();
    json filtered = filter_items(items, query.get(), category.get());
    count.set(std::format("{} / {} pozycji · ", filtered.size(), items.size()) + kind_label(kind));
    int cols = std::max(1, columns);
    int index = 0;
    for (auto& item : filtered) {
        auto* card = new ttk::Frame(gridbody, {{"padding", 7}, {"relief", "ridge"}});
        card->grid({{"row", index / cols}, {"column", index % cols}, {"padx", 4}, {"pady", 4}, {"sticky", "nsew"}});
        gridbody->columnconfigure(index % cols, {{"weight", 1}});
        auto* image = new tk::Canvas(card, {{"width", 190}, {"height", 118}, {"bg", "#fbfaf7"}, {"highlightthickness", 0}, {"cursor", "hand2"}});
        image->pack({{"fill", "x"}});
        catalogue_previews::draw_preview(image, item, 190, 118);
        auto* title = new ttk::Label(card, {{"text", item.at("title").get<std::string>()}, {"wraplength", 185}, {"font", "{Segoe UI} 9 bold"}});
        title->pack({{"fill", "x"}, {"pady", "3 0"}});
        (new ttk::Label(card, {{"text", dimension_text(item)}, {"wraplength", 185}, {"foreground", "#6b7266"}}))->pack({{"fill", "x"}});
        (new ttk::Label(card, {{"text", item.at("category").get<std::string>()}, {"wraplength", 185}, {"foreground", "#4e6b3f"}, {"font", "{Segoe UI} 8"}}))->pack({{"fill", "x"}});
        json copy = item;
        (new ttk::Button(card, {{"text", "Wybierz"}, {"command", std::function<void()>([this, copy] { activate(copy); })}}))->pack({{"fill", "x"}, {"pady", "5 0"}});
        image->bind("<ButtonRelease-1>", [this, copy](tk::Event&) { activate(copy); return std::string(); });
        title->bind("<ButtonRelease-1>", [this, copy](tk::Event&) { activate(copy); return std::string(); });
        for (tk::Widget* widget : {static_cast<tk::Widget*>(card), static_cast<tk::Widget*>(image), static_cast<tk::Widget*>(title)})
            widget->bind("<MouseWheel>", [this](tk::Event& e) { return wheel(e); });
        cards[item.at("id").get<std::string>()] = card;
        ++index;
    }
    bool still_listed = false;
    if (selected)
        for (auto& i : filtered)
            if (i.at("id") == selected->at("id")) { still_listed = true; break; }
    choosebutton->configure({{"state", still_listed ? "normal" : "disabled"}});
    form->mark_clean();
}

bool VisualCatalogue::activate_selected() { return selected ? activate(*selected) : false; }

void VisualCatalogue::open_source() {
    if (selected) {
        json source = py::get(*selected, "source", "");
        if (source.is_string() && source.get<std::string>().starts_with("https://")) py::webbrowser_open(source.get<std::string>());
    }
}

bool VisualCatalogue::activate(const json& item) {
    if (item.is_null() || (item.is_object() && item.empty())) return false;
    try {
        if (generation != app->project_generation) throw ValueError("Projekt zmienił się. Otwórz katalog ponownie.");
        form->ensure_current_project(true);
        if (on_select && !app->flush_parameter_drafts()) return false;
        bool result = on_select ? on_select(item) : apply_item(app, item);
        if (!result) return false;
        if (!winfo_exists()) return true;
        selected = item;
        choosebutton->configure({{"state", "normal"}});
        sourcebutton->configure({{"state", py::truthy(py::get(item, "source")) ? "normal" : "disabled"}});
        std::string item_kind = item.at("kind").get<std::string>();
        bool mounting = !on_select && (item_kind == "hinge" || item_kind == "runner" || item_kind == "led" || item_kind == "handle") && !item.at("payload").contains("option");
        std::string action = mounting ? "Wskaż miejsce na meblu, aby zamontować." : "Wybrano.";
        note.set(item.at("title").get<std::string>() + " · " + action + " " + item.at("note").get<std::string>());
        app->status.set("Katalog: " + item.at("title").get<std::string>() + " · " + action);
        form->mark_clean();
        return true;
    } catch (const std::invalid_argument& error) {  // ValueError
        note.set(error.what());
        return false;
    } catch (const std::out_of_range& error) {      // KeyError
        note.set(error.what());
        return false;
    } catch (const core::json::exception& error) {  // TypeError, KeyError z JSON
        note.set(error.what());
        return false;
    } catch (const py::StopIteration& error) {
        note.set(error.what());
        return false;
    }
}

VisualCatalogue* show(App* app, const std::string& kind_in, OnSelect on_select) {
    std::string kind = canonical_kind(kind_in);
    VisualCatalogue* panel = app->_visual_catalogue_panel;
    if (panel != nullptr) {
        try {
            if (panel->winfo_exists() && panel->generation == app->project_generation) {
                panel->set_kind(kind, on_select);
                panel->form->document->workspace->select(panel->form->document);
                return panel;
            }
        } catch (const tk::TclError&) {
        }
    }
    form_dialog::Form* form = app->dialog("Katalog graficzny", "900x680");
    panel = new VisualCatalogue(form, app, kind, on_select);
    panel->pack({{"fill", "both"}, {"expand", true}});
    app->_visual_catalogue_panel = panel;
    // Filters and card selection are presentation state; the actual backend
    // action commits immediately and is tracked by the project Undo stack.
    form->document->presentation_only = true;
    return panel;
}

void install(App* app) {
    app->visual_catalogue_open = [app](const std::string& kind, OnSelect on_select) -> VisualCatalogue* {
        // Bez własnej akcji wyboru katalog otwiera się w lewym panelu (biblioteka); z akcją — jako zakładka.
        if (!on_select && app->library != nullptr) { app->library->show_kind(kind); return nullptr; }
        return show(app, kind, on_select);
    };
    if (app->_catalogue_buttons_installed) return;
    app->catalogue_buttons.clear();
    std::set<std::string> captioned;
    struct Entry { const char* kind; const char* label; const char* page; };
    for (const Entry& entry : {Entry{"furniture", "Katalog mebli", "Mebel"}, Entry{"hinge", "Katalog okuć", "Mebel"}, Entry{"material", "Katalog materiałów", "Mebel"},
                               Entry{"appliance", "Katalog AGD", "Scena"}}) {
        auto parent = app->ribbon_pages.find(entry.page);
        if (parent == app->ribbon_pages.end() || parent->second == nullptr) continue;
        std::string kind = entry.kind;
        if (captioned.insert(entry.page).second) quick_studio_ui::_group(parent->second, "KATALOGI");
        auto* button = quick_studio_ui::_button(parent->second, entry.label, [app, kind] { app->visual_catalogue_open(kind, nullptr); });
        app->catalogue_buttons[kind] = button;
    }
    app->_catalogue_buttons_installed = !app->catalogue_buttons.empty();
}

}  // namespace visual_catalogue
