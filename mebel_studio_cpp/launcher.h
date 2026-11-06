// Frozen desktop entry point and offline diagnostic mode.
//
// main() jest zdefiniowane tutaj tylko przy MEBEL_LAUNCHER (cel "MebelStudio3D");
// bez tej flagi punkt wejścia pochodzi z app.cpp (app.py: __main__).
//
// Moduły jeszcze nieprzetłumaczone, z których korzysta self_test (założone
// interfejsy): runners_catalog, runner_installation, opening_options (install
// z argumentami nazwanymi jako JSON), shaped_furniture (TEMPLATES,
// create_shaped_furniture, apply_shape), project_print (zwraca JSON),
// ui_preferences, sketch, quick_studio_ui (focus_section, corner_dialog),
// room_objects (set_scene), room_architecture (world_faces), workspace_worktops
// (install_shared_worktop, read_shared_worktop), viewport_render (clear_caches),
// viewport_scene, responsive_ui (UiLayout, interface_settings) i visual_catalogue.
#pragma once
#include <string>

namespace launcher {

// Ustawiane przez natywnego hosta (sys._mebel_native_host): błąd startu jest wtedy przekazywany dalej.
inline bool native_host = false;

// Zapisuje raport JSON pod `path`; zwraca 0 przy powodzeniu, 1 przy błędzie.
int self_test(const std::string& path);

}  // namespace launcher
