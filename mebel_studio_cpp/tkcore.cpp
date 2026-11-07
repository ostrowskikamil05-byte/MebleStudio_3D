#include <unordered_map>
// Jedyne miejsce z nagłówkami Tcl/Tk (nagłówki X11 z tk.h definiują makra None/Bool/Status...).
// UWAGA: katalog projektu NIE może być w ścieżce -I, bo <tk.h> trafiłby do naszego tk.h.
#include "tkcore.h"

#include "tk.h"

// windows.h przed nagłówkami Tcl/Tk (jak robi to _tkinter) — nagłówki X11 z tk.h definiują
// makra Bool/Status/None, które psują późniejsze dołączenie nagłówków systemowych.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <tcl.h>
#include <tk.h>

#ifndef _TK
#error "Nie znaleziono nagłówka Tk (<tk.h>). Nie dodawaj katalogu projektu przez -I; użyj -iquote."
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace tkcore {
namespace {

#ifdef TCL_SIZE_MAX
using Size = Tcl_Size;
#else
using Size = int;
#endif

Tcl_Interp* g_interp = nullptr;
std::function<void(std::exception_ptr)> g_sink;
int g_counter = 0;

namespace fs = std::filesystem;

struct Callback {
    Handler handler;
    std::string name;
    bool once = false;
};

class ObjList {
public:
    explicit ObjList(const Args& words) {
        objs_.reserve(words.size());
        for (const std::string& word : words) {
            Tcl_Obj* obj = Tcl_NewStringObj(word.data(), static_cast<Size>(word.size()));
            Tcl_IncrRefCount(obj);
            objs_.push_back(obj);
        }
    }
    ObjList(const ObjList&) = delete;
    ObjList& operator=(const ObjList&) = delete;
    ~ObjList() {
        for (Tcl_Obj* obj : objs_) Tcl_DecrRefCount(obj);
    }
    Size size() const { return static_cast<Size>(objs_.size()); }
    Tcl_Obj* const* data() const { return objs_.data(); }
private:
    std::vector<Tcl_Obj*> objs_;
};

// MEBEL_TRACE_TCL=1: każde polecenie Tcl (i błąd) trafia na stderr — do diagnozy pierwszego uruchomienia.
bool tracing() {
    static const bool enabled = [] {
        const char* value = std::getenv("MEBEL_TRACE_TCL");
        return value && *value && *value != '0';
    }();
    return enabled;
}

std::string shorten(const std::string& text, std::size_t limit = 100) {
    std::string out = text.size() > limit ? text.substr(0, limit) + "..." : text;
    for (char& c : out) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

void trace_words(const char* prefix, const Args& words) {
    std::string line = std::string("[tcl] ") + prefix;
    for (std::size_t i = 0; i < words.size() && i < 8; ++i) line += ' ' + shorten(words[i]);
    std::fprintf(stderr, "%s\n", line.c_str());
}

void require() {
    if (!g_interp) throw tk::TclError("Interpreter Tcl/Tk nie jest zainicjowany");
}

std::string result_text() { return Tcl_GetString(Tcl_GetObjResult(g_interp)); }

void report(std::exception_ptr error) {
    if (g_sink) {
        try {
            g_sink(error);
            return;
        } catch (...) {
        }
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& ex) {
        std::cerr << "Błąd funkcji interfejsu: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "Nieznany błąd funkcji interfejsu\n";
    }
}

ProfileHook g_profile_hook;
double g_eval_threshold_ms = 25;
// Celowo nigdy niszczone: polecenia Tcl bywają usuwane przy zamykaniu interpretera, już po statycznych destruktorach.
std::unordered_map<std::string, std::string>& labels() {
    static auto* map = new std::unordered_map<std::string, std::string>();
    return *map;
}
EvalStats g_eval_stats;

int command_proc(void* data, Tcl_Interp* interp, Size objc, Tcl_Obj* const objv[]) {
    auto* callback = static_cast<Callback*>(data);
    Args args;
    args.reserve(objc > 0 ? static_cast<std::size_t>(objc) - 1 : 0);
    for (Size i = 1; i < objc; ++i) args.emplace_back(Tcl_GetString(objv[i]));
    std::string result;
    // Kopie, bo polecenie jednorazowe usuwa własną strukturę po wykonaniu.
    const bool once = callback->once;
    const std::string name = callback->name;
    const auto started = std::chrono::steady_clock::now();
    try {
        result = callback->handler(args);
    } catch (...) {
        report(std::current_exception());
        result.clear();
    }
    if (g_profile_hook) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        auto label = labels().find(name);
        g_profile_hook("callback", label != labels().end() ? label->second : name, ms);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(result.data(), static_cast<Size>(result.size())));
    if (once) Tcl_DeleteCommand(interp, name.c_str());
    return TCL_OK;
}

void command_deleted(void* data) {
    auto* callback = static_cast<Callback*>(data);
    labels().erase(callback->name);
    delete callback;
}

void create_command(const std::string& name, Handler handler, bool once) {
    require();
    auto* callback = new Callback{std::move(handler), name, once};
    Tcl_CreateObjCommand(g_interp, name.c_str(), command_proc, callback, command_deleted);
}

// --- ścieżki do bibliotek skryptowych Tcl/Tk ------------------------------------------------

fs::path executable_dir() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return fs::current_path();
    buffer.resize(length);
    return fs::path(buffer).parent_path();
#else
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

void set_environment(const char* name, const fs::path& value) {
#ifdef _WIN32
    std::wstring wide_name;
    for (const char* p = name; *p; ++p) wide_name.push_back(static_cast<wchar_t>(*p));
    SetEnvironmentVariableW(wide_name.c_str(), value.wstring().c_str());
    _wputenv_s(wide_name.c_str(), value.wstring().c_str());
#else
    setenv(name, value.string().c_str(), 1);
#endif
}

// Jeśli TCL_LIBRARY / TK_LIBRARY nie są ustawione, szuka skryptów obok programu
// (w tym w układzie PyInstallera: _internal/_tcl_data, _internal/_tk_data).
void find_script_library(const char* variable, const char* marker, const std::vector<std::string>& candidates) {
    const char* current = std::getenv(variable);
    if (current && *current) return;
    const fs::path base = executable_dir();
    std::error_code ec;
    for (const std::string& relative : candidates) {
        const fs::path directory = base / relative;
        if (fs::exists(directory / marker, ec)) {
            set_environment(variable, directory.lexically_normal());
            return;
        }
    }
}

void configure_libraries() {
    find_script_library("TCL_LIBRARY", "init.tcl",
                        {"_internal/_tcl_data", "_tcl_data", "lib/tcl8.6", "tcl8.6", "../lib/tcl8.6", "lib/tcl9.0", "tcl9.0",
                         "../lib/tcl9.0", "_internal/tcl8.6"});
    find_script_library("TK_LIBRARY", "tk.tcl",
                        {"_internal/_tk_data", "_tk_data", "lib/tk8.6", "tk8.6", "../lib/tk8.6", "lib/tk9.0", "tk9.0",
                         "../lib/tk9.0", "_internal/tk8.6"});
}

}  // namespace

void init() {
    if (g_interp) return;
    if (tracing()) std::fprintf(stderr, "[tcl] tkcore zbudowany %s %s\n", __DATE__, __TIME__);
    configure_libraries();
    Tcl_FindExecutable(nullptr);
    Tcl_Interp* interp = Tcl_CreateInterp();
    if (!interp) throw tk::TclError("Tcl_CreateInterp zwróciło NULL");
    if (Tcl_Init(interp) != TCL_OK) {
        std::string message = Tcl_GetString(Tcl_GetObjResult(interp));
        throw tk::TclError("Tcl_Init: " + message);
    }
    Tcl_SetVar2(interp, "argc", nullptr, "0", TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, "argv", nullptr, "", TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, "argv0", nullptr, "MebelStudio3D", TCL_GLOBAL_ONLY);
    if (Tk_Init(interp) != TCL_OK) {
        std::string message = Tcl_GetString(Tcl_GetObjResult(interp));
        throw tk::TclError("Tk_Init: " + message);
    }
    g_interp = interp;
}

bool initialized() { return g_interp != nullptr; }

std::string eval(const Args& words) {
    require();
    if (words.empty()) return {};
    if (tracing()) trace_words("", words);
    ObjList objs(words);
    const auto started = std::chrono::steady_clock::now();
    const int code = Tcl_EvalObjv(g_interp, objs.size(), objs.data(), TCL_EVAL_GLOBAL);
    const double eval_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    ++g_eval_stats.count;
    g_eval_stats.total_ms += eval_ms;
    if (g_profile_hook && eval_ms >= g_eval_threshold_ms) {
        std::string what;
        for (std::size_t i = 0; i < words.size() && i < 4; ++i) what += (i ? " " : "") + shorten(words[i], 60);
        g_profile_hook("eval", what, eval_ms);
    }
    std::string result = result_text();
    if (code != TCL_OK) {
        if (tracing()) std::fprintf(stderr, "[tcl]   -> BŁĄD (kod %d): %s\n", code, shorten(result, 300).c_str());
        // Komunikat Tcl + polecenie, które go wywołało (ułatwia lokalizację błędu w interfejsie).
        std::string message = result.empty() ? "pusty komunikat Tcl (kod " + std::to_string(code) + ")" : result;
        message += "\n  polecenie Tcl:";
        for (std::size_t i = 0; i < words.size() && i < 5; ++i) {
            message += ' ' + (words[i].size() > 80 ? words[i].substr(0, 80) + "..." : words[i]);
        }
        const char* info = Tcl_GetVar2(g_interp, "errorInfo", nullptr, TCL_GLOBAL_ONLY);
        if (info && *info) {
            std::string text = info;
            if (text.size() > 700) text.resize(700);
            message += "\n  errorInfo: " + text;
        }
        throw tk::TclError(message);
    }
    return result;
}

std::string define(const std::string& prefix, Handler handler, bool once) {
    std::string name = prefix + std::to_string(++g_counter);
    create_command(name, std::move(handler), once);
    return name;
}

void define_named(const std::string& name, Handler handler) { create_command(name, std::move(handler), false); }

void remove(const std::string& name) noexcept {
    if (g_interp) Tcl_DeleteCommand(g_interp, name.c_str());
}

void set_profile_hook(ProfileHook hook, double eval_threshold_ms) {
    g_profile_hook = std::move(hook);
    g_eval_threshold_ms = eval_threshold_ms;
}
void set_label(const std::string& command, const std::string& label) { labels()[command] = label; }
EvalStats eval_stats() { return g_eval_stats; }

void set_exception_sink(std::function<void(std::exception_ptr)> sink) { g_sink = std::move(sink); }

std::string get_var(const std::string& name) {
    require();
    const char* value = Tcl_GetVar2(g_interp, name.c_str(), nullptr, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG);
    if (!value) {
        std::string message = result_text();
        if (tracing()) std::fprintf(stderr, "[tcl] get_var %s -> BŁĄD: %s\n", name.c_str(), message.c_str());
        throw tk::TclError(message.empty() ? "nie można odczytać zmiennej Tcl \"" + name + "\"" : message);
    }
    return value;
}

void set_var(const std::string& name, const std::string& value) {
    require();
    if (!Tcl_SetVar2(g_interp, name.c_str(), nullptr, value.c_str(), TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG)) {
        std::string message = result_text();
        throw tk::TclError(message.empty() ? "nie można ustawić zmiennej Tcl \"" + name + "\"" : message);
    }
}

void put_photo(const std::string& name, int width, int height, const std::vector<std::uint8_t>& rgba) {
    require();
    if (width < 0 || height < 0 || rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        throw tk::TclError("Za mało danych RGBA dla obrazu " + std::to_string(width) + "x" + std::to_string(height));
    }
    Tk_PhotoHandle handle = Tk_FindPhoto(g_interp, name.c_str());
    if (!handle) throw tk::TclError("Nie znaleziono obrazu Tk: " + name);
    Tk_PhotoImageBlock block;
    block.pixelPtr = const_cast<unsigned char*>(rgba.data());
    block.width = width;
    block.height = height;
    block.pitch = width * 4;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    if (Tk_PhotoPutBlock(g_interp, handle, &block, 0, 0, width, height, TK_PHOTO_COMPOSITE_SET) != TCL_OK) {
        std::string message = result_text();
        throw tk::TclError(message.empty() ? "Tk_PhotoPutBlock zgłosiło błąd dla obrazu " + name : message);
    }
}

// ------------------------------------------------------------------------------ próbkowanie stosu podczas postojów (Windows x64)
// Wątek pomocniczy co 40 ms sprawdza, czy pętla zdarzeń tkwi w jednym zdarzeniu dłużej niż 300 ms. Jeśli tak, zatrzymuje na
// chwilę wątek główny, rozwija jego stos (tablice unwind z .pdata) i zapisuje adresy. Po zakończeniu zdarzenia pętla
// zamienia adresy na „moduł!funkcja+offset” (najbliższy eksport DLL) i raportuje najczęstsze miejsca postoju.
#if defined(_WIN32) && defined(_WIN64)
namespace sampler {

constexpr int MAX_FRAMES = 28;
struct Sample {
    DWORD64 pc[MAX_FRAMES];
    int depth;
};

std::atomic<long long> g_event_start{0};
std::atomic<int> g_sample_count{0};
std::mutex g_mutex;
std::vector<Sample> g_samples;
HANDLE g_thread = nullptr;
bool g_started = false;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

DWORD WINAPI worker(LPVOID) {
    for (;;) {
        Sleep(40);
        const long long started = g_event_start.load();
        if (started == 0 || now_ms() - started < 300) continue;
        Sample sample{};
        if (SuspendThread(g_thread) == static_cast<DWORD>(-1)) continue;
        // Uwaga: między Suspend a Resume nie wolno alokować pamięci ani brać blokad (wątek główny mógłby je trzymać).
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(g_thread, &context)) {
            for (int i = 0; i < MAX_FRAMES; ++i) {
                const DWORD64 pc = context.Rip;
                if (pc == 0) break;
                sample.pc[sample.depth++] = pc;
                DWORD64 image_base = 0;
                PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(pc, &image_base, nullptr);
                if (entry == nullptr) {
                    // Funkcja liściowa bez danych unwind: adres powrotu leży na szczycie stosu.
                    DWORD64 value = 0;
                    SIZE_T read = 0;
                    if (context.Rsp == 0 || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(context.Rsp), &value, sizeof(value), &read) || read != sizeof(value)) break;
                    context.Rip = value;
                    context.Rsp += 8;
                } else {
                    PVOID handler_data = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, entry, &context, &handler_data, &establisher, nullptr);
                }
            }
        }
        ResumeThread(g_thread);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_samples.size() < 400) {
            g_samples.push_back(sample);
            g_sample_count.store(static_cast<int>(g_samples.size()));
        }
    }
    return 0;
}

