// Atomic cabinet configurator with editable bays and provenance.
//
// UI API: default_config(index,project=None), normalize_config(config,project),
// read_config(project,group), infer_config(project,group),
// configuration_status(project,group), configure(project,config,group=None,origin=None).
// Height includes purchased legs and an optional worktop. Width/depth describe
// the carcass; front/back thickness and worktop overhang extend its envelope.
#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include "core.h"

namespace furniture_configurator {

using json = core::json;

extern const std::set<std::string> SUPPORTED;

// project == None  ->  nullptr
json default_config(const json& template_index = 0, const core::Project* project = nullptr);
json default_config(const json& template_index, const core::Project& project);

json normalize_config(const json& config, const core::Project* project = nullptr);
json normalize_config(const json& config, const core::Project& project);

// sha256 podpisu geometrii części (hexdigest jako tekst JSON)
json _geometry(const core::Part& part);

// Kopia konfiguracji kreatora albo null (None), gdy mebel nie pochodzi z kreatora.
json read_config(const core::Project& project, const std::string& group);

json configuration_status(const core::Project& project, const std::string& group);

void _remap(core::Project& project, const std::map<std::string, std::string>& mapping);

// Return a complete candidate and group. Input and Undo remain untouched.
// group == None  ->  std::nullopt; origin == None  ->  null
std::pair<core::ProjectPtr, std::string> configure(const core::Project& project, const json& config, const std::optional<std::string>& group = std::nullopt, const json& origin = nullptr);

// Recognise an unchanged library cabinet for an explicitly requested conversion.
json infer_config(const core::Project& project, const std::string& group);

}  // namespace furniture_configurator
