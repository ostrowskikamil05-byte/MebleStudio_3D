// Rozkrój płyt: optymalizator gilotynowy z wieloma wariantami, statystyki i eksport arkuszy. Patrz sheet_nesting.h.
#include "sheet_nesting.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>

#include "pyutil.h"

namespace sheet_nesting {

using core::Part;
using core::Project;

namespace {

constexpr double EPS = 1e-8;

Mode g_mode = Mode::Classic;

struct Item {
    const Part* part = nullptr;
    double a = 0, b = 0;  // wymiar do cięcia (U, V) po odjęciu obrzeży
    bool rotatable = false;
    bool swapped = false;  // włókna wzdłuż V: formatka zawsze leży obrócona (a, b są już zamienione)
};
struct Rect { double x = 0, y = 0, w = 0, h = 0; };  // w, h zawierają zapas na rzaz (jak w core::cut_plan)
struct Placed { size_t item = 0; double x = 0, y = 0, u = 0, v = 0; bool rot = false; };
struct Sheet {
    std::vector<Rect> free;
    std::vector<Placed> placed;
    double used = 0;
};
struct Group {
    std::string material;
    double thickness = 0;
    double sheet_u = 0, sheet_v = 0;
    double sw = 0, sh = 0;  // użyteczny obszar płyty po odjęciu marginesów
    double margin = 0, kerf = 0;
    std::vector<Item> items;
};

enum class Fit { Area, ShortSide, LongSide };
enum class Split { Vertical, Horizontal, ShorterAxis, LongerAxis, MaxArea };
enum class Order { Area, LongSide, ShortSide, Width, Height, Perimeter };

double order_key(const Item& it, Order order) {
    switch (order) {
        case Order::Area: return it.a * it.b;
        case Order::LongSide: return std::max(it.a, it.b);
        case Order::ShortSide: return std::min(it.a, it.b);
        case Order::Width: return it.a;
        case Order::Height: return it.b;
        case Order::Perimeter: return it.a + it.b;
    }
    return 0;
}

std::pair<double, double> fit_score(const Rect& r, double pa, double pb, Fit fit) {
    double lw = r.w - pa, lh = r.h - pb;
    switch (fit) {
        case Fit::Area: return {r.w * r.h - pa * pb, std::min(lw, lh)};
        case Fit::ShortSide: return {std::min(lw, lh), std::max(lw, lh)};
        case Fit::LongSide: return {std::max(lw, lh), std::min(lw, lh)};
    }
    return {0, 0};
}

// true = cięcie poziome najpierw (prawy pas tylko na wysokość formatki, dolny na całą szerokość)
bool horizontal_first(const Rect& r, double pa, double pb, Split split) {
    double rw = r.w - pa, bh = r.h - pb;
    switch (split) {
        case Split::Vertical: return false;
        case Split::Horizontal: return true;
        case Split::ShorterAxis: return rw < bh;
        case Split::LongerAxis: return !(rw < bh);
        case Split::MaxArea: {
            double vertical = std::max(rw * r.h, pa * bh), horizontal = std::max(rw * pb, r.w * bh);
            return horizontal > vertical;
        }
    }
    return false;
}

struct Metric {
    size_t sheets = 0;
    double min_used = 0;  // najmniej zapełniona płyta: im mniej, tym większy zwarty odpad do ponownego użycia
    size_t free_count = 0;
    bool better_than(const Metric& o) const {
        if (sheets != o.sheets) return sheets < o.sheets;
        if (std::abs(min_used - o.min_used) > 1e-6) return min_used < o.min_used;
        return free_count < o.free_count;
    }
};

Metric metric_of(const std::vector<Sheet>& sheets) {
    Metric m;
    m.sheets = sheets.size();
    m.min_used = sheets.empty() ? 0 : std::numeric_limits<double>::max();
    for (const auto& s : sheets) { m.min_used = std::min(m.min_used, s.used); m.free_count += s.free.size(); }
    return m;
}

std::vector<Sheet> pack(const Group& g, Order order, Fit fit, Split split) {
    std::vector<size_t> idx(g.items.size());
    std::iota(idx.begin(), idx.end(), size_t(0));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t l, size_t r) { return order_key(g.items[l], order) > order_key(g.items[r], order); });
    const double k = g.kerf;
    std::vector<Sheet> sheets;
    for (size_t ii : idx) {
        const Item& it = g.items[ii];
        struct Opt { double a, b; bool rot; };
        Opt opts[2] = {{it.a, it.b, it.swapped}, {it.b, it.a, !it.swapped}};
        int option_count = it.rotatable && std::abs(it.a - it.b) > 1e-9 ? 2 : 1;
        std::pair<double, double> best{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
        long si = -1, ri = -1; int oi = 0;
        for (size_t s = 0; s < sheets.size(); ++s) {
            for (size_t r = 0; r < sheets[s].free.size(); ++r) {
                const Rect& rect = sheets[s].free[r];
                for (int o = 0; o < option_count; ++o) {
                    double pa = opts[o].a + k, pb = opts[o].b + k;
                    if (pa > rect.w + EPS || pb > rect.h + EPS) continue;
                    auto score = fit_score(rect, pa, pb, fit);
                    if (score < best) { best = score; si = (long)s; ri = (long)r; oi = o; }
                }
            }
        }
        if (si < 0) {
            Rect fresh{g.margin, g.margin, g.sw + k, g.sh + k};
            for (int o = 0; o < option_count; ++o) {
                double pa = opts[o].a + k, pb = opts[o].b + k;
                if (pa > fresh.w + EPS || pb > fresh.h + EPS) continue;
                auto score = fit_score(fresh, pa, pb, fit);
                if (score < best) { best = score; oi = o; si = -2; }
            }
            if (si == -1) continue;  // nie powinno się zdarzyć: formatki, które się nie mieszczą, odsiano wcześniej
            Sheet s; s.free.push_back(fresh);
            sheets.push_back(std::move(s));
            si = (long)sheets.size() - 1; ri = 0;
        }
        Sheet& sheet = sheets[(size_t)si];
        Rect rect = sheet.free[(size_t)ri];
        sheet.free.erase(sheet.free.begin() + ri);
        double pa = opts[oi].a + k, pb = opts[oi].b + k;
        sheet.placed.push_back({ii, rect.x, rect.y, opts[oi].a, opts[oi].b, opts[oi].rot});
        sheet.used += opts[oi].a * opts[oi].b;
        double rw = rect.w - pa, bh = rect.h - pb;
        Rect right, bottom;
        if (horizontal_first(rect, pa, pb, split)) {
            right = {rect.x + pa, rect.y, rw, pb};
            bottom = {rect.x, rect.y + pb, rect.w, bh};
        } else {
            right = {rect.x + pa, rect.y, rw, rect.h};
            bottom = {rect.x, rect.y + pb, pa, bh};
        }
        if (right.w > k + EPS && right.h > k + EPS) sheet.free.push_back(right);
        if (bottom.w > k + EPS && bottom.h > k + EPS) sheet.free.push_back(bottom);
    }
    return sheets;
}

json sheet_json(const Group& g, const Sheet& s) {
    json out = {{"material", g.material}, {"thickness", g.thickness}, {"u", g.sheet_u}, {"v", g.sheet_v},
                {"free", json::array()}, {"pieces", json::array()}};
    for (const auto& r : s.free) out["free"].push_back(json::array({r.x, r.y, r.w, r.h}));
    for (const auto& p : s.placed)
        out["pieces"].push_back({{"id", g.items[p.item].part->id}, {"x", p.x}, {"y", p.y}, {"u", p.u}, {"v", p.v}, {"rotated", p.rot}});
    annotate(out, g.kerf);
    return out;
}

Metric metric_of_json(const json& sheets) {
    Metric m;
    m.sheets = sheets.size();
    m.min_used = sheets.empty() ? 0 : std::numeric_limits<double>::max();
    for (const auto& s : sheets) {
        double used = 0;
        for (const auto& p : s["pieces"]) used += p["u"].get<double>() * p["v"].get<double>();
        m.min_used = std::min(m.min_used, used);
        m.free_count += s["free"].size();
    }
    return m;
}

std::string cache_key(const Project& project) {
    std::string key = std::format("{}|{}|{}|{}|", project.kerf, project.margin, project.edge_t, project.parts.size());
    for (const auto& m : project.materials) key += std::format("{}:{}x{};", m.id, m.sheet_u, m.sheet_v);
    key += "|";
    for (const auto& p : project.parts)
        key += std::format("{}:{}:{}:{}:{}:{}{}{}{}:{};", p.id, p.u, p.v, p.t, p.material, (int)p.edges[0], (int)p.edges[1], (int)p.edges[2], (int)p.edges[3], (int)p.grain * 2 + (int)p.grain_v);
    return key;
}

std::string g(double v) { return std::format("{:g}", v); }
std::string esc(const std::string& s) { return py::html_escape(s); }

std::map<std::string, const Part*> parts_by_id(const Project& project) {
    std::map<std::string, const Part*> out;
    for (const auto& p : project.parts) out[p.id] = &p;
    return out;
}

}  // namespace

