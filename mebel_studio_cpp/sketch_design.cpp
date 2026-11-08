#include "sketch_design.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <set>
#include <stdexcept>

namespace sketch_design {

namespace {

constexpr double PI = std::numbers::pi;

[[noreturn]] void fail(const std::string& text) { throw std::invalid_argument(text); }

std::string g(double x) { return std::format("{:g}", x); }

std::string pl_number(double x) {
    std::string s = g(x);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// ------------------------------------------------------------------ frezy

json make_cut(const std::string& pattern, const std::string& name, const std::string& type, const std::string& shape, const std::string& face, double u,
              double v, double length, double width, double depth, double angle) {
    json cut = {{"type", type},       {"shape", shape},   {"face", face},   {"u", u},     {"v", v},
                {"length", length},   {"width", width},   {"depth", depth}, {"angle", angle},
                {"generated_decor", "wzorek:" + pattern}, {"purpose", "Wzorek: " + name}};
    return cut;
}

std::vector<Point> rect_points(double u, double v) { return {{0, 0}, {u, 0}, {u, v}, {0, v}}; }

bool point_inside(const Point& q, const std::vector<Point>& polygon) { return core::inside(q, polygon); }

// Te same próbki co features::validate_cut: obrys frezu musi mieścić się w formatce i w konturze.
bool cut_fits(const json& cut, const std::vector<Point>& contour, double u, double v) {
    std::vector<Point> shape = contour.empty() ? rect_points(u, v) : contour;
    std::vector<Point> samples = cut_outline(cut, 16);
    if (std::string(cut.at("shape")) == "circle") samples = cut_outline(cut, 72);
    else if (std::string(cut.at("shape")) == "slot") {
        // walidacja CAD sprawdza cztery narożniki prostokąta length × width, więc sprawdzamy to samo
        double cu = cut.at("u"), cv = cut.at("v"), length = cut.at("length"), width = cut.at("width"), a = double(cut.at("angle")) * PI / 180;
        samples.clear();
        for (auto [du, dv] : std::vector<std::pair<int, int>>{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) {
            double x = du * length / 2, y = dv * width / 2;
            samples.push_back({cu + x * std::cos(a) - y * std::sin(a), cv + x * std::sin(a) + y * std::cos(a)});
        }
    }
    for (const Point& p : samples) {
        if (!(-1e-7 <= p[0] && p[0] <= u + 1e-7) || !(-1e-7 <= p[1] && p[1] <= v + 1e-7)) return false;
        if (!point_inside(p, shape)) return false;
    }
    return true;
}

// ------------------------------------------------------------------ parametry

using Params = std::map<std::string, double>;

Params resolve(const PatternInfo& info, const Params& given) {
    Params out;
    for (const auto& p : info.params) out[p.key] = p.def;
    for (const auto& [key, value] : given) {
        auto found = std::find_if(info.params.begin(), info.params.end(), [&](const PatternParam& p) { return p.key == key; });
        if (found == info.params.end()) fail("Wzorek „" + info.name + "” nie ma parametru „" + key + "”.");
        out[key] = value;
    }
    for (const auto& p : info.params) {
        double value = out[p.key];
        if (!std::isfinite(value) || value < p.min - 1e-9 || value > p.max + 1e-9)
            fail(std::format("{}: wartość musi mieścić się w zakresie {} … {} (jest {}).", p.label, pl_number(p.min), pl_number(p.max), pl_number(value)));
    }
    return out;
}

void need_depth(const Params& p, const std::string& key, const std::string& label, double thickness) {
    if (p.at(key) > thickness - 0.5 + 1e-9)
        fail(std::format("{}: {} mm to za dużo dla płyty {} mm — zostaw przynajmniej 0,5 mm materiału.", label, pl_number(p.at(key)), pl_number(thickness)));
}

void finish(PatternResult& result, const json& cut, const std::vector<Point>& contour, double u, double v) {
    if (cut_fits(cut, contour, u, v)) result.cuts.push_back(cut);
    else ++result.skipped;
    if (result.cuts.size() > MAX_PATTERN_CUTS)
        fail(std::format("Wzorek przekracza {} frezów — zwiększ podziałkę.", MAX_PATTERN_CUTS));
}

}  // namespace

// ------------------------------------------------------------------ kanty

const std::vector<EdgeStyleInfo>& edge_styles() {
    static const std::vector<EdgeStyleInfo> list = {
        {"prosta", "Prosta (ostra)", "Krawędź bez obróbki — zdejmuje zaokrąglenie lub fazkę.", false},
        {"zaokraglenie", "Zaokrąglona", "Zaokrąglenie krawędzi promieniem R (górnej i dolnej powierzchni płyty).", true},
        {"fazka", "Fazowana 45°", "Ścięcie krawędzi pod kątem 45° na odległość C.", true},
    };
    return list;
}

const EdgeStyleInfo& edge_style(const std::string& key) {
    for (const auto& s : edge_styles())
        if (s.id == key || s.name == key) return s;
    fail("Nieznany styl krawędzi: „" + key + "”.");
}

std::vector<Point> edge_section_thumbnail(const std::string& style_key, double width, double height, double margin) {
    const std::string style = edge_style(style_key).id;
    double x0 = margin, y0 = margin, x1 = width - margin, y1 = height - margin;
    if (x1 - x0 < 8 || y1 - y0 < 8) return {};
    double r = std::min(x1 - x0, y1 - y0) * 0.42;
    std::vector<Point> out = {{x0, y1}, {x1, y1}};
    if (style == "prosta") {
        out.push_back({x1, y0});
    } else if (style == "fazka") {
        out.push_back({x1, y0 + r});
        out.push_back({x1 - r, y0});
    } else {
        out.push_back({x1, y0 + r});
        for (int i = 1; i < 8; ++i) {
            double a = (PI / 2) * i / 8.0;
            out.push_back({x1 - r + r * std::cos(a), y0 + r - r * std::sin(a)});
        }
        out.push_back({x1 - r, y0});
    }
    out.push_back({x0, y0});
    return out;
}

std::vector<std::string> edge_ids_for_segment(size_t index) { return {std::format("top:{}", index), std::format("bottom:{}", index)}; }

std::vector<std::string> edge_ids_all(size_t count) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < count; ++i)
        for (auto& id : edge_ids_for_segment(i)) ids.push_back(id);
    return ids;
}

EdgeChange apply_edge_style(const json& fillets, const json& chamfers, const std::string& style_key, double size, const std::vector<std::string>& ids,
                            double thickness) {
    const EdgeStyleInfo& style = edge_style(style_key);
    if (ids.empty()) fail("Brak krawędzi do obróbki — wybierz bok konturu.");
    if (style.has_size) {
        if (!std::isfinite(size)) fail("Wartość obróbki krawędzi musi być liczbą.");
        double limit = thickness / 2 - 0.1;
        if (size < 0.1 || size > limit + 1e-9)
            fail(std::format("{} krawędzi: wartość musi mieścić się w zakresie 0,1 … {} mm dla płyty {} mm (górna i dolna krawędź nie mogą się stykać).",
                             style.id == "fazka" ? "Fazka" : "Zaokrąglenie", pl_number(std::max(0.1, limit)), pl_number(thickness)));
    }
    std::set<std::string> targets(ids.begin(), ids.end());
    EdgeChange result;
    for (const auto& item : fillets)
        if (!targets.contains(item.at("edge").get<std::string>())) result.fillets.push_back(item);
    for (const auto& item : chamfers)
        if (!targets.contains(item.at("edge").get<std::string>())) result.chamfers.push_back(item);
    for (const auto& id : ids) {
        if (style.id == "zaokraglenie") result.fillets.push_back({{"edge", id}, {"radius", size}});
        else if (style.id == "fazka") result.chamfers.push_back({{"edge", id}, {"distance", size}});
    }
    result.touched = targets.size();
    return result;
}

std::string describe_edge_style(const std::string& style_key, double size, size_t count) {
    const EdgeStyleInfo& style = edge_style(style_key);
    if (style.id == "prosta") return std::format("zdjęto obróbkę z {} krawędzi", count);
    return std::format("{} {}{} mm na {} krawędziach", style.id == "fazka" ? "fazka" : "zaokrąglenie", style.id == "fazka" ? "C" : "R", g(size), count);
}

// ------------------------------------------------------------------ katalog wzorków

const std::vector<PatternInfo>& pattern_catalog() {
    static const std::vector<PatternInfo> list = {
        {"ryfle", "Ryfle pionowe", "Równoległe, pionowe rowki jak w froncie ryflowanym.",
         {{"width", "Szerokość rowka [mm]", 8, 1, 100}, {"pitch", "Podziałka [mm]", 24, 5, 500}, {"depth", "Głębokość [mm]", 3, 0.5, 30},
          {"margin_u", "Margines po bokach [mm]", 30, 0, 500}, {"margin_v", "Margines dół / góra [mm]", 20, 0, 500}}},
        {"ramka", "Ramka frezowana", "Rowek w kształcie ramki wokół pola środkowego (front ramkowy).",
         {{"margin", "Odsunięcie od krawędzi [mm]", 40, 5, 300}, {"groove", "Szerokość rowka [mm]", 6, 2, 40}, {"depth", "Głębokość [mm]", 3, 0.5, 30}}},
        {"romby", "Romby (kratka skośna)", "Skrzyżowane rowki pod kątem 45° tworzące romby.",
         {{"pitch", "Podziałka [mm]", 60, 10, 500}, {"width", "Szerokość rowka [mm]", 4, 2, 30}, {"depth", "Głębokość [mm]", 2, 0.5, 30},
          {"margin", "Margines [mm]", 25, 2, 300}}},
        {"perforacja", "Perforacja", "Siatka okrągłych otworów lub dołków (panel perforowany).",
         {{"diameter", "Średnica [mm]", 8, 2, 60}, {"pitch_u", "Podziałka U [mm]", 32, 4, 500}, {"pitch_v", "Podziałka V [mm]", 32, 4, 500},
          {"margin", "Margines [mm]", 30, 0, 300}, {"through", "Przelotowe (0 = dołki, 1 = otwory)", 0, 0, 1}, {"depth", "Głębokość dołków [mm]", 3, 0.5, 30},
          {"stagger", "Przesunięte rzędy (0/1)", 0, 0, 1}}},
        {"szczeliny", "Szczeliny wentylacyjne", "Rzędy podłużnych szczelin (wentylacja, tylne ścianki, drzwi).",
         {{"slot_length", "Długość szczeliny [mm]", 60, 10, 600}, {"slot_width", "Szerokość szczeliny [mm]", 6, 2, 40}, {"pitch_u", "Podziałka U [mm]", 80, 12, 800},
          {"pitch_v", "Podziałka V [mm]", 16, 4, 300}, {"margin", "Margines [mm]", 30, 0, 300}, {"through", "Przelotowe (0 = rowki, 1 = szczeliny)", 1, 0, 1},
          {"depth", "Głębokość rowków [mm]", 3, 0.5, 30}, {"stagger", "Przesunięte rzędy (0/1)", 1, 0, 1}}},
        {"paski", "Paski poziome", "Poziome rowki na całej szerokości (front frezowany poziomo).",
         {{"width", "Szerokość rowka [mm]", 4, 1, 60}, {"pitch", "Podziałka [mm]", 40, 5, 500}, {"depth", "Głębokość [mm]", 2.5, 0.5, 30},
          {"margin_u", "Margines po bokach [mm]", 20, 0, 500}, {"margin_v", "Margines dół / góra [mm]", 40, 0, 500}}},
    };
    return list;
}

const PatternInfo& pattern_info(const std::string& key) {
    for (const auto& p : pattern_catalog())
        if (p.id == key || p.name == key) return p;
    fail("Nieznany wzorek: „" + key + "”.");
}

std::map<std::string, double> default_params(const std::string& id) {
    std::map<std::string, double> out;
    for (const auto& p : pattern_info(id).params) out[p.key] = p.def;
    return out;
}

// ------------------------------------------------------------------ generowanie

PatternResult generate_pattern(const std::string& id_key, const std::vector<Point>& contour, double u, double v, double t, const Params& given,
                               const std::string& face) {
    const PatternInfo& info = pattern_info(id_key);
    const std::string& id = info.id;
    if (face != "top" && face != "bottom") fail("Wzorek frezuje się na górnej (+T) albo dolnej (−T) powierzchni płyty.");
    if (!(u > 1 && v > 1 && t > 1)) fail("Wzorek wymaga płyty o wymiarach większych niż 1 mm.");
    Params p = resolve(info, given);
    PatternResult result;
    auto cut = [&](const std::string& type, const std::string& shape, double cu, double cv, double length, double width, double depth, double angle) {
        finish(result, make_cut(id, info.name, type, shape, face, cu, cv, length, width, type == "cutout" ? t : depth, angle), contour, u, v);
    };

    if (id == "ryfle") {
        need_depth(p, "depth", "Głębokość", t);
        double width = p["width"], pitch = p["pitch"];
        if (pitch < width + 0.5) fail("Podziałka musi być większa od szerokości rowka o co najmniej 0,5 mm.");
        double usable = u - 2 * p["margin_u"], height = v - 2 * p["margin_v"];
        if (usable < width || height < 0.5) fail("Za mały element dla marginesów ryflowania.");
        int count = (int)((usable - width) / pitch) + 1;
        double start = (u - (count - 1) * pitch) / 2;
        for (int k = 0; k < count; ++k) cut("groove", "rect", start + k * pitch, v / 2, width, height, p["depth"], 0);
    } else if (id == "ramka") {
        need_depth(p, "depth", "Głębokość", t);
        double m = p["margin"], gw = p["groove"];
        double inner_u = u - 2 * m - 2 * gw, inner_v = v - 2 * m - 2 * gw;
        if (inner_u < 5 || inner_v < 5) fail("Płyta jest za mała na ramkę o takim odsunięciu i szerokości rowka.");
        double span_u = u - 2 * m, span_v = v - 2 * m - 2 * gw;
        cut("groove", "rect", u / 2, m + gw / 2, span_u, gw, p["depth"], 0);
        cut("groove", "rect", u / 2, v - m - gw / 2, span_u, gw, p["depth"], 0);
        cut("groove", "rect", m + gw / 2, v / 2, gw, span_v, p["depth"], 0);
        cut("groove", "rect", u - m - gw / 2, v / 2, gw, span_v, p["depth"], 0);
    } else if (id == "romby") {
        need_depth(p, "depth", "Głębokość", t);
        double m = p["margin"], w = p["width"], pitch = p["pitch"];
        if (m < w) fail("Margines musi być nie mniejszy niż szerokość rowka.");
        if (u - 2 * m < 10 || v - 2 * m < 10) fail("Za mały element dla marginesu rombów.");
        const double s2 = std::sqrt(2.0);
        // rodzina A: v − u = c (kąt 45°), rodzina B: v + u = c (kąt −45°)
        double ca = v / 2 - u / 2, cb = v / 2 + u / 2;
        int kmax = (int)((u + v) / pitch) + 1;
        for (int k = -kmax; k <= kmax; ++k) {
            double c = ca + k * pitch;
            double t0 = std::max(m, m - c), t1 = std::min(u - m, v - m - c);
            if ((t1 - t0) * s2 > 2 * w) cut("groove", "rect", (t0 + t1) / 2, (t0 + t1) / 2 + c, (t1 - t0) * s2, w, p["depth"], 45);
            c = cb + k * pitch;
            t0 = std::max(m, c - (v - m));
            t1 = std::min(u - m, c - m);
            if ((t1 - t0) * s2 > 2 * w) cut("groove", "rect", (t0 + t1) / 2, c - (t0 + t1) / 2, (t1 - t0) * s2, w, p["depth"], -45);
        }
    } else if (id == "perforacja") {
        bool through = p["through"] >= 0.5;
        if (!through) need_depth(p, "depth", "Głębokość dołków", t);
        double d = p["diameter"], pu = p["pitch_u"], pv = p["pitch_v"], m = p["margin"];
        if (pu < d + 1 || pv < d + 1) fail("Podziałka musi być większa od średnicy otworu o co najmniej 1 mm.");
        double usable_u = u - 2 * m - d, usable_v = v - 2 * m - d;
        if (usable_u < 0 || usable_v < 0) fail("Za mały element dla marginesów perforacji.");
        int nu = (int)(usable_u / pu) + 1, nv = (int)(usable_v / pv) + 1;
        if ((size_t)nu * (size_t)nv > MAX_PATTERN_CUTS * 4) fail(std::format("Perforacja przekracza {} otworów — zwiększ podziałkę.", MAX_PATTERN_CUTS));
        double start_u = (u - (nu - 1) * pu) / 2, start_v = (v - (nv - 1) * pv) / 2;
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                double shift = p["stagger"] >= 0.5 && (j % 2 == 1) ? pu / 2 : 0;
                cut(through ? "cutout" : "pocket", "circle", start_u + i * pu + shift, start_v + j * pv, d, d, p["depth"], 0);
            }
    } else if (id == "szczeliny") {
        bool through = p["through"] >= 0.5;
        if (!through) need_depth(p, "depth", "Głębokość rowków", t);
        double sl = p["slot_length"], sw = p["slot_width"], pu = p["pitch_u"], pv = p["pitch_v"], m = p["margin"];
        if (sl < sw) fail("Długość szczeliny musi być co najmniej równa jej szerokości.");
        if (pu < sl + 2) fail("Podziałka U musi być większa od długości szczeliny o co najmniej 2 mm.");
        if (pv < sw + 2) fail("Podziałka V musi być większa od szerokości szczeliny o co najmniej 2 mm.");
        double usable_u = u - 2 * m - sl, usable_v = v - 2 * m - sw;
        if (usable_u < 0 || usable_v < 0) fail("Za mały element dla marginesów szczelin.");
        int nu = (int)(usable_u / pu) + 1, nv = (int)(usable_v / pv) + 1;
        if ((size_t)nu * (size_t)nv > MAX_PATTERN_CUTS * 4) fail(std::format("Wzorek przekracza {} szczelin — zwiększ podziałkę.", MAX_PATTERN_CUTS));
        double start_u = (u - (nu - 1) * pu) / 2, start_v = (v - (nv - 1) * pv) / 2;
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                double shift = p["stagger"] >= 0.5 && (j % 2 == 1) ? pu / 2 : 0;
                cut(through ? "cutout" : "groove", "slot", start_u + i * pu + shift, start_v + j * pv, sl, sw, p["depth"], 0);
            }
    } else if (id == "paski") {
        need_depth(p, "depth", "Głębokość", t);
        double w = p["width"], pitch = p["pitch"];
        if (pitch < w + 0.5) fail("Podziałka musi być większa od szerokości rowka o co najmniej 0,5 mm.");
        double length = u - 2 * p["margin_u"], usable = v - 2 * p["margin_v"];
        if (length < 1 || usable < w) fail("Za mały element dla marginesów pasków.");
        int count = (int)((usable - w) / pitch) + 1;
        double start = (v - (count - 1) * pitch) / 2;
        for (int k = 0; k < count; ++k) cut("groove", "rect", u / 2, start + k * pitch, length, w, p["depth"], 0);
    }
    if (result.cuts.empty())
        fail(std::format("Wzorek „{}” nie mieści się w konturze tego elementu (pominięto {} frezów) — zmniejsz marginesy lub podziałkę.", info.name, result.skipped));
    return result;
}

