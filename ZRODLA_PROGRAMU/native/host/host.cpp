#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstddef>
#define MS_WINDOWS
#define PyAPI_FUNC(ReturnType) ReturnType
using Py_ssize_t = std::ptrdiff_t;
#include "pyconfig313.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using InitializeFromConfig = PyStatus (*)(const PyConfig*);
using RunSimpleStringFlags = int (*)(const char*, void*);
using FinalizeEx = int (*)();
using PythonGetVersion = const char* (*)();

static fs::path logDirectory() {
    wchar_t local[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) {
        fs::path path = fs::path(local) / L"MebelStudio3D";
        std::error_code error;
        fs::create_directories(path, error);
        if (!error) return path;
    }
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary)) throw std::runtime_error("Cannot locate a writable log directory");
    fs::path path = fs::path(temporary) / L"MebelStudio3D";
    fs::create_directories(path);
    return path;
}

static void appendLog(const fs::path& path, const std::string& message) {
    std::error_code error;
    if (fs::is_regular_file(path, error) && fs::file_size(path, error) > 1024 * 1024) {
        const fs::path backup = path.wstring() + L".1";
        fs::remove(backup, error);
        fs::rename(path, backup, error);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    SYSTEMTIME now{}; GetLocalTime(&now);
    stream << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' '
           << now.wHour << ':' << now.wMinute << ':' << now.wSecond << " PID " << GetCurrentProcessId() << ' ' << message << '\n';
}

static std::wstring wide(const std::string& value) {
    int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), int(value.size()), nullptr, 0);
    std::wstring result(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), int(value.size()), result.data(), count);
    return result;
}

static std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