Mode mode() { return g_mode; }
void set_mode(Mode m) { g_mode = m; }
std::string mode_name(Mode m) { return m == Mode::Optimized ? "optymalny" : "klasyczny"; }

void annotate(json& sheet, double kerf) {
    double used = 0;
    for (const auto& p : sheet["pieces"]) used += p["u"].get<double>() * p["v"].get<double>();
    double area = sheet["u"].get<double>() * sheet["v"].get<double>();
    sheet["used_area"] = used;
    sheet["waste_percent"] = area > 0 ? (1 - used / area) * 100 : 0.0;
    json best = nullptr;
    double best_area = 0;
    for (const auto& r : sheet["free"]) {
        double w = r[2].get<double>() - kerf, h = r[3].get<double>() - kerf;
        if (w <= 0 || h <= 0) continue;
        if (w * h > best_area) { best_area = w * h; best = json::array({r[0].get<double>(), r[1].get<double>(), w, h}); }
    }
    sheet["offcut"] = best;
}

std::pair<json, std::vector<std::string>> optimize(Project& project) {
    static std::string cached_key;
    static std::pair<json, std::vector<std::string>> cached;
    std::string key = cache_key(project);
    if (key == cached_key) return cached;

    // Wynik klasyczny jest zawsze kandydatem — optymalizator nigdy nie daje gorszego planu.
    auto [classic, classic_unplaced] = core::cut_plan(project);  // sprawdza też poprawność projektu
    for (auto& s : classic) annotate(s, project.kerf);

    // Grupy: materiał + grubość, w kolejności pierwszego wystąpienia.
    std::vector<Group> groups;
    std::vector<std::string> unplaced;
    for (const auto& p : project.parts) {
        const auto& m = project.mat(p.material);
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& gr) { return gr.material == p.material && gr.thickness == p.t; });
        if (found == groups.end()) {
            Group gr;
            gr.material = p.material; gr.thickness = p.t; gr.sheet_u = m.sheet_u; gr.sheet_v = m.sheet_v;
            gr.margin = project.margin; gr.kerf = project.kerf;
            gr.sw = m.sheet_u - 2 * project.margin; gr.sh = m.sheet_v - 2 * project.margin;
            groups.push_back(std::move(gr));
            found = groups.end() - 1;
        }
        auto [a, b] = p.blank(project.edge_t);
        if (!p.fits_sheet(a, b, found->sw, found->sh, EPS)) { unplaced.push_back(p.id); continue; }
        if (p.must_turn()) found->items.push_back({&p, b, a, false, true});
        else found->items.push_back({&p, a, b, p.may_turn(), false});
    }

    json sheets = json::array();
    for (const auto& gr : groups) {
        if (gr.items.empty()) continue;
        const bool big = gr.items.size() > 600;
        std::vector<Order> orders = big ? std::vector<Order>{Order::Area, Order::LongSide}
                                        : std::vector<Order>{Order::Area, Order::LongSide, Order::ShortSide, Order::Width, Order::Height, Order::Perimeter};
        std::vector<Split> splits = big ? std::vector<Split>{Split::Vertical, Split::ShorterAxis}
                                        : std::vector<Split>{Split::Vertical, Split::Horizontal, Split::ShorterAxis, Split::LongerAxis, Split::MaxArea};
        std::vector<Sheet> best;
        Metric best_metric;
        bool have = false;
        for (Order o : orders) for (Fit f : {Fit::Area, Fit::ShortSide, Fit::LongSide}) for (Split s : splits) {
            auto result = pack(gr, o, f, s);
            Metric m = metric_of(result);
            if (!have || m.better_than(best_metric)) { best = std::move(result); best_metric = m; have = true; }
        }
        for (const auto& s : best) sheets.push_back(sheet_json(gr, s));
    }

    // Porównanie z klasykiem w obrębie całego planu.
    Metric mine = metric_of_json(sheets), old = metric_of_json(classic);
    std::pair<json, std::vector<std::string>> result;
    if (old.better_than(mine) || classic_unplaced.size() < unplaced.size()) result = {classic, classic_unplaced};
    else result = {sheets, unplaced};
    cached_key = key; cached = result;
    return result;
}

