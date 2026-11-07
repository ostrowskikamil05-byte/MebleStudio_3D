#include "journal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <set>

#include "app.h"
#include "furniture_layout.h"
#include "stretch_choice.h"

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


namespace {

struct PartSig {
    std::string group, name, role, material;
    double lx = 0, ly = 0, lz = 0, lyaw = 0;  // układ mebla (brak pola = 0)
    double u = 0, v = 0, t = 0, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
    size_t operations = 0, machining = 0, content_hash = 0;  // content_hash: treść wierceń i obróbek (przesunięty otwór = zmiana)
    int edges = 0;
    bool visible = true;
};

struct Snap {
    std::map<std::string, PartSig> parts;
    std::map<std::string, std::map<std::string, double>> hardware;  // grupa → etykieta okucia → ilość
    std::map<std::string, std::string> hardware_positions;           // grupa|etykieta|właściciel → rozmieszczenie (zmiana = okucie przesunięte)
    std::map<std::string, std::pair<std::string, std::string>> scene_objects;  // id → (typ/nazwa, pełna treść) — AGD i wyposażenie pokoju
    size_t drills = 0;
    size_t materials = 0;
    std::string name;
    bool valid = false;
};

std::string kind_label(const std::string& kind) {
    static const std::map<std::string, std::string> names = {
        {"hinge", "zawias"}, {"leg", "nóżka"}, {"handle", "uchwyt"}, {"runner", "prowadnica"}, {"led_strip", "taśma LED"}, {"led_profile", "profil LED"},
        {"led_diffuser", "dyfuzor LED"}, {"led_power", "zasilacz LED"}, {"opening", "otwieranie"}, {"connector", "łącznik"}, {"shelf_pin", "kołek półki"}};
    auto it = names.find(kind);
    return it == names.end() ? kind : it->second;
}

Snap make_snap(const core::Project& project) {
    Snap snap;
    snap.valid = true;
    snap.name = project.name;
    snap.materials = project.materials.size();
    std::map<std::string, int> seen;
    for (const auto& p : project.parts) {
        std::string base = p.group + "|" + p.name;
        int n = seen[base]++;
        PartSig sig{p.group, p.name, p.role, p.material, 0, 0, 0, 0, p.u, p.v, p.t, p.x, p.y, p.z, p.rx, p.ry, p.rz, p.operations.is_array() ? p.operations.size() : 0,
                    p.machining.is_array() ? p.machining.size() : 0, 0, 0, p.visible};
        if (p.layout.is_object()) {
            auto number = [&](const char* key) { return p.layout.contains(key) && p.layout.at(key).is_number() ? p.layout.at(key).get<double>() : 0.0; };
            sig.lx = number("x"); sig.ly = number("y"); sig.lz = number("z"); sig.lyaw = number("yaw");
        }
        sig.content_hash = std::hash<std::string>{}(p.operations.dump()) ^ (std::hash<std::string>{}(p.machining.dump()) << 1);
        for (size_t i = 0; i < p.edges.size(); ++i) if (p.edges[i]) sig.edges |= 1 << i;
        snap.drills += sig.operations;
        snap.parts[base + "#" + std::to_string(n)] = sig;
    }
    if (project.scene.is_object() && project.scene.contains("objects") && project.scene["objects"].is_array()) {
        for (const auto& object : project.scene["objects"]) {
            if (!object.is_object() || !object.contains("id") || !object["id"].is_string()) continue;
            std::string label = object.contains("name") && object["name"].is_string() && !object["name"].get<std::string>().empty() ? object["name"].get<std::string>()
                                : object.contains("type") && object["type"].is_string() ? object["type"].get<std::string>() : std::string("obiekt");
            snap.scene_objects[object["id"].get<std::string>()] = {label, object.dump()};
        }
    }
    std::map<std::string, std::string> owner_group;
    for (const auto& p : project.parts) owner_group[p.id] = p.group;
    for (const auto& h : project.hardware) {
        if (!h.is_object()) continue;
        std::string group;
        if (h.contains("group") && h["group"].is_string()) group = h["group"].get<std::string>();
        else if (h.contains("owner_id") && h["owner_id"].is_string() && owner_group.contains(h["owner_id"].get<std::string>())) group = owner_group[h["owner_id"].get<std::string>()];
        else group = "(bez mebla)";
        std::string label;
        if (h.contains("kind") && h["kind"].is_string()) label = kind_label(h["kind"].get<std::string>());
        else if (h.contains("name") && h["name"].is_string()) {
            label = h["name"].get<std::string>();
            auto cut = label.find(" — ");
            if (cut != std::string::npos) label = label.substr(0, cut);
            if (label.size() > 40) label = label.substr(0, 40);
        } else label = "okucie";
        double qty = h.contains("qty") && h["qty"].is_number() ? h["qty"].get<double>() : 1;
        snap.hardware[group][label] += qty;
        std::string owner = h.contains("owner_id") && h["owner_id"].is_string() ? h["owner_id"].get<std::string>() : std::string();
        std::string pos = h.contains("positions") ? h["positions"].dump() : std::string();
        snap.hardware_positions[group + "|" + label + "|" + owner] += pos;
    }
    return snap;
}

bool near(double a, double b) { return std::abs(a - b) < 0.05; }

std::string diff_snaps(const Snap& a, const Snap& b) {
    if (!a.valid || !b.valid) return "";
    std::set<std::string> groups_a, groups_b;
    for (auto& [k, v] : a.parts) groups_a.insert(v.group);
    for (auto& [k, v] : b.parts) groups_b.insert(v.group);
    size_t added = 0, removed = 0, changed = 0;
    struct GroupChange { bool dims = false, moved = false, material = false, edges = false, drills = false, parts = false, visible = false; int added = 0, removed = 0; };
    std::map<std::string, GroupChange> per_group;
    for (auto& [key, now] : b.parts) {
        auto it = a.parts.find(key);
        if (it == a.parts.end()) { ++added; per_group[now.group].parts = true; ++per_group[now.group].added; continue; }
        const PartSig& was = it->second;
        GroupChange& gc = per_group[now.group];
        bool any = false;
        if (!near(was.u, now.u) || !near(was.v, now.v) || !near(was.t, now.t)) { gc.dims = true; any = true; }
        if (!near(was.x, now.x) || !near(was.y, now.y) || !near(was.z, now.z) || !near(was.rx, now.rx) || !near(was.ry, now.ry) || !near(was.rz, now.rz) || !near(was.lx, now.lx) || !near(was.ly, now.ly) || !near(was.lz, now.lz) || !near(was.lyaw, now.lyaw)) { gc.moved = true; any = true; }
        if (was.material != now.material) { gc.material = true; any = true; }
        if (was.edges != now.edges) { gc.edges = true; any = true; }
        if (was.operations != now.operations || was.machining != now.machining || was.content_hash != now.content_hash) { gc.drills = true; any = true; }
        if (was.visible != now.visible) { gc.visible = true; any = true; }
        if (any) ++changed;
    }
    for (auto& [key, was] : a.parts) if (!b.parts.contains(key)) { ++removed; per_group[was.group].parts = true; ++per_group[was.group].removed; }
    std::string out;
    if (a.name != b.name) out += std::format("nazwa „{}” → „{}” · ", a.name, b.name);
    out += std::format("części {}→{} (+{} −{} ~{})", a.parts.size(), b.parts.size(), added, removed, changed);
    if (a.drills != b.drills) out += std::format(" · wiercenia {}→{}", a.drills, b.drills);
    if (a.materials != b.materials) out += std::format(" · materiały {}→{}", a.materials, b.materials);
    std::string new_groups, gone_groups;
    for (auto& g : groups_b) if (!groups_a.contains(g)) new_groups += (new_groups.empty() ? "" : ", ") + g;
    for (auto& g : groups_a) if (!groups_b.contains(g)) gone_groups += (gone_groups.empty() ? "" : ", ") + g;
    if (!new_groups.empty()) out += " · nowe meble: " + new_groups;
    if (!gone_groups.empty()) out += " · usunięte meble: " + gone_groups;
    // okucia: różnice ilości w grupach
    std::set<std::string> hw_groups;
    for (auto& [g, _] : a.hardware) hw_groups.insert(g);
    for (auto& [g, _] : b.hardware) hw_groups.insert(g);
    std::string hw;
    std::map<std::string, double> total_delta;
    for (auto& g : hw_groups) {
        std::map<std::string, double> before = a.hardware.contains(g) ? a.hardware.at(g) : std::map<std::string, double>{};
        std::map<std::string, double> after = b.hardware.contains(g) ? b.hardware.at(g) : std::map<std::string, double>{};
        std::set<std::string> kinds;
        for (auto& [k, _] : before) kinds.insert(k);
        for (auto& [k, _] : after) kinds.insert(k);
        for (auto& k : kinds) {
            double delta = (after.contains(k) ? after[k] : 0) - (before.contains(k) ? before[k] : 0);
            if (std::abs(delta) > 1e-9) total_delta[k] += delta;
        }
    }
    for (auto& [k, d] : total_delta) hw += std::format("{}{} {:+g}", hw.empty() ? "" : ", ", k, d);
    std::string moved_hw;
    for (auto& [key, pos] : b.hardware_positions) {
        auto it = a.hardware_positions.find(key);
        if (it != a.hardware_positions.end() && it->second != pos) {
            auto first = key.find('|'), second = key.find('|', first + 1);
            moved_hw += (moved_hw.empty() ? "" : ", ") + key.substr(first + 1, second - first - 1);
        }
    }
    if (!moved_hw.empty()) hw += std::string(hw.empty() ? "" : ", ") + "przestawione: " + moved_hw;
    if (!hw.empty()) out += " · okucia: " + hw;
    std::string scene_text;
    for (auto& [id, now] : b.scene_objects) {
        auto it = a.scene_objects.find(id);
        if (it == a.scene_objects.end()) scene_text += (scene_text.empty() ? "" : ", ") + std::string("+") + now.first;
        else if (it->second.second != now.second) scene_text += (scene_text.empty() ? "" : ", ") + std::string("zmieniono ") + now.first;
    }
    for (auto& [id, was] : a.scene_objects) if (!b.scene_objects.contains(id)) scene_text += (scene_text.empty() ? "" : ", ") + std::string("−") + was.first;
    if (!scene_text.empty()) out += " · obiekty sceny: " + scene_text;
    int shown = 0;
    std::string details;
    for (auto& [g, gc] : per_group) {
        std::string what;
        auto add = [&](bool on, const char* text) { if (on) what += (what.empty() ? "" : ", ") + std::string(text); };
        add(gc.dims, "wymiary"); add(gc.moved, "położenie/obrót"); add(gc.material, "materiał"); add(gc.edges, "obrzeża"); add(gc.drills, "wiercenia/obróbki"); add(gc.visible, "widoczność");
        if (gc.added) what += (what.empty() ? "" : ", ") + std::format("+{} części", gc.added);
        if (gc.removed) what += (what.empty() ? "" : ", ") + std::format("−{} części", gc.removed);
        if (what.empty()) continue;
        if (shown++ >= 8) continue;
        details += (details.empty() ? "" : " | ") + g + ": " + what;
    }
    if (shown > 8) details += std::format(" | (+{} dalszych mebli)", shown - 8);
    if (!details.empty()) out += " · " + details;
    bool nothing = added == 0 && removed == 0 && changed == 0 && a.drills == b.drills && a.materials == b.materials && hw.empty() && scene_text.empty() && a.name == b.name;
    return nothing ? std::string() : out;
}

std::map<App*, Snap>& snapshots() {
    static std::map<App*, Snap> items;
    return items;
}

}  // namespace

