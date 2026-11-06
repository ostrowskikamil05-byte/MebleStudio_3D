// Self-contained A4 project book: layout, assemblies, parts and sheet plan.
#include "project_print.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <stdexcept>

#include "configurator_accessories.h"
#include "fitting_visuals.h"
#include "production_docs.h"
#include "pyutil.h"

namespace project_print {

using core::Camera;
using core::Part;
using core::Point3;
using core::Project;
using py::ValueError;

namespace {

using Vec = std::vector<Point3>;
using Bounds = std::pair<Point3, Point3>;

std::string esc(const std::string& value) { return py::html_escape(value); }
std::string esc(const json& value) { return py::html_escape(py::str(value)); }

std::string g(double v) { return std::format("{:g}", v); }
std::string f2(double v) { return std::format("{:.2f}", v); }

double fnum(const json& v) { return py::to_float(v); }

// '.2f' rstrip('0') rstrip('.') ; sub-5-microns collapse to 0
std::string num_json(const json& value) { return _num(fnum(value)); }

Vec _vertices(const Part& part) {
    Vec out;
    for (double t : {0.0, part.t})
        for (const auto& p : part.points()) out.push_back(core::world(part, p[0], p[1], t));
    return out;
}

Project _accessory_project(const Project& project, const std::vector<Part>& parts) {
    Project view = project;
    view.parts = parts;
    for (auto& p : view.parts) p.visible = true;
    std::set<std::string> ids;
    for (const auto& p : parts) ids.insert(p.id);
    json hardware = json::array();
    for (const auto& h : project.hardware) {
        json owner = py::get(h, "owner_id");
        json kind = py::get(h, "kind");
        if (owner.is_string() && ids.count(owner.get<std::string>()) && (kind == "leg" || kind == "led_strip")) hardware.push_back(h);
    }
    view.hardware = hardware;
    return view;
}

Bounds _bounds(const std::vector<Part>& parts, const Project* project = nullptr) {
    Vec vertices;
    for (const auto& p : parts) {
        Vec v = _vertices(p);
        vertices.insert(vertices.end(), v.begin(), v.end());
    }
    if (project) {
        Project view = _accessory_project(*project, parts);
        Vec extra = configurator_accessories::accessory_vertices(view);
        vertices.insert(vertices.end(), extra.begin(), extra.end());
    }
    if (vertices.empty()) throw ValueError("Projekt nie zawiera elementów do wydruku.");
    Point3 lo = vertices[0], hi = vertices[0];
    for (const auto& v : vertices)
        for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], v[i]); hi[i] = std::max(hi[i], v[i]); }
    return {lo, hi};
}

std::string _size_text(const Bounds& bounds) {
    const auto& [lo, hi] = bounds;
    std::string out;
    for (int i = 0; i < 3; ++i) out += (i ? " × " : "") + _num(hi[i] - lo[i]);
    return out + " mm (X × Y × Z)";
}

Project _nominal_project(const Project& project, const std::vector<Part>& parts) {
    // Cut-plan validation is independent of expensive 3D finishing features.
    Project nominal = project;
    nominal.parts = parts;
    nominal.hardware = json::array();
    for (auto& p : nominal.parts) {
        p.fillets = json::array(); p.chamfers = json::array(); p.machining = json::array(); p.operations = json::array();
        p.holes.clear();
        p.decor = json::object();
        p.assembly = json::object();
    }
    nominal.validate();
    return nominal;
}

std::string _edge_text(const Part& part) {
    if (!part.contour.empty()) return part.contour_edge ? "Cały kontur" : "Brak";
    std::string out;
    const char* labels[] = {"U0", "U1", "V0", "V1"};
    for (size_t i = 0; i < 4 && i < part.edges.size(); ++i)
        if (part.edges[i]) out += (out.empty() ? "" : ", ") + std::string(labels[i]);
    return out.empty() ? "Brak" : out;
}

std::string _dimension(const Point3& a, const Point3& b, const std::string& text, double offset = 20, const std::string& color = "#173b46") {
    double ax = a[0], ay = a[1], bx = b[0], by = b[1];
    double dx = bx - ax, dy = by - ay;
    double norm = std::hypot(dx, dy);
    if (norm < 2) return "";
    double nx = -dy / norm * offset, ny = dx / norm * offset;
    double aax = ax + nx, aay = ay + ny, bbx = bx + nx, bby = by + ny;
    return std::format("<g class=\"dim\" stroke=\"{}\" fill=\"none\" stroke-width=\"1.3\">"
                       "<path d=\"M{},{}L{},{} M{},{}L{},{}\"/>"
                       "<path d=\"M{},{}L{},{}\" marker-start=\"url(#arrow)\" marker-end=\"url(#arrow)\"/>"
                       "</g><text x=\"{}\" y=\"{}\" text-anchor=\"middle\""
                       " font-size=\"16\" fill=\"{}\" class=\"dim-label\">{}</text>",
                       color, f2(ax), f2(ay), f2(aax), f2(aay), f2(bx), f2(by), f2(bbx), f2(bby), f2(aax), f2(aay), f2(bbx), f2(bby),
                       f2((aax + bbx) / 2), f2((aay + bby) / 2 - 5), color, esc(text));
}

