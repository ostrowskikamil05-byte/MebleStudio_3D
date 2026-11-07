// Narzędzia deweloperskie: panel logów, dziennik operacji, pełny test z raportem, diagnostyka migracji.
// Są dostępne tylko w wersji deweloperskiej (CMake: -DMEBEL_DEV_TOOLS=ON, domyślnie włączone). Wersja dla użytkowników końcowych
// buduje się z -DMEBEL_DEV_TOOLS=OFF: wtedy te elementy znikają z menu i wstążki, a --full-test odmawia uruchomienia.
// Dodatkowo w wersji deweloperskiej można je wyłączyć bez przebudowy zmienną środowiskową MEBEL_DEV=0 (do sprawdzenia wyglądu dla użytkownika).
#pragma once
#include <cstdlib>
#include <string>

namespace dev_tools {

inline constexpr bool compiled =
#ifdef MEBEL_DEV_TOOLS
    true;
#else
    false;
#endif

inline bool enabled() {
    if (!compiled) return false;
    const char* env = std::getenv("MEBEL_DEV");
    return !(env != nullptr && std::string(env) == "0");
}

}  // namespace dev_tools
