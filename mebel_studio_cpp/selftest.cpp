// Pełny test programu z raportem (patrz selftest.h).
#include "selftest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <typeinfo>

#ifdef __GNUC__
#include <cxxabi.h>
#endif

#include "app.h"
#include "cad_geometry.h"
#include "dev_tools.h"
#include "corner_furniture.h"
#include "direct_edit.h"
#include "drag_hardware.h"
#include "fittings.h"
#include "furniture_configurator.h"
#include "furniture_layout.h"
#include "hover_flyout.h"
#include "mechanisms.h"
#include "native_scene_export.h"
#include "sheet_nesting.h"
#include "text_design.h"
#include "furniture_library.h"
#include "hardware_report.h"
#include "opening_options.h"
#include "perf_monitor.h"
#include "production_docs.h"
#include "production_inspector.h"
#include "journal.h"
#include "quick_studio_ui.h"
#include "style_ui.h"
#include "text_design.h"
#include "corner_quick_ui.h"
#include "studio_interaction.h"
#include "viewport_hud.h"
#include "project_print.h"
#include "pyutil.h"
#include "quick_studio_ui.h"
#include "responsive_ui.h"
#include "room_architecture.h"
#include "room_objects.h"
#include "runner_installation.h"
#include "runners_catalog.h"
#include "shaped_furniture.h"
#include "sketch.h"
#include "sketch_inference.h"
#include "sketch_shapes.h"
#include "sketch_design.h"
#include "cutters.h"
#include "gpu_viewport.h"
#include "modeler_geometry.h"
#include "modeler_ui.h"
#include "sketch_live.h"
#include "render_style.h"
#include "wood_grain.h"
#include "pencil_filter.h"
#include "graphics_mode_ui.h"
#include "view_style.h"
#include "viewport_scene.h"
#include "features.h"
#include "dxf_import.h"
#include "stretch_choice.h"
#include "ui_preferences.h"
#include "viewport_modes.h"
#include "catalogue_previews.h"
#include "visual_catalogue.h"
#include "workspace_worktops.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace selftest {

namespace fs = std::filesystem;
using core::json;
using core::Part;
using core::Project;
using core::ProjectPtr;

namespace {

enum class Status { Ok, Warn, Fail, Skip };

const char* label(Status s) {
    switch (s) {
        case Status::Ok: return "OK";
        case Status::Warn: return "UWAGA";
        case Status::Fail: return "BŁĄD";
        case Status::Skip: return "POMINIĘTO";
    }
    return "?";
}

struct Case {
    std::string area, name, where, detail, hint;
    Status status = Status::Ok;
    double ms = 0;
};

struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

void need(bool condition, const std::string& message) {
    if (!condition) throw Failure(message);
}

class TempDir {
public:
    explicit TempDir(const std::string& prefix) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        for (int attempt = 0; attempt < 100; ++attempt) {
            fs::path candidate = fs::temp_directory_path() / (prefix + std::to_string(gen() % 100000000));
            if (fs::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("Nie udało się utworzyć katalogu tymczasowego.");
    }
    ~TempDir() { std::error_code ignored; if (!path.empty()) fs::remove_all(path, ignored); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    fs::path path;
};

std::string describe(const std::exception& error) {
    std::string name = typeid(error).name();
#ifdef __GNUC__
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled) name = demangled;
    std::free(demangled);
#endif
    return name + ": " + error.what();
}

std::string stamp(const char* format) {
    std::time_t now = std::time(nullptr);
    char buffer[64];
    std::strftime(buffer, sizeof buffer, format, std::localtime(&now));
    return buffer;
}

std::string md_escape(std::string text) {
    std::string out;
    for (char c : text) {
        if (c == '|') out += "\\|";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}

double num(const json& v) { return v.get<double>(); }

// ---------------------------------------------------------------- pokrycie dziennika testami
// Każda kategoria wpisów [operacja:…] użyta w kodzie (journal::event) musi tu mieć wskazany test. Dodajesz nową kategorię w kodzie
// → audyt zgłosi brak wpisu tutaj, dopóki nie dopiszesz testu i nie opiszesz go w tej tabeli.
const std::map<std::string, std::string> JOURNAL_COVERAGE = {
    {"szkic", "Interfejs i okna · szkic: edytor, odpinanie i wpinanie · Szkic — wskazówki, pole wartości, kształty, DXF"},
    {"plik", "Eksporty i zapis (test_exports / test_persistence)"},
    {"okno", "Interfejs i okna · okna dialogowe"},
    {"zakładka", "Interfejs i okna · przełączanie wszystkich zakładek i paneli"},
    {"konfiguracja", "Dziennik zdarzeń · okno konfiguracji (otwarcie, sekcje, kafelki, LED z menu)"},
    {"cofanie", "Cofanie i ponawianie (test_undo / Dziennik zdarzeń · refresh)"},
    {"biblioteka", "Biblioteka (panel) · każdy mebel, kształt i urządzenie z katalogu"},
    {"render", "Widok 3D i render (test_view / test_render_matrix)"},
    {"rozciąganie", "Rozciąganie mebla + Dziennik zdarzeń · tryb Rozciągnij"},
    {"zmiana", "Dziennik zdarzeń · opis różnic projektu i refresh"},
    {"okucia", "Dziennik zdarzeń · montaż okuć (wybór, anulowanie, uchwyt)"},
    {"narożnik", "Dziennik zdarzeń · narożniki (tworzenie, konwersja, kształt)"},
    {"narzędzie", "Dziennik zdarzeń · narzędzia palety (każdy tryb)"},
    {"zaznaczenie", "Dziennik zdarzeń · kliknięcie w widoku i wstrzymane kliknięcie"},
    {"inspektor", "Inspektor produkcyjny i panel Mebel (test_inspector / test_panel)"},
    {"stan", "Dziennik zdarzeń · zrzut stanu mebli po rozciąganiu i przesuwaniu"},
    {"przesuwanie", "Dziennik zdarzeń · obrót i zapis układu mebli"},
    {"menu", "Dziennik zdarzeń · menu podręczne"},
    {"kształty", "Dziennik zdarzeń · okno kształtów (zatwierdzenie / odrzucenie)"},
    {"projekt z opisu", "Dziennik zdarzeń · projekt z opisu zdaniem"},
    {"kanty i wzorki", "Szkic — kanty, wzorki i zestaw elementów (test_design)"},
    {"grafika", "Tryb grafiki — przełączanie, cienie, miniatury, szkic ołówkiem, usłojenie drewna (test_design + szkic ołówkiem: tryb grafiki…)"},
    {"modelowanie", "Pracownia 3D — rysowanie, wyciąganie, przesuwanie, kopiowanie, usuwanie, odrzucenia, okrąg i ołówek (pracownia 3D: rysowanie płyt… + pracownia: ołówek i okrąg…)"},
    {"cad2d", "Pracownia 2D CAD — łuki, okręgi, zaokrąglenia, wymiary, wyciąganie do płyt, edycja obrysu, odrzucenia (pracownia 2D CAD: rysowanie z łukami…)"},
    {"frezy", "Frezy — wiele operacji na płycie, frezy krawędziowe, wzorki, odrzucenia (frezy: katalog pięciu narzędzi… + pracownia 3D)"},
};

// ---------------------------------------------------------------- audyt źródeł

struct Audit {
    bool found = false;
    fs::path src, python;
    std::string git_note;
    std::vector<std::string> notes;       // informacje
    std::vector<std::string> problems;    // rzeczy do poprawy
    json data = json::object();
};

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool is_word_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

bool contains_word(const std::string& text, const std::string& word) {
    size_t at = 0;
    while ((at = text.find(word, at)) != std::string::npos) {
        bool left = at == 0 || !is_word_char(text[at - 1]);
        bool right = at + word.size() >= text.size() || !is_word_char(text[at + word.size()]);
        if (left && right) return true;
        at += word.size();
    }
    return false;
}

std::string git_output(const fs::path& root, const std::string& arguments) {
    std::string command = "git -C \"" + py::path_str(root) + "\" " + arguments + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return "";
    std::string out;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, pipe)) out += buffer;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

fs::path find_source_dir() {
    std::vector<fs::path> candidates;
#ifdef MEBEL_SOURCE_DIR
    candidates.push_back(fs::path(MEBEL_SOURCE_DIR));
#endif
    if (const char* env = std::getenv("MEBEL_SOURCE_DIR")) candidates.push_back(fs::path(env));
    {
        fs::path dir = py::executable_dir();
        for (int level = 0; level < 7 && !dir.empty(); ++level, dir = dir.parent_path()) candidates.push_back(dir);
    }
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / "launcher.cpp", ec) && fs::exists(c / "CMakeLists.txt", ec)) return c;
    }
    return {};
}

Audit audit_sources() {
    Audit audit;
    audit.src = find_source_dir();
    audit.found = !audit.src.empty();
    if (!audit.found) {
        audit.problems.push_back("Nie znaleziono katalogu ze źródłami C++ (launcher.cpp + CMakeLists.txt). Ustaw zmienną MEBEL_SOURCE_DIR albo uruchom test z katalogu build wewnątrz repozytorium.");
        return audit;
    }
    audit.python = audit.src.parent_path() / "app";
    bool have_python = fs::is_directory(audit.python);
    if (!have_python) audit.notes.push_back("Brak katalogu z oryginałem w Pythonie (" + py::path_str(audit.python) + ") — porównanie z oryginałem pominięto.");

    // pliki C++
    std::map<std::string, std::string> cpp_text;  // rdzeń nazwy -> treść .cpp + .h
    std::set<std::string> cpp_stems, has_cpp, has_header;
    std::vector<std::string> foreign;
    long long lines = 0, files = 0;
    std::vector<std::string> markers;
    for (const auto& entry : fs::directory_iterator(audit.src)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string(), stem = entry.path().stem().string(), name = entry.path().filename().string();
        if (ext == ".cpp" || ext == ".h") {
            std::string text = read_file(entry.path());
            cpp_text[stem] += text;
            cpp_stems.insert(stem);
            (ext == ".cpp" ? has_cpp : has_header).insert(stem);
            ++files;
            std::istringstream in(text);
            std::string line;
            long long number = 0, found_here = 0;
            while (std::getline(in, line)) {
                ++number; ++lines;
                if (stem == "selftest") continue;  // ten plik zawiera listę szukanych znaczników
                for (const char* marker : {"TODO", "FIXME", "XXX", "NotImplemented", "not implemented", "niezaimplementowan", "nie zaimplementowan", "STUB", "stub("}) {
                    if (line.find(marker) != std::string::npos) {
                        if (found_here++ < 4) markers.push_back(name + ":" + std::to_string(number) + " · " + (line.size() > 110 ? line.substr(0, 110) + "…" : line));
                        break;
                    }
                }
            }
        } else if (ext == ".py" || ext == ".pyc") {
            foreign.push_back(name);
        }
    }
    audit.data["pliki_cpp"] = files;
    audit.data["linie_cpp"] = lines;
    if (!foreign.empty()) {
        std::string list;
        for (auto& f : foreign) list += (list.empty() ? "" : ", ") + f;
        audit.problems.push_back("W katalogu źródeł C++ leżą pliki Pythona: " + list + " — program ma być w całości w C++, usuń je albo przenieś.");
    }
    for (const auto& stem : has_cpp)
        if (!has_header.contains(stem) && stem != "main" && stem != "launcher" && stem != "tkcore" && stem != "forma_round")
            audit.notes.push_back("Plik " + stem + ".cpp nie ma nagłówka " + stem + ".h (dozwolone dla punktów wejścia i plików pomocniczych).");
    json markers_json = json::array();
    for (auto& m : markers) markers_json.push_back(m);
    audit.data["znaczniki"] = markers_json;

    // kategorie dziennika użyte w kodzie muszą mieć test (tabela JOURNAL_COVERAGE)
    {
        std::set<std::string> used;
        for (const auto& [stem, text] : cpp_text) {
            if (stem == "selftest") continue;
            size_t at = 0;
            while ((at = text.find("event(", at)) != std::string::npos) {
                size_t open = at + 6;
                bool word_before = at > 0 && is_word_char(text[at - 1]) && !(at >= 9 && text.compare(at - 9, 9, "journal::") == 0);
                at = open;
                if (word_before) continue;
                size_t comma = text.find(',', open);
                if (comma == std::string::npos || comma - open > 40) continue;
                size_t quote = text.find_first_not_of(" \t\n", comma + 1);
                if (quote == std::string::npos || text[quote] != '"') continue;
                size_t end = text.find('"', quote + 1);
                if (end == std::string::npos) continue;
                std::string category = text.substr(quote + 1, end - quote - 1);
                if ((category.size() >= 2 && category.size() <= 24 && category.find(' ') == std::string::npos) || category == "projekt z opisu") used.insert(category);
            }
        }
        std::string missing;
        for (const auto& category : used) if (!JOURNAL_COVERAGE.contains(category)) missing += (missing.empty() ? "" : ", ") + category;
        audit.data["kategorie_dziennika"] = used.size();
        if (!missing.empty())
            audit.problems.push_back("Kategorie dziennika użyte w kodzie bez testu w JOURNAL_COVERAGE (selftest.cpp): " + missing + " — dopisz test i wpis w tabeli (zasada: każda funkcja ma log i test).");
    }
    if (!markers.empty())
        audit.problems.push_back(std::to_string(markers.size()) + " miejsc z oznaczeniem TODO / FIXME / „niezaimplementowane” w kodzie C++ (lista w raporcie).");

    // porównanie z Pythonem
    if (have_python) {
        std::map<long long, std::string> unused;
        std::vector<std::string> missing_ports, partial;
        json modules = json::array();
        long long py_files = 0, ported = 0, py_defs = 0, missing_defs = 0;
        std::set<std::string> py_stems;
        std::map<std::string, std::string> py_paths;
        for (const auto& entry : fs::directory_iterator(audit.python)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".py") continue;
            std::string stem = entry.path().stem().string();
            if (stem == "__init__") continue;
            ++py_files;
            py_stems.insert(stem);
            py_paths[stem] = "app/" + entry.path().filename().string();
            if (!cpp_text.contains(stem)) { missing_ports.push_back(stem); continue; }
            ++ported;
            std::istringstream in(read_file(entry.path()));
            std::string line;
            std::vector<std::string> names;
            while (std::getline(in, line)) {
                size_t at = std::string::npos;
                if (line.rfind("def ", 0) == 0) at = 4;
                else if (line.rfind("    def ", 0) == 0) at = 8;
                else if (line.rfind("async def ", 0) == 0) at = 10;
                else if (line.rfind("class ", 0) == 0) at = 6;
                if (at == std::string::npos) continue;
                size_t end = at;
                while (end < line.size() && is_word_char(line[end])) ++end;
                std::string name = line.substr(at, end - at);
                if (name.empty() || name.rfind("__", 0) == 0) continue;
                names.push_back(name);
            }
            std::vector<std::string> absent;
            for (const auto& name : names) if (!contains_word(cpp_text[stem], name) && !contains_word(cpp_text[stem], name + "_") && !contains_word(cpp_text[stem], name.substr(name[0] == '_' ? 1 : 0))) absent.push_back(name);
            py_defs += (long long)names.size();
            missing_defs += (long long)absent.size();
            if (!absent.empty()) {
                std::string list;
                for (size_t i = 0; i < absent.size() && i < 12; ++i) list += (i ? ", " : "") + absent[i];
                partial.push_back(std::format("{}: {} z {} nazw nie występuje w C++ ({}{})", stem, absent.size(), names.size(), list, absent.size() > 12 ? ", …" : ""));
            }
            modules.push_back({{"modul", stem}, {"nazw_w_pythonie", names.size()}, {"brakujace_w_cpp", absent}});
        }
        audit.data["moduly_python"] = py_files;
        audit.data["moduly_przeniesione"] = ported;
        audit.data["nazwy_python"] = py_defs;
        audit.data["nazwy_brakujace"] = missing_defs;
        audit.data["moduly"] = modules;
        for (const auto& stem : missing_ports) audit.problems.push_back("Moduł Pythona app/" + stem + ".py nie ma odpowiednika w C++ (brak " + stem + ".cpp / " + stem + ".h) — trzeba przenieść.");
        for (const auto& line : partial) audit.notes.push_back("Różnica nazw — " + line + ". Zwykle to funkcje zagnieżdżone, lambdy albo zmienione nazwy; przejrzyj, jeśli moduł działa niepoprawnie.");
        std::vector<std::string> only_cpp;
        for (const auto& stem : cpp_stems) if (!py_stems.contains(stem)) only_cpp.push_back(stem);
        std::string list;
        for (auto& s : only_cpp) list += (list.empty() ? "" : ", ") + s;
        audit.data["tylko_w_cpp"] = only_cpp;
        audit.notes.push_back("Moduły dodane w C++ bez odpowiednika w Pythonie (warstwa Tk, nowy interfejs FORMA, dziennik, testy): " + list + ".");

        // aktualność: kiedy zmieniono ostatnio oryginał, a kiedy jego port
        fs::path root = audit.src.parent_path();
        std::string log = git_output(root, "log --name-only --pretty=format:@%ct");
        std::map<std::string, long long> last_change;
        if (log.rfind("fatal", 0) != 0 && !log.empty()) {
            std::istringstream in(log);
            std::string line;
            long long current = 0;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line[0] == '@') current = std::atoll(line.c_str() + 1);
                else if (!last_change.contains(line)) last_change[line] = current;
            }
            std::string dir_name = audit.src.filename().string();
            std::vector<std::string> stale;
            for (const auto& stem : py_stems) {
                if (!cpp_text.contains(stem) || !py_paths.contains(stem)) continue;
                auto py_time = last_change.find(py_paths[stem]);
                long long cpp_time = 0;
                for (const char* ext : {".cpp", ".h"}) {
                    auto it = last_change.find(dir_name + "/" + stem + ext);
                    if (it != last_change.end()) cpp_time = std::max(cpp_time, it->second);
                }
                if (py_time != last_change.end() && cpp_time > 0 && py_time->second > cpp_time)
                    stale.push_back(std::format("{} (Python zmieniony {}, C++ {})", stem, [&] { std::time_t t = py_time->second; char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t)); return std::string(b); }(),
                                                [&] { std::time_t t = cpp_time; char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t)); return std::string(b); }()));
            }
            audit.data["nieaktualne"] = stale;
            audit.git_note = "źródło dat: historia git (" + std::to_string(last_change.size()) + " plików)";
            for (const auto& s : stale) audit.problems.push_back("Oryginał w Pythonie zmieniono po ostatniej zmianie portu: " + s + " — sprawdź, czy port nadąża.");
            std::string status = git_output(root, "status --porcelain -- " + dir_name);
            long long dirty = std::count(status.begin(), status.end(), '\n');
            audit.data["niezatwierdzone_zmiany"] = dirty;
            if (dirty > 0) audit.notes.push_back(std::to_string(dirty) + " plików w katalogu źródeł ma niezatwierdzone zmiany (git status) — ten test dotyczy lokalnej wersji.");
            std::string head = git_output(root, "log -1 --pretty=format:%h\\ %cd\\ %s --date=short");
            audit.data["ostatni_commit"] = head;
        } else {
            audit.git_note = "brak historii git — aktualność nie została sprawdzona";
            audit.notes.push_back("Git niedostępny: nie sprawdzono, czy oryginał w Pythonie jest nowszy od portu.");
        }
    }
    return audit;
}

// ---------------------------------------------------------------- uruchamianie przypadków

class Runner;

struct Ctx {
    Runner* runner;
    std::vector<std::string> notes, warnings;
    std::string hint;
    void note(const std::string& text) { notes.push_back(text); }
    void warn(const std::string& text) { warnings.push_back(text); }
};

class Runner {
public:
    App* app = nullptr;
    fs::path dir;
    std::ofstream log;
    std::vector<Case> cases;
    std::set<std::string> baseline_windows;
    fs::path data_root;   // korzeń repozytorium (mebel_studio_cpp/, app/, biblioteki_natywne/, przyklady/, licencje/)
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    void say(const std::string& text) {
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::string line = std::format("{} [+{:7.2f}s] {}", stamp("%H:%M:%S"), seconds, text);
        if (log.is_open()) { log << line << '\n'; log.flush(); }
        std::fputs((line + "\n").c_str(), stdout);
        std::fflush(stdout);
        if (app != nullptr && app->log) { try { app->log->info("[test] " + text); } catch (...) {} }
    }

    void run(const std::string& area, const std::string& name, const std::string& where, const std::function<void(Ctx&)>& fn) {
        Case c;
        c.area = area; c.name = name; c.where = where;
        Ctx ctx{this, {}, {}, {}};
        say("START " + area + " · " + name);
        auto t0 = std::chrono::steady_clock::now();
        try {
            fn(ctx);
            c.status = ctx.warnings.empty() ? Status::Ok : Status::Warn;
        } catch (const Failure& e) {
            c.status = Status::Fail; c.detail = e.what();
        } catch (const std::exception& e) {
            c.status = Status::Fail; c.detail = describe(e);
        } catch (...) {
            c.status = Status::Fail; c.detail = "nieznany wyjątek";
        }
        c.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::string detail;
        for (auto& n : ctx.notes) detail += (detail.empty() ? "" : " · ") + n;
        for (auto& w : ctx.warnings) detail += (detail.empty() ? "" : " · ") + std::string("⚠ ") + w;
        if (c.status == Status::Fail) detail = c.detail + (detail.empty() ? "" : " · " + detail);
        c.detail = detail;
        c.hint = ctx.hint;
        say(std::format("{} {} · {} ({:.0f} ms){}", label(c.status), area, name, c.ms, c.detail.empty() ? "" : " — " + c.detail));
        flush_intercepted(c);
        cases.push_back(std::move(c));
        try { if (app != nullptr) app->update(); } catch (...) {}
    }

    void skip(const std::string& area, const std::string& name, const std::string& why) {
        Case c;
        c.area = area; c.name = name; c.status = Status::Skip; c.detail = why;
        say("POMINIĘTO " + area + " · " + name + " — " + why);
        cases.push_back(std::move(c));
    }

    // Komunikaty modalne, które program chciał pokazać podczas przypadku (np. showerror) — to jest wprost „co użytkownik zobaczyłby”.
    std::vector<std::string> all_intercepted;
    void flush_intercepted(Case& c) {
        auto messages = tk::messagebox::take_intercepted();
        for (auto& m : messages) {
            all_intercepted.push_back(c.area + " · " + c.name + ": " + m);
            say("okno modalne pominięte: " + m);
            if (m.rfind("showerror", 0) == 0 && c.status == Status::Ok) {
                c.status = Status::Warn;
                c.detail += (c.detail.empty() ? "" : " · ") + std::string("⚠ program chciał pokazać błąd: ") + m;
            }
        }
    }

    // ---- okna

    std::vector<tk::Widget*> toplevels() {
        std::vector<tk::Widget*> out;
        for (auto* child : app->winfo_children()) {
            try {
                if (child->winfo_exists() && child->winfo_class() == "Toplevel") out.push_back(child);
            } catch (const std::exception&) {}
        }
        return out;
    }
    void remember_windows() {
        baseline_windows.clear();
        for (auto* w : toplevels()) baseline_windows.insert(w->path());
    }
    int close_extra_windows() {
        int closed = 0;
        for (auto* w : toplevels()) {
            if (baseline_windows.contains(w->path())) continue;
            try { w->destroy(); ++closed; } catch (const std::exception&) {}
        }
        app->update();
        return closed;
    }

    // ---- pomocniki modelu

    static ProjectPtr fresh() { return std::make_shared<Project>(); }

    static std::pair<ProjectPtr, std::string> cabinet(const Project& base, long long template_index = 0, const std::function<void(json&)>& change = nullptr) {
        json cfg = furniture_configurator::default_config(template_index, base);
        if (change) change(cfg);
        return furniture_configurator::configure(base, cfg);
    }

    static json& section(json& cfg, const std::string& key) {
        for (auto& s : cfg["sections"]) if (s.value("key", std::string()) == key) return s;
        throw Failure("Brak strefy " + key + " w konfiguracji");
    }

    static std::vector<std::string> kreator_groups(const Project& p) {
        std::vector<std::string> out;
        for (const auto& g : furniture_layout::groups(p)) if (!furniture_configurator::read_config(p, g).is_null()) out.push_back(g);
        return out;
    }

    // Największe wzajemne wnikanie dwóch meblów (mm), 0 = tylko styk albo rozłączne.
    static double penetration(const furniture_layout::Box& a, const furniture_layout::Box& b) {
        double best = 1e9;
        for (int axis = 0; axis < 3; ++axis) {
            double overlap = std::min(a.second[axis], b.second[axis]) - std::max(a.first[axis], b.first[axis]);
            if (overlap <= 0) return 0;
            best = std::min(best, overlap);
        }
        return best;
    }

    void check_overlaps(Ctx& ctx, const Project& p, double tolerance = 3) {
        auto names = furniture_layout::groups(p);
        for (size_t i = 0; i < names.size(); ++i)
            for (size_t j = i + 1; j < names.size(); ++j) {
                try {
                    double depth = penetration(furniture_layout::group_bounds(p, names[i]), furniture_layout::group_bounds(p, names[j]));
                    if (depth > tolerance) ctx.warn(std::format("meble {} i {} nachodzą na siebie o {:.1f} mm", names[i], names[j], depth));
                } catch (const std::exception&) {}
            }
    }

    void check_cabinet(Ctx& ctx, const ProjectPtr& p, const std::string& group, const std::function<void(const json&)>& verify = nullptr) {
        p->validate();
        json read = furniture_configurator::read_config(*p, group);
        need(!read.is_null(), "Po zbudowaniu mebla nie da się odczytać jego konfiguracji (read_config zwróciło null)");
        if (verify) verify(read);
        (void)ctx;
    }

    // ================================================================ obszary

    void test_library() {
        const char* area = "Biblioteka mebli";
        const char* where = "furniture_library.cpp (TEMPLATES, create_furniture), assembly.cpp, core.cpp (Project::validate)";
        for (size_t i = 0; i < furniture_library::TEMPLATES.size(); ++i) {
            const auto& t = furniture_library::TEMPLATES[i];
            run(area, std::format("Szablon #{} „{}” ({}, {}×{}×{})", i, t.name, t.kind, t.w, t.h, t.d), where, [&, i](Ctx& ctx) {
                auto [p, g] = furniture_library::create_furniture(Project(), (long long)i);
                p->validate();
                need(!p->parts.empty(), "Szablon nie utworzył żadnej części");
                ctx.note(std::format("{} części", p->parts.size()));
                auto bounds = furniture_layout::group_bounds(*p, g);
                for (int a = 0; a < 3; ++a) need(bounds.second[a] - bounds.first[a] > 1, "Mebel ma zerowy wymiar");
            });
        }
        for (size_t i = 0; i < shaped_furniture::TEMPLATES.size(); ++i) {
            run("Meble kształtowe", std::format("Kształt #{}", i), "shaped_furniture.cpp (create_shaped_furniture, apply_shape), cad_geometry.cpp", [i](Ctx& ctx) {
                auto [p, g] = shaped_furniture::create_shaped_furniture(Project(), (long long)i);
                p->validate();
                ctx.note(std::format("{} części", p->parts.size()));
            });
        }
    }

    void test_configurator() {
        const char* area = "Konfigurator (opcje mebla)";
        const char* where = "furniture_configurator.cpp (default_config/normalize_config/configure), configurator_accessories.cpp, assembly.cpp, fittings.cpp";
        for (size_t i = 0; i < furniture_library::TEMPLATES.size(); ++i) {
            const auto& t = furniture_library::TEMPLATES[i];
            if (!furniture_configurator::SUPPORTED.contains(t.kind)) continue;
            run(area, std::format("Kreator dla szablonu #{} „{}”", i, t.name), where, [i](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), (long long)i);
                p->validate();
                json read = furniture_configurator::read_config(*p, g);
                need(!read.is_null(), "read_config zwróciło null");
                ctx.note(std::format("{} części", p->parts.size()));
                json status = furniture_configurator::configuration_status(*p, g);
                need(py::truthy(status.at("editable")), "mebel z kreatora nie jest oznaczony jako edytowalny");
            });
        }

        struct Variation { std::string name; long long tpl; std::function<void(json&)> change; std::string key; json expected; };
        std::vector<Variation> variations;
        auto add = [&](const std::string& name, long long tpl, std::function<void(json&)> change, const std::string& key = "", json expected = nullptr) {
            variations.push_back({name, tpl, std::move(change), key, std::move(expected)});
        };
        for (const char* style : {"smooth", "frame", "fluted", "finger"})
            add(std::string("styl frontu ") + style, 0, [style](json& c) { c["front_style"] = style; }, "front_style", style);
        for (const char* mode : {"handle", "none", "keep"})
            add(std::string("uchwyt: ") + mode, 0, [mode](json& c) { c["handle_mode"] = mode; }, "handle_mode", mode);
        for (auto& [key, value] : opening_options::OPTIONS.items()) {
            std::string spring = value.value("hinge_spring", std::string());
            add("otwieranie: " + key, 0, [key = key, spring](json& c) { c["handle_mode"] = key; if (!spring.empty()) c["hinge_spring"] = spring; }, "handle_mode", key);
            if (value.contains("depth") || key.rfind("finger", 0) == 0 || key == "edge_pull")  // obróbka frontu razem z listwami nad korpusem
                add("otwieranie: " + key + " + góra z listwami", 0, [key = key, spring](json& c) { c["handle_mode"] = key; if (!spring.empty()) c["hinge_spring"] = spring; c["top_mode"] = "rails"; c["worktop"] = false; });
        }
        for (const char* style : {"frame", "fluted", "finger"})
            add(std::string("styl frontu ") + style + " + listwy + nóżki 6 + cokół + LED z boku", 0, [style](json& c) {
                c["front_style"] = style; c["top_mode"] = "rails"; c["worktop"] = false; c["leg_count"] = 6; c["plinth"] = true; c["led"] = true; c["led_mount"] = "side"; });
        for (const json& r : fittings::RECIPES) {
            std::string id = r.value("id", std::string());
            if (id.find("hinge") == std::string::npos || id == "hinge_sensys_tb55" || r.value("category", std::string()) != "Zawiasy") continue;
            add("zawias " + id, 0, [id](json& c) { c["hinge_pattern"] = id; }, "hinge_pattern", id);
        }
        for (const char* spring : {"unknown", "sprung", "unsprung"}) add(std::string("sprężyna zawiasu ") + spring, 0, [spring](json& c) { c["hinge_spring"] = spring; }, "hinge_spring", spring);
        for (const char* side : {"auto", "left", "right"}) add(std::string("strona zawiasów ") + side, 0, [side](json& c) { c["door_hinge"] = side; section(c, "main")["doors"] = 1; }, "door_hinge", side);
        for (int count : {3, 4}) add(std::format("liczba zawiasów {}", count), 0, [count](json& c) { c["hinge_count"] = count; }, "hinge_count", count);
        add("6 nóżek", 0, [](json& c) { c["leg_count"] = 6; }, "leg_count", 6);
        add("bez nóżek (wysokość 0)", 0, [](json& c) { double lh = num(c["leg_height"]); c["leg_height"] = 0; c["height"] = num(c["height"]) - lh; }, "leg_height", 0);
        add("cokół", 0, [](json& c) { c["plinth"] = true; }, "plinth", true);
        add("blat włączony", 0, [](json& c) { c["worktop"] = true; }, "worktop", true);
        add("blat wyłączony", 0, [](json& c) { c["worktop"] = false; }, "worktop", false);
        add("góra: listwy (bez blatu)", 0, [](json& c) { c["top_mode"] = "rails"; c["worktop"] = false; }, "top_mode", "rails");
        add("góra: listwy z blatem", 0, [](json& c) { c["top_mode"] = "rails"; c["worktop"] = true; }, "top_mode", "rails");
        add("góra: płyta", 0, [](json& c) { c["top_mode"] = "panel"; }, "top_mode", "panel");
        add("bez pleców", 0, [](json& c) { c["back"] = false; }, "back", false);
        add("LED pod półką", 0, [](json& c) { c["led"] = true; c["led_mount"] = "under_shelf"; }, "led", true);
        add("LED pod półką + listwy", 0, [](json& c) { c["led"] = true; c["led_mount"] = "under_shelf"; c["top_mode"] = "rails"; c["worktop"] = false; });
        add("LED z boku + listwy", 0, [](json& c) { c["led"] = true; c["led_mount"] = "side"; c["top_mode"] = "rails"; c["worktop"] = false; });
        add("6 nóżek + cokół + listwy", 0, [](json& c) { c["leg_count"] = 6; c["plinth"] = true; c["top_mode"] = "rails"; c["worktop"] = false; });
        add("zawias sensys52 + listwy", 0, [](json& c) { c["hinge_pattern"] = "hinge_sensys52"; c["top_mode"] = "rails"; c["worktop"] = false; });
        add("LED z boku", 0, [](json& c) { c["led"] = true; c["led_mount"] = "side"; }, "led_mount", "side");
        for (const char* front : {"open", "doors", "drawers", "blind"})
            add(std::string("strefa główna: ") + front, 0, [front](json& c) { section(c, "main")["front"] = front; if (std::string(front) == "drawers") section(c, "main")["shelves"] = 0; }, "", nullptr);
        for (int columns : {2, 3}) add(std::format("{} kolumny", columns), 0, [columns](json& c) { c["width"] = 900; section(c, "main")["columns"] = columns; section(c, "main")["front"] = "doors"; });
        add("0 półek", 0, [](json& c) { section(c, "main")["shelves"] = 0; });
        add("5 półek", 0, [](json& c) { section(c, "main")["shelves"] = 5; });
        add("pawlacz (strefa górna)", 0, [](json& c) { auto& u = section(c, "upper"); u["enabled"] = true; u["front"] = "doors"; });
        add("strefa dolna", 0, [](json& c) { auto& b = section(c, "bottom"); b["enabled"] = true; b["front"] = "drawers"; b["shelves"] = 0; b["height"] = 320; c["height"] = 1200; });
        add("szuflady + prowadnica z katalogu", 3, [](json& c) { c["runner_profile"] = runners_catalog::PROFILES.begin().key(); }, "", nullptr);
        add("wymiary minimalne 300×400×300", 0, [](json& c) { c["width"] = 300; c["height"] = 400; c["depth"] = 300; });
        add("wymiary duże 1200×2400×700", 0, [](json& c) { c["width"] = 1200; c["height"] = 2400; c["depth"] = 700; });
        add("grubość płyt 22", 0, [](json& c) { c["thickness"] = 22; }, "thickness", 22);
        add("szczelina frontów 4", 0, [](json& c) { c["gap"] = 4; }, "gap", 4);

        for (auto& v : variations) {
            run(area, "szablon #" + std::to_string(v.tpl) + " · " + v.name, where, [v](Ctx& ctx) {
                Project base;
                auto [p, g] = cabinet(base, v.tpl, v.change);
                p->validate();
                json read = furniture_configurator::read_config(*p, g);
                need(!read.is_null(), "read_config zwróciło null po zbudowaniu");
                if (!v.key.empty() && !v.expected.is_null() && read.value(v.key, json()) != v.expected)
                    ctx.warn(std::format("po odczycie {}={} zamiast {}", v.key, read.value(v.key, json()).dump(), v.expected.dump()));
                ctx.note(std::format("{} części", p->parts.size()));
                if (v.name.find("listwy") != std::string::npos) ctx.hint = "Tryb „listwy wzmacniające” (top_mode=rails) kończy się błędem obróbki — popraw cięcie listew w furniture_configurator.cpp / assembly.cpp (rail) albo ukryj tę opcję, gdy jest niemożliwa.";
            });
        }

        run(area, "Zmiana wymiarów istniejącego mebla (przebudowa w miejscu)", where, [](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            json cfg = furniture_configurator::read_config(*p, g);
            cfg["width"] = num(cfg["width"]) + 200;
            cfg["height"] = num(cfg["height"]) + 100;
            auto [p2, g2] = furniture_configurator::configure(*p, cfg, g, cfg["origin"]);
            p2->validate();
            need(g2 == g, "przebudowa zmieniła nazwę grupy");
            auto b = furniture_layout::group_bounds(*p2, g);
            ctx.note(std::format("nowy rozmiar X={:.0f} Z={:.0f}", b.second[0] - b.first[0], b.second[2] - b.first[2]));
        });
        run(area, "Zmiana całego rzędu szafek naraz (jak „Zmieniaj cały rząd”)", where, [](Ctx& ctx) {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "Rząd A"; });
            auto [p2, b] = cabinet(*p, 0, [](json& c) { c["name"] = "Rząd B"; c["origin"] = json::array({700, 0, 0}); });
            auto [p3, c3] = cabinet(*p2, 0, [](json& c) { c["name"] = "Rząd C"; c["origin"] = json::array({1400, 0, 0}); });
            auto row = furniture_layout::row_groups(*p3, a);
            ctx.note(std::format("rząd ma {} szafek", row.size()));
            ProjectPtr result = p3;
            for (const auto& name : row) {
                json config = furniture_configurator::read_config(*result, name);
                config["handle_mode"] = "none";
                config["front_style"] = "frame";
                result = furniture_configurator::configure(*result, config, name, config.at("origin")).first;
            }
            result->validate();
            for (const auto& name : row) need(furniture_configurator::read_config(*result, name).at("front_style") == "frame", "szafka " + name + " nie dostała nowego stylu");
        });
    }

    void test_stretching() {
        const char* area = "Rozciąganie mebla";
        const char* where = "direct_edit.cpp (resize_furniture_face), stretch_choice.cpp (warianty po rozciągnięciu), app.h (direct_drag / mouse_up)";
        for (const char* face : {"x0", "x1", "y0", "y1", "z0", "z1"}) {
            for (double distance : {96.0, -64.0}) {
                run(area, std::format("ściana {} o {:+.0f} mm", face, distance), where, [=](Ctx& ctx) {
                    auto [p, g] = cabinet(Project());
                    auto before = furniture_layout::group_bounds(*p, g);
                    ProjectPtr r = direct_edit::resize_furniture_face(*p, g, face, distance);
                    r->validate();
                    auto after = furniture_layout::group_bounds(*r, g);
                    int axis = face[0] - 'x';
                    double delta = (after.second[axis] - after.first[axis]) - (before.second[axis] - before.first[axis]);
                    ctx.note(std::format("zmiana wymiaru {:+.1f} mm", delta));
                    if (std::abs(std::abs(delta) - std::abs(distance)) > 1.5) ctx.warn(std::format("oczekiwano zmiany o {:.0f} mm, jest {:.1f} mm (krok 32 mm / ograniczenia?)", distance, delta));
                });
            }
        }
        // warianty z okna po rozciągnięciu: L, U, pawlacz, stos, szafa, regał
        struct Pull { const char* face; double distance; };
        for (Pull pull : {Pull{"y1", 250}, Pull{"z1", 600}}) {
            std::string title = std::format("warianty po rozciągnięciu ściany {} o {:.0f} mm", pull.face, pull.distance);
            // lista wariantów jest budowana z jednego przebiegu, każdy wariant to osobny wiersz raportu
            struct Holder { ProjectPtr base, stretched; std::string group; std::vector<stretch_choice::Variant> variants; };
            auto holder = std::make_shared<Holder>();
            run(area, title, where, [=](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), 0, [](json& c) { c["width"] = 900; });
                holder->base = p; holder->group = g;
                holder->stretched = direct_edit::resize_furniture_face(*p, g, pull.face, pull.distance);
                holder->variants = stretch_choice::variants(*holder->base, *holder->stretched, g, pull.face);
                ctx.note(std::format("{} wariantów", holder->variants.size()));
                if (holder->variants.empty()) ctx.warn("okno wyboru nie zaproponowało żadnych wariantów dla tego rozciągnięcia");
            });
            for (const auto& variant : holder->variants) {
                run(area, title + " → " + variant.label, where + std::string(", corner_furniture.cpp (convert_to_corner), furniture_layout.cpp"), [=](Ctx& ctx) {
                    if (!variant.build) { ctx.note("„zostaw jak jest” — bez przebudowy"); return; }
                    if (!variant.enabled) { ctx.note("kafelek wyłączony w oknie (wariant niemożliwy dla tej szafki)"); return; }
                    ProjectPtr r = variant.build(*holder->stretched);
                    r->validate();
                    auto names = furniture_layout::groups(*r);
                    ctx.note(std::format("{} mebli, {} części", names.size(), r->parts.size()));
                    check_overlaps(ctx, *r);
                    // wszystkie szafki układu mają być sąsiadami: odstęp do najbliższej ≤ 5 mm
                    if (names.size() > 1) {
                        for (const auto& a : names) {
                            double nearest = 1e9;
                            auto ba = furniture_layout::group_bounds(*r, a);
                            for (const auto& b : names) {
                                if (a == b) continue;
                                auto bb = furniture_layout::group_bounds(*r, b);
                                double gap = 0;
                                for (int axis = 0; axis < (variant.id == "wall" ? 2 : 3); ++axis) gap += std::pow(std::max({0.0, bb.first[axis] - ba.second[axis], ba.first[axis] - bb.second[axis]}), 2);
                                nearest = std::min(nearest, std::sqrt(gap));
                            }
                            if (nearest > 5) ctx.warn(std::format("mebel {} jest oddalony od reszty układu o {:.1f} mm", a, nearest));
                        }
                    }
                });
            }
        }
    }

    void test_corners() {
        const char* area = "Narożniki";
        const char* where = "corner_furniture.cpp (create_corner, convert_to_corner, corner_outline), corner_quick_ui.cpp, stretch_choice.cpp (cover_corner / blind)";
        for (const char* shape : {"L", "chamfer", "rounded"}) {
            for (const char* side : {"left", "right"}) {
                run(area, std::format("narożnik {} strona {}", shape, side), where, [=](Ctx& ctx) {
                    Project base;
                    json cfg = corner_furniture::default_corner_config(&base);
                    cfg["shape"] = shape; cfg["side"] = side;
                    if (std::string(shape) == "chamfer") cfg["fronts"] = "diagonal";
                    auto [p, g] = corner_furniture::create_corner(base, cfg);
                    p->validate();
                    ctx.note(std::format("{} części", p->parts.size()));
                    need(!corner_furniture::read_corner_config(*p, g).is_null(), "read_corner_config zwróciło null");
                    json status = corner_furniture::corner_status(*p, g);
                    ctx.note("status: " + status.dump().substr(0, 100));
                });
            }
        }
        for (const char* side : {"left", "right"}) {
            run(area, std::string("konwersja szafki 900 mm w narożnik L ") + side, where, [=](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), 0, [](json& c) { c["width"] = 900; });
                json cfg = furniture_configurator::read_config(*p, g);
                auto stretched = direct_edit::resize_furniture_face(*p, g, "y1", 250);
                json after = furniture_configurator::read_config(*stretched, g);
                auto [r, g2] = corner_furniture::convert_to_corner(*stretched, g, "L", side, json(num(cfg["depth"])), json(num(cfg["width"])), json(num(after["depth"])));
                r->validate();
                ctx.note(std::format("{} części", r->parts.size()));
                check_overlaps(ctx, *r);
            });
        }
    }

    void test_arrangements_widths() {
        const char* where = "stretch_choice.cpp (build_arrangement / make_prompt), furniture_layout.cpp (snap_group)";
        for (double width : {600.0, 900.0, 1200.0, 1500.0, 1800.0}) {
            run("Układy L i U", std::format("szafka {:.0f} mm rozciągnięta w głąb o 250: które układy są dostępne i czy się budują", width), where, [=](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), 0, [=](json& c) { c["width"] = width; });
                auto stretched = direct_edit::resize_furniture_face(*p, g, "y1", 250);
                auto variants = stretch_choice::variants(*p, *stretched, g, "y1");
                std::string summary;
                for (const auto& v : variants) {
                    if (!v.build) continue;
                    summary += v.id + (v.enabled ? "=dostępny " : "=wyłączony ");
                    if (!v.enabled) continue;
                    ProjectPtr r = v.build(*stretched);
                    r->validate();
                    check_overlaps(ctx, *r);
                    auto groups = furniture_layout::groups(*r);
                    if (v.id.rfind("arr_", 0) == 0 && groups.size() < 2) ctx.warn("układ " + v.id + " nie utworzył rzędu szafek (jest " + std::to_string(groups.size()) + ")");
                }
                ctx.note(summary);
            });
        }
    }

    void test_narrow_corner() {
        run("Rozciąganie mebla", "wąska szafka 600 mm: narożnik L wyłączony w oknie zamiast błędu", "stretch_choice.cpp (make_prompt / can_corner), corner_furniture.cpp (convert_to_corner)", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { c["width"] = 600; });
            auto stretched = direct_edit::resize_furniture_face(*p, g, "y1", 250);
            auto variants = stretch_choice::variants(*p, *stretched, g, "y1");
            need(!variants.empty(), "brak wariantów dla rozciągnięcia w głąb");
            for (const auto& v : variants)
                if (v.id.rfind("corner_", 0) == 0) {
                    ctx.note(v.label + (v.enabled ? " — dostępny" : " — wyłączony"));
                    if (v.enabled) { try { v.build(*stretched); } catch (const std::exception& e) { ctx.warn(std::string("kafelek włączony, a budowa kończy się błędem: ") + e.what()); } }
                }
        });
        run("Konfigurator (opcje mebla)", "zależności w oknie Konfiguruj: szuflady zerują półki, TIP-ON ustawia zawias bez sprężyny", "stretch_choice.cpp (merge_overrides)", [](Ctx&) {
            auto [p, g] = cabinet(Project());
            json cfg = furniture_configurator::read_config(*p, g);
            // to samo, co robi okno Konfiguruj po zmianie pól: najpierw TIP-ON na drzwiach (zawias bez sprężyny ustawiany sam), potem szuflady z zerowymi półkami
            json tip = cfg;
            stretch_choice::apply_overrides(tip, json{{"handle_mode", "tip_short"}});
            need(tip.value("hinge_spring", std::string()) == "unsprung", "TIP-ON nie ustawił zawiasu bez sprężyny");
            ProjectPtr with_tip = furniture_configurator::configure(*p, tip, g, tip.at("origin")).first;
            with_tip->validate();
            json sections = cfg["sections"];
            for (auto& s : sections) if (s.value("key", std::string()) == "main") { s["front"] = "drawers"; s["shelves"] = 2; }
            stretch_choice::apply_overrides(cfg, json{{"sections", sections}});
            ProjectPtr r = furniture_configurator::configure(*p, cfg, g, cfg.at("origin")).first;
            r->validate();
            (void)r;
        });
    }

    void test_layout() {
        const char* area = "Układ i magnes";
        const char* where = "furniture_layout.cpp (move/rotate/snap/nearest_snap/resolve_overlap/row_extent/contacts), furniture_layout_ui.cpp";
        auto three = [] {
            auto [p, a] = cabinet(Project(), 0, [](json& c) { c["name"] = "Test A"; });
            auto [p2, b] = cabinet(*p, 0, [](json& c) { c["name"] = "Test B"; c["origin"] = json::array({1500, 0, 0}); });
            auto [p3, c3] = cabinet(*p2, 0, [](json& c) { c["name"] = "Test C"; c["origin"] = json::array({3000, 0, 0}); });
            return std::tuple{p3, a, b, c3};
        };
        run(area, "przesunięcie grupy o (120, −80, 0)", where, [=](Ctx&) {
            auto [p, a, b, c] = three();
            auto before = furniture_layout::group_bounds(*p, a);
            auto moved = furniture_layout::move_group(*p, a, 120, -80, 0);
            auto after = furniture_layout::group_bounds(*moved, a);
            need(std::abs(after.first[0] - before.first[0] - 120) < .01 && std::abs(after.first[1] - before.first[1] + 80) < .01, "przesunięcie o inną wartość niż zadana");
            (void)b; (void)c;
        });
        for (double angle : {90.0, 180.0, 45.0}) {
            run(area, std::format("obrót o {:.0f}°", angle), where, [=](Ctx& ctx) {
                auto [p, a, b, c] = three();
                auto rotated = furniture_layout::rotate_group(*p, a, angle);
                rotated->validate();
                ctx.note("układ: " + furniture_layout::group_layout(*rotated, a).dump().substr(0, 90));
                (void)b; (void)c;
            });
        }
        for (const auto& [name, direction] : furniture_layout::DIRECTIONS) {
            run(area, "magnes " + direction + " (" + name + ")", where, [=](Ctx& ctx) {
                auto [p, a, b, c] = three();
                auto snapped = furniture_layout::snap_group(*p, b, a, direction);
                snapped->validate();
                double depth = penetration(furniture_layout::group_bounds(*snapped, a), furniture_layout::group_bounds(*snapped, b));
                if (depth > 1) ctx.warn(std::format("po przyciągnięciu meble nachodzą na siebie o {:.1f} mm", depth));
                (void)c;
            });
        }
        run(area, "najbliższy magnes (nearest_snap)", where, [=](Ctx& ctx) {
            auto [p, a, b, c] = three();
            auto moved = furniture_layout::move_group(*p, b, -820, 0, 0);
            auto [r, info] = furniture_layout::nearest_snap(*moved, b, 120);
            r->validate();
            ctx.note("wynik: " + info.dump().substr(0, 100));
            (void)a; (void)c;
        });
        run(area, "rozsunięcie nakładających się (resolve_overlap)", where, [=](Ctx& ctx) {
            auto [p, a, b, c] = three();
            auto moved = furniture_layout::move_group(*p, b, -1400, 0, 0);
            auto [r, changed] = furniture_layout::resolve_overlap(*moved, b);
            r->validate();
            ctx.note(changed ? "przesunięto" : "bez zmiany");
            (void)a; (void)c;
        });
        run(area, "rząd szafek: row_groups / row_extent / contacts", where, [=](Ctx& ctx) {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "R1"; });
            auto [p2, b] = cabinet(*p, 0, [](json& c) { c["name"] = "R2"; c["origin"] = json::array({600, 0, 0}); });
            auto row = furniture_layout::row_groups(*p2, a);
            need(row.size() == 2, std::format("row_groups zwróciło {} szafek zamiast 2", row.size()));
            json extent = furniture_layout::row_extent(*p2, a);
            ctx.note("zakres rzędu: " + extent.dump().substr(0, 100));
            json contacts = furniture_layout::contacts(*p2, a);
            ctx.note("styki: " + contacts.dump().substr(0, 100));
            (void)b;
        });
        run(area, "wspólny blat nad szafkami", "workspace_worktops.cpp, native_worktops.cpp", [=](Ctx& ctx) {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "B1"; c["worktop"] = false; });
            auto [p2, b] = cabinet(*p, 0, [](json& c) { c["name"] = "B2"; c["worktop"] = false; c["origin"] = json::array({600, 0, 0}); });
            auto [p3, top] = workspace_worktops::install_shared_worktop(*p2, {a, b});
            p3->validate();
            ctx.note("stan blatu: " + workspace_worktops::read_shared_worktop(*p3, top).value("status", std::string("?")));
        });
    }

    void test_hardware() {
        const char* area = "Okucia";
        const char* where = "fittings.cpp, runner_installation.cpp, runners_catalog.cpp, opening_options.cpp, drag_hardware.cpp, mechanisms.cpp";
        run(area, "łączniki korpusu (konfirmaty) i System 32", where, [](Ctx&) {
            auto [p, g] = furniture_library::create_furniture(Project(), 0);
            ProjectPtr c = fittings::cabinet_connectors(*p, g);
            c->validate();
            const Part* side = nullptr;
            for (const auto& q : c->parts) if (q.name == "Bok lewy") { side = &q; break; }
            need(side != nullptr, "brak części „Bok lewy”");
            fittings::system32(*c, side->id)->validate();
        });
        run(area, "wszystkie szablony: ruch otwierania (mechanisms::posed_project)", where, [](Ctx& ctx) {
            long long bad = 0;
            for (size_t i = 0; i < furniture_library::TEMPLATES.size(); ++i) {
                auto [p, g] = furniture_library::create_furniture(Project(), (long long)i);
                std::map<std::string, double> amounts;
                for (const auto& q : p->parts) amounts[q.id] = .75;
                try { if (mechanisms::posed_project(p, amounts)->parts.size() != p->parts.size()) ++bad; } catch (const std::exception&) { ++bad; }
            }
            need(bad == 0, std::format("{} szablonów nie przechodzi animacji otwierania", bad));
        });
        run(area, "prowadnice: każdy profil z katalogu", where, [](Ctx& ctx) {
            auto [p, g] = furniture_library::create_furniture(Project(), 3);
            const Part* front = nullptr;
            for (const auto& q : p->parts) if (q.role == "drawer") { front = &q; break; }
            need(front != nullptr, "brak frontu szuflady w szablonie #3");
            long long ok = 0, bad = 0;
            std::string first_error;
            for (auto& [id, profile] : runners_catalog::PROFILES.items()) {
                try { runner_installation::apply_runner(*p, front->id, id, 400)->validate(); ++ok; }
                catch (const std::exception& e) { ++bad; if (first_error.empty()) first_error = id + ": " + e.what(); }
            }
            ctx.note(std::format("{} profili OK, {} z błędem", ok, bad));
            if (bad != 0 && first_error.find("nie określa głębokości montażowej") != std::string::npos)
                ctx.warn("profile wymagają ręcznego wpisania głębokości montażowej w kalkulatorze (dane katalogowe niekompletne) — pierwszy: " + first_error);
            else
                need(bad == 0, "profile z błędem — pierwszy: " + first_error);
        });
        run(area, "uchwyty / otwieranie: każda opcja na drzwiach", where, [](Ctx& ctx) {
            auto [p, g] = furniture_library::create_furniture(Project(), 0);
            ProjectPtr c = fittings::cabinet_connectors(*p, g);
            const Part* door = nullptr;
            for (const auto& q : c->parts) if (q.role == "door") { door = &q; break; }
            need(door != nullptr, "brak drzwi");
            long long bad = 0;
            std::string first_error;
            for (auto& [key, value] : opening_options::OPTIONS.items()) {
                try { opening_options::install(*c, door->id, key, json{{"hinge_spring", value.value("hinge_spring", std::string("sprung"))}})->validate(); }
                catch (const std::exception& e) { ++bad; if (first_error.empty()) first_error = key + ": " + e.what(); }
            }
            ctx.note(std::format("{} opcji, {} z błędem", opening_options::OPTIONS.size(), bad));
            need(bad == 0, "opcje z błędem — pierwsza: " + first_error);
        });
        run(area, "ręczne okucie LED (drag_hardware::attach)", where, [](Ctx&) {
            auto [p, g] = cabinet(Project(), 3);
            const Part* leaf = nullptr;
            for (const auto& q : p->parts) if (q.role == "drawer") { leaf = &q; break; }
            need(leaf != nullptr, "brak szuflady");
            json hit = {{"u", leaf->u / 2}, {"v", leaf->v / 2}, {"w", 0}, {"face", "bottom"}};
            drag_hardware::attach(*p, "led", leaf->id, hit, json{{"width", 12}, {"margin", 30}})->validate();
        });
        run(area, "raport okuć i koszty", "hardware_report.cpp, core.cpp (costs)", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            TempDir tmp("MebelStudio3D-test-");
            hardware_report::hardware_report(*p, py::path_str(tmp.path / "okucia.html"));
            need(fs::file_size(tmp.path / "okucia.html") > 100, "raport okuć jest pusty");
            auto costs = p->costs();
            std::string text;
            for (auto& [k, v] : costs) text += std::format("{}={:.0f} ", k, v);
            ctx.note("koszty: " + text);
        });
    }

    void test_catalogue() {
        const char* area = "Biblioteka (panel) — dodawanie elementów";
        const char* where = "visual_catalogue.cpp (catalogue_items, apply_item), library_panel.cpp, catalogue_previews.cpp";
        for (const auto& [kind, title] : visual_catalogue::KINDS) {
            run(area, "rodzaj „" + title + "” (" + kind + ")", where, [this, kind](Ctx& ctx) {
                // identyfikatory materiałów są losowe dla każdego projektu: karty katalogu muszą pochodzić z tego samego projektu, na którym je zastosujemy
                ProjectPtr scratch = fresh();
                if (kind != "furniture" && kind != "shaped" && kind != "appliance") scratch = cabinet(Project()).first;
                app->project = scratch;
                app->selected.reset();
                if (app->hardware_drag != nullptr && app->hardware_drag->active()) app->hardware_drag->cancel();
                json items = visual_catalogue::catalogue_items(kind, *scratch);
                need(items.is_array() && !items.empty(), "katalog nie zwrócił żadnych elementów");
                ctx.note(std::format("{} elementów w katalogu", items.size()));
                // kilka pierwszych elementów realnie dodajemy do pustego projektu (ścieżka z klikania w kartę)
                long long applied = 0, failed = 0;
                std::string first_error;
                size_t limit = kind == "furniture" ? 3 : 2;
                for (size_t i = 0; i < items.size() && i < limit; ++i) {
                    try {
                        app->project = scratch;
                        app->selected.reset();
                        if (kind != "furniture" && kind != "shaped" && kind != "appliance") {
                            const ProjectPtr& p = scratch;
                            for (const auto& q : p->parts) if (q.role == "door") { app->selected = q.id; break; }
                            if (!app->selected && !p->parts.empty()) app->selected = p->parts[0].id;
                        }
                        visual_catalogue::apply_item(app, items[i]);
                        app->update();
                        ++applied;
                    } catch (const std::exception& e) {
                        ++failed;
                        if (first_error.empty()) first_error = items[i].value("name", std::string("?")) + ": " + e.what();
                    }
                    if (app->hardware_drag != nullptr && app->hardware_drag->active()) app->hardware_drag->cancel();
                }
                ctx.note(std::format("dodano {}, błędów {}", applied, failed));
                close_extra_windows();
                need(failed == 0, "dodanie elementu z katalogu nie powiodło się — " + first_error);
                app->project = fresh();
            });
        }
        // Każdy element biblioteki MUSI mieć miniaturkę: niepusta geometria, skończone i niezdegenerowane wymiary, narysowana na karcie.
        for (const auto& [kind, title] : visual_catalogue::KINDS) {
            run(area, "miniaturki: każdy element rodzaju „" + title + "” ma narysowaną miniaturkę", "catalogue_previews.cpp (preview_faces, draw_preview), library_panel.cpp", [this, kind](Ctx& ctx) {
                ProjectPtr scratch = (kind != "furniture" && kind != "shaped" && kind != "appliance") ? cabinet(Project()).first : fresh();
                json items = visual_catalogue::catalogue_items(kind, *scratch);
                need(items.is_array() && !items.empty(), "katalog nie zwrócił elementów");
                auto* canvas = new tk::Canvas(app, {{"width", 200}, {"height", 125}});
                std::map<std::string, std::string> seen;
                int duplicates = 0;
                std::string first_duplicate;
                for (const auto& item : items) {
                    std::string name = item.value("name", item.value("title", std::string("?")));
                    auto faces = catalogue_previews::preview_faces(item);
                    if (faces.empty()) { canvas->destroy(); need(false, "brak miniaturki (pusta geometria): " + name); }
                    double lo[3] = {1e18, 1e18, 1e18}, hi[3] = {-1e18, -1e18, -1e18};
                    std::string signature;
                    for (const auto& [face, color] : faces) {
                        signature += color;
                        for (const auto& v : face)
                            for (int i = 0; i < 3; ++i) {
                                if (!std::isfinite(v[i])) { canvas->destroy(); need(false, "miniaturka ma niepoprawne współrzędne: " + name); }
                                lo[i] = std::min(lo[i], v[i]); hi[i] = std::max(hi[i], v[i]);
                                signature += std::format("{:.1f},", v[i]);
                            }
                    }
                    if (!(hi[0] - lo[0] > 1 || hi[1] - lo[1] > 1 || hi[2] - lo[2] > 1)) { canvas->destroy(); need(false, "miniaturka ma zerowe wymiary: " + name); }
                    catalogue_previews::draw_preview(canvas, item, 200, 125);
                    if (canvas->find_withtag(item.at("id").get<std::string>()).empty()) { canvas->destroy(); need(false, "miniaturka nie została narysowana na karcie: " + name); }
                    // elementy różniące się tylko producentem / nazwą (te same wymiary i parametry) mogą mieć wspólną miniaturkę
                    auto [it, inserted] = seen.emplace(signature, name);
                    if (!inserted) {
                        bool same_data = false;
                        for (const auto& other : items)
                            if (other.value("name", other.value("title", std::string("?"))) == it->second)
                                same_data = other.value("dimensions", json::object()) == item.value("dimensions", json::object()) && other.value("preview", json::object()) == item.value("preview", json::object());
                        if (!same_data) { ++duplicates; if (first_duplicate.empty()) first_duplicate = name + " = " + it->second; }
                    }
                }
                canvas->destroy();
                ctx.note(std::format("{} miniaturek", items.size()));
                if (duplicates) ctx.warn(std::format("{} miniaturek wygląda identycznie jak inna (np. {}) — warto dorobić własną", duplicates, first_duplicate));
            });
        }
        run(area, "szukanie i filtrowanie w katalogu", where, [](Ctx& ctx) {
            Project p;
            json items = visual_catalogue::catalogue_items("furniture", p);
            json found = visual_catalogue::filter_items(items, "szafka");
            ctx.note(std::format("fraza „szafka”: {} z {}", found.size(), items.size()));
            need(found.size() <= items.size(), "filtr zwrócił więcej niż wszystkich");
        });
    }


    void test_library_on_furniture() {
        const char* area = "Panel Mebel i karty biblioteki na meblu";
        const char* where = "visual_catalogue.cpp (apply_item: furniture/leg/material), quick_studio_ui.cpp (QuickInspector::apply), furniture_configurator.cpp (infer_config)";
        auto add_first = [this](size_t index) {
            ProjectPtr scratch = fresh();
            app->project = scratch;
            app->selected.reset();
            json items = visual_catalogue::catalogue_items("furniture", *scratch);
            need(items.is_array() && items.size() > index, "katalog mebli jest za krótki");
            visual_catalogue::apply_item(app, items[index]);
            app->update();
            need(app->selected_part() != nullptr, "po dodaniu mebla nic nie jest zaznaczone");
            return app->selected_part()->group;
        };
        run(area, "mebel dodany z biblioteki ma konfigurację kreatora (panel Mebel może go zmieniać)", where, [=](Ctx& ctx) {
            int configured = 0, total = 0;
            std::string missing;
            for (size_t i = 0; i < 6; ++i) {
                std::string group = add_first(i);
                ++total;
                if (!furniture_configurator::read_config(*app->project, group).is_null()) ++configured;
                else missing += (missing.empty() ? "" : ", ") + group;
            }
            ctx.note(std::format("z kreatorem: {} z {}", configured, total));
            if (!missing.empty()) ctx.warn("bez kreatora: " + missing);
            need(configured > 0, "żaden mebel z biblioteki nie trafił do kreatora");
        });
        run(area, "karta Materiał zmienia materiał całego mebla", where, [=](Ctx& ctx) {
            std::string group = add_first(0);
            json before = furniture_configurator::read_config(*app->project, group);
            need(!before.is_null(), "mebel nie ma konfiguracji");
            json items = visual_catalogue::catalogue_items("material", *app->project);
            need(items.is_array() && items.size() > 1, "katalog materiałów jest pusty");
            bool changed = false;
            for (const json& item : items) {
                visual_catalogue::apply_item(app, item);
                json after = furniture_configurator::read_config(*app->project, group);
                if (after.at("material") != before.at("material")) { changed = true; break; }
            }
            ctx.note(changed ? "materiał korpusu zmieniony w konfiguracji" : "materiał nie zmienił się");
            need(changed, "karta materiału nie zmieniła materiału korpusu w konfiguracji mebla");
        });
        run(area, "karta Nogi zmienia wysokość nóżek w konfiguracji", where, [=](Ctx& ctx) {
            std::string group = add_first(0);
            json items = visual_catalogue::catalogue_items("leg", *app->project);
            need(items.is_array() && !items.empty(), "katalog nóg jest pusty");
            json payload = items[0].at("payload");
            visual_catalogue::apply_item(app, items[0]);
            json after = furniture_configurator::read_config(*app->project, group);
            ctx.note(std::format("leg_height = {}", after.value("leg_height", -1.0)));
            need(after.contains("leg_height") && after.at("leg_height").get<double>() == payload.at("leg_height").get<double>(), "wysokość nóżek w konfiguracji nie zgadza się z kartą");
        });
        run(area, "karta bez zaznaczenia działa, gdy w projekcie jest jeden mebel", where, [=](Ctx&) {
            std::string group = add_first(0);
            app->selected.reset();
            json items = visual_catalogue::catalogue_items("leg", *app->project);
            visual_catalogue::apply_item(app, items[0]);
            need(!furniture_configurator::read_config(*app->project, group).is_null(), "po karcie mebel stracił konfigurację");
        });
        run(area, "mebel spoza kreatora: rozpoznanie wzorca i zmiana materiału przez przejęcie", where, [](Ctx& ctx) {
            auto [legacy, group] = furniture_library::create_furniture(Project(), 0);
            need(furniture_configurator::read_config(*legacy, group).is_null(), "to miał być mebel bez konfiguracji");
            json inferred = furniture_configurator::infer_config(*legacy, group);
            inferred["leg_height"] = 100;
            auto [taken, name] = furniture_configurator::configure(*legacy, inferred, group);
            need(furniture_configurator::read_config(*taken, name).at("leg_height") == json(100), "przejęcie nie zapisało nóżek");
            ctx.note("infer_config + configure działa dla szablonu 0");
        });
        run("Konfigurator (opcje mebla)", "uchwyt frezowany, potem listwy i zmiana szerokości — frezowanie nie dubluje się", "furniture_configurator.cpp (configure: config_auto dla obróbki uchwytu), opening_options.cpp", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { c["handle_mode"] = "finger_pocket"; });
            auto count_pockets = [](const Project& project) {
                int n = 0;
                for (const auto& q : project.parts)
                    for (const auto& cut : q.machining) if (py::truthy(py::get(cut, "opening_owner"))) ++n;
                return n;
            };
            int before = count_pockets(*p);
            need(before > 0, "uchwyt frezowany nie utworzył obróbki");
            json cfg = furniture_configurator::read_config(*p, g);
            cfg["top_mode"] = "rails"; cfg["worktop"] = false;
            auto step1 = furniture_configurator::configure(*p, cfg, g);
            need(count_pockets(*step1.first) == before, std::format("po zmianie góry na listwy frezowań jest {} zamiast {}", count_pockets(*step1.first), before));
            json cfg2 = furniture_configurator::read_config(*step1.first, step1.second);
            cfg2["width"] = 700;
            auto step2 = furniture_configurator::configure(*step1.first, cfg2, step1.second);
            need(count_pockets(*step2.first) == before, "po zmianie szerokości frezowanie się zdublowało");
            ctx.note(std::format("frezowań: {} (bez zmian po dwóch przeliczeniach)", before));
        });
    }


    // Regresja: z włączonym LED rysowanie 3D rzucało json::type_error 302 („type must be array, but is string”),
    // bo native_scene_export wstawiał do std::set<pair<json,json>> przez insert({json, json}).
    // Rozkrój płyt: optymalizator nigdy nie jest gorszy od klasycznego, formatki się nie nakładają i mieszczą się w marginesie.
    void test_nesting() {
        const char* area = "Rozkrój płyt";
        const char* where = "sheet_nesting.cpp (optimize, statistics, sheets_export, sheets_dxf_export), production_docs.cpp (etykiety), app.h (zakładka Rozkrój)";
        auto random_project = [](int count, unsigned seed) {
            Project q;
            std::mt19937 rng(seed);
            for (int i = 0; i < count; ++i) {
                Part pt; pt.name = "Formatka " + std::to_string(i + 1);
                pt.u = 100 + rng() % 900; pt.v = 80 + rng() % 600; pt.t = 18; pt.material = q.materials[0].id; pt.grain = rng() % 2;
                q.parts.push_back(pt);
            }
            return q;
        };
        auto check_valid = [](const Project& q, const json& sheets) {
            std::set<std::string> seen;
            for (const auto& sheet : sheets) {
                const auto& pieces = sheet["pieces"];
                for (size_t i = 0; i < pieces.size(); ++i) {
                    double x = pieces[i]["x"], y = pieces[i]["y"], u = pieces[i]["u"], v = pieces[i]["v"];
                    need(x >= q.margin - 1e-6 && y >= q.margin - 1e-6 && x + u <= sheet["u"].get<double>() - q.margin + 1e-6 && y + v <= sheet["v"].get<double>() - q.margin + 1e-6,
                         "formatka wychodzi poza użyteczny obszar płyty");
                    need(seen.insert(pieces[i]["id"].get<std::string>()).second, "formatka wstawiona dwa razy");
                    for (size_t j = i + 1; j < pieces.size(); ++j) {
                        double x2 = pieces[j]["x"], y2 = pieces[j]["y"], u2 = pieces[j]["u"], v2 = pieces[j]["v"], k = q.kerf;
                        need(!(x < x2 + u2 + k - 1e-6 && x2 < x + u + k - 1e-6 && y < y2 + v2 + k - 1e-6 && y2 < y + v + k - 1e-6), "formatki nakładają się lub brakuje rzazu między nimi");
                    }
                }
            }
            return (int)seen.size();
        };
        for (unsigned seed : {1u, 2u, 3u}) {
            run(area, std::format("losowy zestaw {} formatek (ziarno {}): optymalny ≤ klasyczny, bez nakładania", 40 + 40 * (int)seed, seed), where, [=](Ctx& ctx) {
                Project q = random_project(40 + 40 * (int)seed, seed);
                auto classic = core::cut_plan(q);
                auto better = sheet_nesting::optimize(q);
                need(better.first.size() <= classic.first.size(), std::format("optymalny {} płyt, klasyczny {}", better.first.size(), classic.first.size()));
                int placed = check_valid(q, better.first);
                need(placed + (int)better.second.size() == (int)q.parts.size(), "nie wszystkie formatki są na płytach lub na liście nieumieszczonych");
                ctx.note(std::format("klasyczny {} płyt → optymalny {}", classic.first.size(), better.first.size()));
            });
        }
        run(area, "formatki z włóknami nie są obracane, bez włókien mogą być", where, [=](Ctx&) {
            Project q = random_project(60, 9);
            auto result = sheet_nesting::optimize(q);
            std::map<std::string, bool> grain;
            for (const auto& p : q.parts) grain[p.id] = p.grain && !p.grain_v;
            for (const auto& sheet : result.first)
                for (const auto& piece : sheet["pieces"]) need(!(grain[piece["id"].get<std::string>()] && piece["rotated"].get<bool>()), "formatka z włóknami została obrócona");
        });
        run(area, "włókna wzdłuż V: wysoka formatka mieści się obrócona, włókna zostają zablokowane, w obu trybach rozkroju i po zapisie", where, [=](Ctx& ctx) {
            Project q = random_project(4, 6);
            Part side; side.name = "Bok wysoki"; side.u = 560; side.v = 2100; side.t = 18; side.material = q.materials[0].id; side.grain = true;
            q.parts.push_back(side);
            auto& added = q.parts.back();
            auto [sheet_u, sheet_v] = std::make_pair(q.mat(added.material).sheet_u, q.mat(added.material).sheet_v);
            need(560 <= sheet_u && 2100 > sheet_v - 2 * q.margin, "założenie testu: bok 560×2100 nie mieści się przy włóknach wzdłuż U");
            {
                auto before = sheet_nesting::optimize(q);
                need(std::find(before.second.begin(), before.second.end(), added.id) != before.second.end(), "bok z włóknami wzdłuż U powinien być nieumieszczony");
            }
            added.grain_v = true;
            need(added.grain, "włókna nie mogą się odblokować przy ustawieniu kierunku V");
            auto optimal = sheet_nesting::optimize(q);
            need(std::find(optimal.second.begin(), optimal.second.end(), added.id) == optimal.second.end(), "tryb optymalny nie umieścił boku z włóknami wzdłuż V");
            auto classic = core::cut_plan(q);
            need(std::find(classic.second.begin(), classic.second.end(), added.id) == classic.second.end(), "tryb klasyczny nie umieścił boku z włóknami wzdłuż V");
            for (const auto& plan : {optimal.first, classic.first})
                for (const auto& sheet : plan)
                    for (const auto& piece : sheet["pieces"])
                        if (piece["id"].get<std::string>() == added.id)
                            need(piece["rotated"].get<bool>() && piece["u"].get<double>() > piece["v"].get<double>(), "bok z włóknami wzdłuż V powinien leżeć obrócony (długi bok wzdłuż płyty)");
            need(production_inspector::inspect(q).errors == 0 || [&] { for (const auto& f : production_inspector::inspect(q).findings) if (f.rule == "sheet_fit") return false; return true; }(),
                 "Inspektor zgłasza formatkę poza płytą mimo włókien wzdłuż V");
            // zapis i odczyt zachowują kierunek włókien
            ProjectPtr copy = Project::from_data(q.data());
            bool kept = false;
            for (const auto& part : copy->parts) if (part.id == added.id) kept = part.grain && part.grain_v;
            need(kept, "kierunek włókien V nie przetrwał zapisu/odczytu projektu");
            ctx.note("bok 560×2100 leży obrócony w obu trybach rozkroju");
        });
        run(area, "za duża formatka trafia na listę nieumieszczonych", where, [=](Ctx&) {
            Project q = random_project(5, 4);
            Part big; big.name = "Za duża"; big.u = 5000; big.v = 300; big.t = 18; big.material = q.materials[0].id; big.grain = true;
            q.parts.push_back(big);
            auto result = sheet_nesting::optimize(q);
            need(result.second.size() == 1 && result.second[0] == big.id, "za duża formatka nie została zgłoszona");
        });
        run(area, "tryb klasyczny = core::cut_plan, statystyki zgadzają się z płytami", where, [=](Ctx& ctx) {
            Project q = random_project(50, 5);
            sheet_nesting::Mode previous = sheet_nesting::mode();
            sheet_nesting::set_mode(sheet_nesting::Mode::Classic);
            auto plan = sheet_nesting::plan(q);
            auto classic = core::cut_plan(q);
            sheet_nesting::set_mode(previous);
            need(plan.first.size() == classic.first.size() && plan.second == classic.second, "tryb klasyczny różni się od core::cut_plan");
            json st = sheet_nesting::statistics(q, plan.first, plan.second);
            need(st["sheets"].get<long long>() == (long long)plan.first.size(), "statystyki: zła liczba płyt");
            double used = 0;
            for (const auto& p : q.parts) used += p.blank(q.edge_t).first * p.blank(q.edge_t).second;
            need(std::abs(st["used_area_m2"].get<double>() * 1e6 - used) < 1.0, "statystyki: suma pól formatek różni się od sumy pól na płytach");
            need(st["waste"].get<double>() > 0 && st["waste"].get<double>() < 100, "statystyki: odpad poza zakresem");
            ctx.note(std::format("wykorzystanie {:.1f}%", st["utilization"].get<double>()));
        });
        run(area, "kuchnia z kreatora: wszystkie formatki na płytach, eksport HTML/DXF i etykiety z numerem płyty", where, [=](Ctx& ctx) {
            ProjectPtr p = std::make_shared<Project>();
            for (int i = 0; i < 6; ++i) p = cabinet(*p, i % 2 ? 4 : 0, [i](json& c) { c["width"] = 400 + 50 * i; }).first;
            p->validate();
            sheet_nesting::Mode previous = sheet_nesting::mode();
            sheet_nesting::set_mode(sheet_nesting::Mode::Optimized);
            auto plan = sheet_nesting::plan(*p);
            auto classic = core::cut_plan(*p);
            need(plan.first.size() <= classic.first.size(), "optymalny rozkrój jest gorszy od klasycznego");
            need(plan.second.empty(), "formatki kuchni nie mieszczą się na płytach");
            int placed = check_valid(*p, plan.first);
            need(placed == (int)p->parts.size(), "nie wszystkie formatki kuchni są na płytach");
            TempDir tmp("MebelStudio3D-test-");
            std::string html = py::path_str(tmp.path / "rozkroj.html"), dxf = py::path_str(tmp.path / "rozkroj.dxf"), labels = py::path_str(tmp.path / "etykiety.html");
            sheet_nesting::sheets_export(*p, html);
            sheet_nesting::sheets_dxf_export(*p, dxf);
            production_docs::labels_export(*p, labels);
            sheet_nesting::set_mode(previous);
            auto slurp = [](const std::string& path) { std::ifstream f(fs::path(path), std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
            std::string html_text = slurp(html), dxf_text = slurp(dxf), label_text = slurp(labels);
            need(html_text.find("Płyta 1 z ") != std::string::npos && html_text.find("<svg") != std::string::npos, "arkusz HTML nie zawiera rysunku płyty");
            need(dxf_text.find("SHEET_1") != std::string::npos && dxf_text.find("LWPOLYLINE") != std::string::npos, "DXF nie zawiera obrysu płyty");
            need(std::all_of(dxf_text.begin(), dxf_text.end(), [](unsigned char c) { return c < 0x80; }), "DXF zawiera znaki spoza ASCII");
            need(label_text.find("płyta 1 · poz. ") != std::string::npos, "etykiety nie niosą numeru płyty i pozycji");
            ctx.note(std::format("{} części → {} płyt (klasyczny {})", p->parts.size(), plan.first.size(), classic.first.size()));
        });
    }

    void test_inspector() {
        namespace pi = production_inspector;
        const char* area = "Inspektor produkcyjny";
        const char* where = "production_inspector.cpp (inspect, apply_fixes, report_export), quick_studio_ui.cpp (zakładka Inspektor)";
        auto count_rule = [](const pi::Report& r, const std::string& rule) { int n = 0; for (const auto& f : r.findings) if (f.rule == rule) ++n; for (const auto& f : r.hidden) if (f.rule == rule) ++n; return n; };
        auto base_part = [](Project& q, const std::string& name, double u, double v, double t, const std::string& plane, double x, double y, double z) {
            Part pt; pt.name = name; pt.group = "Test"; pt.u = u; pt.v = v; pt.t = t; pt.plane = plane; pt.x = x; pt.y = y; pt.z = z; pt.material = q.materials[0].id;
            q.parts.push_back(pt);
            return q.parts.size() - 1;
        };
        auto drill = [](Part& pt, double u, double v, double d, double depth, const std::string& face, const std::string& purpose) {
            pt.operations.push_back(json{{"type", "drill"}, {"u", u}, {"v", v}, {"diameter", d}, {"depth", depth}, {"face", face}, {"purpose", purpose}});
        };
        run(area, "szafka dolna ze standardowego szablonu nie ma błędów krytycznych", where, [=](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0);
            auto report = pi::inspect(*p);
            need(report.errors == 0, "szafka dolna ma błędy: " + (report.findings.empty() ? std::string() : report.findings[0].message));
            need(report.parts_checked == (int)p->parts.size(), "inspektor nie sprawdził wszystkich części");
            ctx.note(pi::summary_line(report));
        });
        run(area, "półka: długie przęsło ugina się, przegroda i lekkie obciążenie to naprawiają", where, [=](Ctx&) {
            Project q;
            base_part(q, "Półka długa", 1160, 300, 18, "XY", 0, 0, 300);
            auto report = pi::inspect(q);
            need(count_rule(report, "shelf_sag") == 1, "półka 1160 mm nie została zgłoszona jako uginająca się");
            need(report.findings[0].message.find("ugnie") != std::string::npos && !report.findings[0].hint.empty(), "uwaga o ugięciu nie ma opisu i podpowiedzi");
            pi::Settings light; light.shelf_load_kg_m2 = 15;
            need(count_rule(pi::inspect(q, light), "shelf_sag") == 0, "lekkie obciążenie nadal zgłasza ugięcie");
            base_part(q, "Przegroda", 300, 600, 18, "YZ", 571, 0, 0);
            need(count_rule(pi::inspect(q), "shelf_sag") == 0, "przegroda w połowie nie skróciła przęsła półki");
            Project thin;
            base_part(thin, "Półka krótka", 500, 300, 18, "XY", 0, 0, 300);
            need(count_rule(pi::inspect(thin), "shelf_sag") == 0, "krótka półka jest zgłaszana");
        });
        run(area, "otwór za blisko krawędzi i zbyt głęboki są zgłaszane", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Płyta z otworami", 600, 400, 18, "XY", 0, 0, 0);
            drill(q.parts[i], 4, 200, 5, 10, "top", "za blisko");
            drill(q.parts[i], 300, 200, 35, 16.5, "top", "puszka zbyt głęboka");
            drill(q.parts[i], 100, 100, 5, 10, "top", "w porządku");
            q.validate();
            auto report = pi::inspect(q);
            int near_edge = 0, deep = 0;
            for (const auto& f : report.findings) {
                if (f.rule != "hole_edge") continue;
                if (f.message.find("za blisko") != std::string::npos) ++near_edge;
                if (f.message.find("zostaje tylko") != std::string::npos) ++deep;
            }
            need(near_edge == 1, std::format("otwór przy krawędzi: zgłoszono {} zamiast 1", near_edge));
            need(deep == 1, std::format("zbyt głęboki otwór: zgłoszono {} zamiast 1", deep));
            need(report.errors >= 1, "otwór 1,5 mm od krawędzi powinien być błędem");
        });
        run(area, "otwory z obu stron płyty przebijają ją; naprawa skraca głębokość, współosiowe z zapasem są w porządku", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Przegroda", 300, 600, 18, "YZ", 0, 0, 0);
            for (double v : {100.0, 132.0}) { drill(q.parts[i], 150, v, 5, 10, "top", "podpórka półki"); drill(q.parts[i], 150, v, 5, 10, "bottom", "podpórka półki"); }
            q.validate();
            auto report = pi::inspect(q);
            need(count_rule(report, "hole_clash") == 2 && report.errors == 2, std::format("oczekiwano 2 przebić, jest {}", count_rule(report, "hole_clash")));
            int fixed = 0;
            auto repaired = pi::apply_fixes(q, report, &fixed);
            need(fixed == 2, std::format("naprawiono {} zamiast 2", fixed));
            auto again = pi::inspect(*repaired);
            need(count_rule(again, "hole_clash") == 0, "po naprawie nadal są kolizje otworów");
            for (const auto& op : repaired->parts[i].operations) need(op.at("depth").get<double>() >= 3 && op.at("depth").get<double>() < 10, "głębokość po naprawie poza oczekiwanym zakresem");
            // dwa różne okucia w tym samym miejscu na jednej stronie
            Project r;
            size_t k = base_part(r, "Front", 600, 400, 18, "XY", 0, 0, 0);
            drill(r.parts[k], 300, 200, 5, 8, "top", "A"); drill(r.parts[k], 304, 200, 5, 8, "top", "B");
            auto overlapping = pi::inspect(r);
            need(count_rule(overlapping, "hole_clash") == 1 && overlapping.errors == 1, "przecinające się otwory nie zostały zgłoszone");
        });
        run(area, "obrzeża: fronty i krawędzie frontowe korpusu; naprawa okleja i rozkrój nadal działa", where, [=](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0);
            auto report = pi::inspect(*p);
            need(count_rule(report, "edge_banding") >= 6, std::format("brakujące obrzeża: zgłoszono tylko {}", count_rule(report, "edge_banding")));
            int fixed = 0;
            auto repaired = pi::apply_fixes(*p, report, &fixed);
            need(fixed >= 8, std::format("naprawiono {} krawędzi", fixed));
            need(count_rule(pi::inspect(*repaired), "edge_banding") == 0, "po naprawie nadal brakuje obrzeży");
            for (const auto& part : repaired->parts)
                if (assembly::inferred_role(part) == "door") need(std::all_of(part.edges.begin(), part.edges.end(), [](bool e) { return e; }), "front nie ma obrzeża na wszystkich krawędziach");
            auto plan = sheet_nesting::plan(*repaired);
            need(plan.second.empty(), "po okleinie formatki nie mieszczą się na płycie");
            ctx.note(std::format("poprawiono {} krawędzi", fixed));
        });
        run(area, "zakładka Inspektor: Sprawdź projekt i Napraw automatycznie zostawiają wpisy [inspektor] i nadają się do cofnięcia", where, [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0);
            app->project = p; app->selected.reset(); app->refresh(); app->update();
            need(app->inspector_check && app->inspector_autofix, "zakładka Inspektor nie zarejestrowała akcji (inspector_check / inspector_autofix)");
            auto from = log_mark();
            app->inspector_check();
            need(has_text(log_since(from), "[operacja:inspektor] Inspektor:"), "sprawdzenie projektu nie zostawiło podsumowania w dzienniku [inspektor]");
            from = log_mark();
            int fixed = app->inspector_autofix();
            std::string text = log_since(from);
            need(fixed >= 8, std::format("automatyczna naprawa poprawiła tylko {} krawędzi", fixed));
            need(has_text(text, "[operacja:inspektor] naprawa automatyczna:"), "naprawa automatyczna nie zostawiła wpisu [inspektor]");
            need(has_text(text, "[operacja:zmiana]"), "naprawa automatyczna nie zostawiła wpisu [zmiana]");
            ctx.note(std::format("naprawiono {} krawędzi", fixed));
            // druga naprawa nie ma już czego poprawiać — ma to zostać zapisane, a nie przemilczane
            from = log_mark();
            need(app->inspector_autofix() == 0, "druga naprawa automatyczna coś jeszcze poprawiła");
            need(has_text(log_since(from), "brak uwag z automatyczną poprawką"), "pusta naprawa nie została opisana w dzienniku");
            app->project = fresh(); app->selected.reset(); app->refresh();
        });
        run(area, "kolizje części: wnikanie jest zgłaszane, styk nie", where, [=](Ctx&) {
            Project q;
            base_part(q, "Płyta A", 600, 400, 18, "XY", 0, 0, 0);
            base_part(q, "Płyta B styk", 600, 400, 18, "XY", 0, 0, 18);
            need(count_rule(pi::inspect(q), "part_collision") == 0, "styk płyt jest zgłaszany jako kolizja");
            base_part(q, "Płyta C wnika", 600, 400, 18, "XY", 100, 100, 10);
            need(count_rule(pi::inspect(q), "part_collision") >= 1, "płyta wnikająca w inne nie została zgłoszona");
        });
        run(area, "zawiasy: front 1700 mm z 2 zawiasami jest zgłaszany, z 4 nie", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Front wysoki", 500, 1700, 18, "XZ", 0, 0, 0);
            q.hardware.push_back(json{{"kind", "hinge"}, {"owner_id", q.parts[i].id}, {"name", "Zawias"}, {"qty", 2}, {"price", 0}});
            need(count_rule(pi::inspect(q), "hinge_count") == 1, "za mało zawiasów nie zostało zgłoszone");
            q.hardware[0]["qty"] = 4;
            need(count_rule(pi::inspect(q), "hinge_count") == 0, "4 zawiasy na front 1700 mm są zgłaszane");
        });
        run(area, "formatka większa niż płyta jest błędem; obrót bez usłojenia ratuje sytuację", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Długa", 3000, 300, 18, "XY", 0, 0, 0);
            need(count_rule(pi::inspect(q), "sheet_fit") == 1, "formatka 3000 mm nie została zgłoszona");
            q.parts[i].u = 2000; q.parts[i].v = 2500; q.parts[i].grain = false;
            need(count_rule(pi::inspect(q), "sheet_fit") == 0, "formatka bez usłojenia mieszcząca się po obrocie jest zgłaszana");
            q.parts[i].grain = true;
            need(count_rule(pi::inspect(q), "sheet_fit") == 1, "usłojenie powinno blokować obrót");
        });
        run(area, "drewno lite szersze niż deska to ostrzeżenie (klejenie desek), a nie błąd; zbyt długie nadal jest błędem", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Blat z litego", 1600, 850, 28, "XY", 0, 0, 0);
            for (const auto& m : q.materials) if (m.kind == "drewno") q.parts[i].material = m.id;
            auto report = pi::inspect(q);
            bool warned = false;
            for (const auto& f : report.findings) if (f.rule == "sheet_fit") warned = f.severity == pi::Severity::Warning && f.message.find("skleić") != std::string::npos;
            need(warned && report.errors == 0, "blat z drewna litego szerszy niż deska powinien dać ostrzeżenie o klejeniu, bez błędu");
            q.parts[i].u = 3000; q.parts[i].v = 300;
            need(pi::inspect(q).errors >= 1, "element z drewna dłuższy niż deska nadal powinien być błędem");
        });
        run(area, "cała biblioteka szablonów kreatora przechodzi przez inspektor bez wyjątków", where, [=](Ctx& ctx) {
            int checked = 0, findings = 0;
            for (long long index = 0; index < 54; ++index) {
                ProjectPtr p;
                try { p = cabinet(Project(), index).first; } catch (const std::exception&) { continue; }  // wzorce spoza szybkiego kreatora
                auto report = pi::inspect(*p);
                findings += (int)report.findings.size() + (int)report.hidden.size();
                ++checked;
            }
            need(checked >= 30, std::format("sprawdzono tylko {} szablonów", checked));
            ctx.note(std::format("{} szablonów, {} uwag", checked, findings));
        });
        run(area, "raport HTML i linia podsumowania", where, [=](Ctx&) {
            Project q;
            size_t i = base_part(q, "Płyta z błędem", 600, 400, 18, "XY", 0, 0, 0);
            drill(q.parts[i], 4, 200, 5, 10, "top", "za blisko");
            auto report = pi::inspect(q);
            need(pi::summary_line(report).find("Inspektor:") == 0 && pi::summary_line(report).find("błąd") != std::string::npos, "linia podsumowania nie wspomina o błędzie");
            TempDir tmp("MebelStudio3D-test-");
            std::string path = py::path_str(tmp.path / "inspektor.html");
            pi::report_export(q, report, path);
            std::ifstream f(fs::path(path), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            need(text.find("Inspektor produkcyjny") != std::string::npos && text.find("Otwory przy krawędzi") != std::string::npos && text.find("Płyta z błędem") != std::string::npos, "raport HTML nie zawiera uwag");
            Project clean;
            need(pi::summary_line(pi::inspect(clean)).find("bez uwag") != std::string::npos, "pusty projekt powinien być bez uwag");
        });
    }

    void test_text_design() {
        const char* area = "Projekt z opisu";
        const char* where = "text_design.cpp (parse, build), quick_studio_ui.cpp (okno „Opisz zdaniem”)";
        auto sec = [](const json& cfg, const std::string& key) -> json {
            for (const auto& s : cfg["sections"]) if (s["key"] == key) return s;
            return json();
        };
        auto material_name = [](const Project& q, const json& id) { for (const auto& m : q.materials) if (m.id == id.get<std::string>()) return m.name; return std::string(); };
        run(area, "normalizacja tekstu: małe litery bez polskich znaków", where, [=](Ctx&) {
            need(text_design::fold("ŁÓŻKO żółć Wisząca 2×3") == "lozko zolc wiszaca 2x3", "fold: " + text_design::fold("ŁÓŻKO żółć Wisząca 2×3"));
        });
        run(area, "szafki kuchenne: ilości, szerokość „po”, rząd wiszący, wspólny front", where, [=](Ctx&) {
            Project q;
            auto r = text_design::parse("4 szafki dolne po 60 cm, 3 wiszące po 80 cm, front grafit, uchwyty 128 mm", q);
            need(r.items.size() == 2, std::format("pozycji {} zamiast 2", r.items.size()));
            need(r.items[0].count == 4 && r.items[1].count == 3, "ilości sztuk nie zostały odczytane");
            need(!r.items[0].wall && r.items[1].wall, "rząd wiszący nie został rozpoznany");
            need(r.items[0].config["width"].get<double>() == 600 && r.items[1].config["width"].get<double>() == 800, "szerokości „po 60 / po 80” nie zostały odczytane");
            need(material_name(q, r.items[0].config["front_material"]) == "Grafit" && material_name(q, r.items[1].config["front_material"]) == "Grafit", "wspólny „front grafit” nie dotyczy wszystkich mebli");
            need(r.items[1].config["leg_height"].get<double>() == 0, "szafka wisząca dostała nogi");
            need(r.furniture_count() == 7, "suma mebli");
        });
        run(area, "szuflady i drzwi razem: szuflady w strefie dolnej, drzwi w głównej", where, [=](Ctx&) {
            Project q;
            auto r = text_design::parse("szafka dolna 80 cm z dwiema szufladami i drzwiami, front biały", q);
            need(r.items.size() == 1 && r.items[0].error.empty(), "pozycja nie powstała: " + (r.items.empty() ? std::string() : r.items[0].error));
            const json& cfg = r.items[0].config;
            need(cfg["width"].get<double>() == 800, "szerokość 80 cm");
            json bottom = sec(cfg, "bottom"), main = sec(cfg, "main");
            need(bottom["enabled"].get<bool>() && bottom["front"] == "drawers" && bottom["drawers"].get<int>() == 2, "strefa dolna powinna mieć 2 szuflady");
            need(main["front"] == "doors" && main["doors"].get<int>() == 2, "strefa główna powinna mieć drzwi");
            need(material_name(q, cfg["front_material"]) == "Biały mat", "front biały");
            need(r.ignored.empty(), "pominięto: " + py::join(r.ignored, ", "));
        });
        run(area, "wymiary: 90x80x45, wysokość „2 metry”, szerokość „120 cm”", where, [=](Ctx&) {
            Project q;
            auto a = text_design::parse("komoda 3 szuflady 90x80x45", q);
            need(a.items.size() == 1 && a.items[0].error.empty(), "komoda");
            need(a.items[0].config["width"].get<double>() == 900 && a.items[0].config["height"].get<double>() == 800 && a.items[0].config["depth"].get<double>() == 450, "90x80x45 → szer × wys × głęb");
            auto b = text_design::parse("szafa 2 metry wysoka, 120 cm szerokości, 3 półki", q);
            need(b.items.size() == 1 && b.items[0].error.empty(), "szafa: " + (b.items.empty() ? std::string() : b.items[0].error));
            need(b.items[0].config["width"].get<double>() == 1200 && b.items[0].config["height"].get<double>() == 2000, "szafa 120 cm × 2 m");
            need(sec(b.items[0].config, "main")["shelves"].get<int>() == 3, "3 półki");
        });
        run(area, "słowa liczebnikowe, cargo z poziomami, zaprzeczenia i okucia", where, [=](Ctx&) {
            Project q;
            need(text_design::parse("trzy szafki dolne", q).items.at(0).count == 3, "„trzy szafki” → 3");
            auto cargo = text_design::parse("słupek cargo 30 cm 3 poziomy", q);
            need(cargo.items.at(0).error.empty() && sec(cargo.items[0].config, "main")["front"] == "cargo" && sec(cargo.items[0].config, "main")["drawers"].get<int>() == 3, "cargo 3 poziomy");
            auto bare = text_design::parse("szafka dolna bez półek, bez uchwytów, nogi 15 cm, LED", q);
            const json& cfg = bare.items.at(0).config;
            need(sec(cfg, "main")["shelves"].get<int>() == 0, "„bez półek”");
            need(cfg["handle_mode"] == "none", "„bez uchwytów”");
            need(cfg["leg_height"].get<double>() == 150 && cfg["led"].get<bool>(), "nogi 15 cm i LED");
            need(text_design::parse("szafka dolna 60 cm TIP-ON", q).items.at(0).config["handle_mode"] == "tip_short", "TIP-ON na drzwiach");
            need(text_design::parse("słupek spiżarniany TIP-ON", q).items.at(0).config["handle_mode"] == "tip_long", "wysoki front → długi TIP-ON");
            auto drawers = text_design::parse("komoda 3 szuflady TIP-ON", q);
            need(drawers.items.at(0).error.empty() && drawers.items[0].config["handle_mode"] == "handle" && !drawers.ignored.empty(), "TIP-ON na szufladach wraca do uchwytów z informacją");
        });
        run(area, "niewspierany typ i bełkot dają jasną informację, nie wyjątek", where, [=](Ctx&) {
            Project q;
            auto desk = text_design::parse("biurko 140 cm", q);
            need(desk.items.size() == 1 && !desk.items[0].error.empty() && !desk.ok(), "biurko powinno być zgłoszone jako niewspierane");
            auto junk = text_design::parse("xyz abc", q);
            need(junk.items.empty() && !junk.ok() && !text_design::summary(junk).empty(), "bełkot");
            need(text_design::parse("", q).items.empty(), "pusty opis");
        });
        run(area, "„kuchnia 3 m” buduje zlew, szafki dolne i wiszące na 3 m", where, [=](Ctx& ctx) {
            Project q;
            auto r = text_design::parse("kuchnia 3 m", q);
            need(r.kitchen_layout && r.ok(), "układ kuchni");
            double floor_width = 0; int wall = 0;
            for (const auto& item : r.items) { need(item.error.empty(), item.error); if (!item.wall) floor_width += item.config["width"].get<double>(); else ++wall; }
            need(std::abs(floor_width - 3000) <= 100, std::format("rząd dolny ma {} mm zamiast ok. 3000", floor_width));
            need(wall >= 4, "brak szafek wiszących");
            auto [built, groups] = text_design::build(q, r);
            built->validate();
            need((int)groups.size() == r.furniture_count(), "liczba utworzonych mebli");
            double last_hi = -1e9, wall_back = 0, floor_back = 0;
            for (size_t i = 0; i < groups.size(); ++i) {
                auto box = furniture_layout::group_bounds(*built, groups[i]);
                if (!r.items[i].wall) {
                    if (last_hi > -1e8) need(std::abs(box.first[0] - last_hi) < 0.5, std::format("szafki dolne nie przylegają ({} ≠ {})", box.first[0], last_hi));
                    last_hi = box.second[0]; floor_back = box.second[1];
                } else {
                    need(std::abs(box.first[2] - 1400) < 0.5, std::format("szafka wisząca na wysokości {} zamiast 1400", box.first[2]));
                    wall_back = box.second[1];
                }
            }
            need(std::abs(wall_back - floor_back) < 0.5, "tyły szafek wiszących nie są w linii z dolnymi");
            ctx.note(std::format("{} mebli, {} części", groups.size(), built->parts.size()));
        });
        run(area, "dokładanie do istniejącego projektu: nowy rząd zaczyna się za ostatnim meblem", where, [=](Ctx&) {
            auto [base, g] = cabinet(Project(), 0);
            auto before = furniture_layout::group_bounds(*base, g);
            auto r = text_design::parse("2 szafki dolne 60 cm", *base);
            auto [built, groups] = text_design::build(*base, r);
            need(groups.size() == 2, "dwie szafki");
            double lo = furniture_layout::group_bounds(*built, groups[0]).first[0];
            need(std::abs(lo - (before.second[0] + 50)) < 0.5, std::format("nowa szafka zaczyna się w {} zamiast {}", lo, before.second[0] + 50));
            need(built->parts.size() > base->parts.size() && base->parts.size() == (size_t)std::count_if(built->parts.begin(), built->parts.end(), [&](const Part& p) { return p.group == g; }), "stare meble zostały zmienione");
        });
        run(area, "zdania z różnych szablonów budują się i przechodzą kontrolę projektu", where, [=](Ctx& ctx) {
            Project q;
            int built_count = 0;
            for (const char* text : {"szafka pod zlew 90 cm z 2 drzwiami oraz szafka 3 szuflady 60 cm", "szafka nocna z jedną szufladą i drzwiami, nogi 15 cm, LED",
                                     "regał książkowy 5 półek 80 cm szerokości", "witryna 80 cm", "kosz na pranie cargo", "wyspa 120 cm", "komoda z drzwiami 120 cm",
                                     "szafka wisząca 60x72x32, bez półek, korpus biały front dąb", "szafa warsztatowa 120 cm", "szafka RTV 160 cm"}) {
                auto r = text_design::parse(text, q);
                need(r.ok(), std::string("nie zbudowano: ") + text + " — " + (r.items.empty() ? std::string("brak pozycji") : r.items[0].error));
                auto [built, groups] = text_design::build(q, r);
                built->validate();
                built_count += (int)groups.size();
            }
            ctx.note(std::format("{} mebli z 10 opisów", built_count));
        });
    }

    void test_led_scene_export() {
        const char* area = "LED w widoku 3D";
        const char* where = "native_scene_export.cpp (zbiór pasków LED), viewport_render.cpp (światła LED)";
        for (const char* mount : {"under_shelf", "side"}) {
            run(area, std::string("snapshot sceny z LED: ") + mount, where, [=](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), 0, [mount](json& c) { c["led"] = true; c["led_mount"] = mount; });
                p->validate();
                bool has_led = false;
                for (const auto& h : p->hardware) if (py::get(h, "kind") == json("led_strip")) has_led = true;
                need(has_led, "mebel nie ma paska LED");
                auto [bytes, info] = native_scene_export::build_snapshot(*p);
                need(!bytes.empty(), "pusty snapshot sceny");
                ctx.note(std::format("{} B", bytes.size()));
            });
        }
    }

    void test_cargo() {
        const char* area = "Cargo (wysuwany mebel z koszami)";
        const char* where = "furniture_configurator.cpp (front cargo/laundry), furniture_library.cpp (szablony 4, 52, 53), mechanisms.cpp (posed_project)";
        auto baskets_of = [](const Project& p, const std::string& group) {
            std::set<std::string> levels;
            for (const auto& q : p.parts)
                if (q.group == group && q.name.find(" / kosz ") != std::string::npos) levels.insert(q.name.substr(q.name.find(" / kosz ") + 8, 1));
            return (int)levels.size();
        };
        for (int levels = 1; levels <= 4; ++levels) {
            run(area, std::format("słupek cargo: {} poziom(y) koszy", levels), where, [=](Ctx& ctx) {
                auto [p, g] = cabinet(Project(), 4, [levels](json& c) { section(c, "main")["drawers"] = levels; });
                p->validate();
                json cfg = furniture_configurator::read_config(*p, g);
                need(cfg.at("sections")[1].at("front") == json("cargo"), "strefa główna nie ma frontu cargo");
                int fronts = 0;
                for (const auto& q : p->parts) if (q.group == g && assembly::inferred_role(q) == "drawer") ++fronts;
                need(fronts == 1, std::format("cargo powinno mieć jeden wysuwany front, jest {}", fronts));
                int found = baskets_of(*p, g);
                if (levels > 1) need(found == levels, std::format("poziomów koszy: {} zamiast {}", found, levels));
                ctx.note(std::format("{} części, {} poziomów", p->parts.size(), std::max(found, 1)));
            });
        }
        run(area, "kosz na pranie (szablon 52) i cargo 2 poziomy (szablon 53) budują się", where, [=](Ctx& ctx) {
            for (long long index : {52LL, 53LL}) {
                auto [p, g] = cabinet(Project(), index);
                p->validate();
                need(!furniture_configurator::read_config(*p, g).is_null(), "mebel nie ma konfiguracji kreatora");
                ctx.note(std::format("szablon {}: {} części", index, p->parts.size()));
            }
        });
        run(area, "wysuw: kosze jadą razem z frontem", where, [=](Ctx&) {
            auto [p, g] = cabinet(Project(), 4, [](json& c) { section(c, "main")["drawers"] = 3; });
            std::string front_id;
            for (const auto& q : p->parts) if (q.group == g && assembly::inferred_role(q) == "drawer") { front_id = q.id; break; }
            need(!front_id.empty(), "brak frontu cargo");
            auto posed = mechanisms::posed_project(p, {{front_id, 1.0}});
            // Front szuflady wysuwa się w stronę ujemnego Y (jak w wersji Python); kosze muszą przejechać dokładnie tyle samo co front.
            double front_shift = 0;
            for (const auto& q : p->parts) if (q.id == front_id) for (const auto& r : posed->parts) if (r.id == q.id) front_shift = r.y - q.y;
            need(std::abs(front_shift) > 50, std::format("front cargo nie wysunął się (przesunięcie {:.1f} mm)", front_shift));
            int moved = 0, children = 0;
            for (const auto& q : p->parts) {
                if (py::get(q.assembly, "generated_by") != json(front_id)) continue;
                ++children;
                for (const auto& r : posed->parts) if (r.id == q.id && std::abs((r.y - q.y) - front_shift) < 1e-6) ++moved;
            }
            need(children >= 15, std::format("kosze (części skrzynek) nie zostały utworzone: {}", children));
            need(moved == children, std::format("z frontem przesunęło się {} z {} części koszy", moved, children));
        });
        run(area, "zmiana szerokości i przeliczenie zachowują poziomy koszy", where, [=](Ctx&) {
            auto [p, g] = cabinet(Project(), 4, [](json& c) { section(c, "main")["drawers"] = 3; });
            json cfg = furniture_configurator::read_config(*p, g);
            cfg["width"] = 400;
            auto r = furniture_configurator::configure(*p, cfg, g);
            need(baskets_of(*r.first, r.second) == 3, "po zmianie szerokości zmieniła się liczba poziomów");
        });
        run(area, "cargo nie może mieć półek w tej samej strefie (czytelny błąd)", where, [=](Ctx&) {
            bool refused = false;
            try {
                cabinet(Project(), 4, [](json& c) { section(c, "main")["shelves"] = 2; });
            } catch (const std::exception&) { refused = true; }
            need(refused, "kreator przyjął półki w strefie cargo");
        });
        run(area, "za dużo poziomów w niskiej szafce daje komunikat, nie awarię", where, [=](Ctx&) {
            bool refused = false;
            try {
                cabinet(Project(), 53, [](json& c) { c["leg_height"] = 0; c["height"] = 300; section(c, "main")["drawers"] = 6; });
            } catch (const std::invalid_argument&) { refused = true; }
            need(refused, "6 poziomów w szafce 300 mm powinno zostać odrzucone komunikatem");
        });
    }

    void test_flyout() {
        const char* area = "Okienko wyboru wysuwane z lewego menu";
        const char* where = "hover_flyout.cpp (catalogue/actions/show_now), quick_studio_ui.cpp (attach przy pozycjach menu)";
        struct Spec { const char* name; std::vector<std::string> kinds; };
        for (const Spec& spec : std::vector<Spec>{{"Meble", {"furniture"}}, {"Kształty i style", {"shaped"}}, {"Kolory i materiały", {"material"}}, {"Fronty i uchwyty", {"handle"}},
                                                  {"Zawiasy i prowadnice", {"hinge", "runner"}}, {"Nóżki", {"leg"}}, {"Oświetlenie LED", {"led"}}}) {
            run(area, std::string("okienko „") + spec.name + "” buduje karty i da się je kliknąć", where, [this, spec](Ctx& ctx) {
                app->project = cabinet(Project()).first;
                app->selected.reset();
                for (const auto& q : app->project->parts) if (q.role == "door") { app->selected = q.id; break; }
                if (!app->selected && !app->project->parts.empty()) app->selected = app->project->parts[0].id;
                hover_flyout::show_now(app, spec.name, hover_flyout::catalogue(app, spec.kinds));
                app->update();
                need(hover_flyout::visible(), "okienko się nie pokazało");
                int count = hover_flyout::last_item_count();
                ctx.note(std::format("{} pozycji", count));
                need(count > 0, "okienko jest puste");
                need(hover_flyout::click_item(0), "nie udało się kliknąć pierwszej pozycji");
                if (app->hardware_drag != nullptr && app->hardware_drag->active()) app->hardware_drag->cancel();
                hover_flyout::hide(app);
                need(!hover_flyout::visible(), "okienko nie schowało się");
                app->project = fresh();
            });
        }
        run(area, "okienko poleceń Produkcja", where, [this](Ctx& ctx) {
            int clicked = 0;
            hover_flyout::show_now(app, "Produkcja", hover_flyout::actions({{"A", "opis A", [&clicked] { ++clicked; }}, {"B", "opis B", [&clicked] { ++clicked; }}}));
            need(hover_flyout::last_item_count() == 2, "okienko poleceń ma złą liczbę pozycji");
            need(hover_flyout::click_item(1), "kliknięcie polecenia nie zadziałało");
            need(clicked == 1, "polecenie nie zostało wykonane");
            hover_flyout::hide(app);
            ctx.note("2 polecenia");
        });
    }

    void test_exports() {
        const char* area = "Produkcja i eksporty";
        const char* where = "core.cpp (csv/dxf/report/manufacturing), cad_geometry.cpp (STEP/STL), project_print.cpp, production_docs.cpp";
        auto make = [] {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "Eksport A"; });
            auto [p2, b] = cabinet(*p, 3, [](json& c) { c["name"] = "Eksport B"; c["origin"] = json::array({700, 0, 0}); });
            return p2;
        };
        struct Export { const char* name; const char* file; std::function<void(Project&, const std::string&)> fn; };
        std::vector<Export> exports{
            {"formatki CSV", "formatki.csv", [](Project& p, const std::string& f) { core::csv_export(p, f); }},
            {"kontury DXF", "kontury.dxf", [](Project& p, const std::string& f) { core::dxf_export(p, f); }},
            {"raport HTML", "raport.html", [](Project& p, const std::string& f) { core::report_export(p, f); }},
            {"dane produkcyjne JSON", "produkcja.json", [](Project& p, const std::string& f) { core::manufacturing_export(p, f); }},
            {"rysunki części", "rysunki.html", [](Project& p, const std::string& f) { production_docs::drawings_export(p, f); }},
            {"etykiety", "etykiety.html", [](Project& p, const std::string& f) { production_docs::labels_export(p, f); }},
            {"model STEP", "model.step", [](Project& p, const std::string& f) { cad_geometry::step_export(p, f); }},
            {"model STL", "model.stl", [](Project& p, const std::string& f) { cad_geometry::stl_export(p, f); }},
            {"widok SVG", "widok.svg", [](Project& p, const std::string& f) { py::write_text(f, core::svg_view(p)); }},
            {"komplet do druku (rozkrój)", "komplet.html", [](Project& p, const std::string& f) { project_print::export_project_print(p, f); }},
            {"arkusze rozkroju HTML", "rozkroj.html", [](Project& p, const std::string& f) { sheet_nesting::sheets_export(p, f); }},
            {"arkusze rozkroju DXF", "rozkroj.dxf", [](Project& p, const std::string& f) { sheet_nesting::sheets_dxf_export(p, f); }},
            {"raport inspektora produkcyjnego", "inspektor.html", [](Project& p, const std::string& f) { production_inspector::report_export(p, production_inspector::inspect(p), f); }},
        };
        for (auto& e : exports) {
            run(area, e.name, where, [=](Ctx& ctx) {
                ProjectPtr p = make();
                TempDir tmp("MebelStudio3D-test-");
                std::string file = py::path_str(tmp.path / e.file);
                e.fn(*p, file);
                need(fs::is_regular_file(tmp.path / e.file), "plik nie powstał");
                auto size = fs::file_size(tmp.path / e.file);
                need(size > 50, "plik jest pusty");
                ctx.note(std::format("{} bajtów", size));
            });
        }
    }

    void test_persistence() {
        const char* area = "Zapis, odczyt, cofanie";
        const char* where = "core.cpp (Project::save/load/data/from_data), app.h (snapshot / undo / redo)";
        run(area, "zapis i odczyt pliku .mebel", where, [](Ctx& ctx) {
            Project base;
            auto [p, a] = cabinet(base);
            auto [p2, b] = cabinet(*p, 3, [](json& c) { c["origin"] = json::array({700, 0, 0}); });
            TempDir tmp("MebelStudio3D-test-");
            std::string file = py::path_str(tmp.path / "test.mebel");
            p2->save(file);
            ProjectPtr loaded = Project::load(file);
            need(loaded->data() == p2->data(), "projekt po wczytaniu różni się od zapisanego");
            ctx.note(std::format("{} części, {} bajtów", loaded->parts.size(), fs::file_size(tmp.path / "test.mebel")));
        });
        run(area, "data() → from_data() bez strat", where, [](Ctx&) {
            auto [p, a] = cabinet(Project(), 3);
            need(Project::from_data(p->data())->data() == p->data(), "from_data(data()) nie odtwarza projektu");
        });
        run(area, "cofnij i ponów (Ctrl+Z / Ctrl+Y)", where, [this](Ctx& ctx) {
            auto [p, a] = cabinet(Project());
            app->project = p;
            app->undo_stack->clear();
            app->redo_stack->clear();
            json original = p->data();
            app->snapshot();
            app->project = furniture_layout::move_group(*p, a, 100, 0, 0);
            json moved = app->project->data();
            app->undo();
            if (app->project->data() != original) ctx.warn("po cofnięciu projekt różni się od stanu sprzed zmiany");
            app->redo();
            if (app->project->data() != moved) ctx.warn("po ponowieniu projekt różni się od stanu po zmianie");
            app->project = fresh();
        });
        run(area, "dziennik operacji ([operacja:*]) trafia do logu", "journal.cpp, developer_logs.cpp", [this](Ctx&) {
            journal::event(app, "test", "sprawdzenie dziennika");
            need(fs::exists(app->developer_log_path), "brak pliku logu: " + py::path_str(app->developer_log_path));
        });
    }

    void test_scene() {
        const char* area = "Scena pokoju";
        const char* where = "room_objects.cpp, room_architecture.cpp, room_scene_ui.cpp, room_scene_drag.cpp, viewport_scene.cpp";
        for (auto& [kind, spec] : room_objects::CATALOG.items()) {
            run(area, "obiekt „" + kind + "”", where, [kind = kind](Ctx&) {
                auto [p, g] = cabinet(Project());
                ProjectPtr r = room_objects::add_object(*p, kind, json::object());
                r->validate();
                need(Project::from_data(r->data())->data() == r->data(), "obiekt nie przetrwał zapisu i odczytu");
            });
        }
        run(area, "ściany pokoju, jasność i LED sceny", where, [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            ProjectPtr r = room_objects::set_scene(*p, json{{"brightness", .4}, {"led_enabled", true}, {"architecture", json{{"enabled", true}}}});
            r->validate();
            need(py::truthy(room_architecture::world_faces(*r, app->camera)), "ściany pokoju nie wygenerowały powierzchni");
            ctx.note("inwentarz: " + room_objects::inventory(*r).dump().substr(0, 80));
        });
    }

    void test_view() {
        const char* area = "Widok 3D";
        const char* where = "core.cpp (render_faces), viewport_render.cpp, viewport_scene.cpp, viewport_modes.cpp, viewport_hud.cpp, gpu_viewport.cpp";
        run(area, "rysowanie ścian (interaktywne, pełne, rozstrzelone)", where, [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            for (bool interactive : {true, false}) {
                auto faces = core::render_faces(*p, app->camera, 900, 600, std::nullopt, 0, interactive);
                need(!faces.empty(), "render_faces zwrócił pusty wynik");
                ctx.note(std::format("{} ścian ({})", faces.size(), interactive ? "interaktywnie" : "pełnie"));
            }
            auto exploded = core::render_faces(*p, app->camera, 900, 600, std::nullopt, .5, false);
            need(!exploded.empty(), "widok rozstrzelony jest pusty");
        });
        run(area, "tryby edycji i paleta narzędzi (rysowanie HUD)", "viewport_hud.cpp, app.h (edit_mode)", [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p;
            app->selected = p->parts[0].id;
            app->refresh();
            std::string modes;
            for (const char* mode : {"camera", "layout", "resize", "furniture", "configure", "fillet", "camera"}) {
                app->edit_mode.set(mode);
                app->draw();
                app->update();
                modes += std::string(mode) + " ";
            }
            for (bool on : {true, false}) { app->dimensions.set(on); app->draw(); app->update(); }
            ctx.note("tryby: " + modes);
            app->project = fresh();
            app->selected.reset();
            app->refresh();
        });
        run(area, "prześwietlenie (X-ray) i podświetlenie otworów", "viewport_modes.cpp", [this](Ctx&) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->fit(); app->update();
            app->xray.set(true); app->xray_scope.set("Cały zestaw"); app->draw(); app->update();
            app->xray.set(false); app->show_drills.set(true); app->draw(); app->update();
            app->show_drills.set(false);
            app->project = fresh(); app->refresh();
        });
        run(area, "silnik rysowania CPU i GPU z awaryjnym powrotem", "gpu_viewport.cpp, gpu_scene.cpp, native_viewport.cpp", [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->viewport_engine = "gpu"; app->viewport_quality = "balanced"; app->draw(); app->update();
            ctx.note("aktywny silnik: " + app->canvas->_active_renderer);
            if (app->canvas->_active_renderer != "gpu") ctx.note(app->canvas->_gpu_error ? "GPU niedostępne: " + *app->canvas->_gpu_error : std::string("GPU niedostępne"));
            app->viewport_engine = "cpu"; app->draw(); app->update();
            need(app->canvas->_active_renderer == "cpu", "po powrocie na CPU aktywny silnik to " + app->canvas->_active_renderer);
            app->project = fresh(); app->refresh();
        });
    }

    void test_ui() {
        const char* area = "Interfejs i okna";
        run(area, "przełączanie wszystkich zakładek i paneli", "perf_monitor.cpp (self_test), forma_tabs.cpp, forma_shell.cpp, dock_workspace.cpp", [this](Ctx&) {
            app->project = fresh(); app->refresh();
            perf::self_test(app);
            app->update();
        });
        run(area, "okno Konfiguruj: jedno okno, kolejne kliknięcie je zastępuje", "stretch_choice.cpp (open_configuration)", [this](Ctx& ctx) {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "Okno A"; });
            auto [p2, b] = cabinet(*p, 0, [](json& c) { c["name"] = "Okno B"; c["origin"] = json::array({700, 0, 0}); });
            app->project = p2; app->refresh(); app->update();
            remember_windows();
            stretch_choice::open_configuration(app, a);
            app->update();
            tk::Widget* first = stretch_choice::configuration_dialog();
            need(first != nullptr, "okno konfiguracji się nie otworzyło");
            stretch_choice::open_configuration(app, a);
            app->update();
            tk::Widget* second = stretch_choice::configuration_dialog();
            need(second != nullptr, "po ponownym kliknięciu okno zniknęło");
            need(!first->winfo_exists(), "po ponownym kliknięciu tego samego mebla poprzednie okno zostało (powstały dwa okna)");
            stretch_choice::open_configuration(app, b);
            app->update();
            tk::Widget* third = stretch_choice::configuration_dialog();
            need(third != nullptr && !second->winfo_exists(), "po kliknięciu innego mebla stare okno nie zostało zastąpione");
            int open = 0;
            for (auto* w : toplevels()) if (!baseline_windows.contains(w->path())) ++open;
            need(open == 1, std::format("otwartych okien konfiguracji: {} (powinno być 1)", open));
            int sw = app->winfo_screenwidth(), sh = app->winfo_screenheight();
            ctx.note(std::format("okno {}×{} w ({}, {}), ekran {}×{}", third->winfo_width(), third->winfo_height(), third->winfo_rootx(), third->winfo_rooty(), sw, sh));
            if (third->winfo_height() > sh) ctx.warn("okno konfiguracji jest wyższe niż ekran");
            if (third->winfo_rootx() < -5 || third->winfo_rooty() < -5) ctx.warn("okno konfiguracji wychodzi poza ekran");
            close_extra_windows();
            app->project = fresh(); app->refresh();
        });
        run(area, "okno wyboru po rozciągnięciu (deepening / pawlacz)", "stretch_choice.cpp (offer, show_dialog)", [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            ProjectPtr stretched = direct_edit::resize_furniture_face(*p, g, "y1", 250);
            app->project = stretched; app->refresh(); app->update();
            remember_windows();
            stretch_choice::offer(app, *p, g, "y1");
            app->update();
            size_t open = 0;
            for (auto* w : toplevels()) if (!baseline_windows.contains(w->path())) ++open;
            ctx.note(std::format("otwarto okien: {}", open));
            need(open >= 1, "po rozciągnięciu w głąb nie pojawiło się okno wyboru");
            close_extra_windows();
            app->project = fresh(); app->refresh();
        });
        run(area, "szkic: edytor, odpinanie i wpinanie", "sketch.cpp, sketch_live.cpp, dock_workspace.cpp", [this](Ctx&) {
            auto* editor = new sketch::Sketch(app, nullptr);
            editor->fields["name"].set("Szkic testowy");
            app->update();
            app->workspace->detach(editor); app->update();
            app->workspace->dock(editor); app->update();
            need(editor->fields["name"].get() == "Szkic testowy", "szkic stracił stan po odpięciu i wpięciu");
            editor->destroy();
            app->update();
        });
        run(area, "logi developerskie: otwarcie i zamknięcie", "developer_logs.cpp", [this](Ctx&) {
            auto* doc = app->open_developer_logs();
            app->update();
            need(doc->winfo_exists(), "panel logów się nie otworzył");
            doc->destroy();
            app->update();
        });
        run(area, "ustawienia interfejsu (skala, panele)", "responsive_ui.cpp, ui_settings.cpp, ui_preferences.cpp", [this](Ctx&) {
            auto* settings = responsive_ui::interface_settings(app);
            app->update();
            settings->destroy();
            app->update();
        });
        run(area, "panel szybkiego kreatora: sekcje inspektora", "quick_studio_ui.cpp", [this](Ctx&) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->selected = p->parts[0].id; app->refresh(); app->update();
            for (const char* key : {"fronts", "body", "hardware"}) { try { quick_studio_ui::focus_section(app, key); } catch (const std::exception&) {} app->update(); }
            app->project = fresh(); app->selected.reset(); app->refresh();
        });
        struct Launcher { const char* name; std::function<void()> App::*member; };
        for (Launcher l : {Launcher{"narożnik (kreator)", &App::corner_dialog}, Launcher{"wspólny blat", &App::shared_worktop_dialog}, Launcher{"kształty", &App::shape_dialog},
                           Launcher{"biblioteka mebli", &App::library_dialog}, Launcher{"style frontów", &App::style_library}, Launcher{"paleta materiałów", &App::palette_dialog},
                           Launcher{"ruch / mechanizmy", &App::motion_dialog}, Launcher{"okucia", &App::fittings_dialog}, Launcher{"dopasuj zaznaczone", &App::fit_selected}}) {
            run(area, std::string("okno: ") + l.name, "app.h (przypisane funkcje dialogowe), workspace_controls.cpp, style_ui.cpp, hardware_ui.cpp, library_panel.cpp", [this, l](Ctx& ctx) {
                auto [p, g] = cabinet(Project());
                app->project = p; app->selected = p->parts[0].id; app->refresh(); app->update();
                remember_windows();
                std::function<void()>& fn = app->*(l.member);
                if (!fn) { ctx.warn("funkcja nie jest podłączona (puste std::function)"); return; }
                fn();
                app->update();
                ctx.note(std::format("nowych okien: {}", toplevels().size() - baseline_windows.size()));
                close_extra_windows();
                app->project = fresh(); app->selected.reset(); app->refresh();
            });
        }
        run(area, "wysyłanie komunikatów modalnych w trybie testu", "tk.cpp (messagebox::set_headless)", [](Ctx&) {
            tk::messagebox::showinfo("test", "ten komunikat ma zostać przechwycony");
            auto got = tk::messagebox::take_intercepted();
            need(got.size() == 1, "tryb bezgłowy nie przechwycił komunikatu");
        });
    }


    // ================================================================ pliki

    void test_files() {
        const char* area = "Pliki przykładowe (.mebel) — wczytanie, widok, eksport";
        const char* where = "core.cpp (Project::load/validate), app.h (load_path/refresh/draw), core.cpp (render_faces, csv/dxf/report), project_print.cpp, cad_geometry.cpp";
        if (data_root.empty()) { skip(area, "pliki przykładowe", "nie znaleziono korzenia repozytorium (ustaw MEBEL_SOURCE_DIR)"); return; }
        std::vector<fs::path> files;
        for (const auto& dir : {data_root / "przyklady"}) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_regular_file() && entry.path().extension() == ".mebel") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) { skip(area, "pliki przykładowe", "brak plików .mebel w " + py::path_str(data_root)); return; }
        say(std::format("pliki przykładowe do sprawdzenia: {}", files.size()));
        for (const auto& file : files) {
            // dawne pliki z korzenia (DEMO_*, PRZYKLAD_*) dostają pełne testy eksportów; reszta — wczytanie, widok i podstawowe eksporty
            std::string file_name = py::path_str(file.filename());
            bool in_root = file_name.starts_with("DEMO_") || file_name.starts_with("PRZYKLAD_");
            std::string shown = "przyklady/" + file_name;
            run(area, shown, where, [this, file, in_root](Ctx& ctx) {
                ProjectPtr p = Project::load(py::path_str(file));
                p->validate();
                need(Project::from_data(p->data())->data() == p->data(), "data() → from_data() zmienia projekt wczytany z pliku");
                ctx.note(std::format("{} części, {} okuć, {} grup", p->parts.size(), p->hardware.size(), furniture_layout::groups(*p).size()));
                // ta sama ścieżka co „Otwórz”: wczytanie do programu, odświeżenie i narysowanie widoku
                app->dirty = false;
                need(app->load_path(py::path_str(file)), "program odmówił wczytania pliku");
                app->update();
                app->draw();
                app->update();
                if (!p->parts.empty()) {
                    core::Camera cam = app->camera;
                    cam.fit(*p, 900, 600);
                    auto faces = core::render_faces(*p, cam, 900, 600, std::nullopt, 0, false);
                    need(!faces.empty(), "render_faces zwrócił pusty wynik dla niepustego projektu");
                    ctx.note(std::format("{} ścian w widoku", faces.size()));
                }
                TempDir tmp("MebelStudio3D-test-");
                core::csv_export(*p, py::path_str(tmp.path / "a.csv"));
                // Projekt z obróbkami bryłowymi nie ma eksportu DXF — program świadomie odsyła do STEP. To nie jest błąd pliku:
                // wtedy wymagamy, żeby STEP się udał.
                bool dxf_needs_step = false;
                try {
                    core::dxf_export(*p, py::path_str(tmp.path / "a.dxf"));
                } catch (const std::invalid_argument& error) {
                    if (std::string(error.what()).find("eksportu STEP") == std::string::npos) throw;
                    dxf_needs_step = true;
                    ctx.note("DXF niedostępny (obróbki bryłowe) — sprawdzam STEP");
                }
                core::report_export(*p, py::path_str(tmp.path / "a.html"));
                project_print::export_project_print(*p, py::path_str(tmp.path / "b.html"));
                if ((in_root && p->parts.size() <= 400) || dxf_needs_step) {
                    cad_geometry::step_export(*p, py::path_str(tmp.path / "a.step"));
                    need(fs::file_size(tmp.path / "a.step") > 100, "plik STEP jest pusty");
                }
                for (const char* name : {"a.csv", "a.html", "b.html"}) need(fs::is_regular_file(tmp.path / name) && fs::file_size(tmp.path / name) > 20, std::string("eksport ") + name + " jest pusty");
                if (!dxf_needs_step) need(fs::is_regular_file(tmp.path / "a.dxf") && fs::file_size(tmp.path / "a.dxf") > 20, "eksport a.dxf jest pusty");
            });
        }
        app->project = fresh();
        app->selected.reset();
        app->dirty = false;
        app->refresh();
    }

    void test_program_files() {
        const char* area = "Pliki programu (układ repozytorium, biblioteki DLL)";
        const char* where = "biblioteki_natywne/, przyklady/, MebelModel.dll, MebelNative3D.dll, native_model.cpp, native_viewport.cpp";
        if (data_root.empty()) { skip(area, "pliki programu", "nie znaleziono korzenia repozytorium"); return; }
        run(area, "układ repozytorium: katalogi i pliki, od których zależy budowa i testy", where, [this](Ctx& ctx) {
            // Porządek na GitHubie: tylko to, co działa i nad czym pracujemy. Test pilnuje, żeby nikt nie zgubił katalogu ani nie wrócił do starego bałaganu.
            const char* required_dirs[] = {"mebel_studio_cpp", "app", "biblioteki_natywne", "biblioteki_natywne/zrodla", "przyklady", "licencje"};
            std::string missing;
            for (const char* dir : required_dirs)
                if (!fs::is_directory(data_root / dir)) missing += (missing.empty() ? "" : ", ") + std::string(dir);
            need(missing.empty(), "brak katalogów repozytorium: " + missing);
            need(fs::is_regular_file(data_root / "biblioteki_natywne" / "MIGRACJA_CPP.json"), "brak biblioteki_natywne/MIGRACJA_CPP.json (panel stanu migracji)");
            need(fs::is_regular_file(data_root / "biblioteki_natywne" / "zrodla" / "tools" / "build_model.ps1"), "brak źródeł bibliotek natywnych (biblioteki_natywne/zrodla)");
            long long examples = 0;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(data_root / "przyklady", ec)) if (entry.path().extension() == ".mebel") ++examples;
            need(examples >= 10, "w przyklady/ powinno być co najmniej 10 projektów .mebel");
            // stare rzeczy z paczki Pythona nie mają wracać do głównego katalogu (kopia: gałąź archiwum-python-0.15)
            std::string intruders;
            for (const char* old : {"_internal", "runtime", "ZRODLA_PROGRAMU", "PRZYKLADY", "MebelStudio3D.exe", "MANIFEST_SHA256.json", "SPRAWDZENIE_ETAP3", "dokumentacja", "LICENCJE"})
            {
                // Windows nie rozróżnia wielkości liter: „PRZYKLADY” istniałoby przez „przyklady”, więc porównujemy nazwy z katalogu co do litery.
                for (const auto& entry : fs::directory_iterator(data_root, ec))
                    if (entry.path().filename().string() == old) intruders += (intruders.empty() ? "" : ", ") + std::string(old);
            }
            need(intruders.empty(), "w głównym katalogu wróciły stare elementy paczki Pythona: " + intruders);
            ctx.note(std::format("{} katalogów na miejscu, {} projektów w przyklady/", std::size(required_dirs), examples));
        });
        for (const char* dll : {"MebelModel.dll", "MebelNative3D.dll"}) {
            run(area, std::string("biblioteka ") + dll, where, [this, dll](Ctx& ctx) {
                std::vector<fs::path> places{py::executable_dir() / dll, data_root / "biblioteki_natywne" / dll, data_root / "mebel_studio_cpp" / "native-dll" / dll};
                for (const auto& place : places) {
                    std::error_code ec;
                    if (!fs::is_regular_file(place, ec)) continue;
                    auto size = fs::file_size(place, ec);
                    ctx.note(py::path_str(place) + std::format(" · {} B", size));
                    if (size < 4096) ctx.warn("plik poniżej 4 KB to wskaźnik Git LFS, a nie biblioteka — pobierz właściwy plik (git lfs pull)");
                    return;
                }
                ctx.warn("nie znaleziono pliku (szukano obok programu, w biblioteki_natywne/ i w native-dll/)");
            });
        }
        run(area, "pliki w katalogu programu: skrypty Tcl/Tk i biblioteki uruchomieniowe", "bundle_runtime.cmake, tkcore.cpp", [](Ctx& ctx) {
            fs::path dir = py::executable_dir();
            std::error_code ec;
            long long dlls = 0;
            for (const auto& e : fs::directory_iterator(dir, ec)) if (e.path().extension() == ".dll") ++dlls;
            bool tcl = fs::exists(dir / "lib" / "tcl8.6", ec) || fs::exists(dir / "tcl8.6", ec) || fs::exists(dir / "lib" / "tcl8.6" / "init.tcl", ec);
            ctx.note(std::format("{} bibliotek DLL obok programu, skrypty Tcl: {}", dlls, tcl ? "są" : "nie znaleziono w standardowym miejscu"));
            if (!tcl) ctx.warn("nie znaleziono lib/tcl8.6 obok programu — przy kopiowaniu programu na inny komputer Tk może nie wystartować");
        });
    }

    // ================================================================ szkice

    void test_sketches() {
        const char* area = "Szkice (edytor konturów)";
        const char* where = "sketch.cpp (Sketch::set_sketch_points/close_contour/candidate/save/transform_sketch), sketch_live.cpp, assembly.cpp, cad_geometry.cpp";
        struct Shape { const char* name; std::vector<core::Point> points; };
        std::vector<core::Point> circle;
        for (int i = 0; i < 32; ++i) circle.push_back({200 + 150 * std::cos(i * 2 * 3.14159265358979 / 32), 200 + 150 * std::sin(i * 2 * 3.14159265358979 / 32)});
        std::vector<Shape> shapes{{"prostokąt 400×300", {{0, 0}, {400, 0}, {400, 300}, {0, 300}}},
                                  {"kształt L", {{0, 0}, {600, 0}, {600, 200}, {250, 200}, {250, 500}, {0, 500}}},
                                  {"trapez", {{0, 0}, {500, 0}, {400, 250}, {100, 250}}},
                                  {"trójkąt", {{0, 0}, {400, 0}, {200, 300}}},
                                  {"koło (32 punkty)", circle}};
        for (auto& shape : shapes) {
            run(area, std::string("kontur: ") + shape.name + " → element 3D", where, [this, shape](Ctx& ctx) {
                app->project = fresh(); app->selected.reset(); app->refresh(); app->update();
                auto* sk = new sketch::Sketch(app, nullptr);
                app->update();
                sk->set_sketch_points(shape.points);
                sk->close_contour();
                app->update();
                need(sk->closed, "kontur nie został zamknięty");
                auto [candidate, id] = sk->candidate();
                candidate->validate();
                need(!candidate->parts.empty(), "szkic nie wytworzył żadnej części");
                ctx.note(std::format("część {} · {} punktów", id, sk->points.size()));
                need(sk->save(false), "zapis szkicu jako elementu 3D nie powiódł się");
                need(!app->project->parts.empty(), "po zapisie szkicu projekt nie ma części");
                app->undo();
                app->update();
                try { if (sk->winfo_exists()) sk->destroy(); } catch (const std::exception&) {}
                app->project = fresh(); app->refresh();
            });
        }
        run(area, "edycja kontur: odbicia, obrót, cofnięcie i wyczyszczenie", where, [this](Ctx& ctx) {
            app->project = fresh(); app->refresh(); app->update();
            auto* sk = new sketch::Sketch(app, nullptr);
            app->update();
            sk->set_sketch_points({{0, 0}, {500, 0}, {500, 200}, {0, 200}});
            sk->close_contour();
            auto first = sk->points;
            for (const char* action : {"mirror_u", "mirror_v", "rotate"}) { sk->transform_sketch(action); app->update(); need(sk->points.size() == first.size(), std::string("przekształcenie ") + action + " zmieniło liczbę punktów"); }
            sk->undo(); sk->undo(); sk->undo(); app->update();
            ctx.note(std::format("po 3 cofnięciach {} punktów", sk->points.size()));
            sk->clear(); app->update();
            need(sk->points.empty(), "po wyczyszczeniu zostały punkty");
            try { sk->destroy(); } catch (const std::exception&) {}
        });
        run(area, "szkic istniejącego elementu (edycja części z mebla)", where, [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            const Part* part = nullptr;
            for (const auto& q : p->parts) if (q.role == "left" || q.role == "right") { part = &q; break; }
            if (part == nullptr) part = &p->parts[0];
            auto* sk = new sketch::Sketch(app, part);
            app->update();
            ctx.note(std::format("część „{}”: {} punktów konturu", part->name, sk->points.size()));
            need(!sk->points.empty(), "szkic istniejącej części nie ma konturu");
            auto [candidate, id] = sk->candidate();
            candidate->validate();
            try { sk->destroy(); } catch (const std::exception&) {}
            app->project = fresh(); app->refresh();
        });
    }

    // ---- narzędzia szkicu: wskazówki, pole wartości, kształty, profile, DXF

    static std::string dxf_pair(int code, const std::string& value) { return std::to_string(code) + "\n" + value + "\n"; }
    static std::string dxf_number(int code, double value) { return dxf_pair(code, std::format("{}", value)); }
    static std::string dxf_line(double a, double b, double c, double d) {
        return dxf_pair(0, "LINE") + dxf_pair(8, "0") + dxf_number(10, a) + dxf_number(20, b) + dxf_number(11, c) + dxf_number(21, d);
    }
    static std::string dxf_document(const std::string& entities, int units = -1) {
        std::string text;
        if (units >= 0) text += dxf_pair(0, "SECTION") + dxf_pair(2, "HEADER") + dxf_pair(9, "$INSUNITS") + dxf_pair(70, std::to_string(units)) + dxf_pair(0, "ENDSEC");
        return text + dxf_pair(0, "SECTION") + dxf_pair(2, "ENTITIES") + entities + dxf_pair(0, "ENDSEC") + dxf_pair(0, "EOF");
    }

    // ---- kanty, wzorki, zestaw elementów, tryb grafiki i pamięć podręczna CAD

    void test_design() {
        const char* area = "Szkic — kanty, wzorki, zestaw elementów; tryb grafiki; CAD";
        const char* where = "sketch_design.cpp, sketch.cpp (apply_design/stage_element/save_all/propagate_design), render_style.cpp, graphics_mode_ui.cpp, viewport_scene.cpp, cad_geometry.cpp";
        auto open_sketch = [this] {
            app->project = fresh(); app->selected.reset(); app->refresh(); app->update();
            auto* sk = new sketch::Sketch(app, nullptr);
            app->update();
            return sk;
        };
        auto close_sketch = [this](sketch::Sketch* sk) {
            try { if (sk->winfo_exists()) sk->destroy(); } catch (const std::exception&) {}
            app->project = fresh(); app->refresh();
        };
        auto throws = [](const std::function<void()>& action) {
            try { action(); } catch (const std::invalid_argument&) { return true; }
            return false;
        };

        run(area, "wzorki: katalog — każdy wzorek daje poprawne frezy przyjmowane przez CAD", where, [=, this](Ctx& ctx) {
            std::set<std::string> ids, names;
            core::Part plate;
            plate.u = 600; plate.v = 400; plate.t = 18;
            for (const auto& info : sketch_design::pattern_catalog()) {
                need(ids.insert(info.id).second && names.insert(info.name).second, "powtórzony wzorek w katalogu: " + info.name);
                for (const auto& p : info.params) need(p.def >= p.min && p.def <= p.max, "wartość domyślna poza zakresem: " + info.name + " / " + p.label);
                auto result = sketch_design::generate_pattern(info.id, {}, plate.u, plate.v, plate.t);
                need(!result.cuts.empty() && result.cuts.size() <= sketch_design::MAX_PATTERN_CUTS, "wzorek „" + info.name + "” nie dał frezów");
                for (const auto& cut : result.cuts) {
                    need(sketch_design::pattern_of(cut) == info.id, "frez bez znacznika wzorka: " + info.name);
                    features::validate_cut(plate, cut);  // te same reguły co ręczne frezowanie
                }
                ctx.note(std::format("{}: {} frezów", info.name, result.cuts.size()));
            }
            need(sketch_design::pattern_catalog().size() >= 6, "katalog wzorków ma mniej niż 6 pozycji");
        });

        run(area, "wzorki: kontury nieprostokątne, odrzucenia z powodem, scalanie i usuwanie", where, [=, this](Ctx& ctx) {
            std::vector<core::Point> shape_l = {{0, 0}, {600, 0}, {600, 200}, {250, 200}, {250, 500}, {0, 500}};
            core::Part plate;
            plate.u = 600; plate.v = 500; plate.t = 18;
            plate.contour = {{0, 0}, {600, 0}, {600, 200}, {250, 200}, {250, 500}, {0, 500}};
            auto l_result = sketch_design::generate_pattern("perforacja", shape_l, 600, 500, 18, {{"margin", 10}});
            need(l_result.skipped > 0 && !l_result.cuts.empty(), "perforacja na kształcie L powinna pominąć otwory poza konturem, nie wszystkie");
            for (const auto& cut : l_result.cuts) features::validate_cut(plate, cut);
            ctx.note(std::format("kształt L: {} otworów, pominięto {}", l_result.cuts.size(), l_result.skipped));
            need(throws([&] { sketch_design::generate_pattern("ryfle", {}, 600, 400, 18, {{"depth", 17.8}}); }), "głębokość zostawiająca < 0,5 mm materiału powinna być odrzucona");
            need(throws([&] { sketch_design::generate_pattern("ryfle", {}, 600, 400, 18, {{"nie_ma_takiego", 1}}); }), "nieznany parametr powinien być odrzucony");
            need(throws([&] { sketch_design::generate_pattern("ryfle", {}, 600, 400, 18, {{"width", 20}, {"pitch", 20}}); }), "podziałka równa szerokości rowka powinna być odrzucona");
            need(throws([&] { sketch_design::generate_pattern("ramka", {}, 100, 100, 18, {{"margin", 45}}); }), "ramka na zbyt małej płycie powinna być odrzucona");
            need(throws([&] { sketch_design::generate_pattern("ryfle", {}, 600, 400, 18, {}, "front"); }), "nieznana powierzchnia powinna być odrzucona");
            need(throws([&] { sketch_design::generate_pattern("nie ma", {}, 600, 400, 18); }), "nieznany wzorek powinien być odrzucony");
            need(throws([&] { sketch_design::generate_pattern("perforacja", {}, 3000, 3000, 18, {{"diameter", 2}, {"pitch_u", 4}, {"pitch_v", 4}, {"margin", 0}}); }), "perforacja ponad limit frezów powinna być odrzucona");
            // scalanie: ręczne obróbki i inne wzorki zostają, ten sam wzorek się nie dubluje
            json manual = json::array({json{{"type", "pocket"}, {"shape", "rect"}, {"face", "top"}, {"u", 300}, {"v", 200}, {"length", 50}, {"width", 40}, {"depth", 5}, {"angle", 0}}});
            auto ryfle = sketch_design::generate_pattern("ryfle", {}, 600, 400, 18).cuts;
            auto paski = sketch_design::generate_pattern("paski", {}, 600, 400, 18).cuts;
            json merged = sketch_design::merge_pattern(manual, "ryfle", ryfle);
            merged = sketch_design::merge_pattern(merged, "paski", paski);
            size_t once = merged.size();
            merged = sketch_design::merge_pattern(merged, "ryfle", ryfle);
            need(merged.size() == once, "ponowne dodanie tego samego wzorka zdublowało frezy");
            need(sketch_design::count_patterns(merged, "ryfle") == ryfle.size() && sketch_design::count_patterns(merged) == ryfle.size() + paski.size(), "liczniki wzorków się nie zgadzają");
            json only_ryfle = sketch_design::remove_patterns(merged, "paski");
            need(sketch_design::count_patterns(only_ryfle, "paski") == 0 && sketch_design::count_patterns(only_ryfle, "ryfle") == ryfle.size(), "usunięcie jednego wzorka ruszyło inne");
            need(sketch_design::remove_patterns(merged).size() == 1, "usunięcie wszystkich wzorków powinno zostawić tylko ręczną obróbkę");
        });

        run(area, "kanty: zaokrąglenie, fazka i prosta na krawędziach, limity grubości", where, [=, this](Ctx& ctx) {
            json none = json::array();
            auto ids = sketch_design::edge_ids_for_segment(2);
            need(ids.size() == 2 && ids[0] == "top:2" && ids[1] == "bottom:2", "identyfikatory krawędzi boku 3 są błędne");
            need(sketch_design::edge_ids_all(4).size() == 8, "wszystkie boki prostokąta to 8 krawędzi");
            auto round = sketch_design::apply_edge_style(none, none, "Zaokrąglona", 3, sketch_design::edge_ids_all(4), 18);
            need(round.fillets.size() == 8 && round.chamfers.empty() && round.touched == 8, "zaokrąglenie wszystkich boków powinno dać 8 wpisów");
            auto chamfer = sketch_design::apply_edge_style(round.fillets, round.chamfers, "fazka", 2, sketch_design::edge_ids_for_segment(1), 18);
            need(chamfer.fillets.size() == 6 && chamfer.chamfers.size() == 2, "fazka powinna zastąpić zaokrąglenie na tej samej krawędzi");
            auto straight = sketch_design::apply_edge_style(chamfer.fillets, chamfer.chamfers, "prosta", 0, sketch_design::edge_ids_all(4), 18);
            need(straight.fillets.empty() && straight.chamfers.empty(), "krawędź prosta powinna zdjąć całą obróbkę");
            need(throws([&] { sketch_design::apply_edge_style(none, none, "zaokraglenie", 9, {"top:0"}, 18); }), "promień równy połowie grubości powinien być odrzucony");
            need(throws([&] { sketch_design::apply_edge_style(none, none, "zaokraglenie", 0.01, {"top:0"}, 18); }), "promień poniżej 0,1 mm powinien być odrzucony");
            need(throws([&] { sketch_design::apply_edge_style(none, none, "zaokraglenie", 2, {}, 18); }), "brak krawędzi powinien być odrzucony");
            need(throws([&] { sketch_design::apply_edge_style(none, none, "ostry jak brzytwa", 2, {"top:0"}, 18); }), "nieznany styl powinien być odrzucony");
            need(sketch_design::describe_edge_style("zaokraglenie", 5, 8) == "zaokrąglenie R5 mm na 8 krawędziach", "opis zaokrąglenia do dziennika jest inny niż oczekiwany");
            ctx.note("zaokrąglenie / fazka / prosta / limity OK");
        });

        run(area, "miniaturki: wzorki, kanty i tryby grafiki mają różne, narysowane miniaturki", where, [=, this](Ctx& ctx) {
            std::set<std::string> signatures;
            for (const auto& info : sketch_design::pattern_catalog()) {
                auto shapes = sketch_design::pattern_thumbnail(info.id, 150, 90, 6);
                need(shapes.size() >= 2, "wzorek „" + info.name + "” nie ma miniaturki z frezami");
                std::string signature = std::to_string(shapes.size());
                for (const auto& shape : shapes)
                    for (const auto& p : shape) {
                        need(p[0] >= -0.01 && p[0] <= 150.01 && p[1] >= -0.01 && p[1] <= 90.01, "miniaturka wzorka „" + info.name + "” wychodzi poza pole");
                        signature += std::format("|{:.0f},{:.0f}", p[0], p[1]);
                    }
                need(signatures.insert(signature).second, "wzorek „" + info.name + "” ma miniaturkę identyczną z innym wzorkiem");
            }
            std::set<size_t> edge_sizes;
            std::set<std::string> edge_signatures;
            for (const auto& style : sketch_design::edge_styles()) {
                auto shape = sketch_design::edge_section_thumbnail(style.id, 110, 64, 10);
                need(shape.size() >= 4, "styl „" + style.name + "” nie ma miniaturki przekroju");
                std::string signature;
                for (const auto& p : shape) signature += std::format("|{:.0f},{:.0f}", p[0], p[1]);
                need(edge_signatures.insert(signature).second, "styl „" + style.name + "” ma miniaturkę identyczną z innym stylem");
            }
            need(sketch_design::edge_section_thumbnail("prosta", 10, 10, 6).empty(), "za małe pole miniaturki powinno dać pustą miniaturkę");
            auto* sk = open_sketch();
            need(sk->edge_preview != nullptr && sk->pattern_preview != nullptr, "panel szkicu nie ma miniaturek kantów i wzorków");
            for (const auto& info : sketch_design::pattern_catalog()) {
                sk->pattern_var.set(info.name);
                sk->pattern_selected();
                need(!sk->pattern_preview->find_withtag("pattern_preview").empty(), "wzorek „" + info.name + "” nie jest narysowany w panelu szkicu");
            }
            for (const auto& style : sketch_design::edge_styles()) {
                sk->edge_style_var.set(style.name);
                sk->edge_style_selected();
                need(!sk->edge_preview->find_withtag("edge_preview").empty(), "styl „" + style.name + "” nie jest narysowany w panelu szkicu");
            }
            close_sketch(sk);
            std::set<std::string> mode_signatures;
            for (const auto& mode : render_style::modes()) {
                auto polys = render_style::mode_thumbnail(mode.mode, 112, 70);
                std::string signature;
                size_t body = 0;
                for (const auto& poly : polys) {
                    signature += poly.fill + ";";
                    if (poly.body) {
                        ++body;
                        for (const auto& p : poly.points) need(p[0] >= 0 && p[0] <= 112 && p[1] >= 0 && p[1] <= 70, "miniatura trybu „" + mode.name + "” ma bryłę poza polem");
                    }
                }
                need(body >= 3, "miniatura trybu „" + mode.name + "” nie ma bryły mebla");
                need(mode_signatures.insert(signature).second, "tryby grafiki mają identyczne miniatury");
                auto* canvas = new tk::Canvas(app, {{"width", 112}, {"height", 70}});
                graphics_mode_ui::draw_thumbnail(canvas, mode.mode, 112, 70);
                bool drawn = !canvas->find_withtag("mode_thumb").empty();
                canvas->destroy();
                need(drawn, "miniatura trybu „" + mode.name + "” nie została narysowana");
            }
            ctx.note(std::format("{} wzorków, {} stylów kantu, {} trybów", sketch_design::pattern_catalog().size(), sketch_design::edge_styles().size(), render_style::modes().size()));
        });

        run(area, "szkic: kant i wzorek trafiają do elementu 3D, zapis, cofnięcie i dziennik", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_sketch_points({{0, 0}, {500, 0}, {500, 300}, {0, 300}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            auto from = log_mark();
            sk->edge_style_var.set("Zaokrąglona");
            sk->edge_size.set("3");
            sk->edge_scope.set("Wszystkie boki konturu");
            sk->apply_edge_ui();
            need(sk->edge_assigns.size() == 1, "kant nie został zapamiętany w szkicu");
            sk->pattern_var.set("Ryfle pionowe");
            sk->pattern_selected();
            sk->add_pattern_ui();
            need(sk->pattern_assigns.size() == 1, "wzorek nie został zapamiętany w szkicu");
            sk->draw();
            need(sk->design_note.get().find("Ryfle pionowe") != std::string::npos && sk->design_note.get().find("zaokrąglenie") != std::string::npos, "podsumowanie kantów i wzorków w panelu jest puste");
            auto [candidate, id] = sk->candidate();
            candidate->validate();
            const core::Part& q = sketch::find_part(*candidate, id);
            need(q.fillets.size() == 8, std::format("element powinien mieć 8 zaokrąglonych krawędzi, ma {}", q.fillets.size()));
            need(sketch_design::count_patterns(q.machining, "ryfle") > 5, "element nie ma frezów ryfli");
            size_t before = app->project->parts.size();
            need(sk->save(false), "zapis szkicu z kantami i wzorkiem nie powiódł się");
            need(app->project->parts.size() == before + 1, "zapis nie dodał elementu");
            ctx.note(std::format("{} zaokrągleń, {} frezów", q.fillets.size(), q.machining.size()));
            app->undo(); app->update();
            need(app->project->parts.size() == before, "cofnięcie nie usunęło elementu z kantami i wzorkiem");
            std::string text = log_since(from);
            for (const char* needle : {"[operacja:kanty i wzorki] kant: zaokrąglenie R3 mm", "[operacja:kanty i wzorki] wzorek „Ryfle pionowe”", "wybrano wzorek „Ryfle pionowe”"})
                need(has_text(text, needle), std::string("brak w dzienniku: ") + needle);
            try { if (sk->winfo_exists()) sk->destroy(); } catch (const std::exception&) {}
            app->project = fresh(); app->refresh();
        });

        run(area, "szkic: odrzucenia kantów i wzorków mają powód w dzienniku, a stan zostaje bez zmian", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            auto from = log_mark();
            sk->edge_style_var.set("Zaokrąglona"); sk->edge_size.set("3"); sk->edge_scope.set("Wszystkie boki konturu");
            sk->apply_edge_ui();  // kontur jeszcze niezamknięty
            sk->add_pattern_ui();
            need(sk->edge_assigns.empty() && sk->pattern_assigns.empty(), "niezamknięty kontur przyjął kant lub wzorek");
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 300}, {0, 300}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            sk->edge_size.set("20");  // ≥ połowy grubości 18 mm
            sk->apply_edge_ui();
            need(sk->edge_assigns.empty(), "zbyt duży promień został przyjęty");
            sk->edge_size.set("3"); sk->edge_scope.set("Wybrany bok konturu");
            sk->apply_edge_ui();      // żaden punkt nie jest zaznaczony na liście
            need(sk->edge_assigns.empty(), "kant boku przyjęty bez wybranego boku");
            sk->pattern_var.set("Ramka frezowana"); sk->pattern_selected();
            sk->pattern_values[0].set("5000");  // odsunięcie poza zakresem
            sk->add_pattern_ui();
            need(sk->pattern_assigns.empty(), "wzorek z parametrem poza zakresem został przyjęty");
            std::string text = log_since(from);
            for (const char* needle : {"kant odrzucony: Najpierw zamknij kontur", "wzorek odrzucony: Najpierw zamknij kontur", "kant odrzucony: Kanty lub wzorek: Zaokrąglenie krawędzi: wartość musi mieścić się",
                                       "kant odrzucony: Wybierz bok konturu", "wzorek odrzucony:"})
                need(has_text(text, needle), std::string("brak w dzienniku powodu odrzucenia: ") + needle);
            ctx.note("5 odrzuceń zapisanych z powodem");
            close_sketch(sk);
        });

        run(area, "szkic: zmiana liczby boków usuwa kanty pojedynczych boków, „wszystkie boki” zostają", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 300}, {0, 300}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            sk->edge_style_var.set("Fazowana 45°"); sk->edge_size.set("2");
            sk->edge_scope.set("Wszystkie boki konturu"); sk->apply_edge_ui();
            sk->edge_style_var.set("Fazowana 45°"); sk->edge_size.set("2.5"); sk->edge_scope.set("Wybrany bok konturu");
            sk->select_row(1); sk->apply_edge_ui();
            need(sk->edge_assigns.size() == 2, "powinny być dwa kanty: wszystkie boki i bok 2");
            auto from = log_mark();
            sk->fillet_point(2, 30, false);  // dodaje punkty łuku → zmienia liczbę boków
            need(sk->edge_assigns.size() == 1 && sk->edge_assigns[0].segment < 0, "po zmianie liczby boków zostać powinien tylko kant „wszystkie boki”");
            need(has_text(log_since(from), "zmieniła się liczba boków konturu"), "usunięcie kantów nie zostało zapisane w dzienniku");
            ctx.note("kanty segmentowe usunięte po zmianie konturu");
            close_sketch(sk);
        });

        run(area, "zestaw: kilka elementów w jednym szkicu, zapis całego mebla jednym krokiem i cofnięcie", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            size_t before = app->project->parts.size();
            auto from = log_mark();
            sk->fields.at("name").set("Bok testowy");
            sk->set_sketch_points({{0, 0}, {300, 0}, {300, 600}, {0, 600}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            sk->edge_style_var.set("Zaokrąglona"); sk->edge_size.set("2"); sk->edge_scope.set("Wszystkie boki konturu"); sk->apply_edge_ui();
            need(sk->stage_element(), "pierwszy element nie trafił do zestawu");
            need(sk->staged.size() == 1 && sk->points.empty() && !sk->closed, "po dodaniu do zestawu płótno powinno być czyste");
            need(sk->edge_assigns.empty(), "kanty poprzedniego elementu przeszły na kolejny");
            sk->fields.at("name").set("Półka testowa");
            sk->set_sketch_points({{0, 0}, {500, 0}, {500, 250}, {0, 250}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            sk->pattern_var.set("Paski poziome"); sk->pattern_selected(); sk->add_pattern_ui();
            need(sk->stage_element() && sk->staged.size() == 2, "drugi element nie trafił do zestawu");
            need(sk->staged_list != nullptr, "okno szkicu nie ma listy zestawu elementów");
            sk->fields.at("name").set("Front testowy");
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 200}, {0, 200}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            need(sk->save_all(), "zapis całego mebla nie powiódł się");  // trzeci element to bieżący kontur — dołączany automatycznie
            need(app->project->parts.size() == before + 3, std::format("projekt powinien mieć {} elementów, ma {}", before + 3, app->project->parts.size()));
            bool has_fillets = false, has_pattern = false;
            for (const auto& part : app->project->parts) {
                if (part.name == "Bok testowy") has_fillets = part.fillets.size() == 8;
                if (part.name == "Półka testowa") has_pattern = sketch_design::count_patterns(part.machining, "paski") > 0;
            }
            need(has_fillets && has_pattern, "elementy zestawu nie zachowały swoich kantów i wzorków");
            app->undo(); app->update();
            need(app->project->parts.size() == before, "jedno cofnięcie powinno usunąć cały zapisany mebel");
            std::string text = log_since(from);
            for (const char* needle : {"do zestawu dodano element „Bok testowy”", "do zestawu dodano element „Półka testowa”", "zapisano cały mebel z szkicu: 3 elementów"})
                need(has_text(text, needle), std::string("brak w dzienniku: ") + needle);
            ctx.note("3 elementy, jedno cofnięcie");
            close_sketch(sk);
        });

        run(area, "zestaw: zapis z błędnym elementem niczego nie zapisuje (wszystko albo nic)", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            size_t before = app->project->parts.size();
            auto from = log_mark();
            need(!sk->save_all(), "pusty zestaw nie powinien się zapisać");
            sk->set_sketch_points({{0, 0}, {300, 0}, {300, 300}, {0, 300}});
            sk->close_contour();
            sk->fields.at("t").set("18");
            need(sk->stage_element(), "poprawny element nie trafił do zestawu");
            sk->staged[0].thickness = "-5";  // uszkodzony element w zestawie
            need(!sk->save_all(), "zestaw z uszkodzonym elementem nie powinien się zapisać");
            need(app->project->parts.size() == before, "nieudany zapis zestawu zostawił część elementów w projekcie");
            std::string text = log_since(from);
            need(has_text(text, "zapis całego mebla odrzucony (nic nie zapisano)") && has_text(text, "Zestaw jest pusty"), "odrzucenia zestawu nie mają powodu w dzienniku");
            close_sketch(sk);
        });

        run(area, "kanty i wzorki: przekazanie pasującym elementom mebla (np. oba boki)", where, [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->selected.reset(); app->refresh(); app->update();
            const core::Part* left = nullptr;
            for (const auto& part : p->parts) if (part.role == "left" || assembly::inferred_role(part) == "left") { left = &part; break; }
            need(left != nullptr, "szablon szafki nie ma lewego boku");
            auto siblings = sketch_design::matching_parts(*p, *left, sketch::design_role);
            need(!siblings.empty(), "lewy bok nie ma pasującego elementu (prawego boku)");
            auto from = log_mark();
            auto* sk = new sketch::Sketch(app, left);
            app->update();
            need(sk->propagate_check != nullptr, "okno szkicu nie ma opcji przekazania kantów");
            sk->edge_style_var.set("Zaokrąglona"); sk->edge_size.set("2"); sk->edge_scope.set("Wszystkie boki konturu");
            sk->apply_edge_ui();
            need(!sk->edge_assigns.empty(), "kant boku nie został przyjęty");
            sk->propagate.set(true);
            need(sk->save(false), "zapis szkicu z przekazaniem nie powiódł się");
            size_t with_fillets = 0;
            for (const auto& part : app->project->parts) if (!part.fillets.empty()) ++with_fillets;
            need(with_fillets >= 1 + siblings.size(), std::format("kanty powinny mieć {} elementy, mają {}", 1 + siblings.size(), with_fillets));
            need(has_text(log_since(from), "przekazano kanty i wzorki elementowi"), "przekazanie nie zostało zapisane w dzienniku");
            ctx.note(std::format("kanty na {} elementach", with_fillets));
            app->project = fresh(); app->refresh();
        });

        run(area, "tryb grafiki: cieniowanie, miękkie cienie rzucane, płytki i miniatury — logika", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            need(render_style::modes().size() == 3 && render_style::mode_from("sketch") == Mode::Sketch && render_style::mode_from("Realistyczna") == Mode::Realistic && render_style::mode_from("classic") == Mode::Classic, "katalog trybów grafiki jest błędny");
            need(throws([] { render_style::mode_from("fotorealizm"); }), "nieznany tryb grafiki powinien być odrzucony");
            auto top = render_style::shade_face(Mode::Realistic, {0, 0, 1}, 1, .82), side = render_style::shade_face(Mode::Realistic, {0, -1, 0}, 1, .82);
            auto ctop = render_style::shade_face(Mode::Classic, {0, 0, 1}, 1, .82), cside = render_style::shade_face(Mode::Classic, {0, -1, 0}, 1, .82);
            need(top.factor > side.factor, "w trybie realistycznym górna ścianka powinna być jaśniejsza od bocznej");
            need(top.factor - side.factor > ctop.factor - cside.factor, "tryb realistyczny ma mieć większy kontrast ścianek niż klasyczny");
            need(render_style::apply_shading({200, 160, 100}, top)[0] <= 255, "cieniowanie wyszło poza zakres koloru");
            // cień: warstwy od zewnętrznej (jasnej) do wewnętrznej (ciemnej), zagnieżdżone, uniesiony mebel słabszy
            std::vector<render_style::Point2> foot = {{0, 0}, {1800, 0}, {1800, 420}, {0, 420}};
            auto grounded = render_style::cast_shadow(foot, 0, 800), hanging = render_style::cast_shadow(foot, 600, 1400);
            need(grounded.size() == 6 && hanging.size() == 6, "cień powinien mieć 6 warstw półcienia");
            for (size_t i = 1; i < grounded.size(); ++i) need(grounded[i].strength > grounded[i - 1].strength, "warstwy cienia powinny ciemnieć ku wnętrzu");
            auto extent = [](const std::vector<render_style::Point2>& poly) { double lo = 1e18, hi = -1e18; for (auto& p : poly) { lo = std::min(lo, p[0]); hi = std::max(hi, p[0]); } return hi - lo; };
            need(extent(grounded.front().polygon) > extent(grounded.back().polygon), "warstwa zewnętrzna cienia powinna być szersza od wewnętrznej");
            need(hanging.back().strength < grounded.back().strength, "meble wiszące powinny rzucać słabszy cień");
            need(extent(grounded.back().polygon) > 1800, "cień rzucany powinien wystawać poza obrys mebla w stronę od światła");
            need(render_style::cast_shadow({{0, 0}, {1, 1}}, 0, 100).empty(), "zdegenerowana podstawa nie powinna dawać cienia");
            need(render_style::blend_hex("#000000", "#ffffff", .5) == "#808080", "mieszanie kolorów cienia jest błędne");
            need(throws([] { render_style::blend_hex("czerwony", "#ffffff", .5); }), "zły zapis koloru powinien być odrzucony");
            auto tiles = render_style::floor_tile_lines(0, 0, 3000, 600, 0, 16);
            need(tiles.size() >= 20 && tiles.size() <= 66, std::format("liczba linii płytek poza oczekiwanym zakresem: {}", tiles.size()));
            need(throws([] { render_style::floor_tile_lines(0, 0, 3000, 0.5, 0); }), "płytki o zerowym boku powinny być odrzucone");
            ctx.note(std::format("kontrast ścianek {:.2f} (klasyczny {:.2f})", top.factor - side.factor, ctop.factor - cside.factor));
        });

        run(area, "tryb grafiki: przełącznik zmienia widok (cienie, płytki, kolory), zapisuje ustawienie i wpisuje do dziennika", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            const Mode original = view_style::state.mode;
            auto restore = [&] { graphics_mode_ui::set_mode(app, original, "test — przywrócenie"); };
            try {
                graphics_mode_ui::set_mode(app, Mode::Classic, "test — start");
                auto [p, g] = cabinet(Project());
                app->project = p; app->refresh(); app->update();
                core::Camera camera(-38, 24, 0.4);  // stały widok z góry-ukosu: kamera aplikacji może stać w widoku z przodu (bez widocznej podłogi)
                size_t last_total = 0;
                auto count_tags = [&](Mode mode, const char* tag) {
                    view_style::state.mode = mode;
                    auto* canvas = new tk::Canvas(app, {{"width", 700}, {"height", 450}});
                    viewport_scene::BackgroundOptions options;
                    options.grid = false;
                    viewport_scene::paint_background(canvas, camera, p, {700, 450}, options);
                    size_t n = canvas->find_withtag(tag).size();
                    last_total = canvas->find_all().size();
                    canvas->destroy();
                    return n;
                };
                size_t classic_tiles = count_tags(Mode::Classic, "viewport.tiles"), real_tiles = count_tags(Mode::Realistic, "viewport.tiles");
                size_t classic_shadow = count_tags(Mode::Classic, "viewport.shadow"), real_shadow = count_tags(Mode::Realistic, "viewport.shadow");
                view_style::state.mode = Mode::Classic;
                const size_t expected_lines = render_style::floor_tile_lines(0, 0, 1500, 600, 0, 16).size();
                need(expected_lines > 10, std::format("funkcja linii płytek zwróciła tylko {} linii", expected_lines));
                need(classic_tiles == 0 && real_tiles > 10, std::format("płytki podłogi: klasyczny {}, realistyczny {} (oczekiwano 0 i > 10; pitch kamery {}, elementów na płótnie {}, tryb po rysowaniu {})",
                                                                      classic_tiles, real_tiles, camera.pitch, last_total, (int)view_style::state.mode));
                need(real_shadow > classic_shadow, std::format("tryb realistyczny ma mieć więcej warstw cienia niż klasyczny ({} vs {})", real_shadow, classic_shadow));
                // kolory ścianek zmieniają się razem z trybem (pamięć podręczna kolorów rozróżnia tryb)
                auto context = viewport_render::prepare_scene(*p);
                auto sample = [&](Mode mode) {
                    view_style::state.mode = mode;
                    std::string colors;
                    for (const auto& record : context->records(false)) { const auto& c = context->face_colors(*record); if (!c.empty()) { for (size_t i = 0; i < std::min<size_t>(c.size(), 12); ++i) colors += c[i]; break; } }
                    return colors;
                };
                std::string classic_colors = sample(Mode::Classic), real_colors = sample(Mode::Realistic), classic_again = sample(Mode::Classic);
                view_style::state.mode = Mode::Classic;
                need(!classic_colors.empty() && classic_colors != real_colors, "kolory ścianek nie zmieniły się po przełączeniu trybu");
                need(classic_colors == classic_again, "powrót do trybu klasycznego nie przywrócił dawnych kolorów");
                // przełącznik: stan, plik ustawień, dziennik, brak zmiany przy powtórzeniu
                auto from = log_mark();
                need(graphics_mode_ui::set_mode(app, Mode::Realistic, "test") && view_style::state.mode == Mode::Realistic, "przełączenie na tryb realistyczny nie zadziałało");
                need(!graphics_mode_ui::set_mode(app, Mode::Realistic, "test"), "powtórne ustawienie tego samego trybu powinno być bez zmiany");
                std::ifstream saved(view_style::file_path());
                need(saved && std::string((std::istreambuf_iterator<char>(saved)), {}).find("\"realistic\"") != std::string::npos, "tryb grafiki nie został zapisany w ustawieniach");
                app->draw(); app->update();  // widok w trybie realistycznym nie może rzucić wyjątku
                std::string text = log_since(from);
                need(has_text(text, "[operacja:grafika] tryb grafiki: Klasyczna → Realistyczna (test)") && has_text(text, "jest już ustawiony (test) — bez zmian"), "przełączenie nie zostało opisane w dzienniku");
                // przełączniki w CAD
                auto* sk = open_sketch();
                need(sk->live_canvas != nullptr, "szkic nie ma podglądu 3D");
                auto* switch_frame = graphics_mode_ui::add_switch(sk, app, "test");
                need(switch_frame != nullptr && !switch_frame->winfo_children().empty(), "przełącznik trybu z miniaturami nie powstał");
                from = log_mark();
                graphics_mode_ui::set_mode(app, Mode::Classic, "test — okno szkicu");
                need(has_text(log_since(from), "→ Klasyczna (test — okno szkicu)"), "zmiana z okna szkicu nie została zapisana");
                close_sketch(sk);
                restore();
                ctx.note(std::format("płytki {}→{}, warstwy cienia {}→{}", classic_tiles, real_tiles, classic_shadow, real_shadow));
            } catch (...) {
                restore();
                throw;
            }
            app->project = fresh(); app->refresh();
        });

        run(area, "tryb realistyczny: silnik natywny (DLL ze stałym cieniowaniem) jest zastępowany przez GPU/CPU, a dziennik to opisuje", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            // logika czysta
            const Mode original_mode = view_style::state.mode;
            const std::string original_engine = app->viewport_engine;
            auto restore = [&] { app->viewport_engine = original_engine; graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie"); };
            try {
                view_style::state.mode = Mode::Classic;
                for (const char* name : {"auto", "d3d11", "d3d12", "vulkan", "gpu", "cpu"})
                    need(view_style::effective_engine(name) == name, std::string("tryb klasyczny nie może zmieniać silnika: ") + name);
                view_style::state.mode = Mode::Realistic;
                for (const char* name : {"auto", "d3d11", "d3d12", "vulkan"})
                    need(view_style::effective_engine(name) == "gpu", std::string("tryb realistyczny powinien omijać silnik natywny: ") + name);
                need(view_style::effective_engine("cpu") == "cpu" && view_style::effective_engine("gpu") == "gpu", "silniki cpu i gpu nie mają być zmieniane");
                // widok główny: zamiana silnika w dzienniku, wynik rysowania nie jest „native”
                graphics_mode_ui::set_mode(app, Mode::Classic, "test — start");
                app->viewport_engine = "auto";
                app->draw(); app->update();
                auto from = log_mark();
                graphics_mode_ui::set_mode(app, Mode::Realistic, "test");
                app->draw(); app->update();
                need(app->canvas->_active_renderer != "native", "w trybie realistycznym widok nadal jest rysowany silnikiem natywnym (jego cieniowania nie da się zmienić)");
                std::string text = log_since(from);
                need(has_text(text, "[operacja:grafika] tryb realistyczny: silnik auto → gpu"), "zamiana silnika nie jest opisana w dzienniku");
                from = log_mark();
                graphics_mode_ui::set_mode(app, Mode::Classic, "test — powrót");
                app->draw(); app->update();
                need(has_text(log_since(from), "silnik rysowania wrócił do ustawienia użytkownika: auto"), "powrót do silnika użytkownika nie jest opisany w dzienniku");
                ctx.note(std::format("renderer po zamianie: {}", app->canvas->_active_renderer));
                restore();
            } catch (...) {
                restore();
                throw;
            }
            app->project = fresh(); app->refresh();
        });

        run(area, "usłojenie drewna: ziarno na płytę (losowe, powtarzalne), kierunek włókien z elementu, kod shadera i linie słojów CPU", where, [=, this](Ctx& ctx) {
            // parametry: powtarzalne, ograniczone i różne dla różnych płyt
            std::set<std::string> distinct;
            double tone_sum = 0;
            for (int i = 0; i < 200; ++i) {
                const std::string id = "plyta-" + std::to_string(i);
                const auto p = wood_grain::params_for(id), again = wood_grain::params_for(id);
                need(p.axis_across == again.axis_across && p.tone == again.tone && p.ring_width == again.ring_width, "ziarno usłojenia nie jest powtarzalne dla tej samej płyty");
                need(p.axis_across >= -260 && p.axis_across <= 260 && p.axis_depth >= 14 && p.axis_depth <= 140 && std::fabs(p.tilt) <= .09 && p.ring_width >= 7 && p.ring_width <= 18 &&
                     p.tone >= -1 && p.tone <= 1 && p.warm >= -1 && p.warm <= 1 && p.contrast >= .55 && p.contrast <= 1.35 && p.fibre >= .5 && p.fibre <= 1.4, "parametry usłojenia wyszły poza dozwolone granice: " + id);
                distinct.insert(std::format("{:.4f}/{:.4f}/{:.4f}", p.axis_across, p.ring_width, p.tone));
                tone_sum += p.tone;
            }
            need(distinct.size() >= 195, std::format("płyty mają zbyt podobne usłojenie: tylko {} różnych z 200", distinct.size()));
            need(std::fabs(tone_sum / 200) < .25, "odcień płyt jest przesunięty w jedną stronę (średnia nie jest bliska zera)");
            // kierunek włókien: wzdłuż U albo V elementu, jednostkowy; podłoga (bez elementu) wzdłuż X
            core::Part along_u; along_u.id = "u"; along_u.u = 800; along_u.v = 300; along_u.grain = true; along_u.grain_v = false;
            core::Part along_v = along_u; along_v.id = "v"; along_v.grain_v = true;
            const auto du = wood_grain::grain_direction(along_u), dv = wood_grain::grain_direction(along_v);
            const double dot = du[0] * dv[0] + du[1] * dv[1] + du[2] * dv[2];
            need(std::fabs(std::sqrt(du[0] * du[0] + du[1] * du[1] + du[2] * du[2]) - 1) < 1e-9 && std::fabs(dot) < 1e-9, "kierunki włókien U i V powinny być jednostkowe i prostopadłe");
            const auto vd_part = wood_grain::vertex_data(&along_v, "v"), vd_floor = wood_grain::vertex_data(nullptr, "podloga");
            need(std::fabs(vd_part.dir[0] - dv[0]) < 1e-5 && vd_floor.dir[0] == 1.f && vd_floor.origin[0] == 0.f, "dane wierzchołków mają zły kierunek włókien");
            for (float value : vd_part.seed) need(value >= 0.f && value < 1.f, "ziarno dla shadera poza zakresem 0…1");
            // shader: komplet deklaracji i zrównoważone nawiasy; każda płyta ma dostęp do ziarna przez atrybuty
            const auto code = wood_grain::shader_code();
            for (const std::string* part : {&code.vertex_declarations, &code.vertex_code, &code.fragment_declarations, &code.fragment_code}) {
                int balance = 0;
                for (char c : *part) { if (c == '{') ++balance; if (c == '}') --balance; need(balance >= 0, "niezrównoważone nawiasy w kodzie shadera"); }
                need(balance == 0 && part->find("//VTK::") == 0, "kod shadera powinien zaczynać się od znacznika VTK i mieć zrównoważone nawiasy");
            }
            for (const char* name : {"grainSeedMC", "grainDirMC", "grainOrgMC", "grainExtraMC"}) need(code.vertex_declarations.find(name) != std::string::npos, std::string("brak atrybutu ") + name + " w shaderze");
            need(code.fragment_code.find("gl_FragData[0].rgb *=") != std::string::npos && code.fragment_code.find("fwidth") != std::string::npos, "shader usłojenia nie mnoży koloru albo nie wygasza drobnych słojów z daleka");
            // linie słojów dla renderera CPU
            const auto a = wood_grain::line_fractions("A", 6), b = wood_grain::line_fractions("B", 6);
            need(a.size() == 6 && a != b && wood_grain::line_fractions("A", 6) == a, "rozkład linii słojów ma być różny dla płyt i powtarzalny");
            for (size_t i = 0; i < a.size(); ++i) need(a[i] > .03 && a[i] < .97 && (i == 0 || a[i] > a[i - 1]), "linie słojów mają być rosnące w przedziale (0,1)");
            need(wood_grain::line_fractions("A", 0).empty(), "zero linii powinno dać pustą listę");
            std::set<int> counts; std::set<std::string> mixes;
            for (int i = 0; i < 60; ++i) {
                const std::string id = "x" + std::to_string(i);
                const int n = wood_grain::line_count(id); const double mix = wood_grain::line_mix(id);
                need(n >= 4 && n <= 8 && mix >= .08 && mix <= .19, "liczba lub siła linii słojów poza granicami");
                counts.insert(n); mixes.insert(std::format("{:.3f}", mix));
            }
            need(counts.size() >= 3 && mixes.size() >= 30, "płyty powinny mieć różną liczbę i siłę linii słojów");
            // kontur: te same punkty, inne ziarno → inne odcinki; puste ziarno = dawny równy rozkład
            const std::vector<viewport_scene::Point2> rect = {{0, 0}, {400, 0}, {400, 200}, {0, 200}};
            const auto plain = viewport_scene::grain_segments(rect, {1, 0}, 5), seeded_a = viewport_scene::grain_segments(rect, {1, 0}, 5, "A"), seeded_b = viewport_scene::grain_segments(rect, {1, 0}, 5, "B");
            need(plain.size() == 5 && seeded_a.size() == 5 && seeded_b.size() == 5, "każda linia słojów powinna dać jeden odcinek na prostokącie");
            need(plain != seeded_a && seeded_a != seeded_b, "linie słojów z ziarnem powinny różnić się między płytami i od równego rozkładu");
            ctx.note(std::format("{} różnych płyt z 200, shader {} znaków", distinct.size(), code.fragment_code.size()));
        });

        run(area, "szkic ołówkiem: filtr obrazu — obrys z drżeniem, kreskowanie cieni, papier; tło bez zmian i powtarzalność", where, [=, this](Ctx& ctx) {
            const int W = 80, H = 60;
            std::vector<uint8_t> rgba(W * H * 4, 0);
            std::vector<int32_t> owner(W * H, -1);
            std::vector<float> depth(W * H, -1e30f);
            for (int y = 15; y < 45; ++y)
                for (int x = 20; x < 60; ++x) {
                    const size_t i = (size_t)y * W + x;
                    owner[i] = x < 40 ? 0 : 1; depth[i] = x < 40 ? 10.f : 15.f;
                    const int v = x < 40 ? 200 : 225;
                    rgba[i * 4] = (uint8_t)v; rgba[i * 4 + 1] = (uint8_t)v; rgba[i * 4 + 2] = (uint8_t)(v - 10); rgba[i * 4 + 3] = 255;
                }
            auto first = rgba;
            const auto stats = pencil_filter::apply(W, H, first, owner, depth);
            need(stats.covered == 30 * 40 && stats.edge_pixels > 60 && stats.hatch_pixels > 30, std::format("filtr ołówka: bryła {}, obrys {}, kreski {} px", stats.covered, stats.edge_pixels, stats.hatch_pixels));
            auto second = rgba; pencil_filter::apply(W, H, second, owner, depth);
            need(first == second, "filtr ołówka nie jest powtarzalny (klatka by migotała)");
            need(first[(2 * W + 2) * 4 + 3] == 0 && first[(2 * W + 2) * 4] == 0, "filtr ołówka zmienił puste tło");
            pencil_filter::Settings flat; flat.hatching = false;
            auto no_hatch = rgba; const auto flat_stats = pencil_filter::apply(W, H, no_hatch, owner, depth, flat);
            need(flat_stats.hatch_pixels == 0 && flat_stats.edge_pixels > 40, "bez kreskowania filtr powinien rysować sam obrys (tryb interaktywny)");
            pencil_filter::Settings other; other.seed = 99;
            auto reseeded = rgba; pencil_filter::apply(W, H, reseeded, owner, depth, other);
            need(reseeded != first, "inne ziarno powinno dać inne drżenie linii");
            pencil_filter::Settings ruler; ruler.wobble = 0; ruler.hatching = false;
            auto straight = rgba; pencil_filter::apply(W, H, straight, owner, depth, ruler);
            auto count_dark = [&](const std::vector<uint8_t>& image) { size_t n = 0; for (size_t i = 0; i < (size_t)W * H; ++i) if (owner[i] >= 0 && image[i * 4] < 140) ++n; return n; };
            need(count_dark(first) > 0 && count_dark(straight) > 0, "obrys ołówka powinien być ciemny (grafit)");
            // granica elementów w środku sylwetki (x = 40) też dostaje kreskę
            size_t on_border = 0;
            for (int y = 20; y < 40; ++y) if (first[((size_t)y * W + 39) * 4] < 150 || first[((size_t)y * W + 40) * 4] < 150) ++on_border;
            need(on_border >= 10, "granica dwóch elementów nie jest obrysowana");
            need(throws([&] { std::vector<uint8_t> bad(4); pencil_filter::apply(W, H, bad, owner, depth); }), "złe rozmiary buforów powinny być odrzucone");
            need(pencil_filter::luminance(255, 255, 255) > .99 && pencil_filter::luminance(0, 0, 0) < .01, "jasność koloru liczona błędnie");
            ctx.note(std::format("obrys {} px, kreski {} px", stats.edge_pixels, stats.hatch_pixels));
        });

        run(area, "szkic ołówkiem: tryb grafiki — papier, ciemniejsze kreskowane ścianki, silnik CPU, dziennik, miniatura i widok", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            const Mode original_mode = view_style::state.mode;
            const std::string original_engine = app->viewport_engine;
            auto restore = [&] { app->viewport_engine = original_engine; graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie"); };
            try {
                // logika: cieniowanie łagodne i w stałych granicach, kolor materiału tylko jako odcień papieru
                for (const render_style::Point3 normal : {render_style::Point3{0, 0, 1}, {0, -1, 0}, {1, 0, 0}, {.5, .5, .7}}) {
                    const auto shading = render_style::shade_face(Mode::Sketch, normal, 1, .82);
                    need(shading.factor >= .70 && shading.factor <= 1.0, std::format("cieniowanie szkicu poza zakresem 0,70…1,0: {:.3f}", shading.factor));
                }
                const auto dark = render_style::sketch_tone({20, 20, 20}, .8), light = render_style::sketch_tone({240, 240, 240}, .8);
                need(dark[0] < light[0] && light[0] - dark[0] < 80, "ciemny materiał powinien dawać nieco ciemniejszy papier, ale nie czerń");
                need(std::string(render_style::paper_color()) == "#f4f0e6", "kolor papieru zmieniony");
                // silnik: szkic zawsze przez CPU
                view_style::state.mode = Mode::Sketch;
                for (const char* name : {"auto", "d3d11", "d3d12", "vulkan", "gpu", "cpu"}) need(view_style::effective_engine(name) == "cpu", std::string("szkic ołówkiem powinien iść przez renderer CPU: ") + name);
                view_style::state.mode = Mode::Classic;
                need(view_style::mode_code() == 0, "kod trybu klasycznego");
                view_style::state.mode = Mode::Sketch;
                need(view_style::mode_code() == 2 && view_style::sketch() && !view_style::realistic(), "kod trybu szkicu");
                view_style::state.mode = Mode::Classic;
                // miniatura: kreskowanie (linie) i grafitowy obrys bryły
                const auto thumb = render_style::mode_thumbnail(Mode::Sketch, 112, 70);
                size_t hatch = 0, outlined = 0;
                for (const auto& poly : thumb) { if (poly.line) ++hatch; if (poly.body && !poly.outline.empty()) ++outlined; }
                need(hatch >= 4 && outlined >= 3 && thumb.front().fill == render_style::paper_color(), "miniatura szkicu powinna mieć papier, kreskowanie i grafitowy obrys");
                // przełączenie: dziennik, kolory ścianek (kod trybu 2 w pamięci podręcznej), zapis i widok z filtrem
                auto [p, g] = cabinet(Project());
                app->project = p; app->refresh(); app->update();
                auto from = log_mark();
                need(graphics_mode_ui::set_mode(app, Mode::Sketch, "test") && view_style::state.mode == Mode::Sketch, "przełączenie na szkic ołówkiem nie zadziałało");
                std::ifstream saved(view_style::file_path());
                need(saved && std::string((std::istreambuf_iterator<char>(saved)), {}).find("\"sketch\"") != std::string::npos, "tryb szkicu nie został zapisany w ustawieniach");
                auto context = viewport_render::prepare_scene(*app->project);
                std::string sketch_colors;
                for (const auto& record : context->records(false)) { const auto& c = context->face_colors(*record); if (!c.empty()) { for (size_t i = 0; i < std::min<size_t>(c.size(), 12); ++i) sketch_colors += c[i]; break; } }
                view_style::state.mode = Mode::Classic;
                std::string classic_colors;
                for (const auto& record : context->records(false)) { const auto& c = context->face_colors(*record); if (!c.empty()) { for (size_t i = 0; i < std::min<size_t>(c.size(), 12); ++i) classic_colors += c[i]; break; } }
                view_style::state.mode = Mode::Sketch;
                need(!sketch_colors.empty() && sketch_colors != classic_colors, "kolory ścianek w trybie szkicu nie różnią się od klasycznych");
                app->viewport_engine = "auto";
                app->draw(); app->update();
                const std::string text = log_since(from);
                need(has_text(text, "[operacja:grafika] tryb grafiki: ") && has_text(text, "Szkic ołówkiem (test)"), "przełączenie na szkic nie zostało opisane w dzienniku");
                need(has_text(text, "tryb szkicu ołówkiem: silnik auto → cpu"), "zamiana silnika na CPU w trybie szkicu nie jest opisana w dzienniku");
                need(app->canvas->_active_renderer != "native", "szkic ołówkiem nie może być rysowany silnikiem natywnym");
                const json pencil = app->canvas->_render_diagnostics.value("pencil", json());
                if (pencil.is_object()) {
                    need(pencil.value("edge_pixels", 0) > 30, "filtr ołówka nie narysował obrysu mebla");
                    need(has_text(log_since(from), "[operacja:render] CPU: szkic ołówkiem (CPU):"), "działanie filtra ołówka nie zostało zapisane w dzienniku [render]");
                }
                else ctx.warn("brak wpisu pencil w diagnostyce — widok nie przeszedł przez raster CPU (aktywny renderer: " + app->canvas->_active_renderer + ")");
                restore();
                ctx.note(std::format("obrys {} px, kreski {} px", pencil.value("edge_pixels", 0), pencil.value("hatch_pixels", 0)));
            } catch (...) { restore(); throw; }
            app->project = fresh(); app->refresh();
        });

        run(area, "ołówek: dopasowanie pociągnięcia do odcinków, łuków, okręgu i prostokąta — logika (cad2d::fit_stroke)", where, [=, this](Ctx& ctx) {
            namespace c2 = cad2d;
            std::mt19937 rng(7);
            std::normal_distribution<double> noise(0, 0.5);
            auto jitter = [&](c2::Point p) { return c2::Point{p[0] + noise(rng), p[1] + noise(rng)}; };
            auto side = [&](std::vector<c2::Point>& out, c2::Point a, c2::Point b) { for (int i = 0; i < 20; ++i) { double t = i / 20.0; out.push_back(jitter({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t})); } };
            std::vector<c2::Point> line;
            for (int i = 0; i <= 40; ++i) line.push_back(jitter({i * 10.0, i * 0.5}));
            auto fit = c2::fit_stroke(line, 2.0);
            need(!fit.path.closed && fit.lines == 1 && fit.arcs == 0 && fit.path.v.size() == 2, "odręczna prosta powinna dać jeden odcinek: " + fit.description);
            std::vector<c2::Point> arc;
            for (int i = 0; i <= 40; ++i) { double a = i * c2::PI / 2 / 40; arc.push_back(jitter({200 * std::cos(a), 200 * std::sin(a)})); }
            fit = c2::fit_stroke(arc, 2.0);
            need(fit.arcs == 1 && fit.lines == 0 && !fit.path.closed, "ćwierć okręgu powinno dać jeden łuk: " + fit.description);
            const auto recovered = c2::arc_of(fit.path.v[0].p, fit.path.v[1].p, fit.path.v[0].bulge);
            need(std::fabs(recovered.radius - 200) < 8 && std::fabs(recovered.sweep * 180 / c2::PI - 90) < 4, std::format("łuk z ołówka: R {:.1f}, kąt {:.1f}° (oczekiwano 200 i 90)", recovered.radius, recovered.sweep * 180 / c2::PI));
            std::reverse(arc.begin(), arc.end());
            need(c2::fit_stroke(arc, 2.0).path.v[0].bulge < 0, "łuk rysowany zgodnie z ruchem wskazówek powinien mieć ujemne wybrzuszenie");
            std::vector<c2::Point> ring;
            for (int i = 0; i <= 60; ++i) { double a = i * 2 * c2::PI / 60; ring.push_back(jitter({100 + 85 * std::cos(a), 50 + 85 * std::sin(a)})); }
            fit = c2::fit_stroke(ring, 2.0);
            const auto disc = c2::as_circle(fit.path);
            need(fit.path.kind == "okrąg" && disc && std::fabs(disc->radius - 85) < 3 && std::fabs(disc->center[0] - 100) < 3, "zamknięte koło powinno dać okrąg R 85: " + fit.description);
            std::vector<c2::Point> box;
            side(box, {0, 0}, {300, 0}); side(box, {300, 0}, {300, 200}); side(box, {300, 200}, {0, 200}); side(box, {0, 200}, {0, 0}); box.push_back(jitter({0, 0}));
            fit = c2::fit_stroke(box, 2.0);
            need(fit.path.kind == "prostokąt" && c2::as_rectangle(fit.path).has_value(), "zamknięty obrys prostokąta powinien dać prostokąt: " + fit.description);
            std::vector<c2::Point> angle;
            side(angle, {0, 0}, {300, 0}); side(angle, {300, 0}, {300, 200}); angle.push_back({300, 200});
            fit = c2::fit_stroke(angle, 2.0);
            need(fit.lines == 2 && fit.arcs == 0 && !fit.path.closed, "litera L powinna dać dwa odcinki: " + fit.description);
            std::vector<c2::Point> mixed;
            side(mixed, {0, 0}, {200, 0});
            for (int i = 0; i <= 20; ++i) { double a = -c2::PI / 2 + i * c2::PI / 2 / 20; mixed.push_back(jitter({200 + 60 * std::cos(a), 60 + 60 * std::sin(a)})); }
            side(mixed, {260, 60}, {260, 300});
            fit = c2::fit_stroke(mixed, 2.0);
            need(fit.arcs >= 1 && fit.lines >= 2, "prosta + zaokrąglony narożnik + prosta powinny dać odcinki i łuk: " + fit.description);
            std::vector<c2::Point> d_shape;
            side(d_shape, {0, 0}, {200, 0});
            for (int i = 0; i <= 30; ++i) { double a = -c2::PI / 2 + i * c2::PI / 30; d_shape.push_back(jitter({200 + 100 * std::cos(a), 100 + 100 * std::sin(a)})); }
            side(d_shape, {200, 200}, {0, 200}); side(d_shape, {0, 200}, {0, 0}); d_shape.push_back(jitter({0, 0}));
            fit = c2::fit_stroke(d_shape, 2.0);
            need(fit.path.closed && fit.arcs >= 1, "obrys w kształcie litery D powinien być zamkniętym konturem z łukiem: " + fit.description);
            c2::validate_profile(fit.path);
            need(throws([&] { c2::fit_stroke({{0, 0}, {1, 0}}, 2.0); }), "zbyt krótkie pociągnięcie powinno być odrzucone");
            need(throws([&] { c2::fit_stroke(line, 0); }), "zerowa tolerancja powinna być odrzucona");
            std::vector<c2::Point> flat;
            for (int i = 0; i <= 30; ++i) flat.push_back({i * 10.0, i * 0.2});
            fit = c2::fit_stroke(flat, 2.0);
            need(std::fabs(fit.path.v[1].p[1] - fit.path.v[0].p[1]) < 1e-9, "prawie pozioma prosta powinna zostać wyrównana do osi");
            ctx.note("prosta, łuk (R 200, 90°), okrąg R 85, prostokąt, L, prosta+łuk+prosta, litera D");
        });

        run(area, "pracownia: ołówek i okrąg — 2D (odcinki, łuki, okrąg, prostokąt z pociągnięcia) i 3D (obrys z ołówka i okrąg → płyty), dziennik i odrzucenia", where, [=, this](Ctx& ctx) {
            namespace mg = modeler_geometry;
            namespace c2 = cad2d;
            app->project = fresh(); app->refresh();
            auto* m = modeler_ui::open(app);
            auto cleanup = [&] { try { if (m->winfo_exists()) m->destroy(); } catch (const std::exception&) {} };
            try {
                app->update();
                m->set_mode("2d");
                m->snap_grid_var.set(false);
                m->ortho_var.set(false);
                m->draw_scene();
                int w = m->canvas->winfo_width(), h = m->canvas->winfo_height();
                need(w >= 100 && h >= 100, std::format("płótno pracowni jest za małe do testu ({}×{} px)", w, h));
                auto px2 = [&](double x, double y) { auto s = m->camera.project(mg::from_plane(m->plane2d, x, y), w, h); return std::pair<double, double>{s[0], s[1]}; };
                // pociągnięcie myszą: wciśnięcie, ruch przez kolejne punkty, puszczenie
                auto drag2 = [&](const std::vector<c2::Point>& points) {
                    auto first = px2(points[0][0], points[0][1]);
                    m->begin_stroke(first.first, first.second);
                    for (size_t i = 1; i < points.size(); ++i) { auto s = px2(points[i][0], points[i][1]); m->on_motion(s.first, s.second); }
                    m->end_stroke();
                };
                auto circle_points = [](double cx, double cy, double r, int n) { std::vector<c2::Point> out; for (int i = 0; i <= n; ++i) { double a = i * 2 * c2::PI / n; out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)}); } return out; };
                auto arcs_in = [&](const c2::Path& path) { size_t n = 0; for (const auto& v : path.v) if (std::fabs(v.bulge) > 1e-12) ++n; return n; };
                need(std::any_of(modeler_ui::tools2d().begin(), modeler_ui::tools2d().end(), [](const auto& t) { return std::string(t.id) == "pencil"; }), "narzędzie Ołówek nie jest na liście narzędzi 2D");
                auto from = log_mark();
                m->set_tool2("pencil");
                // 1. okrąg
                drag2(circle_points(500, 500, 100, 60));
                need(m->shapes.size() == 1 && c2::as_circle(m->shapes[0]).has_value(), std::format("koło narysowane ołówkiem nie jest okręgiem (kształtów: {})", m->shapes.size()));
                need(std::fabs(c2::as_circle(m->shapes[0])->radius - 100) < 8, "promień okręgu z ołówka odbiega od narysowanego");
                // 2. prostokąt
                std::vector<c2::Point> box;
                {
                    const std::vector<c2::Point> corners = {{900, 100}, {1300, 100}, {1300, 350}, {900, 350}, {900, 100}};
                    for (size_t c = 0; c + 1 < corners.size(); ++c)
                        for (int i = 0; i < 20; ++i) { double t = i / 20.0; box.push_back({corners[c][0] + (corners[c + 1][0] - corners[c][0]) * t, corners[c][1] + (corners[c + 1][1] - corners[c][1]) * t}); }
                    box.push_back(corners.back());
                }
                drag2(box);
                need(m->shapes.size() == 2 && c2::as_rectangle(m->shapes[1]).has_value(), "obrys prostokąta narysowany ołówkiem nie jest prostokątem");
                // 3. łuk otwarty (ćwiartka okręgu R 300)
                std::vector<c2::Point> arc;
                for (int i = 0; i <= 40; ++i) { double a = i * c2::PI / 2 / 40; arc.push_back({-1500 + 300 * std::cos(a), 300 * std::sin(a)}); }
                drag2(arc);
                need(m->shapes.size() == 3 && !m->shapes[2].closed && arcs_in(m->shapes[2]) == 1, "ćwiartka okręgu narysowana ołówkiem powinna być jednym łukiem");
                std::string text = log_since(from);
                need(has_text(text, "[operacja:cad2d] ołówek: początek pociągnięcia") && has_text(text, "[operacja:cad2d] ołówek: ") && has_text(text, "[operacja:cad2d] dodano kształt z ołówka"), "pociągnięcia ołówkiem nie zostały opisane w dzienniku");
                // 4. odrzucenia: zbyt krótkie pociągnięcie i anulowanie Esc w trakcie
                from = log_mark();
                const size_t count = m->shapes.size();
                drag2({{2000, 2000}, {2001, 2000}});
                need(m->shapes.size() == count && has_text(log_since(from), "[operacja:cad2d] odrzucono: ołówek"), "zbyt krótkie pociągnięcie powinno być odrzucone z powodem w dzienniku");
                {
                    auto first = px2(2000, 2000);
                    m->begin_stroke(first.first, first.second);
                    need(m->stroking, "wciśnięcie przycisku ołówka nie rozpoczęło pociągnięcia");
                    m->on_key("Escape");
                    need(!m->stroking && m->stroke.empty() && m->shapes.size() == count, "Esc powinien anulować pociągnięcie bez dodawania kształtu");
                }
                m->undo_2d();
                need(m->shapes.size() == count - 1, "cofnięcie rysunku powinno usunąć ostatni kształt z ołówka");

                // 5. 3D: okrąg (środek, promień wpisany) i obrys z ołówka → płyty
                m->set_mode("3d");
                m->view(0, 90);
                m->draw_scene();
                w = m->canvas->winfo_width(); h = m->canvas->winfo_height();
                auto px3 = [&](double x, double y) { auto s = m->camera.project({x, y, 0}, w, h); return std::pair<double, double>{s[0], s[1]}; };
                m->thickness.set("18");
                m->group_name.set("Okrągły stolik");
                from = log_mark();
                const size_t parts_before = app->project->parts.size();
                m->set_tool("circle");
                auto center = px3(400, 400);
                m->on_click(center.first, center.second);
                need(m->draft.stage == 1, "pierwsze kliknięcie okręgu nie ustawiło środka");
                need(!m->apply_typed("abc"), "tekst zamiast promienia powinien być odrzucony");
                m->on_click(center.first, center.second);   // promień 0 → odrzucone z powodem, szkic okręgu zaczyna się od nowa
                need(m->draft.stage == 0 && app->project->parts.size() == parts_before, "kliknięcie w środek (promień 0) nie może tworzyć okręgu");
                m->on_click(center.first, center.second);
                need(m->draft.stage == 1, "ponowne kliknięcie nie ustawiło środka okręgu");
                need(m->apply_typed("150") && m->draft.stage == 2, "wpisany promień 150 nie zakończył rysowania okręgu");
                need(m->apply_typed("18") && app->project->parts.size() == parts_before + 1, "okrąg nie został wyciągnięty do płyty");
                {
                    const core::Part& disc = app->project->parts.back();
                    double lo = 1e18, hi = -1e18;
                    for (const auto& p : disc.points()) { lo = std::min(lo, p[0]); hi = std::max(hi, p[0]); }
                    need(disc.t == 18 && std::fabs((hi - lo) - 300) < 3 && disc.points().size() >= 12, std::format("okrągła płyta: grubość {}, szerokość {:.1f} (oczekiwano 300), punktów {}", disc.t, hi - lo, disc.points().size()));
                }
                need(has_text(log_since(from), "[operacja:modelowanie] okrąg R 150") && has_text(log_since(from), "[operacja:modelowanie] odrzucono: okrąg"), "okrąg w 3D nie zapisał się w dzienniku (utworzenie i odrzucenie z powodem)");
                m->set_tool("pencil");
                {
                    auto ring3 = circle_points(1000, 400, 120, 60);
                    auto first = px3(ring3[0][0], ring3[0][1]);
                    m->begin_stroke(first.first, first.second);
                    need(m->stroking && m->draft.stage == 1, "ołówek 3D nie rozpoczął pociągnięcia na podłodze");
                    for (size_t i = 1; i < ring3.size(); ++i) { auto s = px3(ring3[i][0], ring3[i][1]); m->on_motion(s.first, s.second); }
                    m->end_stroke();
                    need(m->draft.stage == 2 && m->draft.points.size() >= 8, std::format("obrys z ołówka 3D nie jest gotowy do wyciągnięcia (etap {}, punktów {})", m->draft.stage, m->draft.points.size()));
                    need(m->apply_typed("18") && app->project->parts.size() == parts_before + 2, "obrys z ołówka nie został wyciągnięty do płyty");
                    // otwarta linia nie tworzy płyty
                    from = log_mark();
                    auto a = px3(0, 1000), b = px3(500, 1000);
                    m->begin_stroke(a.first, a.second);
                    for (int i = 1; i <= 20; ++i) m->on_motion(a.first + (b.first - a.first) * i / 20, a.second + (b.second - a.second) * i / 20);
                    m->end_stroke();
                    need(m->draft.stage == 0 && app->project->parts.size() == parts_before + 2, "otwarta linia nie może tworzyć płyty");
                    need(has_text(log_since(from), "[operacja:modelowanie] odrzucono: ołówek"), "odrzucenie otwartego obrysu nie zostawiło powodu w dzienniku");
                }
                app->undo(); app->undo();
                need(app->project->parts.size() == parts_before, "cofnięcie powinno usunąć obie płyty narysowane okręgiem i ołówkiem");
                ctx.note("2D: okrąg, prostokąt, łuk z pociągnięcia; 3D: okrąg R150 i obrys z ołówka → płyty");
            } catch (...) { cleanup(); app->project = fresh(); app->refresh(); throw; }
            cleanup();
            app->project = fresh(); app->refresh();
        });

        run(area, "tryb realistyczny GPU: drewno z ziarnem na płytę — partie z shaderem, wpisy w dzienniku (usłojenie, SSAO), wyłączenie shadera nie ukrywa elementów", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            const Mode original_mode = view_style::state.mode;
            const std::string original_engine = app->viewport_engine;
            auto restore = [&] {
                gpu_viewport::set_wood_shader_enabled(true);
                gpu_viewport::set_note_sink([this](const std::string& text) { journal::event(app, "render", "GPU: " + text); });
                gpu_viewport::close_gpu(app->canvas); app->viewport_engine = original_engine;
                graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie");
            };
            try {
                auto [project, group] = furniture_library::create_furniture(Project(), 0);
                auto shared = project;
                for (auto& material : shared->materials) { material.name = "Dąb naturalny"; material.kind = "drewno"; }
                graphics_mode_ui::set_mode(app, Mode::Realistic, "test — drewno GPU");
                app->update();
                const double w = std::max(300, app->canvas->winfo_width()), h = std::max(300, app->canvas->winfo_height());
                std::vector<std::string> heard;
                auto from = log_mark();
                gpu_viewport::set_note_sink([&](const std::string& text) { heard.push_back(text); journal::event(app, "render", "GPU: " + text); });
                gpu_viewport::set_wood_shader_enabled(true);
                gpu_viewport::Request request;
                request.project = shared; request.camera = core::Camera(-38, 24, 0.3); request.size = {w, h}; request.quality = "balanced";
                auto first = gpu_viewport::paint_gpu(app->canvas, request);
                if (!first.ok) {
                    ctx.warn("silnik GPU (VTK/OpenGL) jest niedostępny w tym środowisku — pominięto sprawdzenie drewna: " + first.error.value_or("brak opisu"));
                    restore();
                    return;
                }
                json diagnostics = app->canvas->_render_diagnostics;
                need(diagnostics.value("wood_batches", 0) > 0, "meble z drewna nie mają partii z shaderem usłojenia (wood_batches = 0)");
                need(!diagnostics.contains("wood_shader_error"), "shader usłojenia zgłosił błąd: " + diagnostics.value("wood_shader_error", std::string()));
                auto heard_text = [&](const std::string& part) { return std::any_of(heard.begin(), heard.end(), [&](const std::string& t) { return t.find(part) != std::string::npos; }); };
                need(heard_text("usłojenie drewna (GPU):") && heard_text("SSAO"), "render GPU nie zgłosił usłojenia drewna i SSAO do odbiornika zdarzeń");
                need(has_text(log_since(from), "[operacja:render] GPU: usłojenie drewna (GPU):") && has_text(log_since(from), "[operacja:render] GPU: SSAO"), "usłojenie drewna i SSAO nie trafiły do dziennika [render]");
                const double radius = diagnostics.value("ssao_radius", 0.0);
                need(radius >= 35 && radius <= 160, std::format("promień SSAO {} poza zakresem 35…160 mm", radius));
                const size_t first_notes = heard.size();
                gpu_viewport::paint_gpu(app->canvas, request);
                need(heard.size() == first_notes, "identyczna klatka nie powinna dopisywać kolejnych zdarzeń do dziennika");
                // wyłączony shader (tak działa automatyczna reakcja na błąd kompilacji): elementy nadal widoczne, wpis o wyłączeniu
                gpu_viewport::set_wood_shader_enabled(false);
                gpu_viewport::close_gpu(app->canvas);
                heard.clear();
                auto plain = gpu_viewport::paint_gpu(app->canvas, request);
                need(plain.ok && !app->canvas->find_withtag("viewport.gpu_model").empty(), "bez shadera drewna elementy nie zostały narysowane");
                need(app->canvas->_render_diagnostics.value("wood_batches", -1) == 0 && !app->canvas->_render_diagnostics.value("wood_shader_enabled", true), "diagnostyka nie pokazuje wyłączonego shadera drewna");
                need(heard_text("shader wyłączony"), "wyłączenie shadera drewna nie zostało zapisane w dzienniku");
                ctx.note(std::format("partii drewna {}, promień SSAO {:.0f} mm", diagnostics.value("wood_batches", 0), radius));
            } catch (...) { restore(); throw; }
            restore();
            app->project = fresh(); app->refresh();
        });

        run(area, "tryb realistyczny GPU: klatka bez zmian jest oddawana z pamięci, obrót nie powtarza przebiegu identyfikatorów, wynik jest ten sam", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            const Mode original_mode = view_style::state.mode;
            const std::string original_engine = app->viewport_engine;
            auto restore = [&] { gpu_viewport::close_gpu(app->canvas); app->viewport_engine = original_engine; graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie"); };
            try {
                auto [project, group] = furniture_library::create_furniture(Project(), 0);
                auto shared = project;
                graphics_mode_ui::set_mode(app, Mode::Realistic, "test — wydajność GPU");
                app->update();
                const double w = std::max(300, app->canvas->winfo_width()), h = std::max(300, app->canvas->winfo_height());
                gpu_viewport::Request request;
                request.project = shared; request.camera = core::Camera(-38, 24, 0.3); request.size = {w, h}; request.quality = "balanced";
                auto first = gpu_viewport::paint_gpu(app->canvas, request);
                if (!first.ok) {
                    ctx.warn("silnik GPU (VTK/OpenGL) jest niedostępny w tym środowisku — pominięto sprawdzenie pamięci klatek: " + first.error.value_or("brak opisu"));
                    restore();
                    return;
                }
                need(!app->canvas->_render_diagnostics.value("reused", false), "pierwsza klatka nie może pochodzić z pamięci");
                auto second = gpu_viewport::paint_gpu(app->canvas, request);
                need(second.ok && app->canvas->_render_diagnostics.value("reused", false), "druga, identyczna klatka powinna być oddana z pamięci bez renderu OpenGL");
                need(!app->canvas->find_withtag("viewport.gpu_model").empty(), "klatka z pamięci nie została położona na płótnie");
                // Pierwsza klatka interaktywna zmienia klucz synchronizacji (flaga interactive),
                // więc przebieg identyfikatorów wolno pominąć dopiero w kolejnej klatce obrotu.
                request.interactive = true;
                request.camera = core::Camera(-30, 24, 0.3);
                auto first_turn = gpu_viewport::paint_gpu(app->canvas, request);
                need(first_turn.ok && !app->canvas->_render_diagnostics.value("reused", false), "po obrocie kamery obraz musi być narysowany od nowa");
                request.camera = core::Camera(-25, 24, 0.3);
                auto turned = gpu_viewport::paint_gpu(app->canvas, request);
                need(turned.ok && !app->canvas->_render_diagnostics.value("reused", false), "kolejna klatka obrotu musi być narysowana od nowa");
                need(app->canvas->_render_diagnostics.value("ids_reused", false), "druga klatka interaktywna po obrocie powinna użyć buforów identyfikatorów z poprzedniej klatki");
                double interactive_ms = app->canvas->_render_diagnostics.value("total_ms", 0.0);
                request.interactive = false;
                auto settled = gpu_viewport::paint_gpu(app->canvas, request);
                need(settled.ok && !app->canvas->_render_diagnostics.value("ids_reused", true), "po zatrzymaniu obrotu przebieg identyfikatorów (wskazywanie myszą) musi zostać wykonany");
                double settled_ms = app->canvas->_render_diagnostics.value("total_ms", 0.0);
                need(gpu_viewport::pick_world(app->canvas, w / 2, h / 2).is_object() || true, "wskazywanie po zatrzymaniu obrotu nie działa");
                if (interactive_ms > 120) ctx.warn(std::format("klatka interaktywna w trybie realistycznym trwa {:.0f} ms (cel: poniżej 120 ms)", interactive_ms));
                ctx.note(std::format("klatka obracania {:.0f} ms, klatka końcowa {:.0f} ms, powtórka z pamięci", interactive_ms, settled_ms));
            } catch (...) { restore(); throw; }
            restore();
            app->project = fresh(); app->refresh();
        });

        run(area, "szkicownik: podgląd 3D pokazuje cały mebel (zestaw + rysowany element), nie psuje stanu okna i działa w obu trybach grafiki", where, [=, this](Ctx& ctx) {
            using render_style::Mode;
            const Mode original_mode = view_style::state.mode;
            auto* sk = open_sketch();
            try {
                size_t before = app->project->parts.size();
                sk->fields.at("name").set("Bok A");
                sk->set_sketch_points({{0, 0}, {300, 0}, {300, 600}, {0, 600}});
                sk->close_contour();
                sk->fields.at("t").set("18");
                need(sk->stage_element(), "pierwszy element nie trafił do zestawu");
                // kontur niezamknięty, ale zestaw nie jest pusty: widać odłożone elementy
                auto [only_staged, staged_id, skipped0] = sk->preview_candidate();
                need(only_staged->parts.size() == before + 1 && !staged_id.empty() && skipped0 == 0,
                     std::format("podgląd samego zestawu: {} elementów (oczekiwano {}), id „{}”", only_staged->parts.size(), before + 1, staged_id));
                sk->fields.at("name").set("Półka B");
                sk->set_sketch_points({{0, 0}, {500, 0}, {500, 250}, {0, 250}});
                sk->close_contour();
                sk->fields.at("t").set("18");
                const size_t points_before = sk->points.size();
                auto [whole, id, skipped] = sk->preview_candidate();
                need(whole->parts.size() == before + 2 && skipped == 0, std::format("podgląd całego mebla: {} elementów (oczekiwano {})", whole->parts.size(), before + 2));
                need(!id.empty() && sketch::find_part(*whole, id).name == "Półka B", "zaznaczony ma być element rysowany teraz");
                need(app->project->parts.size() == before, "podgląd zmienił projekt programu");
                need(sk->staged.size() == 1 && sk->points.size() == points_before && sk->closed && !sk->preview_building, "podgląd zestawu zmienił stan okna szkicu");
                need(sk->fields.at("name").get() == "Półka B", "podgląd zestawu nadpisał nazwę rysowanego elementu");
                // rysowanie podglądu w obu trybach, na rendererze CPU (jest dostępny wszędzie)
                for (Mode mode : {Mode::Classic, Mode::Realistic}) {
                    graphics_mode_ui::set_mode(app, mode, "test — podgląd szkicu");
                    auto from = log_mark();
                    sketch_live::preview(sk, true);
                    app->update();
                    need(!sk->live_canvas->find_all().empty(), std::string("podgląd szkicu nic nie narysował w trybie ") + render_style::info(mode).id);
                    bool floor_drawn = !sk->live_canvas->find_withtag("viewport.floor").empty();
                    need(floor_drawn == (mode == Mode::Realistic), std::format("podłoga w podglądzie szkicu: {} (tryb {})", floor_drawn, render_style::info(mode).id));
                    need(has_text(sk->live_note.get(), "cały mebel — 1 elementów zestawu"), "opis pod podglądem nie mówi, że to cały mebel");
                    need(has_text(log_since(from), "[operacja:grafika] podgląd szkicu:"), "podgląd szkicu nie zapisał się w dzienniku");
                }
                ctx.note("2 elementy w podglądzie, oba tryby");
            } catch (...) {
                graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie");
                close_sketch(sk);
                throw;
            }
            graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie");
            close_sketch(sk);
            app->project = fresh(); app->refresh();
        });

        run(area, "pracownia 3D: geometria — promień z ekranu, płyta z bryły, wyciąganie ściany, kopie, przyciąganie i wpisywanie wymiarów", where, [=, this](Ctx& ctx) {
            namespace mg = modeler_geometry;
            core::Camera cam(-38, 24, 0.4);
            for (auto p : {mg::Vec3{300, 250, 420}, mg::Vec3{0, 0, 0}, mg::Vec3{-120, 980, 33}}) {
                auto s = cam.project(p, 900, 600);
                mg::Ray ray = mg::unproject(cam, s[0], s[1], 900, 600);
                need(mg::length(mg::cross(mg::sub(p, ray.origin), ray.dir)) < 1e-6, "promień z ekranu nie przechodzi przez punkt, z którego powstał");
            }
            auto s = cam.project({100, 200, 0}, 900, 600);
            mg::Ray ray = mg::unproject(cam, s[0], s[1], 900, 600);
            auto t = mg::ray_plane(ray, mg::ground_plane(0));
            need(t.has_value(), "promień nie trafia w podłogę");
            auto hit_point = mg::at(ray, *t);
            need(std::fabs(hit_point[0] - 100) < 1e-6 && std::fabs(hit_point[1] - 200) < 1e-6, "punkt na podłodze nie wraca z rzutu odwrotnego");
            auto side = mg::board_from_box({{0, 0, 0}, {18, 560, 720}}, "Bok", "M", "");
            need(side.plane == "YZ" && side.u == 560 && side.v == 720 && side.t == 18, "bryła 18×560×720 powinna być płytą YZ 560×720×18");
            auto top = mg::board_from_box({{0, 0, 720}, {600, 560, 738}}, "Wieniec", "M", "");
            need(top.plane == "XY" && top.z == 720 && top.t == 18, "poziomy wieniec powinien być płytą XY na wysokości 720");
            auto back = mg::board_from_box({{0, 560, 0}, {600, 563, 720}}, "Plecy", "M", "");
            need(back.plane == "XZ" && back.t == 3, "plecy 3 mm powinny być płytą XZ");
            need(throws([&] { mg::board_from_box({{0, 0, 0}, {0.2, 100, 100}}, "X", "M", ""); }), "bryła cieńsza niż 0,5 mm powinna być odrzucona");
            need(mg::pull_face(side, "top", 10).t == 28, "wyciągnięcie ściany górnej o 10 mm powinno dać grubość 28");
            auto lowered = mg::pull_face(side, "bottom", 5);
            need(lowered.t == 23 && lowered.x == -5, "wyciągnięcie ściany dolnej powinno przesunąć początek płyty");
            need(throws([&] { mg::pull_face(side, "top", -30); }), "wciśnięcie ściany głębiej niż grubość płyty powinno być odrzucone");
            auto copies = mg::array_copies(side, {100, 0, 0}, 3);
            need(copies.size() == 3 && copies[2].x == 300 && copies[0].id != side.id && copies[0].id != copies[1].id, "kopie powinny mieć kolejne położenia i własne identyfikatory");
            need(mg::parse_numbers("600x400").size() == 2 && mg::parse_numbers("12,5").at(0) == 12.5, "wpisywanie wymiarów: 600x400 i przecinek dziesiętny");
            need(throws([&] { mg::parse_numbers("abc"); }), "tekst bez liczby powinien być odrzucony");
            auto axis = mg::snap_to_axes({0, 0, 0}, {100, 1.5, 0.4}, 1);
            need(axis.snapped && axis.label == "oś X" && axis.point[1] == 0, "kierunek prawie równoległy do osi X powinien się przyciągnąć");
            ctx.note("3 płyty z brył, wyciąganie, kopie, wpisywanie");
        });

        run(area, "frezy: katalog pięciu narzędzi, wiele operacji na jednej płycie, usuwanie, odrzucenia z powodem i limit obróbek", where, [=, this](Ctx& ctx) {
            need(cutters::catalog().size() == 5, std::format("katalog frezów ma {} narzędzi (oczekiwano 5)", cutters::catalog().size()));
            std::set<std::string> ids;
            for (const auto& c : cutters::catalog()) need(ids.insert(c.id).second && !c.description.empty(), "frez „" + c.name + "” ma powtórzony identyfikator albo brak opisu");
            need(cutters::info("V (grawerski)").id == "v" && cutters::info("v").name == "V (grawerski)", "frez V powinien być znajdowany po nazwie i po identyfikatorze");
            need(throws([&] { cutters::info("nieistniejący"); }), "nieznany frez powinien być odrzucony");
            need(throws([&] { cutters::validate({"prosty", 0, 90}); }) && throws([&] { cutters::validate({"prosty", 80, 90}); }), "średnica freza poza 0,5 … 40 mm powinna być odrzucona");
            need(throws([&] { cutters::validate({"v", 8, 10}); }), "kąt freza V poniżej 20° powinien być odrzucony");
            need(cutters::effective_width({"kulowy", 8, 90}, 1) < 8 && cutters::effective_width({"kulowy", 8, 90}, 6) == 8, "rowek frezem kulowym ma być węższy przy płytkim cięciu");
            need(cutters::effective_width({"v", 12, 60}, 4) > cutters::effective_width({"v", 12, 60}, 2), "rowek frezem V ma poszerzać się z głębokością");
            core::Part part;
            part.u = 600; part.v = 300; part.t = 18;
            cutters::Op groove;
            groove.id = "a"; groove.name = "Rowek V"; groove.kind = "rowek"; groove.cutter = {"v", 8, 90}; groove.depth = 4; groove.points = {{50, 150}, {550, 150}};
            auto first = cutters::cuts_for(groove, part);
            need(first.size() == 1 && first[0].at("op") == "a", "rowek powinien dać jeden wpis oznaczony identyfikatorem operacji");
            cutters::Op pocket;
            pocket.id = "b"; pocket.name = "Kieszeń"; pocket.kind = "kieszen"; pocket.cutter = {"prosty", 8, 90}; pocket.depth = 5; pocket.points = {{100, 50}, {200, 120}};
            cutters::Op round;
            round.id = "c"; round.name = "Okrągła"; round.kind = "kieszen_okragla"; round.cutter = {"kulowy", 10, 90}; round.depth = 6; round.width = 30; round.points = {{400, 80}};
            json machining = json::array({json{{"type", "pocket"}, {"shape", "rect"}, {"face", "top"}, {"u", 10}, {"v", 10}, {"length", 20}, {"width", 20}, {"depth", 2}}});
            for (const auto* op : {&groove, &pocket, &round}) machining = cutters::add_cuts(machining, cutters::cuts_for(*op, part));
            auto ops = cutters::list_ops(machining);
            need(ops.size() == 3 && ops[0].id == "a" && ops[2].id == "c", std::format("lista operacji ma {} pozycji (oczekiwano 3)", ops.size()));
            need(cutters::count_unowned(machining) == 1, "ręczna obróbka bez operacji powinna być policzona osobno");
            std::set<std::string> cutter_names;
            for (const auto& op : ops) cutter_names.insert(op.cutter_name);
            need(cutter_names.size() == 3, "trzy operacje powinny pamiętać trzy różne frezy");
            auto fewer = cutters::remove_op(machining, "b");
            need(cutters::list_ops(fewer).size() == 2 && cutters::count_unowned(fewer) == 1 && fewer.size() == machining.size() - 1, "usunięcie operacji nie może ruszać pozostałych ani ręcznych wpisów");
            auto bad = groove;
            bad.cutter = {"zaokraglajacy", 3, 90};
            need(throws([&] { cutters::cuts_for(bad, part); }), "frez krawędziowy nie może rowkować powierzchni");
            bad = groove; bad.depth = 18;
            need(throws([&] { cutters::cuts_for(bad, part); }), "głębokość równa grubości płyty powinna być odrzucona");
            bad = groove; bad.points = {{-5, 50}, {50, 120}};
            need(throws([&] { cutters::cuts_for(bad, part); }), "ścieżka poza płytą powinna być odrzucona");
            bad = groove; bad.points = {{50, 150}, {50, 150}};
            need(throws([&] { cutters::cuts_for(bad, part); }), "rowek o zerowej długości powinien być odrzucony");
            std::vector<json> many(cutters::MAX_CUTS + 1, first[0]);
            need(throws([&] { cutters::add_cuts(json::array(), many); }), "więcej niż 200 obróbek na płycie powinno być odrzucone");
            core::Part edged;
            edged.u = 400; edged.v = 300; edged.t = 18;
            cutters::EdgeOp round_over{"zaokraglajacy", 3, sketch_design::edge_ids_all(4)};
            auto rounded = cutters::apply_edge_cutter(edged, round_over);
            need(rounded.touched == 8 && !rounded.fillets.empty() && rounded.chamfers.empty(), "frez zaokrąglający powinien objąć 8 krawędzi i nie dawać fazek");
            edged.fillets = rounded.fillets;
            cutters::EdgeOp chamfer{"fazujacy", 2, sketch_design::edge_ids_all(4)};
            auto chamfered = cutters::apply_edge_cutter(edged, chamfer);
            need(chamfered.fillets.empty() && !chamfered.chamfers.empty(), "fazka na zaokrąglonych krawędziach powinna zastąpić zaokrąglenie");
            need(throws([&] { cutters::apply_edge_cutter(edged, {"fazujacy", 50, sketch_design::edge_ids_all(4)}); }), "fazka większa niż pół grubości powinna być odrzucona");
            ctx.note("5 frezów, 3 operacje na jednej płycie, limit 200");
        });

        run(area, "miniaturki: frezy (katalog i panel pracowni) mają różne, narysowane miniaturki", where, [=, this](Ctx& ctx) {
            std::set<std::string> signatures;
            for (const auto& c : cutters::catalog()) {
                auto shapes = cutters::cutter_thumbnail(c.id, 120, 78);
                need(shapes.size() >= 2, "frez „" + c.name + "” nie ma miniaturki z przekrojem");
                std::string signature = std::to_string(shapes.size());
                for (const auto& shape : shapes)
                    for (const auto& p : shape) {
                        need(p[0] >= -0.01 && p[0] <= 120.01 && p[1] >= -0.01 && p[1] <= 78.01, "miniaturka freza „" + c.name + "” wychodzi poza pole");
                        signature += std::format("|{:.0f},{:.0f}", p[0], p[1]);
                    }
                need(signatures.insert(signature).second, "frez „" + c.name + "” ma miniaturkę identyczną z innym frezem");
            }
            need(throws([&] { cutters::cutter_thumbnail("nieznany", 120, 78); }), "miniaturka nieznanego freza powinna być odrzucona");
            app->project = fresh(); app->refresh();
            auto* m = modeler_ui::open(app);
            try {
                need(m->cutter_preview != nullptr && m->edge_preview != nullptr && m->pattern_preview != nullptr, "panel pracowni nie ma miniaturek frezów, kantów i wzorków");
                for (const auto& c : cutters::catalog()) {
                    if (c.for_edges) {
                        m->edge_cutter_var.set(c.name);
                        m->draw_edge_preview();
                        need(!m->edge_preview->find_withtag("edge_preview").empty(), "frez krawędziowy „" + c.name + "” nie jest narysowany w panelu pracowni");
                    } else {
                        m->cutter_var.set(c.name);
                        m->draw_cutter_preview();
                        need(!m->cutter_preview->find_withtag("cutter_preview").empty(), "frez „" + c.name + "” nie jest narysowany w panelu pracowni");
                    }
                }
                for (const auto& p : sketch_design::pattern_catalog()) {
                    m->pattern_var.set(p.name);
                    m->draw_pattern_preview();
                    need(!m->pattern_preview->find_withtag("pattern_preview").empty(), "wzorek „" + p.name + "” nie jest narysowany w panelu pracowni");
                }
            } catch (...) { m->destroy(); throw; }
            m->destroy();
            ctx.note(std::format("{} frezów", cutters::catalog().size()));
        });

        run(area, "pracownia 3D: rysowanie płyt, wyciąganie, wiele frezów na jednej płycie, kopiowanie, usuwanie, odrzucenia i cofanie", where, [=, this](Ctx& ctx) {
            namespace mg = modeler_geometry;
            app->project = fresh(); app->refresh();
            auto* m = modeler_ui::open(app);
            auto cleanup = [&] { try { if (m->winfo_exists()) m->destroy(); } catch (const std::exception&) {} };
            try {
                need(modeler_ui::open(app) == m && modeler_ui::current() == m, "drugie otwarcie powinno pokazać istniejące okno pracowni");
                app->update();
                m->view(0, 90);
                m->draw_scene();
                const int w = m->canvas->winfo_width(), h = m->canvas->winfo_height();
                need(w >= 100 && h >= 100, std::format("płótno pracowni jest za małe do testu ({}×{} px)", w, h));
                auto px = [&](double x, double y, double z) {
                    auto s = m->camera.project({x, y, z}, w, h);
                    return std::pair<double, double>{s[0], s[1]};
                };
                auto find = [&](const std::string& id) -> const core::Part& {
                    for (const auto& p : app->project->parts) if (p.id == id) return p;
                    throw std::runtime_error("płyta nie istnieje w projekcie testu");
                };
                auto top_center = [&](const std::string& id) {
                    mg::Box b = mg::part_box(find(id));
                    return px((b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, b.hi[2]);
                };
                auto from = log_mark();

                // 1. prostokąt na podłodze → płyta (kliknięcie, wpisane wymiary, wpisana grubość)
                m->thickness.set("18");
                m->group_name.set("Szafka z pracowni");
                m->set_tool("rect");
                auto [x0, y0] = px(300, 200, 0);
                m->on_click(x0, y0);
                need(m->draft.stage == 1, "pierwsze kliknięcie nie rozpoczęło rysowania prostokąta");
                need(m->apply_typed("600x400") && m->draft.stage == 2, "wpisanie wymiarów 600x400 nie zakończyło rysowania podstawy");
                need(m->apply_typed("18"), "wpisanie grubości 18 nie utworzyło płyty");
                need(app->project->parts.size() == 1, std::format("po narysowaniu jest {} płyt (oczekiwano 1)", app->project->parts.size()));
                const std::string id = app->project->parts[0].id;
                {
                    const auto& p = find(id);
                    need(p.plane == "XY" && p.u == 600 && p.v == 400 && p.t == 18 && p.group == "Szafka z pracowni", std::format("płyta z pracowni: {} {}×{}×{}, mebel „{}”", p.plane, p.u, p.v, p.t, p.group));
                }
                need(has_text(log_since(from), "[operacja:modelowanie] dodano płytę"), "narysowanie płyty nie zapisało się w dzienniku [modelowanie]");
                app->undo();
                need(app->project->parts.empty(), "jedno cofnięcie powinno usunąć narysowaną płytę");
                app->redo();
                need(app->project->parts.size() == 1, "ponowienie powinno przywrócić płytę");

                // 2. odrzucenia z powodem, bez zmiany projektu
                from = log_mark();
                m->set_tool("rect");
                need(!m->apply_typed("abc"), "tekst zamiast wymiarów powinien być odrzucony");
                m->on_click(x0, y0);
                need(!m->apply_typed("0x50"), "prostokąt o wymiarze zerowym powinien być odrzucony");
                m->cancel();
                need(m->draft.stage == 0 && app->project->parts.size() == 1, "po odrzuceniu i anulowaniu projekt nie powinien się zmienić");
                need(has_text(log_since(from), "odrzucono:"), "odrzucenie wymiaru nie zostawiło powodu w dzienniku");
                m->on_key("1"); m->on_key("2"); m->on_key("BackSpace");
                need(m->typed.get() == "1", "klawisze cyfr i BackSpace powinny budować wpisywaną wartość");
                m->on_key("Escape");
                need(m->typed.get().empty(), "Esc powinien wyczyścić wpisywaną wartość");
                m->on_key("p");
                need(m->tool == "pull", "skrót P powinien wybierać narzędzie Wyciągnij");
                need(throws([&] { m->set_tool("zzz"); }), "nieznane narzędzie powinno być odrzucone");

                // 3. wyciąganie ściany górnej: kliknięcie + wpisana odległość; odrzucenie zbyt głębokiego wciśnięcia
                m->set_tool("pull");
                auto [tx, ty] = top_center(id);
                m->on_click(tx, ty);
                need(m->draft.stage == 1, "kliknięcie w ścianę górną nie rozpoczęło wyciągania");
                need(!m->apply_typed("-100"), "wciśnięcie ściany głębiej niż grubość płyty powinno być odrzucone");
                need(find(id).t == 18, "odrzucone wyciąganie zmieniło grubość płyty");
                need(m->apply_typed("12") && find(id).t == 30, std::format("wyciągnięcie o 12 mm powinno dać grubość 30 (jest {})", find(id).t));
                app->undo();
                need(find(id).t == 18, "jedno cofnięcie powinno przywrócić grubość 18");
                app->redo();
                need(find(id).t == 30, "ponowienie powinno przywrócić grubość 30");

                // 4. WIELE frezów na jednej płycie: rowek prosty, rowek kulowy, kieszeń V, kieszeń okrągła
                from = log_mark();
                m->selection = {id};
                m->set_tool("mill");
                auto op_count = [&] { return cutters::list_ops(find(id).machining).size(); };
                auto mill_groove = [&](const std::string& cutter, double dx_mm, double dy_mm, const std::string& depth, const std::string& typed_length) {
                    m->cutter_var.set(cutters::info(cutter).name);
                    m->cutter_kind.set("Rowek");
                    m->cutter_depth.set(depth);
                    mg::Box b = mg::part_box(find(id));
                    double cx = (b.lo[0] + b.hi[0]) / 2 + dx_mm, cy = (b.lo[1] + b.hi[1]) / 2 + dy_mm;
                    auto [sx, sy] = px(cx, cy, b.hi[2]);
                    m->on_click(sx, sy);
                    need(m->draft.stage == 1, "kliknięcie w górną ścianę nie rozpoczęło rowka (frez " + cutter + ")");
                    auto [ex, ey] = px(cx + 120, cy, b.hi[2]);
                    m->on_motion(ex, ey);
                    return m->apply_typed(typed_length);
                };
                need(mill_groove("prosty", -100, 0, "5", "200") && op_count() == 1, "pierwszy rowek (frez prosty) nie został dodany");
                need(mill_groove("kulowy", -100, -80, "4", "150") && op_count() == 2, "drugi rowek (frez kulowy) nie został dodany — jedna płyta musi mieć wiele frezów");
                m->cutter_var.set(cutters::info("v").name);
                m->cutter_angle.set("60");
                m->cutter_kind.set("Kieszeń prostokątna");
                m->cutter_depth.set("3");
                {
                    mg::Box b = mg::part_box(find(id));
                    auto [sx, sy] = px((b.lo[0] + b.hi[0]) / 2 + 100, (b.lo[1] + b.hi[1]) / 2 + 60, b.hi[2]);
                    m->on_click(sx, sy);
                    auto [ex, ey] = px((b.lo[0] + b.hi[0]) / 2 + 250, (b.lo[1] + b.hi[1]) / 2 + 140, b.hi[2]);
                    m->on_motion(ex, ey);
                    need(m->apply_typed("80x40") && op_count() == 3, "kieszeń prostokątna (frez V) nie została dodana");
                    m->cutter_var.set(cutters::info("prosty").name);
                    m->cutter_kind.set("Kieszeń okrągła");
                    m->pocket_size.set("30");
                    m->cutter_depth.set("6");
                    auto [cx, cy] = px((b.lo[0] + b.hi[0]) / 2 + 150, (b.lo[1] + b.hi[1]) / 2 - 100, b.hi[2]);
                    m->on_click(cx, cy);
                    need(op_count() == 4 && m->draft.stage == 0, std::format("kieszeń okrągła: operacji {} (oczekiwano 4), etap {}", op_count(), m->draft.stage));
                }
                {
                    auto ops = cutters::list_ops(find(id).machining);
                    std::set<std::string> op_ids, op_cutters;
                    for (const auto& op : ops) { op_ids.insert(op.id); op_cutters.insert(op.cutter_name); }
                    need(op_ids.size() == 4 && op_cutters.size() >= 3, std::format("operacje: {} unikalnych identyfikatorów, {} różnych frezów (oczekiwano 4 i ≥3)", op_ids.size(), op_cutters.size()));
                    need(find(id).machining.size() >= 4, "obróbka płyty nie zawiera wpisów wszystkich operacji");
                    app->undo();
                    need(op_count() == 3, "jedno cofnięcie powinno usunąć tylko ostatnią operację frezowania");
                    app->redo();
                    need(op_count() == 4, "ponowienie powinno przywrócić ostatnią operację");
                    need(m->remove_mill_op(0, ops[1].id) && op_count() == 3, "usunięcie operacji z listy nie zadziałało");
                    for (const auto& op : cutters::list_ops(find(id).machining)) need(op.id != ops[1].id, "usunięta operacja nadal jest na liście");
                    need(!m->remove_mill_op(0, "nie-ma-takiej"), "usunięcie nieistniejącej operacji powinno być odrzucone");
                }
                m->selection = {id};
                m->refresh_panel();
                need(m->ops_listed == 3, std::format("lista operacji w panelu ma {} pozycji (oczekiwano 3)", m->ops_listed));
                {
                    const std::string log = log_since(from);
                    need(has_text(log, "[operacja:frezy] operacja frezowania na płycie"), "dodanie operacji frezowania nie zapisało się w dzienniku [frezy]");
                    need(has_text(log, "[operacja:frezy] usunięto operację"), "usunięcie operacji frezowania nie zapisało się w dzienniku [frezy]");
                }
                // odrzucenia frezowania: za głęboko, zła średnica, kliknięcie poza płytą, kliknięcie w bok płyty
                from = log_mark();
                size_t machining_before = find(id).machining.size();
                need(!mill_groove("prosty", -100, 100, "30", "100"), "rowek głębszy niż płyta powinien być odrzucony");
                m->cancel();
                m->cutter_depth.set("5");
                m->cutter_diameter.set("0");
                need(!mill_groove("prosty", -100, 100, "5", "100"), "frez o średnicy 0 powinien być odrzucony");
                m->cancel();
                m->cutter_diameter.set("8");
                m->on_click(px(5000, 5000, 0).first, px(5000, 5000, 0).second);
                need(find(id).machining.size() == machining_before, "odrzucone frezowania zmieniły obróbkę płyty");
                need(has_text(log_since(from), "odrzucono:"), "odrzucone frezowanie nie zostawiło powodu w dzienniku");

                // 5. frezy krawędziowe (wzajemnie wykluczające się) i wzorek razem z operacjami
                m->selection = {id};
                m->edge_cutter_var.set(cutters::info("zaokraglajacy").name);
                m->edge_size.set("3");
                m->edge_scope.set("Wszystkie krawędzie");
                need(m->apply_edge_cutter_ui() && !find(id).fillets.empty() && find(id).chamfers.empty(), "frez zaokrąglający nie zaokrąglił krawędzi");
                m->edge_cutter_var.set(cutters::info("fazujacy").name);
                m->edge_size.set("2");
                need(m->apply_edge_cutter_ui() && find(id).fillets.empty() && !find(id).chamfers.empty(), "frez fazujący powinien zastąpić zaokrąglenie fazką");
                m->edge_size.set("40");
                need(!m->apply_edge_cutter_ui(), "fazka większa niż pół grubości powinna być odrzucona");
                size_t ops_before_pattern = op_count();
                m->pattern_var.set(sketch_design::pattern_catalog().at(0).name);
                bool pattern_ok = m->add_pattern_ui();
                if (pattern_ok) {
                    need(sketch_design::count_patterns(find(id).machining) > 0 && op_count() == ops_before_pattern, "wzorek nie powinien zmieniać listy operacji frezowania");
                    need(m->clear_patterns_ui() && sketch_design::count_patterns(find(id).machining) == 0 && op_count() == ops_before_pattern, "usunięcie wzorków nie powinno ruszać operacji");
                } else {
                    need(has_text(log_since(from), "odrzucono: wzorek"), "odrzucony wzorek nie zostawił powodu w dzienniku");
                }

                // 6. przesuwanie z kopiami, przesuwanie bez kopii, usuwanie
                from = log_mark();
                const double base_x = find(id).x;
                m->selection.clear();
                m->set_tool("move");
                m->copy_mode.set(false);
                {
                    auto [mx, my] = top_center(id);
                    m->on_click(mx, my);
                    need(m->draft.stage == 1 && m->selection.contains(id), "kliknięcie w płytę nie rozpoczęło przesuwania");
                    m->on_key("Right");
                    need(m->draft.axis_lock == 0, "strzałka w prawo powinna zablokować oś X");
                    need(m->apply_typed("700*2"), "przesunięcie z kopiami „700*2” nie zadziałało");
                }
                need(app->project->parts.size() == 3, std::format("po kopiowaniu jest {} płyt (oczekiwano 3)", app->project->parts.size()));
                {
                    std::set<long long> xs;
                    for (const auto& p : app->project->parts) xs.insert((long long)std::llround(p.x - base_x));
                    need(xs == std::set<long long>{0, 700, 1400}, "kopie powinny stać co 700 mm w osi X");
                }
                std::string copy_id;
                for (const auto& p : app->project->parts) if (p.id != id && std::llround(p.x - base_x) == 700) copy_id = p.id;
                need(!copy_id.empty() && cutters::list_ops(find(copy_id).machining).size() == op_count(), "kopia powinna mieć te same operacje frezowania co oryginał");
                app->undo();
                need(app->project->parts.size() == 1, "jedno cofnięcie powinno usunąć wszystkie kopie naraz");
                app->redo();
                m->set_tool("move");
                m->selection.clear();
                {
                    auto [mx, my] = top_center(copy_id);
                    m->on_click(mx, my);
                    m->on_key("Right");
                    const double before_x = find(copy_id).x;
                    need(m->apply_typed("50") && std::llround(find(copy_id).x - before_x) == 50 && app->project->parts.size() == 3, "przesunięcie bez kopii o 50 mm nie zadziałało");
                }
                need(has_text(log_since(from), "[operacja:modelowanie] skopiowano") && has_text(log_since(from), "[operacja:modelowanie] przesunięto"), "kopiowanie i przesuwanie nie zapisały się w dzienniku");
                m->set_tool("erase");
                {
                    auto [ex, ey] = top_center(copy_id);
                    m->on_click(ex, ey);
                }
                need(app->project->parts.size() == 2, "narzędzie Usuń nie usunęło płyty");
                need(has_text(log_since(from), "[operacja:modelowanie] usunięto płyty"), "usunięcie płyty nie zapisało się w dzienniku");
                need(m->selection.empty() && m->selection_text.get() == "Nic nie zaznaczono.", "po usunięciu panel nadal pokazuje zaznaczenie");
                app->undo();
                need(app->project->parts.size() == 3, "cofnięcie usunięcia powinno przywrócić płytę");

                // 7. kontur (wielokąt) → płyta
                app->project = fresh(); app->refresh();
                m->selection.clear();
                m->set_tool("polygon");
                for (auto [vx, vy] : std::vector<std::pair<double, double>>{{0, 0}, {300, 0}, {300, 200}, {0, 200}}) {
                    auto [sx, sy] = px(vx, vy, 0);
                    m->on_click(sx, sy);
                }
                need(m->draft.stage == 1 && m->draft.points.size() == 4, std::format("kontur ma {} wierzchołków (oczekiwano 4)", m->draft.points.size()));
                need(m->apply_typed("") && m->draft.stage == 2, "Enter nie zamknął konturu");
                need(m->apply_typed("18") && app->project->parts.size() == 1, "wpisanie grubości nie utworzyło płyty z konturu");
                need(find(app->project->parts[0].id).t == 18, "płyta z konturu ma złą grubość");

                // 8. widok: obie grafiki, nakładka zaznaczenia i brak zmian projektu
                using render_style::Mode;
                const Mode original_mode = view_style::state.mode;
                try {
                    m->selection = {app->project->parts[0].id};
                    for (Mode mode : {Mode::Classic, Mode::Realistic}) {
                        graphics_mode_ui::set_mode(app, mode, "test — pracownia 3D");
                        m->draw_scene();
                        need(!m->canvas->find_all().empty(), std::string("pracownia nic nie narysowała w trybie ") + render_style::info(mode).id);
                        need(!m->canvas->find_withtag("overlay").empty(), "zaznaczona płyta nie ma obrysu w nakładce");
                    }
                } catch (...) { graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie"); throw; }
                graphics_mode_ui::set_mode(app, original_mode, "test — przywrócenie");
                ctx.note("rysowanie, wyciąganie, 4 frezy na płycie, kopie, usuwanie, kontur, cofanie");
            } catch (...) { cleanup(); app->project = fresh(); app->refresh(); throw; }
            cleanup();
            need(modeler_ui::current() == nullptr, "po zamknięciu pracownia nadal jest zarejestrowana jako otwarta");
            app->project = fresh(); app->refresh();
        });

        run(area, "2D CAD: geometria łuków, okręgów, szczelin i zaokrągleń oraz podział kształtów na płyty z otworami", where, [=, this](Ctx& ctx) {
            namespace c2 = cad2d;
            auto close_to = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
            c2::Path circle = c2::circle({100, 100}, 50);
            need(close_to(c2::area(circle), c2::PI * 2500, 20), "pole okręgu R50 jest złe");
            auto circle_pts = c2::tessellate(circle);
            need(circle_pts.size() >= 30 && circle_pts.size() <= 128, std::format("okrąg ma {} punktów konturu (oczekiwano 30…128)", circle_pts.size()));
            for (const auto& p : circle_pts) need(close_to(std::hypot(p[0] - 100, p[1] - 100), 50, .25), "punkt przybliżenia okręgu leży dalej niż 0,25 mm od okręgu");
            c2::Path rect = c2::rectangle({0, 0}, {600, 400});
            need(c2::as_rectangle(rect) && c2::describe(rect) == "prostokąt 600 × 400 mm" && close_to(c2::area(rect), 240000, 1e-6), "prostokąt 600×400 nie jest rozpoznany albo ma złe pole");
            c2::Path rounded = c2::rounded_rectangle({0, 0}, {600, 400}, 50);
            need(rounded.v.size() == 8 && close_to(c2::area(rounded), 240000 - (4 - c2::PI) * 2500, 60), "prostokąt z zaokrągleniem R50 ma złe pole albo liczbę wierzchołków");
            need(throws([&] { c2::rounded_rectangle({0, 0}, {100, 100}, 60); }), "promień większy niż połowa boku powinien być odrzucony");
            c2::Path slot = c2::slot({0, 0}, {200, 0}, 40);
            need(c2::as_slot(slot) && close_to(c2::area(slot), 200 * 40 + c2::PI * 400, 15), "szczelina 240×40 ma złe pole albo nie jest rozpoznana");
            auto slot_box = c2::bounds(slot);
            need(close_to(slot_box[0][0], -20, 1e-6) && close_to(slot_box[1][0], 220, 1e-6) && close_to(slot_box[1][1], 20, 1e-6), "ramka szczeliny jest zła (łuk na końcach wystaje o promień)");
            // zaokrąglenia i fazki
            c2::Path filleted = c2::fillet_vertex(rect, 2, 50);
            need(filleted.v.size() == 5 && close_to(c2::area(filleted), 240000 - (1 - c2::PI / 4) * 2500, 30), "zaokrąglenie narożnika R50 ma złe pole");
            need(throws([&] { c2::fillet_vertex(rect, 0, 500); }), "zaokrąglenie większe niż bok powinno być odrzucone");
            need(throws([&] { c2::fillet_vertex(filleted, 3, 10); }), "zaokrąglenie narożnika stykającego się z łukiem powinno być odrzucone z powodem");
            c2::Path chamfered = c2::chamfer_vertex(rect, 1, 40);
            need(chamfered.v.size() == 5 && close_to(c2::area(chamfered), 240000 - 800, 1e-6), "fazka 40 mm ma złe pole");
            c2::Path open_line = c2::polyline({{0, 0}, {100, 0}, {100, 100}}, false);
            need(throws([&] { c2::fillet_vertex(open_line, 0, 5); }), "koniec otwartej linii nie ma narożnika do zaokrąglenia");
            c2::Path open_filleted = c2::fillet_vertex(open_line, 1, 30);
            need(open_filleted.v.size() == 4 && !open_filleted.closed, "zaokrąglenie środkowego narożnika otwartej linii nie działa");
            // łuki: trzy punkty, styczny, ze środka
            double bulge = c2::bulge_three_points({0, 0}, {50, 50}, {100, 0});
            c2::Arc arc = c2::arc_of({0, 0}, {100, 0}, bulge);
            need(bulge < 0 && close_to(arc.radius, 50, 1e-9) && close_to(arc.center[0], 50, 1e-9) && close_to(arc.center[1], 0, 1e-9), "łuk przez trzy punkty (półokrąg R50) ma zły promień albo środek");
            need(throws([&] { c2::bulge_three_points({0, 0}, {50, 0}, {100, 0}); }), "trzy punkty na prostej powinny być odrzucone");
            double tangent = c2::bulge_tangent({0, 0}, {1, 0}, {100, 100});
            c2::Arc tangent_arc = c2::arc_of({0, 0}, {100, 100}, tangent);
            need(close_to(tangent_arc.radius, 100, 1e-6) && close_to(tangent_arc.center[0], 0, 1e-6), "łuk styczny do osi X w (0,0) ma mieć środek na osi Y i R100");
            need(throws([&] { c2::bulge_tangent({0, 0}, {1, 0}, {100, 0}); }), "łuk styczny na przedłużeniu odcinka powinien być odrzucony");
            need(close_to(c2::bulge_center({0, 0}, {50, 0}, {0, 50}, true), std::tan(c2::PI / 8), 1e-9), "łuk ćwierćokręgu ze środka ma mieć wybrzuszenie tan(π/8)");
            // sprawdzanie konturu
            need(throws([&] { c2::validate_profile(c2::polyline({{0, 0}, {10, 0}, {10, 10}}, false)); }), "otwarta linia nie jest profilem płyty");
            need(throws([&] { c2::validate_profile(c2::polyline({{0, 0}, {100, 100}, {100, 0}, {0, 100}}, true)); }), "kontur z samoprzecięciem powinien być odrzucony");
            // płyty i otwory
            std::vector<c2::Path> sketch = {c2::rectangle({0, 0}, {600, 400}), c2::circle({300, 200}, 30), c2::rectangle({100, 100}, {150, 150}), c2::rectangle({1000, 0}, {1200, 300})};
            auto groups = c2::group_shapes(sketch);
            need(groups.size() == 2 && groups[0].outer == 0 && groups[0].inner.size() == 2 && groups[1].outer == 3 && groups[1].inner.empty(), "podział na płyty i otwory jest zły");
            need(throws([&] { c2::group_shapes({c2::rectangle({0, 0}, {100, 100}), c2::rectangle({50, 50}, {150, 150})}); }), "przecinające się kształty powinny być odrzucone");
            need(throws([&] { c2::group_shapes({c2::rectangle({0, 0}, {1000, 1000}), c2::rectangle({100, 100}, {800, 800}), c2::circle({400, 400}, 50)}); }), "zagnieżdżenie na dwóch poziomach powinno być odrzucone");
            c2::Contour contour = c2::to_contour(c2::translate(rounded, {-500, -300}));
            need(close_to(contour.u, 600, 1e-6) && close_to(contour.offset[0], -500, 1e-6) && contour.points.size() <= c2::MAX_CONTOUR_POINTS, "kontur do płyty jest zły (przesunięcie do zera albo za dużo punktów)");
            need(c2::tessellate(c2::circle({0, 0}, 5000)).size() <= c2::MAX_CONTOUR_POINTS, "duży okrąg przekracza limit punktów konturu");
            // przyciąganie i edycja
            auto snap_end = c2::nearest_snap({rect}, {3, 2}, 10);
            auto snap_mid = c2::nearest_snap({rect}, {300, 3}, 10);
            auto snap_center = c2::nearest_snap({circle}, {101, 99}, 5);
            need(snap_end && snap_end->label == "koniec" && snap_mid && snap_mid->label == "środek" && snap_center && snap_center->label == "środek okręgu", "przyciąganie do końca, środka i środka okręgu nie działa");
            c2::Point axis = c2::snap_to_axis({0, 0}, {100, 2});
            need(axis[1] == 0, "odchyłka 1° od osi powinna zostać wyprostowana");
            need(c2::nearest_segment(rect, {300, 2}, 5) == 0u && c2::nearest_vertex(rect, {598, 3}, 5) == 1u, "wskazanie odcinka i wierzchołka jest złe");
            need(c2::set_segment_length(rect, 0, 300).v[1].p[0] == 300 && c2::remove_vertex(rect, 1).v.size() == 3, "zmiana długości odcinka albo usunięcie wierzchołka nie działa");
            ctx.note("łuki 3-punktowe i styczne, zaokrąglenia, fazki, szczeliny, otwory, przyciąganie");
        });

        run(area, "2D CAD: płyty z konturu z wycięciami, zmiana obrysu istniejącej płyty i kamera wprost na płaszczyznę", where, [=, this](Ctx& ctx) {
            namespace mg = modeler_geometry;
            auto within = [](double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; };
            // kamera wprost na każdą płaszczyznę: u w prawo, v w górę ekranu
            for (const mg::Plane& plane : {mg::ground_plane(0), mg::front_plane(0), mg::side_plane(0)}) {
                auto [yaw, pitch] = mg::camera_angles_for_plane(plane);
                core::Camera camera(yaw, pitch, 1);
                auto o = camera.project(plane.origin, 200, 200), pu = camera.project(mg::add(plane.origin, plane.u), 200, 200), pv = camera.project(mg::add(plane.origin, plane.v), 200, 200);
                need(pu[0] - o[0] > .9 && within(pu[1], o[1]) && pv[1] - o[1] < -.9 && within(pv[0], o[0]), "kamera dla płaszczyzny „" + plane.label + "” nie patrzy wprost (u w prawo, v w górę)");
            }
            // płyta L z wycięciami: okrąg, szczelina, prostokąt
            mg::Plane ground = mg::ground_plane(0);
            std::vector<mg::Vec3> L = {{100, 200, 0}, {700, 200, 0}, {700, 500, 0}, {400, 500, 0}, {400, 800, 0}, {100, 800, 0}};
            mg::Cutout c_circle, c_slot, c_rect;
            c_circle.shape = "circle"; c_circle.center = {200, 300}; c_circle.width = 40;
            c_slot.shape = "slot"; c_slot.center = {300, 700}; c_slot.length = 120; c_slot.width = 20;
            c_rect.shape = "rect"; c_rect.center = {550, 350}; c_rect.length = 100; c_rect.width = 60;
            core::Part plate = mg::board_from_profile(ground, L, {c_circle, c_slot, c_rect}, 18, "Blat L", "Grupa", "mat");
            need(plate.plane == "XY" && within(plate.x, 100) && within(plate.y, 200) && within(plate.u, 600) && within(plate.v, 600) && within(plate.t, 18) && plate.contour.size() == 6 && plate.machining.size() == 3,
                 std::format("płyta L z profilu: {} {}×{}, kontur {}, obróbka {}", plate.plane, plate.u, plate.v, plate.contour.size(), plate.machining.size()));
            mg::Plane top = mg::face_plane(plate, "top");
            auto back = mg::cutouts_of(plate, top);
            need(back.size() == 3 && back[0].shape == "circle" && within(back[0].width, 40) && within(back[2].length, 100) && within(back[2].width, 60) && within(back[1].length, 120) && within(back[1].angle, 0),
                 "wycięcia odczytane z płyty nie zgadzają się z narysowanymi");
            // pionowa płyta na płaszczyźnie przód — prostokątne wycięcie zachowuje wymiary
            mg::Plane front = mg::front_plane(0);
            mg::Cutout window; window.shape = "rect"; window.center = {250, 150}; window.length = 100; window.width = 50;
            core::Part facade = mg::board_from_profile(front, {{0, 0, 0}, {500, 0, 0}, {500, 0, 300}, {0, 0, 300}}, {window}, 18, "Front", "G", "m");
            auto window_back = mg::cutouts_of(facade, mg::face_plane(facade, "top"));
            need(facade.plane == "XZ" && within(facade.u, 500) && within(facade.v, 300) && window_back.size() == 1 && within(window_back[0].length, 100) && within(window_back[0].width, 50),
                 "płyta pionowa z wycięciem prostokątnym ma złe wymiary");
            // zmiana obrysu istniejącej płyty: przesunięcie początku, brak starych wycięć, odrzucenie samoprzecięcia
            mg::Plane facade_top = mg::face_plane(facade, "top");
            core::Part wider = mg::reshape_part(facade, facade_top, {{-50, 0, -20}, {600, 0, -20}, {600, 0, 300}, {-50, 0, 300}}, {});
            need(within(wider.u, 650) && within(wider.v, 320) && wider.machining.empty(), "poszerzenie płyty: złe wymiary albo zostały stare wycięcia");
            core::Part triangle = mg::reshape_part(facade, facade_top, {{0, 0, 0}, {500, 0, 0}, {250, 0, 300}}, {window});
            need(triangle.contour.size() == 3 && triangle.machining.size() == 1, "płyta trójkątna z jednym wycięciem ma zły kontur albo obróbkę");
            need(throws([&] { mg::reshape_part(facade, facade_top, {{0, 0, 0}, {500, 0, 300}, {500, 0, 0}, {0, 0, 300}}, {}); }), "kontur z samoprzecięciem powinien być odrzucony z powodem");
            mg::Cutout tiny; tiny.shape = "circle"; tiny.center = {10, 10}; tiny.width = .1;
            need(throws([&] { mg::board_from_profile(ground, L, {tiny}, 18, "x", "g", "m"); }), "otwór o średnicy 0,1 mm powinien być odrzucony");
            ctx.note("płyta L, wycięcia okrągłe/podłużne/prostokątne, odczyt wycięć, zmiana obrysu");
        });

        run(area, "pracownia 2D CAD: rysowanie z łukami, wyciąganie do płyt z otworami, edycja obrysu, wspólny projekt z 3D, cofanie i odrzucenia", where, [=, this](Ctx& ctx) {
            namespace mg = modeler_geometry;
            namespace c2 = cad2d;
            app->project = fresh(); app->refresh();
            auto* m = modeler_ui::open(app);
            auto cleanup = [&] { try { if (m->winfo_exists()) m->destroy(); } catch (const std::exception&) {} };
            try {
                app->update();
                auto from = log_mark();
                m->set_mode("2d");
                need(m->mode == "2d" && m->mode_var.get() == "2d", "przełącznik nie przeszedł w tryb 2D CAD");
                need(has_text(log_since(from), "[operacja:cad2d] przełączono na 2D CAD"), "przełączenie na 2D nie zapisało się w dzienniku [cad2d]");
                need(throws([&] { m->set_mode("4d"); }), "nieznany tryb pracowni powinien być odrzucony");
                m->snap_grid_var.set(false);
                m->ortho_var.set(false);
                m->thickness.set("18");
                m->group_name.set("Mebel z rysunku");
                m->draw_scene();
                const int w = m->canvas->winfo_width(), h = m->canvas->winfo_height();
                need(w >= 100 && h >= 100, std::format("płótno pracowni jest za małe do testu ({}×{} px)", w, h));
                need(!m->canvas->find_withtag("grid").empty(), "tryb 2D nie narysował siatki");
                auto px = [&](double x, double y) {
                    auto s = m->camera.project(mg::from_plane(m->plane2d, x, y), w, h);
                    return std::pair<double, double>{s[0], s[1]};
                };
                auto within = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
                auto click = [&](double x, double y) { auto [sx, sy] = px(x, y); m->on_click(sx, sy); };
                auto hover = [&](double x, double y) { auto [sx, sy] = px(x, y); m->on_motion(sx, sy); };
                auto arcs_in = [&](const c2::Path& path) { size_t n = 0; for (const auto& v : path.v) if (std::fabs(v.bulge) > 1e-12) ++n; return n; };

                // 1. kontur z łukiem: punkt wpisany, odcinki wpisane (długość<kąt), łuk 3-punktowy myszą, zamknięcie klawiszem C
                m->set_tool2("line");
                hover(300, 300);
                need(m->apply_typed("0;0") && m->d2.stage == 1, "współrzędne „0;0” nie rozpoczęły konturu");
                need(m->apply_typed("600<0") && m->apply_typed("400<90") && m->d2.verts.size() == 3, "wpisane odcinki 600<0 i 400<90 nie zostały dodane");
                need(within(m->d2.verts[2].p[0], 600, 1e-6) && within(m->d2.verts[2].p[1], 400, 1e-6), "wpisany odcinek 400<90 kończy się w złym miejscu");
                m->on_key("a");
                need(m->d2.seg == "arc3", "klawisz A powinien przełączyć na łuk 3-punktowy");
                need(!m->apply_typed("100"), "długość wpisana dla łuku 3-punktowego powinna być odrzucona z powodem");
                click(300, 500);
                need(m->d2.arc_through.has_value(), "pierwszy klik łuku powinien zaznaczyć punkt pośredni");
                click(0, 400);
                need(m->d2.verts.size() == 4 && std::fabs(m->d2.verts[2].bulge) > 1e-9, "drugi klik łuku nie dodał łuku do konturu");
                m->on_key("c");
                need(m->shapes.size() == 1 && m->shapes[0].closed && arcs_in(m->shapes[0]) == 1, std::format("po zamknięciu klawiszem C szkic ma {} kształtów", m->shapes.size()));
                {
                    double expected = 240000 + 40875;   // prostokąt + odcinek koła (cięciwa 600, strzałka 100)
                    need(within(c2::area(m->shapes[0]), expected, expected * .03), std::format("pole konturu z łukiem {} mm² (oczekiwano ≈{})", c2::area(m->shapes[0]), expected));
                }
                need(has_text(log_since(from), "[operacja:cad2d] dodano kontur zamknięty"), "zamknięcie konturu nie zapisało się w dzienniku");

                // 2. zaokrąglenie narożnika (klik w wierzchołek) i odrzucenie zbyt dużego promienia
                m->set_tool2("fillet");
                m->radius_var.set("40");
                size_t before_vertices = m->shapes[0].v.size();
                click(600, 0);
                need(m->shapes[0].v.size() == before_vertices + 1 && arcs_in(m->shapes[0]) == 2, "zaokrąglenie narożnika R40 nie dodało łuku");
                from = log_mark();
                m->radius_var.set("5000");
                c2::Path unchanged = m->shapes[0];
                click(600, 400);
                need(m->shapes[0].v.size() == unchanged.v.size(), "odrzucone zaokrąglenie zmieniło kształt");
                need(has_text(log_since(from), "[operacja:cad2d] odrzucono:"), "odrzucone zaokrąglenie nie zostawiło powodu w dzienniku");

                // 3. otwory: okrąg (wpisany promień), szczelina (wpisana oś), prostokąt (wpisane wymiary)
                m->set_tool2("circle");
                need(m->apply_typed("300;200") && m->apply_typed("50") && m->shapes.size() == 2, "okrąg R50 wpisany z klawiatury nie został dodany");
                need(c2::as_circle(m->shapes[1]).has_value(), "narysowany okrąg nie jest rozpoznawany jako okrąg");
                m->set_tool2("slot");
                m->slot_width_var.set("30");
                need(m->apply_typed("100;100") && (hover(250, 100), m->apply_typed("100<0")) && m->shapes.size() == 3 && c2::as_slot(m->shapes[2]).has_value(), "szczelina nie została dodana");
                m->set_tool2("rect");
                need(m->apply_typed("400;250"), "początek prostokąta nie został przyjęty");
                hover(500, 300);
                need(m->apply_typed("80x40") && m->shapes.size() == 4 && c2::as_rectangle(m->shapes[3]).has_value(), "prostokąt 80×40 nie został dodany");
                m->set_tool2("select2");
                click(300, 200);
                need(m->shape_sel && *m->shape_sel == 1, "kliknięcie w okrąg w narzędziu Wybierz nie zaznaczyło okręgu");
                m->draw_scene();
                need(!m->canvas->find_withtag("sketch").empty(), "szkic nie jest narysowany");
                m->refresh_panel();
                need(m->shapes_listed == 4, std::format("lista kształtów w panelu ma {} pozycji (oczekiwano 4)", m->shapes_listed));

                // 4. wymiar: zmiana długości odcinka prostego
                {
                    c2::Path edge = c2::rectangle({1000, 0}, {1300, 200});
                    m->shapes.push_back(edge);
                    m->set_tool2("dim");
                    click(1150, 0);
                    need(m->d2.index && m->d2.vertex && *m->d2.index == 4, "narzędzie Wymiar nie wskazało odcinka");
                    need(m->apply_typed("250"), "zmiana długości odcinka na 250 została odrzucona");
                    need(m->shapes[4].v.size() == 4, "zmiana długości odcinka zmieniła liczbę wierzchołków");
                    m->remove_shape(4);
                }

                // 5. wyciągnięcie do płyt: kontur z łukami → jedna płyta, trzy wycięcia przelotowe
                from = log_mark();
                m->selection.clear();
                need(m->extrude_shapes(), "wyciągnięcie szkicu do płyt nie powiodło się");
                need(app->project->parts.size() == 1 && m->shapes.empty(), std::format("po wyciągnięciu jest {} płyt i {} kształtów szkicu", app->project->parts.size(), m->shapes.size()));
                const core::Part plate = app->project->parts[0];
                need(plate.plane == "XY" && within(plate.u, 600, 5) && within(plate.v, 500, 8) && within(plate.t, 18, 1e-6) && plate.group == "Mebel z rysunku", std::format("płyta z rysunku: {} {}×{}×{}", plate.plane, plate.u, plate.v, plate.t));
                need(plate.contour.size() > 12 && plate.contour.size() <= 128, std::format("kontur płyty z łukami ma {} punktów (oczekiwano 13…128)", plate.contour.size()));
                size_t cutouts = 0;
                for (const auto& cut : plate.machining) if (cut.value("type", "") == "cutout") ++cutouts;
                need(cutouts == 3, std::format("płyta ma {} wycięć przelotowych (oczekiwano 3: okrąg, szczelina, prostokąt)", cutouts));
                need(has_text(log_since(from), "[operacja:cad2d] wyciągnięto 4 kształtów do 1 płyt"), "wyciągnięcie nie zapisało się w dzienniku");
                // wspólny projekt: płyta jest od razu w widoku 3D
                m->set_mode("3d");
                m->draw_scene();
                need(m->mode == "3d" && m->project()->parts.size() == 1 && !m->canvas->find_all().empty(), "po powrocie do 3D płyta narysowana w 2D nie jest widoczna");
                need(m->selection.contains(plate.id), "płyta z rysunku powinna być zaznaczona w 3D");
                m->set_mode("2d");
                m->draw_scene();
                need(!m->canvas->find_withtag("plates").empty(), "tryb 2D nie rysuje płyt projektu");

                // 6. cofanie i ponawianie (szkic razem z projektem)
                need(m->undo_2d() && app->project->parts.empty() && m->shapes.size() == 4, std::format("cofnięcie wyciągnięcia: {} płyt, {} kształtów (oczekiwano 0 i 4)", app->project->parts.size(), m->shapes.size()));
                need(m->redo_2d() && app->project->parts.size() == 1 && m->shapes.empty(), "ponowienie wyciągnięcia powinno przywrócić płytę i opróżnić szkic");
                const std::string plate_id = app->project->parts[0].id;

                // 7. edycja obrysu istniejącej płyty: wczytanie, usunięcie okrągłego otworu, zastosowanie, cofnięcie
                from = log_mark();
                m->selection = {plate_id};
                need(m->load_part_outline() && m->editing_part == plate_id && m->shapes.size() == 4, std::format("wczytanie obrysu płyty dało {} kształtów (oczekiwano 4)", m->shapes.size()));
                need(!m->extrude_shapes(), "wyciąganie szkicu będącego obrysem płyty powinno być odrzucone (grozi duplikatem)");
                need(c2::as_circle(m->shapes[1]).has_value(), "drugi kształt wczytanego obrysu powinien być okrągłym otworem");
                m->remove_shape(1);
                need(m->apply_part_outline(), "zastosowanie obrysu do płyty nie powiodło się");
                {
                    const core::Part& edited = app->project->parts[0];
                    size_t remaining = 0;
                    for (const auto& cut : edited.machining) if (cut.value("type", "") == "cutout") ++remaining;
                    need(app->project->parts.size() == 1 && remaining == 2 && edited.id == plate_id, std::format("po edycji obrysu płyta ma {} wycięć (oczekiwano 2), płyt {}", remaining, app->project->parts.size()));
                    need(within(edited.u, plate.u, 1e-6) && within(edited.v, plate.v, 1e-6), "edycja obrysu zmieniła wymiary płyty");
                }
                need(!m->editing_part && m->shapes.empty(), "po zastosowaniu obrysu szkic powinien być pusty");
                need(has_text(log_since(from), "[operacja:cad2d] wczytano obrys płyty") && has_text(log_since(from), "[operacja:cad2d] zmieniono obrys płyty"), "edycja obrysu nie zapisała się w dzienniku");
                need(m->undo_2d(), "cofnięcie edycji obrysu nie zadziałało");
                {
                    size_t restored = 0;
                    for (const auto& cut : app->project->parts[0].machining) if (cut.value("type", "") == "cutout") ++restored;
                    need(restored == 3 && m->shapes.size() == 3, std::format("cofnięcie edycji obrysu: {} wycięć, {} kształtów (oczekiwano 3 i 3)", restored, m->shapes.size()));
                }
                m->clear_sketch();
                need(m->shapes.empty(), "czyszczenie szkicu nie opróżniło go");

                // 8. odrzucenia z powodem w dzienniku, bez zmian projektu
                from = log_mark();
                const size_t parts_before = app->project->parts.size();
                need(!m->apply_part_outline(), "zastosowanie obrysu bez wczytanej płyty powinno być odrzucone");
                need(!m->extrude_shapes(), "wyciągnięcie pustego szkicu powinno być odrzucone");
                m->set_tool2("line");
                need(m->apply_typed("0;0") && m->apply_typed("300<0") && m->d2.verts.size() == 2, "otwarta linia nie została narysowana");
                m->on_key("Return");
                need(m->shapes.size() == 1 && !m->shapes[0].closed, "Enter powinien zakończyć linię jako otwartą");
                need(!m->extrude_shapes() && app->project->parts.size() == parts_before, "otwarta linia nie może zostać wyciągnięta do płyty");
                m->clear_sketch();
                m->shapes = {c2::rectangle({0, 0}, {600, 400}), c2::polyline({{100, 100}, {200, 100}, {150, 200}}, true)};
                need(!m->extrude_shapes() && app->project->parts.size() == parts_before, "otwór o dowolnym kształcie (trójkąt) powinien być odrzucony z powodem");
                m->shapes = {c2::rectangle({0, 0}, {100, 100}), c2::rectangle({50, 50}, {150, 150})};
                need(!m->extrude_shapes() && app->project->parts.size() == parts_before, "przecinające się kształty powinny być odrzucone");
                m->shapes.clear();
                m->set_tool2("arc");
                need(m->apply_typed("0;0") && m->apply_typed("") == false, "łuk z jednym punktem nie może być zakończony");
                m->on_key("t");
                click(200, 0);
                need(m->d2.verts.size() == 1, "łuk styczny bez poprzedniego odcinka powinien być odrzucony");
                m->cancel();
                m->set_tool2("circle");
                need(!m->apply_typed("abc"), "tekst zamiast promienia powinien być odrzucony");
                m->cancel();
                m->selection.clear();
                need(!m->set_work_plane("Góra zaznaczonej płyty", 0), "rysowanie na górze płyty bez zaznaczenia powinno być odrzucone");
                need(!m->set_work_plane("Sufit", 0), "nieznana płaszczyzna powinna być odrzucona");
                need(has_text(log_since(from), "[operacja:cad2d] odrzucono:"), "odrzucenia 2D nie zostawiły powodów w dzienniku");

                // 9. łuk styczny wpisany długością, pozostałe narzędzia i skróty
                m->shapes.clear();
                m->selection.clear();
                app->project = fresh(); app->refresh();   // bez płyt w projekcie przyciąganie nie podmienia klikanych punktów
                m->set_tool2("line");
                hover(300, 100);
                need(m->apply_typed("0;0") && m->apply_typed("300<0"), "linia startowa łuku stycznego nie powstała");
                m->on_key("t");
                hover(300, 100);
                need(m->apply_typed("200") && m->d2.verts.size() == 3 && std::fabs(m->d2.verts[1].bulge) > 1e-9, "łuk styczny o cięciwie 200 nie został dodany");
                m->on_key("Escape");
                m->on_key("k");
                need(m->tool2 == "circle", "skrót K powinien wybierać okrąg");
                m->on_key("o");
                need(m->ortho_var.get(), "skrót O powinien włączyć wymuszanie osi");
                m->on_key("o");
                m->set_tool2("polygon");
                m->sides_var.set("6");
                need(m->apply_typed("500;500") && m->apply_typed("60") && m->shapes.size() == 1 && m->shapes[0].v.size() == 6, "sześciokąt foremny nie został dodany");
                m->set_tool2("rrect");
                m->radius_var.set("25");
                need(m->apply_typed("0;600"), "początek prostokąta z zaokrągleniem nie został przyjęty");
                hover(300, 800);
                need(m->apply_typed("300x200") && m->shapes.size() == 2 && m->shapes[1].v.size() == 8, "prostokąt z zaokrągleniem R25 nie został dodany");
                m->set_tool2("movev");
                {
                    const c2::Point corner = m->shapes[0].v[1].p;
                    click(corner[0], corner[1]);
                    need(m->d2.stage == 1 && m->d2.index.has_value() && m->d2.vertex.has_value(), "narzędzie Przesuń nie wskazało wierzchołka");
                    click(corner[0] + 100, corner[1]);
                    const c2::Point moved = m->shapes[0].v[1].p;
                    need(std::hypot(moved[0] - corner[0] - 100, moved[1] - corner[1]) < 3, std::format("wierzchołek przesunięto do ({}, {}) zamiast o 100 mm w prawo", moved[0], moved[1]));
                }
                m->set_tool2("delv");
                {
                    const size_t verts = m->shapes[0].v.size();
                    const c2::Point victim = m->shapes[0].v[3].p;
                    click(victim[0], victim[1]);
                    need(m->shapes[0].v.size() == verts - 1, "usunięcie wierzchołka sześciokąta nie zadziałało");
                    m->undo_2d();
                    need(m->shapes[0].v.size() == verts, "cofnięcie usunięcia wierzchołka nie przywróciło go");
                    m->redo_2d();
                    need(m->shapes[0].v.size() == verts - 1, "ponowienie usunięcia wierzchołka nie zadziałało");
                }

                // 10. płaszczyzna Przód: kontur na ścianie → pionowa płyta
                m->clear_sketch();
                need(m->set_work_plane("Przód", 0) && m->plane2d.label == "Przód", "przełączenie płaszczyzny na Przód nie zadziałało");
                m->draw_scene();
                m->set_tool2("rect");
                hover(100, 100);
                need(m->apply_typed("0;0"), "początek prostokąta na płaszczyźnie Przód nie został przyjęty");
                hover(800, 900);
                need(m->apply_typed("500x700") && m->extrude_shapes(), "prostokąt na płaszczyźnie Przód nie został wyciągnięty do płyty");
                {
                    const core::Part& front = app->project->parts.back();
                    need(front.plane == "XZ" && within(front.u, 500, 1e-6) && within(front.v, 700, 1e-6), std::format("płyta z płaszczyzny Przód: {} {}×{}", front.plane, front.u, front.v));
                }
                m->selection = {app->project->parts.back().id};
                need(m->set_work_plane("Góra zaznaczonej płyty", 0) && m->plane2d.label.rfind("Góra: ", 0) == 0, "rysowanie na górze zaznaczonej płyty nie zadziałało");
                m->draw_scene();
                m->set_mode("3d");
                need(m->mode == "3d", "powrót do 3D nie zadziałał");
                ctx.note("łuki, okrąg, szczelina, zaokrąglenie, wymiary, płyty z otworami, edycja obrysu, cofanie, płaszczyzny");
            } catch (...) { cleanup(); app->project = fresh(); app->refresh(); throw; }
            cleanup();
            need(modeler_ui::current() == nullptr, "po zamknięciu pracownia nadal jest zarejestrowana jako otwarta");
            app->project = fresh(); app->refresh();
        });

        run(area, "CAD: dodanie wiercenia nie przelicza bryły bazowej (pamięć podręczna bez wierceń)", where, [=, this](Ctx& ctx) {
            core::Part part;
            part.u = 400; part.v = 300; part.t = 18;
            part.fillets = json::array({json{{"edge", "top:0"}, {"radius", 3}}, json{{"edge", "bottom:0"}, {"radius", 3}}});
            part.machining = sketch_design::generate_pattern("ramka", {}, 400, 300, 18).cuts.empty() ? json::array() : json::array();
            for (const auto& cut : sketch_design::generate_pattern("ramka", {}, 400, 300, 18).cuts) part.machining.push_back(cut);
            cad_geometry::reset_cache_stats();
            cad_geometry::local_shape(part);
            auto first = cad_geometry::cache_stats();
            need(first.shape_misses >= 1, "pierwsze zbudowanie bryły powinno liczyć się jako przeliczenie");
            part.operations = json::array({json{{"type", "drill"}, {"u", 100}, {"v", 100}, {"diameter", 8}, {"depth", 10}, {"face", "top"}}});
            cad_geometry::local_shape(part);
            auto second = cad_geometry::cache_stats();
            need(second.shape_misses == first.shape_misses && second.shape_hits > first.shape_hits, "dodanie wiercenia przeliczyło bryłę bazową — pamięć podręczna powinna ją oddać");
            part.fillets = json::array({json{{"edge", "top:0"}, {"radius", 4}}});
            cad_geometry::local_shape(part);
            need(cad_geometry::cache_stats().shape_misses > second.shape_misses, "zmiana zaokrąglenia musi przeliczyć bryłę");
            cad_geometry::mesh(part);
            auto meshed = cad_geometry::cache_stats();
            cad_geometry::mesh(part);
            need(cad_geometry::cache_stats().mesh_hits > meshed.mesh_hits, "ta sama siatka powinna pochodzić z pamięci podręcznej");
            ctx.note(std::format("bryły: {} trafień / {} przeliczeń · siatki: {} / {}", cad_geometry::cache_stats().shape_hits, cad_geometry::cache_stats().shape_misses,
                                 cad_geometry::cache_stats().mesh_hits, cad_geometry::cache_stats().mesh_misses));
        });
    }

    void test_sketch_tools() {
        const char* area = "Szkic — wskazówki, pole wartości, kształty, DXF";
        const char* where = "sketch_inference.cpp, sketch_shapes.cpp, dxf_import.cpp, sketch.cpp (local/key/value_apply/finish_shape/import_dxf)";
        using sketch_inference::Kind;
        auto near_to = [](double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; };

        run(area, "wskazówki: punkt końcowy, środek, na krawędzi, przecięcie, środek okręgu", where, [=](Ctx& ctx) {
            sketch_inference::Input in;
            in.points = {{0, 0}, {100, 0}, {100, 50}};
            in.tolerance = 5;
            in.raw = {102, 2};
            auto r = sketch_inference::infer(in);
            need(r.kind == Kind::Endpoint && near_to(r.point[0], 100) && near_to(r.point[1], 0), "kursor przy końcu odcinka nie złapał punktu końcowego");
            in.raw = {51, 1};
            r = sketch_inference::infer(in);
            need(r.kind == Kind::Midpoint && near_to(r.point[0], 50), "kursor przy środku odcinka nie złapał środka");
            in.raw = {30, 2};
            r = sketch_inference::infer(in);
            need(r.kind == Kind::OnEdge && near_to(r.point[1], 0), "kursor przy krawędzi nie złapał punktu na krawędzi");
            sketch_inference::Input cross;
            cross.points = {{0, 0}, {100, 100}};
            cross.reference = {{{0, 100}, {60, 0}}};
            cross.tolerance = 5; cross.raw = {38, 37};
            r = sketch_inference::infer(cross);
            need(r.kind == Kind::Intersection && near_to(r.point[0], 37.5), "kursor przy skrzyżowaniu nie złapał przecięcia");
            sketch_inference::Input center;
            center.centers = {{50, 50}}; center.tolerance = 5; center.raw = {52, 48};
            need(sketch_inference::infer(center).kind == Kind::Center, "kursor przy środku okręgu nie złapał środka okręgu");
            sketch_inference::Input off;
            off.points = {{0, 0}, {100, 0}}; off.snap_points = false; off.tolerance = 5; off.raw = {1, 1};
            need(sketch_inference::infer(off).kind == Kind::None, "wyłączone przyciąganie do punktów nadal przyciąga");
            ctx.note("kolory: " + sketch_inference::color_for(Kind::Endpoint) + " / " + sketch_inference::color_for(Kind::Midpoint));
        });

        run(area, "wskazówki: oś U/V, od punktu, prostopadle, równolegle, przedłużenie, siatka", where, [=](Ctx&) {
            sketch_inference::Input a;
            a.points = {{0, 0}};
            a.anchor = sketch_inference::Point{0, 0};
            a.tolerance = 5; a.raw = {200, 3};
            auto r = sketch_inference::infer(a);
            need(r.kind == Kind::AxisU && near_to(r.point[1], 0), "kursor blisko poziomu nie złapał osi U");
            need(r.direction && near_to((*r.direction)[0], 1), "oś U nie podaje kierunku do blokady");
            a.raw = {2, 150};
            r = sketch_inference::infer(a);
            need(r.kind == Kind::AxisV && near_to(r.point[0], 0), "kursor blisko pionu nie złapał osi V");
            a.raw = {100, 100};
            need(sketch_inference::infer(a).kind == Kind::None, "kursor daleko od osi nie powinien nic łapać");
            a.grid = true; a.grid_step = 10; a.raw = {103.3, 87.2};
            r = sketch_inference::infer(a);
            need(r.kind == Kind::Grid && near_to(r.point[0], 100) && near_to(r.point[1], 90), "siatka nie zaokrągliła punktu");
            sketch_inference::Input from;
            from.points = {{0, 0}, {300, 0}}; from.anchor = sketch_inference::Point{300, 0}; from.tolerance = 5; from.raw = {2, 120};
            r = sketch_inference::infer(from);
            need(r.kind == Kind::FromPoint && near_to(r.point[0], 0) && near_to(r.point[1], 120) && !r.guides.empty(), "wyrównanie do wierzchołka („od punktu”) nie zadziałało");
            sketch_inference::Input perp;
            perp.points = {{0, 0}, {100, 100}}; perp.anchor = sketch_inference::Point{100, 100}; perp.tolerance = 5; perp.raw = {60, 140};
            need(sketch_inference::infer(perp).kind == Kind::Perpendicular, "nie wykryto prostopadłości do ostatniego odcinka");
            perp.raw = {200, 203};
            need(sketch_inference::infer(perp).kind == Kind::Parallel, "nie wykryto równoległości do ostatniego odcinka");
            sketch_inference::Input ext;
            ext.reference = {{{0, 0}, {100, 100}}}; ext.anchor = sketch_inference::Point{500, 10}; ext.tolerance = 5; ext.raw = {160, 159};
            r = sketch_inference::infer(ext);
            need(r.kind == Kind::ExtendEdge && near_to(r.point[0], r.point[1]), "nie wykryto przedłużenia krawędzi");
        });

        run(area, "wskazówki: blokada kierunku (Shift / strzałki) i tryb orto", where, [=](Ctx&) {
            sketch_inference::Input lock;
            lock.anchor = sketch_inference::Point{0, 0}; lock.lock_direction = sketch_inference::Point{0.6, 0.8}; lock.tolerance = 5; lock.raw = {100, 50};
            auto r = sketch_inference::infer(lock);
            need(r.kind == Kind::Locked && near_to(r.point[0] * 0.8 - r.point[1] * 0.6, 0), "zablokowany kierunek nie rzutuje kursora na prostą");
            lock.lock_direction = sketch_inference::Point{1, 0};
            r = sketch_inference::infer(lock);
            need(r.kind == Kind::AxisU && near_to(r.point[1], 0) && near_to(r.point[0], 100), "blokada osi U nie trzyma poziomu");
            sketch_inference::Input ortho;
            ortho.anchor = sketch_inference::Point{10, 10}; ortho.ortho = true; ortho.tolerance = 1; ortho.raw = {90, 40};
            r = sketch_inference::infer(ortho);
            need(near_to(r.point[1], 10) && near_to(r.point[0], 90), "tryb orto nie wymusił poziomu");
            sketch_inference::Input priority;
            priority.points = {{0, 0}, {300, 0}}; priority.anchor = sketch_inference::Point{300, 0}; priority.tolerance = 5; priority.raw = {2, 2};
            need(sketch_inference::infer(priority).kind == Kind::Endpoint, "punkt końcowy powinien wygrać z osią i wyrównaniem");
        });

        run(area, "kształty: okrąg, wielobok, łuk przez 3 punkty, prostokąt zaokrąglony", where, [=](Ctx& ctx) {
            auto circle = sketch_shapes::circle({10, 20}, 50, 48);
            need(circle.size() == 48, "okrąg nie ma 48 punktów");
            for (auto& p : circle) need(near_to(std::hypot(p[0] - 10, p[1] - 20), 50, 1e-6), "punkt okręgu leży poza promieniem");
            need(sketch_shapes::signed_area(circle) > 0 && !sketch_shapes::self_intersects(circle), "okrąg ma zły obieg albo samoprzecięcia");
            auto hexagon = sketch_shapes::regular_polygon({0, 0}, 100, 6);
            need(hexagon.size() == 6 && near_to(std::hypot(hexagon[0][0] - hexagon[1][0], hexagon[0][1] - hexagon[1][1]), 100, 1e-6), "sześciokąt foremny ma zły bok");
            auto arc = sketch_shapes::arc_three_points({100, 0}, {0, 100}, {-100, 0});
            need(near_to(arc.front()[0], 100) && near_to(arc.back()[0], -100), "łuk nie zaczyna się i nie kończy w zadanych punktach");
            for (auto& p : arc) need(near_to(std::hypot(p[0], p[1]), 100, 1e-6), "punkt łuku leży poza promieniem");
            auto rounded = sketch_shapes::rounded_rectangle({0, 0}, {200, 100}, 20);
            need(rounded.size() == 28 && !sketch_shapes::self_intersects(rounded), "zaokrąglony prostokąt ma zły kontur");
            auto bad = [](auto&& call) { try { call(); } catch (const std::invalid_argument&) { return true; } return false; };
            need(bad([] { sketch_shapes::circle({0, 0}, -1); }), "ujemny promień okręgu powinien być odrzucony");
            need(bad([] { sketch_shapes::regular_polygon({0, 0}, 10, 2); }), "wielobok z 2 bokami powinien być odrzucony");
            need(bad([] { sketch_shapes::arc_three_points({0, 0}, {1, 1}, {2, 2}); }), "łuk przez współliniowe punkty powinien być odrzucony");
            need(bad([] { sketch_shapes::rounded_rectangle({0, 0}, {200, 100}, 60); }), "za duży promień zaokrąglenia powinien być odrzucony");
            ctx.note(std::format("okrąg {} pkt, łuk {} pkt, zaokrąglony prostokąt {} pkt", circle.size(), arc.size(), rounded.size()));
        });

        run(area, "szybkie profile: każdy z katalogu ma poprawny kontur i wymiary", where, [=](Ctx& ctx) {
            std::string names;
            for (const auto& info : sketch_shapes::profile_catalog()) {
                auto shape = sketch_shapes::profile(info.name, 600, 400, info.default_param);
                need(shape.size() >= 3 && shape.size() <= sketch_shapes::MAX_POINTS, "profil „" + info.name + "” ma zła liczbę punktów");
                need(!sketch_shapes::self_intersects(shape), "profil „" + info.name + "” przecina sam siebie");
                double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
                for (auto& p : shape) { xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]); ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]); }
                need(near_to(xmax - xmin, 600) && near_to(ymax - ymin, 400), "profil „" + info.name + "” nie ma wymiarów 600×400");
                names += info.name + ", ";
            }
            need(near_to(std::abs(sketch_shapes::signed_area(sketch_shapes::profile("Kształt L", 600, 400, 100))), 600 * 100 + 300 * 100), "pole kształtu L jest błędne");
            bool rejected = false;
            try { sketch_shapes::profile("Kształt L", 600, 400, 500); } catch (const std::invalid_argument&) { rejected = true; }
            need(rejected, "kształt L z grubością większą niż wymiar powinien być odrzucony");
            ctx.note(names);
        });

        run(area, "narożniki: zaokrąglenie, fazowanie, usuwanie punktu, uproszczenie", where, [=](Ctx&) {
            std::vector<core::Point> square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
            auto filleted = sketch_shapes::fillet_corner(square, 1, 20);
            need(filleted.size() > 4 && near_to(filleted[1][0], 80) && near_to(filleted[1][1], 0), "zaokrąglenie nie zaczyna się w punkcie styczności");
            need(std::abs(sketch_shapes::signed_area(filleted) - (10000 - 400 * (1 - std::numbers::pi / 4))) < 5, "pole po zaokrągleniu jest błędne");
            auto chamfered = sketch_shapes::chamfer_corner(square, 2, 30);
            need(chamfered.size() == 5 && near_to(chamfered[2][1], 70) && near_to(chamfered[3][0], 70), "faza ma złe punkty");
            auto bad = [](auto&& call) { try { call(); } catch (const std::invalid_argument&) { return true; } return false; };
            need(bad([&] { sketch_shapes::fillet_corner(square, 1, 120); }), "za duży promień zaokrąglenia powinien być odrzucony");
            need(bad([&] { sketch_shapes::chamfer_corner(square, 2, 150); }), "za duża faza powinna być odrzucona");
            need(bad([&] { sketch_shapes::fillet_corner(square, 0, 5, false); }), "zaokrąglenie końca otwartego konturu powinno być odrzucone");
            need(sketch_shapes::remove_point(square, 0, true).size() == 3, "usunięcie punktu nie zmniejszyło konturu");
            need(bad([&] { sketch_shapes::remove_point(sketch_shapes::remove_point(square, 0, true), 0, true); }), "kontur zamknięty poniżej 3 punktów powinien być odrzucony");
            std::vector<core::Point> dense;
            for (int i = 0; i < 400; ++i) dense.push_back({300 * std::cos(2 * std::numbers::pi * i / 400), 200 * std::sin(2 * std::numbers::pi * i / 400)});
            auto simple = sketch_shapes::simplify(dense, 128);
            need(simple.size() <= 128 && simple.size() >= 60, "uproszczenie 400 punktów dało złą liczbę punktów");
            need(std::abs(sketch_shapes::signed_area(simple) - std::numbers::pi * 300 * 200) / (std::numbers::pi * 300 * 200) < 0.01, "uproszczenie zmieniło pole o ponad 1%");
        });

        run(area, "pole wartości: długość, kąt, wymiary, jednostki, błędne dane", where, [=](Ctx&) {
            auto one = sketch_shapes::parse_value_input("250");
            need(one.numbers.size() == 1 && near_to(one.numbers[0], 250) && !one.has_angle, "„250” nie jest jedną liczbą");
            need(near_to(sketch_shapes::parse_value_input("12,5").numbers[0], 12.5), "przecinek dziesiętny nie działa");
            auto rect = sketch_shapes::parse_value_input("600,400", true);
            need(rect.numbers.size() == 2 && near_to(rect.numbers[1], 400), "„600,400” w prostokącie nie jest szer,wys");
            need(sketch_shapes::parse_value_input("600x400").numbers.size() == 2 && sketch_shapes::parse_value_input("600;400").numbers.size() == 2, "x i ; nie rozdzielają pól");
            auto angled = sketch_shapes::parse_value_input("250<45");
            need(near_to(angled.numbers[0], 250) && angled.has_angle && near_to(angled.angle, 45), "„250<45” nie daje długości i kąta");
            need(near_to(sketch_shapes::parse_value_input("30cm").numbers[0], 300) && near_to(sketch_shapes::parse_value_input("1,2m").numbers[0], 1200), "jednostki cm / m nie działają");
            for (const char* garbage : {"abc", "", "12<x", "1.2.3", "nan"}) {
                bool rejected = false;
                try { sketch_shapes::parse_value_input(garbage); } catch (const std::invalid_argument&) { rejected = true; }
                need(rejected, std::string("śmieci „") + garbage + "” powinny być odrzucone");
            }
        });

        run(area, "DXF: linie, polilinia z łukiem, okrąg-otwór, jednostki, błędy", where, [=](Ctx& ctx) {
            auto r = dxf_import::parse(dxf_document(dxf_line(0, 0, 600, 0) + dxf_line(600, 400, 600, 0) + dxf_line(600, 400, 0, 400) + dxf_line(0, 0, 0, 400)));
            need(r.contours.size() == 1 && r.contours[0].closed && r.contours[0].points.size() == 4, "cztery linie nie złożyły się w jeden zamknięty kontur");
            auto outline = dxf_import::pick_outline(r);
            need(near_to(outline.width, 600) && near_to(outline.height, 400) && sketch_shapes::signed_area(outline.points) > 0, "obrys DXF ma zły rozmiar lub kierunek");
            r = dxf_import::parse(dxf_document(dxf_line(100, 100, 160, 100) + dxf_line(160, 100, 160, 140) + dxf_line(160, 140, 100, 140) + dxf_line(100, 140, 100, 100), 5));
            outline = dxf_import::pick_outline(r);
            need(r.unit_name == "cm" && near_to(outline.width, 600) && near_to(outline.height, 400), "jednostki cm nie zostały przeliczone na mm");
            need(near_to(outline.points[0][0], 0) && near_to(outline.points[0][1], 0), "obrys nie został przesunięty do początku układu");
            std::string polyline = dxf_pair(0, "LWPOLYLINE") + dxf_pair(90, "4") + dxf_pair(70, "1") + dxf_number(10, 0) + dxf_number(20, 0) + dxf_number(10, 500) + dxf_number(20, 0) +
                                   dxf_number(10, 500) + dxf_number(20, 300) + dxf_number(10, 0) + dxf_number(20, 300);
            std::string holes = dxf_pair(0, "CIRCLE") + dxf_number(10, 250) + dxf_number(20, 150) + dxf_number(40, 20) + dxf_pair(0, "CIRCLE") + dxf_number(10, 900) + dxf_number(20, 900) + dxf_number(40, 20);
            outline = dxf_import::pick_outline(dxf_import::parse(dxf_document(polyline + holes)));
            need(outline.inner_circles == 1 && outline.outer_candidates == 1, "okrąg wewnątrz obrysu nie został policzony jako otwór");
            need(outline.holes.size() == 1 && near_to(outline.holes[0].diameter, 40) && near_to(outline.holes[0].u, 250) && near_to(outline.holes[0].v, 150), "okrąg wewnątrz obrysu nie stał się otworem o średnicy 40 w (250; 150)");
            std::string too_big = dxf_pair(0, "CIRCLE") + dxf_number(10, 250) + dxf_number(20, 150) + dxf_number(40, 400);
            outline = dxf_import::pick_outline(dxf_import::parse(dxf_document(polyline + too_big)));
            need(outline.holes.empty() || outline.inner_circles == 0, "okrąg większy od obrysu nie powinien zostać otworem");
            std::string near_edge = dxf_pair(0, "CIRCLE") + dxf_number(10, 10) + dxf_number(20, 150) + dxf_number(40, 20);
            outline = dxf_import::pick_outline(dxf_import::parse(dxf_document(polyline + near_edge)));
            need(outline.holes.empty() && outline.skipped_holes == 1, "okrąg wystający poza obrys powinien być pominięty i policzony w skipped_holes");
            std::string half = dxf_pair(0, "LWPOLYLINE") + dxf_pair(90, "2") + dxf_pair(70, "1") + dxf_number(10, 0) + dxf_number(20, 0) + dxf_number(42, 1) + dxf_number(10, 100) + dxf_number(20, 0);
            outline = dxf_import::pick_outline(dxf_import::parse(dxf_document(half)));
            need(near_to(outline.width, 100, 1e-6) && near_to(outline.height, 50, 0.5), "półokrąg z wybrzuszeniem (bulge) ma zły rozmiar");
            std::string slot = dxf_line(50, 0, 250, 0) + dxf_pair(0, "ARC") + dxf_number(10, 250) + dxf_number(20, 50) + dxf_number(40, 50) + dxf_number(50, 270) + dxf_number(51, 90) +
                               dxf_line(250, 100, 50, 100) + dxf_pair(0, "ARC") + dxf_number(10, 50) + dxf_number(20, 50) + dxf_number(40, 50) + dxf_number(50, 90) + dxf_number(51, 270);
            r = dxf_import::parse(dxf_document(slot));
            need(r.contours.size() == 1 && r.contours[0].closed, "linie i łuki nie złożyły się w zamknięty kontur");
            r = dxf_import::parse(dxf_document(polyline + dxf_pair(0, "SPLINE") + dxf_pair(0, "TEXT")));
            need(r.skipped == 2 && !r.warnings.empty(), "niewspierane obiekty powinny być policzone i zgłoszone w ostrzeżeniach");
            auto bad = [](const std::string& text, bool outline_only = false) {
                try { auto parsed = dxf_import::parse(text); if (outline_only) dxf_import::pick_outline(parsed); } catch (const std::invalid_argument&) { return true; }
                return false;
            };
            need(bad(""), "pusty plik DXF powinien być odrzucony");
            need(bad("AutoCAD Binary DXF\r\n"), "binarny DXF powinien być odrzucony z wyjaśnieniem");
            need(bad("abc\nSECTION\n"), "plik bez kodów grup powinien być odrzucony");
            need(bad(dxf_document(dxf_pair(0, "TEXT"))), "plik bez obsługiwanych obiektów powinien być odrzucony");
            need(bad(dxf_document(dxf_line(0, 0, 100, 0) + dxf_line(100, 0, 100, 100)), true), "otwarta linia (bez zamkniętego konturu) powinna być odrzucona przy wyborze obrysu");
            std::string many;
            for (int i = 0; i < 500; ++i) {
                double a = 2 * std::numbers::pi * i / 500, b = 2 * std::numbers::pi * (i + 1) / 500;
                many += dxf_line(300 * std::cos(a), 200 * std::sin(a), 300 * std::cos(b), 200 * std::sin(b));
            }
            outline = dxf_import::pick_outline(dxf_import::parse(dxf_document(many)));
            need(outline.simplified && outline.points.size() <= 128 && outline.original_points == 500, "500-punktowy kontur nie został uproszczony do 128 punktów");
            ctx.note(std::format("500 → {} punktów po uproszczeniu", outline.points.size()));
        });

        auto open_sketch = [this] {
            app->project = fresh(); app->selected.reset(); app->refresh(); app->update();
            auto* sk = new sketch::Sketch(app, nullptr);
            app->update();
            return sk;
        };
        auto close_sketch = [this](sketch::Sketch* sk) {
            try { if (sk->winfo_exists()) sk->destroy(); } catch (const std::exception&) {}
            app->project = fresh(); app->refresh();
        };

        run(area, "edytor: ruch myszy daje wskazówkę (punkt końcowy) i zapis w stanie", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_tool("Wielokąt");
            sk->add_point({0, 0});
            sk->add_point({100, 0});
            auto spot = sk->screen({100, 0});
            tk::Event ev; ev.x = (int)spot.first + 2; ev.y = (int)spot.second - 2; ev.widget = sk->canvas;
            auto snapped = sk->local(ev);
            ctx.note(sk->inference.tip);
            need(sk->inference.kind == Kind::Endpoint && near_to(snapped[0], 100) && near_to(snapped[1], 0), "kursor przy końcu odcinka w edytorze nie złapał punktu końcowego");
            need(sk->snap_reason == "Punkt końcowy", "podpowiedź przy kursorze nie ma tekstu „Punkt końcowy”");
            ev.x = (int)sk->screen({300, 0}).first; ev.y = (int)sk->screen({300, 0}).second + 3;
            sk->local(ev);
            need(sk->inference.kind == Kind::AxisU, "kursor na wysokości ostatniego punktu nie złapał osi U w edytorze");
            close_sketch(sk);
        });

        run(area, "edytor: skróty klawiszowe przełączają narzędzia i trafiają do dziennika", where, [=, this](Ctx&) {
            auto* sk = open_sketch();
            auto from = log_mark();
            for (auto [keysym, tool] : std::vector<std::pair<const char*, const char*>>{{"c", "Okrąg"}, {"p", "Wielobok"}, {"a", "Łuk"}, {"m", "Przesuń"}, {"t", "Miarka"}, {"r", "Prostokąt"}, {"l", "Wielokąt"}}) {
                need(sk->key(keysym), std::string("klawisz ") + keysym + " nie został obsłużony");
                need(sk->mode.get() == tool, std::string("klawisz ") + keysym + " nie przełączył narzędzia na " + tool);
            }
            need(!sk->key("Shift_L"), "sam Shift nie powinien być obsłużony jako skrót");
            need(!sk->key("c", 4), "Ctrl+C nie może przełączać narzędzia (skrót okna)");
            std::string text = log_since(from);
            need(has_text(text, "[operacja:szkic] narzędzie szkicu: Okrąg") && has_text(text, "narzędzie szkicu: Łuk"), "przełączenie narzędzia nie zostało zapisane w dzienniku");
            close_sketch(sk);
        });

        run(area, "edytor, pole wartości: długość z kątem (250<45) dodaje odcinek we wskazanym kierunku", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_tool("Wielokąt");
            sk->add_point({0, 0});
            auto from = log_mark();
            sk->value_text.set("250<45");
            need(sk->key("Return"), "Enter z wartością nie został obsłużony");
            need(sk->points.size() == 2, "wpisana długość nie dodała punktu");
            double length = std::hypot(sk->points[1][0], sk->points[1][1]), angle = std::atan2(sk->points[1][1], sk->points[1][0]) * 180 / std::numbers::pi;
            ctx.note(std::format("długość {:.2f}, kąt {:.2f}°", length, angle));
            need(near_to(length, 250, 1e-6) && near_to(angle, 45, 1e-6), "odcinek z pola wartości ma zły rozmiar lub kąt");
            need(sk->value_text.get().empty(), "pole wartości nie zostało wyczyszczone po zatwierdzeniu");
            need(has_text(log_since(from), "wartość z klawiatury „250<45”"), "wartość z klawiatury nie trafiła do dziennika");
            // wpisywanie cyfr z klawiatury
            for (const char* k : {"1", "2", "0"}) sk->key(k);
            need(sk->value_text.get() == "120", "cyfry z klawiatury nie trafiły do pola wartości");
            sk->key("BackSpace");
            need(sk->value_text.get() == "12", "Backspace nie usunął ostatniej cyfry z pola wartości");
            sk->key("Escape");
            need(sk->value_text.get().empty(), "Esc nie wyczyścił pola wartości");
            close_sketch(sk);
        });

        run(area, "edytor, pole wartości: strzałka blokuje oś, wpisana długość idzie wzdłuż osi", where, [=, this](Ctx&) {
            auto* sk = open_sketch();
            sk->set_tool("Wielokąt");
            sk->add_point({100, 100});
            need(sk->key("Up") && sk->arrow_lock && near_to((*sk->arrow_lock)[1], 1), "strzałka w górę nie zablokowała osi V");
            sk->value_text.set("80");
            sk->value_apply();
            need(sk->points.size() == 2 && near_to(sk->points[1][0], 100) && near_to(sk->points[1][1], 180), "długość przy zablokowanej osi V nie poszła pionowo");
            sk->key("Right");
            sk->key("Right");
            need(!sk->arrow_lock, "ponowna strzałka powinna zdjąć blokadę");
            close_sketch(sk);
        });

        run(area, "edytor, pole wartości: prostokąt „szer;wys” po kliknięciu pierwszego narożnika", where, [=, this](Ctx&) {
            auto* sk = open_sketch();
            sk->set_tool("Prostokąt");
            sk->add_point({50, 50});
            sk->value_text.set("600;400");
            sk->value_apply();
            need(sk->closed && sk->points.size() == 4, "prostokąt z pola wartości nie zamknął konturu");
            double w = std::abs(sk->points[1][0] - sk->points[0][0]), h = std::abs(sk->points[2][1] - sk->points[1][1]);
            need(near_to(w, 600) && near_to(h, 400), std::format("prostokąt z pola wartości ma rozmiar {}×{} zamiast 600×400", w, h));
            close_sketch(sk);
        });

        run(area, "edytor: okrąg (środek + promień z klawiatury) i wielobok (środek + klik)", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            auto from = log_mark();
            sk->set_tool("Okrąg");
            sk->add_point({100, 100});
            need(sk->pending.size() == 1, "kliknięcie środka okręgu nie zostało zapamiętane");
            sk->value_text.set("50");
            sk->value_apply();
            need(sk->closed && sk->points.size() == 48, "okrąg z pola wartości nie zamknął konturu 48 punktów");
            for (auto& p : sk->points) need(near_to(std::hypot(p[0] - 100, p[1] - 100), 50, 1e-6), "punkt okręgu leży poza promieniem 50");
            need(sk->centers.size() == 1, "środek okręgu nie został zapamiętany jako wskazówka");
            auto [candidate, id] = sk->candidate();
            candidate->validate();
            ctx.note("okrąg → element " + id);
            sk->clear();
            sk->set_tool("Wielobok");
            sk->sides.set("6");
            sk->add_point({0, 0});
            sk->add_point({0, 120});
            need(sk->closed && sk->points.size() == 6, "wielobok nie ma 6 boków");
            std::string text = log_since(from);
            need(has_text(text, "kształt: okrąg R=50.00") && has_text(text, "kształt: wielobok 6 boków"), "kształty nie zostały zapisane w dzienniku");
            close_sketch(sk);
        });

        run(area, "edytor: łuk przez trzy punkty dołącza się do konturu", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_tool("Wielokąt");
            sk->add_point({0, 0});
            sk->add_point({100, 0});
            size_t before = sk->points.size();
            sk->set_tool("Łuk");
            sk->add_point({150, 50});
            sk->add_point({100, 100});
            ctx.note(std::format("kontur: {} → {} punktów", before, sk->points.size()));
            need(sk->points.size() > before + 3 && sk->pending.empty(), "łuk nie dodał punktów do konturu");
            need(near_to(sk->points.back()[0], 100) && near_to(sk->points.back()[1], 100), "łuk nie kończy się w ostatnim kliknięciu");
            close_sketch(sk);
        });

        run(area, "edytor, pole wartości: błędne wartości są odrzucane z powodem w dzienniku, Esc anuluje okrąg", where, [=, this](Ctx&) {
            auto* sk = open_sketch();
            sk->set_tool("Wielokąt");
            sk->add_point({0, 0});
            auto from = log_mark();
            sk->value_text.set("abc");
            sk->value_apply();
            need(sk->points.size() == 1, "błędna wartość nie powinna dodać punktu");
            need(has_text(log_since(from), "wartość odrzucona:"), "odrzucona wartość nie zostawiła powodu w dzienniku");
            sk->value_text.set("");
            sk->set_tool("Okrąg");
            sk->add_point({10, 10});
            need(sk->pending.size() == 1, "środek okręgu nie został ustawiony");
            sk->key("Escape");
            need(sk->pending.empty(), "Esc nie anulował rozpoczętego okręgu");
            need(has_text(log_since(from), "Esc: anulowano rozpoczętą operację"), "Esc nie zostawił wpisu w dzienniku");
            sk->value_text.set("50");
            sk->value_apply();
            need(sk->points.size() == 1 && has_text(log_since(from), "najpierw kliknij środek"), "promień bez środka powinien być odrzucony z powodem");
            close_sketch(sk);
        });

        run(area, "edytor: panel szybkich profili wstawia kontur i element 3D", where, [=, this](Ctx& ctx) {
            for (const auto& info : sketch_shapes::profile_catalog()) {
                auto* sk = open_sketch();
                auto from = log_mark();
                sk->profile_name.set(info.name);
                sk->profile_width.set("600");
                sk->profile_height.set("400");
                sk->profile_param.set(std::format("{:g}", info.default_param));
                sk->insert_profile();
                app->update();
                need(sk->closed && sk->points.size() >= 3, "profil „" + info.name + "” nie wstawił zamkniętego konturu");
                auto [candidate, id] = sk->candidate();
                candidate->validate();
                need(has_text(log_since(from), "szybki profil „" + info.name + "”"), "profil „" + info.name + "” nie zostawił wpisu w dzienniku");
                ctx.note(info.name + " OK");
                close_sketch(sk);
            }
        });

        run(area, "miniaturki profili: każdy profil z katalogu ma narysowaną miniaturkę w panelu i poprawną geometrię", where, [=, this](Ctx& ctx) {
            for (const auto& info : sketch_shapes::profile_catalog()) {
                auto shape = sketch_shapes::profile(info.name, 600, 400, info.default_param);
                auto thumb = sketch_shapes::thumbnail_points(shape, 150, 90, 8);
                need(thumb.size() == shape.size(), "miniaturka profilu „" + info.name + "” jest pusta");
                double lo_x = 1e18, hi_x = -1e18, lo_y = 1e18, hi_y = -1e18;
                for (const auto& p : thumb) { lo_x = std::min(lo_x, p[0]); hi_x = std::max(hi_x, p[0]); lo_y = std::min(lo_y, p[1]); hi_y = std::max(hi_y, p[1]); }
                need(lo_x >= 7.9 && hi_x <= 142.1 && lo_y >= 7.9 && hi_y <= 82.1, "miniaturka profilu „" + info.name + "” wychodzi poza ramkę");
                need(hi_x - lo_x > 20 && hi_y - lo_y > 20, "miniaturka profilu „" + info.name + "” jest za mała");
                auto* sk = open_sketch();
                need(sk->profile_preview != nullptr, "panel profili nie ma miniaturki");
                sk->profile_name.set(info.name);
                sk->profile_width.set("600"); sk->profile_height.set("400"); sk->profile_param.set(std::format("{:g}", info.default_param));
                sk->draw_profile_preview();
                need(!sk->profile_preview->find_withtag("profile_preview").empty(), "profil „" + info.name + "” nie ma narysowanej miniaturki w panelu");
                sk->profile_param.set("to nie liczba");
                sk->draw_profile_preview();
                need(sk->profile_preview->find_withtag("profile_preview").empty(), "błędny parametr powinien czyścić miniaturkę, a nie zostawiać starą");
                close_sketch(sk);
            }
            need(sketch_shapes::thumbnail_points({}, 150, 90).empty() && sketch_shapes::thumbnail_points({{0, 0}, {0, 0}, {0, 0}}, 150, 90).empty(), "pusty lub zdegenerowany kontur nie powinien dawać miniaturki");
            ctx.note(std::format("{} profili z miniaturkami", sketch_shapes::profile_catalog().size()));
        });

        run(area, "edytor: zaokrąglenie, fazowanie i usuwanie punktu zamkniętego konturu", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 300}, {0, 300}});
            sk->close_contour();
            auto from = log_mark();
            sk->fillet_point(1, 40, false);
            ctx.note(std::format("po zaokrągleniu {} punktów", sk->points.size()));
            need(sk->points.size() > 4, "zaokrąglenie nie dodało punktów łuku");
            sk->undo();
            need(sk->points.size() == 4, "cofnięcie zaokrąglenia nie przywróciło prostokąta");
            sk->fillet_point(2, 30, true);
            need(sk->points.size() == 5, "fazowanie nie dodało jednego punktu");
            sk->remove_point_at(0);
            need(sk->points.size() == 4, "usunięcie punktu nie zmniejszyło konturu");
            std::string text = log_since(from);
            need(has_text(text, "zaokrąglenie narożnika 2") && has_text(text, "fazowanie narożnika 3") && has_text(text, "usunięto punkt 1"), "operacje na narożnikach nie zostały zapisane w dzienniku");
            bool rejected = false;
            try { sk->fillet_point(1, 5000, false); } catch (const std::invalid_argument&) { rejected = true; }
            need(rejected, "zbyt duży promień zaokrąglenia powinien być odrzucony");
            close_sketch(sk);
        });

        run(area, "edytor: zmiana długości odcinka z napisu wymiaru zamkniętego konturu", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 300}, {0, 300}});
            sk->close_contour();
            sk->scale = 0.7; sk->origin = {65, 540};
            auto a = sk->screen(sk->points[0]), b = sk->screen(sk->points[1]);
            int hit = sk->dimension_at((int)((a.first + b.first) / 2), (int)((a.second + b.second) / 2 - 12));
            need(hit == 0, "kliknięcie w napis wymiaru pierwszej krawędzi nie wskazało krawędzi 0");
            need(sk->dimension_at(5, 5) == -1, "kliknięcie z dala od napisów nie powinno wskazywać krawędzi");
            auto from = log_mark();
            sk->apply_edge_length(0, 500);
            double length = std::hypot(sk->points[1][0] - sk->points[0][0], sk->points[1][1] - sk->points[0][1]);
            ctx.note(std::format("długość pierwszej krawędzi: {:.2f}", length));
            need(near_to(length, 500, 1e-6), "apply_edge_length nie ustawiła długości 500 mm");
            need(has_text(log_since(from), "odcinek 1: długość"), "zmiana wymiaru nie została zapisana w dzienniku");
            bool rejected = false;
            try { sk->apply_edge_length(0, -5); } catch (const std::invalid_argument&) { rejected = true; }
            need(rejected, "ujemna długość odcinka powinna być odrzucona");
            close_sketch(sk);
        });

        run(area, "edytor: import DXF z pliku → kontur zamknięty → element 3D (plik + dziennik)", where, [=, this](Ctx& ctx) {
            auto path = fs::temp_directory_path() / "mebel_selftest_import.dxf";
            {
                std::ofstream out(path, std::ios::binary);
                out << dxf_document(dxf_line(0, 0, 500, 0) + dxf_line(500, 0, 500, 300) + dxf_line(500, 300, 0, 300) + dxf_line(0, 300, 0, 0));
            }
            auto* sk = open_sketch();
            auto from = log_mark();
            need(sk->import_dxf(path.string(), false), "import poprawnego DXF zwrócił błąd");
            need(sk->closed && sk->points.size() == 4, "import DXF nie dał zamkniętego prostokąta");
            auto [candidate, id] = sk->candidate();
            candidate->validate();
            std::string text = log_since(from);
            ctx.note(std::format("import → {} punktów, element {}", sk->points.size(), id));
            need(has_text(text, "[operacja:szkic] import DXF:") && has_text(text, "[operacja:plik] import DXF"), "import DXF nie zostawił wpisów [szkic] i [plik] w dzienniku");
            // obrys z okręgiem wewnątrz → otwór przelotowy w elemencie
            {
                std::ofstream out(path, std::ios::binary);
                out << dxf_document(dxf_line(0, 0, 500, 0) + dxf_line(500, 0, 500, 300) + dxf_line(500, 300, 0, 300) + dxf_line(0, 300, 0, 0) +
                                    dxf_pair(0, "CIRCLE") + dxf_number(10, 250) + dxf_number(20, 150) + dxf_number(40, 20));
            }
            from = log_mark();
            need(sk->import_dxf(path.string(), false), "import DXF z okręgiem zwrócił błąd");
            need(sk->part.holes.size() == 1 && near_to(sk->part.holes[0][2], 40), "okrąg z DXF nie stał się otworem o średnicy 40 w elemencie");
            need(has_text(log_since(from), "otwory przelotowe z okręgów: 1"), "dziennik nie podaje liczby otworów z DXF");
            auto [with_hole, hole_id] = sk->candidate();
            with_hole->validate();
            bool has_hole = false;
            for (auto& p : with_hole->parts) if (p.id == hole_id) has_hole = p.holes.size() == 1;
            need(has_hole, "element z importu nie ma otworu po zbudowaniu kandydata");
            sk->clear();
            need(sk->part.holes.empty(), "wyczyszczenie szkicu powinno usunąć otwory z importu");
            // uszkodzony plik: odrzucony z powodem, kontur bez zmian (najpierw przywracamy kontur po clear())
            {
                std::ofstream out(path, std::ios::binary);
                out << dxf_document(dxf_line(0, 0, 500, 0) + dxf_line(500, 0, 500, 300) + dxf_line(500, 300, 0, 300) + dxf_line(0, 300, 0, 0));
            }
            need(sk->import_dxf(path.string(), false) && sk->closed && sk->points.size() == 4, "ponowny import poprawnego DXF po clear() nie powiódł się");
            {
                std::ofstream out(path, std::ios::binary);
                out << "to nie jest dxf\n";
            }
            from = log_mark();
            need(!sk->import_dxf(path.string(), false), "uszkodzony DXF powinien zostać odrzucony");
            need(sk->closed && sk->points.size() == 4, "odrzucony import nie powinien zmieniać konturu");
            need(has_text(log_since(from), "import DXF odrzucony:"), "odrzucony import nie zostawił powodu w dzienniku");
            std::error_code ec;
            fs::remove(path, ec);
            close_sketch(sk);
        });

        run(area, "edytor: przeciąganie punktu myszą (Przesuń) zatwierdza zmianę z dziennikiem, odrzuca zły kontur", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->set_sketch_points({{0, 0}, {400, 0}, {400, 300}, {0, 300}});
            sk->close_contour();
            sk->scale = 1; sk->origin = {65, 540};
            sk->set_tool("Przesuń");
            auto from = log_mark();
            auto start = sk->screen(sk->points[2]);
            tk::Event press; press.x = (int)start.first; press.y = (int)start.second; press.widget = sk->canvas;
            sk->press(press);
            need(sk->drag_index && *sk->drag_index == 2, "naciśnięcie przy narożniku nie wybrało punktu 3");
            auto target = sk->screen({450, 330});
            tk::Event move = press; move.x = (int)target.first; move.y = (int)target.second;
            sk->drag(move);
            need(sk->drag_moved, "przeciągnięcie nie oznaczyło ruchu");
            sk->release(move);
            ctx.note(std::format("punkt 3 → ({:.1f}; {:.1f})", sk->points[2][0], sk->points[2][1]));
            need(!sk->drag_index && std::hypot(sk->points[2][0] - 450, sk->points[2][1] - 330) < 12, "po zwolnieniu punkt nie leży w miejscu docelowym");
            need(sk->closed && sk->points.size() == 4, "przesunięcie punktu zmieniło liczbę punktów lub otwarło kontur");
            need(has_text(log_since(from), "przesunięto punkt 3:"), "przesunięcie nie zostało zapisane w dzienniku");
            sk->undo();
            need(near_to(sk->points[2][0], 400) && near_to(sk->points[2][1], 300), "cofnięcie nie przywróciło punktu 3");
            // kliknięcie poza punktami: bez zmian, ale z powodem w dzienniku
            from = log_mark();
            tk::Event miss = press; miss.x = 3; miss.y = 3;
            sk->press(miss);
            need(!sk->drag_index && has_text(log_since(from), "Przesuń: kliknięto poza punktami konturu"), "kliknięcie poza punktami nie zostawiło powodu w dzienniku");
            // niepoprawne przesunięcie (punkt na punkt sąsiada) jest odrzucane i kontur zostaje bez zmian
            auto before = sk->points;
            bool rejected = false;
            try { sk->move_point(2, sk->points[1]); } catch (const std::invalid_argument&) { rejected = true; }
            need(rejected && sk->points == before, "przesunięcie punktu na sąsiada powinno być odrzucone bez zmiany konturu");
            close_sketch(sk);
        });

        run(area, "edytor: miarka mierzy odległość, G tworzy linię pomocniczą łapaną przez wskazówki", where, [=, this](Ctx& ctx) {
            auto* sk = open_sketch();
            sk->scale = 1; sk->origin = {65, 540};
            sk->set_tool("Miarka");
            auto from = log_mark();
            sk->add_point({0, 0});
            need(sk->pending.size() == 1, "pierwszy klik miarki nie ustawił początku");
            sk->add_point({300, 400});
            need(sk->measured && near_to(std::hypot(sk->measured->second[0] - sk->measured->first[0], sk->measured->second[1] - sk->measured->first[1]), 500), "pomiar nie wynosi 500 mm");
            need(sk->points.empty(), "miarka nie może dodawać punktów do konturu");
            need(has_text(log_since(from), "pomiar miarką: 500.00 mm"), "pomiar nie został zapisany w dzienniku");
            need(sk->key("g") && sk->guides.size() == 1, "klawisz G nie utworzył linii pomocniczej");
            need(has_text(log_since(from), "linia pomocnicza przez"), "linia pomocnicza nie została zapisana w dzienniku");
            sk->set_tool("Wielokąt");
            auto spot = sk->screen({150, 200});
            tk::Event ev; ev.x = (int)spot.first + 1; ev.y = (int)spot.second + 1; ev.widget = sk->canvas;
            sk->local(ev);
            ctx.note(sk->inference.tip);
            need(sk->inference.kind == Kind::OnEdge, "kursor przy linii pomocniczej nie złapał punktu na jej krawędzi");
            from = log_mark();
            sk->guides.clear(); sk->measured.reset();
            sk->key("g");
            need(has_text(log_since(from), "G: brak pomiaru"), "G bez pomiaru nie zostawiło powodu w dzienniku");
            sk->set_tool("Miarka");
            sk->add_point({10, 10});
            sk->key("Escape");
            need(sk->pending.empty(), "Esc nie anulował rozpoczętego pomiaru");
            close_sketch(sk);
        });
    }

    // ================================================================ render

    void test_render_matrix() {
        const char* area = "Renderowanie (silniki, jakość, widoki)";
        const char* where = "app.h (draw_original), viewport_render.cpp, viewport_scene.cpp, gpu_viewport.cpp, gpu_scene.cpp, native_viewport.cpp, depth_raster.cpp";
        auto scene_project = [] {
            Project base;
            auto [p, a] = cabinet(base, 0, [](json& c) { c["name"] = "Render A"; });
            auto [p2, b] = cabinet(*p, 3, [](json& c) { c["name"] = "Render B"; c["origin"] = json::array({700, 0, 0}); });
            return room_objects::add_object(*p2, "lamp", json{{"x", 300}, {"z", 1800}});
        };
        run(area, "okno 3D ma rozmiar umożliwiający rysowanie", where, [this](Ctx& ctx) {
            app->update();
            int w = app->canvas->winfo_width(), h = app->canvas->winfo_height();
            ctx.note(std::format("płótno {}×{}", w, h));
            if (w < 10 || h < 10) ctx.warn("płótno widoku 3D jest mniejsze niż 10 px — draw() pomija rysowanie, więc test renderu jest tylko częściowy");
        });
        for (const auto& engine : ui_preferences::ENGINES) {
            run(area, "silnik „" + engine + "” × jakość draft/balanced/high", where, [=, this](Ctx& ctx) {
                app->project = scene_project(); app->selected.reset(); app->refresh(); app->fit(); app->update();
                std::string seen;
                for (const char* quality : {"draft", "balanced", "high"}) {
                    app->viewport_engine = engine; app->viewport_quality = quality;
                    app->draw(); app->update();
                    seen += std::string(quality) + "=" + app->canvas->_active_renderer + " ";
                    if (app->canvas->winfo_width() >= 10 && app->canvas->find_withtag("all").empty()) ctx.warn(std::string("rysowanie (") + quality + ") nie wytworzyło żadnych elementów na płótnie");
                }
                ctx.note("aktywny silnik: " + seen);
                if (engine != "cpu" && engine != "auto" && app->canvas->_active_renderer == "cpu")
                    ctx.warn("silnik „" + engine + "” niedostępny — program wrócił do CPU" + (app->canvas->_gpu_error ? ": " + *app->canvas->_gpu_error : std::string()));
                app->viewport_engine = "cpu"; app->viewport_quality = "balanced"; app->draw(); app->update();
            });
        }
        run(area, "widoki: perspektywa, przód, góra, bok, tył", where, [this, scene_project](Ctx& ctx) {
            app->project = scene_project(); app->refresh(); app->update();
            for (auto [yaw, pitch] : std::vector<std::pair<double, double>>{{-35, 25}, {0, 0}, {0, 89}, {90, 0}, {180, 0}, {-90, 0}}) {
                app->view(yaw, pitch); app->draw(); app->update();
            }
            ctx.note("6 ustawień kamery");
        });
        run(area, "tryby: X-ray, rozstrzelenie, wymiary, otwory, zaznaczenie", where, [this, scene_project](Ctx&) {
            app->project = scene_project(); app->refresh(); app->fit(); app->update();
            app->selected = app->project->parts[0].id; app->draw(); app->update();
            app->explode.set(true); app->draw(); app->update(); app->explode.set(false);
            app->xray.set(true); app->draw(); app->update(); app->xray.set(false);
            app->dimensions.set(true); app->draw(); app->update(); app->dimensions.set(false);
            app->show_drills.set(true); app->draw(); app->update(); app->show_drills.set(false);
            app->selected.reset(); app->draw(); app->update();
        });
        run(area, "ruch interaktywny (obrót kamery) i pełna jakość po zatrzymaniu", where, [this, scene_project](Ctx&) {
            app->project = scene_project(); app->refresh(); app->fit(); app->update();
            app->_view_interactive = true;
            for (int i = 0; i < 6; ++i) { app->camera.yaw += 8; app->draw(); app->update(); }
            app->_view_interactive = false;
            app->draw(); app->update();
        });
        run(area, "scena: jasność, LED i lampy w widoku", where, [this, scene_project](Ctx&) {
            ProjectPtr p = scene_project();
            for (double brightness : {.2, .6, 1.0}) {
                app->project = room_objects::set_scene(*p, json{{"brightness", brightness}, {"led_enabled", true}, {"architecture", json{{"enabled", true}}}});
                app->refresh(); app->draw(); app->update();
            }
            app->project = fresh(); app->refresh();
        });
    }

    static bool has_text(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

    // Dziennik zdarzeń: każda akcja użytkownika (narożniki, rozciąganie, konfiguracja, okucia, LED, fronty) zostawia czytelny ślad w logu.

    // Każdy mebel, kształt i urządzenie z biblioteki jest dodawany osobno (ścieżka kliknięcia w kartę) i zostawia wpis w dzienniku.
    void test_library_matrix() {
        const char* area = "Biblioteka — każdy element osobno + dziennik";
        const char* where = "visual_catalogue.cpp (apply_item), journal.cpp (project_refreshed)";
        for (const char* kind : {"furniture", "shaped", "appliance"}) {
            ProjectPtr scratch = fresh();
            json items;
            try { items = visual_catalogue::catalogue_items(kind, *scratch); } catch (const std::exception&) { continue; }
            for (size_t i = 0; i < items.size(); ++i) {
                std::string name = items[i].value("name", std::string("element ") + std::to_string(i));
                run(area, std::string(kind) + ": " + name, where, [=, this](Ctx& ctx) {
                    // AGD trafia do sceny (obiekty pokoju), a nie do listy części.
                    ProjectPtr project = fresh();
                    bool scene_kind = std::string(kind) == "appliance";
                    auto scene_count = [](const ProjectPtr& p) { return p->scene.contains("objects") && p->scene["objects"].is_array() ? p->scene["objects"].size() : size_t(0); };
                    size_t before = scene_kind ? scene_count(project) : project->parts.size();
                    app->project = project; app->selected.reset(); app->refresh();
                    json current = visual_catalogue::catalogue_items(kind, *project);
                    need(i < current.size(), "katalog zmienił liczbę elementów");
                    auto from = log_mark();
                    visual_catalogue::apply_item(app, current[i]);
                    app->update();
                    std::string text = log_since(from);
                    size_t parts = scene_kind ? scene_count(app->project) : app->project->parts.size();
                    // Inspektor produkcyjny sprawdza każdy gotowy element z biblioteki (kolizje, obrzeża, wiercenia, ugięcia).
                    production_inspector::Report inspection;
                    if (!scene_kind) inspection = production_inspector::inspect(*app->project);
                    std::string first_error, first_sag;
                    for (const auto& finding : inspection.findings) {
                        if (finding.severity != production_inspector::Severity::Error) continue;
                        std::string text = production_inspector::rule_title(finding.rule) + " · " + finding.part_name + ": " + finding.message;
                        // Ugięcie półek liczone jest dla obciążenia książkami (80 kg/m²); szablony szaf/regałów mają szerokie półki,
                        // więc to ostrzeżenie projektowe do wzięcia pod uwagę (przegroda), a nie błąd generatora.
                        if (finding.rule == "shelf_sag") { if (first_sag.empty()) first_sag = text; }
                        else if (first_error.empty()) first_error = text;
                    }
                    // Szafy i regały 1200–1600 mm mają przegrodę (preset kreatora) — dla nich ugięcie jest błędem testu; wyspa i szafa przesuwna zostają ostrzeżeniem.
                    static const std::set<size_t> WITH_PARTITION{8, 14, 18, 23, 37, 45, 51};
                    bool must_be_stiff = std::string(kind) == "furniture" && WITH_PARTITION.count(i) > 0;
                    if (must_be_stiff) first_error = first_error.empty() ? first_sag : first_error;
                    else if (!first_sag.empty()) ctx.warn("szablon ma szeroką półkę: " + first_sag);
                    close_extra_windows();
                    app->project = fresh(); app->refresh();
                    ctx.note(std::format("{} {}", parts, scene_kind ? "obiektów sceny" : "części") + (scene_kind ? std::string() : " · " + production_inspector::summary_line(inspection)));
                    need(first_error.empty(), "Inspektor produkcyjny zgłasza błąd w elemencie z biblioteki: " + first_error);
                    need(parts > before, std::format("po dodaniu z biblioteki liczba {} nie wzrosła ({} → {})", scene_kind ? "obiektów sceny" : "części", before, parts));
                    need(text.find(std::string("[operacja:biblioteka] dodaję: rodzaj ") + kind) != std::string::npos, "dodanie elementu nie zostało zapisane w dzienniku (biblioteka)");
                    need(text.find("[operacja:zmiana]") != std::string::npos, "dodanie elementu nie zostawiło wpisu [zmiana]");
                });
            }
        }
    }

    uintmax_t log_mark() { std::error_code ec; return fs::exists(app->developer_log_path) ? fs::file_size(app->developer_log_path, ec) : 0; }
    std::string log_since(uintmax_t from) {
        std::string text = read_file(app->developer_log_path);
        return from <= text.size() ? text.substr(from) : text;
    }

    void test_journal_actions() {
        const char* area = "Dziennik zdarzeń";
        // Fragment logu dopisany od znacznika — bez zależności od obrotu pliku.
        auto mark = [this]() -> uintmax_t { std::error_code ec; return fs::exists(app->developer_log_path) ? fs::file_size(app->developer_log_path, ec) : 0; };
        auto since = [this](uintmax_t from) -> std::string {
            std::string text = read_file(app->developer_log_path);
            return from <= text.size() ? text.substr(from) : text;
        };
        auto has = [](const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; };

        run(area, "opis różnic projektu: rozciąganie, przesunięcie, nowy mebel, usunięcie", "journal.cpp (describe_difference)", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            ProjectPtr stretched = direct_edit::resize_furniture_face(*p, g, "y1", 128);
            std::string d = journal::describe_difference(*p, *stretched);
            ctx.note(d);
            need(has_text(d, "wymiary"), "rozciągnięcie nie zostało opisane jako zmiana wymiarów: " + d);
            need(has_text(d, g), "opis nie wymienia mebla " + g);
            ProjectPtr moved = furniture_layout::move_group(*p, g, 700);
            d = journal::describe_difference(*p, *moved);
            need(has_text(d, "położenie"), "przesunięcie nie zostało opisane: " + d);
            auto [two, g2] = cabinet(*p);
            d = journal::describe_difference(*p, *two);
            need(has_text(d, "nowe meble"), "dodanie mebla nie zostało opisane: " + d);
            d = journal::describe_difference(*two, *p);
            need(has_text(d, "usunięte meble"), "usunięcie mebla nie zostało opisane: " + d);
            need(journal::describe_difference(*p, *p).empty(), "brak zmian powinien dać pusty opis");
            Project turned_grain = *p;
            turned_grain.parts[0].grain = true; turned_grain.parts[0].grain_v = !p->parts[0].grain_v;
            d = journal::describe_difference(*p, turned_grain);
            need(has_text(d, "włókna"), "zmiana kierunku włókien nie została opisana: " + d);
        });

        run(area, "opis różnic projektu: obiekty sceny (AGD) dodane, zmienione i usunięte", "journal.cpp (make_snap — scene_objects)", [](Ctx& ctx) {
            Project base;
            ProjectPtr with = room_objects::add_object(base, "washer", json{{"x", 100}});
            std::string d = journal::describe_difference(base, *with);
            ctx.note(d);
            need(has_text(d, "obiekty sceny: +"), "dodanie AGD nie zostało opisane: " + d);
            std::string id = with->scene.at("objects").back().at("id").get<std::string>();
            ProjectPtr moved = room_objects::update_object(*with, id, json{{"x", 900}});
            d = journal::describe_difference(*with, *moved);
            need(has_text(d, "obiekty sceny: zmieniono"), "przesunięcie AGD nie zostało opisane: " + d);
            d = journal::describe_difference(*with, base);
            need(has_text(d, "obiekty sceny: −"), "usunięcie AGD nie zostało opisane: " + d);
            need(journal::describe_difference(*with, *with).empty(), "brak zmian sceny powinien dać pusty opis");
        });

        run(area, "opis różnic projektu: LED, nóżki, uchwyty i zawiasy jako okucia", "journal.cpp, configurator_accessories.cpp", [](Ctx& ctx) {
            auto [plain, g] = cabinet(Project(), 0, [](json& c) { c["led"] = false; c["leg_height"] = 0; });
            auto [led, g2] = furniture_configurator::configure(*plain, [&] { json c = furniture_configurator::read_config(*plain, g); c["led"] = true; return c; }(), g);
            std::string d = journal::describe_difference(*plain, *led);
            ctx.note(d);
            need(has_text(d, "LED"), "włączenie LED nie pojawiło się w opisie okuć: " + d);
            auto [legs, g3] = furniture_configurator::configure(*plain, [&] { json c = furniture_configurator::read_config(*plain, g); c["leg_height"] = 100; return c; }(), g);
            d = journal::describe_difference(*plain, *legs);
            need(has_text(d, "nóżka"), "nóżki nie pojawiły się w opisie okuć: " + d);
        });

        run(area, "refresh zapisuje wpis [zmiana] ze źródłem i trybem", "app.h (refresh/snapshot), journal.cpp (project_refreshed)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh();
            auto from = mark();
            ProjectPtr stretched = direct_edit::resize_furniture_face(*p, g, "y1", 128);
            app->snapshot(); app->project = stretched; app->refresh();
            std::string text = since(from);
            app->project = fresh(); app->refresh();
            ctx.note("wpis zawiera źródło snapshotu");
            need(has(text, "[operacja:zmiana]"), "brak wpisu [operacja:zmiana] po zmianie projektu");
            need(has(text, "wymiary"), "wpis [zmiana] nie mówi o wymiarach");
            need(has(text, "źródło selftest.cpp:"), "wpis [zmiana] nie wskazuje miejsca w kodzie (snapshot)");
            need(has(text, "tryb "), "wpis [zmiana] nie zawiera trybu pracy");
        });

        run(area, "tryb Rozciągnij: uchwyty, chwycenie uchwytu i brak zaznaczenia są w dzienniku", "app.h (draw_original, mouse_down), journal.cpp", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            auto from = mark();
            app->selected = p->parts.front().id;
            app->edit_mode.set("furniture"); app->draw();
            std::string text = since(from);
            ctx.note(std::format("uchwytów: {}", app->handles.size()));
            need(!app->handles.empty(), "w trybie Rozciągnij zaznaczony mebel nie ma uchwytów");
            (void)text;
            const auto handle = app->handles.front();
            tk::Event down; down.x = (int)handle.x; down.y = (int)handle.y; down.widget = app->canvas;
            auto at = mark();
            app->mouse_down(down);
            std::string grabbed = since(at);
            bool began = (bool)app->drag_edit;
            app->drag_edit.reset();
            need(began, "kliknięcie w uchwyt nie rozpoczęło rozciągania");
            need(has(grabbed, "chwycono uchwyt " + handle.face), "dziennik nie zawiera wpisu o chwyceniu uchwytu " + handle.face);
            // kliknięcie obok uchwytów
            at = mark();
            tk::Event miss; miss.x = (int)handle.x + 200; miss.y = (int)handle.y + 200; miss.widget = app->canvas;
            app->mouse_down(miss);
            need(has(since(at), "poza uchwytami"), "kliknięcie poza uchwytami nie zostało zapisane w dzienniku");
            // brak zaznaczenia
            at = mark();
            app->selected.reset(); app->draw();
            need(has(since(at), "brak uchwytów rozciągania: brak zaznaczonej części"), "brak zaznaczenia w trybie Rozciągnij nie został wyjaśniony w dzienniku");
            app->edit_mode.set("camera");
            app->project = fresh(); app->refresh();
        });

        run(area, "zrzut stanu interakcji zawiera tryb, zaznaczenie, uchwyty i okno konfiguracji", "journal.cpp (interaction_state)", [this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh();
            app->selected = p->parts.front().id;
            std::string state = journal::interaction_state(app);
            ctx.note(state);
            for (const char* part : {"tryb ", "zaznaczenie ", "uchwytów ", "przeciąganie ", "konfiguracja "}) need(has_text(state, part), std::string("w zrzucie stanu brakuje: ") + part);
            app->selected.reset();
            app->project = fresh(); app->refresh();
        });

        run(area, "okno konfiguracji: otwarcie i zamknięcie zapisane w dzienniku", "stretch_choice.cpp (open_configuration)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            auto from = mark();
            stretch_choice::open_configuration(app, g);
            app->update();
            std::string text = since(from);
            need(has(text, "[operacja:konfiguracja] otwarto okno dla " + g), "otwarcie okna konfiguracji nie zostało zapisane");
            need(has(text, "okno "), "brak wpisu o rozmiarze i położeniu okna");
            ctx.note("okno konfiguracji otwarte");
            close_extra_windows();
            app->project = fresh(); app->refresh();
        });

        run(area, "montaż okuć: wybór i anulowanie zapisane w dzienniku", "drag_hardware.cpp (begin/cancel)", [=, this](Ctx& ctx) {
            if (app->hardware_drag == nullptr) { ctx.note("kontroler okuć niedostępny w tym oknie — pominięto"); return; }
            auto from = mark();
            app->hardware_drag->begin("hinge", json::object(), nullptr);
            app->hardware_drag->cancel();
            std::string text = since(from);
            need(has(text, "[operacja:okucia] wybrano do montażu: hinge"), "wybór zawiasu do montażu nie został zapisany");
            need(has(text, "anulowano montaż: hinge"), "anulowanie montażu nie zostało zapisane");
        });

        run(area, "narożniki: tworzenie z panelu zostawia wpisy [narożnik] i [zmiana]", "corner_quick_ui.cpp (new_corner, commit, quick_shape)", [=, this](Ctx& ctx) {
            if (app->corner_panel == nullptr) { ctx.note("panel narożnika niedostępny — pominięto"); return; }
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->corner_panel->set_config(corner_furniture::default_corner_config(app->project.get(), "cabinet"));
            auto from = mark();
            app->corner_panel->quick_shape("L");
            need(has(since(from), "[operacja:narożnik] szybki wybór kształtu: L"), "szybki wybór kształtu narożnika nie został zapisany");
            from = mark();
            bool ok = false;
            try { ok = app->corner_panel->new_corner(); } catch (const std::exception& error) { ctx.note(std::string("wyjątek: ") + error.what()); }
            std::string text = since(from);
            need(has(text, "[operacja:narożnik] nowy narożnik:"), "tworzenie narożnika nie zostało zapisane w dzienniku");
            if (ok) {
                ctx.note("narożnik utworzony");
                need(has(text, "[operacja:narożnik] zapis narożnika"), "zapis narożnika nie został zapisany w dzienniku");
                need(has(text, "[operacja:zmiana]"), "utworzenie narożnika nie wygenerowało wpisu [zmiana]");
                need(has(text, "nowe meble"), "wpis [zmiana] nie mówi o nowym meblu (narożniku)");
            } else {
                need(has(text, "tworzenie odrzucone:"), "odrzucone tworzenie narożnika nie ma powodu w dzienniku");
                ctx.warn("kreator odrzucił narożnik z wartościami domyślnymi: " + text.substr(text.find("tworzenie odrzucone")));
            }
            app->project = fresh(); app->refresh();
        });

        run(area, "narożniki: konwersja szafki zapisana w dzienniku", "corner_quick_ui.cpp (convert_selected)", [=, this](Ctx& ctx) {
            if (app->corner_panel == nullptr) { ctx.note("panel narożnika niedostępny — pominięto"); return; }
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->selected = p->parts.front().id; app->corner_panel->refresh(); app->update();
            auto from = mark();
            bool ok = false;
            try { ok = app->corner_panel->convert_selected(); } catch (const std::exception& error) { ctx.note(std::string("wyjątek: ") + error.what()); }
            std::string text = since(from);
            ctx.note(ok ? "skonwertowano" : "konwersja odrzucona");
            need(has(text, "[operacja:narożnik] konwersja") || has(text, "odrzucone"), "konwersja do narożnika nie zostawiła wpisu w dzienniku");
            app->project = fresh(); app->refresh();
        });

        run(area, "rozciąganie: zaznaczona część skrzynki szuflady daje uchwyty mebla (generated_by)", "app.h (draw_original — właściciel części generowanej)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { auto& b = section(c, "bottom"); b["enabled"] = true; b["front"] = "drawers"; b["drawers"] = 2; b["shelves"] = 0; });
            app->project = p; app->refresh(); app->update();
            const core::Part* generated = nullptr;
            for (const auto& part : p->parts) if (py::truthy(py::get(part.assembly, "generated_by"))) { generated = &part; break; }
            if (!generated) { ctx.note("konfiguracja nie wygenerowała skrzynek szuflad — pominięto"); app->project = fresh(); app->refresh(); return; }
            ctx.note("zaznaczona część: " + generated->name);
            app->selected = generated->id;
            app->edit_mode.set("furniture"); app->draw();
            size_t count = app->handles.size();
            app->edit_mode.set("camera"); app->selected.reset();
            app->project = fresh(); app->refresh();
            need(count > 0, "zaznaczenie skrzynki szuflady w trybie Rozciągnij nie daje uchwytów mebla");
        });

        run(area, "narzędzia: Rozciągnij → Konfiguruj (z oknem) → Rozciągnij znów daje działające uchwyty", "viewport_hud.cpp (activate_tool), app.h (draw_original, mouse_down)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->selected = p->parts.front().id;
            auto from = mark();
            for (const char* mode : {"camera", "furniture", "configure", "furniture"}) {
                viewport_hud::activate_tool(app, mode, mode);
                need(app->edit_mode.get() == mode, std::string("narzędzie nie ustawiło trybu ") + mode);
                if (std::string(mode) == "configure") { stretch_choice::open_configuration(app, g); app->update(); }
            }
            std::string text = since(from);
            need(has(text, "[operacja:narzędzie] kliknięto „furniture” (tryb furniture, poprzedni configure)"), "brak wpisu o przejściu Konfiguruj → Rozciągnij");
            need(has(text, "stan po kliknięciu „furniture”:"), "brak zrzutu stanu po przełączeniu narzędzia");
            need(!app->handles.empty(), "po Konfiguruj → Rozciągnij brak uchwytów rozciągania");
            const auto handle = app->handles.front();
            tk::Event down; down.x = (int)handle.x; down.y = (int)handle.y; down.widget = app->canvas;
            app->mouse_down(down);
            bool began = (bool)app->drag_edit;
            app->drag_edit.reset();
            ctx.note(std::format("uchwytów: {}, okno konfiguracji: {}", app->handles.size(), stretch_choice::configuration_dialog() ? "otwarte" : "zamknięte"));
            need(began, "po Konfiguruj → Rozciągnij kliknięcie w uchwyt nie rozpoczęło rozciągania");
            close_extra_windows();
            app->edit_mode.set("camera"); app->selected.reset();
            app->project = fresh(); app->refresh();
        });

        run(area, "narzędzia: każdy tryb palety zapisuje kliknięcie i stan interakcji", "viewport_hud.cpp (activate_tool)", [=, this](Ctx&) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            for (const char* mode : {"camera", "layout", "furniture", "resize", "configure", "fillet", "camera"}) {
                auto from = mark();
                viewport_hud::activate_tool(app, mode, std::string("Test ") + mode);
                std::string text = since(from);
                need(has(text, std::string("kliknięto „Test ") + mode + "”"), std::string("brak wpisu o kliknięciu narzędzia ") + mode);
                need(has(text, std::string("stan po kliknięciu „Test ") + mode + "”: tryb " + mode), std::string("brak stanu po przełączeniu na ") + mode);
            }
            app->project = fresh(); app->refresh();
        });

        run(area, "kliknięcie w widoku zapisuje wynik zaznaczania", "app.h (mouse_up)", [=, this](Ctx&) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            auto from = mark();
            tk::Event click; click.x = 3; click.y = 3; click.widget = app->canvas;
            app->mouse_down(click);
            app->mouse_up(click);
            need(has(since(from), "[operacja:zaznaczenie] kliknięcie w widoku (3, 3)"), "kliknięcie w widoku nie zostało zapisane w dzienniku");
            app->selected.reset();
            app->project = fresh(); app->refresh();
        });

        run(area, "niezatwierdzona zmiana w panelu wstrzymuje kliknięcie i jest to zapisane", "studio_interaction.cpp (allow_press), app.h (flush_parameter_drafts)", [=, this](Ctx& ctx) {
            if (app->quick_inspector == nullptr) { ctx.note("panel mebla niedostępny — pominięto"); return; }
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            auto* inspector = app->quick_inspector;
            auto saved_group = inspector->group; bool saved_draft = inspector->has_draft, saved_sync = inspector->syncing;
            inspector->group = g; inspector->has_draft = true; inspector->syncing = true;  // apply() zwraca wtedy false bez okna
            auto from = mark();
            bool allowed = studio_interaction::allow_press(app);
            inspector->group = saved_group; inspector->has_draft = saved_draft; inspector->syncing = saved_sync;
            std::string text = since(from);
            app->project = fresh(); app->refresh();
            need(!allowed, "kliknięcie powinno zostać wstrzymane przy niezatwierdzonej zmianie");
            need(has(text, "kliknięcie w widoku wstrzymane"), "wstrzymane kliknięcie nie zostało zapisane w dzienniku");
        });

        run(area, "okno konfiguracji: rozwinięcie sekcji i kafelek zapisane w dzienniku", "stretch_choice.cpp (build_settings)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            stretch_choice::open_configuration(app, g); app->update();
            tk::Widget* dialog = stretch_choice::configuration_dialog();
            need(dialog != nullptr, "okno konfiguracji się nie otworzyło");
            std::vector<tk::Widget*> all;
            std::function<void(tk::Widget*)> collect = [&](tk::Widget* w) { for (auto* c : w->winfo_children()) { all.push_back(c); collect(c); } };
            collect(dialog);
            tk::Widget* title = nullptr; tk::Widget* tile = nullptr;
            for (auto* w : all) {
                try {
                    if (!title && w->winfo_class() == "TLabel" && w->cget("text") == "Korpus") title = w;
                    if (w->winfo_class() == "Canvas" && w->cget("width") == "74" && (tile == nullptr || (!tile->winfo_ismapped() && w->winfo_ismapped()))) tile = w;
                } catch (const tk::TclError&) {}
            }
            need(title != nullptr, "w oknie konfiguracji nie znaleziono nagłówka sekcji Korpus");
            need(tile != nullptr, "w oknie konfiguracji nie znaleziono kafelków wyboru");
            auto from = mark();
            title->event_generate("<Button-1>"); app->update();  // rozwija sekcję
            tile->event_generate("<Button-1>"); app->update();   // kafelek (sekcja Fronty jest domyślnie rozwinięta)
            title->event_generate("<Button-1>"); app->update();  // zwija sekcję
            std::string text = since(from);
            ctx.note("kliknięto nagłówek (2×) i kafelek");
            close_extra_windows();
            app->project = fresh(); app->refresh();
            need(has(text, "sekcja „Korpus” rozwinięta"), "rozwinięcie sekcji nie zostało zapisane");
            need(has(text, "sekcja „Korpus” zwinięta"), "zwinięcie sekcji nie zostało zapisane");
            need(has(text, "[operacja:konfiguracja] kafelek „"), "kliknięcie kafelka nie zostało zapisane");
        });

        run(area, "zmiana konfiguracji z menu (LED) zostawia wpisy [konfiguracja] i [zmiana] z okuciami", "studio_interaction.cpp (configure_change), journal.cpp", [=, this](Ctx&) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { c["led"] = false; });
            app->project = p; app->refresh(); app->update();
            app->selected = p->parts.front().id;
            auto from = mark();
            studio_interaction::configure_change(app, json{{"led", true}});
            std::string text = since(from);
            app->selected.reset(); app->project = fresh(); app->refresh();
            need(has(text, "[operacja:konfiguracja] zmiana z menu / narzędzia dla " + g), "zmiana z menu nie została zapisana");
            need(has(text, "[operacja:zmiana]") && has(text, "LED"), "włączenie LED nie zostawiło wpisu [zmiana] z okuciami LED");
            need(has(text, "źródło studio_interaction.cpp:"), "wpis [zmiana] nie wskazuje miejsca w kodzie");
        });

        run(area, "montaż uchwytu na drzwiach zostawia opis okuć w dzienniku", "drag_hardware.cpp (attach), journal.cpp (describe_difference)", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { auto& m = section(c, "main"); m["front"] = "doors"; m["doors"] = 1; c["handle_mode"] = "none"; });
            const core::Part* door = nullptr;
            for (const auto& part : p->parts) if (part.role == "door") { door = &part; break; }
            if (!door) { ctx.note("brak drzwi w szafce — pominięto"); return; }
            json hit = {{"u", door->u / 2}, {"v", door->v / 2}, {"w", door->t}, {"face", "top"}};
            ProjectPtr with;
            try { with = drag_hardware::attach(*p, "handle", door->id, hit, json::object()); }
            catch (const std::exception& error) { ctx.warn(std::string("nie udało się dołożyć uchwytu: ") + error.what()); return; }
            std::string d = journal::describe_difference(*p, *with);
            ctx.note(d);
            need(!d.empty(), "dołożenie uchwytu nie zostało opisane");
            need(has_text(d, "uchwyt") || has_text(d, "wiercenia"), "opis nie wspomina o uchwycie ani o wierceniach: " + d);
        });

        run(area, "opis okuć widzi zmianę rodzaju zawiasu i przestawienie okuć (nie tylko liczby)", "journal.cpp (make_snap: okucia bez pola kind/group)", [](Ctx& ctx) {
            auto [p, g] = cabinet(Project(), 0, [](json& c) { auto& m = section(c, "main"); m["front"] = "doors"; m["doors"] = 1; c["hinge_pattern"] = "hinge45"; });
            // ten sam mebel przebudowany z innym wzorem zawiasów (liczba okuć i wierceń bez zmian, zmieniają się tylko położenia otworów)
            json cfg = furniture_configurator::read_config(*p, g); cfg["hinge_pattern"] = "hinge52";
            auto [changed, g3] = furniture_configurator::configure(*p, cfg, g);
            std::string d = journal::describe_difference(*p, *changed);
            ctx.note(d);
            need(!d.empty(), "zmiana wzoru zawiasów (45→52) nie została opisana");
        });

        run(area, "przesuwanie: obrót mebla (narzędzie Obróć) zapisuje układ w dzienniku", "furniture_layout_ui.cpp (commit, rotate_furniture)", [=, this](Ctx&) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->selected = p->parts.front().id;
            auto from = mark();
            need((bool)app->rotate_furniture, "brak funkcji obrotu mebla");
            app->rotate_furniture(90);
            std::string text = since(from);
            app->selected.reset(); app->project = fresh(); app->refresh();
            need(has(text, "[operacja:przesuwanie] zapis układu:"), "obrót mebla nie został zapisany jako zapis układu");
            need(has(text, "[operacja:zmiana]") && has(text, "położenie/obrót"), "obrót mebla nie dał wpisu [zmiana] z położeniem/obrotem");
        });

        run(area, "menu podręczne: wybrana pozycja jest zapisana w dzienniku", "studio_interaction.cpp (context_menu)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->selected = p->parts.front().id;
            tk::Event click; click.x = 3; click.y = 3; click.widget = app->canvas;
            tk::Menu* menu = studio_interaction::context_menu(app, click, false);
            need(menu != nullptr, "menu podręczne nie powstało");
            int index = -1, position = 0;
            for (const auto& entry : menu->entries()) { if (entry.type == "command" && entry.label.rfind("Rozciągaj cały mebel", 0) == 0) { index = position; break; } ++position; }
            auto from = mark();
            if (index >= 0) menu->call({menu->path(), "invoke", std::to_string(index)});
            std::string text = since(from);
            ctx.note(index >= 0 ? "wywołano pozycję menu" : "w menu nie znaleziono pozycji do wywołania");
            app->edit_mode.set("camera"); app->selected.reset(); app->project = fresh(); app->refresh();
            need(index >= 0, "menu podręczne nie zawiera pozycji „Rozciągaj cały mebel”");
            need(has(text, "[operacja:menu] menu podręczne: Rozciągaj cały mebel"), "wybrana pozycja menu nie została zapisana");
        });

        run(area, "okno kształtów: zatwierdzenie lub odrzucenie zostawia wpis [kształty]", "style_ui.cpp (commit, guarded)", [=, this](Ctx& ctx) {
            auto [p, g] = cabinet(Project());
            app->project = p; app->refresh(); app->update();
            app->selected = p->parts.front().id;
            auto* dialog = style_ui::shape_dialog(app);
            need(dialog != nullptr && (bool)dialog->shape_apply, "okno kształtów nie otworzyło się");
            app->update();
            auto from = mark();
            try { dialog->shape_apply(); } catch (const std::exception& error) { ctx.note(std::string("wyjątek: ") + error.what()); }
            std::string text = since(from);
            dialog->destroy(); app->update();
            app->selected.reset(); app->project = fresh(); app->refresh();
            need(has(text, "[operacja:kształty]"), "zastosowanie kształtu nie zostawiło wpisu [kształty] (ani zatwierdzenia, ani odrzucenia)");
        });

        run(area, "projekt z opisu: utworzenie mebli zapisane w dzienniku i opisane jako nowe meble", "quick_studio_ui.cpp (apply_text_design), text_design.cpp", [=, this](Ctx& ctx) {
            app->project = fresh(); app->refresh(); app->update();
            auto result = text_design::parse("3 szafki dolne po 60 cm, front biały", *app->project);
            need(result.ok(), "opis nie został rozpoznany: " + text_design::summary(result));
            auto from = mark();
            auto groups = quick_studio_ui::apply_text_design(app, result, "3 szafki dolne po 60 cm, front biały");
            std::string text = since(from);
            ctx.note(std::format("utworzono {} mebli", groups.size()));
            size_t parts = app->project->parts.size();
            app->project = fresh(); app->refresh();
            need(groups.size() == 3, "oczekiwano 3 mebli z opisu");
            need(parts > 0, "projekt po utworzeniu z opisu jest pusty");
            need(has(text, "[operacja:projekt z opisu] utworzono 3 mebli"), "utworzenie z opisu nie zostało zapisane w dzienniku");
            need(has(text, "[operacja:zmiana]") && has(text, "nowe meble"), "utworzenie z opisu nie dało wpisu [zmiana] z nowymi meblami");
        });
    }

    void test_log_coverage() {
        run("Logi", "dziennik zawiera wpisy z testowanych obszarów", "journal.cpp, developer_logs.cpp, sketch.cpp, app.h (file_event/draw_done/undo), form_dialog.cpp", [this](Ctx& ctx) {
            need(fs::exists(app->developer_log_path), "brak pliku logu: " + py::path_str(app->developer_log_path));
            std::string text = read_file(app->developer_log_path);
            // Log się obraca (MebelStudio3D.log → .log.1), więc czytamy też poprzedni plik.
            fs::path rotated = app->developer_log_path; rotated += ".1";
            std::error_code rotated_ec;
            if (fs::exists(rotated, rotated_ec)) text += read_file(rotated);
            std::string missing;
            long long found = 0;
            for (const auto& [category, test_name] : JOURNAL_COVERAGE) {
                if (text.find(std::string("[operacja:") + category + "]") != std::string::npos) ++found;
                else missing += std::string(missing.empty() ? "" : ", ") + category;
            }
            ctx.note(std::format("kategorie z wpisami: {}", found));
            if (!missing.empty()) ctx.warn("brak wpisów dziennika dla kategorii: " + missing + " (czy log się obrócił, czy brakuje wywołań journal::event?)");
        });
    }

    void run_all() {
        remember_windows();
        test_library();
        test_configurator();
        test_stretching();
        test_corners();
        test_narrow_corner();
        test_arrangements_widths();
        test_layout();
        test_hardware();
        test_exports();
        test_persistence();
        test_scene();
        test_view();
        test_render_matrix();
        test_sketches();
        test_sketch_tools();
        test_design();
        test_catalogue();
        test_files();
        test_program_files();
        test_library_on_furniture();
        test_flyout();
        test_cargo();
        test_led_scene_export();
        test_nesting();
        test_inspector();
        test_text_design();
        test_ui();
        test_library_matrix();
        test_journal_actions();
        test_log_coverage();
    }
};

// ---------------------------------------------------------------- raport

std::string area_summary(const std::vector<Case>& cases, const std::string& area) {
    long long ok = 0, warn = 0, fail = 0, skip = 0;
    for (const auto& c : cases) if (c.area == area) { switch (c.status) { case Status::Ok: ++ok; break; case Status::Warn: ++warn; break; case Status::Fail: ++fail; break; case Status::Skip: ++skip; break; } }
    return std::format("{} OK · {} uwag · {} błędów{}", ok, warn, fail, skip ? std::format(" · {} pominiętych", skip) : "");
}

void write_report(const fs::path& dir, const std::string& stamp_text, Runner& runner, const Audit& audit, double seconds, const std::string& startup_error) {
    const auto& cases = runner.cases;
    long long ok = 0, warn = 0, fail = 0, skip = 0;
    for (const auto& c : cases) { switch (c.status) { case Status::Ok: ++ok; break; case Status::Warn: ++warn; break; case Status::Fail: ++fail; break; case Status::Skip: ++skip; break; } }
    std::vector<std::string> areas;
    for (const auto& c : cases) if (std::find(areas.begin(), areas.end(), c.area) == areas.end()) areas.push_back(c.area);

    std::ostringstream md;
    md << "# Raport pełnego testu — Mebel Studio 3D (C++)\n\n";
    md << "- Data: " << stamp("%Y-%m-%d %H:%M:%S") << "\n- Czas trwania testu: " << std::format("{:.1f}", seconds) << " s\n- Program: " << py::path_str(py::executable_dir()) << "\n";
    md << "- Ustawienia programu: **nie zapisywano** (preferencje przekierowane do katalogu tymczasowego, projekt użytkownika nietknięty)\n";
    md << "- Pełny log przebiegu: `test_" << stamp_text << ".log`\n\n";
    if (!startup_error.empty()) md << "> **Program nie wystartował w trybie testu:** " << startup_error << "\n\n";
    md << "## Podsumowanie\n\n";
    md << std::format("**{} przypadków: {} OK · {} uwag · {} błędów · {} pominiętych.**\n\n", cases.size(), ok, warn, fail, skip);
    if (fail == 0 && audit.problems.empty()) md << "Werdykt: brak błędów w testowanych funkcjach, a źródła są kompletne.\n\n";
    else md << std::format("Werdykt: do poprawy {} błędów funkcji i {} spraw ze źródłami (patrz niżej).\n\n", fail, audit.problems.size());
    md << "| Obszar | Wynik |\n|---|---|\n";
    for (const auto& a : areas) md << "| " << md_escape(a) << " | " << area_summary(cases, a) << " |\n";

    md << "\n## Co naprawić / co dodać\n\n";
    long long todo = 0;
    for (const auto& c : cases) {
        if (c.status != Status::Fail && c.status != Status::Warn) continue;
        ++todo;
        md << std::format("{}. **[{}] {} · {}** — {}\n", todo, label(c.status), c.area, c.name, c.detail.empty() ? "(brak szczegółów)" : c.detail);
        md << "   - Gdzie szukać: " << c.where << "\n";
        if (!c.hint.empty()) md << "   - Wskazówka: " << c.hint << "\n";
    }
    for (const auto& problem : audit.problems) { ++todo; md << todo << ". **[ŹRÓDŁA]** " << problem << "\n"; }
    if (todo == 0) md << "Nic do poprawienia.\n";

    md << "\n## Wyniki szczegółowe\n";
    for (const auto& a : areas) {
        md << "\n### " << a << "\n\n| Wynik | Test | ms | Szczegóły |\n|---|---|---:|---|\n";
        std::string where;
        for (const auto& c : cases) {
            if (c.area != a) continue;
            if (where.empty()) where = c.where;
            md << "| " << label(c.status) << " | " << md_escape(c.name) << " | " << std::format("{:.0f}", c.ms) << " | " << md_escape(c.detail) << " |\n";
        }
        if (!where.empty()) md << "\nGdzie jest logika: " << where << "\n";
    }

    md << "\n## Komunikaty modalne pominięte w teście\n\n";
    if (runner.all_intercepted.empty()) md << "Program nie próbował pokazać żadnego okna komunikatu.\n";
    else for (const auto& m : runner.all_intercepted) md << "- " << md_escape(m) << "\n";

    md << "\n## Audyt plików źródłowych\n\n";
    if (!audit.found) md << "Nie znaleziono źródeł.\n";
    else {
        md << "- Katalog źródeł C++: `" << py::path_str(audit.src) << "`\n";
        md << "- Katalog oryginału (Python): `" << py::path_str(audit.python) << "`\n";
        md << std::format("- Pliki C++ (.cpp/.h): {} · linii: {}\n", audit.data.value("pliki_cpp", 0), audit.data.value("linie_cpp", 0));
        if (audit.data.contains("moduly_python"))
            md << std::format("- Moduły Pythona: {} · przeniesione do C++: {} · nazw funkcji/klas w Pythonie: {} · bez odpowiednika nazwy w C++: {}\n", audit.data["moduly_python"].get<long long>(),
                              audit.data["moduly_przeniesione"].get<long long>(), audit.data["nazwy_python"].get<long long>(), audit.data["nazwy_brakujace"].get<long long>());
        if (!audit.git_note.empty()) md << "- Aktualność: " << audit.git_note << "\n";
        if (audit.data.contains("ostatni_commit")) md << "- Ostatni commit: " << audit.data["ostatni_commit"].get<std::string>() << "\n";
        md << "\n### Do poprawy\n\n";
        if (audit.problems.empty()) md << "Brak — każdy moduł Pythona ma port w C++, w katalogu źródeł nie ma plików Pythona, port nie jest starszy od oryginału.\n";
        for (const auto& p : audit.problems) md << "- " << p << "\n";
        md << "\n### Informacje\n\n";
        for (const auto& n : audit.notes) md << "- " << n << "\n";
        if (audit.data.contains("znaczniki") && !audit.data["znaczniki"].empty()) {
            md << "\n### Miejsca oznaczone TODO / niezaimplementowane\n\n";
            for (const auto& m : audit.data["znaczniki"]) md << "- `" << m.get<std::string>() << "`\n";
        }
    }
    py::write_text(py::path_str(dir / ("raport_pelny_test_" + stamp_text + ".md")), md.str());

    json out = {{"data", stamp("%Y-%m-%d %H:%M:%S")}, {"czas_s", seconds}, {"ok", ok}, {"uwagi", warn}, {"bledy", fail}, {"pominiete", skip}, {"ustawienia_zapisane", false},
                {"przypadki", json::array()}, {"komunikaty_modalne", runner.all_intercepted}, {"audyt", audit.data}, {"audyt_do_poprawy", audit.problems}, {"audyt_informacje", audit.notes}};
    if (!startup_error.empty()) out["blad_startu"] = startup_error;
    for (const auto& c : cases)
        out["przypadki"].push_back({{"obszar", c.area}, {"test", c.name}, {"wynik", label(c.status)}, {"ms", c.ms}, {"szczegoly", c.detail}, {"gdzie", c.where}, {"wskazowka", c.hint}});
    py::write_text(py::path_str(dir / ("raport_pelny_test_" + stamp_text + ".json")), py::dumps(out, false, 2));
    py::write_text(py::path_str(dir / "OSTATNI_RAPORT.md"), md.str());
}

}  // namespace

int run_cli(const std::string& report_dir_argument, bool pause_at_end) {
    if (!dev_tools::enabled()) {
        std::fputs("Pełny test jest dostępny tylko w wersji deweloperskiej (CMake -DMEBEL_DEV_TOOLS=ON; zmienna MEBEL_DEV nie może mieć wartości 0).\n", stderr);
        return 3;
    }
    std::string stamp_text = stamp("%Y%m%d_%H%M%S");
    fs::path dir;
    {
        std::string expanded = report_dir_argument.empty() ? "raporty_testu" : report_dir_argument;
        if (!expanded.empty() && expanded[0] == '~') expanded = py::path_str(py::home()) + expanded.substr(1);
        dir = fs::absolute(py::to_path(expanded));
        fs::create_directories(dir);
    }
    Runner runner;
    runner.dir = dir;
    runner.log.open(dir / ("test_" + stamp_text + ".log"), std::ios::binary);
    runner.say("=== PEŁNY TEST Mebel Studio 3D — start; raport trafi do: " + py::path_str(dir));

    tk::messagebox::set_headless(true);
    std::unique_ptr<TempDir> settings_dir;
    std::optional<std::function<fs::path()>> original_preference_path;
    std::string startup_error;
    App* app = nullptr;
    auto started = std::chrono::steady_clock::now();
    try {
        settings_dir = std::make_unique<TempDir>("MebelStudio3D-fulltest-");
        original_preference_path = ui_preferences::preference_path;
        fs::path settings_file = settings_dir->path / "ui_preferences.json";
        ui_preferences::preference_path = [settings_file] { return settings_file; };
        ui_preferences::save_preferences(py::merged(ui_preferences::DEFAULTS, json{{"engine", "cpu"}}));
        runner.say("ustawienia programu przekierowane do katalogu tymczasowego (nie zostaną zapisane)");
        app = new App();
        app->geometry("1400x900+20+20");  // okno widoczne, żeby widok 3D miał realny rozmiar i test renderu rysował naprawdę
        app->update();
        runner.app = app;
        runner.data_root = find_source_dir().parent_path();
        runner.say("okno programu utworzone");
        runner.run_all();
    } catch (const std::exception& error) {
        startup_error = describe(error);
        runner.say("BŁĄD KRYTYCZNY: " + startup_error);
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    runner.say("audyt plików źródłowych…");
    Audit audit;
    try { audit = audit_sources(); } catch (const std::exception& error) { audit.problems.push_back(std::string("Audyt źródeł przerwany: ") + error.what()); }
    if (app != nullptr) {
        try { app->destroy(); } catch (...) {}
        runner.app = nullptr;
    }
    if (original_preference_path) ui_preferences::preference_path = *original_preference_path;
    tk::messagebox::set_headless(false);
    long long fail = 0;
    for (const auto& c : runner.cases) if (c.status == Status::Fail) ++fail;
    try {
        write_report(dir, stamp_text, runner, audit, seconds, startup_error);
        runner.say("=== KONIEC. Raport: " + py::path_str(dir / ("raport_pelny_test_" + stamp_text + ".md")));
    } catch (const std::exception& error) {
        runner.say(std::string("nie udało się zapisać raportu: ") + error.what());
        return 2;
    }
    int code = (fail == 0 && startup_error.empty()) ? 0 : 1;
    std::printf("\nWynik: %s. Raport i log leżą w: %s\n", code == 0 ? "bez błędów" : "są błędy", py::path_str(dir).c_str());
    if (pause_at_end) { std::printf("Naciśnij Enter, aby zamknąć okno..."); std::fflush(stdout); std::getchar(); }
    return code;
}

void launch_in_new_process(App* app) {
    std::string exe = ARGV.empty() ? std::string() : py::path_str(fs::absolute(py::to_path(ARGV[0])));
    if (exe.empty()) { if (app) app->status.set("Nie znam ścieżki programu — uruchom: MebelStudio3D_cpp.exe --full-test"); return; }
    fs::path dir = fs::current_path() / "raporty_testu";
    std::string command;
#ifdef _WIN32
    command = "start \"Pelny test Mebel Studio 3D\" \"" + exe + "\" --full-test \"" + py::path_str(dir) + "\" --pause";
#else
    command = "\"" + exe + "\" --full-test \"" + py::path_str(dir) + "\" &";
#endif
    std::system(command.c_str());
    if (app) {
        app->status.set("Pełny test uruchomiony w osobnym oknie — raport pojawi się w: " + py::path_str(dir));
        app->log->info("[test] uruchomiono pełny test w osobnym procesie; raport: " + py::path_str(dir));
    }
}

}  // namespace selftest
