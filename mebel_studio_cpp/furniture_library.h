// Parametric furniture library: closed production geometry and motion metadata.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace furniture_library {

using json = core::json;

// (category, name, w, h, d, kind, shelves, fronts) — wymiary w Pythonie są int.
struct Template {
    std::string category, name;
    long long w, h, d;
    std::string kind;
    long long shelves, fronts;
};

extern const std::vector<Template> TEMPLATES;

// {index: dict(...)} — opcje edytowalnych wzorców dopisanych na końcu listy
extern const std::map<long long, json> PRESET_OPTIONS;

// TEMPLATES[index] z semantyką list Pythona (indeksy ujemne, IndexError)
const Template& template_at(long long index);

json template_options(long long index);

// width/height/depth == None  ->  std::nullopt; group/material == None  ->  std::nullopt
std::pair<core::ProjectPtr, std::string> create_furniture(const core::Project& project, long long index, std::optional<double> width = std::nullopt,
                                                          std::optional<double> height = std::nullopt, std::optional<double> depth = std::nullopt,
                                                          double thickness = 18, core::Point3 origin = {0, 0, 0}, const std::optional<std::string>& group = std::nullopt,
                                                          const std::optional<std::string>& material = std::nullopt, const std::optional<std::string>& front_material = std::nullopt);

}  // namespace furniture_library