bool is_pattern_cut(const json& cut) {
    if (!cut.is_object() || !cut.contains("generated_decor") || !cut["generated_decor"].is_string()) return false;
    return cut["generated_decor"].get<std::string>().rfind("wzorek:", 0) == 0;
}

std::string pattern_of(const json& cut) {
    if (!is_pattern_cut(cut)) return "";
    return cut["generated_decor"].get<std::string>().substr(7);
}

size_t count_patterns(const json& machining, const std::string& id) {
    size_t n = 0;
    for (const auto& cut : machining)
        if (is_pattern_cut(cut) && (id.empty() || pattern_of(cut) == id)) ++n;
    return n;
}

json remove_patterns(const json& machining, const std::string& id) {
    json kept = json::array();
    for (const auto& cut : machining)
        if (!(is_pattern_cut(cut) && (id.empty() || pattern_of(cut) == id))) kept.push_back(cut);
    return kept;
}

json merge_pattern(const json& machining, const std::string& id, const std::vector<json>& cuts) {
    json result = remove_patterns(machining, pattern_info(id).id);
    for (const auto& cut : cuts) result.push_back(cut);
    return result;
}

std::vector<Point> cut_outline(const json& cut, int segments) {
    double cu = cut.at("u"), cv = cut.at("v"), length = cut.at("length"), width = cut.at("width");
    double angle = (cut.contains("angle") ? double(cut.at("angle")) : 0.0) * PI / 180;
    std::string shape = cut.at("shape");
    std::vector<Point> local;
    segments = std::max(segments, 8);
    if (shape == "circle") {
        for (int i = 0; i < segments; ++i) local.push_back({width / 2 * std::cos(2 * PI * i / segments), width / 2 * std::sin(2 * PI * i / segments)});
    } else if (shape == "slot") {
        double r = width / 2, half = std::max(0.0, length / 2 - r);
        int half_segments = std::max(4, segments / 2);
        for (int i = 0; i <= half_segments; ++i) {
            double a = -PI / 2 + PI * i / half_segments;
            local.push_back({half + r * std::cos(a), r * std::sin(a)});
        }
        for (int i = 0; i <= half_segments; ++i) {
            double a = PI / 2 + PI * i / half_segments;
            local.push_back({-half + r * std::cos(a), r * std::sin(a)});
        }
    } else {
        local = {{-length / 2, -width / 2}, {length / 2, -width / 2}, {length / 2, width / 2}, {-length / 2, width / 2}};
    }
    std::vector<Point> out;
    for (const auto& q : local) out.push_back({cu + q[0] * std::cos(angle) - q[1] * std::sin(angle), cv + q[0] * std::sin(angle) + q[1] * std::cos(angle)});
    return out;
}

