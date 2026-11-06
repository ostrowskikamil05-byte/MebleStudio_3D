// Motyw wizualny "Forma": jasne, ciepłe tło, zielony akcent, płaskie kontrolki.
// Zmienia tylko wygląd (kolory, odstępy, czcionki) — nie dotyka funkcji programu.
#pragma once
#include "tk.h"

namespace forma_theme {

// Paleta (do użycia także w widgetach tk, które nie podlegają stylom ttk).
inline constexpr const char* BG = "#f6f3ee";        // tło okna
inline constexpr const char* PANEL = "#fbfaf7";     // panele boczne
inline constexpr const char* CARD = "#ffffff";      // pola, karty
inline constexpr const char* BORDER = "#e2ded5";    // ramki i separatory
inline constexpr const char* TEXT = "#1f2a1f";      // tekst podstawowy
inline constexpr const char* MUTED = "#6b7266";     // tekst drugorzędny
inline constexpr const char* ACCENT = "#4e6b3f";    // zielony akcent
inline constexpr const char* ACCENT_HOVER = "#5b7b4a";
inline constexpr const char* ACCENT_SOFT = "#e3ebd7";  // zaznaczenie / aktywny element
inline constexpr const char* HOVER = "#efece5";
inline constexpr const char* WARM = "#e8872b";      // wymiary / wyróżnienia

void apply(tk::Widget* root);

}  // namespace forma_theme
