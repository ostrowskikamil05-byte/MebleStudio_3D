#include "perf_monitor.h"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app.h"
#include "developer_logs.h"
#include "tkcore.h"

namespace perf {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double SLOW_CALLBACK_MS = 40;   // wolna akcja interfejsu
constexpr double SLOW_EVAL_MS = 25;       // wolne pojedyncze polecenie Tcl
constexpr double LOOP_LAG_MS = 150;       // blokada pętli zdarzeń
constexpr double SLOW_TAB_MS = 150;       // przełączenie zakładki

struct Stat {
    long long count = 0;
    double total = 0, max = 0;
};

struct State {
    App* app = nullptr;
    std::map<std::string, Stat> stats;
    Clock::time_point process_start = Clock::now();
    std::string last_slow;                // ostatnia wolna czynność (podpowiedź przy blokadzie pętli)
    Clock::time_point last_slow_time;
    Clock::time_point log_window = Clock::now();
    int logged_in_window = 0;
    bool started_logged = false;
};

State& state() {
    static State& instance = *new State();  // nigdy niszczony: hook może zadziałać przy zamykaniu
    return instance;
}

void write(int level, const std::string& message) {
    State& s = state();
    if (s.app == nullptr || s.app->log == nullptr) return;
    // Najwyżej 8 wpisów na sekundę, żeby diagnostyka sama nie spowalniała programu.
    auto now = Clock::now();
    if (now - s.log_window > std::chrono::seconds(1)) { s.log_window = now; s.logged_in_window = 0; }
    if (level < developer_logs::ERROR && ++s.logged_in_window > 8) return;
    s.app->log->log(level, message);
}

void record(const std::string& kind, const std::string& what, double ms) {
    State& s = state();
    Stat& stat = s.stats[kind + ": " + what];
    ++stat.count;
    stat.total += ms;
    stat.max = std::max(stat.max, ms);
    double threshold = kind == "eval" ? SLOW_EVAL_MS : SLOW_CALLBACK_MS;
    if (ms >= threshold) {
        s.last_slow = kind + ": " + what + std::format(" ({:.0f} ms)", ms);
        s.last_slow_time = Clock::now();
        const char* title = kind == "eval" ? "Wolne polecenie Tcl" : kind == "scope" ? "Wolny blok" : "Wolna akcja";
        write(developer_logs::WARNING, std::format("[wydajność] {} {:.0f} ms — {}", title, ms, what));
    }
}

std::string pad(std::string text, size_t width) {
    // Szerokość liczona w bajtach wystarcza do wyrównania ASCII w raporcie.
    if (text.size() > width) text = text.substr(0, width - 1) + "…";
    return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
}

}  // namespace

Scope::Scope(const char* name) : name_(name), started_(Clock::now()) {}

Scope::~Scope() {
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
    record("scope", name_, ms);
}

void reset() {
    state().stats.clear();
}

void watch_notebook(App* app, ttk::Notebook* notebook, const std::string& name) {
    if (notebook == nullptr) return;
    notebook->bind("<<NotebookTabChanged>>", [app, notebook, name](tk::Event&) {
        try {
            auto started = std::make_shared<Clock::time_point>(Clock::now());
            std::string tab = notebook->select();
            std::string title = tab.empty() ? std::string("?") : notebook->tab(tab, "text");
            // Dwa kolejne "bezczynne" kroki: układ i rysowanie zaległych widgetów kończą się przed drugim.
            app->after_idle([app, started, name, title] {
                app->after_idle([started, name, title] {
                    double ms = std::chrono::duration<double, std::milli>(Clock::now() - *started).count();
                    record("zakładka", name + " → " + title, ms);
                    if (ms >= SLOW_TAB_MS)
                        write(developer_logs::WARNING, std::format("[wydajność] Przełączenie zakładki „{}” ({}) trwało {:.0f} ms", title, name, ms));
                });
            });
        } catch (const tk::TclError&) {
        }
        return std::string();
    }, true);
}