std::pair<json, std::vector<std::string>> plan(Project& project) {
    if (g_mode == Mode::Optimized) return optimize(project);
    auto result = core::cut_plan(project);
    for (auto& s : result.first) annotate(s, project.kerf);
    return result;
}

std::map<std::string, Placement> placements(const json& sheets) {
    std::map<std::string, Placement> out;
    if (!sheets.is_array()) return out;
    for (size_t i = 0; i < sheets.size(); ++i) {
        const auto& pieces = sheets[i]["pieces"];
        for (size_t n = 0; n < pieces.size(); ++n)
            out[pieces[n]["id"].get<std::string>()] = {(int)i + 1, (int)n + 1, pieces[n].value("rotated", false)};
    }
    return out;
}

json statistics(const Project& project, const json& sheets, const std::vector<std::string>& unplaced) {
    double total_area = 0, used = 0, stock_cost = 0;
    std::map<std::pair<std::string, double>, json> by_group;
    std::vector<std::string> group_order;
    json offcuts = json::array();
    for (size_t i = 0; i < sheets.size(); ++i) {
        const auto& s = sheets[i];
        double area = s["u"].get<double>() * s["v"].get<double>(), sheet_used = 0;
        for (const auto& p : s["pieces"]) sheet_used += p["u"].get<double>() * p["v"].get<double>();
        const auto& material = project.mat(s["material"].get<std::string>());
        Part blank; blank.u = s["u"]; blank.v = s["v"]; blank.t = s["thickness"];
        double cost = material.cost(blank);
        total_area += area; used += sheet_used; stock_cost += cost;
        auto key = std::make_pair(s["material"].get<std::string>(), s["thickness"].get<double>());
        auto found = by_group.find(key);
        if (found == by_group.end()) {
            found = by_group.emplace(key, json{{"material", material.name}, {"thickness", key.second}, {"sheet_u", s["u"]}, {"sheet_v", s["v"]},
                                               {"sheets", 0}, {"area", 0.0}, {"used", 0.0}, {"cost", 0.0}}).first;
            group_order.push_back(key.first + "|" + g(key.second));
        }
        auto& entry = found->second;
        entry["sheets"] = entry["sheets"].get<int>() + 1;
        entry["area"] = entry["area"].get<double>() + area;
        entry["used"] = entry["used"].get<double>() + sheet_used;
        entry["cost"] = entry["cost"].get<double>() + cost;
        if (s.contains("offcut") && s["offcut"].is_array()) {
            double w = s["offcut"][2].get<double>(), h = s["offcut"][3].get<double>();
            if (w >= 200 && h >= 200) offcuts.push_back({{"sheet", (int)i + 1}, {"material", material.name}, {"thickness", s["thickness"]}, {"x", s["offcut"][0]},
                                                         {"y", s["offcut"][1]}, {"width", w}, {"height", h}});
        }
    }
    json groups = json::array();
    for (auto& [key, entry] : by_group) {
        double area = entry["area"].get<double>();
        entry["utilization"] = area > 0 ? entry["used"].get<double>() / area * 100 : 0.0;
        groups.push_back(entry);
    }
    double edge = 0;
    for (const auto& p : project.parts) edge += p.edge_m();
    return {{"sheets", (long long)sheets.size()}, {"parts", (long long)project.parts.size()}, {"unplaced", (long long)unplaced.size()},
            {"sheet_area_m2", total_area / 1e6}, {"used_area_m2", used / 1e6}, {"utilization", total_area > 0 ? used / total_area * 100 : 0.0},
            {"waste", total_area > 0 ? (1 - used / total_area) * 100 : 0.0}, {"stock_cost", stock_cost}, {"edge_m", edge * 1.0},
            {"groups", groups}, {"offcuts", offcuts}, {"mode", mode_name(g_mode)}};
}

