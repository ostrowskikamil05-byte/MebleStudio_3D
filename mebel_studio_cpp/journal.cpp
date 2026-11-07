#include "journal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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

void draw_done(App* app, double milliseconds) {
    if (app == nullptr || app->log == nullptr) return;
    struct Window { int count = 0; double total = 0, longest = 0; std::chrono::steady_clock::time_point started{}; };
    static Window window;
    auto now = std::chrono::steady_clock::now();
    if (window.count == 0) window.started = now;
    ++window.count;
    window.total += milliseconds;
    window.longest = std::max(window.longest, milliseconds);
    if (milliseconds > 150) event(app, "render", std::format("wolne rysowanie {:.0f} ms (silnik {}, jakość {})", milliseconds, app->canvas ? app->canvas->_active_renderer : std::string("?"), app->viewport_quality));
    if (now - window.started < std::chrono::seconds(1)) return;
    try {
        event(app, "render", std::format("{} odrysowań w {:.1f} s · średnio {:.1f} ms, najdłużej {:.0f} ms · silnik {} (ustawiony {}) · jakość {} · części {} · kamera yaw {:.0f} pitch {:.0f} · tryb {}{}{}",
                                         window.count, std::chrono::duration<double>(now - window.started).count(), window.total / window.count, window.longest,
                                         app->canvas ? app->canvas->_active_renderer : std::string("?"), app->viewport_engine, app->viewport_quality, app->project ? app->project->parts.size() : 0,
                                         app->camera.yaw, app->camera.pitch, app->edit_mode.get(), app->xray.get() ? " · X-ray" : "", app->explode.get() ? " · rozstrzelony" : ""));
    } catch (...) {
    }
    window = Window{};
}

void file_event(App* app, const std::string& action, const std::string& path, double milliseconds, const std::string& result) {
    std::string size;
    try {
        std::error_code ec;
        if (!path.empty() && std::filesystem::is_regular_file(path, ec)) size = std::format(" · {} B", std::filesystem::file_size(path, ec));
    } catch (...) {
    }
    event(app, "plik", std::format("{}: {}{} · {:.0f} ms · {}", action, path.empty() ? std::string("(bez ścieżki)") : path, size, milliseconds, result));
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
                // oś „rozdzielająca" to ta o największym odstępie; w pozostałych meble muszą się pokrywać (inaczej nie sąsiadują)
                int axis = 0;
                for (int k = 1; k < 3; ++k) if (gap[k] > gap[axis]) axis = k;
                bool neighbours = true;
                for (int other = 0; other < 3; ++other) if (other != axis && gap[other] > 0.5) neighbours = false;
                if (!neighbours || gap[axis] > 250) continue;
                event(app, "stan", std::format("  sąsiedzi {} ↔ {}: {} {:.1f} mm (oś {})", names[i], names[j], gap[axis] > 0.5 ? "odstęp" : (gap[axis] < -0.5 ? "nakładają się o" : "styk"),
                                               std::abs(gap[axis]), "XYZ"[axis]));
            }
        }
    } catch (const std::exception& error) {
        event(app, "stan", std::string("nie udało się zrobić zrzutu: ") + error.what());
    }
}

}  // namespace journal
