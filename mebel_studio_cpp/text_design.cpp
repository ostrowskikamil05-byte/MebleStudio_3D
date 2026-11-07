// Projekt z opisu zdaniem. Patrz text_design.h.
#include "text_design.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <set>

#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "furniture_library.h"
#include "opening_options.h"
#include "pyutil.h"

namespace text_design {

using core::Project;

std::string fold(const std::string& text) {
    // Małe litery ASCII; polskie znaki bez ogonków; inne znaki spoza ASCII (poza × = x) zamieniane na spację.
    static const std::map<std::string, char> pl{
        {"\xC4\x85", 'a'}, {"\xC4\x84", 'a'}, {"\xC4\x87", 'c'}, {"\xC4\x86", 'c'}, {"\xC4\x99", 'e'}, {"\xC4\x98", 'e'}, {"\xC5\x82", 'l'}, {"\xC5\x81", 'l'},
        {"\xC5\x84", 'n'}, {"\xC5\x83", 'n'}, {"\xC3\xB3", 'o'}, {"\xC3\x93", 'o'}, {"\xC5\x9B", 's'}, {"\xC5\x9A", 's'}, {"\xC5\xBA", 'z'}, {"\xC5\xB9", 'z'},
        {"\xC5\xBC", 'z'}, {"\xC5\xBB", 'z'}, {"\xC3\x97", 'x'}};
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) { out += (char)std::tolower(c); ++i; continue; }
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        auto found = pl.find(text.substr(i, len));
        out += found != pl.end() ? found->second : ' ';
        i += len;
    }
    return out;
}

namespace {

struct Tok {
    std::string w;      // słowo (po fold) albo separator „,”
    bool num = false;
    double v = 0;
    bool sep = false;
    bool used = false;
};

bool starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
bool starts_any(const std::string& s, std::initializer_list<const char*> prefixes) {
    for (const char* p : prefixes) if (starts(s, p)) return true;
    return false;
}

std::vector<Tok> tokenize(const std::string& text) {
    std::string f = fold(text);
    std::vector<Tok> out;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (std::isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < f.size() && std::isdigit((unsigned char)f[j])) ++j;
            if (j + 1 < f.size() && (f[j] == ',' || f[j] == '.') && std::isdigit((unsigned char)f[j + 1])) {
                ++j;
                while (j < f.size() && std::isdigit((unsigned char)f[j])) ++j;
            }
            std::string raw = f.substr(i, j - i);
            std::replace(raw.begin(), raw.end(), ',', '.');
            Tok t; t.num = true; t.v = std::stod(raw); t.w = raw;
            out.push_back(t);
            i = j;
        } else if (std::isalpha((unsigned char)c)) {
            size_t j = i;
            while (j < f.size() && std::isalpha((unsigned char)f[j])) ++j;
            Tok t; t.w = f.substr(i, j - i);
            out.push_back(t);
            i = j;
        } else if (c == ',' || c == ';' || c == '\n' || c == '+') {
            Tok t; t.sep = true; t.w = ",";
            out.push_back(t);
            ++i;
        } else {
            ++i;
        }
    }
    return out;
}

std::optional<int> count_word(const std::string& w) {
    static const std::vector<std::pair<std::vector<const char*>, int>> words{
        {{"jeden", "jedna", "jedno", "jedne", "jedn"}, 1}, {{"dwa", "dwie", "dwoje", "dwoch", "dwoma", "dwiema"}, 2}, {{"trzy", "trzech", "trzema"}, 3},
        {{"cztery", "czterech", "czterema"}, 4}, {{"piec", "pieciu"}, 5}, {{"szesc", "szesciu"}, 6}, {{"siedem", "siedmiu"}, 7}, {{"osiem", "osmiu"}, 8},
        {{"dziewiec", "dziewieciu"}, 9}, {{"dziesiec", "dziesieciu"}, 10}};
    for (const auto& [list, n] : words) for (const char* x : list) if (w == x) return n;
    return std::nullopt;
}

bool is_preposition(const std::string& w) {
    static const std::set<std::string> p{"z", "ze", "na", "po", "w", "o", "bez", "od", "do", "dla", "przy", "pod", "nad", "i", "oraz", "plus", "a"};
    return p.contains(w);
}

bool is_noun(const std::string& w) {
    return starts_any(w, {"szaf", "komod", "regal", "slupek", "slupk", "witryn", "kontener", "wysp", "cargo", "modul", "mebel", "biurk", "lozk", "stol", "lawk",
                          "toaletk", "skrzyn", "kosz", "zabudow"});
}

// Czy pozycja zaczyna się od „[liczba] wiszące / dolne / górne …” bez rzeczownika?
bool implicit_noun(const std::vector<Tok>& chunk) {
    size_t i = 0;
    if (i < chunk.size() && (chunk[i].num || count_word(chunk[i].w))) ++i;
    if (i < chunk.size() && chunk[i].w == "x") ++i;
    return i < chunk.size() && !chunk[i].num && starts_any(chunk[i].w, {"wisz", "gorn", "doln", "podblat", "nadblat", "stoja"});
}

bool chunk_has_noun(const std::vector<Tok>& chunk) {
    for (const auto& t : chunk) if (!t.num && !t.sep && is_noun(t.w)) return true;
    return implicit_noun(chunk);
}

const core::Material* find_material(const Project& project, const std::string& word);

