// Najcieńsza warstwa nad C API Tcl/Tk (jedyny plik z <tcl.h>/<tk.h> to tkcore.cpp).
// Wszystkie "słowa" polecenia to zwykłe std::string; cytowanie robi Tcl (Tcl_EvalObjv),
// więc spacje, nawiasy i polskie znaki w tekstach są bezpieczne.
#pragma once
#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace tkcore {

using Args = std::vector<std::string>;
using Handler = std::function<std::string(const Args&)>;

// Tworzy interpreter Tcl i inicjuje Tk (okno główne "."). Idempotentne. Rzuca tk::TclError.
void init();
bool initialized();
// Wykonuje polecenie Tcl (words[0] = polecenie). Rzuca tk::TclError z komunikatem Tcl.
std::string eval(const Args& words);

// Rejestruje polecenie Tcl o automatycznej nazwie `prefix<n>`; zwraca nazwę.
// once=true: polecenie usuwa się samo po wykonaniu (after).
std::string define(const std::string& prefix, Handler handler, bool once = false);
// Rejestruje polecenie o podanej nazwie (np. bgerror).
void define_named(const std::string& name, Handler handler);
void remove(const std::string& name) noexcept;
// Wyjątek z procedury obsługi trafia tutaj (Tk::report_callback_exception).
void set_exception_sink(std::function<void(std::exception_ptr)> sink);

std::string get_var(const std::string& name);  // rzuca tk::TclError
void set_var(const std::string& name, const std::string& value);

// Wpisuje piksele RGBA (w*h*4 bajtów) do istniejącego obrazu typu photo.
void put_photo(const std::string& name, int width, int height, const std::vector<std::uint8_t>& rgba);

void main_loop();

// Lista Tcl <-> wektor tekstów
std::string merge(const Args& items);
Args split(const std::string& list);  // rzuca tk::TclError

}  // namespace tkcore
