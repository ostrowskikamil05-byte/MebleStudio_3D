// Atomic drag placement and pure, inexpensive fitting previews.
//
// Supported sourced recipes retain their existing limitations. A runner planning
// profile does not invent manufacturer clip drilling or notches. LED groove
// machining is available only on the CAD engine's broad +T/-T surfaces.
#pragma once
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core.h"
#include "tk.h"

class App;

namespace drag_hardware {

using json = core::json;

extern const std::vector<std::pair<std::string, std::string>> KINDS;

// Stare części potomne według nazwy (kolejność jak w dict Pythona)
using NamedParts = std::vector<std::pair<std::string, core::Part>>;

core::Part& _part(core::Project& project, const json& target);
const core::Part& _part(const core::Project& project, const json& target);

// Keep managed generated children stable after a legitimate runner rebuild.
void _creator_changed(core::Project& project, const core::Part& owner, const NamedParts& old_children = {});

// Zwraca ten sam wskaźnik, gdy projekt zmieniono w miejscu (jak `is` w Pythonie).
core::ProjectPtr _hinge_install(core::ProjectPtr project, const json& target, const json& hit, const json& options, const json& ident);
core::ProjectPtr _runner_install(core::ProjectPtr project, const json& target, const json& hit, const json& options, const json& ident);

json _light_values(const core::Project& project, const std::string& kind, const json& target, const json& hit, const json& options);
core::ProjectPtr _light_install(const core::Project& project, const std::string& kind, const json& target, const json& hit, const json& options);

// Return one validated candidate; failure leaves the source entirely intact.
core::ProjectPtr attach(const core::Project& project, const std::string& kind, const json& target_id = nullptr, const json& local_hit = nullptr,
                        const json& options = nullptr, const json& attachment_id = nullptr);

// Pure ghost proposal. An invalid target is a red proposal, not an exception.
json preview_attachment(const core::Project& project, const std::string& kind, const json& target_id = nullptr, const json& local_hit = nullptr, const json& options = nullptr);

// Refresh a caller-owned candidate after assembly/configurator rebuilding.
core::Project& refresh_manual_attachments(core::Project& project, const std::optional<std::set<std::string>>& changed_ids = std::nullopt);

// Panel z polem `note` (w Pythonie dowolny obiekt z atrybutem note)
struct HardwarePanel {
    virtual ~HardwarePanel() = default;
    tk::StringVar note;
};

// Physical tray drag and click-tool/click-target share one atomic engine.
class HardwareDragController {
public:
    App* app; HardwarePanel* panel; std::optional<std::string> kind; json options = json::object(); json proposal = nullptr;
    std::optional<std::pair<double, double>> pointer; std::optional<std::pair<double, double>> start; bool dragging = false;
    int project_generation = 0;

    HardwareDragController(App* app, HardwarePanel* panel = nullptr);

    bool active() const { return kind.has_value(); }
    const std::optional<std::string>& active_tool() const { return kind; }

    void _note(const std::string& text);
    std::string begin(const std::string& kind, const json& options = nullptr, tk::Event* event = nullptr);
    std::pair<double, double> _position(const tk::Event& event);
    std::optional<json> _target(double x, double y);
    std::optional<std::string> move(const tk::Event& event, bool force = false);
    void draw_ghost();
    std::optional<std::string> target_click(const tk::Event& event);
    std::optional<std::string> end(const tk::Event& event);
    std::string cancel(tk::Event* event = nullptr);

private:
    std::list<std::pair<std::string, json>> _cache;  // OrderedDict
    double _last_move = 0;
    const core::Project* _cache_project = nullptr;
};

}  // namespace drag_hardware
