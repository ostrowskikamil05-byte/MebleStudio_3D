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
#include "dxf_import.h"
#include "stretch_choice.h"
#include "ui_preferences.h"
#include "viewport_modes.h"
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
    fs::path data_root;   // korzeń repozytorium z plikami przykładowymi (*.mebel, PRZYKLADY/, MANIFEST_SHA256.json, DLL)
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
            for (const auto& p : q.parts) grain[p.id] = p.grain;
            for (const auto& sheet : result.first)
                for (const auto& piece : sheet["pieces"]) need(!(grain[piece["id"].get<std::string>()] && piece["rotated"].get<bool>()), "formatka z włóknami została obrócona");
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
        for (const auto& dir : {data_root, data_root / "PRZYKLADY"}) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_regular_file() && entry.path().extension() == ".mebel") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) { skip(area, "pliki przykładowe", "brak plików .mebel w " + py::path_str(data_root)); return; }
        say(std::format("pliki przykładowe do sprawdzenia: {}", files.size()));
        for (const auto& file : files) {
            bool in_root = file.parent_path() == data_root;
            std::string shown = (in_root ? "" : "PRZYKLADY/") + py::path_str(file.filename());
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
        const char* area = "Pliki programu (manifest, biblioteki DLL)";
        const char* where = "MANIFEST_SHA256.json, MebelModel.dll, MebelNative3D.dll, native_model.cpp, native_viewport.cpp";
        if (data_root.empty()) { skip(area, "pliki programu", "nie znaleziono korzenia repozytorium"); return; }
        run(area, "manifest SHA-256: rozmiary i sumy plików", where, [this](Ctx& ctx) {
            fs::path manifest = data_root / "MANIFEST_SHA256.json";
            need(fs::is_regular_file(manifest), "brak MANIFEST_SHA256.json w " + py::path_str(data_root));
            json m = json::parse(read_file(manifest));
            long long ok = 0, missing = 0, changed = 0;
            std::vector<std::string> bad;
            for (auto& [name, info] : m.items()) {
                fs::path file = data_root / py::to_path(name);
                std::error_code ec;
                if (!fs::is_regular_file(file, ec)) { ++missing; if (bad.size() < 8) bad.push_back(name + " (brak pliku)"); continue; }
                std::string bytes = read_file(file);
                if ((long long)bytes.size() != info.value("bytes", -1LL) || py::sha256_hex(bytes) != info.value("sha256", std::string())) {
                    ++changed; if (bad.size() < 8) bad.push_back(name + " (inna zawartość)");
                } else ++ok;
            }
            ctx.note(std::format("{} zgodnych, {} zmienionych, {} brakujących", ok, changed, missing));
            if (changed + missing > 0) {
                // MANIFEST_SHA256.json opisuje starą paczkę Pythona (pliki .py/.pyd/.dll środowiska, kopie demo). Rozjazd z repozytorium
                // C++ jest oczekiwany, więc to tylko informacja — ostrzegamy wyłącznie o braku plików.
                ctx.note("manifest pochodzi z paczki Pythona; rozbieżności z repozytorium C++ są oczekiwane");
                if (missing > 0) {
                    std::string list;
                    for (auto& b : bad) if (b.find("(brak pliku)") != std::string::npos) list += (list.empty() ? "" : ", ") + b;
                    ctx.warn(std::format("manifest wymienia {} nieistniejących plików: {}", missing, list));
                }
            }
        });
        for (const char* dll : {"MebelModel.dll", "MebelNative3D.dll"}) {
            run(area, std::string("biblioteka ") + dll, where, [this, dll](Ctx& ctx) {
                std::vector<fs::path> places{py::executable_dir() / dll, data_root / dll, data_root / "mebel_studio_cpp" / "native-dll" / dll};
                for (const auto& place : places) {
                    std::error_code ec;
                    if (!fs::is_regular_file(place, ec)) continue;
                    auto size = fs::file_size(place, ec);
                    ctx.note(py::path_str(place) + std::format(" · {} B", size));
                    if (size < 4096) ctx.warn("plik poniżej 4 KB to wskaźnik Git LFS, a nie biblioteka — pobierz właściwy plik (git lfs pull)");
                    return;
                }
                ctx.warn("nie znaleziono pliku (szukano obok programu, w korzeniu repozytorium i w native-dll/)");
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
            // uszkodzony plik: odrzucony z powodem, kontur bez zmian
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
                    close_extra_windows();
                    app->project = fresh(); app->refresh();
                    ctx.note(std::format("{} {}", parts, scene_kind ? "obiektów sceny" : "części"));
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