void start() {
    if (g_started) return;
    g_started = true;
    if (const char* off = std::getenv("MEBEL_NO_SAMPLER"); off && *off == '1') return;
    g_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    if (!g_thread) return;
    HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

void begin_event() {
    if (!g_thread) return;
    if (g_sample_count.load() != 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_samples.clear();
        g_sample_count.store(0);
    }
    g_event_start.store(now_ms());
}

struct Place {
    std::string module;
    std::string symbol;
    unsigned long long offset = 0;  // względem symbolu (gdy znany), inaczej względem początku modułu
    bool system = false;
};

std::string narrow_ascii(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

Place locate(DWORD64 pc) {
    static std::map<DWORD64, Place> cache;
    if (auto found = cache.find(pc); found != cache.end()) return found->second;
    Place place;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(pc), &module) || !module) {
        place.module = "?";
        place.offset = pc;
        return cache[pc] = place;
    }
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring full(path, length);
    wchar_t windows_dir[MAX_PATH] = {};
    UINT windows_length = GetSystemWindowsDirectoryW(windows_dir, MAX_PATH);
    if (windows_length > 0 && full.size() > windows_length && _wcsnicmp(full.c_str(), windows_dir, windows_length) == 0) place.system = true;
    auto slash = full.find_last_of(L"\\/");
    place.module = narrow_ascii(slash == std::wstring::npos ? full : full.substr(slash + 1));
    const unsigned long long rva = pc - reinterpret_cast<DWORD64>(module);
    place.offset = rva;
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {
            const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (directory.VirtualAddress != 0 && directory.Size != 0) {
                const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
                const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
                const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
                const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
                DWORD best = 0;
                const char* best_name = nullptr;
                for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
                    const DWORD function = functions[ordinals[i]];
                    if (function >= directory.VirtualAddress && function < directory.VirtualAddress + directory.Size) continue;  // przekierowanie
                    if (function <= rva && function >= best) {
                        best = function;
                        best_name = reinterpret_cast<const char*>(base + names[i]);
                    }
                }
                if (best_name) {
                    place.symbol = best_name;
                    place.offset = rva - best;
                }
            }
        }
    }
    return cache[pc] = place;
}

