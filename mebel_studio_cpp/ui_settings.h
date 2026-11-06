// Interface and viewport preferences in an ordinary detachable workspace tab.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core.h"
#include "form_dialog.h"
#include "tk.h"

class App;

namespace ui_settings {

// W Pythonie to formularz (app.dialog) z dopisanymi atrybutami interface_vars,
// interface_choices i apply_interface_preferences; tu osobna struktura nad formularzem.
struct SettingsDialog {
    form_dialog::Form* form = nullptr;
    std::vector<std::string> key_order;                     // kolejność słownika `vars`
    std::map<std::string, tk::StringVar> interface_vars;    // listy wyboru i pola liczbowe
    std::map<std::string, tk::BooleanVar> interface_flags;  // klucze show_*
    std::map<std::string, core::json> interface_choices;    // etykieta -> wartość (słownik uporządkowany)
    std::function<void()> apply_interface_preferences;

    void destroy() { if (form) form->destroy(); }
    bool winfo_exists() const { return form != nullptr && form->winfo_exists(); }
};

SettingsDialog* open_settings(App* app);

}  // namespace ui_settings
