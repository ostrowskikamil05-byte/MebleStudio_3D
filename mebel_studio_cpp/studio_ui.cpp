#include "studio_ui.h"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>

#include "app.h"
#include "assembly.h"
#include "dimension_model.h"
#include "features.h"
#include "fittings.h"
#include "furniture_library.h"
#include "hardware_ui.h"
#include "mechanisms.h"
#include "palette.h"
#include "pyutil.h"
#include "responsive_ui.h"

namespace studio_ui {

using core::ValueError;
using Cmd = std::function<void()>;

namespace {

// float(text.replace(',', '.'))
double comma_float(const std::string& text) { return py::to_float(py::replace_all(text, ",", ".")); }

// str.casefold() dla UTF-8: litery łacińskie, w tym polskie znaki diakrytyczne.
std::string casefold(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); ++i; continue; }
        size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        char32_t cp = length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
        for (size_t k = 1; k < length && i + k < text.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += length;
        if (cp == 0xDF) { out += "ss"; continue; }
        if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) cp += 32;
        else if (cp >= 0x100 && cp <= 0x137) { if (cp % 2 == 0) cp += 1; }
        else if (cp >= 0x139 && cp <= 0x148) { if (cp % 2 == 1) cp += 1; }
        else if (cp >= 0x14A && cp <= 0x177) { if (cp % 2 == 0) cp += 1; }
        else if (cp >= 0x179 && cp <= 0x17E) { if (cp % 2 == 1) cp += 1; }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// Indeks listy Pythona (także ujemny); IndexError poza zakresem.
template <class T>
const T& at_py(const std::vector<T>& items, long long index) {
    if (index < 0) index += static_cast<long long>(items.size());
    if (index < 0 || index >= static_cast<long long>(items.size())) throw std::out_of_range("list index out of range");
    return items[static_cast<size_t>(index)];
}

const json& recipe_named(const std::string& name) {
    for (const auto& r : fittings::RECIPES)
        if (r.at("name") == json(name)) return r;
    throw py::StopIteration();
}

json num_or(const json& value, const json& fallback) { return py::truthy(value) ? value : fallback; }

}  // namespace

void commit(App* app, const core::ProjectPtr& project) {
    app->snapshot();
    app->project = project;
    app->refresh();
    app->fit();
}

core::Part& selected(App* app) {
    core::Part* p = app->selected_part();
    if (!p) throw ValueError("Wybierz element w drzewie projektu.");
    return *p;
}

std::function<void()> safe(App* app, std::function<void()> fn) {
    return [app, fn = std::move(fn)] {
        try {
            fn();
        } catch (const std::invalid_argument& e) {  // ValueError
            tk::messagebox::showerror("Sprawdź ustawienia", e.what(), app);
        } catch (const std::system_error& e) {      // OSError
            tk::messagebox::showerror("Sprawdź ustawienia", e.what(), app);
        } catch (const std::out_of_range& e) {      // KeyError
            tk::messagebox::showerror("Sprawdź ustawienia", e.what(), app);
        } catch (const py::StopIteration& e) {
            tk::messagebox::showerror("Sprawdź ustawienia", e.what(), app);
        }
    };
}

