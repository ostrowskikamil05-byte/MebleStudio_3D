// Bounded diagnostics, persisted outside the installation, with a dockable viewer.
#include "developer_logs.h"

#include <ctime>
#include <format>
#include <iostream>
#include <map>
#include <set>
#include <typeinfo>
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#endif

#include "app.h"
#include "perf_monitor.h"
#include "pyutil.h"

namespace fs = std::filesystem;

namespace developer_logs {

namespace {
std::set<App*>& _apps() { static std::set<App*> apps; return apps; }  // weakref.WeakSet()
bool _message_wrapped = false;

// sys.excepthook / threading.excepthook: w C++ nieobsłużony wyjątek (także
// w wątku) kończy się std::terminate, więc jeden handler obejmuje oba haki.
struct Hooks { std::terminate_handler previous = nullptr; std::shared_ptr<Logger> previous_logger, logger; };
std::map<App*, Hooks>& _hooks() { static std::map<App*, Hooks> hooks; return hooks; }
std::shared_ptr<Logger> _terminate_logger;
void _diagnostic_exception_hook() {
    if (_terminate_logger) {
        try {
            _terminate_logger->error("Nieobsłużony błąd", std::current_exception());
        } catch (...) {
        }
    }
    std::abort();
}

std::string exception_message(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "";
    }
}

std::string local_strftime(const char* pattern, std::time_t seconds) {
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &seconds);
#else
    localtime_r(&seconds, &parts);
#endif
    char buffer[128];
    std::size_t size = std::strftime(buffer, sizeof buffer, pattern, &parts);
    return std::string(buffer, size);
}
}  // namespace

std::string level_name(int level) {
    switch (level) {
        case CRITICAL: return "CRITICAL";
        case ERROR: return "ERROR";
        case WARNING: return "WARNING";
        case INFO: return "INFO";
        case DEBUG: return "DEBUG";
        case NOTSET: return "NOTSET";
    }
    return std::format("Level {}", level);
}

// ---------------------------------------------------------------- Formatter

Formatter::Formatter(std::string fmt_, std::optional<std::string> datefmt_) : fmt(std::move(fmt_)), datefmt(std::move(datefmt_)) {}

std::string Formatter::formatTime(const LogRecord& record) const {
    auto since = record.created.time_since_epoch();
    std::time_t seconds = (std::time_t)std::chrono::duration_cast<std::chrono::seconds>(since).count();
    if (datefmt) {
        return local_strftime(datefmt->c_str(), seconds);
    }
    long long msecs = std::chrono::duration_cast<std::chrono::milliseconds>(since).count() % 1000;
    return local_strftime("%Y-%m-%d %H:%M:%S", seconds) + std::format(",{:03d}", msecs);
}

std::string Formatter::formatException(std::exception_ptr error) const {
    // traceback.print_exception: C++ nie ma śladu stosu, zostaje
    // ostatnia linia „Typ: komunikat”.
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        std::string name = typeid(e).name();
#if __has_include(<cxxabi.h>)
        int status = 0;
        char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
        if (demangled) { name = demangled; std::free(demangled); }
#endif
        return name + ": " + e.what();
    } catch (...) {
        return "Nieznany wyjątek";
    }
}

std::string Formatter::format(const LogRecord& record) const {
    std::string s = fmt;
    s = py::replace_all(s, "%(asctime)s", fmt.find("%(asctime)") != std::string::npos ? formatTime(record) : "");
    s = py::replace_all(s, "%(levelname)s", record.levelname);
    // %(message)s na końcu, aby treść komunikatu nie była interpretowana.
    std::size_t at = s.find("%(message)s");
    if (at != std::string::npos) s.replace(at, 11, record.message);
    if (record.exc_info) {
        std::string exc_text = formatException(record.exc_info);
        if (!exc_text.empty()) {
            if (s.empty() || s.back() != '\n') s += "\n";
            s += exc_text;
        }
    }
    return s;
}

// ---------------------------------------------------------------- Handlers

void Handler::handle(const LogRecord& record) {
    emit(record);
}

MemoryHandler::MemoryHandler() {
    setFormatter(Formatter("%(asctime)s  %(levelname)s  %(message)s", "%H:%M:%S"));
}

void MemoryHandler::emit(const LogRecord& record) {
    sequence += 1;
    records.emplace_back(sequence, record.levelno, format(record));
    if (records.size() > maxlen) records.pop_front();
}