// Kawałek bez rzeczownika, który opisuje tylko wygląd / okucia (front, korpus, uchwyty, kolor, nogi, LED …), dotyczy wszystkich mebli.
bool is_global_modifier(const std::vector<Tok>& chunk, const Project& project) {
    bool style = false;
    for (const auto& t : chunk) {
        if (t.num) continue;
        if (starts_any(t.w, {"drzwi", "szuflad", "polk", "komor", "kolumn", "sekcj", "poziom", "przegrod", "otwart", "plec", "po"}) && t.w != "pod") return false;
        if (starts_any(t.w, {"front", "korpus", "wnetrz", "uchwyt", "klamk", "galk", "led", "nog", "nozk", "cokol", "blat", "tip", "push", "salice", "ramow", "frezowan", "gladk", "bezuchwyt", "oswietl"}) || find_material(project, t.w))
            style = true;
    }
    return style;
}

struct Chunks {
    std::vector<std::vector<Tok>> items;
    std::vector<Tok> globals;
};

// Dzieli opis na pozycje: separatory, „oraz”, „plus” i „i”; kawałki bez rzeczownika wracają do poprzedniej pozycji
// (albo, gdy opisują tylko wygląd i okucia, dotyczą wszystkich mebli).
Chunks split_chunks(const std::vector<Tok>& tokens, const Project& project) {
    std::vector<std::vector<Tok>> raw(1);
    for (const auto& t : tokens) {
        if (t.sep || t.w == "oraz" || t.w == "plus" || t.w == "i" || t.w == "tez" || t.w == "a") { raw.emplace_back(); continue; }
        raw.back().push_back(t);
    }
    Chunks out;
    for (auto& chunk : raw) {
        if (chunk.empty()) continue;
        if (chunk_has_noun(chunk) || out.items.empty()) {
            out.items.push_back(std::move(chunk));
        } else if (is_global_modifier(chunk, project)) {
            out.globals.insert(out.globals.end(), chunk.begin(), chunk.end());
        } else {
            Tok sep; sep.sep = true; sep.w = ",";
            out.items.back().push_back(sep);
            out.items.back().insert(out.items.back().end(), chunk.begin(), chunk.end());
        }
    }
    return out;
}

enum class Unit { None, Mm, Cm, M };

std::optional<Unit> unit_of(const std::string& w) {
    if (w == "mm" || starts(w, "milimetr")) return Unit::Mm;
    if (w == "cm" || starts(w, "centymetr")) return Unit::Cm;
    if (w == "m" || w == "metr" || w == "metra" || w == "metry" || w == "metrow" || w == "metrowa" || w == "metrowy") return Unit::M;
    return std::nullopt;
}

double to_mm(double v, Unit unit) { return unit == Unit::Mm ? v : unit == Unit::Cm ? v * 10 : v * 1000; }

// Liczba bez jednostki: <=10 metry, <=300 centymetry, więcej milimetry.
double guess_mm(double v) { return v <= 10 ? v * 1000 : v <= 300 ? v * 10 : v; }

struct Dimension { char role = 0; double mm = 0; bool explicit_unit = false; size_t at = 0; };

struct Parsed {
    std::optional<double> w, h, d;
    std::optional<double> each_width;  // „po 60 cm”
};

struct Context {
    const Project& project;
    std::vector<std::string>& ignored;
};

bool negated(const std::vector<Tok>& t, size_t i) {
    // „bez półek”, „bez żadnych półek” — zaprzeczenie dotyczy tylko słowa stojącego bezpośrednio po „bez”
    if (i >= 1 && t[i - 1].w == "bez") return true;
    if (i >= 2 && t[i - 2].w == "bez" && starts(t[i - 1].w, "zadn")) return true;
    return false;
}

// Liczba stojąca bezpośrednio przed słowem (bez jednostki), np. „2 drzwi”, „trzy szuflady”, „2 x drzwi”.
std::optional<int> count_before(std::vector<Tok>& t, size_t i) {
    size_t k = i;
    if (k >= 1 && t[k - 1].w == "x") --k;
    if (k >= 1) {
        const Tok& p = t[k - 1];
        if (p.num && p.v == std::floor(p.v) && p.v >= 0 && p.v <= 50) {
            bool unit_after = k < t.size() && unit_of(t[k].w) && k == i;  // „2 m” nie jest ilością
            if (!unit_after) { t[k - 1].used = true; if (k != i) t[i - 1].used = true; return (int)p.v; }
        }
        if (auto n = count_word(p.w)) { t[k - 1].used = true; if (k != i) t[i - 1].used = true; return *n; }
    }
    return std::nullopt;
}

const core::Material* find_material(const Project& project, const std::string& word) {
    static const std::set<std::string> generic{"mat", "lite", "naturalny", "plyta", "plyty", "kolor", "kolorze"};
    if (word.size() < 3 || generic.contains(word)) return nullptr;
    for (const auto& m : project.materials) {
        std::string name = fold(m.name);
        std::string token;
        for (size_t i = 0; i <= name.size(); ++i) {
            if (i < name.size() && std::isalnum((unsigned char)name[i])) { token += name[i]; continue; }
            if (token.size() >= 3 && !generic.contains(token)) {
                size_t need = std::min<size_t>({4, token.size(), word.size()});
                if (token.compare(0, need, word, 0, need) == 0) return &m;
            }
            token.clear();
        }
    }
    return nullptr;
}

json& section_of(json& cfg, const std::string& key) {
    for (auto& s : cfg["sections"]) if (s["key"] == key) return s;
    throw std::runtime_error("brak strefy " + key);
}

std::string pl_count(int n, const char* one, const char* few, const char* many) {
    int m10 = n % 10, m100 = n % 100;
    return std::format("{} {}", n, n == 1 ? one : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many);
}