// repr(float)/repr(int) for the exploded value inside the marker key
std::string repr_number(const json& v) { return v.is_number_integer() ? std::to_string(v.get<long long>()) : py::repr(v.get<double>()); }

std::string diagram(const Project& project, const std::vector<Part>& parts, const std::string& view = "iso", const json& exploded = 0,
                    const std::map<std::string, int>* numbers = nullptr, const std::vector<Part>* callout_parts = nullptr, int width = 1000, int height = 570,
                    bool overall_dimensions = true) {
    Bounds bounds = _bounds(parts, &project);
    json ids = json::array(), callout_ids = json::array();
    for (const auto& p : parts) ids.push_back(p.id);
    if (callout_parts) for (const auto& p : *callout_parts) callout_ids.push_back(p.id);
    std::string marker_key = "('" + view + "', " + repr_number(exploded) + ", " + py::str(ids) + ", " + py::str(callout_ids) + ")";
    std::string marker_id = "arrow-" + py::sha256_hex(marker_key).substr(0, 16);
    const auto& [lo, hi] = bounds;
    Point3 center{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    double ex = fnum(exploded);
    std::map<std::string, std::pair<double, double>> angles{{"iso", {-38, 24}}, {"front", {0, 0}}, {"top", {0, 90}}};
    Camera camera(angles.at(view).first, angles.at(view).second, 1, center);
    std::map<std::string, Point3> shifts, centers;
    std::vector<std::pair<Vec, std::string>> faces;
    Vec vertices;
    for (const auto& part : parts) {
        Point3 xyz = core::world(part, part.u / 2, part.v / 2, part.t / 2);
        Point3 shift{(xyz[0] - center[0]) * ex, (xyz[1] - center[1]) * ex, (xyz[2] - center[2]) * ex};
        shifts[part.id] = shift;
        auto points = part.points();
        Vec bottom, top;
        for (const auto& [u, v] : points) {
            Point3 b = core::world(part, u, v, 0);
            bottom.push_back({b[0] + shift[0], b[1] + shift[1], b[2] + shift[2]});
        }
        for (const auto& [u, v] : points) {
            Point3 t = core::world(part, u, v, part.t);
            top.push_back({t[0] + shift[0], t[1] + shift[1], t[2] + shift[2]});
        }
        vertices.insert(vertices.end(), bottom.begin(), bottom.end());
        vertices.insert(vertices.end(), top.begin(), top.end());
        centers[part.id] = {xyz[0] + shift[0], xyz[1] + shift[1], xyz[2] + shift[2]};
        std::vector<Vec> geometry;
        geometry.push_back(Vec(bottom.rbegin(), bottom.rend()));
        geometry.push_back(top);
        size_t n = points.size();
        for (size_t i = 0; i < n; ++i) geometry.push_back({bottom[i], bottom[(i + 1) % n], top[(i + 1) % n], top[i]});
        std::string color = project.mat(part.material).color;
        for (const auto& face : geometry) {
            const Point3 &a = face[0], &b = face[1], &c = face[2];
            double ab[3], ac[3];
            for (int i = 0; i < 3; ++i) { ab[i] = b[i] - a[i]; ac[i] = c[i] - a[i]; }
            double nn[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
            double norm = std::sqrt(py::sum(std::vector<double>{nn[0] * nn[0], nn[1] * nn[1], nn[2] * nn[2]}));
            if (norm == 0) norm = 1;
            const double lightv[3] = {-.3, -.5, .81};
            double light = std::abs(py::sum(std::vector<double>{nn[0] * lightv[0], nn[1] * lightv[1], nn[2] * lightv[2]}) / norm);
            faces.push_back({face, core::shade(color, .72 + light * .28)});
        }
    }
    for (const auto& ff : fitting_visuals::fitting_faces(_accessory_project(project, parts))) {
        Point3 shift{0, 0, 0};
        auto it = shifts.find(ff.owner);
        if (it != shifts.end()) shift = it->second;
        Vec shifted;
        for (const auto& point : ff.face) shifted.push_back({point[0] + shift[0], point[1] + shift[1], point[2] + shift[2]});
        vertices.insert(vertices.end(), shifted.begin(), shifted.end());
        faces.push_back({shifted, ff.color});
    }
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    bool first = true;
    for (const auto& p : vertices) {
        Point3 q = camera.view(p);
        if (first) { minx = maxx = q[0]; miny = maxy = q[1]; first = false; }
        else { minx = std::min(minx, q[0]); maxx = std::max(maxx, q[0]); miny = std::min(miny, q[1]); maxy = std::max(maxy, q[1]); }
    }
    int side_margin = callout_parts && !callout_parts->empty() ? 240 : 90;
    camera.scale = std::min((width - 2.0 * side_margin) / std::max(maxx - minx, 1.0), (height - 130.0) / std::max(maxy - miny, 1.0));
    camera.pan = {-camera.scale * (minx + maxx) / 2, camera.scale * (miny + maxy) / 2};
    bool has_exploded = ex != 0;
    std::vector<std::string> items;
    items.push_back(std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\" role=\"img\" aria-label=\"{}\" data-explosion=\"{}\">", width, height,
                                esc(has_exploded ? std::string("Widok rozstrzelony") : "Widok " + view), _num(ex)));
    items.push_back(std::format("<defs><marker id=\"{}\" markerWidth=\"5\" markerHeight=\"5\" refX=\"2.5\" refY=\"2.5\" orient=\"auto-start-reverse\"><path d=\"M0,0 L5,2.5 L0,5\" fill=\"#173b46\"/></marker></defs>", marker_id));
    items.push_back("<rect width=\"100%\" height=\"100%\" fill=\"white\"/>");
    struct Drawn { double depth; std::vector<Point3> pts; std::string color; };
    std::vector<Drawn> drawn;
    for (const auto& [face, color] : faces) {
        std::vector<Point3> pts;
        std::vector<double> depths;
        for (const auto& p : face) { pts.push_back(camera.project(p, width, height)); depths.push_back(pts.back()[2]); }
        drawn.push_back({py::sum(depths) / (double)pts.size(), pts, color});
    }
    std::stable_sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });
    for (const auto& d : drawn) {
        std::string xy;
        for (size_t i = 0; i < d.pts.size(); ++i) xy += (i ? " " : "") + f2(d.pts[i][0]) + "," + f2(d.pts[i][1]);
        items.push_back(std::format("<polygon points=\"{}\" fill=\"{}\" stroke=\"#647078\" stroke-width=\".65\"/>", xy, d.color));
    }
    if (overall_dimensions) {
        // These endpoints always belong to the CLOSED bounds, independent of
        // the visual explosion displacement.
        const char* axis_names = "XYZ";
        for (int index = 0; index < 3; ++index) {
            Point3 base = view == "iso" && index == 1 ? Point3{hi[0], lo[1], lo[2]} : Point3{lo[0], lo[1], lo[2]};
            Point3 end = base;
            end[index] = hi[index];
            items.push_back(_dimension(camera.project(base, width, height), camera.project(end, width, height),
                                       std::string(1, axis_names[index]) + " " + _num(hi[index] - lo[index]) + " mm", index == 2 ? -(25.0 + index * 7) : 25.0 + index * 7));
        }
    }
    if (numbers && !numbers->empty()) {
        const std::vector<Part>& source = callout_parts && !callout_parts->empty() ? *callout_parts : parts;
        if (!callout_parts || callout_parts->empty()) {
            for (const auto& part : source) {
                Point3 q = camera.project(centers.at(part.id), width, height);
                items.push_back(std::format("<g class=\"part-number\" data-part-id=\"{}\"><circle cx=\"{}\" cy=\"{}\" r=\"15\" fill=\"white\" stroke=\"#173b46\"/><text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"16\">{}</text></g>",
                                            esc(part.id), f2(q[0]), f2(q[1]), f2(q[0]), f2(q[1] + 5), numbers->at(part.id)));
            }
        } else {
            struct Row { double y, x; const Part* part; };
            std::vector<Row> sides[2];
            for (const auto& part : *callout_parts) {
                Point3 q = camera.project(centers.at(part.id), width, height);
                sides[q[0] >= width / 2.0 ? 1 : 0].push_back({q[1], q[0], &part});
            }
            for (int right = 0; right < 2; ++right) {
                auto& rows = sides[right];
                std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.y < b.y; });
                double step = std::min(35.0, (height - 70.0) / std::max<size_t>(1, rows.size()));
                double start = (height - ((double)rows.size() - 1) * step) / 2;
                int label_x = right ? width - 220 : 18;
                for (size_t index = 0; index < rows.size(); ++index) {
                    const Part& part = *rows[index].part;
                    double y = start + (double)index * step;
                    Point3 point = camera.project(centers.at(part.id), width, height);
                    int edge_x = right ? label_x - 6 : label_x + 205;
                    std::string label = std::format("#{} · {} × {} × {} mm", numbers->at(part.id), _num(part.u), _num(part.v), _num(part.t));
                    items.push_back(std::format("<g class=\"part-callout\" data-part-id=\"{}\" data-u=\"{}\" data-v=\"{}\" data-t=\"{}\">"
                                                "<path d=\"M{},{}L{},{}\" stroke=\"#50666d\" fill=\"none\"/>"
                                                "<circle cx=\"{}\" cy=\"{}\" r=\"2.5\" fill=\"#173b46\"/>"
                                                "<rect x=\"{}\" y=\"{}\" width=\"210\" height=\"23\" rx=\"3\" fill=\"white\" fill-opacity=\".94\"/>"
                                                "<text x=\"{}\" y=\"{}\" font-size=\"16\" fill=\"#173b46\">{}</text></g>",
                                                esc(part.id), _num(part.u), _num(part.v), _num(part.t), f2(point[0]), f2(point[1]), f2((double)edge_x), f2(y),
                                                f2(point[0]), f2(point[1]), label_x - 3, f2(y - 13), label_x, f2(y + 4), esc(label)));
                }
            }
        }
    }
    items.push_back("</svg>");
    std::string out;
    for (const auto& s : items) out += s;
    return py::replace_all(out, "url(#arrow)", "url(#" + marker_id + ")");
}