std::vector<std::vector<Point>> pattern_thumbnail(const std::string& id, double width, double height, double margin) {
    const double su = 360, sv = 240, thickness = 18;
    PatternResult result = generate_pattern(id, {}, su, sv, thickness);
    double scale = std::min((width - 2 * margin) / su, (height - 2 * margin) / sv);
    if (!(scale > 0)) return {};
    double ox = (width - su * scale) / 2, oy = (height - sv * scale) / 2;
    auto map = [&](const std::vector<Point>& shape) {
        std::vector<Point> out;
        for (const auto& q : shape) out.push_back({ox + q[0] * scale, oy + (sv - q[1]) * scale});
        return out;
    };
    std::vector<std::vector<Point>> shapes;
    shapes.push_back(map(rect_points(su, sv)));
    for (const auto& cut : result.cuts) shapes.push_back(map(cut_outline(cut, 10)));
    return shapes;
}

// ------------------------------------------------------------------ pasujące elementy

std::vector<std::string> matching_parts(const core::Project& project, const core::Part& model, const std::function<std::string(const core::Part&)>& role_of) {
    std::vector<std::string> ids;
    const std::string role = role_of(model);
    if (role == "other") return ids;
    for (const auto& p : project.parts) {
        if (p.id == model.id || p.group != model.group) continue;
        if (role_of(p) != role) continue;
        if (std::abs(p.u - model.u) > 0.01 || std::abs(p.v - model.v) > 0.01 || std::abs(p.t - model.t) > 0.01) continue;
        if (p.contour != model.contour || p.profile != model.profile) continue;
        ids.push_back(p.id);
    }
    return ids;
}

}  // namespace sketch_design