std::string describe(const json& cfg, const Project& project) {
    std::string out = std::format("{:g} × {:g} × {:g} mm", cfg["width"].get<double>(), cfg["height"].get<double>(), cfg["depth"].get<double>());
    for (const auto& s : cfg["sections"]) {
        if (!s["enabled"].get<bool>()) continue;
        std::string front = s["front"].get<std::string>();
        std::string where = s["key"] == "bottom" ? "dół: " : s["key"] == "upper" ? "góra: " : "";
        if (front == "doors") out += " · " + where + pl_count(s["doors"].get<int>(), "drzwi", "drzwi", "drzwi");
        else if (front == "drawers") out += " · " + where + pl_count(s["drawers"].get<int>(), "szuflada", "szuflady", "szuflad");
        else if (front == "cargo") out += " · " + where + pl_count(s["drawers"].get<int>(), "poziom cargo", "poziomy cargo", "poziomów cargo");
        else if (front == "open") out += " · " + where + "otwarta";
        if (front != "drawers" && front != "cargo" && s["shelves"].get<int>() > 0) out += ", " + pl_count(s["shelves"].get<int>(), "półka", "półki", "półek");
        if (s["columns"].get<int>() > 1) out += ", " + pl_count(s["columns"].get<int>(), "komora", "komory", "komór");
    }
    auto name_of = [&](const json& id) {
        for (const auto& m : project.materials) if (id.is_string() && m.id == id.get<std::string>()) return m.name;
        return std::string("?");
    };
    out += " · korpus " + name_of(cfg["material"]) + ", front " + name_of(cfg["front_material"]);
    std::string mode = cfg["handle_mode"].get<std::string>();
    if (mode == "none") out += " · bez uchwytów";
    else if (mode == "handle") out += cfg["handle_spacing"].get<double>() == 0 ? " · gałki" : std::format(" · uchwyty {:g} mm", cfg["handle_spacing"].get<double>());
    else if (opening_options::OPTIONS.contains(mode)) out += " · " + opening_options::OPTIONS.at(mode).at("label").get<std::string>();
    if (cfg["led"].get<bool>()) out += " · LED";
    return out;
}

// Sprawdza konfigurację tak samo jak późniejsze tworzenie mebla. TIP-ON (i inne systemy otwierania bez uchwytu) nie pasuje do każdego
// frontu — wtedy wracamy do zwykłych uchwytów i mówimy o tym użytkownikowi, zamiast odrzucać cały mebel.
void validate_item(Item& item, const Project& project, std::vector<std::string>& ignored, bool had_opening_system) {
    // Wynik sprawdzenia zależy tylko od konfiguracji (identyczne szafki w kuchni sprawdzamy raz); pamięć podręczna jest krótka.
    static std::map<std::string, std::string> cache;
    auto check = [&](const json& cfg) -> std::string {
        std::string key = cfg.dump();
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
        std::string error;
        try { furniture_configurator::configure(project, cfg); } catch (const std::exception& e) { error = e.what(); }
        if (cache.size() > 400) cache.clear();
        cache[key] = error;
        return error;
    };
    std::string error = check(item.config);
    if (error.empty()) return;
    if (!had_opening_system) { item.error = error; item.config = json(); return; }
    json retry = item.config;
    retry["handle_mode"] = "handle";
    retry["hinge_spring"] = "unknown";
    error = check(retry);
    try {
        if (!error.empty()) throw std::runtime_error(error);
        item.config = retry;
        item.description = describe(item.config, project);
        ignored.push_back("TIP-ON / system otwierania (nie pasuje do frontów tego mebla — użyto uchwytów)");
    } catch (const std::exception& e) {
        item.error = e.what();
        item.config = json();
    }
}