std::string _parts_table(const Project& project, const std::vector<Part>& parts, const std::map<std::string, int>& numbers) {
    std::string rows = "<table class=\"parts\"><thead><tr><th>Nr</th><th>Element / materiał</th><th>Gotowe U × V × T [mm]</th><th>Cięcie U × V [mm]</th><th>Szt.</th><th>Obrzeża</th></tr></thead><tbody>";
    for (const auto& part : parts) {
        auto [a, b] = part.blank(project.edge_t);
        rows += std::format("<tr data-part-id=\"{}\"><td>{}</td><td>{}<br><small>{}</small></td><td>{} × {} × {}</td><td>{} × {}</td><td>1</td><td>{}</td></tr>",
                            esc(part.id), numbers.at(part.id), esc(part.name), esc(project.mat(part.material).name), _num(part.u), _num(part.v), _num(part.t), _num(a), _num(b), esc(_edge_text(part)));
    }
    return rows + "</tbody></table>";
}

std::string _operations(const Part& part) {
    json drills = json::array();
    for (const auto& h : part.holes) drills.push_back(json{{"u", h.at(0)}, {"v", h.at(1)}, {"diameter", h.at(2)}, {"depth", h.at(3)}, {"face", "top"}});
    for (const auto& op : part.operations) drills.push_back(op);
    std::string out;
    if (!drills.empty()) {
        out += "<h3>Wiercenia</h3><table><thead><tr><th>Nr</th><th>Strona</th><th>Współrzędne 1 / 2 [mm]</th><th>Ø / głębokość [mm]</th><th>Pogłębienie [mm]</th></tr></thead><tbody>";
        int i = 1;
        for (const auto& op : drills) {
            std::string cone = !py::truthy(py::get(op, "countersink_diameter")) ? std::string("—")
                : "Ø" + num_json(op.at("countersink_diameter")) + " × " + num_json(op.at("countersink_depth"));
            out += std::format("<tr><td>{}</td><td>{}</td><td>{} / {}</td><td>{} / {}</td><td>{}</td></tr>", i, esc(op.at("face")), num_json(op.at("u")), num_json(op.at("v")),
                               num_json(op.at("diameter")), num_json(op.at("depth")), esc(cone));
            ++i;
        }
        out += "</tbody></table>";
    }
    if (!part.machining.empty()) {
        out += "<h3>Frezowanie</h3><table><thead><tr><th>Rodzaj / strona</th><th>Środek U,V</th><th>Długość × szerokość</th><th>Głębokość</th><th>Kąt</th></tr></thead><tbody>";
        for (const auto& cut : part.machining) {
            double depth = cut.at("type") == "cutout" ? part.t : fnum(cut.at("depth"));
            out += std::format("<tr><td>{}</td><td>{} / {}</td><td>{} × {}</td><td>{}</td><td>{}°</td></tr>",
                               esc(cut.at("type").get<std::string>() + " / " + cut.at("shape").get<std::string>() + " / " + cut.at("face").get<std::string>()),
                               num_json(cut.at("u")), num_json(cut.at("v")), num_json(cut.at("length")), num_json(cut.at("width")), _num(depth), num_json(py::get(cut, "angle", 0)));
        }
        out += "</tbody></table>";
    }
    struct Edge { const json* values; const char* label; const char* param; };
    for (const Edge& e : {Edge{&part.fillets, "Zaokrąglenia", "radius"}, Edge{&part.chamfers, "Fazy 45°", "distance"}}) {
        if (!e.values->empty()) {
            std::string joined;
            bool first = true;
            for (const auto& item : *e.values) {
                joined += (first ? "" : "; ") + py::str(item.at("edge")) + ": " + num_json(item.at(e.param)) + " mm";
                first = false;
            }
            out += std::string("<p>") + e.label + ": " + esc(joined) + "</p>";
        }
    }
    if (py::truthy(part.profile))
        out += "<p>Dokładny profil zapisany w modelu: " + esc(py::get(part.profile, "kind", "")) + ". Krzywe w tym rzucie są próbkowane; geometria dokładna znajduje się w STEP.</p>";
    return out;
}