form_dialog::Form* library_dialog(App* app) {
    auto* d = app->dialog("Biblioteka mebli — wymiary i wyposażenie", "900x760");
    auto* top = new ttk::Frame(d, {{"padding", 12}});
    top->pack({{"fill", "x"}});
    tk::StringVar cat("Wszystkie"), search;
    std::vector<std::string> categories = {"Wszystkie"};
    for (const auto& t : furniture_library::TEMPLATES)
        if (std::find(categories.begin(), categories.end(), t.category) == categories.end()) categories.push_back(t.category);
    (new ttk::Combobox(top, {{"textvariable", cat}, {"values", categories}, {"state", "readonly"}, {"width", 25}}))->pack({{"side", "left"}});
    (new ttk::Entry(top, {{"textvariable", search}, {"width", 35}}))->pack({{"side", "left"}, {"padx", 12}});
    (new ttk::Label(top, {{"text", "Szukaj nazwy mebla"}}))->pack({{"side", "left"}});
    auto* content = new ttk::Frame(d);
    content->pack({{"fill", "both"}, {"expand", true}, {"padx", 12}});
    auto* table = new ttk::Treeview(content, {{"columns", std::vector<std::string>{"category", "name"}}, {"show", "headings"}, {"height", 16}});
    table->heading("category", {{"text", "Pomieszczenie"}});
    table->heading("name", {{"text", "Mebel"}});
    table->column("category", {{"width", 170}});
    table->column("name", {{"width", 265}});
    table->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* sb = new ttk::Scrollbar(content, {{"orient", "vertical"}, {"command", table->path() + " yview"}});
    sb->pack({{"side", "left"}, {"fill", "y"}});
    table->configure({{"yscrollcommand", sb->path() + " set"}});
    auto* form = new ttk::Frame(content, {{"padding", 10}});
    form->pack({{"side", "right"}, {"fill", "y"}});
    auto vars = std::make_shared<std::map<std::string, tk::StringVar>>();
    struct Spec { const char* key; const char* label; json value; };
    for (const Spec& spec : std::vector<Spec>{{"name", "Nazwa / grupa", "Nowy mebel"}, {"w", "Szerokość X [mm]", 600}, {"h", "Wysokość Z [mm]", 720},
                                              {"d", "Głębokość Y [mm]", 560}, {"t", "Grubość materiału [mm]", 18}, {"x", "Pozycja X [mm]", 0},
                                              {"y", "Pozycja Y [mm]", 0}, {"z", "Pozycja Z [mm]", 0}})
        vars->insert_or_assign(spec.key, app->field(form, spec.label, spec.value));
    std::vector<std::string> material_names;
    for (const auto& m : app->project->materials) material_names.push_back(m.name);
    (new ttk::Label(form, {{"text", "Materiał konstrukcji"}}))->pack({{"anchor", "w"}});
    auto* mat = new ttk::Combobox(form, {{"values", material_names}, {"state", "readonly"}});
    mat->pack({{"fill", "x"}});
    mat->current(0);
    (new ttk::Label(form, {{"text", "Materiał frontów"}}))->pack({{"anchor", "w"}});
    auto* front = new ttk::Combobox(form, {{"values", material_names}, {"state", "readonly"}});
    front->pack({{"fill", "x"}});
    front->current(static_cast<int>(std::min<size_t>(1, app->project->materials.size() - 1)));
    tk::StringVar note;
    (new ttk::Label(d, {{"textvariable", note}, {"wraplength", 840}, {"padding", 12}}))->pack({{"fill", "x"}});

    auto choose = [table, vars, note]() mutable {
        if (table->selection().empty()) return;
        long long index = py::to_int(table->selection()[0]);
        const auto& t = at_py(furniture_library::TEMPLATES, index);
        vars->at("name").set(t.name);
        vars->at("w").set(std::to_string(t.w));
        vars->at("h").set(std::to_string(t.h));
        vars->at("d").set(std::to_string(t.d));
        note.set(t.category + " · " + std::to_string(t.shelves) + " półek · " + std::to_string(t.fronts) +
                 " frontów. Zawiasy i uchwyty generują się dla drzwi. Cargo i klapy mają modele koncepcyjne mechanizmów. Wymiary i materiały są edytowalne.");
    };
    auto refresh = [table, cat, search, choose]() mutable {
        table->delete_(table->get_children());
        const auto& templates = furniture_library::TEMPLATES;
        for (size_t i = 0; i < templates.size(); ++i) {
            const auto& t = templates[i];
            if ((cat.get() == "Wszystkie" || cat.get() == t.category) && casefold(t.name).find(casefold(search.get())) != std::string::npos)
                table->insert("", "end", {{"iid", std::to_string(i)}, {"values", std::vector<std::string>{t.category, t.name}}});
        }
        if (!table->get_children().empty()) {
            table->selection_set(table->get_children()[0]);
            choose();
        }
    };
    table->bind("<<TreeviewSelect>>", [choose](tk::Event&) mutable { choose(); return std::string(); });
    cat.trace_add("write", Cmd(refresh));
    search.trace_add("write", Cmd(refresh));
    auto add = [app, d, table, vars, mat, front] {
        d->ensure_current_project(true);
        if (table->selection().empty()) throw ValueError("Wybierz mebel.");
        std::map<std::string, std::string> values;
        for (auto& [k, v] : *vars) values[k] = v.get();
        auto result = furniture_library::create_furniture(
            *app->project, py::to_int(table->selection()[0]), comma_float(values["w"]), comma_float(values["h"]), comma_float(values["d"]),
            comma_float(values["t"]), core::Point3{comma_float(values["x"]), comma_float(values["y"]), comma_float(values["z"])}, values["name"],
            at_py(app->project->materials, mat->current()).id, at_py(app->project->materials, front->current()).id);
        commit(app, result.first);
        const std::string& group = result.second;
        auto chosen = std::find_if(app->project->parts.begin(), app->project->parts.end(), [&](const core::Part& p) { return p.group == group; });
        if (chosen == app->project->parts.end()) throw py::StopIteration();
        app->selected = chosen->id;
        app->refresh();
        d->destroy();
        app->workspace->select(app->design_page);
    };
    (new ttk::Button(d, {{"text", "Utwórz mebel z wyposażeniem"}, {"command", safe(app, add)}, {"style", "Accent.TButton"}}))
        ->pack({{"fill", "x"}, {"padx", 12}, {"pady", 12}});
    refresh();
    return d;
}