// Pełna analiza jednego kawałka opisu.
Item parse_chunk(std::vector<Tok> t, const Project& project, std::vector<std::string>& ignored) {
    Item item;
    for (const auto& tok : t) item.source += (item.source.empty() ? "" : " ") + tok.w;

    // --- rodzaj mebla i liczba sztuk
    auto has = [&](const char* stem) {
        bool found = false;
        for (auto& x : t) if (!x.num && starts(x.w, stem)) { x.used = true; found = true; }
        return found;
    };
    auto mark = [&](std::initializer_list<const char*> stems) { for (auto& x : t) if (!x.num && starts_any(x.w, stems)) x.used = true; };
    size_t noun = t.size();
    for (size_t i = 0; i < t.size(); ++i) if (!t[i].num && is_noun(t[i].w)) { noun = i; break; }
    if (noun == t.size()) for (size_t i = 0; i < t.size(); ++i) if (!t[i].num && starts_any(t[i].w, {"wisz", "gorn", "doln", "podblat", "nadblat", "stoja"})) { noun = i; break; }
    int count = 1;
    if (noun < t.size()) {
        for (size_t k = 1; k <= 3 && k <= noun; ++k) {
            size_t i = noun - k;
            Tok& p = t[i];
            if (is_preposition(p.w) && !p.num) break;
            bool plain_number = p.num && p.v == std::floor(p.v) && p.v >= 1 && p.v <= 50 && !(i + 1 < t.size() && unit_of(t[i + 1].w));
            if (plain_number || count_word(p.w)) { count = plain_number ? (int)p.v : *count_word(p.w); p.used = true; break; }
            if (p.w == "x") { p.used = true; continue; }
        }
    }
    item.count = std::clamp(count, 1, 30);
    mark({"szaf", "komod", "regal", "slupek", "slupk", "witryn", "kontener", "wysp", "cargo", "modul", "mebel", "kosz", "zabudow", "wisz", "gorn", "doln", "podblat", "nadblat", "stoja"});

    // --- wymiary
    std::vector<Dimension> dims;
    std::optional<double> each_width;
    for (size_t i = 0; i < t.size(); ++i) {
        if (!t[i].num || t[i].used) continue;
        size_t end = i;
        Unit unit = Unit::None;
        if (i + 1 < t.size()) if (auto u = unit_of(t[i + 1].w)) { unit = *u; end = i + 1; }
        // łańcuch „60x72x56”
        std::vector<std::pair<size_t, double>> chain{{i, t[i].v}};
        std::vector<Unit> chain_units{unit};
        size_t last = end;
        while (last + 2 < t.size() && t[last + 1].w == "x" && t[last + 2].num) {
            size_t j = last + 2;
            Unit u2 = Unit::None;
            size_t e2 = j;
            if (j + 1 < t.size()) if (auto uu = unit_of(t[j + 1].w)) { u2 = *uu; e2 = j + 1; }
            chain.push_back({j, t[j].v}); chain_units.push_back(u2);
            last = e2;
        }
        if (chain.size() >= 2) {
            Unit shared = Unit::None;
            for (Unit u : chain_units) if (u != Unit::None) shared = u;
            const char roles[3] = {'w', 'h', 'd'};
            for (size_t k = 0; k < chain.size() && k < 3; ++k) {
                Unit u = chain_units[k] != Unit::None ? chain_units[k] : shared;
                dims.push_back({roles[k], u != Unit::None ? to_mm(chain[k].second, u) : guess_mm(chain[k].second), u != Unit::None, chain[k].first});
            }
            for (size_t k = i; k <= last; ++k) t[k].used = true;
            i = last;
            continue;
        }
        // pojedyncza liczba — rola z sąsiednich słów
        char role = 0;
        auto role_of = [&](size_t at) -> char {
            if (t[at].used || t[at].num) return 0;
            const std::string& w = t[at].w;
            if (starts(w, "szer")) return 'w';
            if (starts(w, "wys")) return 'h';
            if (starts(w, "gleb")) return 'd';
            if (starts(w, "dlug")) return 'w';
            return 0;
        };
        size_t role_at = t.size();
        if (i >= 1 && role_of(i - 1)) role_at = i - 1;
        else if (i >= 2 && (t[i - 1].w == "o" || t[i - 1].w == "ok" || t[i - 1].w == "okolo") && role_of(i - 2)) role_at = i - 2;
        else if (end + 1 < t.size() && role_of(end + 1)) role_at = end + 1;
        if (role_at < t.size()) role = role_of(role_at);
        bool per_piece = i >= 1 && t[i - 1].w == "po";
        double mm = unit != Unit::None ? to_mm(t[i].v, unit) : guess_mm(t[i].v);
        if (per_piece) { each_width = mm; for (size_t k = i - 1; k <= end; ++k) t[k].used = true; i = end; continue; }
        if (role) {
            dims.push_back({role, mm, unit != Unit::None, i});
            for (size_t k = i; k <= end; ++k) t[k].used = true;
            t[role_at].used = true;
            i = end;
            continue;
        }
        if (unit != Unit::None) {  // liczba z jednostką bez słowa kluczowego — rozstrzygnie dalsza analiza
            dims.push_back({'?', mm, true, i});
            for (size_t k = i; k <= end; ++k) t[k].used = true;
        }
        i = end;
    }

    // --- elementy wyposażenia
    int doors = -1, drawers = -1, shelves = -1, columns = 0, levels = 0;
    bool open = false, cargo = has("cargo") || has("wysuwan"), led = false, led_off = false, back_off = false;
    bool drawers_top = false, no_handle = false, plinth = false, worktop_on = false, worktop_off = false;
    std::optional<double> leg_height, handle_spacing;
    std::string handle_color, handle_mode, front_style;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].num) continue;
        const std::string& w = t[i].w;
        bool no = negated(t, i);
        if (starts_any(w, {"drzwi", "drzwiczk"}) || starts_any(w, {"dwudrzw", "jednodrzw", "trzydrzw"})) {
            t[i].used = true;
            if (starts(w, "dwudrzw")) { doors = 2; continue; }
            if (starts(w, "jednodrzw")) { doors = 1; continue; }
            if (starts(w, "trzydrzw")) { doors = 3; continue; }
            if (no) { open = true; continue; }
            auto n = count_before(t, i);
            doors = n ? *n : 0;  // 0 = domyślnie według szerokości
        } else if (starts(w, "dwuskrzyd")) { doors = 2; t[i].used = true; }
        else if (starts(w, "szuflad")) {
            t[i].used = true;
            if (no) { drawers = 0; continue; }
            auto n = count_before(t, i);
            drawers = n ? *n : (w == "szuflada" || w == "szuflade" || w == "szuflad" ? 1 : 3);
            for (size_t k = i + 1; k < t.size() && k <= i + 3; ++k) {
                if (starts_any(t[k].w, {"na", "u"}) && k + 1 < t.size() && starts_any(t[k + 1].w, {"gorz", "gor"})) { drawers_top = true; t[k].used = t[k + 1].used = true; }
                if (starts_any(t[k].w, {"na", "u"}) && k + 1 < t.size() && starts_any(t[k + 1].w, {"dol"})) { t[k].used = t[k + 1].used = true; }
                if (t[k].sep || t[k].num) break;
            }
        } else if (starts(w, "polk") || w == "polek") {
            t[i].used = true;
            if (no) { shelves = 0; continue; }
            auto n = count_before(t, i);
            shelves = n ? *n : (w == "polka" || w == "polke" || w == "polce" ? 1 : 2);
        } else if (starts_any(w, {"komor", "kolumn", "sekcj", "przegrod"}) && !has("regal")) {
            t[i].used = true;
            if (auto n = count_before(t, i)) columns = *n;
        } else if (starts(w, "poziom") || (starts(w, "kosz") && cargo)) {
            t[i].used = true;
            if (auto n = count_before(t, i)) { levels = *n; cargo = true; }
        } else if (w == "led" || starts_any(w, {"oswietl", "podswietl", "tasm"})) {
            t[i].used = true;
            (no ? led_off : led) = true;
        } else if (starts_any(w, {"nog", "nozk"})) {
            t[i].used = true;
            if (no) { leg_height = 0; continue; }
            for (size_t k = i + 1; k < t.size() && k <= i + 3; ++k) {
                for (auto& dim : dims) if (dim.at == k && dim.role == '?') { leg_height = dim.mm; dim.role = 'x'; t[k].used = true; }
            }
            if (i >= 1) for (auto& dim : dims) if (dim.at == i - 1 && dim.role == '?') { leg_height = dim.mm; dim.role = 'x'; }
        } else if (starts(w, "cokol")) {
            t[i].used = true;
            plinth = !no;
        } else if (starts(w, "blat")) {
            t[i].used = true;
            (no ? worktop_off : worktop_on) = true;
        } else if (starts(w, "plec")) {
            t[i].used = true;
            if (no) back_off = true;
        } else if (starts(w, "otwart") || starts(w, "bezfront")) {
            t[i].used = true;
            open = true;
        } else if (starts_any(w, {"uchwyt", "klamk", "galk"})) {
            t[i].used = true;
            if (no) { no_handle = true; continue; }
            if (starts(w, "galk")) handle_spacing = 0;
            // rozstaw: najbliższa liczba w mm obok słowa
            for (size_t k = (i >= 2 ? i - 2 : 0); k < t.size() && k <= i + 3; ++k) {
                if (!t[k].num || t[k].used) continue;
                double v = t[k].v;
                bool is_mm = (k + 1 < t.size() && unit_of(t[k + 1].w) == Unit::Mm) || (!(k + 1 < t.size() && unit_of(t[k + 1].w)) && v >= 32 && v <= 800);
                if (is_mm) { handle_spacing = v; t[k].used = true; if (k + 1 < t.size() && unit_of(t[k + 1].w)) t[k + 1].used = true; for (auto& dim : dims) if (dim.at == k) dim.role = 'x'; break; }
            }
            for (size_t k : {i >= 1 ? i - 1 : i, i + 1}) {  // kolor tuż przed albo tuż po słowie „uchwyt”
                if (k >= t.size() || k == i || t[k].num || t[k].used || find_material(project, t[k].w)) continue;
                if (starts(t[k].w, "czarn")) { handle_color = "#242628"; t[k].used = true; }
                else if (starts_any(t[k].w, {"zlot", "mosi"})) { handle_color = "#c9a227"; t[k].used = true; }
                else if (starts_any(t[k].w, {"chrom", "srebrn", "stal", "inox"})) { handle_color = "#b8bcc2"; t[k].used = true; }
            }
        } else if (w == "tipon" || (w == "tip" && i + 1 < t.size() && t[i + 1].w == "on") || w == "push" || starts(w, "bezuchwyt")) {
            t[i].used = true;
            if (w == "tip" || w == "tipon" || w == "push") { handle_mode = "tip_short"; if (w == "tip") t[i + 1].used = true; }
            else no_handle = true;
        } else if (starts(w, "salice")) {
            t[i].used = true; handle_mode = "salice_push";
        } else if (starts_any(w, {"ramow", "ramka"})) {
            t[i].used = true; front_style = "frame";
        } else if (starts(w, "frezowan") || w == "finger") {
            t[i].used = true; front_style = "finger";
        } else if (starts_any(w, {"gladk", "plask"})) {
            t[i].used = true; front_style = "smooth";
        }
    }

    // --- materiały
    std::string body_material, front_material;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].num || t[i].used || starts_any(t[i].w, {"front", "korpus", "wnetrz", "bok", "drzw"})) continue;
        const core::Material* m = find_material(project, t[i].w);
        if (!m) continue;
        t[i].used = true;
        bool for_front = false, for_body = false;
        for (size_t k = 1; k <= 3 && k <= i; ++k) {
            const std::string& p = t[i - k].w;
            if (t[i - k].sep || find_material(project, p)) break;
            if (starts_any(p, {"front", "drzw"})) { for_front = true; break; }
            if (starts_any(p, {"korpus", "wnetrz", "bok"})) { for_body = true; break; }
        }
        if (!for_front && !for_body && i + 1 < t.size()) {
            if (starts_any(t[i + 1].w, {"front", "drzw"})) for_front = true;
            else if (starts_any(t[i + 1].w, {"korpus", "wnetrz"})) for_body = true;
        }
        if (for_front) front_material = m->id;
        else if (for_body) body_material = m->id;
        else { front_material = m->id; body_material = m->id; }
    }
    mark({"front", "korpus", "wnetrz", "kolor", "w", "z", "ze", "na"});

    // --- wzorzec
    bool unsupported = false;
    long long index = 0;
    std::string unsupported_name;
    double want_w = 0, want_h = 0;
    for (const auto& d : dims) { if (d.role == 'w') want_w = d.mm; if (d.role == 'h') want_h = d.mm; }
    if (each_width) want_w = *each_width;
    if (has("kosz") && has("pran")) index = 52;
    else if (has("pralk")) index = 49;
    else if (cargo) index = (has("slupek") || has("slupk") || want_h >= 1200 || (want_h == 0 && !has("doln") && !has("podblat") && !has("szafk"))) ? 4 : 53;
    else if (has("wysp")) index = 7;
    else if (has("zlew")) index = (has("listw") || has("otwart")) ? 35 : 2;
    else if (has("umywal")) index = 19;
    else if (has("piekarnik")) index = (has("slupek") || has("slupk") || has("wysok")) ? 34 : 6;
    else if (has("spizar")) index = 5;
    else if (has("garderob")) index = 39;
    else if (std::any_of(t.begin(), t.end(), [](const Tok& x) { return !x.num && starts(x.w, "szaf") && !starts(x.w, "szafk"); })) {
        if (has("przesuwn") || has("drzwiprzesuw")) { index = 9; unsupported = true; unsupported_name = "szafa przesuwna"; }
        else index = has("warsztat") ? 46 : 8;
    }
    else if (has("komod")) index = has("bielizn") ? 42 : has("warsztat") ? 48 : (doors > 0 || has("drzwi")) ? 18 : 10;
    else if (has("kontener")) index = 44;
    else if (has("nocn")) index = 11;
    else if (has("rtv") || has("telewiz")) index = 14;
    else if (has("witryn")) index = 15;
    else if (has("regal")) index = (has("dwustron") || has("przegrod")) ? 43 : has("biur") ? 33 : has("warsztat") ? 23 : has("recznik") ? 22 : has("lazienk") ? (has("wask") ? 50 : 22) : has("donic") ? 30 : 16;
    else if (has("slupek") || has("slupk")) index = has("lazienk") ? 20 : has("piekarnik") ? 34 : 5;
    else if (has("buty") || has("obuw")) index = (has("wysok") || want_h >= 1200) ? 41 : 40;
    else if (has("segregator")) index = 45;
    else if (has("narzedzi")) index = 25;
    else if (has("garaz")) index = has("gorn") ? 47 : 25;
    else if (has("czesci")) index = 26;
    else if (has("biurk")) { index = 31; unsupported = true; unsupported_name = "biurko"; }
    else if (has("lozk")) { index = 12; unsupported = true; unsupported_name = "łóżko"; }
    else if (has("toaletk")) { index = 13; unsupported = true; unsupported_name = "toaletka"; }
    else if (has("stol")) { index = has("kawow") ? 17 : 24; unsupported = true; unsupported_name = "stół"; }
    else if (has("lawk")) { index = 28; unsupported = true; unsupported_name = "ławka"; }
    else if (has("skrzyn")) { index = 29; unsupported = true; unsupported_name = "skrzynia"; }
    else if (has("wisz") || has("gorn") || has("nadblat")) {
        index = has("klap") ? 21 : has("poziom") && !has("poziomy") ? 37 : (has("wask") || (want_w > 0 && want_w <= 450)) ? 36 : 1;
        if (index == 21) { unsupported = true; unsupported_name = "szafka z klapą"; }
    }
    else if (drawers > 0 && doors < 0 && shelves < 0 && !open) index = 3;
    else index = 0;
    item.template_index = index;
    const auto& tpl = furniture_library::template_at(index);
    item.label = tpl.name;
    item.wall = tpl.name.find("wisząc") != std::string::npos || tpl.name.find("górna") != std::string::npos;
    if (unsupported) {
        item.error = "Typ „" + unsupported_name + "” nie jest obsługiwany przez szybki kreator — dodaj go z Biblioteki wszystkich mebli.";
        return item;
    }

    // --- konfiguracja
    try {
        json cfg = furniture_configurator::default_config(index, project);
        for (auto& d : dims) {
            if (d.role == '?') { if (want_w == 0 && !each_width) { want_w = d.mm; d.role = 'w'; } else continue; }
            if (d.role == 'w') cfg["width"] = d.mm;
            else if (d.role == 'h') cfg["height"] = d.mm;
            else if (d.role == 'd') cfg["depth"] = d.mm;
        }
        if (each_width) cfg["width"] = *each_width;
        double width = cfg["width"].get<double>();
        if (!body_material.empty()) cfg["material"] = body_material;
        if (!front_material.empty()) cfg["front_material"] = front_material;
        if (item.wall && !leg_height && cfg["leg_height"].get<double>() > 0) leg_height = 0;  // szafki wiszące nie stoją na nogach
        if (leg_height) {
            double old_legs = cfg["leg_height"].get<double>();
            cfg["leg_height"] = *leg_height;
            if (!std::any_of(dims.begin(), dims.end(), [](const Dimension& d) { return d.role == 'h'; })) cfg["height"] = cfg["height"].get<double>() - old_legs + *leg_height;
        }
        if (plinth) cfg["plinth"] = true;
        if (worktop_on) cfg["worktop"] = true;
        if (worktop_off) cfg["worktop"] = false;
        if (back_off) cfg["back"] = false;
        if (led) cfg["led"] = true;
        if (led_off) cfg["led"] = false;
        if (handle_spacing) cfg["handle_spacing"] = *handle_spacing;
        if (!handle_color.empty()) cfg["handle_color"] = handle_color;
        if (no_handle) cfg["handle_mode"] = "none";
        if (!handle_mode.empty()) {
            cfg["handle_mode"] = handle_mode;
            const json& spec = opening_options::OPTIONS.at(handle_mode);
            if (spec.contains("hinge_spring")) cfg["hinge_spring"] = spec["hinge_spring"];
        }
        if (!front_style.empty()) cfg["front_style"] = front_style;

        json& main = section_of(cfg, "main");
        if (columns > 1) main["columns"] = columns;
        if (cargo && tpl.kind != "cargo") { main["front"] = "cargo"; main["shelves"] = 0; main["drawers"] = levels > 0 ? levels : 2; }
        else if (cargo && levels > 0) main["drawers"] = levels;
        else if (open) { main["front"] = "open"; main["doors"] = 1; }
        else {
            int d = drawers, dr = doors;
            if (dr == 0) dr = width <= 650 ? 1 : 2;
            if (d > 0 && dr > 0) {
                // drzwi i szuflady: szuflady w osobnej strefie (domyślnie na dole), drzwi w głównej
                json& zone = section_of(cfg, drawers_top ? "upper" : "bottom");
                zone["enabled"] = true; zone["front"] = "drawers"; zone["drawers"] = d; zone["shelves"] = 0;
                zone["height"] = std::clamp(d * 170.0, 150.0, 450.0);
                main["front"] = "doors"; main["doors"] = dr;
            } else if (d > 0) {
                main["front"] = "drawers"; main["drawers"] = d; main["shelves"] = 0;
            } else if (dr > 0) {
                main["front"] = "doors"; main["doors"] = dr;
            }
            if (shelves >= 0 && main["front"] != "drawers" && main["front"] != "cargo") main["shelves"] = shelves;
        }
        if (shelves >= 0 && main["front"] == "open") main["shelves"] = shelves;
        if (starts(handle_mode, "tip") && cfg["height"].get<double>() > 1250) {  // krótki TIP-ON obsługuje fronty do ok. 1300 mm
            cfg["handle_mode"] = "tip_long";
            const json& spec = opening_options::OPTIONS.at("tip_long");
            if (spec.contains("hinge_spring")) cfg["hinge_spring"] = spec["hinge_spring"];
        }
        item.config = furniture_configurator::normalize_config(cfg, project);
        item.description = describe(item.config, project);
        validate_item(item, project, ignored, !handle_mode.empty());
    } catch (const std::exception& e) {
        item.error = e.what();
        item.config = json();
    }

    // --- co zostało niezrozumiałe
    static const std::set<std::string> filler{"z", "ze", "na", "po", "w", "o", "bez", "od", "do", "dla", "przy", "pod", "nad", "x", "cm", "mm", "m", "ok", "okolo", "kolorze", "kolor", "to", "ma", "jest",
                                              "chce", "potrzebuje", "potrzebny", "potrzebna", "zrob", "zaprojektuj", "dodaj", "stworz", "prosze", "mi", "mnie", "sie", "oraz", "tez", "jakis", "jakas", "ktory", "ktora",
                                              "dolna", "dolne", "dolny", "kuchenna", "kuchenne", "stojaca", "stojace", "wiszaca", "wiszace", "gorna", "gorne", "wysoka", "wysoki", "niska", "niski", "waska", "waski", "duza", "duzy",
                                              "szerokosc", "szerokosci", "szeroka", "szeroki", "wysokosc", "wysokosci", "glebokosc", "glebokosci", "dlugosc", "dlugosci", "metr", "metra", "metry", "centymetry", "milimetry",
                                              "tip", "on", "sztuki", "sztuk", "szt", "kazda", "kazde", "kazdy", "po", "jedna", "jeden", "pod", "zlew", "zlewem", "zlewu", "umywalke", "umywalka", "piekarnik", "piekarnika",
                                              "pralke", "pralka", "pranie", "buty", "obuwie", "ubrania", "naczynia", "ksiazki", "ksiazkowy", "ksiazkowa", "komorami", "komory", "dwoch", "mieszcza", "mieszczaca"};
    for (const auto& tok : t) {
        if (tok.used || tok.sep) continue;
        if (tok.num) { ignored.push_back(tok.w); continue; }
        if (tok.w.size() >= 4 && !filler.contains(tok.w) && !count_word(tok.w) && !is_noun(tok.w) && !starts_any(tok.w, {"szaf", "regal", "komod", "kuchni", "mebl", "dolne", "dolna", "wisz", "kolor", "front", "korpus", "drzw", "szuflad", "polk", "ustaw", "umiesc", "pomiesc"}))
            ignored.push_back(tok.w);
    }
    return item;
}