std::string describe_difference(const core::Project& before, const core::Project& after) { return diff_snaps(make_snap(before), make_snap(after)); }

void forget_project_state(App* app) { snapshots().erase(app); }

std::string project_refreshed(App* app) {
    if (app == nullptr || app->log == nullptr || !app->project) return "";
    try {
        Snap now = make_snap(*app->project);
        Snap& last = snapshots()[app];
        std::string text = diff_snaps(last, now);
        last = std::move(now);
        if (!text.empty()) {
            event(app, "zmiana", text + (app->last_snapshot_site.empty() ? std::string() : " · źródło " + app->last_snapshot_site) + " · tryb " + app->edit_mode.get());
            app->last_snapshot_site.clear();
        }
        return text;
    } catch (const std::exception& error) {
        event(app, "zmiana", std::string("nie udało się opisać zmiany: ") + error.what());
    }
    return "";
}

std::string interaction_state(App* app) {
    std::string out = "tryb " + app->edit_mode.get();
    try {
        out += std::format(" · zaznaczenie {}", app->selected ? *app->selected : std::string("brak"));
        if (core::Part* p = app->selected_part()) out += std::format(" ({} / {})", p->name, p->group);
        out += std::format(" · uchwytów {} · przeciąganie {}{}{}{}", app->handles.size(), app->drag_edit ? "rozciągania" : "brak", app->layout_drag ? " + przesuwania" : "",
                           app->scene_drag ? " + sceny" : "", app->hardware_drag && app->hardware_drag->active() ? " + okucia" : "");
        out += std::format(" · otwarte animacje {}", app->open_amounts.size());
        out += std::format(" · szkice/dialogi: konfiguracja {}", stretch_choice::configuration_dialog() ? "otwarta" : "zamknięta");
    } catch (const std::exception& error) {
        out += std::string(" · błąd zrzutu: ") + error.what();
    }
    return out;
}

}  // namespace journal
