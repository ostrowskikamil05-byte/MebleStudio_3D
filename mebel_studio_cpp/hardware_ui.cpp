// Dockable sourced planning calculators and handle alternatives.
#include "hardware_ui.h"

#include <filesystem>
#include <format>

#include "app.h"
#include "assembly.h"
#include "hardware_report.h"
#include "material_presets.h"
#include "opening_options.h"
#include "pyutil.h"
#include "runner_installation.h"
#include "runners_catalog.h"
#include "studio_ui.h"

namespace hardware_ui {

using py::ValueError;

namespace {

// f'{value:g}' dla int/float/bool z JSON-a
std::string g(const json& value) {
    double v = value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>();
    return std::format("{:g}", v);
}

bool is_number(const json& value) { return value.is_number() || value.is_boolean(); }

std::string number_text(double value) {
    if (value == std::floor(value) && std::abs(value) < 1e15) return std::to_string(static_cast<long long>(value));
    return py::repr(value);
}

std::vector<std::string> strings(const json& values) {
    std::vector<std::string> out;
    for (const auto& item : values) out.push_back(py::str(item));
    return out;
}

bool contains(const std::vector<std::string>& values, const std::string& text) {
    return std::find(values.begin(), values.end(), text) != values.end();
}

}  // namespace

ScrollForm _scroll_form(tk::Widget* parent, int width) {
    auto* holder = new ttk::Frame(parent, {{"width", width}});
    auto* viewport = new tk::Canvas(holder, {{"width", width}, {"highlightthickness", 0}, {"bg", "#f6f8fa"}});
    auto* scrollbar = new ttk::Scrollbar(holder, {{"orient", "vertical"}, {"command", viewport->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    viewport->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    viewport->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    auto* form = new ttk::Frame(viewport);
    int item = viewport->create_window(0, 0, {{"window", form->path()}, {"anchor", "nw"}});
    form->bind("<Configure>", [viewport](tk::Event&) {
        auto box = viewport->bbox("all");
        if (box) {
            std::string region;
            for (size_t i = 0; i < box->size(); ++i) region += (i ? " " : "") + std::format("{:g}", (*box)[i]);
            viewport->configure({{"scrollregion", region}});
        }
        return std::string();
    });
    viewport->bind("<Configure>", [viewport, item](tk::Event& e) { viewport->itemconfigure(item, {{"width", e.width}}); return std::string(); });
    auto wheel = [viewport](tk::Event& event) { viewport->yview_scroll(event.delta > 0 ? -1 : 1, "units"); return std::string("break"); };
    viewport->bind("<MouseWheel>", wheel);
    auto bind_children = [form, wheel] {
        std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
            widget->bind("<MouseWheel>", wheel, true);
            for (auto* child : widget->winfo_children()) visit(child);
        };
        visit(form);
    };
    return ScrollForm{holder, form, viewport, bind_children};
}

RunnerDialog::RunnerDialog(App* app_) : dock_workspace::DockDocument(app_) {
    part_id = app->selected;
    title("Prowadnice — katalog i kalkulator"); geometry("990x730");
    profiles = runners_catalog::PROFILES;
    choice = tk::StringVar(py::str(profiles.begin().value().at("label")));
    summary = tk::StringVar(); note = tk::StringVar();
    auto* content = new ttk::Frame(this, {{"padding", 16}});
    content->pack({{"fill", "both"}, {"expand", true}});
    (new ttk::Label(content, {{"text", "Seria i wariant prowadnicy"}, {"style", "Title.TLabel"}}))->pack({{"anchor", "w"}});
    std::vector<std::string> labels;
    for (auto& [k, p] : profiles.items()) labels.push_back(py::str(p.at("label")));
    (new ttk::Combobox(content, {{"textvariable", choice}, {"values", labels}, {"state", "readonly"}, {"width", 90}}))->pack({{"fill", "x"}, {"pady", 8}});
    // Reserve the actions before assigning the remaining height to forms.
    // A docked page is shorter than the old 730 px floating dialog.
    auto* footer = new ttk::Frame(content);
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    (new ttk::Label(footer, {{"text", "Skrzynka drewniana z czterema ściankami. Zastosowanie pobiera światło i głębokość z korpusu. Zaczepy, rowki i mocowania zależą od wybranego wariantu."},
                             {"wraplength", 1160}, {"padding", "0 8"}}))->pack({{"fill", "x"}});
    auto* actions = new ttk::Frame(footer);
    actions->pack({{"fill", "x"}, {"pady", "2 0"}});
    (new ttk::Button(actions, {{"text", "Dokumentacja wybranego wariantu"}, {"command", std::function<void()>([this] { source(); })}}))->pack({{"side", "left"}, {"padx", "0 10"}});
    (new ttk::Button(actions, {{"text", "Zastosuj do zaznaczonego frontu szuflady"}, {"command", studio_ui::safe(app, [this] { apply(); })}, {"style", "Accent.TButton"}}))
        ->pack({{"side", "right"}, {"fill", "x"}, {"expand", true}});
    auto* columns = new ttk::Frame(content);
    columns->pack({{"fill", "both"}, {"expand", true}, {"pady", "0 6"}});
    ScrollForm scroll = _scroll_form(columns);
    form_viewport = scroll.viewport;
    scroll.holder->pack({{"side", "left"}, {"fill", "y"}, {"padx", "0 15"}});
    auto* info = new ttk::Notebook(columns);
    info->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* dimensions = new ttk::Frame(info);
    auto* notes = new ttk::Frame(info);
    info->add(dimensions, {{"text", "Wymiary planowania"}});
    info->add(notes, {{"text", "Uwagi i warunki montażu"}});
    result_tabs = info;
    json width = 564, depth = 560;
    Part* p = app->selected_part();
    if (p) {
        try {
            assembly::Frame f = assembly::frame(*app->project, p->group);
            width = f.inner_right - f.inner_left;
            depth = runner_installation::drawer_depth_context(*app->project, *p, json{{"drawer_front_gap", 0}}).at("usable_depth");
        } catch (const ValueError&) {
        }
    }
    struct Field { const char* key; const char* label; json value; };
    for (const Field& f : {Field{"inside", "Światło między bokami korpusu [mm]", width}, Field{"cabdepth", "Użyteczna głębokość korpusu [mm]", depth},
                           Field{"side", "Grubość boku skrzynki [mm]", 16}, Field{"bottom", "Grubość dna [mm]", 6},
                           Field{"recess", "Cofnięcie dna od dołu boku [mm]", 12}})
        vars.emplace_back(f.key, app->field(scroll.form, f.label, f.value));
    (new ttk::Label(scroll.form, {{"text", "Długość nominalna prowadnicy [mm]"}}))->pack({{"anchor", "w"}, {"pady", "8 2"}});
    nominal = tk::StringVar("400");
    length_combo = new ttk::Combobox(scroll.form, {{"textvariable", nominal}, {"state", "readonly"}});
    length_combo->pack({{"fill", "x"}});
    (new ttk::Label(scroll.form, {{"text", "Potwierdzona min. głębokość [mm]\nWypełnij tylko przy braku danych w karcie"}, {"wraplength", 300}}))->pack({{"anchor", "w"}, {"pady", "8 2"}});
    confirmed = tk::StringVar("");
    confirmed_entry = new ttk::Entry(scroll.form, {{"textvariable", confirmed}});
    confirmed_entry->pack({{"fill", "x"}});
    (new ttk::Label(dimensions, {{"textvariable", summary}, {"font", "{Segoe UI} 11"}, {"wraplength", 650}, {"padding", 10}}))->pack({{"fill", "x"}});
    auto* details = new ttk::Frame(notes, {{"padding", 8}});
    details->pack({{"fill", "both"}, {"expand", true}});
    auto* note_text = new tk::Text(details, {{"wrap", "word"}, {"height", 5}, {"font", "{Segoe UI} 10"}, {"relief", "flat"}, {"bg", "#f6f8fa"}});
    note_text->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* scrollbar = new ttk::Scrollbar(details, {{"command", note_text->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    note_text->configure({{"yscrollcommand", scrollbar->path() + " set"}, {"state", "disabled"}});
    note.trace_add("write", [this, note_text] {
        note_text->configure({{"state", "normal"}});
        note_text->delete_("1.0", "end"); note_text->insert("1.0", note.get());
        note_text->configure({{"state", "disabled"}});
    });
    choice.trace_add("write", [this] { choose(); });
    for (auto& [key, v] : vars) v.trace_add("write", [this] { calculate(); });
    nominal.trace_add("write", [this] { calculate(); });
    confirmed.trace_add("write", [this] { calculate(); });
    choose();
    after_idle(scroll.bind_children);
}

tk::StringVar& RunnerDialog::var(const std::string& key) {
    for (auto& [k, v] : vars) if (k == key) return v;
    throw py::KeyError(key);
}

json RunnerDialog::profile() {
    for (auto& [k, p] : profiles.items())
        if (py::str(p.at("label")) == choice.get()) return runners_catalog::get_profile(k);
    throw py::StopIteration();
}

void RunnerDialog::choose() {
    json spec = profile();
    json lengths = spec.at("lengths");
    length_combo->configure({{"values", strings(lengths)}});
    confirmed_entry->configure({{"state", spec.at("depth_status") == "unverified" ? "normal" : "disabled"}});
    confirmed.set("");
    if (!contains(strings(lengths), nominal.get())) {
        bool has_400 = false;
        for (const auto& x : lengths) if (x.is_number() && x.get<double>() == 400) has_400 = true;
        nominal.set(has_400 ? std::string("400") : py::str(lengths.at(0)));
    }
    calculate();
}

json RunnerDialog::confirmed_depth() {
    if (!py::strip(confirmed.get()).empty()) return app->number(confirmed);
    return nullptr;
}

void RunnerDialog::calculate() {
    try {
        std::vector<std::pair<std::string, double>> v;
        for (auto& [k, x] : vars) v.emplace_back(k, app->number(x));
        auto value = [&](const std::string& key) {
            for (auto& [k, x] : v) if (k == key) return x;
            throw py::KeyError(key);
        };
        json prof = profile();
        json plan = runners_catalog::calculate_runner(prof.at("id"), value("inside"), app->number(nominal), value("side"), value("cabdepth"), "four_sided", value("side"), confirmed_depth());
        last_plan = plan;
        auto fmt = [&](const std::string& key) {
            json found = py::get(plan, key);
            return is_number(found) ? g(found) + " mm" : std::string("Brak danych dla wariantu");
        };
        std::vector<std::string> lines;
        for (auto& [key, label] : std::vector<std::pair<std::string, std::string>>{
                 {"drawer_inner_width", "Wnętrze skrzynki"}, {"drawer_outer_width", "Skrzynka z zewnątrz"}, {"external_clearance_per_side", "Luz na jedną stronę"},
                 {"drawer_length", "Długość skrzynki"}, {"min_cabinet_depth", "Minimalna głębokość korpusu"}, {"bottom_recess_min", "Cofnięcie dna min."}})
            lines.push_back(label + ": " + fmt(key));
        std::string details = py::join(lines, "\n");
        json range = plan.at("drawer_inner_width_range");
        details += "\nDopuszczalne wnętrze: " + g(range.at(0)) + "–" + g(range.at(1)) + " mm";
        if (py::truthy(py::get(plan, "article"))) details += "\nArtykuł: " + plan.at("article").get<std::string>();
        if (plan.at("length_status") == "workshop") details += "\nDługość skrzynki: ustawienie warsztatowe";
        if (plan.at("depth_status") == "user_confirmed") details += "\nGłębokość: potwierdzona przez użytkownika";
        summary.set(details);
        std::string prefix;
        if (py::truthy(plan.at("valid")) && !plan.at("min_cabinet_depth").is_null()) prefix = "Wymiary obrysu mieszczą się w podanej przestrzeni.\n";
        else if (py::truthy(plan.at("valid"))) prefix = "Obrys pasuje; wpisz potwierdzoną głębokość montażową przed zastosowaniem.\n";
        else prefix = py::join(strings(plan.at("errors")), "\n") + "\n";
        note.set(prefix + plan.at("status").get<std::string>() + "\n" + py::join(strings(plan.at("warnings")), "\n"));
    } catch (const ValueError&) {
        last_plan = nullptr; summary.set("Wpisz poprawne wymiary."); note.set("");
    } catch (const std::out_of_range&) {
        last_plan = nullptr; summary.set("Wpisz poprawne wymiary."); note.set("");
    } catch (const json::out_of_range&) {
        last_plan = nullptr; summary.set("Wpisz poprawne wymiary."); note.set("");
    }
}

void RunnerDialog::source() {
    json prof = profile();
    json by_length = py::get(prof, "source_by_length", json::object());
    json found = py::get(by_length, number_text(app->number(nominal)), prof.at("source"));
    py::webbrowser_open(py::str(found));
}

void RunnerDialog::apply() {
    if (!part_id) throw ValueError("Przed otwarciem kalkulatora wybierz front szuflady.");
    studio_ui::commit(app, runner_installation::apply_runner(*app->project, *part_id, profile().at("id"), app->number(nominal), app->number(var("side")),
                                                              app->number(var("bottom")), app->number(var("recess")), confirmed_depth()));
    app->selected = part_id; app->refresh();
    const core::Part* part = nullptr;
    for (auto& q : app->project->parts) if (q.id == *part_id) { part = &q; break; }
    if (!part) throw py::StopIteration();
    assembly::Frame f = assembly::frame(*app->project, part->group);
    var("inside").set(py::str(json(f.inner_right - f.inner_left)));
    var("cabdepth").set(py::str(runner_installation::drawer_depth_context(*app->project, *part).at("usable_depth")));
    calculate();
    mark_clean();
}

OpeningDialog::OpeningDialog(App* app_) : dock_workspace::DockDocument(app_) {
    core::Part& p = studio_ui::selected(app);
    part_id = p.id;
    title("Otwieranie bez uchwytów — " + p.name); geometry("850x620");
    options = opening_options::OPTIONS;
    choice = tk::StringVar(py::str(options.at("tip_bumper").at("label")));
    note = tk::StringVar();
    auto* content = new ttk::Frame(this, {{"padding", 18}});
    content->pack({{"fill", "both"}, {"expand", true}});
    std::vector<std::string> labels;
    for (auto& [k, s] : options.items()) labels.push_back(py::str(s.at("label")));
    (new ttk::Combobox(content, {{"textvariable", choice}, {"values", labels}, {"state", "readonly"}, {"width", 85}}))->pack({{"fill", "x"}});
    auto* footer = new ttk::Frame(content);
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    (new ttk::Label(footer, {{"textvariable", note}, {"wraplength", 800}, {"padding", 10}}))->pack({{"fill", "x"}});
    (new ttk::Button(footer, {{"text", "Dokumentacja"}, {"command", std::function<void()>([this] { source(); })}}))->pack({{"side", "left"}, {"padx", "0 10"}});
    (new ttk::Button(footer, {{"text", "Zastosuj sposób otwierania"}, {"command", studio_ui::safe(app, [this] { apply(); })}, {"style", "Accent.TButton"}}))
        ->pack({{"side", "right"}, {"fill", "x"}, {"expand", true}});
    ScrollForm scroll = _scroll_form(content, 750);
    scroll.holder->pack({{"fill", "both"}, {"expand", true}, {"pady", 8}});
    struct Field { const char* key; const char* label; json value; };
    for (const Field& f : {Field{"length", "Długość uchwytu / kieszeni [mm]", 120}, Field{"width", "Szerokość kieszeni [mm]", 24},
                           Field{"depth", "Głębokość kieszeni / wysunięcie profilu [mm]", 8}, Field{"level", "Poziom V / wysokość wypychacza na froncie [mm]", p.v / 2},
                           Field{"center_u", "Położenie U środka kieszeni [mm]", p.u / 2}, Field{"angle", "Kąt kieszeni [°]", 0}})
        fields.emplace_back(f.key, app->field(scroll.form, f.label, f.value));
    spring_labels = {{"Nieznany — wymaga doboru", "unknown"}, {"Ze sprężyną zamykającą", "sprung"}, {"Bez sprężyny", "unsprung"}};
    json state = py::get(py::get(p.assembly, "config", json::object()), "hinge_spring", "unknown");
    std::string spring_label;
    bool found = false;
    for (auto& [k, v] : spring_labels) if (json(v) == state) { spring_label = k; found = true; break; }
    if (!found) throw py::StopIteration();
    spring = tk::StringVar(spring_label);
    (new ttk::Label(scroll.form, {{"text", "Rodzaj zawiasu przy wypychaczu"}}))->pack({{"anchor", "w"}, {"pady", "8 2"}});
    std::vector<std::string> spring_values;
    for (auto& [k, v] : spring_labels) spring_values.push_back(k);
    (new ttk::Combobox(scroll.form, {{"textvariable", spring}, {"values", spring_values}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    edge_labels = {{"Górna", "top"}, {"Dolna", "bottom"}, {"Lewa", "left"}, {"Prawa", "right"}};
    edge = tk::StringVar("Górna");
    (new ttk::Label(scroll.form, {{"text", "Krawędź własnego profilu uchwytu"}}))->pack({{"anchor", "w"}, {"pady", "8 2"}});
    std::vector<std::string> edge_values;
    for (auto& [k, v] : edge_labels) edge_values.push_back(k);
    (new ttk::Combobox(scroll.form, {{"textvariable", edge}, {"values", edge_values}, {"state", "readonly"}}))->pack({{"fill", "x"}});
    choice.trace_add("write", [this] { describe(); });
    describe();
    after_idle(scroll.bind_children);
}

std::string OpeningDialog::key() {
    for (auto& [k, v] : options.items()) if (py::str(v.at("label")) == choice.get()) return k;
    throw py::StopIteration();
}

void OpeningDialog::describe() {
    note.set(py::str(options.at(key()).at("note")) +
             "\nU/V kieszeni zachowują proporcję po zmianie frontu. Zawias magnetycznego TIP-ON musi być jawnie bez sprężyny. Profil może leżeć na dowolnej z czterech krawędzi.");
}

void OpeningDialog::source() {
    json src = options.at(key()).at("source");
    if (py::truthy(src)) py::webbrowser_open(py::str(src));
}

void OpeningDialog::apply() {
    auto number = [&](const std::string& name) {
        for (auto& [k, v] : fields) if (k == name) return app->number(v);
        throw py::KeyError(name);
    };
    auto label_value = [](const std::vector<std::pair<std::string, std::string>>& labels, const std::string& label) {
        for (auto& [k, v] : labels) if (k == label) return v;
        throw py::KeyError(label);
    };
    std::string mode = key();
    double length = number("length"), width = number("width"), depth = number("depth"), level = number("level"), center_u = number("center_u"), angle = number("angle");
    std::string hinge_spring = label_value(spring_labels, spring.get()), profile_edge = label_value(edge_labels, edge.get());
    studio_ui::commit(app, opening_options::install(*app->project, part_id, mode, length, width, depth, level, center_u, angle, hinge_spring, profile_edge));
    app->selected = part_id; app->refresh();
    mark_clean();
}

dock_workspace::DockDocument* material_dialog(App* app) {
    core::Part& p = studio_ui::selected(app);
    std::string part_id = p.id;
    auto* d = new dock_workspace::DockDocument(app);
    d->title("Materiał i grubość — " + p.name);
    auto* form = new ttk::Frame(d, {{"padding", 18}});
    form->pack({{"fill", "both"}, {"expand", true}});
    auto choice = std::make_shared<tk::StringVar>(material_presets::PRESETS.front().first);
    auto thick = std::make_shared<tk::StringVar>("18");
    auto info = std::make_shared<tk::StringVar>();
    std::vector<std::string> names;
    for (auto& [k, v] : material_presets::PRESETS) names.push_back(k);
    (new ttk::Combobox(form, {{"textvariable", *choice}, {"values", names}, {"state", "readonly"}, {"width", 70}}))->pack({{"fill", "x"}, {"pady", 8}});
    auto* combo = new ttk::Combobox(form, {{"textvariable", *thick}, {"state", "readonly"}});
    combo->pack({{"fill", "x"}, {"pady", 8}});
    (new ttk::Label(form, {{"textvariable", *info}, {"wraplength", 800}, {"padding", 15}}))->pack({{"fill", "x"}});
    auto choose = [choice, thick, info, combo] {
        std::string name = choice->get();
        const auto& spec = material_presets::preset(name);
        std::vector<std::string> values;
        for (int t : spec.thicknesses) values.push_back(std::to_string(t));
        combo->configure({{"values", values}});
        if (!contains(values, thick->get())) thick->set(std::to_string(spec.stock_thicknesses.empty() ? spec.thicknesses.front() : spec.stock_thicknesses.front()));
        info->set(std::format("Format półfabrykatu: {:g} × {:g} mm. ", spec.sheet.first, spec.sheet.second) +
                  (spec.has_note ? spec.note : std::string("Edytowalne ustawienia warsztatowe; nie są normą dla wszystkich płyt i dostawców.")) +
                  " Cena 0: wpisz cenę w Materiały i ceny. Zmiana grubości przelicza powiązane elementy i może wymagać korekty okuć.");
    };
    choice->trace_add("write", choose);
    choose();
    (new ttk::Button(form, {{"text", "Źródło producenta"}, {"command", std::function<void()>([choice] {
        std::string name = choice->get();
        const std::string& source = material_presets::preset(name).source;
        if (!source.empty()) py::webbrowser_open(source);
    })}}))->pack({{"fill", "x"}});
    auto apply = [app, d, part_id, choice, thick] {
        studio_ui::commit(app, material_presets::apply_material_preset(*app->project, part_id, choice->get(), py::to_float(thick->get())));
        d->mark_clean();
    };
    (new ttk::Button(form, {{"text", "Zastosuj do elementu"}, {"command", studio_ui::safe(app, d->guard(apply))}, {"style", "Accent.TButton"}}))->pack({{"fill", "x"}, {"pady", 12}});
    return d;
}

void export_hardware(App* app) {
    std::string path = tk::filedialog::asksaveasfilename({{"parent", app}, {"title", "Plan montażu i lista okuć"}, {"defaultextension", ".html"},
                                                         {"filetypes", tk::FileTypes{{"Raport HTML", "*.html"}}}});
    if (!path.empty()) {
        hardware_report::hardware_report(*app->project, path);
        py::webbrowser_open(py::as_uri(std::filesystem::weakly_canonical(py::to_path(path))));
    }
}

void install(App* app) {
    tk::Widget* bar = app->nametowidget(app->cget("menu"));
    auto* menu = new tk::Menu(bar, {{"tearoff", false}});
    bar->call({bar->path(), "add", "cascade", "-label", "Kreator", "-menu", menu->path()});
    struct Entry { const char* label; std::function<void()> fn; };
    for (const Entry& entry : {Entry{"Prowadnice producentów — kalkulator…", [app] { new RunnerDialog(app); }},
                               Entry{"Fronty bez uchwytów / frezowanie…", [app] { new OpeningDialog(app); }},
                               Entry{"Materiały i grubości…", [app] { material_dialog(app); }},
                               Entry{"Plan montażu i lista okuć…", [app] { export_hardware(app); }},
                               Entry{"Dodatkowa zakładka wizualizacji", [app] { new dock_workspace::VisualizationDocument(app); }}})
        menu->add_command({{"label", entry.label}, {"command", studio_ui::safe(app, entry.fn)}});
}

}  // namespace hardware_ui