void install(App* app) {
    State& s = state();
    s.app = app;
    app->bind("<Destroy>", [app](tk::Event& e) {
        if (e.widget == app) {
            tkcore::set_profile_hook(nullptr, SLOW_EVAL_MS);
            state().app = nullptr;
        }
        return std::string();
    }, true);
    tkcore::set_profile_hook([](const char* kind, const std::string& what, double ms) {
        // Najczęstsze krótkie procedury nie trafiają do statystyk (tylko bardzo wolne i powyżej 1 ms).
        if (ms < 1.0) return;
        record(kind, what, ms);
    }, SLOW_EVAL_MS);

    // Pętla kontrolna: co 50 ms sprawdza, czy zdarzenia przetwarzane są na czas.
    auto expected = std::make_shared<Clock::time_point>(Clock::now() + std::chrono::milliseconds(50));
    auto tick = std::make_shared<std::function<void()>>();
    *tick = [app, expected, tick] {
        try {
            auto now = Clock::now();
            double lag = std::chrono::duration<double, std::milli>(now - *expected).count();
            if (lag >= LOOP_LAG_MS) {
                State& st = state();
                std::string hint;
                if (now - st.last_slow_time < std::chrono::milliseconds((long long)lag + 200)) hint = " · ostatnia wolna czynność: " + st.last_slow;
                record("blokada", "pętla zdarzeń", lag);
                write(developer_logs::WARNING, std::format("[wydajność] Interfejs nie reagował ok. {:.0f} ms{}", lag, hint));
            }
            *expected = Clock::now() + std::chrono::milliseconds(50);
            app->after(50, *tick);
        } catch (const tk::TclError&) {
        }
    };
    app->after(50, *tick);

    // Czas startu: od uruchomienia procesu do pierwszej chwili bezczynności po zbudowaniu okna.
    app->after_idle([app] {
        State& st = state();
        if (st.started_logged) return;
        st.started_logged = true;
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - st.process_start).count();
        auto evals = tkcore::eval_stats();
        app->log->info(std::format("[wydajność] Start: interfejs gotowy po {:.0f} ms; poleceń Tcl: {} ({:.0f} ms)", ms, evals.count, evals.total_ms));
    });
}

void report(App* app) {
    State& s = state();
    std::vector<std::pair<std::string, Stat>> rows(s.stats.begin(), s.stats.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.total > b.second.total; });
    auto evals = tkcore::eval_stats();
    app->log->info("[wydajność] ===== RAPORT WYDAJNOŚCI =====");
    app->log->info(std::format("[wydajność] Poleceń Tcl od startu: {} · łącznie {:.0f} ms · średnio {:.3f} ms", evals.count, evals.total_ms,
                               evals.count ? evals.total_ms / (double)evals.count : 0.0));
    app->log->info("[wydajność] czynność (łącznie ms · razy · maks ms · średnio ms)");
    size_t shown = 0;
    for (const auto& [name, stat] : rows) {
        if (++shown > 25) break;
        app->log->info(std::format("[wydajność] {:>8.0f} ms · {:>5}× · maks {:>6.0f} · śr. {:>6.1f} — {}", stat.total, stat.count, stat.max,
                                   stat.total / (double)stat.count, name));
    }
    if (rows.empty()) app->log->info("[wydajność] Brak zmierzonych czynności powyżej 1 ms.");
    app->log->info("[wydajność] ===== KONIEC RAPORTU =====");
}

void self_test(App* app) {
    app->log->info("[wydajność] Test przełączania zakładek: start");
    auto time_one = [app](const std::string& label, const std::function<void()>& action) {
        auto started = Clock::now();
        try {
            action();
            app->update_idletasks();
        } catch (const tk::TclError& error) {
            app->log->warning("[wydajność] " + label + ": błąd " + error.what());
            return;
        }
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        auto level = ms >= SLOW_TAB_MS ? developer_logs::WARNING : developer_logs::INFO;
        app->log->log(level, std::format("[wydajność] {:>7.0f} ms — {}", ms, label));
    };
    if (app->tabs != nullptr) {
        std::string original = app->tabs->select();
        for (const auto& tab : app->tabs->tabs()) {
            std::string title = app->tabs->tab(tab, "text");
            time_one("zakładka dokumentu: " + title, [app, tab] { app->tabs->select(tab); });
        }
        if (!original.empty()) app->tabs->select(original);
    }
    if (app->select_tool_page) {
        for (const char* page : {"Biblioteka", "Mebel", "CAD", "Układ", "Scena", "Produkcja", "Widok"})
            time_one(std::string("panel boczny: ") + page, [app, page] { app->select_tool_page(page); });
        app->select_tool_page("Biblioteka");
    }
    if (app->inspector_tabs != nullptr) {
        std::string original = app->inspector_tabs->select();
        for (const auto& tab : app->inspector_tabs->tabs()) {
            std::string title = app->inspector_tabs->tab(tab, "text");
            time_one("panel właściwości: " + title, [app, tab] { app->inspector_tabs->select(tab); });
        }
        if (!original.empty()) app->inspector_tabs->select(original);
    }
    time_one("pełne odrysowanie widoku 3D", [app] { app->draw(); });
    auto evals = tkcore::eval_stats();
    app->log->info(std::format("[wydajność] Test zakończony · poleceń Tcl łącznie: {} ({:.0f} ms)", evals.count, evals.total_ms));
}

}  // namespace perf
