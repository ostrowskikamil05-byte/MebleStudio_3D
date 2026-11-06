// Persistent room/equipment panel; root mounts it beside the 3D inspector.
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include "core.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace room_scene_ui {

using json = core::json;

// Requires app.project, snapshot(), refresh(), draw(); selected_scene is an ID.
class RoomScenePanel : public ttk::Frame {
public:
    RoomScenePanel(tk::Widget* parent, App* app);

    bool _error(const std::string& message);
    bool _commit(core::ProjectPtr candidate, const std::optional<std::string>& selection = std::nullopt);
    void refresh();
    bool select_object(const std::string& object_id);
    void tree_select();
    bool apply_scene();
    // Zwraca identyfikator dodanego obiektu lub nullopt (błąd).
    std::optional<std::string> add_selected(const std::optional<std::string>& kind = std::nullopt);
    bool apply_selected();
    bool rotate_selected(double angle);
    bool delete_selected();
    bool apply_room();
    void fit_room();

    App* app;
    bool syncing = false;
    std::optional<std::string> selected;
    tk::StringVar note, room, brightness;
    tk::BooleanVar led, lights, walls, cutaway, ceiling;
    std::map<std::string, tk::StringVar> room_values;
    tk::StringVar catalogue;
    std::map<std::string, tk::StringVar> values;
    tk::BooleanVar enabled, light_enabled;
    tk::StringVar total;
    ttk::Combobox* roomcombo = nullptr;
    ttk::Button* apply_button = nullptr;
    tk::Canvas* canvas = nullptr;
    ttk::Combobox* cataloguecombo = nullptr;
    ttk::Treeview* tree = nullptr;
};

}  // namespace room_scene_ui