std::string label(const Place& place) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "+0x%llx", place.offset);
    return place.symbol.empty() ? place.module + buffer : place.module + "!" + place.symbol + buffer;
}

std::string top(const std::map<std::string, int>& histogram, int total, size_t count) {
    std::vector<std::pair<std::string, int>> rows(histogram.begin(), histogram.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string out;
    for (size_t i = 0; i < rows.size() && i < count; ++i) {
        if (i) out += "  |  ";
        out += std::to_string(rows[i].second * 100 / std::max(total, 1)) + "% " + rows[i].first;
    }
    return out;
}

void end_event(double ms) {
    if (!g_thread) return;
    g_event_start.store(0);
    if (g_sample_count.load() < 2) return;
    std::vector<Sample> taken;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        taken.swap(g_samples);
        g_sample_count.store(0);
    }
    if (taken.size() < 2 || !g_profile_hook) return;
    std::map<std::string, int> chains, leaves, first_app, first_exe;
    for (const Sample& sample : taken) {
        std::string chain, last_module, leaf, app_frame, exe_frame;
        int distinct = 0;
        for (int i = 0; i < sample.depth; ++i) {
            const Place place = locate(sample.pc[i]);
            if (i == 0) leaf = label(place);
            if (place.module != last_module && distinct < 7) {
                chain += (chain.empty() ? "" : " < ") + place.module;
                last_module = place.module;
                ++distinct;
            }
            if (app_frame.empty() && !place.system) app_frame = label(place);
            if (exe_frame.empty() && place.module.size() > 4 && place.module.substr(place.module.size() - 4) == ".exe") exe_frame = label(place);
        }
        ++chains[chain];
        ++leaves[leaf];
        if (!app_frame.empty()) ++first_app[app_frame];
        if (!exe_frame.empty()) ++first_exe[exe_frame];
    }
    const int total = static_cast<int>(taken.size());
    g_profile_hook("stack", "próbek: " + std::to_string(total) + " · moduły od szczytu stosu: " + top(chains, total, 3), ms);
    g_profile_hook("stack", "szczyt stosu: " + top(leaves, total, 4), ms);
    g_profile_hook("stack", "pierwsza ramka spoza systemu (Tk/sterownik/nasz kod): " + top(first_app, total, 4), ms);
    if (!first_exe.empty()) g_profile_hook("stack", "ramki naszego programu (adres do ./dev.sh addr): " + top(first_exe, total, 4), ms);
}

}  // namespace sampler
#else
namespace sampler {
inline void start() {}
inline void begin_event() {}
inline void end_event(double) {}
}  // namespace sampler
#endif

