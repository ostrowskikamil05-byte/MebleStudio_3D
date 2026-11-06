// Independent part transforms, arrays and reusable furniture project templates.
#include "productivity.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "assembly.h"
#include "cad_geometry.h"
#include "furniture_configurator.h"
#include "workspace_worktops.h"

namespace productivity {

using core::Material;
using core::Part;
using core::Point3;
using core::Project;
using core::ProjectPtr;
using core::finite;
using core::uid;
using py::ValueError;

namespace {

const json* find_key(const json& obj, const std::string& key) { return obj.is_object() && obj.contains(key) ? &obj.at(key) : nullptr; }

// {k: v for k, v in item.items() if k not in excluded}
json without(const json& item, std::initializer_list<const char*> excluded) {
    json out = json::object();
    for (auto& [key, value] : item.items()) {
        bool skip = false;
        for (const char* e : excluded) if (key == e) skip = true;
        if (!skip) out[key] = value;
    }
    return out;
}

// int(count) != count or not low <= count <= high
double whole(const json& count, double low, double high, const char* message) {
    double c = py::to_float(count);
    if (!std::isfinite(c) || std::trunc(c) != c || !(low <= c && c <= high)) throw ValueError(message);
    return c;
}

double reflect_value(double length, double v) { return length - v; }

}  // namespace

Part independent_copy(const Part& part) {
    Part q = part;
    q.id = uid(); q.assembly = json::object(); q.role = "other"; q.style_source = json::object(); q.configurator_source = json::object();
    json ops = json::array();
    for (const auto& op : q.operations) ops.push_back(without(op, {"owner_id", "opening_owner", "corner_hinge_owner", "attachment_id"}));
    q.operations = ops;
    json cuts = json::array();
    for (const auto& cut : q.machining) cuts.push_back(without(cut, {"opening_owner", "attachment_id"}));
    q.machining = cuts;
    return q;
}

ProjectPtr part_array(const Project& project, const std::string& part_id, const json& count_, const json& dx_, const json& dy_, const json& dz_) {
    double count = whole(count_, 1, 100, "Liczba kopii: od 1 do 100.");
    double dx = finite(dx_), dy = finite(dy_), dz = finite(dz_);
    if (dx == 0 && dy == 0 && dz == 0) throw ValueError("Podaj niezerowy krok szyku.");
    auto result = std::make_shared<Project>(project);
    const Part* found = nullptr;
    for (const auto& p : result->parts) if (p.id == part_id) { found = &p; break; }
    if (found == nullptr) throw py::StopIteration();
    Part part = *found;  // result.parts będzie rosnąć
    Point3 d = core::rotate({dx, dy, dz}, 0, 0, -py::get(part.layout, "yaw", 0).get<double>());
    for (int i = 1; i <= (int)count; ++i) {
        Part q = independent_copy(part);
        q.name += std::format(" — kopia {}", i);
        q.x += i * d[0]; q.y += i * d[1]; q.z += i * d[2];
        result->parts.push_back(q);
    }
    result->validate();
    return result;
}

Part mirror_part(const Part& part, const std::string& axis) {
    if (axis != "u" && axis != "v") throw ValueError("Odbicie w osi lokalnej U lub V.");
    if (py::truthy(part.profile) && (py::truthy(part.fillets) || py::truthy(part.chamfers)))
        throw ValueError("Przed odbiciem obłego obrysu usuń jego fazy i zaokrąglenia kantów w edytorze CAD; odbicie zmienia numerację krawędzi.");
    Part q = independent_copy(part);
    q.name += " — odbicie"; q.profile = json::object(); q.decor = json::object(); q.style_source = json::object();
    const bool along_u = axis == "u";
    const double length = along_u ? part.u : part.v;
    auto reflect3 = [&](const Point3& p) { return along_u ? Point3{part.u - p[0], p[1], p[2]} : Point3{p[0], part.v - p[1], p[2]}; };
    if (!part.contour.empty()) {
        q.contour.clear();
        for (const auto& p : part.points()) q.contour.push_back(along_u ? std::vector<double>{part.u - p[0], p[1]} : std::vector<double>{p[0], part.v - p[1]});
    } else {
        q.edges = part.edges;
        size_t a = along_u ? 0 : 2, b = along_u ? 1 : 3;
        std::vector<bool> e = q.edges;
        q.edges[a] = e[b]; q.edges[b] = e[a];
    }
    for (auto& hole : q.holes) {
        double a = hole[0], b = hole[1], d = hole[2], h = hole[3];
        hole = {along_u ? part.u - a : a, !along_u ? part.v - b : b, d, h};
    }
    for (auto& op : q.operations) {
        std::string face = op.at("face");
        if (face == "top" || face == "bottom") op[axis] = reflect_value(length, op.at(axis).get<double>());
        else if (face.starts_with(axis)) op["face"] = axis + (face.ends_with("0") ? "1" : "0");
        else op["u"] = reflect_value(length, op.at("u").get<double>());
    }
    for (auto& cut : q.machining) {
        cut[axis] = reflect_value(length, cut.at(axis).get<double>());
        cut["angle"] = py::mod(-py::get(cut, "angle", 0).get<double>(), 180);
    }
    auto new_specs = cad_geometry::edge_specs(q.points(), q.t);
    std::map<std::string, cad_geometry::EdgeSpec> old_specs;
    for (const auto& item : cad_geometry::edge_specs(part.points(), part.t)) old_specs.emplace(item.edge, item);
    auto distance = [](const Point3& a, const Point3& b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); };
    for (json* collection : {&q.fillets, &q.chamfers}) {
        for (auto& feature : *collection) {
            auto old = old_specs.find(feature.at("edge").get<std::string>());
            if (old == old_specs.end()) throw py::KeyError("'" + feature.at("edge").get<std::string>() + "'");
            Point3 a = reflect3(old->second.geometry.point), b = reflect3(old->second.end);
            const cad_geometry::EdgeSpec* match = nullptr;
            for (const auto& item : new_specs) {
                if ((distance(a, item.geometry.point) < 1e-6 && distance(b, item.end) < 1e-6) || (distance(b, item.geometry.point) < 1e-6 && distance(a, item.end) < 1e-6)) { match = &item; break; }
            }
            if (match == nullptr) throw py::StopIteration();
            feature["edge"] = match->edge;
        }
    }
    q.validate();
    return q;
}