std::string _sheet_svg(const json& sheet, const std::map<std::string, int>& numbers) {
    double width = fnum(sheet.at("u")), height = fnum(sheet.at("v"));
    double font = std::max(width, height) / 85;
    std::string out = std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"{} {} {} {}\"><rect x=\"0\" y=\"0\" width=\"{}\" height=\"{}\" fill=\"#faf8f1\" stroke=\"#444\" stroke-width=\"{}\"/>",
                                  g(-font * 2), g(-font * 2), g(width + font * 4), g(height + font * 4), g(width), g(height), g(font / 9));
    for (const auto& piece : sheet.at("pieces")) {
        double x = fnum(piece.at("x")), y = fnum(piece.at("y")), u = fnum(piece.at("u")), v = fnum(piece.at("v"));
        std::string label = "#" + std::to_string(numbers.at(piece.at("id").get<std::string>()));
        out += std::format("<g data-part-id=\"{}\"><rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"#dce7e9\" stroke=\"#47616a\" stroke-width=\"{}\"/>"
                           "<text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"{}\" fill=\"#173b46\">{}</text></g>",
                           esc(piece.at("id")), g(x), g(y), g(u), g(v), g(font / 12), g(x + u / 2), g(y + v / 2), g(std::min(font, std::max(5.0, std::min(u, v) / 3))), label);
    }
    return out + "</svg>";
}

