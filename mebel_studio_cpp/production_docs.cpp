// Rysunki części i etykiety (HTML/SVG).
#include "production_docs.h"

#include <algorithm>
#include <format>
#include <vector>

#include "features.h"
#include "pyutil.h"

namespace production_docs {

using core::Part;
using core::Project;
using json = core::json;

namespace {

std::string g(double v) { return std::format("{:g}", v); }
double num(const json& v) { return py::to_float(v); }

// dict(u=u,v=v,diameter=d,depth=h,face='top') for u,v,d,h in part.holes + part.operations
json drills_of(const Part& part) {
    json drills = json::array();
    for (const auto& h : part.holes)
        drills.push_back(json{{"u", h.at(0)}, {"v", h.at(1)}, {"diameter", h.at(2)}, {"depth", h.at(3)}, {"face", "top"}});
    for (const auto& op : part.operations) drills.push_back(op);
    return drills;
}

}  // namespace

std::string part_svg(const Project& /*project*/, const Part& part) {
    auto esc = [](const std::string& s) { return py::html_escape(s); };
    double margin = std::max(part.u, part.v) * .12 + 30, font = std::max(part.u, part.v) / 45 + 5;
    double width = part.u + margin * 2, height = part.v + margin * 2;
    auto screen = [&](double u, double v) { return std::array<double, 2>{u + margin, margin + part.v - v}; };
    std::vector<std::string> items;
    items.push_back(std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/>", g(width), g(height)));
    std::string points;
    bool first = true;
    for (const auto& p : part.points()) {
        auto s = screen(p[0], p[1]);
        if (!first) points += ' ';
        first = false;
        points += g(s[0]) + "," + g(s[1]);
    }
    items.push_back(std::format("<polygon points=\"{}\" fill=\"#f3eee4\" stroke=\"#333\" stroke-width=\"{}\"/>", points, g(font / 10)));
    for (const auto& cut : part.machining) {
        auto [x, y] = screen(num(cut.at("u")), num(cut.at("v")));
        if (cut.at("shape") == "circle") {
            items.push_back(std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"none\" stroke=\"#c54b35\"/>", g(x), g(y), g(num(cut.at("width")) / 2)));
        } else {
            double length = num(cut.at("length")), width_cut = num(cut.at("width"));
            double radius = cut.at("shape") == "slot" ? width_cut / 2 : 0;
            items.push_back(std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" rx=\"{}\" fill=\"none\" stroke=\"#c54b35\" transform=\"rotate({} {} {})\"/>",
                                        g(x - length / 2), g(y - width_cut / 2), g(length), g(width_cut), g(radius), g(-num(py::get(cut, "angle", 0))), g(x), g(y)));
        }
    }
    json drills = drills_of(part);
    int i = 1;
    for (const auto& op : drills) {
        auto [position, unused] = features::drill_location(part, op);
        (void)unused;
        auto [x, y] = screen(position[0], position[1]);
        double radius = num(op.at("diameter")) / 2;
        std::string face = op.at("face");
        bool flat = face == "top" || face == "bottom";
        if (flat) items.push_back(std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"none\" stroke=\"#246698\"/>", g(x), g(y), g(radius)));
        else items.push_back(std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"#246698\"/>", g(x - font / 5), g(y - font / 5), g(font * 2 / 5), g(font * 2 / 5)));
        if (py::truthy(py::get(op, "countersink_diameter")) && flat)
            items.push_back(std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"none\" stroke=\"#246698\" stroke-dasharray=\"3 2\"/>", g(x), g(y), g(num(op.at("countersink_diameter")) / 2)));
        items.push_back(std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\" fill=\"#246698\">{}</text>", g(x + radius + font / 5), g(y), g(font * .6), i));
        ++i;
    }
    auto [x0, y0] = screen(0, 0);
    auto [x1, y1] = screen(part.u, part.v);
    items.push_back(std::format("<path d=\"M{},{}H{} M{},{}V{}\" fill=\"none\" stroke=\"#333\"/>", g(x0), g(y0 + margin * .35), g(x1), g(x1 + margin * .35), g(y0), g(y1)));
    items.push_back(std::format("<text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"{}\">U {} mm</text>", g((x0 + x1) / 2), g(y0 + margin * .7), g(font), g(part.u)));
    items.push_back(std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\">V {}</text>", g(x1 + margin * .5), g((y0 + y1) / 2), g(font * .7), g(part.v)));
    items.push_back(std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\">{} · T {} mm</text></svg>", g(margin), g(margin * .5), g(font * .7), esc(part.name), g(part.t)));
    std::string out;
    for (const auto& s : items) out += s;
    return out;
}