// "kuchnia 3 m" bez wyliczenia mebli: zlew + szafki dolne + takie same wiszące.
std::optional<std::vector<Item>> kitchen_items(const std::vector<Tok>& tokens, const Project& project, std::vector<std::string>& ignored) {
    bool kitchen = false;
    for (const auto& t : tokens) if (!t.num && starts(t.w, "kuchni")) kitchen = true;
    if (!kitchen) return std::nullopt;
    for (const auto& t : tokens) if (!t.num && is_noun(t.w) && !starts(t.w, "kuchni")) return std::nullopt;  // użytkownik wymienił meble
    double length = 0;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (!tokens[i].num) continue;
        if (auto u = unit_of(tokens[i + 1].w)) length = to_mm(tokens[i].v, *u);
        else if (tokens[i].v >= 1.2 && tokens[i].v <= 10) length = tokens[i].v * 1000;
    }
    if (length < 1200 || length > 12000) return std::nullopt;  // kuchnia od 1,2 do 12 m
    double sink = 800, remaining = length - sink;
    bool sink_included = remaining >= 400;
    if (!sink_included) { sink = 0; remaining = length; }
    // moduły po 600 mm; reszta jako jeden węższy moduł (min. 300 mm) albo dołożona do ostatniego (max 900 mm)
    int full = (int)std::floor(remaining / 600.0);
    double leftover = remaining - full * 600.0;
    std::vector<double> widths(full, 600.0);
    if (leftover >= 300) widths.push_back(std::round(leftover / 50) * 50);
    else if (leftover > 0 && !widths.empty()) widths.back() += std::round(leftover / 50) * 50;
    else if (leftover > 0) widths.push_back(300);
    std::vector<std::pair<long long, double>> floor_row;
    if (sink_included) floor_row.push_back({2, sink});
    for (size_t i = 0; i < widths.size(); ++i) floor_row.push_back({i == 0 ? 3 : 0, widths[i]});
    std::vector<Item> out;
    auto add = [&](long long index, double width, bool wall) {
        Item item;
        const auto& tpl = furniture_library::template_at(index);
        item.template_index = index; item.label = tpl.name; item.wall = wall; item.source = "kuchnia";
        try {
            json cfg = furniture_configurator::default_config(index, project);
            cfg["width"] = width;
            if (index == 0) { json& main = section_of(cfg, "main"); main["doors"] = width <= 650 ? 1 : 2; }
            if (wall && cfg["leg_height"].get<double>() > 0) {  // szafki wiszące nie stoją na nogach
                cfg["height"] = cfg["height"].get<double>() - cfg["leg_height"].get<double>();
                cfg["leg_height"] = 0;
            }
            item.config = furniture_configurator::normalize_config(cfg, project);
            item.description = describe(item.config, project);
            validate_item(item, project, ignored, false);
        } catch (const std::exception& e) { item.error = e.what(); }
        out.push_back(item);
    };
    for (const auto& [index, width] : floor_row) add(index, width, false);
    for (const auto& [index, width] : floor_row) add(1, width, true);
    return out;
}

}  // namespace

