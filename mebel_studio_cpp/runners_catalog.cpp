// Sourced drawer-runner planning dimensions, in millimetres.
#include "runners_catalog.h"

#include <cmath>
#include <format>

namespace runners_catalog {

using py::ValueError;

const std::string CHECKED_ON = "2026-10-03";

namespace {

const std::string BLUM_PLAN = "https://publications.blum.com/2022/catalogue/de/419/";
const std::string BLUM_ASSEMBLY = "https://ea.blum.com/en/building-a-movento-drawer/";
const std::string HETTICH_FAQ = "https://www.hettich.com/en-ca/products/runner-systems/quadro";
const std::string QUADRO_5D = "https://shop.hettich.com/by_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-V6-5D/Full-extension-runner-Quadro-V6-5D-M-%2830-kg%29-with-Push-to-open%2C-500-mm%2C-right/p/9332688";
const std::string QUADRO_YOU = "https://shop.hettich.com/me_EN/Runner-systems/Concealed-drawer-runners/Quadro-drawer-runners/Quadro-25-YOU/Partial-extension-runner-Quadro-25-YOU-S-%2825-kg%29-with-Silent-System%2C-500-mm%2C-left-and-right/p/9333185";
const std::string QUADRO_YOU_RANGE = "https://shop.hettich.com/cr_EN/-/Quadro-25-YOU-with-Silent-System%2C-partial-extension-runner-for-wooden-drawer-%28slide-on-installation%2C-drawer-side-profile-thickness-%E2%89%A4-16-mm%29/bp/variant17776458291608";
const std::string KA5632 = "https://shop.hettich.com/pl_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-5632-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm%2C-blue-passivated%2C-galvanised/p/73281";
const std::string KA4532 = "https://shop.hettich.com/au_EN/Runner-systems/Ball-bearing-runners/Side-installation/KA-4532-Push-to-open-ball-bearing-runner%2C-side-installation%2C-dimensions-%28H-x-W%29-46-x-12-7-mm%2C-500-mm/p/9302510";
const std::string KA4532_RANGE = "https://shop.hettich.com/au_EN/-/KA-4532-full-extension-runner-with-Push-to-open%2C-load-class-35-kg%2C%2C-12-7-mm-installed-width/bp/variant9420787652550";
const std::string GRASS_PLAN = "https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/13/";
const std::string GRASS_RANGE = "https://mediacenter.grass.eu/Produktkataloge/Dynapro/EN/12/";
const std::string SALICE_PLAN = "https://www.salice.com/media/Catalog/runners-and-drawers/futura/Salice_FUTURA_cdn.pdf";
const std::string SALICE_A615 = "https://www.salice.com/downloads/4071/4122/Salice-Futura-Self-closing-A615-concealed-runner-CDN.pdf";
const std::string HAFELE_PLAN = "https://files.hafele.co.uk/CatalogFiles/www/18T10-262.PDF";
const std::string HETTICH_2026 = "https://www.hettich.com/fileadmin/Media_Center/Catalogue/Timber_Drawer_Systems_2026.pdf";

// range(start, stop, step) jako lista JSON
json range(long long start, long long stop, long long step = 1) {
    json out = json::array();
    for (long long i = start; i < stop; i += step) out.push_back(i);
    return out;
}

// {n: f(n) for n in range(...)}: klucze liczbowe zapisane tekstowo
template <class F>
json by_length(const json& lengths, F f) {
    json out = json::object();
    for (const auto& n : lengths) out[std::to_string(n.get<long long>())] = f(n.get<long long>());
    return out;
}

json strings(std::initializer_list<std::string> items) {
    json out = json::array();
    for (const auto& s : items) out.push_back(s);
    return out;
}

// _profile(label, manufacturer, family, mount, lengths, sources, **values)
json profile(const std::string& label, const std::string& manufacturer, const std::string& family, const std::string& mount,
             const json& lengths, const json& sources, const json& values) {
    json p = json::object();
    p["label"] = label; p["manufacturer"] = manufacturer; p["family"] = family; p["mount"] = mount;
    p["lengths"] = lengths; p["sources"] = sources; p["source"] = sources.at(0);
    p["status"] = "wymiary planistyczne producenta; model uproszczony";
    p["checked_on"] = CHECKED_ON; p["notes"] = json::array(); p["depth_extra"] = nullptr;
    p["depth_by_length"] = json::object(); p["depth_status"] = "unverified"; p["length_offset"] = 0;
    p["length_status"] = "verified"; p["max_side_thickness"] = nullptr;
    p["bottom_recess_min"] = 0; p["bottom_recess_max"] = nullptr;
    p["inner_width_offset"] = nullptr; p["external_clearance"] = nullptr;
    p["inner_width_tolerance"] = json::array({0, 0}); p["front_recess"] = nullptr;
    p["rear_recess"] = nullptr; p["cnc_patterns_available"] = false;
    p["extension"] = "full"; p["load_kg"] = nullptr; p["nominal_runner_thickness"] = nullptr;
    for (auto& [key, value] : values.items()) p[key] = value;  // p.update(values)
    return p;
}

json build() {
    json P = json::object();
    P["blum_movento_760h"] = profile(
        "Blum MOVENTO 760H BLUMOTION · 40 kg", "Blum", "MOVENTO 760H", "undermount",
        json::array({250, 270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 600}),
        strings({BLUM_PLAN, BLUM_ASSEMBLY}),
        json{{"depth_extra", 3}, {"depth_status", "verified"}, {"length_offset", -10},
             {"max_side_thickness", 16}, {"bottom_recess_min", 12}, {"bottom_recess_max", 15},
             {"inner_width_offset", 42}, {"inner_width_tolerance", json::array({-1.5, 0})}, {"load_kg", 40},
             {"notes", strings({"Profil dotyczy nakładanego frontu, standardowego montażu bocznego i sprzęgieł.",
                                "Stabilizacja boczna wymaga dodatkowych 12 mm głębokości; nie jest objęta tym profilem.",
                                "Szuflada wewnętrzna wymaga dodania grubości frontu do długości i miejsca w korpusie.",
                                "Wymagane osobne sprzęgła, tylne przygotowanie i otwory haka; nie są tutaj generowane."})}});
    P["hettich_quadro_v6_5d_eb21_push"] = profile(
        "Hettich Quadro V6 5D M EB21 · Push to open · 30 kg", "Hettich", "Quadro V6 5D M EB21", "undermount",
        json::array({250, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 580, 600}),
        strings({QUADRO_5D, HETTICH_FAQ, HETTICH_2026 + "#page=27"}),
        json{{"depth_extra", 3}, {"depth_status", "verified"}, {"length_offset", -10},
             {"max_side_thickness", 16}, {"bottom_recess_min", 12}, {"bottom_recess_max", 13},
             {"inner_width_offset", 42}, {"inner_width_tolerance", json::array({-1.5, 0})},
             {"front_recess", 75}, {"rear_recess", 50}, {"load_kg", 30},
             {"activation_gap_min", 4}, {"lowest_screw_height_min", 38},
             {"notes", strings({"Wyłącznie nowa metryczna generacja EB21; nie przenosić na starsze EB20 ani wersje calowe USA.",
                                "Do montażu wymagany osobno zamawiany komplet sprzęgieł 2D; wycięcia przednie 75 mm, tylne co najmniej 50 mm.",
                                "Push to open wymaga miejsca do aktywacji: 4 mm. Dla szerokich frontów dobrać synchronizację.",
                                "Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania prowadnicy."})}});
    P["hettich_quadro_25_you_eb21"] = profile(
        "Hettich Quadro 25 YOU EB21 · Silent System · 25 kg", "Hettich", "Quadro 25 YOU EB21", "undermount",
        json::array({270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550}),
        strings({QUADRO_YOU, QUADRO_YOU_RANGE, HETTICH_FAQ}),
        json{{"depth_extra", 3}, {"depth_status", "verified"}, {"length_offset", -10},
             {"max_side_thickness", 16}, {"bottom_recess_min", 12}, {"bottom_recess_max", 13},
             {"inner_width_offset", 42}, {"front_recess", 50}, {"rear_recess", 50}, {"load_kg", 25},
             {"extension", "partial"}, {"lowest_screw_height_min", 34},
             {"notes", strings({"Nowa platforma YOU EB21, montaż nasuwany ze sprzęgłami YOU; nie starsze Quadro EB20.",
                                "Wycięcia przednie 50 mm i tylne co najmniej 50 mm; sprzęgła zamawiane osobno.",
                                "Wysuw częściowy: długość prowadnicy nie jest automatycznie długością drogi wysuwu.",
                                "Nie wygenerowano wycięć pod sprzęgła, haka ani otworów mocowania."})}});
    {
        json lengths = range(250, 701, 50);
        P["hettich_ka5632"] = profile(
            "Hettich KA 5632 · boczna 12,7 mm · 45 kg", "Hettich", "KA 5632", "side", lengths, strings({KA5632}),
            json{{"depth_by_length", by_length(lengths, [](long long n) { return n + 4; })}, {"depth_status", "verified"},
                 {"external_clearance", 12.7}, {"nominal_runner_thickness", 12.7},
                 {"length_status", "workshop"},
                 {"runner_height", 46}, {"load_kg", 45},
                 {"article_by_length", json{{"250", "73276"}, {"300", "73277"}, {"350", "73278"}, {"400", "73279"}, {"450", "73280"},
                                            {"500", "73281"}, {"550", "73282"}, {"600", "73283"}, {"650", "73284"}, {"700", "41355"}}},
                 {"notes", strings({"Luz 12,7 mm na zewnątrz skrzynki po każdej stronie, a nie odsunięcie od jej wnętrza.",
                                    "Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 10 artykułów serii.",
                                    "Długość boku skrzynki przyjmujemy roboczo równą NL; karta artykułu podaje długość prowadnicy, nie normę długości drewnianej skrzynki. Nie wygenerowano otworów mocowania."})}});
    }
    {
        json lengths = range(300, 601, 50);
        P["hettich_ka4532_push"] = profile(
            "Hettich KA 4532 · Push to open · boczna 12,7 mm · 35 kg", "Hettich", "KA 4532 Push to open", "side", lengths, strings({KA4532, KA4532_RANGE}),
            json{{"depth_by_length", by_length(lengths, [](long long n) { return n + 4; })}, {"depth_status", "verified"},
                 {"external_clearance", 12.7}, {"nominal_runner_thickness", 12.7},
                 {"length_status", "workshop"},
                 {"runner_height", 46}, {"load_kg", 35},
                 {"article_by_length", json{{"300", "9302506"}, {"350", "9302507"}, {"400", "9302508"}, {"450", "9302509"},
                                            {"500", "9302510"}, {"550", "9302511"}, {"600", "9302512"}}},
                 {"travel_by_length", json{{"300", 293}}},
                 {"notes", strings({"Profil bieżącej serii 9302506–9302512; nie mieszamy otworów ze starszym KA4532 AM.",
                                    "Głębokość NL+4 mm sprawdzona w aktualnych kartach każdego z 7 artykułów serii.",
                                    "Producent podaje drogę wysuwu 293 mm dla wersji nominalnej 300 mm.",
                                    "Długość skrzynki = NL jest ustawieniem warsztatowym, a nie zweryfikowanym wymiarem drewnianej skrzynki producenta.",
                                    "Nie wygenerowano otworów mocowania ani automatycznego dystansu aktywatora."})}});
    }
    P["grass_dynapro_softclose_eb21"] = profile(
        "GRASS Dynapro Soft-close EB21 · 40 kg", "GRASS", "Dynapro Soft-close EB21", "undermount",
        json::array({250, 270, 300, 320, 350, 380, 400, 420, 450, 480, 500, 520, 550, 600}),
        strings({GRASS_PLAN, GRASS_RANGE}),
        json{{"depth_extra", 3}, {"depth_status", "verified"}, {"length_offset", -10},
             {"max_side_thickness", 16}, {"bottom_recess_min", 12},
             {"inner_width_offset", 42}, {"inner_width_tolerance", json::array({-1.5, 0})}, {"load_kg", 40},
             {"min_drawer_inner_width", 138}, {"length_tolerance", json::array({-0.5, 0.5})},
             {"notes", strings({"Nakładany front i montaż boczny; wersja z frontem wpuszczanym wymaga NL+21 zamiast NL+3.",
                                "Wymagane sprzęgła przednie i dopasowane adaptery 2D/3D/4D; minimalne wnętrze skrzynki 138 mm.",
                                "Tipmatic, Sensomatic, montaż do dna i wersje 70 kg mają osobne wymagania; nie są objęte profilem.",
                                "Nie wygenerowano otworów haka, sprzęgieł ani wzoru mocowania."})}});
    {
        json lengths = range(250, 601, 50);
        P["salice_futura_a615_clip_cdn"] = profile(
            "Salice FUTURA A615 · clip A710.610 · katalog CDN", "Salice", "FUTURA A615 Clip (CDN)", "undermount", lengths,
            strings({SALICE_A615, SALICE_PLAN}),
            json{{"depth_extra", 0}, {"depth_status", "verified"}, {"length_offset", -10},
                 {"max_side_thickness", 16}, {"bottom_recess_min", 12},
                 {"inner_width_offset", 42}, {"inner_width_tolerance", json::array({0, 1})}, {"load_kg", 34},
                 {"extension", "partial"}, {"four_sided_add_front_thickness", true},
                 {"article_by_length", by_length(lengths, [](long long n) { return "A615/" + std::to_string(n / 10); })},
                 {"notes", strings({"Wariant CDN A615, wysuw częściowy ze sprzęgłem A710.610. Nie stosować wymiarów do wersji pin-fix ani Futura 65500.",
                                    "Skrzynka trójboczna: długość NL−10. Skrzynka czteroboczna: NL−10 + grubość wewnętrznego czoła (subfrontu).",
                                    "Katalog określa X−42 jako szerokość WEWNĘTRZNĄ skrzynki, z tolerancją +1/0.",
                                    "Przy skrzynce czterobocznej dodatkowo sprawdzić miejsce na wewnętrzne czoło i jego położenie względem prowadnicy.",
                                    "Nie wygenerowano wycięć, otworów tylnego haka ani sprzęgła."})}});
    }
    P["hafele_43244_full_extension"] = profile(
        "Häfele 432.44 · kulkowa pełny wysuw · katalog UK 2018", "Häfele", "432.44 (UK 2018)", "side", range(250, 701, 50), strings({HAFELE_PLAN}),
        json{{"external_clearance", 12.9}, {"external_clearance_range", json::array({12.9, 13.2})},
             {"nominal_runner_thickness", 12.7}, {"runner_height", 44.4},
             {"length_status", "workshop"},
             {"article_by_length", json{{"250", "432.44.926"}, {"300", "432.44.931"}, {"350", "432.44.936"}, {"400", "432.44.941"},
                                        {"450", "432.44.946"}, {"500", "432.44.951"}, {"550", "432.44.956"}, {"600", "432.44.961"},
                                        {"650", "432.44.966"}, {"700", "432.44.971"}}},
             {"load_by_length", json{{"250", 41}, {"300", 42}, {"350", 43}, {"400", 44}, {"450", 45}, {"500", 45}, {"550", 45}, {"600", 45}, {"650", 44}, {"700", 43}}},
             {"status", "wymiary z archiwalnego katalogu UK 2018; dostępność do sprawdzenia"},
             {"notes", strings({"Grubość nominalna prowadnicy 12,7 mm. Producent zaleca miejsce większe o 0,2–0,5 mm: 12,9–13,2 mm na stronę.",
                                "Obliczenia przyjmują dolny limit 12,9 mm. Nie zastępuje go 12,7 mm nominalnej grubości.",
                                "Archiwalny katalog 2018: dostępność i obecne dane artykułu wymagają sprawdzenia przed zakupem.",
                                "Minimalna głębokość montażowa nie jest podana w użytym źródle; sprawdzamy tylko obrys długości prowadnicy.",
                                "Długość skrzynki = NL jest ustawieniem warsztatowym. Nie wygenerowano otworów mocowania."})}});

    // Separate load classes and opening variants: their article numbers are not
    // interchangeable.  Source is the 2026 catalogue, pages 8–11, not older Actro.
    struct Mode { std::string mode, title; int page; json articles40, articles70; };
    std::vector<Mode> modes{
        {"silent", "Silent System", 8,
         json{{"270", "9318269"}, {"300", "9318270"}, {"350", "9318282"}, {"400", "9318284"}, {"450", "9318286"}, {"500", "9318288"}, {"550", "9318290"}, {"600", "9318291"}},
         json{{"500", "9318311"}, {"550", "9318313"}, {"600", "9318315"}, {"650", "9318316"}, {"700", "9318317"}, {"750", "9318318"}}},
        {"push", "Push to open", 10,
         json{{"270", "9320613"}, {"300", "9320614"}, {"350", "9320616"}, {"400", "9320618"}, {"450", "9320620"}, {"500", "9320622"}, {"550", "9320624"}, {"600", "9320625"}},
         json{{"500", "9320627"}, {"550", "9320629"}, {"600", "9320631"}, {"650", "9320632"}, {"700", "9320633"}, {"750", "9320634"}}}};
    for (const auto& m : modes) {
        for (int load : {40, 70}) {
            const json& articles = load == 40 ? m.articles40 : m.articles70;
            std::string id = "hettich_actro5d_" + m.mode + "_" + std::to_string(load);
            json lengths = json::array(), sources = json::object();
            for (auto& [k, v] : articles.items()) { lengths.push_back(std::stoll(k)); sources[k] = HETTICH_2026 + "#page=" + std::to_string(m.page); }
            P[id] = profile(
                std::format("Hettich Actro 5D {} EB21 · {} kg · 2026", m.title, load), "Hettich",
                std::format("Actro 5D {} EB21 {} kg", m.title, load), "undermount", lengths, strings({HETTICH_2026 + "#page=" + std::to_string(m.page)}),
                json{{"depth_extra", 3}, {"depth_status", "verified"}, {"length_offset", -10},
                     {"max_side_thickness", 16}, {"bottom_recess_min", 12}, {"bottom_recess_max", 15},
                     {"inner_width_offset", 42}, {"inner_width_tolerance", json::array({-1.5, 0})},
                     {"front_recess", 75}, {"rear_recess", 50}, {"load_kg", load},
                     {"article_by_length", articles},
                     {"source_by_length", sources},
                     {"activation_gap_min", m.mode == "push" ? json(3) : json(nullptr)},
                     {"notes", strings({"Montaż na bokach korpusu, nasuwany, front nakładany; osobno zamawiane sprzęgła 9320333 (para).",
                                        "Szerokość wnętrza LB−42, tolerancja −1,5/+0. Długość skrzynki NL−10; głębokość korpusu NL+3.",
                                        "Bok maks. 16 mm; cofnięcie dna 12–15 mm; wycięcie z przodu min. 75, z tyłu min. 50 mm.",
                                        m.mode == "push" ? "Push to open wymaga min. 3 mm szczeliny." : "Push to open Silent jest osobnym zestawem, nie częścią tego profilu.",
                                        "Klasa nośności opisuje prowadnicę, nie wytrzymałość zaprojektowanego mebla. Nie generujemy zaczepów, wycięć ani wierceń haka."})}});
        }
    }

    // Each KA variant was checked in its own current manufacturer product page;
    // the archived AM catalogue is not used as evidence for these dimensions.
    for (auto& [key, url] : std::vector<std::pair<std::string, std::string>>{{"hettich_ka5632", KA5632}, {"hettich_ka4532_push", KA4532}}) {
        json& p = P[key];
        json sources = json::object();
        for (auto& [n, sku] : p["article_by_length"].items()) {
            std::string replaced = py::replace_all(url, "-500-mm", "-" + n + "-mm");
            sources[n] = replaced.substr(0, replaced.rfind("/p/")) + "/p/" + sku.get<std::string>();
        }
        p["source_by_length"] = sources;
    }
    return P;
}

// _positive(value, label)
double positive(const json& value, const std::string& label) {
    if (value.is_boolean()) throw ValueError(label + ": podaj liczbę w mm.");
    double v;
    try { v = py::to_float(value); }
    catch (const std::invalid_argument&) { throw ValueError(label + ": podaj liczbę w mm."); }
    catch (const nlohmann::json::exception&) { throw ValueError(label + ": podaj liczbę w mm."); }
    if (!std::isfinite(v) || v <= 0 || v > 10000) throw ValueError(label + ": wymiar musi być większy od 0 i nie większy niż 10000 mm.");
    return v;
}

// dict.get(key) dla słownika z kluczami liczbowymi; nl jest liczbą zmiennoprzecinkową
json by_nl(const json& dict, double nl) {
    if (!dict.is_object() || nl != std::floor(nl)) return nullptr;
    auto found = dict.find(std::to_string((long long)nl));
    return found == dict.end() ? json(nullptr) : *found;
}

std::string g(double v) { return std::format("{:g}", v); }

}  // namespace

const json PROFILES = build();
const json& RUNNER_PROFILES = PROFILES;

json get_profile(const json& profile_) {
    if (profile_.is_string()) {
        auto found = PROFILES.find(profile_.get<std::string>());
        if (found == PROFILES.end()) throw ValueError("Nieznany profil prowadnicy: " + profile_.get<std::string>());
        json result = *found;
        result["id"] = profile_;
        return result;
    }
    if (profile_.is_object()) {
        json result = profile_;
        if (!result.contains("id")) {
            std::string id = "custom";
            for (auto& [k, v] : PROFILES.items()) if (v == profile_) { id = k; break; }
            result["id"] = id;
        }
        return result;
    }
    throw ValueError("Wybierz profil prowadnicy z katalogu.");
}

json calculate_runner(const json& profile_, const json& cabinet_inside_width, const json& nominal_length, const json& side_thickness,
                      const json& cabinet_depth, const std::string& construction, const json& drawer_front_thickness, const json& confirmed_min_depth) {
    json p = get_profile(profile_);
    double width = positive(cabinet_inside_width, "Światło korpusu");
    double nl = positive(nominal_length, "Długość nominalna prowadnicy");
    double side = positive(side_thickness, "Grubość boku szuflady");
    double depth = positive(cabinet_depth, "Użyteczna głębokość korpusu");
    bool listed = false;
    for (const auto& length : p["lengths"]) if (length.get<double>() == nl) { listed = true; break; }
    if (!listed) throw ValueError(std::format("{}: brak zweryfikowanego wariantu {:g} mm w katalogu.", p["label"].get<std::string>(), nl));
    if (construction != "three_sided" && construction != "four_sided") throw ValueError("Konstrukcja skrzynki: three_sided albo four_sided.");
    std::vector<std::string> errors;
    json warnings = p["notes"];  // list(p["notes"])
    if (!p["max_side_thickness"].is_null() && side > p["max_side_thickness"].get<double>() + 1e-8)
        errors.push_back(std::format("Bok {} mm przekracza dopuszczalne {} mm.", g(side), g(p["max_side_thickness"].get<double>())));
    double inner, outer, clearance;
    std::array<double, 2> inner_range, outer_range, clearance_range;
    if (p["mount"] == "undermount") {
        inner = width - p["inner_width_offset"].get<double>();
        outer = inner + 2 * side;
        clearance = (width - outer) / 2;
        double tol_low = p["inner_width_tolerance"][0].get<double>(), tol_high = p["inner_width_tolerance"][1].get<double>();
        inner_range = {inner + tol_low, inner + tol_high};
        outer_range = {inner_range[0] + 2 * side, inner_range[1] + 2 * side};
        clearance_range = {(width - outer_range[1]) / 2, (width - outer_range[0]) / 2};
    } else {
        clearance = p["external_clearance"].get<double>();
        outer = width - 2 * clearance;
        inner = outer - 2 * side;
        json range = py::get(p, "external_clearance_range", json::array({p["external_clearance"], p["external_clearance"]}));
        clearance_range = {range[0].get<double>(), range[1].get<double>()};
        outer_range = {width - 2 * clearance_range[1], width - 2 * clearance_range[0]};
        inner_range = {outer_range[0] - 2 * side, outer_range[1] - 2 * side};
    }
    if (inner <= 0 || outer <= 0 || clearance < 0) errors.push_back("Brak miejsca na boki skrzynki i prowadnice przy podanych wymiarach.");
    if (py::truthy(py::get(p, "min_drawer_inner_width")) && inner < p["min_drawer_inner_width"].get<double>())
        errors.push_back(std::format("Wnętrze skrzynki musi mieć co najmniej {} mm.", g(p["min_drawer_inner_width"].get<double>())));
    double drawer_length = nl + p["length_offset"].get<double>();
    json front_t = nullptr;
    if (construction == "four_sided" && py::truthy(py::get(p, "four_sided_add_front_thickness"))) {
        if (drawer_front_thickness.is_null()) throw ValueError("Salice: podaj grubość wewnętrznego czoła skrzynki czterobocznej (subfrontu).");
        front_t = positive(drawer_front_thickness, "Grubość wewnętrznego czoła");
        drawer_length += front_t.get<double>();
    }
    json min_depth = by_nl(py::get(p, "depth_by_length", json::object()), nl);
    if (min_depth.is_null() && !py::get(p, "depth_extra").is_null()) min_depth = nl + p["depth_extra"].get<double>();
    json depth_status = p["depth_status"];
    if (min_depth.is_null() && !confirmed_min_depth.is_null()) {
        min_depth = positive(confirmed_min_depth, "Potwierdzona głębokość montażowa");
        if (min_depth.get<double>() < nl) throw ValueError("Potwierdzona głębokość montażowa nie może być mniejsza niż długość prowadnicy.");
        depth_status = "user_confirmed";
        warnings.push_back("Głębokość montażowa wpisana i potwierdzona przez użytkownika; brak tego wymiaru w dołączonym źródle.");
    }
    json fits_depth = nullptr;
    if (!min_depth.is_null()) fits_depth = depth + 1e-8 >= min_depth.get<double>();
    if (fits_depth == false) errors.push_back(std::format("Za mała głębokość korpusu: {} mm; wymagane co najmniej {} mm.", g(depth), g(min_depth.get<double>())));
    bool fits_envelope = depth + 1e-8 >= nl;
    if (!fits_envelope) errors.push_back(std::format("Prowadnica {} mm jest dłuższa niż użyteczna głębokość {} mm.", g(nl), g(depth)));
    bool fits_drawer_envelope = depth + 1e-8 >= drawer_length;
    if (!fits_drawer_envelope) errors.push_back(std::format("Cała skrzynka ({} mm) jest dłuższa niż użyteczna głębokość {} mm.", g(drawer_length), g(depth)));
    if (min_depth.is_null()) warnings.push_back("Głębokość montażowa tego wariantu niezweryfikowana. Dopasowanie obrysu nie potwierdza dopasowania montażowego.");
    if (construction == "four_sided" && py::truthy(py::get(p, "four_sided_add_front_thickness")))
        warnings.push_back("Głębokość PM z tabeli prowadnicy nie obejmuje automatycznie miejsca na wewnętrzne czoło; sprawdź układ skrzynki.");
    json article = by_nl(py::get(p, "article_by_length", json::object()), nl);
    json source_for_nl = by_nl(py::get(p, "source_by_length", json::object()), nl);
    json load = by_nl(py::get(p, "load_by_length", json::object()), nl);
    json errors_json = json::array();
    for (const auto& e : errors) errors_json.push_back(e);
    json result = json::object();
    result["profile_id"] = p["id"]; result["label"] = p["label"]; result["manufacturer"] = p["manufacturer"];
    result["family"] = p["family"]; result["mount"] = p["mount"]; result["nominal_length"] = nl;
    result["cabinet_inside_width"] = width; result["cabinet_depth"] = depth; result["side_thickness"] = side;
    result["drawer_inner_width"] = inner; result["drawer_outer_width"] = outer;
    result["external_clearance_per_side"] = clearance;
    result["drawer_inner_width_range"] = json::array({inner_range[0], inner_range[1]});
    result["drawer_outer_width_range"] = json::array({outer_range[0], outer_range[1]});
    result["external_clearance_range"] = json::array({clearance_range[0], clearance_range[1]});
    result["drawer_length"] = drawer_length;
    result["min_cabinet_depth"] = min_depth; result["fits_depth"] = fits_depth;
    result["fits_runner_envelope"] = fits_envelope; result["fits_drawer_envelope"] = fits_drawer_envelope;
    result["planning_complete"] = !min_depth.is_null() && p["length_status"] == "verified";
    result["valid"] = errors.empty(); result["errors"] = errors_json; result["warnings"] = warnings;
    result["bottom_recess_min"] = p["bottom_recess_min"]; result["bottom_recess_max"] = p["bottom_recess_max"];
    result["front_recess"] = p["front_recess"]; result["rear_recess"] = p["rear_recess"];
    result["max_side_thickness"] = p["max_side_thickness"]; result["sources"] = p["sources"];
    result["variant_source"] = source_for_nl.is_null() ? p["source"] : source_for_nl;
    result["status"] = p["status"]; result["depth_status"] = depth_status;
    result["length_status"] = p["length_status"]; result["confirmed_min_depth"] = confirmed_min_depth;
    result["checked_on"] = CHECKED_ON;
    result["cnc_patterns_available"] = false; result["article"] = article; result["construction"] = construction;
    result["drawer_front_thickness"] = front_t; result["extension"] = p["extension"];
    result["load_kg"] = load.is_null() ? p["load_kg"] : load;
    result["documented_travel"] = by_nl(py::get(p, "travel_by_length", json::object()), nl);
    return result;
}

json available_lengths(const json& profile_, const json& cabinet_depth) {
    json p = get_profile(profile_);
    double depth = positive(cabinet_depth, "Użyteczna głębokość korpusu");
    json result = json::array();
    for (const auto& length : p["lengths"]) {
        json minimum = by_nl(py::get(p, "depth_by_length", json::object()), length.get<double>());
        if (minimum.is_null() && !py::get(p, "depth_extra").is_null()) minimum = length.get<double>() + p["depth_extra"].get<double>();
        if (!minimum.is_null() && minimum.get<double>() <= depth + 1e-8) result.push_back(length);
    }
    return result;
}

}  // namespace runners_catalog