ProjectPtr extract_group(const Project& project, const std::string& group) {
    auto result = std::make_shared<Project>(project);
    result->name = group;
    std::vector<Part> kept;
    for (const auto& p : result->parts) if (p.group == group) kept.push_back(p);
    result->parts = kept;
    std::set<std::string> ids;
    for (const auto& p : result->parts) ids.insert(p.id);
    json hardware = json::array();
    for (const auto& h : result->hardware) {
        json owner = py::get(h, "owner_id");
        if (owner.is_string() && ids.count(owner.get<std::string>())) hardware.push_back(h);
    }
    result->hardware = hardware;
    for (auto& p : result->parts) {
        json source = py::get(p.assembly, "shared_worktop");
        if (py::truthy(source)) {
            bool outside = false;
            for (const auto& anchor : py::get(source, "anchors", json::array())) if (!ids.count(anchor.get<std::string>())) outside = true;
            if (outside) p.assembly.erase("shared_worktop");
        }
        for (const auto& i : py::get(p.assembly, "anchors", json::array()))
            if (!ids.count(i.get<std::string>())) throw ValueError("Mebel ma powiązania z inną grupą. Odłącz je przed zapisem szablonu.");
    }
    result->validate();
    return result;
}

namespace {

void attachment_values(const json& value, std::set<std::string>& out) {
    if (value.is_object()) {
        for (auto& [key, item] : value.items()) {
            if (key == "attachment_id" && item.is_string()) out.insert(item.get<std::string>());
            else attachment_values(item, out);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) attachment_values(item, out);
    }
}

json remap_attachments(const json& value, const std::map<std::string, std::string>& attachments) {
    if (value.is_object()) {
        json out = json::object();
        for (auto& [key, item] : value.items()) {
            if (key == "attachment_id" && item.is_string()) {
                auto found = attachments.find(item.get<std::string>());
                out[key] = found != attachments.end() ? json(found->second) : item;
            } else out[key] = remap_attachments(item, attachments);
        }
        return out;
    }
    if (value.is_array()) {
        json out = json::array();
        for (const auto& item : value) out.push_back(remap_attachments(item, attachments));
        return out;
    }
    return value;
}

json remap_ids(const json& value, const std::map<std::string, std::string>& ids) {
    if (value.is_object()) {
        json out = json::object();
        for (auto& [k, v] : value.items()) out[k] = remap_ids(v, ids);
        return out;
    }
    if (value.is_array()) {
        json out = json::array();
        for (const auto& v : value) out.push_back(remap_ids(v, ids));
        return out;
    }
    if (value.is_string()) {
        auto found = ids.find(value.get<std::string>());
        return found != ids.end() ? json(found->second) : value;
    }
    return value;
}

// [a + b for a, b in zip(values, offset)]
json shifted(const json& values, const std::vector<double>& offset) {
    json out = json::array();
    for (size_t i = 0; i < values.size() && i < offset.size(); ++i) out.push_back(values[i].get<double>() + offset[i]);
    return out;
}

const std::string& mapped(const std::map<std::string, std::string>& map, const std::string& key) {
    auto found = map.find(key);
    if (found == map.end()) throw py::KeyError("'" + key + "'");
    return found->second;
}

json* manifest_of(Part& p) {
    // configurator_source.get('manifest') or assembly.get('corner_source', {}).get('manifest')
    if (p.configurator_source.is_object() && p.configurator_source.contains("manifest") && py::truthy(p.configurator_source["manifest"])) return &p.configurator_source["manifest"];
    if (p.assembly.is_object() && p.assembly.contains("corner_source")) {
        json& corner = p.assembly["corner_source"];
        if (corner.is_object() && corner.contains("manifest") && py::truthy(corner["manifest"])) return &corner["manifest"];
    }
    return nullptr;
}

}  // namespace

