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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

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

int command_proc(void* data, Tcl_Interp* interp, Size objc, Tcl_Obj* const objv[]) {
    auto* callback = static_cast<Callback*>(data);
    Args args;
    args.reserve(objc > 0 ? static_cast<std::size_t>(objc) - 1 : 0);
    for (Size i = 1; i < objc; ++i) args.emplace_back(Tcl_GetString(objv[i]));
    std::string result;
    // Kopie, bo polecenie jednorazowe usuwa własną strukturę po wykonaniu.
    const bool once = callback->once;
    const std::string name = callback->name;
    try {
        result = callback->handler(args);
    } catch (...) {
        report(std::current_exception());
        result.clear();
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(result.data(), static_cast<Size>(result.size())));
    if (once) Tcl_DeleteCommand(interp, name.c_str());
    return TCL_OK;
}

void command_deleted(void* data) { delete static_cast<Callback*>(data); }

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
    ObjList objs(words);
    const int code = Tcl_EvalObjv(g_interp, objs.size(), objs.data(), TCL_EVAL_GLOBAL);
    std::string result = result_text();
    if (code != TCL_OK) {
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

void set_exception_sink(std::function<void(std::exception_ptr)> sink) { g_sink = std::move(sink); }

std::string get_var(const std::string& name) {
    require();
    const char* value = Tcl_GetVar2(g_interp, name.c_str(), nullptr, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG);
    if (!value) throw tk::TclError(result_text());
    return value;
}

void set_var(const std::string& name, const std::string& value) {
    require();
    if (!Tcl_SetVar2(g_interp, name.c_str(), nullptr, value.c_str(), TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG)) {
        throw tk::TclError(result_text());
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
        throw tk::TclError(result_text());
    }
}

void main_loop() {
    require();
    Tk_MainLoop();
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
        throw tk::TclError(g_interp ? result_text() : "Niepoprawna lista Tcl: " + list);
    }
    Args result;
    result.reserve(static_cast<std::size_t>(count));
    for (Size i = 0; i < count; ++i) result.emplace_back(items[i]);
    Tcl_Free(reinterpret_cast<char*>(items));
    return result;
}

}  // namespace tkcore
