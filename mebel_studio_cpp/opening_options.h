// Handle alternatives; sourced ejector bores and editable finger-pull machining.
#pragma once
#include <string>
#include <vector>
#include "core.h"
#include "fitting_visuals.h"

namespace opening_options {

using json = core::json;

extern const std::string BLUM;
extern const std::string SALICE;
extern const json OPTIONS;  // dict klucz -> {label,[diameter,depth,gap,hinge_spring],source,note}

void clear(core::Project& project, const std::string& part_id);

// Argumenty install(...) po `option`; None -> nullptr.
// Python: length=120, width=24, depth=8, level=None, color='#242628', *, hinge_spring=None, center_u=None, angle=0, profile_edge='top'
struct InstallOptions {
    json length = 120;
    json width = 24;
    json depth = 8;
    json level = nullptr;
    json color = "#242628";
    json hinge_spring = nullptr;
    json center_u = nullptr;
    json angle = 0;
    std::string profile_edge = "top";
};

core::ProjectPtr install(const core::Project& project, const std::string& part_id, const std::string& option = "tip_bumper",
                         const InstallOptions& options = {});
// install(project, id, option, length, width, depth, hinge_spring=...)
core::ProjectPtr install(const core::Project& project, const std::string& part_id, const std::string& option, const json& length, const json& width,
                         const json& depth, const json& hinge_spring);
// install(project, id, option, length, width, depth, level, hinge_spring=..., center_u=..., angle=..., profile_edge=...)
core::ProjectPtr install(const core::Project& project, const std::string& part_id, const std::string& option, const json& length, const json& width,
                         const json& depth, const json& level, const json& center_u, const json& angle, const json& hinge_spring,
                         const std::string& profile_edge);
// install(project, id, option, **kwargs); nieznany klucz -> TypeError
core::ProjectPtr install(const core::Project& project, const std::string& part_id, const std::string& option, const json& kwargs);

void refresh_openings(core::Project& project, const std::string& part_id);

fitting_visuals::Faces opening_faces(const core::Project& project, const core::Part& part, const json& hardware);

}  // namespace opening_options
