// Szkielet okna w stylu "Forma": pasek górny z menu Projekt / Edycja / Widok.
#pragma once

class App;

namespace forma_shell {

// Wywoływane po zainstalowaniu wszystkich modułów: składa menu z istniejących poleceń,
// chowa systemowy pasek menu i utrzymuje nazwę projektu w pasku górnym.
void finish(App* app);

}  // namespace forma_shell