void main_loop() {
    require();
    if (!g_profile_hook) {
        Tk_MainLoop();
        return;
    }
    // Pętla z pomiarem: osobno zdarzenia okien, zegary i zadania "bezczynne" (m.in. rysowanie widgetów Tk),
    // żeby wolne odświeżanie interfejsu, które nie jest żadną naszą procedurą, dało się przypisać do rodzaju zdarzeń.
    sampler::start();
    auto timed = [](int flags, const char* kind) {
        const auto started = std::chrono::steady_clock::now();
        sampler::begin_event();
        const int handled = Tcl_DoOneEvent(flags);
        if (g_profile_hook) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            sampler::end_event(ms);
            if (handled) g_profile_hook("loop", kind, ms);
        }
        return handled != 0;
    };
    while (Tk_GetNumMainWindows() > 0) {
        bool worked = timed(TCL_WINDOW_EVENTS | TCL_FILE_EVENTS | TCL_DONT_WAIT, "zdarzenia okien");
        worked = timed(TCL_TIMER_EVENTS | TCL_DONT_WAIT, "zegary (after)") || worked;
        worked = timed(TCL_IDLE_EVENTS | TCL_DONT_WAIT, "bezczynność (rysowanie/układ Tk)") || worked;
        // Brak pracy: czekamy na zdarzenie (zegar kontrolny profilera wybudza pętlę co 50 ms).
        if (!worked) {
            const auto started = std::chrono::steady_clock::now();
            sampler::begin_event();
            Tcl_DoOneEvent(TCL_ALL_EVENTS);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            sampler::end_event(ms);
            // Zegar kontrolny wybudza pętlę co 50 ms, więc znacznie dłuższe oczekiwanie to blokada w obsłudze komunikatów systemu (np. okno natywnego widoku).
            if (ms > 120 && g_profile_hook) g_profile_hook("loop", "komunikaty systemu / okna natywne", ms);
        }
    }
}

std::string merge(const Args& items) {
    std::vector<const char*> argv;
    argv.reserve(items.size());
    for (const std::string& item : items) argv.push_back(item.c_str());
    char* merged = Tcl_Merge(static_cast<Size>(argv.size()), argv.data());
    std::string result = merged ? merged : "";
    if (merged) Tcl_Free(merged);
    return result;
}

Args split(const std::string& list) {
    Size count = 0;
    const char** items = nullptr;
    if (Tcl_SplitList(g_interp, list.c_str(), &count, &items) != TCL_OK) {
        std::string message = g_interp ? result_text() : std::string();
        throw tk::TclError(message.empty() ? "Niepoprawna lista Tcl: " + shorten(list, 200) : message);
    }
    Args result;
    result.reserve(static_cast<std::size_t>(count));
    for (Size i = 0; i < count; ++i) result.emplace_back(items[i]);
    Tcl_Free(reinterpret_cast<char*>(items));
    return result;
}

}  // namespace tkcore