std::string summary_line(const Project& project, const json& sheets, const std::vector<std::string>& unplaced) {
    json st = statistics(project, sheets, unplaced);
    std::string line = std::format("Płyty: {} · wykorzystanie {:.1f}% · odpad {:.1f}% · płyty do zakupu {:.2f} zł · użyteczne odpady: {} · nie mieszczą się: {} · rzaz {:g} mm / margines {:g} mm · tryb {}",
                                   st["sheets"].get<long long>(), st["utilization"].get<double>(), st["waste"].get<double>(), st["stock_cost"].get<double>(),
                                   st["offcuts"].size(), unplaced.size(), project.kerf, project.margin, mode_name(g_mode));
    return line;
}

void sheets_export(Project& project, const std::string& path) {
    auto [sheets, unplaced] = plan(project);
    json st = statistics(project, sheets, unplaced);
    auto by_id = parts_by_id(project);
    std::string out =
        "<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>Rozkrój płyt — " + esc(project.name) + "</title><style>"
        "@page{size:A4 landscape;margin:10mm}body{font:12px Segoe UI,Arial;color:#1d2733;margin:16px}h1{font-size:20px;margin:0 0 4px}h2{font-size:15px;margin:14px 0 6px}"
        "table{border-collapse:collapse;width:100%;margin:6px 0}th,td{border:1px solid #b9c0c8;padding:3px 6px;text-align:left}th{background:#eef1f4}"
        ".sheet{break-inside:avoid;page-break-after:always;margin-bottom:12px}svg{width:100%;height:auto;max-height:150mm;border:1px solid #888;background:#fff}"
        ".kpi{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0}.kpi div{border:1px solid #c9d0d8;border-radius:6px;padding:6px 10px;background:#f7f9fb}.kpi b{display:block;font-size:16px}"
        "small{color:#5b6672}@media print{button{display:none}}</style><button onclick=\"print()\">Drukuj rozkrój</button>";
    out += "<h1>Rozkrój płyt — " + esc(project.name) + "</h1><small>Tryb: " + mode_name(g_mode) + " · rzaz " + g(project.kerf) + " mm · margines " + g(project.margin) +
           " mm · włókna wzdłuż dłuższego boku płyty (U), formatki z włóknami nie są obracane.</small>";
    out += std::format("<div class=\"kpi\"><div><small>Płyty</small><b>{}</b></div><div><small>Wykorzystanie</small><b>{:.1f}%</b></div><div><small>Odpad</small><b>{:.1f}%</b></div>"
                       "<div><small>Płyty do zakupu</small><b>{:.2f} zł</b></div><div><small>Obrzeże</small><b>{:.1f} m</b></div><div><small>Nie mieszczą się</small><b>{}</b></div></div>",
                       st["sheets"].get<long long>(), st["utilization"].get<double>(), st["waste"].get<double>(), st["stock_cost"].get<double>(), st["edge_m"].get<double>(), unplaced.size());
    out += "<table><tr><th>Materiał</th><th>Grubość</th><th>Płyta</th><th>Sztuk</th><th>Wykorzystanie</th><th>Koszt płyt</th></tr>";
    for (const auto& gr : st["groups"])
        out += std::format("<tr><td>{}</td><td>{} mm</td><td>{} × {}</td><td>{}</td><td>{:.1f}%</td><td>{:.2f} zł</td></tr>", esc(gr["material"].get<std::string>()), g(gr["thickness"].get<double>()),
                           g(gr["sheet_u"].get<double>()), g(gr["sheet_v"].get<double>()), gr["sheets"].get<int>(), gr["utilization"].get<double>(), gr["cost"].get<double>());
    out += "</table>";
    if (!st["offcuts"].empty()) {
        out += "<h2>Użyteczne odpady (min. 200 × 200 mm)</h2><table><tr><th>Płyta</th><th>Materiał</th><th>Wymiar odpadu</th></tr>";
        for (const auto& o : st["offcuts"])
            out += std::format("<tr><td>{}</td><td>{} {} mm</td><td>{} × {} mm</td></tr>", o["sheet"].get<int>(), esc(o["material"].get<std::string>()), g(o["thickness"].get<double>()),
                               g(std::floor(o["width"].get<double>())), g(std::floor(o["height"].get<double>())));
        out += "</table>";
    }
    out += "<div style=\"page-break-after:always\"></div>";
    for (size_t i = 0; i < sheets.size(); ++i) {
        const auto& s = sheets[i];
        double su = s["u"].get<double>(), sv = s["v"].get<double>();
        const auto& material = project.mat(s["material"].get<std::string>());
        double font = std::max(su, sv) / 70;
        out += std::format("<section class=\"sheet\"><h2>Płyta {} z {} — {}, {} mm · {} × {} mm · wykorzystanie {:.1f}%, odpad {:.1f}%</h2>", i + 1, sheets.size(), esc(material.name),
                           g(s["thickness"].get<double>()), g(su), g(sv), 100 - s["waste_percent"].get<double>(), s["waste_percent"].get<double>());
        out += std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\"><defs><pattern id=\"h{}\" width=\"24\" height=\"24\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\">"
                           "<line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"24\" stroke=\"#cfd5db\" stroke-width=\"5\"/></pattern></defs><rect width=\"{}\" height=\"{}\" fill=\"#f4f1ea\" stroke=\"#333\" stroke-width=\"{}\"/>",
                           g(su), g(sv), i, g(su), g(sv), g(font / 6));
        double kerf = project.kerf;
        for (const auto& r : s["free"]) {
            double w = r[2].get<double>() - kerf, h = r[3].get<double>() - kerf;
            if (w <= 0 || h <= 0) continue;
            out += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"url(#h{})\" stroke=\"#9aa4ae\" stroke-dasharray=\"{} {}\" stroke-width=\"{}\"/>", g(r[0].get<double>()), g(r[1].get<double>()),
                               g(w), g(h), i, g(font), g(font / 2), g(font / 8));
            if (w >= 200 && h >= 120)
                out += std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\" fill=\"#5b6672\" text-anchor=\"middle\">odpad {} × {}</text>", g(r[0].get<double>() + w / 2), g(r[1].get<double>() + h / 2 + font / 3),
                                   g(font * .8), g(std::floor(w)), g(std::floor(h)));
        }
        std::string table = "<table><tr><th>Nr</th><th>Formatka</th><th>Grupa</th><th>Cięcie U × V [mm]</th><th>Gotowe U × V × T [mm]</th><th>Obrzeża U0/U1/V0/V1</th><th>Włókna</th><th>Pozycja x, y [mm]</th><th>ID</th></tr>";
        for (size_t n = 0; n < s["pieces"].size(); ++n) {
            const auto& r = s["pieces"][n];
            auto found = by_id.find(r["id"].get<std::string>());
            if (found == by_id.end()) continue;
            const Part& p = *found->second;
            double x = r["x"].get<double>(), y = r["y"].get<double>(), u = r["u"].get<double>(), v = r["v"].get<double>();
            bool rotated = r.value("rotated", false);
            out += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"{}\" fill-opacity=\".78\" stroke=\"#333\" stroke-width=\"{}\"/>", g(x), g(y), g(u), g(v), esc(project.mat(p.material).color), g(font / 10));
            double label = std::min(font * 1.5, std::min(u, v) / 2.2);
            out += std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\" font-weight=\"bold\" fill=\"#111\" text-anchor=\"middle\">{}</text>", g(x + u / 2), g(y + v / 2 + label * .15), g(label), n + 1);
            if (v > label * 3.2 && u > label * 6)
                out += std::format("<text x=\"{}\" y=\"{}\" font-size=\"{}\" fill=\"#222\" text-anchor=\"middle\">{} × {}{}</text>", g(x + u / 2), g(y + v / 2 + label * 1.3), g(label * .6), g(u), g(v), rotated ? " ⟳" : "");
            if (p.grain && u > label * 5)  // strzałka kierunku włókien
                out += std::format("<g stroke=\"#5a4630\" stroke-width=\"{}\" fill=\"#5a4630\"><line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\"/><polygon points=\"{},{} {},{} {},{}\" stroke=\"none\"/></g>",
                                   g(font / 14), g(x + u * .12), g(y + v - label * .7), g(x + u * .12 + label * 2.2), g(y + v - label * .7), g(x + u * .12 + label * 2.5), g(y + v - label * .7),
                                   g(x + u * .12 + label * 2.0), g(y + v - label * .95), g(x + u * .12 + label * 2.0), g(y + v - label * .45));
            std::string edges;
            for (int e = 0; e < 4; ++e) edges += (e ? " / " : "") + std::string(p.edges[(size_t)e] ? "tak" : "—");
            table += std::format("<tr><td>{}</td><td>{}</td><td>{}</td><td>{} × {}</td><td>{} × {} × {}</td><td>{}</td><td>{}</td><td>{}, {}</td><td>{}</td></tr>", n + 1, esc(p.name), esc(p.group), g(u), g(v), g(p.u), g(p.v), g(p.t),
                                 edges, p.grain ? (p.grain_v ? "wzdłuż V" : "wzdłuż U") : "dowolnie", g(std::round(x * 10) / 10), g(std::round(y * 10) / 10), esc(p.id));
        }
        out += "</svg>" + table + "</table></section>";
    }
    if (!unplaced.empty()) {
        out += "<h2>Nie mieszczą się na płycie</h2><table><tr><th>Formatka</th><th>Cięcie U × V</th><th>Płyta</th></tr>";
        for (const auto& id : unplaced) {
            auto found = by_id.find(id);
            if (found == by_id.end()) continue;
            auto [a, b] = found->second->blank(project.edge_t);
            const auto& m = project.mat(found->second->material);
            out += std::format("<tr><td>{}</td><td>{} × {}</td><td>{} × {}</td></tr>", esc(found->second->name), g(a), g(b), g(m.sheet_u), g(m.sheet_v));
        }
        out += "</table>";
    }
    out += "</html>";
    py::write_text(path, out);
}

