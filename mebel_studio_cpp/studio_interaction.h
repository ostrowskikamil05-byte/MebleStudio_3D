// Camera gestures, whole-cabinet shortcuts and cursor-centred axis controls.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include "core.h"
#include "tk.h"

class App;

namespace studio_interaction {

using json = core::json;

// Identyfikator ściany pod kursorem (bufor identyfikatorów, natywne trafienie albo lista narysowanych ścian).
// Czy kliknięcie w widoku może się odbyć (panel nie ma niezatwierdzonej, niemożliwej do zastosowania zmiany). Gdy nie — wpis w dzienniku i komunikat.
bool allow_press(App* app);

std::optional<std::string> pick(App* app, int x, int y);

void select(App* app, const std::optional<std::string>& identity);

// Zmienia konfigurację kreatora zaznaczonego mebla (cfg.update(changes)).
void configure_change(App* app, const json& changes);

tk::Menu* context_menu(App* app, const tk::Event& event, bool show = true);

void set_mode(App* app, const std::string& mode, const std::optional<std::pair<double, double>>& origin = std::nullopt);

void paint_gizmo(App* app);

void install(App* app);

}  // namespace studio_interaction
