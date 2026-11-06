// Offline desktop application. C++20.
#include "app.h"

#include <iostream>

#ifndef MEBEL_LAUNCHER  // przy MEBEL_LAUNCHER punkt wejścia jest w launcher.cpp
int main(int argc, char** argv) {
    ARGV.assign(argv, argv + argc);
    try { App().mainloop(); }
    catch (const std::exception& exc) {
        std::string error = exc.what();
        try { py::write_text(py::path_str(py::home() / "MebelStudio3D-blad.txt"), error); }
        catch (const py::OSError&) {}
        try { tk::messagebox::showerror("Błąd uruchomienia", "Nie udało się uruchomić aplikacji.\n" + error.substr(error.size() > 1500 ? error.size() - 1500 : 0)); }
        catch (const std::exception&) { std::cout << error << std::endl; }
        return 1;
    }
    return 0;
}
#endif