const char* CSS = R"CSS(@page{size:A4 portrait;margin:12mm;@bottom-right{font:9px Arial;color:#52636a;content:"Strona " counter(page) " / " counter(pages)}}*{box-sizing:border-box}body{font:11px Arial,sans-serif;color:#182f38;background:#e6ebee;margin:0;line-height:1.35}.toolbar{position:sticky;top:0;background:#173b46;color:white;padding:12px 20px;z-index:2}.toolbar button{padding:8px 14px;margin-right:12px}.book{max-width:210mm;margin:18px auto}.print-page{background:white;padding:12mm;margin:0 0 15px;break-after:page;page-break-after:always}.print-page:last-child{break-after:auto;page-break-after:auto}h1{font-size:22px;margin:0 0 10px}h2{font-size:17px;margin:0 0 7px}h3{font-size:12px;margin:9px 0 4px}p{margin:6px 0}.note{color:#52636a;font-size:10px}.meta{border-bottom:1px solid #bdcbd0;padding-bottom:7px;margin-bottom:9px}table{border-collapse:collapse;width:100%;margin:7px 0}th,td{border:1px solid #b5c2c7;padding:4px;vertical-align:top;text-align:left}th{background:#edf3f4}thead{display:table-header-group}tr{break-inside:avoid;page-break-inside:avoid}small{color:#54666c}.overview-view{margin:4px 0;break-inside:avoid}.overview-view svg{width:100%;height:85mm}.overview-view h3{margin:1px 0}.overview-iso svg{width:100%;height:155mm}.exploded svg{width:100%;height:110mm}.part-drawing svg{width:100%;height:116mm}.sheet-drawing svg{width:100%;height:130mm}.dimensions{font-weight:bold;color:#173b46}.page-footer{font-size:9px;color:#52636a;border-top:1px solid #d4dfe3;padding-top:5px;margin-top:8px}.dim-label{paint-order:stroke;stroke:white;stroke-width:5px;stroke-linejoin:round}a{color:#173b46;overflow-wrap:anywhere}@media print{body{background:white}.toolbar{display:none}.book{margin:0;max-width:none}.print-page{padding:0;margin:0;box-shadow:none}a{text-decoration:none}svg{print-color-adjust:exact;-webkit-print-color-adjust:exact}th{print-color-adjust:exact;-webkit-print-color-adjust:exact}})CSS";

}  // namespace