RotatingFileHandler::RotatingFileHandler(const fs::path& filename, std::uintmax_t maxBytes_, int backupCount_)
    : baseFilename(fs::absolute(filename)), maxBytes(maxBytes_), backupCount(backupCount_) {
    open();  // delay=False: błąd otwarcia jest zgłaszany od razu (OSError)
}

void RotatingFileHandler::open() {
    // open(path, 'a', encoding='utf-8'): tryb tekstowy, jak w Pythonie.
    stream = std::make_unique<std::ofstream>(baseFilename, std::ios::out | std::ios::app);
    if (!*stream) {
        stream.reset();
        throw py::OSError(std::make_error_code(std::errc::permission_denied), "Nie można otworzyć pliku logu: " + py::path_str(baseFilename));
    }
}

bool RotatingFileHandler::shouldRollover(const std::string& message) {
    if (!stream) {
        open();
    }
    if (maxBytes > 0) {
        stream->seekp(0, std::ios::end);
        auto pos = (std::uintmax_t)(std::streamoff)stream->tellp();
        if (!pos) {
            // gh-116263: Never rollover an empty file
            return false;
        }
        std::string msg = message + "\n";
        if (pos + py::len(msg) >= maxBytes) {
            std::error_code ignored;
            if (fs::exists(baseFilename, ignored) && !fs::is_regular_file(baseFilename, ignored)) {
                return false;
            }
            return true;
        }
    }
    return false;
}

void RotatingFileHandler::doRollover() {
    if (stream) {
        stream->close();
        stream.reset();
    }
    if (backupCount > 0) {
        std::string base = baseFilename.string();
        for (int i = backupCount - 1; i > 0; --i) {
            fs::path sfn = base + "." + std::to_string(i);
            fs::path dfn = base + "." + std::to_string(i + 1);
            if (fs::exists(sfn)) {
                if (fs::exists(dfn)) {
                    fs::remove(dfn);
                }
                fs::rename(sfn, dfn);
            }
        }
        fs::path dfn = base + ".1";
        if (fs::exists(dfn)) {
            fs::remove(dfn);
        }
        if (fs::exists(baseFilename)) {
            fs::rename(baseFilename, dfn);
        }
    }
    open();
}

void RotatingFileHandler::emit(const LogRecord& record) {
    try {
        std::string message = format(record);
        if (shouldRollover(message)) {
            doRollover();
        }
        *stream << message << "\n";
        stream->flush();
    } catch (const std::exception& error) {
        // Handler.handleError
        std::cerr << "--- Logging error ---\n" << error.what() << "\n";
    }
}

void RotatingFileHandler::close() {
    if (stream) {
        stream->flush();
        stream->close();
        stream.reset();
    }
}

// ---------------------------------------------------------------- Logger

void Logger::addHandler(std::shared_ptr<Handler> handler) {
    if (std::find(handlers.begin(), handlers.end(), handler) == handlers.end()) {
        handlers.push_back(std::move(handler));
    }
}

void Logger::removeHandler(const std::shared_ptr<Handler>& handler) {
    std::erase(handlers, handler);
}

void Logger::log(int level_, const std::string& message, std::exception_ptr exc_info) {
    if (level_ < level) {
        return;
    }
    LogRecord record;
    record.levelno = level_;
    record.levelname = level_name(level_);
    record.message = message;
    record.created = std::chrono::system_clock::now();
    record.exc_info = exc_info;
    for (auto& handler : std::vector<std::shared_ptr<Handler>>(handlers)) {
        if (record.levelno >= handler->level) {
            handler->handle(record);
        }
    }
}

void Logger::warning(const std::string& message, bool exc_info) {
    log(WARNING, message, exc_info ? std::current_exception() : nullptr);
}

std::shared_ptr<Logger> getLogger(const std::string& name) {
    static std::map<std::string, std::shared_ptr<Logger>> loggers;
    auto& logger = loggers[name];
    if (!logger) logger = std::make_shared<Logger>(name);
    return logger;
}

void _observe_messages() {
    if (_message_wrapped) return;
    for (auto [name, level] : std::vector<std::pair<std::string, int>>{{"showerror", ERROR}, {"showwarning", WARNING}}) {
        int level_ = level;
        tk::messagebox::add_observer(name, [level_](const std::string& title, const std::string& message) {
            for (App* app : std::vector<App*>(_apps().begin(), _apps().end())) app->log->log(level_, std::format("{}: {}", title, message));
        });
    }
    _message_wrapped = true;
}