void drawings_export(Project& project, const std::string& path) {
    auto esc = [](const std::string& s) { return py::html_escape(s); };
    project.validate();
    std::string out =
        "<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>Rysunki części</title><style>body{font:14px Segoe UI;margin:20px}svg{width:100%;max-height:650px}"
        "table{border-collapse:collapse;width:100%}td,th{border:1px solid #aaa;padding:5px}.part{break-after:page}@media print{button{display:none}}</style>"
        "<button onclick=\"print()\">Drukuj / PDF</button><h1>" + esc(project.name) +
        "</h1><p>Rzuty nominalne U/V w mm. Numery odnoszą się do tabeli wierceń. Strony krawędziowe: U=0/max używają V,T; V=0/max używają U,T. "
        "Zaokrąglenia i fazy 3D opisuje STEP. Rysunki są dokumentacją roboczą.</p>";
    for (const auto& part : project.parts) {
        out += "<section class=\"part\"><h2>" + esc(part.group + " / " + part.name) + "</h2><p>ID " + esc(part.id) + " · " + esc(project.mat(part.material).name) + "</p>" + part_svg(project, part);
        out += "<table><tr><th>Nr</th><th>Strona</th><th>Wsp. 1</th><th>Wsp. 2</th><th>Ø</th><th>Głębokość</th><th>Pogłębienie stożkowe</th></tr>";
        int i = 1;
        for (const auto& op : drills_of(part)) {
            std::string countersink = py::truthy(py::get(op, "countersink_diameter"))
                ? std::format("Ø{} × {}", g(num(op.at("countersink_diameter"))), g(num(op.at("countersink_depth")))) : std::string("—");
            out += std::format("<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>", i, esc(op.at("face")), g(num(op.at("u"))), g(num(op.at("v"))),
                               g(num(op.at("diameter"))), g(num(op.at("depth"))), countersink);
            ++i;
        }
        out += "</table>";
        if (!part.machining.empty()) {
            out += "<h3>Frezowanie</h3><table><tr><th>Rodzaj / kształt / strona</th><th>Środek U,V</th><th>Długość × szerokość</th><th>Głębokość</th><th>Kąt</th></tr>";
            for (const auto& cut : part.machining) {
                double depth = cut.at("type") == "cutout" ? part.t : num(cut.at("depth"));
                out += std::format("<tr><td>{}</td><td>{}, {}</td><td>{} × {}</td><td>{}</td><td>{}°</td></tr>",
                                   esc(cut.at("type").get<std::string>() + " / " + cut.at("shape").get<std::string>() + " / " + cut.at("face").get<std::string>()),
                                   g(num(cut.at("u"))), g(num(cut.at("v"))), g(num(cut.at("length"))), g(num(cut.at("width"))), g(depth), g(num(py::get(cut, "angle", 0))));
            }
            out += "</table>";
        }
        json edge_ops = part.fillets;
        for (const auto& c : part.chamfers) edge_ops.push_back(c);
        out += "<p>Obróbka krawędzi: " + esc(py::str(edge_ops)) + "</p></section>";
    }
    out += "</html>";
    py::write_text(path, out);
}

void labels_export(Project& project, const std::string& path) {
    auto esc = [](const std::string& s) { return py::html_escape(s); };
    project.validate();
    std::string out =
        "<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>Etykiety części</title><style>@page{size:A4;margin:10mm}body{font:12px Segoe UI}"
        ".labels{display:grid;grid-template-columns:repeat(2,90mm);gap:5mm}.label{border:1px dashed #888;box-sizing:border-box;width:90mm;min-height:55mm;padding:4mm;break-inside:avoid}"
        "h3{margin:2mm 0}.id{font:bold 15px Consolas}svg{width:28mm;height:20mm;float:right}@media print{button{display:none}}</style>"
        "<button onclick=\"print()\">Drukuj etykiety</button><div class=\"labels\">";
    int index = 1;
    for (const auto& part : project.parts) {
        auto [a, b] = part.blank(project.edge_t);
        std::string edges;
        for (size_t k = 0; k < part.edges.size(); ++k) edges += (k ? " / " : "") + std::string(part.edges[k] ? "tak" : "—");
        out += std::format("<section class=\"label\">{}<small>{} · #{}</small><h3>{}</h3><div>{}</div><div>{}</div><div>Gotowe: {} × {} × {} mm</div>"
                           "<div>Cięcie: {} × {} mm · włókna: {}</div><div>Obrzeża U0/U1/V0/V1: {}</div><div class=\"id\">{}</div></section>",
                           part_svg(project, part), esc(project.name), index, esc(part.name), esc(part.group), esc(project.mat(part.material).name), g(part.u), g(part.v), g(part.t),
                           g(a), g(b), part.grain ? "U" : "dowolnie", edges, esc(part.id));
        ++index;
    }
    out += "</div></html>";
    py::write_text(path, out);
}

}  // namespace production_docs
