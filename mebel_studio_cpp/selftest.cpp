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
#include "furniture_library.h"
#include "hardware_report.h"
#include "opening_options.h"
#include "perf_monitor.h"
#include "production_docs.h"
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
        add("strefa dolna", 0, [](json& c) { auto& b = section(c, "bottom"); b["enabled"] = true; b["front"] = "drawers"; b["shelves"] = 0; });
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
                auto stretched = direct_edit::resize_furniture_face(*p, g, "y0", 250);
                json after = furniture_configurator::read_config(*stretched, g);
                auto [r, g2] = corner_furniture::convert_to_corner(*stretched, g, "L", side, json(num(cfg["depth"])), json(num(cfg["width"])), json(num(after["depth"])));
                r->validate();
                ctx.note(std::format("{} części", r->parts.size()));
                check_overlaps(ctx, *r);
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
            // to samo, co robi okno Konfiguruj po zmianie pól (sekcja główna → szuflady, uchwyt → TIP-ON)
            json sections = cfg["sections"];
            for (auto& s : sections) if (s.value("key", std::string()) == "main") { s["front"] = "drawers"; s["shelves"] = 2; }
            stretch_choice::apply_overrides(cfg, json{{"handle_mode", "tip_short"}, {"sections", sections}});
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
                app->project = fresh();
                app->selected.reset();
                if (app->hardware_drag != nullptr && app->hardware_drag->active()) app->hardware_drag->cancel();
                json items = visual_catalogue::catalogue_items(kind, *app->project);
                need(items.is_array() && !items.empty(), "katalog nie zwrócił żadnych elementów");
                ctx.note(std::format("{} elementów w katalogu", items.size()));
                // kilka pierwszych elementów realnie dodajemy do pustego projektu (ścieżka z klikania w kartę)
                long long applied = 0, failed = 0;
                std::string first_error;
                size_t limit = kind == "furniture" ? 3 : 2;
                for (size_t i = 0; i < items.size() && i < limit; ++i) {
                    try {
                        app->project = fresh();
                        app->selected.reset();
                        items = visual_catalogue::catalogue_items(kind, *app->project);  // karty są związane z projektem, z którego powstały
                        if (kind != "furniture" && kind != "shaped" && kind != "appliance") {
                            auto [p, g] = cabinet(Project());
                            app->project = p;
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
                core::dxf_export(*p, py::path_str(tmp.path / "a.dxf"));
                core::report_export(*p, py::path_str(tmp.path / "a.html"));
                project_print::export_project_print(*p, py::path_str(tmp.path / "b.html"));
                if (in_root && p->parts.size() <= 400) {
                    cad_geometry::step_export(*p, py::path_str(tmp.path / "a.step"));
                    need(fs::file_size(tmp.path / "a.step") > 100, "plik STEP jest pusty");
                }
                for (const char* name : {"a.csv", "a.dxf", "a.html", "b.html"}) need(fs::is_regular_file(tmp.path / name) && fs::file_size(tmp.path / name) > 20, std::string("eksport ") + name + " jest pusty");
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
                std::string list;
                for (auto& b : bad) list += (list.empty() ? "" : ", ") + b;
                ctx.warn("pliki niezgodne z manifestem: " + list + " — zaktualizuj manifest albo przywróć pliki");
                ctx.hint = "Manifest jest nieaktualny albo plik zmieniono: wygeneruj MANIFEST_SHA256.json ponownie po zatwierdzeniu zmian w plikach przykładowych.";
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

    void test_log_coverage() {
        run("Logi", "dziennik zawiera wpisy z testowanych obszarów", "journal.cpp, developer_logs.cpp, sketch.cpp, app.h (file_event/draw_done/undo), form_dialog.cpp", [this](Ctx& ctx) {
            need(fs::exists(app->developer_log_path), "brak pliku logu: " + py::path_str(app->developer_log_path));
            std::string text = read_file(app->developer_log_path);
            std::string missing;
            long long found = 0;
            for (const char* category : {"szkic", "plik", "okno", "konfiguracja", "cofanie", "biblioteka", "render", "rozciąganie"}) {
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
        test_layout();
        test_hardware();
        test_exports();
        test_persistence();
        test_scene();
        test_view();
        test_render_matrix();
        test_sketches();
        test_catalogue();
        test_files();
        test_program_files();
        test_ui();
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
