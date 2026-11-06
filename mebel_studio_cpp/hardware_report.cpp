// Human-readable purchase list and installation planning dimensions.
#include "hardware_report.h"

#include <format>
#include <map>

namespace hardware_report {

using json = core::json;

namespace {

std::string esc(const json& x) { return py::html_escape(py::str(x)); }
std::string esc(const std::string& x) { return py::html_escape(x); }

// f'{val:g} mm' dla int/float/bool
bool is_numeric(const json& value) { return value.is_number() || value.is_boolean(); }

}  // namespace

void hardware_report(const core::Project& project, const std::string& path) {
    std::map<std::string, const core::Part*> owners;
    for (auto& p : project.parts) owners[p.id] = &p;
    auto owner_of = [&](const json& h) -> const core::Part* {
        json id = py::get(h, "owner_id");
        if (!id.is_string()) return nullptr;
        auto found = owners.find(id.get<std::string>());
        return found != owners.end() ? found->second : nullptr;
    };
    std::vector<std::string> out;
    out.push_back("<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>Okucia — plan montażu</title><style>body{font:15px Segoe UI,sans-serif;margin:32px;color:#243144}table{border-collapse:collapse;width:100%}td,th{border:1px solid #bbc;padding:8px;text-align:left}h2{margin-top:30px}@media print{a{color:black}}</style><h1>Okucia i wymiary montażowe</h1>");
    out.push_back("<p>Model produkcyjny w pozycji zamkniętej. Profil prowadnic zawiera wymiary planowania. Dobór zaczepów, rowków, mocowań i nośności jest opisany przy danej pozycji.</p><table><tr><th>Mebel / element</th><th>Okucie</th><th>Ilość</th><th>Informacja</th></tr>");
    for (const json& h : project.hardware) {
        const core::Part* p = owner_of(h);
        std::string owner = p ? p->group + " / " + p->name : "Projekt";
        std::string info = py::str(py::get(h, "note", py::get(h, "installation_status", "Wzorzec edytowalny; sprawdź wariant montażu.")));
        if (py::truthy(py::get(h, "compatibility_status"))) info += " " + py::str(h.at("compatibility_status"));
        out.push_back("<tr><td>" + esc(owner) + "</td><td>" + esc(h.at("name")) + "</td><td>" + esc(py::get(h, "qty", 1)) + "</td><td>" + esc(info) + "</td></tr>");
    }
    out.push_back("</table>");
    for (const json& h : project.hardware) {
        json plan = py::get(h, "dimensions");
        if (!py::truthy(plan)) continue;
        const core::Part* p = owner_of(h);
        out.push_back("<h2>" + esc((p ? p->name + " — " : std::string()) + py::str(h.at("name"))) + "</h2><table>");
        static const std::vector<std::pair<std::string, std::string>> rows{
            {"drawer_inner_width", "Szerokość wewnętrzna skrzynki"}, {"drawer_outer_width", "Szerokość zewnętrzna skrzynki"}, {"external_clearance_per_side", "Luz zewnętrzny na stronę"},
            {"drawer_length", "Długość skrzynki"}, {"min_cabinet_depth", "Minimalna głębokość korpusu"}, {"bottom_recess_min", "Minimalne cofnięcie dna"}};
        for (auto& [key, label] : rows) {
            json val = py::get(plan, key);
            std::string value = is_numeric(val) ? std::format("{:g} mm", py::to_float(val)) : "Nieustalone dla wariantu";
            out.push_back("<tr><th>" + label + "</th><td>" + esc(value) + "</td></tr>");
        }
        if (py::truthy(py::get(plan, "article"))) out.push_back("<tr><th>Artykuł</th><td>" + esc(plan.at("article")) + "</td></tr>");
        json range = py::get(plan, "drawer_inner_width_range", json::array({nullptr, nullptr}));
        json low = range.at(0), high = range.at(1);  // low, high = ... (rozpakowanie dwóch elementów)
        if (!low.is_null()) out.push_back(std::format("<tr><th>Dopuszczalne wnętrze skrzynki</th><td>{:g}–{:g} mm</td></tr>", py::to_float(low), py::to_float(high)));
        out.push_back("</table>");
        if (py::get(plan, "length_status") == "workshop")
            out.push_back("<p>Długość skrzynki = długość nominalna prowadnicy jest ustawieniem warsztatowym. Dokumentacja artykułu nie definiuje tu długości drewnianej skrzynki.</p>");
        if (py::get(plan, "depth_status") == "user_confirmed")
            out.push_back("<p>Minimalną głębokość montażową podał i potwierdził użytkownik; dołączona karta nie określa tego wymiaru.</p>");
        std::vector<std::string> warnings;
        for (auto& w : py::get(plan, "warnings", json::array())) warnings.push_back(w.get<std::string>());
        out.push_back("<p>" + esc(py::join(warnings, " · ")) + "</p>");
        // dict.fromkeys([variant_source] + sources): bez powtórzeń, w kolejności
        std::vector<json> sources{py::get(plan, "variant_source")};
        for (auto& s : py::get(plan, "sources", json::array())) sources.push_back(s);
        std::vector<json> unique;
        for (auto& s : sources) if (std::find(unique.begin(), unique.end(), s) == unique.end()) unique.push_back(s);
        for (auto& src : unique)
            if (py::truthy(src)) out.push_back("<p><a href=\"" + esc(src) + "\">Dokumentacja wariantu / producenta</a></p>");
    }
    out.push_back("</html>");
    py::write_text(path, py::join(out, ""));
}

}  // namespace hardware_report