form_dialog::Form* palette_dialog(App* app) {
    core::Part p = selected(app);
    auto* d = app->dialog("Paleta kolorów i dekorów", "850x680");
    tk::StringVar chosen(palette::PALETTE[0].first), scope("Wybrany element");
    auto* frame = new ttk::Frame(d, {{"padding", 15}});
    frame->pack({{"fill", "both"}, {"expand", true}});
    for (size_t i = 0; i < palette::PALETTE.size(); ++i) {
        const auto& [name, color] = palette::PALETTE[i];
        long long sum = 0;
        for (size_t k : {1, 3, 5}) sum += py::int16(color.substr(k, 2));
        auto* button = new tk::Button(frame, {{"text", name}, {"bg", color}, {"fg", sum < 330 ? "#fff" : "#18212b"}, {"width", 16}, {"height", 2},
                                              {"command", Cmd([chosen, n = name]() mutable { chosen.set(n); })}});
        button->grid({{"row", static_cast<int>(i / 6)}, {"column", static_cast<int>(i % 6)}, {"padx", 3}, {"pady", 3}, {"sticky", "nsew"}});
    }
    for (int col = 0; col < 6; ++col) frame->columnconfigure(col, {{"weight", 1}});
    (new ttk::Label(d, {{"textvariable", chosen}, {"padding", 10}, {"font", "{Segoe UI} 12 bold"}}))->pack();
    (new ttk::Combobox(d, {{"textvariable", scope}, {"values", std::vector<std::string>{"Wybrany element", "Cały mebel", "Wszystkie fronty mebla"}}, {"state", "readonly"}}))
        ->pack({{"fill", "x"}, {"padx", 15}});
    auto apply = [app, d, p, chosen, scope] {
        d->ensure_current_project(true);
        auto result = std::make_shared<core::Project>(*app->project);  // copy.deepcopy
        palette::install_palette(result);
        auto material = std::find_if(result->materials.begin(), result->materials.end(), [&](const core::Material& m) { return m.name == "Dekor — " + chosen.get(); });
        if (material == result->materials.end()) throw py::StopIteration();
        std::string material_id = material->id;
        for (auto& q : result->parts) {
            bool applies;
            if (scope.get() == "Wybrany element") applies = q.id == p.id;
            else {
                std::string role = q.group == p.group ? assembly::inferred_role(q) : "";
                applies = q.group == p.group && (scope.get() == "Cały mebel" || role == "door" || role == "drawer");
            }
            if (applies) q.material = material_id;
        }
        result->validate();
        commit(app, result);
    };
    (new ttk::Label(d, {{"text", "Kolory ekranowe są przybliżone. Ceny nowych dekorów wpisz w Materiały i ceny."}, {"padding", 8}}))->pack();
    (new ttk::Button(d, {{"text", "Zastosuj kolor / dekor"}, {"command", safe(app, apply)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 12}});
    return d;
}

form_dialog::Form* motion_dialog(App* app) {
    core::Part p = selected(app);
    json motion = mechanisms::effective_motion(p);
    auto* d = app->dialog("Mechanizm i otwieranie — " + p.name, "590x420");
    auto found = std::find_if(mechanisms::MODES.begin(), mechanisms::MODES.end(), [&](const auto& item) { return json(item.second) == motion.at("mode"); });
    if (found == mechanisms::MODES.end()) throw py::StopIteration();
    tk::StringVar mode(found->first);
    (new ttk::Label(d, {{"text", "Wybierz ruch frontu lub pokrywy"}, {"padding", 15}}))->pack({{"anchor", "w"}});
    std::vector<std::string> labels;
    for (const auto& item : mechanisms::MODES) labels.push_back(item.first);
    (new ttk::Combobox(d, {{"textvariable", mode}, {"values", labels}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"padx", 15}});
    tk::StringVar angle = app->field(d, "Maksymalny kąt [°]", py::get(motion, "angle", 100));
    tk::StringVar travel = app->field(d, "Maksymalny wysuw [mm]", py::get(motion, "travel", 400));
    (new ttk::Label(d, {{"text", "Suwak otwierania jest pod widokami 3D. Zaznacz front i porusz suwakiem lub uruchom animację. Klapy, drzwi przesuwne i cargo pokazują ruch koncepcyjny; nie dobierają nośności ani otworów producenta."},
                       {"wraplength", 540}, {"padding", 15}}))->pack({{"fill", "x"}});
    auto apply = [app, d, p, mode, angle, travel]() mutable {
        d->ensure_current_project(true);
        auto item = std::find_if(mechanisms::MODES.begin(), mechanisms::MODES.end(), [&](const auto& m) { return m.first == mode.get(); });
        if (item == mechanisms::MODES.end()) throw std::out_of_range("'" + mode.get() + "'");
        commit(app, mechanisms::set_motion(*app->project, p.id, item->second, app->number(angle), app->number(travel)));
        d->destroy();
    };
    (new ttk::Button(d, {{"text", "Zapisz mechanizm"}, {"command", safe(app, apply)}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 12}});
    return d;
}

FittingsDialog::FittingsDialog(App* app_) : dock_workspace::DockDocument(app_), app(app_) {
    part = selected(app);
    title("Okucia i standardy otworów — " + part.name);
    geometry("1000x" + std::to_string(std::min(800, winfo_screenheight() - 110)) + "+20+20");
    minsize(900, 600);
    transient(app);
    grab_set();
    (new ttk::Button(this, {{"text", "Zamknij"}, {"command", Cmd([this] { destroy(); })}}))->pack({{"side", "bottom"}, {"fill", "x"}, {"padx", 10}, {"pady", 8}});
    auto* tabs = new ttk::Notebook(this);
    tabs->pack({{"fill", "both"}, {"expand", true}, {"padx", 10}, {"pady", 10}});
    for (const char* name : {"Katalog otworów", "Zawiasy", "System 32", "Uchwyty", "Połączenia korpusu", "Prowadnice i cargo"}) {
        auto* page = new ttk::Frame(tabs, {{"padding", 14}});
        tabs->add(page, {{"text", name}});
        pages.push_back({name, page});
    }
    auto page_named = [this](const std::string& name) -> ttk::Frame* {
        for (auto& [n, p] : pages)
            if (n == name) return p;
        throw std::out_of_range("'" + name + "'");
    };
    ttk::Frame* catalog = page_named("Katalog otworów");
    auto* footer = new ttk::Frame(catalog);
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    (new ttk::Button(footer, {{"text", "Dodaj otwory według wzoru"}, {"command", safe(app, [this] { drill(); })}}))
        ->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"pady", 8}});
    (new ttk::Button(footer, {{"text", "Dokumentacja źródłowa"}, {"command", Cmd([this] { source(); })}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", 8}});
    auto* scrollbody = new ttk::Frame(catalog);
    scrollbody->pack({{"fill", "both"}, {"expand", true}});
    auto* formcanvas = new tk::Canvas(scrollbody, {{"bg", "#f6f8fa"}, {"highlightthickness", 0}});
    formcanvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* formsb = new ttk::Scrollbar(scrollbody, {{"command", formcanvas->path() + " yview"}});
    formsb->pack({{"side", "right"}, {"fill", "y"}});
    formcanvas->configure({{"yscrollcommand", formsb->path() + " set"}});
    auto* page = new ttk::Frame(formcanvas);
    int formwin = formcanvas->create_window(0, 0, {{"window", page->path()}, {"anchor", "nw"}});
    page->bind("<Configure>", [formcanvas](tk::Event&) { responsive_ui::update_scrollregion(formcanvas); return std::string(); });
    formcanvas->bind("<Configure>", [formcanvas, formwin](tk::Event& e) { formcanvas->itemconfigure(formwin, {{"width", e.width}}); return std::string(); });
    std::vector<std::string> recipe_names;
    for (const auto& r : fittings::RECIPES) recipe_names.push_back(r.at("name").get<std::string>());
    choice = tk::StringVar(recipe_names.front());
    (new ttk::Combobox(page, {{"textvariable", choice}, {"values", recipe_names}, {"state", "readonly"}, {"width", 80}}))->pack({{"fill", "x"}});
    info = tk::StringVar();
    (new ttk::Label(page, {{"textvariable", info}, {"wraplength", 900}, {"padding", "0 8"}}))->pack({{"fill", "x"}});
    choice.trace_add("write", Cmd([this] { describe(); }));
    describe();
    struct Spec { const char* key; const char* label; json value; };
    for (const Spec& spec : std::vector<Spec>{{"u", "Pierwsza współrzędna", 37}, {"v", "Druga współrzędna", 100}, {"diameter", "Średnica [mm]", 5},
                                              {"depth", "Głębokość [mm]", 10}, {"count", "Liczba w rzędzie", 1}, {"pitch", "Rozstaw [mm]", 32}})
        fields.push_back({spec.key, app->field(page, spec.label, spec.value)});
    fields_ready = true;
    std::vector<std::string> face_labels;
    for (const auto& item : features::FACES) face_labels.push_back(item.first);
    face = tk::StringVar("Góra (+T)");
    (new ttk::Combobox(page, {{"textvariable", face}, {"values", face_labels}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    (new ttk::Label(page, {{"text", "Na ±T: U,V. Na U=0/max: V,T. Na V=0/max: U,T. Wzory puszek: środek puszki, tylko ±T."}, {"padding", 8}}))->pack({{"fill", "x"}});

    page = page_named("Zawiasy");
    hinge = tk::StringVar("45 × 9,5");
    (new ttk::Combobox(page, {{"textvariable", hinge}, {"values", std::vector<std::string>{"45 × 9,5", "52 × 9", "Hettich Sensys TH52 (52 × 5,5)"}}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    hinge_count = app->field(page, "Liczba zawiasów", 2);
    side = tk::StringVar("Automatycznie");
    (new ttk::Combobox(page, {{"textvariable", side}, {"values", std::vector<std::string>{"Automatycznie", "Lewy", "Prawy"}}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Label(page, {{"text", "Przelicza front, puszki, otwory mocowania i prowadniki na boku. Front musi być przy wybranym boku. Możesz użyć „Dopasuj element do korpusu”, jeśli szkic nie pasuje. Sprawdź konkretny wariant zawiasu i grubość frontu."},
                          {"wraplength", 900}, {"padding", 15}}))->pack({{"fill", "x"}});
    (new ttk::Button(page, {{"text", "Dodaj / napraw zawiasy na drzwiczkach"}, {"command", safe(app, [this] { hinges(); })}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"pady", 8}});

    page = page_named("System 32");
    for (const Spec& spec : std::vector<Spec>{{"front", "Linia od przodu", 37}, {"back", "Linia od tyłu", 37}, {"start", "Początek od dołu", 64},
                                              {"end", "Margines na końcu", 64}, {"diameter", "Średnica", 5}, {"depth", "Głębokość", 10}})
        sys_fields.push_back({spec.key, app->field(page, spec.label, spec.value)});
    both = tk::BooleanVar(true);
    (new ttk::Checkbutton(page, {{"text", "Dwa rzędy — przód i tył"}, {"variable", both}}))->pack({{"anchor", "w"}});
    sys_face = tk::StringVar("top");
    (new ttk::Combobox(page, {{"textvariable", sys_face}, {"values", std::vector<std::string>{"top", "bottom"}}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Label(page, {{"text", "U = głębokość boku, V = wysokość. Krok stały 32 mm. Dla boku lewego powierzchnia wewnętrzna zwykle top, dla prawego bottom."},
                          {"wraplength", 900}, {"padding", 10}}))->pack({{"fill", "x"}});
    (new ttk::Button(page, {{"text", "Generuj / zastąp rzędy System 32"}, {"command", safe(app, [this] { system(); })}}))->pack({{"fill", "x"}});

    page = page_named("Uchwyty");
    handle = tk::StringVar("Uchwyt 128");
    std::vector<std::string> handle_names;
    for (const auto& item : fittings::HANDLES) handle_names.push_back(item.first);
    (new ttk::Combobox(page, {{"textvariable", handle}, {"values", handle_names}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    handle_u = app->field(page, "Środek U", part.u - 40);
    handle_v = app->field(page, "Środek V", part.v / 2);
    vertical = tk::BooleanVar(true);
    (new ttk::Checkbutton(page, {{"text", "Pionowo (odznacz = poziomo)"}, {"variable", vertical}}))->pack({{"anchor", "w"}});
    finish = tk::StringVar("Czarny mat");
    std::vector<std::string> finish_names;
    for (const auto& [name, value] : fittings::FINISHES.items()) finish_names.push_back(name);
    (new ttk::Combobox(page, {{"textvariable", finish}, {"values", finish_names}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Label(page, {{"text", "Uchwyt / gałka ma model 3D i przeloty Ø4,5 pod śrubę M4. Rozstaw oraz wygląd są wzorcem ogólnym."}, {"padding", 12}}))->pack({{"fill", "x"}});
    (new ttk::Button(page, {{"text", "Dodaj / zmień uchwyt i otwory"}, {"command", safe(app, [this] { handles(); })}}))->pack({{"fill", "x"}});

    page = page_named("Połączenia korpusu");
    connector = tk::StringVar("confirmat7");
    (new ttk::Combobox(page, {{"textvariable", connector}, {"values", std::vector<std::string>{"confirmat7", "confirmat5", "dowel6", "dowel8", "dowel10"}}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    connector_front = app->field(page, "Od przodu [mm]", 50);
    connector_back = app->field(page, "Od tyłu [mm]", 50);
    (new ttk::Label(page, {{"text", "Tworzy pary otworów w bokach oraz krawędziach dna i wieńca wybranego korpusu. Konfirmat 7: przelot Ø7, pilot Ø5, pogłębienie Ø10 × 2. Wartości robocze wymagają doboru do łącznika."},
                          {"wraplength", 900}, {"padding", 15}}))->pack({{"fill", "x"}});
    (new ttk::Button(page, {{"text", "Połącz korpus i wygeneruj wiercenia"}, {"command", safe(app, [this] { connectors(); })}}))->pack({{"fill", "x"}});

    page = page_named("Prowadnice i cargo");
    (new ttk::Label(page, {{"text", "Katalog prowadnic producentów: dobór długości, grubości skrzynki, luzów i cofnięcia dna. Kalkulator odróżnia wymiary producenta od wartości warsztatowych. Otwory zaczepów i mocowania konkretnego artykułu wymagają jego dokumentacji.\n\nCargo i klapy: schemat ruchu i wizualizacja wyposażenia; dostawę okuć dobierz z katalogu producenta.\n\nWybierz front szuflady, aby zmienić parametry skrzynki. Wybierz front korpusu, aby dodać cargo."},
                          {"wraplength", 890}, {"padding", 12}}))->pack({{"fill", "x"}});
    (new ttk::Button(page, {{"text", "Katalog prowadnic producentów — przelicz szufladę"}, {"command", safe(app, [this] { runners(); })}}))->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Button(page, {{"text", "Dodaj cargo i mechanizm wysuwu"}, {"command", safe(app, [this] { cargo(); })}}))->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Button(page, {{"text", "Ustaw mechanizm klapy / przesuwu…"}, {"command", safe(app, [this] { motion_dialog(this->app); })}}))->pack({{"fill", "x"}, {"pady", 8}});
    scroll_pages();
}

void FittingsDialog::scroll_pages() {
    for (auto& [name, page] : pages) {
        if (name == "Katalog otworów") continue;
        std::vector<tk::Widget*> children = page->winfo_children();
        std::map<tk::Widget*, std::map<std::string, std::string>> options;
        for (auto* widget : children) options[widget] = widget->pack_info();
        for (auto* widget : children) widget->pack_forget();
        auto* footer = new ttk::Frame(page);
        footer->pack({{"side", "bottom"}, {"fill", "x"}});
        auto* body = new ttk::Frame(page);
        body->pack({{"fill", "both"}, {"expand", true}});
        auto* viewport = new tk::Canvas(body, {{"highlightthickness", 0}, {"bg", "#f6f8fa"}});
        viewport->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
        auto* scrollbar = new ttk::Scrollbar(body, {{"command", viewport->path() + " yview"}});
        scrollbar->pack({{"side", "right"}, {"fill", "y"}});
        viewport->configure({{"yscrollcommand", scrollbar->path() + " set"}});
        auto* form = new ttk::Frame(viewport);
        int window = viewport->create_window(0, 0, {{"window", form->path()}, {"anchor", "nw"}});
        form->bind("<Configure>", [viewport](tk::Event&) { responsive_ui::update_scrollregion(viewport); return std::string(); });
        viewport->bind("<Configure>", [viewport, window](tk::Event& e) { viewport->itemconfigure(window, {{"width", e.width}}); return std::string(); });
        for (auto* widget : children) {
            if (widget->winfo_class() == "TButton") {
                widget->pack({{"in", footer}, {"fill", "x"}, {"pady", 4}});
            } else {
                tk::Opts info;
                for (const auto& [key, value] : options[widget])
                    if (key != "in") info.push_back({key, value});
                info.push_back({"in", form});
                widget->pack(info);
                widget->bind("<MouseWheel>", [viewport](tk::Event& e) { viewport->yview_scroll(e.delta > 0 ? -1 : 1, "units"); return std::string(); }, true);
            }
            widget->lift();
        }
    }
}

void FittingsDialog::describe() {
    const std::string recipe_name = choice.get();
    const json& r = recipe_named(recipe_name);
    info.set(r.at("verified").get<std::string>() + "\n" + (py::truthy(r.at("source")) ? r.at("source").get<std::string>() : std::string("Własny wzorzec roboczy — wartości edytowalne.")));
    if (fields_ready) {
        for (auto& [key, var] : fields) {
            json value;
            if (key == "diameter") value = r.at("diameter");
            else if (key == "depth") value = num_or(r.at("depth"), json(part.t));
            else if (key == "pitch") value = py::get(r, "pitch", 32);
            else continue;
            var.set(py::str(value));
        }
    }
}

void FittingsDialog::source() {
    const std::string recipe_name = choice.get();
    const json& r = recipe_named(recipe_name);
    if (py::truthy(r.at("source"))) py::webbrowser_open(r.at("source").get<std::string>());
}

void FittingsDialog::update_project(const core::ProjectPtr& result) {
    ensure_current_project(true);
    commit(app, result);
    auto found = std::find_if(result->parts.begin(), result->parts.end(), [&](const core::Part& p) { return p.id == part.id; });
    if (found == result->parts.end()) throw py::StopIteration();
    part = *found;
    mark_clean();
}

void FittingsDialog::drill() {
    const std::string recipe_name = choice.get();
    const json& r = recipe_named(recipe_name);
    std::map<std::string, double> values;
    for (auto& [key, var] : fields) values[key] = app->number(var);
    double count = values["count"], pitch = values["pitch"];
    values.erase("count");
    values.erase("pitch");
    if (count != std::trunc(count)) throw ValueError("Liczba otworów musi być całkowita.");
    auto result = std::make_shared<core::Project>(*app->project);  // copy.deepcopy
    auto found = std::find_if(result->parts.begin(), result->parts.end(), [&](const core::Part& p) { return p.id == part.id; });
    if (found == result->parts.end()) throw py::StopIteration();
    core::Part& p = *found;
    auto face_item = std::find_if(features::FACES.begin(), features::FACES.end(), [&](const auto& item) { return item.first == face.get(); });
    if (face_item == features::FACES.end()) throw std::out_of_range("'" + face.get() + "'");
    const std::string& face_name = face_item->second;
    json operations = features::hole_array(p, values.at("u"), values.at("v"), values.at("diameter"), values.at("depth"), 1, count, 32, pitch, face_name);
    if (r.at("id").get<std::string>().rfind("hinge", 0) == 0) {
        if (face_name != "top" && face_name != "bottom") throw ValueError("Wzór puszki wymaga powierzchni ±T.");
        const json snapshot = operations;  // list(operations)
        for (const auto& op : snapshot) {
            double half = r.at("pitch").get<double>() / 2;
            for (double v : {op.at("v").get<double>() - half, op.at("v").get<double>() + half}) {
                json more = features::hole_array(p, op.at("u").get<double>() - r.at("offset").get<double>(), v, r.at("pilot").get<double>(),
                                                 r.at("pilot_depth").get<double>(), 1, 1, 32, 32, face_name);
                for (const auto& extra : more) operations.push_back(extra);
            }
        }
    }
    for (auto& op : operations) {
        op["recipe_id"] = r.at("id");
        op["source"] = r.at("source");
        op["purpose"] = r.at("name");
    }
    for (const auto& op : operations) p.operations.push_back(op);
    result->validate();
    update_project(result);
}

void FittingsDialog::hinges() {
    double count = app->number(hinge_count);
    std::optional<std::string> side_value;
    if (side.get() != "Automatycznie") side_value = side.get() == "Lewy" ? "left" : "right";
    static const std::vector<std::pair<std::string, std::string>> patterns = {
        {"45 × 9,5", "hinge45"}, {"52 × 9", "hinge52"}, {"Hettich Sensys TH52 (52 × 5,5)", "hinge_sensys52"}};
    auto found = std::find_if(patterns.begin(), patterns.end(), [&](const auto& item) { return item.first == hinge.get(); });
    if (found == patterns.end()) throw std::out_of_range("'" + hinge.get() + "'");
    update_project(dimension_model::regenerate_hinges(*app->project, part.id, found->second, count, side_value));
}

void FittingsDialog::system() {
    std::map<std::string, double> v;
    for (auto& [key, var] : sys_fields) v[key] = app->number(var);
    update_project(fittings::system32(*app->project, part.id, v["front"], v["back"], v["start"], v["end"], v["diameter"], v["depth"], both.get(), sys_face.get()));
}

void FittingsDialog::handles() {
    auto found = std::find_if(fittings::HANDLES.begin(), fittings::HANDLES.end(), [&](const auto& item) { return item.first == handle.get(); });
    if (found == fittings::HANDLES.end()) throw std::out_of_range("'" + handle.get() + "'");
    if (!fittings::FINISHES.contains(finish.get())) throw std::out_of_range("'" + finish.get() + "'");
    update_project(fittings::add_handle(*app->project, part.id, found->second, app->number(handle_u), app->number(handle_v), vertical.get(),
                                        fittings::FINISHES.at(finish.get())));
}

void FittingsDialog::connectors() {
    update_project(fittings::cabinet_connectors(*app->project, part.group, connector.get(), app->number(connector_front), app->number(connector_back)));
}

void FittingsDialog::runners() {
    destroy();
    new hardware_ui::RunnerDialog(app);
}

void FittingsDialog::cargo() { update_project(mechanisms::set_motion(*app->project, part.id, "cargo", 100, 400)); }

void install(App* app) {
    auto* bar = dynamic_cast<tk::Menu*>(app->nametowidget(app->cget("menu")));
    if (bar == nullptr) throw tk::TclError("bad menu entry: " + app->cget("menu"));
    auto* menu = new tk::Menu(bar, {{"tearoff", false}});
    bar->add_cascade({{"label", "Okucia i wyposażenie"}, {"menu", menu}});
    for (auto& [text, fn] : std::vector<std::pair<std::string, Cmd>>{
             {"Katalog otworów, zawiasy, uchwyty…", [app] { new FittingsDialog(app); }},
             {"Paleta kolorów i dekorów…", [app] { palette_dialog(app); }},
             {"Mechanizm otwierania…", [app] { motion_dialog(app); }},
             {"Dopasuj element do korpusu", [app] { commit(app, dimension_model::fit_part(*app->project, selected(app).id)); }},
             {"Biblioteka mebli do pomieszczeń…", [app] { library_dialog(app); }}})
        menu->add_command({{"label", text}, {"command", safe(app, fn)}});
    app->fittings_dialog = [app] { safe(app, [app] { new FittingsDialog(app); })(); };
    app->palette_dialog = [app] { safe(app, [app] { palette_dialog(app); })(); };
    app->motion_dialog = [app] { safe(app, [app] { motion_dialog(app); })(); };
    app->library_dialog = [app] { library_dialog(app); };
    app->fit_selected = [app] { safe(app, [app] { commit(app, dimension_model::fit_part(*app->project, selected(app).id)); })(); };
}

}  // namespace studio_ui