namespace {
constexpr int MAX_FURNITURE = 60;  // jeden opis nie tworzy więcej mebli
}

int Result::furniture_count() const {
    int n = 0;
    for (const auto& item : items) if (item.error.empty()) n += item.count;
    return n;
}

Result parse(const std::string& text, const Project& project) {
    Result result;
    auto tokens = tokenize(text);
    if (tokens.empty()) return result;
    auto limit = [&result] {
        int total = 0;
        for (auto& item : result.items) {
            if (!item.error.empty()) continue;
            if (total + item.count > MAX_FURNITURE) { item.error = std::format("Za dużo mebli w jednym opisie (limit {}).", MAX_FURNITURE); item.config = json(); continue; }
            total += item.count;
        }
    };
    if (auto kitchen = kitchen_items(tokens, project, result.ignored)) {
        result.items = *kitchen;
        result.kitchen_layout = true;
        limit();
        return result;
    }
    auto chunks = split_chunks(tokens, project);
    for (auto& chunk : chunks.items) {
        if (!chunk_has_noun(chunk)) { for (const auto& t : chunk) if (!t.sep) result.ignored.push_back(t.w); continue; }
        std::vector<Tok> all = chunks.globals;  // ogólne opisy najpierw, żeby zapis przy meblu miał pierwszeństwo
        all.insert(all.end(), chunk.begin(), chunk.end());
        result.items.push_back(parse_chunk(all, project, result.ignored));
    }
    limit();
    return result;
}

