#include "cutters.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <stdexcept>

#include "sketch_design.h"

namespace cutters {

namespace {

constexpr double RAD = std::numbers::pi / 180;

[[noreturn]] void fail(const std::string& text) { throw std::invalid_argument(text); }

std::string number(double x) { return std::format("{:g}", x); }

bool finite(double x) { return std::isfinite(x); }

}  // namespace

const std::vector<CutterInfo>& catalog() {
    static const std::vector<CutterInfo> list = {
        {"prosty", "Prosty (walcowy)", "Płaskie dno i pionowe ścianki — rowki pod plecy, kieszenie, wpusty.", false, false, 8, 90},
        {"kulowy", "Kulowy", "Zaokrąglone dno rowka (półokrągły profil) — uchwyty wpuszczane, dekory.", false, false, 8, 90},
        {"v", "V (grawerski)", "Rowek w kształcie litery V o zadanym kącie — fazowanie, litery, dekoracyjne linie.", false, true, 12, 90},
        {"zaokraglajacy", "Zaokrąglający krawędź", "Zaokrągla krawędź płyty o zadany promień (frez do krawędzi, nie do rowków).", true, false, 3, 90},
        {"fazujacy", "Fazujący krawędź", "Ścina krawędź płyty pod kątem 45° (frez do krawędzi, nie do rowków).", true, false, 2, 45},
    };
    return list;
}

const CutterInfo& info(const std::string& id_or_name) {
    for (const auto& entry : catalog())
        if (entry.id == id_or_name || entry.name == id_or_name) return entry;
    fail("Nieznany frez „" + id_or_name + "”. Dostępne: prosty, kulowy, V, zaokrąglający, fazujący.");
}

Cutter cutter_from_json(const json& data) {
    Cutter cutter;
    if (!data.is_object()) fail("Opis freza musi być obiektem {typ, średnica, kąt}.");
    cutter.type = info(data.value("type", std::string("prosty"))).id;
    cutter.diameter = data.value("diameter", info(cutter.type).default_diameter);
    cutter.angle = data.value("angle", info(cutter.type).default_angle);
    validate(cutter);
    return cutter;
}

json cutter_to_json(const Cutter& cutter) { return json{{"type", cutter.type}, {"diameter", cutter.diameter}, {"angle", cutter.angle}}; }

void validate(const Cutter& cutter) {
    const CutterInfo& entry = info(cutter.type);
    if (!finite(cutter.diameter) || cutter.diameter < .5 || cutter.diameter > 40)
        fail(std::format("Średnica freza „{}” musi mieścić się w zakresie 0,5 … 40 mm (podano {}).", entry.name, number(cutter.diameter)));
    if (entry.has_angle && (!finite(cutter.angle) || cutter.angle < 20 || cutter.angle > 150))
        fail(std::format("Kąt freza V musi mieścić się w zakresie 20 … 150° (podano {}).", number(cutter.angle)));
}

double effective_width(const Cutter& cutter, double depth) {
    validate(cutter);
    if (!finite(depth) || depth <= 0) fail("Głębokość frezowania musi być dodatnia.");
    if (cutter.type == "kulowy") {
        double r = cutter.diameter / 2;
        if (depth >= r) return cutter.diameter;
        return 2 * std::sqrt(std::max(0.0, r * r - (r - depth) * (r - depth)));
    }
    if (cutter.type == "v") return std::min(cutter.diameter, 2 * depth * std::tan(cutter.angle / 2 * RAD));
    return cutter.diameter;
}

std::vector<json> cuts_for(const Op& op, const core::Part& part) {
    if (op.id.empty()) fail("Operacja frezowania nie ma identyfikatora.");
    validate(op.cutter);
    const CutterInfo& tool = info(op.cutter.type);
    if (tool.for_edges) fail(std::format("Frez „{}” obrabia krawędzie płyty — do rowka lub kieszeni wybierz frez prosty, kulowy albo V.", tool.name));
    if (op.face != "top" && op.face != "bottom") fail("Frezować można tylko powierzchnię górną (+T) albo dolną (−T).");
    if (!finite(op.depth) || op.depth < .1) fail("Głębokość frezowania musi wynosić co najmniej 0,1 mm.");
    if (op.depth >= part.t) fail(std::format("Głębokość {} mm jest nie mniejsza niż grubość płyty {} mm — frez przeciąłby płytę na wylot.", number(op.depth), number(part.t)));

    json cut = {{"type", "groove"}, {"shape", "slot"}, {"face", op.face}, {"depth", op.depth}, {"angle", 0.0}, {"op", op.id}, {"op_name", op.name},
                {"cutter", cutter_to_json(op.cutter)}, {"purpose", "frez: " + (op.name.empty() ? tool.name : op.name)}};
    double width = op.width > 0 ? op.width : effective_width(op.cutter, op.depth);

    if (op.kind == "rowek") {
        if (op.points.size() != 2) fail("Rowek wymaga dwóch punktów: początku i końca ścieżki.");
        if (op.width > 0 && op.width > op.cutter.diameter + 1e-9)
            fail(std::format("Rowek o szerokości {} mm jest szerszy niż frez Ø{} mm — wybierz grubszy frez albo zrób kieszeń.", number(op.width), number(op.cutter.diameter)));
        double dx = op.points[1][0] - op.points[0][0], dy = op.points[1][1] - op.points[0][1];
        double length = std::hypot(dx, dy);
        if (length < .1) fail("Początek i koniec rowka leżą w tym samym miejscu.");
        cut["u"] = (op.points[0][0] + op.points[1][0]) / 2;
        cut["v"] = (op.points[0][1] + op.points[1][1]) / 2;
        cut["angle"] = std::atan2(dy, dx) / RAD;
        cut["width"] = width;
        cut["length"] = length + width;  // zaokrąglone końce rowka wchodzą za punkty ścieżki o połowę szerokości
    } else if (op.kind == "kieszen") {
        if (op.points.size() != 2) fail("Kieszeń wymaga dwóch przeciwległych narożników.");
        double length = std::fabs(op.points[1][0] - op.points[0][0]), side = std::fabs(op.points[1][1] - op.points[0][1]);
        if (length < op.cutter.diameter - 1e-9 || side < op.cutter.diameter - 1e-9)
            fail(std::format("Kieszeń {} × {} mm jest za mała dla freza Ø{} mm.", number(length), number(side), number(op.cutter.diameter)));
        cut["type"] = "pocket"; cut["shape"] = "rect";
        cut["u"] = (op.points[0][0] + op.points[1][0]) / 2;
        cut["v"] = (op.points[0][1] + op.points[1][1]) / 2;
        cut["length"] = length; cut["width"] = side;
    } else if (op.kind == "kieszen_okragla") {
        if (op.points.size() != 1) fail("Kieszeń okrągła wymaga jednego punktu — środka.");
        double diameter = op.width > 0 ? op.width : op.cutter.diameter * 2;
        if (diameter < op.cutter.diameter - 1e-9) fail(std::format("Średnica kieszeni {} mm jest mniejsza niż frez Ø{} mm.", number(diameter), number(op.cutter.diameter)));
        cut["type"] = "pocket"; cut["shape"] = "circle";
        cut["u"] = op.points[0][0]; cut["v"] = op.points[0][1];
        cut["length"] = diameter; cut["width"] = diameter;
    } else {
        fail("Nieznany rodzaj operacji „" + op.kind + "”. Dostępne: rowek, kieszen, kieszen_okragla.");
    }

    // Obrys freza musi mieścić się w formatce i w jej konturze (tak samo jak w features::validate_cut) — z powodem zamiast cichego pominięcia.
    auto outline = sketch_design::cut_outline(cut, 24);
    auto polygon = part.points();
    for (const auto& p : outline) {
        if (p[0] < -1e-7 || p[0] > part.u + 1e-7 || p[1] < -1e-7 || p[1] > part.v + 1e-7)
            fail(std::format("Frez „{}” wykracza poza płytę {} × {} mm.", op.name.empty() ? tool.name : op.name, number(part.u), number(part.v)));
        if (!core::inside(p, polygon)) fail(std::format("Frez „{}” wykracza poza kontur płyty.", op.name.empty() ? tool.name : op.name));
    }
    return {cut};
}

EdgeResult apply_edge_cutter(const core::Part& part, const EdgeOp& op) {
    const CutterInfo& tool = info(op.cutter);
    if (!tool.for_edges) fail(std::format("Frez „{}” nie obrabia krawędzi — wybierz zaokrąglający albo fazujący.", tool.name));
    if (op.edges.empty()) fail("Nie wskazano żadnej krawędzi do obróbki.");
    auto change = sketch_design::apply_edge_style(part.fillets, part.chamfers, op.cutter == "zaokraglajacy" ? "zaokraglenie" : "fazka", op.size, op.edges, part.t);
    EdgeResult result;
    result.fillets = change.fillets;
    result.chamfers = change.chamfers;
    result.touched = change.touched;
    return result;
}

json add_cuts(const json& machining, const std::vector<json>& cuts) {
    json result = machining.is_array() ? machining : json::array();
    if (result.size() + cuts.size() > MAX_CUTS)
        fail(std::format("Na jednej płycie może być najwyżej {} obróbek (jest {}, dodajesz {}).", MAX_CUTS, result.size(), cuts.size()));
    for (const auto& cut : cuts) result.push_back(cut);
    return result;
}

json remove_op(const json& machining, const std::string& op_id) {
    json kept = json::array();
    if (!machining.is_array()) return kept;
    for (const auto& cut : machining)
        if (!(cut.is_object() && cut.value("op", std::string()) == op_id)) kept.push_back(cut);
    return kept;
}

std::vector<OpSummary> list_ops(const json& machining) {
    std::vector<OpSummary> out;
    if (!machining.is_array()) return out;
    for (const auto& cut : machining) {
        if (!cut.is_object() || !cut.contains("op")) continue;
        std::string id = cut.value("op", std::string());
        auto found = std::find_if(out.begin(), out.end(), [&](const OpSummary& s) { return s.id == id; });
        if (found == out.end()) {
            OpSummary summary;
            summary.id = id;
            summary.name = cut.value("op_name", std::string());
            summary.depth = cut.value("depth", 0.0);
            summary.kind = cut.value("type", std::string()) == "groove" ? "rowek" : cut.value("shape", std::string()) == "circle" ? "kieszen_okragla" : "kieszen";
            if (cut.contains("cutter") && cut["cutter"].is_object()) {
                const auto& c = cut["cutter"];
                try {
                    const CutterInfo& tool = info(c.value("type", std::string("prosty")));
                    summary.cutter_name = tool.name + " Ø" + number(c.value("diameter", tool.default_diameter));
                } catch (const std::invalid_argument&) {
                    summary.cutter_name = "nieznany frez";
                }
            }
            out.push_back(summary);
            found = out.end() - 1;
        }
        ++found->cuts;
    }
    return out;
}

size_t count_unowned(const json& machining) {
    size_t n = 0;
    if (machining.is_array())
        for (const auto& cut : machining)
            if (!(cut.is_object() && cut.contains("op"))) ++n;
    return n;
}

std::string describe(const Op& op, size_t cuts) {
    const CutterInfo& tool = info(op.cutter.type);
    std::string cutter = tool.name + (tool.has_angle ? " " + number(op.cutter.angle) + "°" : std::string()) + " Ø" + number(op.cutter.diameter);
    return std::format("{} „{}”: {}, głębokość {} mm, {} wpis{}", op.kind, op.name, cutter, number(op.depth), cuts, cuts == 1 ? "" : "ów");
}

std::vector<std::vector<Point>> cutter_thumbnail(const std::string& id, double width, double height, double margin) {
    if (!(width >= 30 && height >= 30)) fail("Miniatura freza wymaga pola co najmniej 30 × 30 px.");
    const CutterInfo& tool = info(id);
    std::vector<std::vector<Point>> out;
    out.push_back({{0, 0}, {width, 0}, {width, height}, {0, height}});
    const double cx = width / 2, top = margin, bottom = height - margin;
    const double half = (width - 2 * margin) * .16;  // połowa średnicy trzonka na miniaturce
    const double cut_h = (bottom - top) * .34;      // wysokość części roboczej
    // Trzonek (wspólny): prostokąt nad częścią roboczą.
    out.push_back({{cx - half * .8, top}, {cx + half * .8, top}, {cx + half * .8, bottom - cut_h}, {cx - half * .8, bottom - cut_h}});
    // Część robocza zależna od rodzaju freza.
    std::vector<Point> work;
    if (id == "prosty") {
        work = {{cx - half, bottom - cut_h}, {cx + half, bottom - cut_h}, {cx + half, bottom}, {cx - half, bottom}};
    } else if (id == "kulowy") {
        work = {{cx - half, bottom - cut_h}, {cx + half, bottom - cut_h}, {cx + half, bottom - half}};
        for (int i = 0; i <= 12; ++i) {
            double a = std::numbers::pi * i / 12;
            work.push_back({cx + half * std::cos(a), bottom - half + half * std::sin(a)});
        }
        work.push_back({cx - half, bottom - half});
    } else if (id == "v") {
        work = {{cx - half * 1.3, bottom - cut_h}, {cx + half * 1.3, bottom - cut_h}, {cx, bottom}};
    } else if (id == "zaokraglajacy") {
        work = {{cx - half, bottom - cut_h}, {cx + half * 1.6, bottom - cut_h}, {cx + half * 1.6, bottom}, {cx - half, bottom}};
        out.push_back(work);
        // wycięty łuk (ćwiartka) po prawej stronie — profil zaokrąglenia
        std::vector<Point> arc;
        for (int i = 0; i <= 10; ++i) {
            double a = std::numbers::pi / 2 * i / 10;
            arc.push_back({cx + half * 1.6 - half * 1.2 * std::cos(a), bottom - half * 1.2 * std::sin(a)});
        }
        arc.push_back({cx + half * 1.6, bottom});
        out.push_back(arc);
        return out;
    } else {  // fazujący
        work = {{cx - half, bottom - cut_h}, {cx + half * 1.6, bottom - cut_h}, {cx + half * 1.6, bottom - half * .6}, {cx + half * .4, bottom}, {cx - half, bottom}};
    }
    out.push_back(work);
    // Ślad freza na próbce płyty (dla narzędzi do rowków) — pokazuje profil rowka u dołu miniatury.
    if (!tool.for_edges) {
        double base = bottom + margin * .2;
        std::vector<Point> trace;
        if (id == "prosty") trace = {{cx - half, base - 4}, {cx + half, base - 4}, {cx + half, base}, {cx - half, base}};
        else if (id == "kulowy") {
            for (int i = 0; i <= 8; ++i) { double a = std::numbers::pi * i / 8; trace.push_back({cx + half * std::cos(a), base - 4 + 4 * std::sin(a)}); }
        } else trace = {{cx - half, base - 4}, {cx + half, base - 4}, {cx, base}};
        out.push_back(trace);
    }
    return out;
}

}  // namespace cutters