static std::string literal(const std::wstring& value) {
    std::string result = "\"";
    for (unsigned char c : utf8(value)) {
        if (c == '\\' || c == '"') { result += '\\'; result += char(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += char(c);
    }
    return result + "\"";
}

template<class T> static T symbol(HMODULE module, const char* name) {
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!function) throw std::runtime_error(std::string("Missing bundled Python API: ") + name);
    return function;
}

static int launch(int argc, wchar_t** argv) {
    fs::path hostLog, stderrLog;
    bool diagnostic = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]) == L"--host-test" || std::wstring(argv[index]) == L"--self-test" || std::wstring(argv[index]) == L"-c") diagnostic = true;
    }
    try {
        const fs::path logs = logDirectory();
        hostLog = logs / L"native-host.log";
        stderrLog = logs / L"native-host-stderr.log";
        appendLog(hostLog, "Starting C++ host 0.15");
        appendLog(stderrLog, "Starting embedded application 0.15");
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (!length || length >= buffer.size()) throw std::runtime_error("Nie można ustalić ścieżki programu.");
        const fs::path executable(std::wstring(buffer.data(), length));
        fs::path root = executable.parent_path(), runtime = root / L"_internal", app = root / L"app";
        std::vector<std::wstring> arguments{executable.wstring()};
        for (int index = 1; index < argc; ++index) {
            if ((std::wstring(argv[index]) == L"--host-runtime" || std::wstring(argv[index]) == L"--host-app") && index + 1 < argc) {
                const auto option = std::wstring(argv[index]);
                auto path = fs::absolute(argv[++index]);
                if (option == L"--host-runtime") runtime = path; else app = path;
            } else arguments.emplace_back(argv[index]);
        }
        if (!fs::is_regular_file(runtime / L"python313.dll") || !fs::is_regular_file(runtime / L"base_library.zip"))
            throw std::runtime_error("Brakuje plików środowiska programu. Rozpakuj cały ZIP do jednego folderu i uruchom MebelStudio3D.exe z tego folderu.");
        if (!fs::is_regular_file(app / L"launcher.py")) throw std::runtime_error("Brakuje folderu app z plikami programu. Rozpakuj cały ZIP.");
        // DLLs are resolved only from the package and Windows system directories.
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) throw std::runtime_error("Cannot configure DLL search paths");
        std::vector<DLL_DIRECTORY_COOKIE> cookies;
        auto addDirectory = [&](const fs::path& path) {
            if (auto cookie = AddDllDirectory(path.c_str())) cookies.push_back(cookie);
        };
        addDirectory(runtime);
        for (const auto& item : fs::directory_iterator(runtime)) {
            if (item.is_directory() && (item.path().extension() == L".libs" || item.path().filename() == L"vtkmodules" || item.path().filename() == L"OCP"))
                addDirectory(item.path());
        }
        SetEnvironmentVariableW(L"TCL_LIBRARY", (runtime / L"_tcl_data").c_str());
        SetEnvironmentVariableW(L"TK_LIBRARY", (runtime / L"_tk_data").c_str());
        HMODULE python = LoadLibraryExW((runtime / L"python313.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!python) throw std::runtime_error("Nie można wczytać wbudowanego środowiska programu (błąd Windows " + std::to_string(GetLastError()) + ").");
        const auto version = symbol<PythonGetVersion>(python, "Py_GetVersion");
        if (std::string(version()).rfind("3.13.", 0) != 0) throw std::runtime_error("Niezgodna wersja środowiska programu. Użyj kompletnej paczki 0.15.");
        const auto configInit = symbol<decltype(&PyConfig_InitIsolatedConfig)>(python, "PyConfig_InitIsolatedConfig");
        const auto configSet = symbol<decltype(&PyConfig_SetString)>(python, "PyConfig_SetString");
        const auto configClear = symbol<decltype(&PyConfig_Clear)>(python, "PyConfig_Clear");
        const auto append = symbol<decltype(&PyWideStringList_Append)>(python, "PyWideStringList_Append");
        const auto initialize = symbol<InitializeFromConfig>(python, "Py_InitializeFromConfig");
        const auto run = symbol<RunSimpleStringFlags>(python, "PyRun_SimpleStringFlags");
        const auto finalize = symbol<FinalizeEx>(python, "Py_FinalizeEx");
        const auto check = [](PyStatus status) { if (status._type != PyStatus::_PyStatus_TYPE_OK) throw std::runtime_error(status.err_msg ? status.err_msg : "Python initialization failed"); };
        PyConfig config;
        configInit(&config);
        config.site_import = 0;
        config.write_bytecode = 0;
        config.parse_argv = 0;
        config.install_signal_handlers = 0;
        config.use_environment = 0;
        config.user_site_directory = 0;
        config.pathconfig_warnings = 0;
        config.module_search_paths_set = 1;
        check(configSet(&config, &config.home, runtime.c_str()));
        check(configSet(&config, &config.executable, executable.c_str()));
        check(configSet(&config, &config.program_name, executable.c_str()));
        check(configSet(&config, &config.stdio_encoding, L"utf-8"));
        check(append(&config.module_search_paths, app.c_str()));
        check(append(&config.module_search_paths, (runtime / L"base_library.zip").c_str()));
        check(append(&config.module_search_paths, runtime.c_str()));
        for (const auto& value : arguments) check(append(&config.argv, value.c_str()));
        const auto status = initialize(&config);
        configClear(&config);
        check(status);
        std::ostringstream script;
        script << "import sys, os, json, traceback, runpy\nfrom pathlib import Path\n"
               << "sys.stderr = open(" << literal(stderrLog.wstring()) << ", 'a', encoding='utf-8', buffering=1)\nsys.stdout = sys.stderr\n"
               << "sys.frozen = True\nsys.dont_write_bytecode = True\nsys._MEIPASS = " << literal(runtime.wstring()) << "\n"
               // Tcl consults CRT getenv; Win32 SetEnvironmentVariable alone is
               // insufficient when the CRT environment was initialized earlier.
               << "os.environ['TCL_LIBRARY'] = str(Path(sys._MEIPASS)/'_tcl_data')\n"
               << "os.environ['TK_LIBRARY'] = str(Path(sys._MEIPASS)/'_tk_data')\n"
               << "sys._base_executable = sys.executable\n"
               << "sys._mebel_native_host = True\n"
               << "try:\n"
               << "    if len(sys.argv) > 2 and sys.argv[1] == '--host-test':\n"
               << "        import core, tkinter, vtkmodules.vtkCommonCore, cadquery, numpy\n"
               << "        result = dict(ok=True, python=sys.version, executable=sys.executable, pid=os.getpid(), embedded=True, frozen=sys.frozen, modules=dict(core=core.__file__, tkinter=tkinter.__file__, vtk=vtkmodules.vtkCommonCore.__file__, cadquery=cadquery.__file__, numpy=numpy.__file__), paths=sys.path)\n"
               << "        Path(sys.argv[2]).write_text(json.dumps(result, indent=2), encoding='utf-8')\n"
               << "    elif len(sys.argv) > 2 and sys.argv[1] == '-c':\n"
               << "        exec(sys.argv[2], {'__name__':'__main__'})\n"
               << "    else:\n"
               << "        runpy.run_path(" << literal((app / L"launcher.py").wstring()) << ", run_name='__main__')\n"
               << "except SystemExit as exc:\n"
               << "    if exc.code not in (None, 0): raise RuntimeError('Application exit: %s' % exc.code)\n"
               << "except BaseException:\n"
               << "    text=traceback.format_exc()\n"
               << "    if len(sys.argv)>2 and sys.argv[1]=='--host-test': Path(sys.argv[2]).write_text(json.dumps(dict(ok=False,error=text),indent=2),encoding='utf-8')\n"
               << "    raise\n";
        const int result = run(script.str().c_str(), nullptr);
        appendLog(hostLog, result == 0 ? "Application returned normally" : "Python startup/application error; see native-host-stderr.log");
        const int final = finalize();
        // Unload only after finalization; native modules hold Python references.
        FreeLibrary(python);
        for (auto cookie : cookies) RemoveDllDirectory(cookie);
        if (result != 0 && !diagnostic) {
            const std::wstring message = L"Nie udało się uruchomić programu. Szczegóły zapisano w:\n" + stderrLog.wstring();
            MessageBoxW(nullptr, message.c_str(), L"MebelStudio3D — błąd uruchomienia", MB_OK | MB_ICONERROR);
        }
        return result == 0 && final == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        if (!hostLog.empty()) appendLog(hostLog, std::string("ERROR ") + error.what());
        std::cerr << error.what() << '\n';
        if (!diagnostic) {
            const std::wstring message = wide(error.what()) + L"\n\nLog: " + hostLog.wstring();
            MessageBoxW(nullptr, message.c_str(), L"MebelStudio3D — błąd uruchomienia", MB_OK | MB_ICONERROR);
        }
        return 2;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Tk 8.6 and native children share physical pixel coordinates at System DPI.
    // The manifest sets awareness before any window is created.
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    const int result = launch(argc, argv);
    LocalFree(argv);
    return result;
}
