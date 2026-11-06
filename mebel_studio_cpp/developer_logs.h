// Bounded diagnostics, persisted outside the installation, with a dockable viewer.
//
// Python korzysta z modułu logging; tutaj odtworzono jego potrzebną część:
// Logger z poziomami, MemoryHandler (deque 1500) i RotatingFileHandler
// (2 000 000 bajtów, 3 kopie) z tymi samymi formatami linii.
#pragma once
#include <chrono>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace developer_logs {

// windows.h (dołączane przez nagłówki OpenCASCADE/VTK) definiuje makro ERROR
#ifdef ERROR
#undef ERROR
#endif
enum Level { NOTSET = 0, DEBUG = 10, INFO = 20, WARNING = 30, ERROR = 40, CRITICAL = 50 };

std::string level_name(int level);

struct LogRecord {
    int levelno = INFO;
    std::string levelname;
    std::string message;
    std::chrono::system_clock::time_point created;
    std::exception_ptr exc_info;  // exc_info=(kind, value, tb)
};

// logging.Formatter(fmt, datefmt): %(asctime)s, %(levelname)s, %(message)s.
class Formatter {
public:
    std::string fmt; std::optional<std::string> datefmt;
    explicit Formatter(std::string fmt = "%(message)s", std::optional<std::string> datefmt = std::nullopt);
    std::string formatTime(const LogRecord& record) const;
    std::string formatException(std::exception_ptr error) const;
    std::string format(const LogRecord& record) const;
};

class Handler {
public:
    int level = NOTSET;
    Formatter formatter;
    virtual ~Handler() = default;
    void setFormatter(const Formatter& value) { formatter = value; }
    std::string format(const LogRecord& record) const { return formatter.format(record); }
    virtual void emit(const LogRecord& record) = 0;
    void handle(const LogRecord& record);
    virtual void close() {}
};

class MemoryHandler : public Handler {
public:
    std::deque<std::tuple<int, int, std::string>> records;  // deque(maxlen=1500)
    static constexpr std::size_t maxlen = 1500;
    int sequence = 0;
    MemoryHandler();
    void emit(const LogRecord& record) override;
};

// logging.handlers.RotatingFileHandler(path, maxBytes, backupCount, encoding='utf-8')
class RotatingFileHandler : public Handler {
public:
    std::filesystem::path baseFilename;
    std::uintmax_t maxBytes; int backupCount;
    std::unique_ptr<std::ofstream> stream;
    RotatingFileHandler(const std::filesystem::path& filename, std::uintmax_t maxBytes = 0, int backupCount = 0);
    void emit(const LogRecord& record) override;
    bool shouldRollover(const std::string& message);
    void doRollover();
    void close() override;
private:
    void open();
};

class Logger {
public:
    std::string name;
    int level = NOTSET;
    bool propagate = true;
    std::vector<std::shared_ptr<Handler>> handlers;
    explicit Logger(std::string name) : name(std::move(name)) {}
    void setLevel(int value) { level = value; }
    void addHandler(std::shared_ptr<Handler> handler);
    void removeHandler(const std::shared_ptr<Handler>& handler);
    void log(int level, const std::string& message, std::exception_ptr exc_info = nullptr);
    void info(const std::string& message) { log(INFO, message); }
    void warning(const std::string& message, bool exc_info = false);
    void error(const std::string& message, std::exception_ptr exc_info = nullptr) { log(ERROR, message, exc_info); }
    // logger.exception(...) — błąd z bieżącym wyjątkiem.
    void exception(const std::string& message) { log(ERROR, message, std::current_exception()); }
};

// logging.getLogger(name): ten sam obiekt dla tej samej nazwy.
std::shared_ptr<Logger> getLogger(const std::string& name);

void _observe_messages();

class LogDocument : public dock_workspace::DockDocument {
public:
    int cursor = 0;
    tk::Text* text = nullptr;
    explicit LogDocument(App* app);
    void poll();
    void export_();  // export
    void history();
    void destroy() override;
};

void install(App* app);
void dispose(App* app);

}  // namespace developer_logs
