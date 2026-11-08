// Lokalne ustawienia wyglądu widoku 3D (panel "Silnik i jakość 3D"): oświetlenie, cienie,
// antyaliasing, tło. To preferencje programu — nie trafiają do projektu mebla.
#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include "core.h"
#include "render_style.h"
#include "ui_preferences.h"

namespace view_style {

struct State {
    bool lighting = true;  // plamy światła LED na podłodze
    bool shadows = true;   // cienie kontaktowe pod meblami
    int aa = 0;            // MSAA dla silnika OpenGL (0 = wyłączony); działa od następnego uruchomienia
    int background = 0;    // indeks palety tła
    render_style::Mode mode = render_style::Mode::Classic;  // tryb grafiki: klasyczny / realistyczny
};

inline State state;

inline bool realistic() { return state.mode == render_style::Mode::Realistic; }

// Silniki natywne (biblioteka DLL) mają stałe, wbudowane cieniowanie — nie pokażą trybu realistycznego.
inline bool native_engine_name(const std::string& engine) { return engine == "auto" || engine == "d3d11" || engine == "d3d12" || engine == "vulkan"; }

// Silnik faktycznie użyty do rysowania: w trybie realistycznym zamiast DLL idzie GPU (VTK: PBR + SSAO), a gdy go brak — renderer CPU
// z cieniowaniem, cieniami rzucanymi i płytkami podłogi. Tryb klasyczny zostaje bez zmian.
inline std::string effective_engine(const std::string& engine) { return realistic() && native_engine_name(engine) ? "gpu" : engine; }

struct Palette {
    const char* swatch;      // próbka w panelu
    const char* sky_top;     // tło widoku (góra)
    const char* sky_bottom;  // tło widoku (dół)
    const char* floor;       // podłoga
    const char* grid;        // siatka
    float floor_rgb[3];      // podłoga w silnikach natywnych
    float grid_rgb[3];       // siatka w silnikach natywnych
};

inline const std::array<Palette, 4>& palettes() {
    static const std::array<Palette, 4> list = {{
        {"#f3efe7", nullptr, nullptr, nullptr, nullptr, {0.93f, 0.92f, 0.89f}, {0.66f, 0.65f, 0.62f}},
        {"#efe3d0", "#efe6d6", "#f7f0e4", "#ebe0cc", "#d6c9b0", {0.92f, 0.88f, 0.80f}, {0.70f, 0.64f, 0.54f}},
        {"#dddfe1", "#e4e6e8", "#f2f3f4", "#e6e7e8", "#cfd1d3", {0.90f, 0.91f, 0.92f}, {0.66f, 0.67f, 0.69f}},
        {"#55585d", "#3a3d42", "#5a5e64", "#4b4e53", "#62666b", {0.30f, 0.31f, 0.33f}, {0.42f, 0.43f, 0.45f}},
    }};
    return list;
}

inline const Palette& palette() { return palettes()[(size_t)std::clamp(state.background, 0, 3)]; }

inline std::filesystem::path file_path() { return ui_preferences::preference_path().parent_path() / "view_style.json"; }

inline void load() {
    try {
        std::ifstream input(file_path());
        if (!input) return;
        core::json data = core::json::parse(input);
        state.lighting = data.value("lighting", true);
        state.shadows = data.value("shadows", true);
        state.aa = std::clamp(data.value("aa", 0), 0, 8);
        state.background = std::clamp(data.value("background", 0), 0, 3);
        state.mode = render_style::mode_from(data.value("mode", std::string("classic")));
    } catch (...) {
    }
}

inline void save() {
    try {
        std::filesystem::create_directories(file_path().parent_path());
        std::ofstream output(file_path());
        core::json data = {{"lighting", state.lighting}, {"shadows", state.shadows}, {"aa", state.aa}, {"background", state.background},
                           {"mode", render_style::info(state.mode).id}};
        output << data.dump(2);
    } catch (...) {
    }
}

}  // namespace view_style