std::string _num(double value) {
    if (!std::isfinite(value)) throw ValueError("Wymiary do wydruku muszą być skończone.");
    if (std::abs(value) < .005) value = 0;
    std::string s = f2(value);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

json export_project_print(const Project& project, const std::string& path, const Options& options) {
    double explosion = options.explosion;
    if (!std::isfinite(explosion) || !(0 <= explosion && explosion <= 2)) throw ValueError("Rozstrzelenie do wydruku: wartość od 0 do 2.");
    Project snapshot = project;
    std::map<std::string, int> numbers;
    {
        int i = 1;
        for (const auto& p : snapshot.parts) numbers[p.id] = i++;
    }
    std::vector<Part> parts;
    for (const auto& p : snapshot.parts) if (!options.group || p.group == *options.group) parts.push_back(p);
    if (parts.empty()) throw ValueError("Wybrany projekt lub mebel nie zawiera elementów.");
    Project nominal = _nominal_project(snapshot, parts);
    std::vector<std::string> groups;
    for (const auto& p : parts) if (std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
    std::vector<std::string> out;
    out.push_back("<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    out.push_back("<title>" + esc(snapshot.name) + " — komplet do wydruku</title><style>" + CSS + "</style></head><body>");
    out.push_back("<div class=\"toolbar\"><button type=\"button\" onclick=\"window.print()\">Drukuj / zapisz PDF</button>Komplet A4 · wymiary w mm · druk w skali strony</div><main class=\"book\">");
    const std::string footer = "<div class=\"page-footer\">MebelStudio3D 0.12 · wszystkie podane wymiary w mm · rysunki nominalne, bez skali 1:1</div>";
    auto section = [&](const std::string& title, const std::string& body, const std::string& attrs = "") {
        out.push_back("<section class=\"print-page\" " + attrs + "><h2>" + esc(title) + "</h2>" + body + footer + "</section>");
    };
    std::string views;
    for (auto [view, label] : std::vector<std::pair<std::string, std::string>>{{"top", "Cały zestaw — z góry"}, {"front", "Cały zestaw — z przodu"}})
        views += "<div class=\"overview-view\"><h3>" + label + "</h3>" + diagram(snapshot, parts, view, 0, nullptr, nullptr, 1000, 490) + "</div>";
    section(snapshot.name + " — cały zestaw",
            "<div class=\"meta\">" + esc(std::format("{} mebli / grup · {} części", groups.size(), parts.size())) + "<p class=\"dimensions\">Gabaryty zamkniętego zestawu: " +
                esc(_size_text(_bounds(parts, &snapshot))) + "</p></div>" + views,
            "data-kind=\"overview\"");
    section("Cały zestaw — 3D", "<div class=\"meta dimensions\">Gabaryty zamkniętego zestawu: " + esc(_size_text(_bounds(parts, &snapshot))) + "</div><div class=\"overview-iso\">" +
                diagram(snapshot, parts, "iso", 0, nullptr, nullptr, 1000, 740) + "</div>", "data-kind=\"overview-3d\"");
    {
        std::string list;
        for (const auto& name : groups) list += "<li>" + esc(name) + "</li>";
        section("Spis mebli i części", "<p>" + esc(snapshot.name) + "</p><ul>" + list + "</ul>" + _parts_table(snapshot, parts, numbers) +
                    "<p class=\"note\">Każdy wiersz oznacza jedną część. Cięcie uwzględnia obrzeża prostokątnych formatek o grubości " + _num(snapshot.edge_t) +
                    " mm. Kontury nieregularne mają prostokątny półfabrykat. Ukryte części modelu również są ujęte.</p>", "data-kind=\"parts-list\"");
    }
    for (const auto& name : groups) {
        std::vector<Part> assembly;
        for (const auto& p : parts) if (p.group == name) assembly.push_back(p);
        // Furniture reports use the furniture's construction axes, while the
        // overview above retains rigid room/layout transforms through world().
        for (auto& p : assembly) p.layout = json::object();
        for (size_t offset = 0; offset < assembly.size(); offset += 20) {
            std::vector<Part> chunk(assembly.begin() + (long)offset, assembly.begin() + (long)std::min(assembly.size(), offset + 20));
            std::string suffix = assembly.size() > 20 ? std::format(" · części {}–{}", offset + 1, offset + chunk.size()) : std::string();
            std::string body = "<div class=\"meta dimensions\">Gabaryty zamkniętego mebla w jego osiach: " + esc(_size_text(_bounds(assembly, &snapshot))) + "</div>";
            body += "<div class=\"exploded\">" + diagram(snapshot, assembly, "iso", explosion, &numbers, &chunk, 1000, 570, false) + "</div>";
            body += "<p class=\"note\">Rozstrzelenie służy pokazaniu części. Opisy U × V × T są rzeczywistymi wymiarami nominalnymi części; nie obejmują odstępów widoku rozstrzelonego. Numery odpowiadają tabeli.</p>";
            body += _parts_table(snapshot, chunk, numbers);
            body += "<p class=\"note\">Schemat brył konstrukcyjnych: otwory, kieszenie i fazy nie są pokazane jako geometria 3D. Nóżki i profile LED pokazano schematycznie. Ich dane są na rysunkach części i w zestawieniu okuć; dokładne bryły eksportuje STEP.</p>";
            section(name + " — rozstrzelony" + suffix, body, "data-kind=\"assembly\" data-group=\"" + esc(name) + "\"");
        }
    }
    if (options.include_parts) {
        for (const auto& part : parts) {
            std::string body = "<div class=\"meta\">" + esc(part.group) + " · ID " + esc(part.id) + "<p class=\"dimensions\">Gotowe: " +
                               esc(_num(part.u) + " × " + _num(part.v) + " × " + _num(part.t)) + " mm · 1 szt.</p></div>";
            body += "<div class=\"part-drawing\">" + production_docs::part_svg(snapshot, part) + "</div>" + _parts_table(snapshot, {part}, numbers);
            body += std::string("<p>Włókna: ") + (part.grain ? "wzdłuż U" : "obrót w rozkroju dozwolony") + " · Obrzeże: " + _num(part.edge_m()) + " m.</p>" + _operations(part);
            body += "<p class=\"note\">Rzut U/V. Wiercenia top/bottom: U,V; na krawędziach U: V,T; na krawędziach V: U,T. Numery otworów odnoszą się do tabeli. Rysunek nie przedstawia profilu faz i zaokrągleń 3D.</p>";
            section("#" + std::to_string(numbers.at(part.id)) + " — " + part.name, body, "data-kind=\"part\" data-part-id=\"" + esc(part.id) + "\"");
        }
    }
    std::set<std::string> selected_ids;
    for (const auto& p : parts) selected_ids.insert(p.id);
    std::vector<json> hardware;
    for (const auto& h : snapshot.hardware) {
        json owner = py::get(h, "owner_id");
        if (!py::truthy(owner) || (owner.is_string() && selected_ids.count(owner.get<std::string>()))) hardware.push_back(h);
    }
    std::map<std::string, const Part*> by_id;
    for (const auto& p : parts) by_id[p.id] = &p;
    if (!hardware.empty()) {
        std::string body = "<table><thead><tr><th>Okucie / komponent</th><th>Ilość</th><th>Element</th><th>Producent / profil</th></tr></thead><tbody>";
        for (const auto& item : hardware) {
            json owner_id = py::get(item, "owner_id");
            const Part* owner = nullptr;
            if (owner_id.is_string() && by_id.count(owner_id.get<std::string>())) owner = by_id.at(owner_id.get<std::string>());
            json metadata = py::get(item, "meta", json::object());
            json source = py::get(item, "source");
            if (!py::truthy(source)) source = py::get(metadata, "source");
            std::string link;
            if (source.is_string()) {
                std::string s = source.get<std::string>();
                if (s.starts_with("https://") || s.starts_with("http://")) link = "<br><a href=\"" + esc(s) + "\">Dokumentacja producenta</a>";
            }
            json maker = py::get(metadata, "manufacturer");
            if (!py::truthy(maker)) maker = py::get(metadata, "profile_id");
            if (!py::truthy(maker)) maker = py::get(item, "kind", "");
            body += "<tr><td>" + esc(item.at("name")) + link + "</td><td>" + num_json(item.at("qty")) + " " + esc(py::get(item, "unit", "szt.")) + "</td><td>" +
                    esc(owner ? owner->group + " / " + owner->name : std::string("Zestaw")) + "</td><td>" + esc(maker) + "</td></tr>";
        }
        section("Okucia i wyposażenie", body + "</tbody></table><p class=\"note\">Komponenty kupowane są zestawione oddzielnie od formatek.</p>", "data-kind=\"hardware\"");
    }
    if (!options.group && py::truthy(py::get(snapshot.scene, "objects"))) {
        std::string body = "<table><thead><tr><th>Wyposażenie kupowane</th><th>S × G × H [mm]</th><th>Pozycja X / Y / Z [mm]</th><th>Cena własna [zł]</th></tr></thead><tbody>";
        for (const auto& item : snapshot.scene.at("objects")) {
            body += "<tr><td>" + esc(item.at("name")) + "</td><td>" + esc(num_json(item.at("width")) + " × " + num_json(item.at("depth")) + " × " + num_json(item.at("height"))) + "</td><td>" +
                    esc(num_json(item.at("x")) + " / " + num_json(item.at("y")) + " / " + num_json(item.at("z"))) + "</td><td>" + num_json(py::get(item, "price", 0)) + "</td></tr>";
        }
        section("Wyposażenie pomieszczenia", body + "</tbody></table><p class=\"note\">Modele ogólne do wizualizacji. Wymiary i prześwity montażowe wymagają danych konkretnego urządzenia. Te pozycje nie wchodzą do rozkroju płyt.</p>", "data-kind=\"room-objects\"");
    }
    json sheets = json::array();
    std::vector<std::string> unplaced;
    if (options.include_cut_plan) std::tie(sheets, unplaced) = core::cut_plan(nominal);
    int sheet_no = 1;
    for (const auto& sheet : sheets) {
        const auto& material = nominal.mat(sheet.at("material").get<std::string>());
        std::string body = "<div class=\"meta\">" + esc(material.name) + " · grubość " + num_json(sheet.at("thickness")) + " mm · arkusz " + num_json(sheet.at("u")) + " × " + num_json(sheet.at("v")) + " mm</div>";
        body += "<div class=\"sheet-drawing\">" + _sheet_svg(sheet, numbers) + "</div><p class=\"note\">Przybliżony układ gilotynowy. Kontury nieregularne są pakowane jako prostokątne półfabrykaty. Rzaz: " +
                _num(snapshot.kerf) + " mm; margines: " + _num(snapshot.margin) + " mm.</p>";
        body += "<table><thead><tr><th>Nr / element</th><th>X / Y na arkuszu [mm]</th><th>Wymiary na arkuszu [mm]</th><th>Obrót</th></tr></thead><tbody>";
        for (const auto& piece : sheet.at("pieces")) {
            const Part& p = *by_id.at(piece.at("id").get<std::string>());
            body += "<tr><td>#" + std::to_string(numbers.at(p.id)) + " " + esc(p.name) + "</td><td>" + num_json(piece.at("x")) + " / " + num_json(piece.at("y")) + "</td><td>" +
                    num_json(piece.at("u")) + " × " + num_json(piece.at("v")) + "</td><td>" + (py::truthy(piece.at("rotated")) ? "90°" : "0°") + "</td></tr>";
        }
        section("Rozkrój — arkusz " + std::to_string(sheet_no), body + "</tbody></table>", "data-kind=\"cut-plan\"");
        ++sheet_no;
    }
    if (!unplaced.empty()) {
        std::vector<Part> missing;
        for (const auto& p : parts) if (std::find(unplaced.begin(), unplaced.end(), p.id) != unplaced.end()) missing.push_back(p);
        section("Formatki nieumieszczone w rozkroju",
                "<p>Te elementy nie mieszczą się w wybranym półfabrykacie z uwzględnieniem marginesu i kierunku włókien:</p>" + _parts_table(snapshot, missing, numbers), "data-kind=\"unplaced\"");
    }
    out.push_back("</main></body></html>");
    std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
    std::string joined;
    for (const auto& block : out) joined += block;
    py::write_text(path, joined);
    int sections = 0;
    for (const auto& block : out) if (block.find("<section class=\"print-page\"") != std::string::npos) ++sections;
    json unplaced_json = json::array();
    for (const auto& id : unplaced) unplaced_json.push_back(id);
    json groups_json = json::array();
    for (const auto& name : groups) groups_json.push_back(name);
    return json{{"path", py::path_str(std::filesystem::absolute(target).lexically_normal())}, {"groups", groups_json}, {"parts", (long long)parts.size()},
                {"sheets", (long long)sheets.size()}, {"unplaced", unplaced_json}, {"sections", sections}};
}

}  // namespace project_print