// ---------------------------------------------------------------- LogDocument

LogDocument::LogDocument(App* app_) : dock_workspace::DockDocument(app_) {
    title("Logi developerskie"); cursor = 0;
    auto* bar = new ttk::Frame(this, {{"padding", 6}}); bar->pack({{"fill", "x"}});
    (new ttk::Label(bar, {{"text", "Zielony: OK / informacje   Żółty: ostrzeżenie   Czerwony: błąd"}}))->pack({{"side", "left"}});
    (new ttk::Button(bar, {{"text", "Zapisz log…"}, {"command", std::function<void()>([this] { export_(); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Historia pliku"}, {"command", std::function<void()>([this] { history(); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Zeruj statystyki"}, {"command", std::function<void()>([this] { perf::reset(); this->app->log->info("[wydajność] Statystyki wyzerowane"); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Test zakładek"}, {"command", std::function<void()>([this] { perf::self_test(this->app); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Diagnoza rysowania"}, {"command", std::function<void()>([this] { perf::diagnose_redraw(this->app); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Raport wydajności"}, {"command", std::function<void()>([this] { perf::report(this->app); })}}))->pack({{"side", "right"}, {"padx", 4}});
    (new ttk::Button(bar, {{"text", "Zamknij panel"}, {"command", std::function<void()>([this] { destroy(); })}}))->pack({{"side", "right"}});
    (new ttk::Label(this, {{"text", "Plik: " + py::path_str(app->developer_log_path)}, {"padding", 6}}))->pack({{"fill", "x"}});
    auto* box = new ttk::Frame(this); box->pack({{"fill", "both"}, {"expand", true}});
    text = new tk::Text(box, {{"bg", "#172332"}, {"fg", "#89d7ad"}, {"font", "Consolas 10"}, {"wrap", "word"}, {"state", "disabled"}});
    auto* sb = new ttk::Scrollbar(box, {{"command", text->path() + " yview"}}); sb->pack({{"side", "right"}, {"fill", "y"}}); text->pack({{"fill", "both"}, {"expand", true}});
    text->configure({{"yscrollcommand", sb->path() + " set"}});
    for (auto& [name, color] : std::vector<std::pair<std::string, std::string>>{{"info", "#89d7ad"}, {"warning", "#ffda78"}, {"error", "#ff8c8c"}}) text->tag_configure(name, {{"foreground", color}});
    poll();
}

void LogDocument::poll() {
    std::vector<std::tuple<int, int, std::string>> pending;
    for (auto& record : app->developer_memory->records) if (std::get<0>(record) > cursor) pending.push_back(record);
    if (!pending.empty()) {
        bool at_end = text->yview().second >= .98; text->configure({{"state", "normal"}});
        for (auto& [seq, level, line] : pending) {
            text->insert("end", line + "\n", level >= ERROR ? "error" : level >= WARNING ? "warning" : "info");
            cursor = seq;
        }
        if (std::stoi(py::split(text->index("end-1c"), '.')[0]) > 4000) text->delete_("1.0", "1000.0");
        text->configure({{"state", "disabled"}});
        if (at_end) text->see("end");
    }
    after(350, [this] { poll(); });
}

void LogDocument::export_() {
    std::string path = tk::filedialog::asksaveasfilename({{"parent", winfo_toplevel()}, {"defaultextension", ".log"}, {"initialfile", "MebelStudio3D-diagnostyka.log"}});
    if (!path.empty()) {
        try {
            std::vector<std::string> lines;
            for (auto& record : app->developer_memory->records) lines.push_back(std::get<2>(record));
            py::write_text(path, py::join(lines, "\n"));
        } catch (const py::OSError&) {
            app->log->exception("Nie udało się wyeksportować logu");
        }
    }
}

void LogDocument::history() {
    try {
        std::ifstream source(app->developer_log_path, std::ios::binary);
        if (!source) throw py::OSError(std::make_error_code(std::errc::no_such_file_or_directory), py::path_str(app->developer_log_path));
        source.seekg(0, std::ios::end); std::streamoff size = source.tellg(); source.seekg(std::max<std::streamoff>(0, size - 200000));
        std::string raw((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        std::string content = py::decode_utf8_replace(raw);
        text->configure({{"state", "normal"}}); text->delete_("1.0", "end"); std::string tag = "info";
        for (auto& line : py::splitlines(content)) {
            if (line.find(" ERROR ") != std::string::npos || line.find(" CRITICAL ") != std::string::npos) tag = "error";
            else if (line.find(" WARNING ") != std::string::npos) tag = "warning";
            else if (line.find(" INFO ") != std::string::npos) tag = "info";
            text->insert("end", line + "\n", tag);
        }
        text->configure({{"state", "disabled"}}); text->see("end");
    } catch (const py::OSError&) {
        app->log->exception("Nie udało się odczytać historii logu");
    }
}

void LogDocument::destroy() {
    // W Pythonie zniszczony dokument rozpoznaje winfo_exists(); w C++
    // wskaźnik w aplikacji jest zerowany, aby nie wskazywał zwolnionego obiektu.
    if (app->developer_log_document == this) app->developer_log_document = nullptr;
    dock_workspace::DockDocument::destroy();
}

// ---------------------------------------------------------------- install / dispose

void install(App* app) {
    auto logger = getLogger("MebelStudio3D." + std::to_string((std::uintptr_t)app)); logger->setLevel(INFO); logger->propagate = false;
    auto memory = std::make_shared<MemoryHandler>(); logger->addHandler(memory);
    fs::path base = py::to_path(py::getenv("LOCALAPPDATA").value_or(py::path_str(py::home()))) / "MebelStudio3D" / "logi";
    app->developer_log_path = base / "MebelStudio3D.log"; app->developer_memory = memory; app->log = logger;
    _apps().insert(app); _observe_messages();
    try {
        std::error_code error;
        fs::create_directories(base, error);
        if (error) throw py::OSError(error, py::path_str(base));
        auto disk = std::make_shared<RotatingFileHandler>(app->developer_log_path, 2'000'000, 3);
        disk->setFormatter(Formatter("%(asctime)s %(levelname)s %(message)s")); logger->addHandler(disk);
    } catch (const py::OSError&) {
        logger->warning("Plik logu niedostępny; diagnostyka pozostaje w pamięci.", true);
    }
    Hooks hooks; hooks.previous = std::get_terminate(); hooks.previous_logger = _terminate_logger; hooks.logger = logger;
    _hooks()[app] = hooks;
    _terminate_logger = logger; std::set_terminate(_diagnostic_exception_hook);
    app->report_callback_exception = [logger, app](std::exception_ptr value) {
        logger->error("Błąd funkcji interfejsu", value);
        app->status.set("Błąd: " + exception_message(value) + " · szczegóły w panelu Logi");
    };
    app->createcommand("bgerror", [logger, app](const std::vector<std::string>& args) {
        std::string message = args.empty() ? "" : args[0];
        std::string details;
        try {
            details = app->getvar("errorInfo");
        } catch (const tk::TclError&) {
            details = message;
        }
        logger->error(std::format("Błąd Tcl/Tk: {}\n{}", message, details));
        app->status.set("Błąd interfejsu · szczegóły w panelu Logi");
        return std::string();
    });
    app->open_developer_logs = [app]() -> LogDocument* {
        LogDocument* doc = app->developer_log_document;
        if (doc == nullptr || !doc->winfo_exists()) { doc = new LogDocument(app); app->developer_log_document = doc; }
        app->workspace->select(doc); return doc;
    };
    // „Python %s” — w wersji C++ wersja standardu języka; program jest zawsze skompilowany (EXE True).
    logger->info(std::format("Uruchomiono MebelStudio3D 0.15 · C++ {} · EXE {}", __cplusplus, "True"));
    // native_model.available()/diagnostics(): silnik C++20 jest tu częścią programu — pominięte.
    app->changed_observers.push_back([logger, app] {
        logger->info(std::format("Zmieniono projekt {} · {} części", app->project->name, app->project->parts.size()));
    });
}

void dispose(App* app) {
    _apps().erase(app);
    auto found = _hooks().find(app);
    if (found != _hooks().end()) {
        if (std::get_terminate() == _diagnostic_exception_hook && _terminate_logger == found->second.logger) {
            std::set_terminate(found->second.previous);
            _terminate_logger = found->second.previous_logger;
        }
        _hooks().erase(found);
    }
    if (app->log) {
        for (auto& handler : std::vector<std::shared_ptr<Handler>>(app->log->handlers)) {
            handler->close(); app->log->removeHandler(handler);
        }
    }
}

}  // namespace developer_logs