std::string summary(const Result& result) {
    if (result.items.empty()) return "Nie rozpoznałem żadnego mebla. Opisz np.: „szafka dolna 80 cm z dwiema szufladami i drzwiami, front biały”.";
    std::string out = result.kitchen_layout ? "Zaproponowałem układ kuchni (zlew + szafki dolne i wiszące):\n" : "Rozumiem:\n";
    for (const auto& item : result.items) {
        if (!item.error.empty()) { out += "  ✗ " + item.label + " — " + item.error + "\n"; continue; }
        out += std::format("  • {} × {}{} — {}\n", item.count, item.label, item.wall && item.label.find("wisz") == std::string::npos ? " (wisząca)" : "", item.description);
    }
    if (!result.ignored.empty()) {
        out += "Pominąłem: ";
        for (size_t i = 0; i < result.ignored.size() && i < 12; ++i) out += (i ? ", " : "") + result.ignored[i];
        out += "\n";
    }
    return out;
}

std::pair<core::ProjectPtr, std::vector<std::string>> build(const Project& project, const Result& result) {
    auto current = std::make_shared<Project>(project);
    std::vector<std::string> groups;
    double start_x = 0;
    if (!project.parts.empty()) {
        bool first = true;
        for (const auto& g : furniture_layout::groups(project)) {
            double hi_x = furniture_layout::group_bounds(project, g).second[0];
            if (first || hi_x > start_x) start_x = hi_x;
            first = false;
        }
        start_x += 50;
    }
    double floor_x = start_x, wall_x = start_x;
    std::optional<double> floor_back;
    for (const auto& item : result.items) {
        if (!item.error.empty()) continue;
        for (int k = 0; k < item.count; ++k) {
            auto [made, group] = furniture_configurator::configure(*current, item.config);
            auto box = furniture_layout::group_bounds(*made, group);
            double& cursor = item.wall ? wall_x : floor_x;
            double dx = cursor - box.first[0];
            double dy = item.wall && floor_back ? *floor_back - box.second[1] : 0;
            double dz = item.wall ? 1400 - box.first[2] : 0;
            made = furniture_layout::move_group(*made, group, dx, dy, dz);
            auto placed = furniture_layout::group_bounds(*made, group);
            cursor = placed.second[0];
            if (!item.wall && !floor_back) floor_back = placed.second[1];
            current = made;
            groups.push_back(group);
        }
    }
    return {current, groups};
}

}  // namespace text_design