ProjectPtr insert_template(const Project& project, const Project& template_source, const std::string& name, const json& offset_) {
    if (py::strip(name).empty()) throw ValueError("Podaj nazwę wstawianego mebla.");
    std::vector<double> offset;
    for (const auto& v : offset_) offset.push_back(finite(v));
    auto result = std::make_shared<Project>(project);
    Project tpl = template_source;
    tpl.validate();
    std::map<std::string, std::string> ids;
    for (const auto& p : tpl.parts) ids[p.id] = uid();
    std::map<std::string, std::string> material_ids;
    // One new purchase attachment per copied installation, shared by its
    // hardware rows and manufacturing features but independent of the source.
    std::set<std::string> found;
    {
        json sources = json::array({tpl.hardware});
        for (const auto& p : tpl.parts) sources.push_back(json{{"operations", p.operations}, {"machining", p.machining}, {"assembly", p.assembly}, {"source", p.configurator_source}});
        attachment_values(sources, found);
    }
    std::map<std::string, std::string> attachments;
    for (const auto& value : found) attachments[value] = uid();
    for (auto& p : tpl.parts) {
        p.operations = remap_attachments(p.operations, attachments); p.machining = remap_attachments(p.machining, attachments);
        p.assembly = remap_attachments(p.assembly, attachments); p.configurator_source = remap_attachments(p.configurator_source, attachments);
    }
    tpl.hardware = remap_attachments(tpl.hardware, attachments);
    std::map<std::string, const Part*> by_old_id;
    for (const auto& p : tpl.parts) by_old_id[p.id] = &p;
    std::set<std::string> verified_manifests, verified_worktops;
    for (auto& p : tpl.parts) {
        json* manifest = manifest_of(p);
        if (manifest != nullptr) {
            bool all = true;
            for (auto& [k, entry] : manifest->items()) {
                json entry_id = py::get(entry, "id");
                auto it = entry_id.is_string() ? by_old_id.find(entry_id.get<std::string>()) : by_old_id.end();
                if (it == by_old_id.end() || furniture_configurator::_geometry(*it->second) != py::get(entry, "signature")) { all = false; break; }
            }
            if (all) verified_manifests.insert(p.id);
        }
        json source = py::get(p.assembly, "shared_worktop");
        if (py::truthy(source) && py::get(source, "signature") == workspace_worktops::_signature(p)) verified_worktops.insert(p.id);
    }
    for (const auto& material : tpl.materials) {
        json data = core::asdict(material);
        data.erase("id");
        const Material* match = nullptr;
        for (const auto& m : result->materials) {
            json other = core::asdict(m);
            other.erase("id");
            if (other == data) { match = &m; break; }
        }
        std::string match_id;
        if (match == nullptr) {
            Material copy = material;
            copy.id = uid();
            std::set<std::string> occupied;
            for (const auto& m : result->materials) occupied.insert(m.name);
            if (occupied.count(copy.name)) copy.name += " — import " + copy.id.substr(0, 4);
            match_id = copy.id;
            result->materials.push_back(copy);
        } else match_id = match->id;
        material_ids[material.id] = match_id;
    }
    std::vector<std::string> groups;
    for (const auto& p : tpl.parts) if (std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
    std::map<std::string, std::string> group_names;
    std::set<std::string> occupied;
    for (const auto& p : result->parts) occupied.insert(p.group);
    for (const auto& group : groups) {
        std::string target = groups.size() == 1 ? name : name + " / " + group;
        std::string base = target;
        int number = 2;
        while (occupied.count(target)) { target = base + std::format(" ({})", number); ++number; }
        occupied.insert(target);
        group_names[group] = target;
    }
    std::set<std::string> placed_ids;
    for (const auto& p : tpl.parts) if (py::truthy(p.layout)) placed_ids.insert(p.id);
    const char* axes[3] = {"x", "y", "z"};
    for (auto& p : tpl.parts) {
        std::string old = p.id;
        p.id = ids.at(old); p.material = mapped(material_ids, p.material); p.group = group_names.at(p.group);
        bool placed = placed_ids.count(old) > 0;
        if (placed) {
            for (size_t i = 0; i < 3 && i < offset.size(); ++i) p.layout[axes[i]] = py::get(p.layout, axes[i], 0).get<double>() + offset[i];
        } else { p.x += offset[0]; p.y += offset[1]; p.z += offset[2]; }
        if (py::truthy(p.style_source)) {
            if (!placed) p.style_source["origin"] = shifted(p.style_source.at("origin"), offset);
            for (const char* key : {"material", "front_material"})
                if (py::truthy(py::get(p.style_source, key))) p.style_source[key] = mapped(material_ids, p.style_source[key].get<std::string>());
        }
        if (py::truthy(p.configurator_source)) {
            p.configurator_source = remap_ids(p.configurator_source, ids);
            json* cfg = find_key(p.configurator_source, "config") ? &p.configurator_source["config"] : nullptr;
            if (cfg != nullptr && py::truthy(*cfg)) {
                (*cfg)["name"] = p.group;
                if (!placed) (*cfg)["origin"] = shifted((*cfg).at("origin"), offset);
                for (const char* key : {"material", "front_material"})
                    if (py::truthy(py::get(*cfg, key))) (*cfg)[key] = mapped(material_ids, (*cfg)[key].get<std::string>());
            }
        }
        if (py::truthy(py::get(p.assembly, "corner_source"))) {
            p.assembly["corner_source"] = remap_ids(p.assembly["corner_source"], ids);
            json& corner = p.assembly["corner_source"];
            json* cfg = find_key(corner, "config") ? &corner["config"] : nullptr;
            if (cfg != nullptr && py::truthy(*cfg)) {
                (*cfg)["name"] = p.group;
                if (!placed) (*cfg)["origin"] = shifted((*cfg).at("origin"), offset);
                if (py::truthy(py::get(*cfg, "material"))) (*cfg)["material"] = mapped(material_ids, (*cfg)["material"].get<std::string>());
                if (py::truthy(py::get(*cfg, "front_material"))) (*cfg)["front_material"] = mapped(material_ids, (*cfg)["front_material"].get<std::string>());
            }
        }
        json source = py::get(p.assembly, "shared_worktop");
        if (py::truthy(source)) {
            bool all = true;
            for (const auto& anchor : py::get(source, "anchors", json::array())) if (!ids.count(anchor.get<std::string>())) all = false;
            if (all) {
                p.assembly["shared_worktop"] = remap_ids(source, ids);
                if (verified_worktops.count(old)) p.assembly["shared_worktop"]["signature"] = workspace_worktops::_signature(p);
            } else p.assembly.erase("shared_worktop");
        }
        json config = py::get(p.assembly, "config", json::object());
        if (py::truthy(py::get(config, "bay"))) p.assembly["config"]["bay"] = remap_ids(p.assembly["config"]["bay"], ids);
        config = py::get(p.assembly, "config", json::object());
        if (py::truthy(py::get(config, "corner_bay"))) {
            json bay = remap_ids(p.assembly["config"]["corner_bay"], ids);
            if (!placed) bay["hinge_point"] = shifted(bay.at("hinge_point"), std::vector<double>(offset.begin(), offset.begin() + std::min<size_t>(2, offset.size())));
            p.assembly["config"]["corner_bay"] = bay;
        }
        for (const char* key : {"anchors", "children"}) {
            if (p.assembly.contains(key)) {
                json mapped_ids = json::array();
                for (const auto& i : p.assembly[key]) mapped_ids.push_back(mapped(ids, i.get<std::string>()));
                p.assembly[key] = mapped_ids;
            }
        }
        if (p.assembly.contains("generated_by")) p.assembly["generated_by"] = mapped(ids, p.assembly["generated_by"].get<std::string>());
        if (p.assembly.contains("group")) p.assembly["group"] = p.group;
        for (auto& op : p.operations) {
            if (py::truthy(py::get(op, "connection_tag"))) op["connection_tag"] = "connector:" + p.group;
            if (py::truthy(py::get(op, "owner_id"))) op["owner_id"] = mapped(ids, op["owner_id"].get<std::string>());
            if (py::truthy(py::get(op, "opening_owner"))) op["opening_owner"] = mapped(ids, op["opening_owner"].get<std::string>());
            if (py::truthy(py::get(op, "corner_hinge_owner"))) op["corner_hinge_owner"] = mapped(ids, op["corner_hinge_owner"].get<std::string>());
        }
        for (auto& cut : p.machining)
            if (py::truthy(py::get(cut, "opening_owner"))) cut["opening_owner"] = mapped(ids, cut["opening_owner"].get<std::string>());
        result->parts.push_back(p);
    }
    for (auto item : tpl.hardware) {
        json owner_before = py::get(item, "owner_id");
        std::vector<double> placement_offset = owner_before.is_string() && placed_ids.count(owner_before.get<std::string>()) ? std::vector<double>{0, 0, 0} : offset;
        json group = py::get(item, "group");
        if (group.is_string() && group_names.count(group.get<std::string>())) item["group"] = group_names[group.get<std::string>()];
        if (py::truthy(py::get(item, "owner_id"))) item["owner_id"] = mapped(ids, item["owner_id"].get<std::string>());
        if (item.contains("anchors")) {
            json mapped_ids = json::array();
            for (const auto& i : item["anchors"]) mapped_ids.push_back(mapped(ids, i.get<std::string>()));
            item["anchors"] = mapped_ids;
        }
        if (item.contains("origin")) item["origin"] = shifted(item["origin"], placement_offset);
        if (item.contains("closed_y")) item["closed_y"] = item["closed_y"].get<double>() + placement_offset[1];
        if (item.contains("closed_front_y")) item["closed_front_y"] = item["closed_front_y"].get<double>() + placement_offset[1];
        if (item.contains("connection_tag")) {
            json wanted = py::get(item, "owner_id");
            for (const auto& q : result->parts) if (wanted.is_string() && q.id == wanted.get<std::string>()) { item["connection_tag"] = "connector:" + q.group; break; }
        }
        if (item.contains("positions")) {
            for (auto& position : item["positions"])
                for (size_t i = 0; i < 3; ++i) position[axes[i]] = position.at(axes[i]).get<double>() + placement_offset[i];
        }
        result->hardware.push_back(item);
    }
    std::set<std::string> verified_new;
    for (const auto& key : verified_manifests) verified_new.insert(ids.at(key));
    std::vector<std::string> configured;
    for (auto& p : result->parts) if (verified_new.count(p.id) && manifest_of(p) != nullptr) configured.push_back(p.id);
    if (!configured.empty()) {
        std::map<std::string, const Part*> by_id;
        for (const auto& p : result->parts) by_id[p.id] = &p;
        for (const auto& id : configured) {
            Part* p = nullptr;
            for (auto& q : result->parts) if (q.id == id) p = &q;
            json* manifest = manifest_of(*p);
            for (auto& [k, entry] : manifest->items()) {
                auto it = by_id.find(entry.at("id").get<std::string>());
                if (it == by_id.end()) throw py::KeyError("'" + entry.at("id").get<std::string>() + "'");
                entry["signature"] = furniture_configurator::_geometry(*it->second);
            }
        }
    }
    result = workspace_worktops::refresh_shared_worktops(result, false);
    result->validate();
    return result;
}

ProjectPtr equal_shelves(const Project& project, const std::string& group, const json& count_, double thickness, const std::string& material) {
    double count = whole(count_, 1, 30, "Liczba półek: od 1 do 30.");
    assembly::Frame f = assembly::frame(project, group);
    double clear_height = f.inner_top - f.inner_bottom;
    if (clear_height <= count * thickness) throw ValueError("Brak miejsca na półki.");
    auto result = std::make_shared<Project>(project);
    double space = (clear_height - count * thickness) / (count + 1);
    for (int i = 0; i < (int)count; ++i) {
        double level = f.inner_bottom - f.lower[2] + space + i * (space + thickness);
        auto [u, v] = assembly::suggested_size(*result, group, "shelf", thickness);
        Part shelf;
        shelf.name = std::format("Półka automatyczna {}", i + 1);
        shelf.u = u; shelf.v = v; shelf.t = thickness; shelf.material = material;
        result = assembly::attach(*result, shelf, group, "shelf", json{{"level", level}}).first;
    }
    return result;
}

}  // namespace productivity
