// Small persistent tray; root places it beside the actual 3D canvas.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "core.h"
#include "drag_hardware.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace hardware_placement_ui {

using json = core::json;

class HardwarePlacementPanel : public ttk::Frame, public drag_hardware::HardwarePanel {
public:
    App* app;
    // self.note dziedziczone z drag_hardware::HardwarePanel
    tk::StringVar face, width, axis, length, margin, price, price_unit;
    tk::BooleanVar groove;
    tk::StringVar count, profile, hinge;
    drag_hardware::HardwareDragController* controller = nullptr;
    ttk::Frame* details = nullptr;
    bool _details_visible = false;
    ttk::Button* details_button = nullptr;
    std::vector<std::pair<std::string, ttk::Label*>> tiles;  // dict: rodzaj -> kafelek
    ttk::Label* note_label = nullptr;

    HardwarePlacementPanel(tk::Widget* parent, App* app);
    void toggle_details();
    json options_for(const std::string& kind);
    std::string begin(const std::string& kind, tk::Event* event = nullptr);
    void refresh();
};

}  // namespace hardware_placement_ui