void sheets_dxf_export(Project& project, const std::string& path) {
    auto [sheets, unplaced] = plan(project);
    std::vector<std::string> lines{"0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", "AC1015", "9", "$INSUNITS", "70", "4", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"};
    auto pair = [&](int code, const std::string& value) { lines.push_back(std::to_string(code)); lines.push_back(value); };
    auto number = [](double v) { return std::format("{}", v); };
    auto rectangle = [&](const std::string& layer, double x, double y, double w, double h) {
        pair(0, "LWPOLYLINE"); pair(8, layer); pair(90, "4"); pair(70, "1");
        for (auto [px, py_] : std::vector<std::pair<double, double>>{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}) { pair(10, number(px)); pair(20, number(py_)); }
    };
    double offset = 0;
    for (size_t i = 0; i < sheets.size(); ++i) {
        const auto& s = sheets[i];
        double su = s["u"].get<double>(), sv = s["v"].get<double>();
        rectangle("SHEET_" + std::to_string(i + 1), offset, 0, su, sv);
        for (size_t n = 0; n < s["pieces"].size(); ++n) {
            const auto& r = s["pieces"][n];
            double u = r["u"].get<double>(), v = r["v"].get<double>(), x = offset + r["x"].get<double>(), y = sv - r["y"].get<double>() - v;
            rectangle("PARTS", x, y, u, v);
            pair(0, "TEXT"); pair(8, "LABELS"); pair(10, number(x + u / 2)); pair(20, number(y + v / 2)); pair(30, "0");
            pair(40, number(std::clamp(std::min(u, v) / 4, 10.0, 40.0))); pair(1, std::to_string(i + 1) + "." + std::to_string(n + 1));
        }
        offset += su + 200;
    }
    for (std::string s : {"0", "ENDSEC", "0", "EOF"}) lines.push_back(s);
    py::write_text(path, py::join(lines, "\n") + "\n");
}

}  // namespace sheet_nesting
