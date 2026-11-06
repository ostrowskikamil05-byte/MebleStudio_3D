// Explicit, editable construction recipes. No machine or vendor certification.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "core.h"

namespace assembly {

using json = core::json;

// dict etykieta -> funkcja (kolejność jak w Pythonie)
extern const std::vector<std::pair<std::string, std::string>> ROLES;
extern const json DEFAULTS;
extern const std::vector<std::pair<std::string, std::string>> PARAMETERS;

// Wynik frame(): w Pythonie dict z obiektami Part. Części są tu wskazane
// identyfikatorem, bo wektor części projektu może się realokować.
struct Frame {
    core::Point3 lower{};
    core::Point3 upper{};
    std::optional<std::string> left;   // id boku lewego albo None
    std::optional<std::string> right;  // id boku prawego albo None
    double inner_left = 0;
    double inner_right = 0;
    double inner_bottom = 0;
    double inner_top = 0;
    std::vector<std::string> parts;    // id części konstrukcji odniesienia
    // Tylko corner_furniture.corner_front_frame:
    std::optional<std::string> coordinate_system;
    std::optional<std::array<core::Point3, 3>> basis;
    std::optional<core::Point3> origin;
};

std::string inferred_role(const core::Part& part);

std::pair<core::Point3, core::Point3> bounds(const core::Part& part);

Frame frame(const core::Project& project, const std::string& group, const std::optional<std::string>& exclude = std::nullopt, const json& bay = nullptr);

Frame frame_for_part(const core::Project& project, const core::Part& part);

json recipe(const json& values = nullptr);

std::pair<double, double> suggested_size(const core::Project& project, const std::string& group, const std::string& role, double thickness,
                                         const json& config = nullptr, const std::optional<std::string>& exclude = std::nullopt);

void clear_generated(core::Project& project, const std::string& owner_id);

void delete_with_connections(core::Project& project, const std::string& part_id);

void drill(core::Part& part, const core::Part& owner, double u, double v, double diameter, double depth, const std::string& face, const std::string& purpose);

void accessory(core::Project& project, const core::Part& owner, const std::string& name, const json& qty, const json& price,
               const std::vector<const core::Part*>& anchors, const json& positions);

bool rectangular(const core::Part& part);

std::pair<core::Part*, core::Part*> require_sides(core::Project& project, const Frame& f);

void set_placement(core::Part& q, const Frame& f, const json& c);

void check_overlap(const core::Project& project, const core::Part& q);

void hinges(core::Project& project, core::Part& q, const Frame& f, const json& c);

void shelf_supports(core::Project& project, core::Part& q, const Frame& f, const json& c);

void drawer_box(core::Project& project, core::Part& q, const Frame& f, json& c);

// Return an atomic candidate project; validation failure never mutates input.
// Drugi element wskazuje element q wewnątrz zwróconego projektu.
std::pair<core::ProjectPtr, core::Part*> attach(const core::Project& project, const core::Part& part, const std::string& group, const std::string& role, const json& config = nullptr);

core::ProjectPtr rebuild_dependents(const core::Project& project, const std::string& changed_id);

}  // namespace assembly
