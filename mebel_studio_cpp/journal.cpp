#include "journal.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "app.h"
#include "furniture_layout.h"

namespace journal {

void event(App* app, const std::string& category, const std::string& message) {
    if (app == nullptr || app->log == nullptr) return;
    try {
        app->log->info("[operacja:" + category + "] " + message);
    } catch (...) {
    }
}

namespace {

std::string front_text(const core::Project& project, const std::string& group, const furniture_layout::Box& box) {
    double cx = (box.first[0] + box.second[0]) / 2, cy = (box.first[1] + box.second[1]) / 2;
    auto name = [](double dx, double dy) -> std::string {
        if (std::hypot(dx, dy) < 1) return "?";
        if (std::abs(dx) > std::abs(dy)) return dx > 0 ? "+x" : "-x";
        return dy > 0 ? "+y" : "-y";
    };
    for (const auto& p : project.parts) {
        if (p.group != group || p.role != "back") continue;
        auto w = core::world(p, p.u / 2, p.v / 2, p.t / 2);
        return name(cx - w[0], cy - w[1]);
    }
    for (const auto& p : project.parts) {
        if (p.group != group || (p.role != "door" && p.role != "drawer")) continue;
        auto w = core::world(p, p.u / 2, p.v / 2, p.t / 2);
        return name(w[0] - cx, w[1] - cy);
    }
    return "?";
}

}  // namespace

void dump_groups(App* app, const core::Project& project, const std::string& label) {
    if (app == nullptr || app->log == nullptr) return;
    try {
        auto names = furniture_layout::groups(project);
        std::vector<furniture_layout::Box> boxes;
        event(app, "stan", std::format("{} — {} mebli", label, names.size()));
        for (const auto& name : names) {
            auto box = furniture_layout::group_bounds(project, name);
            boxes.push_back(box);
            json layout = furniture_layout::group_layout(project, name);
            double yaw = layout.contains("yaw") && layout.at("yaw").is_number() ? layout.at("yaw").get<double>() : 0;
            event(app, "stan", std::format("  {} · X {:.0f}…{:.0f} · Y {:.0f}…{:.0f} · Z {:.0f}…{:.0f} · obrót {:g}° · front {}", name, box.first[0], box.second[0], box.first[1], box.second[1],
                                           box.first[2], box.second[2], yaw, front_text(project, name, box)));
        }
        for (size_t i = 0; i < names.size(); ++i) {
            for (size_t j = i + 1; j < names.size(); ++j) {
                double gap[3];
                for (int axis = 0; axis < 3; ++axis)
                    gap[axis] = std::max(boxes[i].first[axis] - boxes[j].second[axis], boxes[j].first[axis] - boxes[i].second[axis]);
                // odstęp liczymy tylko tam, gdzie mebel naprawdę sąsiaduje: w pozostałych osiach muszą się pokrywać
                for (int axis = 0; axis < 3; ++axis) {
                    bool others_overlap = true;
                    for (int other = 0; other < 3; ++other) if (other != axis && gap[other] > 0.5) others_overlap = false;
                    if (!others_overlap || gap[axis] > 250) continue;
                    event(app, "stan", std::format("  sąsiedzi {} ↔ {}: {} {:.1f} mm (oś {})", names[i], names[j], gap[axis] > 0.5 ? "odstęp" : (gap[axis] < -0.5 ? "nakładają się o" : "styk"),
                                                   std::abs(gap[axis]), "XYZ"[axis]));
                    break;
                }
            }
        }
    } catch (const std::exception& error) {
        event(app, "stan", std::string("nie udało się zrobić zrzutu: ") + error.what());
    }
}

}  // namespace journal
