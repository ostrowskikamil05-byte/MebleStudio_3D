// Profile editor and curved furniture library using the common project model.
#include "style_ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <system_error>

#include "app.h"
#include "core.h"
#include "pyutil.h"
#include "responsive_ui.h"
#include "shaped_furniture.h"

namespace style_ui {

using core::ValueError;
using shaped_furniture::SHAPES;
using shaped_furniture::TEMPLATES;
using shaped_furniture::THICKNESSES;
using Cmd = std::function<void()>;
using json = core::json;

namespace {

std::string g(double x) { return std::format("{:g}", x); }

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

void commit(App* app, const core::ProjectPtr& project) {
    app->snapshot();
    app->project = project;
    app->refresh();
    app->fit();
}

// _guard(app, fn): ValueError, OSError, KeyError, StopIteration -> okno błędu.
Cmd guarded(App* app, std::function<void()> fn) {
    return [app, fn = std::move(fn)] {
        auto fail = [app](const char* what) { tk::messagebox::showerror("Sprawdź wymiary", what, app); };
        try {
            fn();
        } catch (const std::invalid_argument& e) { fail(e.what()); }
        catch (const std::system_error& e) { fail(e.what()); }
        catch (const std::out_of_range& e) { fail(e.what()); }
        catch (const py::StopIteration& e) { fail(e.what()); }
    };
}

// x / y z ZeroDivisionError Pythona.
double divide(double a, double b) {
    if (b == 0) throw std::domain_error("float division by zero");
    return a / b;
}

struct ScrollForm { ttk::Frame* outer; ttk::Frame* form; };

ScrollForm scroll_form(tk::Widget* parent, int width = 380) {
    auto* outer = new ttk::Frame(parent);
    auto* canvas = new tk::Canvas(outer, {{"width", width}, {"bg", "#f6f3ee"}, {"highlightthickness", 0}});
    canvas->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* scrollbar = new ttk::Scrollbar(outer, {{"orient", "vertical"}, {"command", canvas->path() + " yview"}});
    scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    canvas->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    auto* form = new ttk::Frame(canvas);
    int window = canvas->create_window(0, 0, {{"window", form->path()}, {"anchor", "nw"}});
    form->bind("<Configure>", [canvas](tk::Event&) { responsive_ui::update_scrollregion(canvas); return std::string(); });
    canvas->bind("<Configure>", [canvas, window](tk::Event& e) { canvas->itemconfigure(window, {{"width", e.width}}); return std::string(); });
    auto wheel = [canvas](tk::Event& e) { canvas->yview_scroll(-static_cast<int>(e.delta / 120.0), "units"); return std::string(); };
    canvas->bind("<MouseWheel>", wheel);
    form->bind("<MouseWheel>", wheel);
    return {outer, form};
}

const std::vector<int>& thicknesses(const std::string& kind) {
    for (auto& [name, values] : THICKNESSES)
        if (name == kind) return values;
    throw std::out_of_range("'" + kind + "'");
}

std::string shape_kind(const std::string& label) {
    for (auto& [name, kind] : SHAPES)
        if (name == label) return kind;
    throw std::out_of_range("'" + label + "'");
}

std::vector<std::string> as_strings(const std::vector<int>& values) {
    std::vector<std::string> out;
    for (int v : values) out.push_back(std::to_string(v));
    return out;
}

const shaped_furniture::Template& template_at(long long index) {
    if (index < 0) index += (long long)TEMPLATES.size();
    if (index < 0 || index >= (long long)TEMPLATES.size()) throw std::out_of_range("list index out of range");
    return TEMPLATES[(size_t)index];
}

}  // namespace

// ---------------------------------------------------------------- shape_dialog

tk::StringVar& ShapeDialog::shape_var(const std::string& key) {
    for (auto& [name, var] : shape_vars)
        if (name == key) return var;
    throw std::out_of_range("'" + key + "'");
}

ShapeDialog::ShapeDialog(App* app_, const core::Part& part) : dock_workspace::DockDocument(app_), p(part) {
    ShapeDialog* d = this;
    d->title("Obłe kształty — " + p.name);
    d->geometry("980x740+40+40");
    (new ttk::Label(d, {{"text", "Kształt elementu i osobne narożniki"}, {"style", "Title.TLabel"}, {"padding", 12}}))->pack({{"anchor", "w"}});
    auto* footer = new ttk::Frame(d, {{"padding", 10}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* body = new ttk::Frame(d);
    body->pack({{"fill", "both"}, {"expand", true}, {"padx", 10}});
    auto* left = new ttk::Frame(body);
    left->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}, {"padx", "0 10"}});
    auto* canvas = new tk::Canvas(left, {{"bg", "#f1eee8"}, {"highlightthickness", 0}});
    canvas->pack({{"fill", "both"}, {"expand", true}});
    tk::StringVar summary;
    (new ttk::Label(left, {{"textvariable", summary}, {"wraplength", 470}, {"padding", 10}}))->pack({{"fill", "x"}});
    auto [outer, form] = scroll_form(body, 400);
    outer->pack({{"side", "right"}, {"fill", "y"}});
    json saved = p.profile;
    std::string initial_shape = "Prostokąt — cztery promienie";
    for (auto& [label, kind] : SHAPES)
        if (py::get(saved, "kind") == json(kind)) { initial_shape = label; break; }
    shape_choice = tk::StringVar(initial_shape);
    tk::StringVar& shape = shape_choice;
    (new ttk::Label(form, {{"text", "Kształt obrysu"}, {"padding", "15 6"}}))->pack({{"anchor", "w"}});
    std::vector<std::string> shape_labels;
    for (auto& [label, kind] : SHAPES) shape_labels.push_back(label);
    (new ttk::Combobox(form, {{"textvariable", shape}, {"values", shape_labels}, {"state", "readonly"}}))->pack({{"fill", "x"}, {"padx", 15}, {"pady", 5}});
    double default_radius = std::min(p.u, p.v) * .1;
    json radii = py::get(saved, "radii", json::array({default_radius, default_radius, default_radius, default_radius}));
    struct Spec { const char* key; const char* label; json value; };
    std::vector<Spec> specs{{"u", "Szerokość U / średnica [mm]", p.u}, {"v", "Wysokość V [mm]", p.v}, {"t", "Grubość [mm]", p.t},
                            {"r0", "R1 — lewy dolny", radii.at(0)}, {"r1", "R2 — prawy dolny", radii.at(1)},
                            {"r2", "R3 — prawy górny", radii.at(2)}, {"r3", "R4 — lewy górny", radii.at(3)},
                            {"rise", "Wysokość łuku [mm]", py::get(saved, "arch_rise", std::min(p.v / 3, p.u / 2))},
                            {"taper", "Zwężenie z każdej strony [mm]", py::get(saved, "taper", std::min(10.0, p.u / 8))},
                            {"segments", "Odcinki ćwiartki (4–24)", py::get(saved, "segments", 12)}};
    for (auto& spec : specs) shape_vars.push_back({spec.key, app->field(form, spec.label, spec.value)});
    shape_edge = tk::BooleanVar(p.contour_edge || std::all_of(p.edges.begin(), p.edges.end(), [](bool e) { return e; }));
    (new ttk::Checkbutton(form, {{"text", "Obrzeże całego obrysu — obwód i koszt"}, {"variable", shape_edge}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 8}});
    (new ttk::Label(form, {{"text", "Dla obrysu krzywego formatka do rozkroju pozostaje nominalną obwiednią U/V; nie jest automatycznie pomniejszana o obrzeże."},
                           {"wraplength", 360}, {"padding", 15}}))->pack({{"fill", "x"}});
    shape_flute = tk::BooleanVar(py::get(p.decor, "type") == json("fluting"));
    (new ttk::Checkbutton(form, {{"text", "Ryflowanie parametryczne"}, {"variable", shape_flute}}))->pack({{"anchor", "w"}, {"padx", 15}, {"pady", 8}});
    std::vector<Spec> flute_specs{{"flute_width", "Szerokość rowka [mm]", py::get(p.decor, "width", 8)},
                                  {"flute_pitch", "Podziałka rowków [mm]", py::get(p.decor, "pitch", 24)},
                                  {"flute_depth", "Głębokość rowka [mm]", py::get(p.decor, "depth", std::min(3.0, p.t * .2))},
                                  {"flute_margin_u", "Margines po bokach [mm]", py::get(p.decor, "margin_u", p.u * .1)},
                                  {"flute_margin_v", "Margines dół / góra [mm]", py::get(p.decor, "margin_v", p.v * .05)}};
    for (auto& spec : flute_specs) shape_vars.push_back({spec.key, app->field(form, spec.label, spec.value)});
    shape_flute_face = tk::StringVar(py::str(py::get(p.decor, "face", "bottom")));
    (new ttk::Label(form, {{"text", "Strona rowków (bottom = dolna / przód frontu)"}, {"wraplength", 360}, {"padding", 15}}))->pack({{"fill", "x"}});
    (new ttk::Combobox(form, {{"textvariable", shape_flute_face}, {"values", std::vector<std::string>{"bottom", "top"}}, {"state", "readonly"}}))
        ->pack({{"fill", "x"}, {"padx", 15}, {"pady", 5}});
    (new ttk::Label(form, {{"text", "Podpowiedź grubości materiału"}, {"padding", "15 6"}}))->pack({{"anchor", "w"}});
    tk::StringVar kind("Płyta meblowa"), stock(py::str(json(p.t)));
    std::vector<std::string> kinds;
    for (auto& [name, values] : THICKNESSES) kinds.push_back(name);
    auto* stock_kind = new ttk::Combobox(form, {{"textvariable", kind}, {"values", kinds}, {"state", "readonly"}});
    stock_kind->pack({{"fill", "x"}, {"padx", 15}});
    auto* stock_pick = new ttk::Combobox(form, {{"textvariable", stock}, {"values", as_strings(thicknesses(kind.get()))}, {"state", "readonly"}});
    stock_pick->pack({{"fill", "x"}, {"padx", 15}, {"pady", 5}});
    stock_kind->bind("<<ComboboxSelected>>", [stock_pick, kind](tk::Event&) { stock_pick->configure({{"values", as_strings(thicknesses(kind.get()))}}); return std::string(); });
    stock_pick->bind("<<ComboboxSelected>>", [this, stock](tk::Event&) { shape_var("t").set(stock.get()); return std::string(); });
    (new ttk::Label(form, {{"text", "Wartości są listą typowych grubości do wyboru. Dostępność i rzeczywistą grubość potwierdź u dostawcy."}, {"wraplength", 360}, {"padding", 15}}))
        ->pack({{"fill", "x"}});
    (new ttk::Label(form, {{"text", shaped_furniture::CURVE_NOTE}, {"wraplength", 360}, {"padding", 15}}))->pack({{"fill", "x"}});
    (new ttk::Label(form, {{"text", "R1…R4 odnoszą się do obrysu widocznego na szkicu. Otwory, rowki i okucia zostają zachowane; program sprawdza, czy mieszczą się w nowym kształcie."},
                           {"wraplength", 360}, {"padding", 15}}))->pack({{"fill", "x"}});
    tk::StringVar error;
    (new ttk::Label(footer, {{"textvariable", error}, {"foreground", "#a33232"}, {"wraplength", 940}}))->pack({{"fill", "x"}});

    struct Parsed { std::map<std::string, double> val; std::string which; json opts; };
    auto parameters = [this, shape]() {
        Parsed out;
        for (auto& [key, var] : shape_vars) out.val[key] = app->number(var);
        out.opts = json::object();
        out.opts["radii"] = json::array({out.val.at("r0"), out.val.at("r1"), out.val.at("r2"), out.val.at("r3")});
        out.opts["segments"] = out.val.at("segments");
        out.opts["arch_rise"] = out.val.at("rise");
        out.opts["taper"] = out.val.at("taper");
        out.which = shape_kind(shape.get());
        return out;
    };

    auto pending = std::make_shared<std::optional<std::string>>();
    auto preview = [this, canvas, pending, summary, error, shape, parameters]() mutable {
        pending->reset();
        canvas->delete_("all");
        try {
            Parsed parsed = parameters();
            const auto& val = parsed.val;
            double u = val.at("u"), v = val.at("v"), t = val.at("t");
            std::vector<std::vector<double>> pts = shaped_furniture::contour(json(parsed.which), u, v, parsed.opts);
            if (pts.empty()) pts = {{0, 0}, {u, 0}, {u, v}, {0, v}};
            int w = std::max(100, canvas->winfo_width()), h = std::max(100, canvas->winfo_height());
            double scale = std::min(divide(w - 70, u), divide(h - 90, v));
            double cx = w / 2.0, cy = (h - 35) / 2.0;
            std::vector<double> flat;
            for (auto& pt : pts) {
                flat.push_back(cx + (pt[0] - u / 2) * scale);
                flat.push_back(cy - (pt[1] - v / 2) * scale);
            }
            canvas->create_polygon(flat, {{"fill", app->project->mat(p.material).color}, {"outline", "#2f4a26"}, {"width", 2}});
            canvas->create_text(w / 2.0, h - 24.0, {{"text", std::format("U = {} mm", g(u))}, {"font", "{Segoe UI} 11 bold"}, {"fill", "#2f4a26"}});
            canvas->create_text(12, 15, {{"anchor", "nw"}, {"text", std::format("V = {} mm\nT = {} mm", g(v), g(t))}, {"fill", "#2f4a26"}});
            struct Corner { double x, y; const char* label; };
            for (const Corner& corner : {Corner{0, 0, "R1"}, Corner{u, 0, "R2"}, Corner{u, v, "R3"}, Corner{0, v, "R4"}})
                canvas->create_text(cx + (corner.x - u / 2) * scale, cy - (corner.y - v / 2) * scale,
                                    {{"text", corner.label}, {"fill", "#4e6b3f"}, {"anchor", corner.y != 0 ? "sw" : "nw"}});
            double exact = shaped_furniture::profile_area(shaped_furniture::shape_definition(json(parsed.which), u, v, parsed.opts), u, v);
            summary.set(std::format("{} · pole {:.4f} m² · {} punktów podglądu.\nParametry obrysu są zapisane; STEP ma dokładne krzywe.", shape.get(), exact / 1e6, pts.size()));
            error.set("");
        } catch (const std::invalid_argument& e) {  // ValueError
            error.set(e.what());
            summary.set("Popraw parametry, aby zobaczyć kształt.");
        } catch (const std::out_of_range& e) {  // KeyError
            error.set(e.what());
            summary.set("Popraw parametry, aby zobaczyć kształt.");
        } catch (const std::domain_error& e) {  // ZeroDivisionError
            error.set(e.what());
            summary.set("Popraw parametry, aby zobaczyć kształt.");
        } catch (const py::StopIteration& e) {
            error.set(e.what());
            summary.set("Popraw parametry, aby zobaczyć kształt.");
        }
    };

    auto schedule = [this, pending, shape, preview]() mutable {
        if (shape_kind(shape.get()) == "circle" && shape_var("v").get() != shape_var("u").get()) shape_var("v").set(shape_var("u").get());
        if (*pending) after_cancel(**pending);
        *pending = after(100, preview);
    };
    shape.trace_add("write", Cmd(schedule));
    for (auto& [key, var] : shape_vars) var.trace_add("write", Cmd(schedule));
    canvas->bind("<Configure>", [schedule](tk::Event&) mutable { schedule(); return std::string(); });

    shape_apply = [this, parameters]() {
        ensure_current_project(true);
        Parsed parsed = parameters();
        const auto& val = parsed.val;
        auto source = std::make_shared<core::Project>(*app->project);
        auto find = [&](core::Project& project) -> core::Part& {
            for (auto& q : project.parts)
                if (q.id == p.id) return q;
            throw py::StopIteration();
        };
        shaped_furniture::remove_decor(find(*source));
        core::ProjectPtr result = shaped_furniture::apply_shape(*source, p.id, parsed.which, val.at("u"), val.at("v"), val.at("t"), parsed.opts);
        core::Part& q = find(*result);
        q.contour_edge = !q.contour.empty() ? shape_edge.get() : false;
        if (q.contour.empty()) q.edges = std::vector<bool>(4, shape_edge.get());
        if (shape_flute.get()) {
            q.decor = json::object();
            q.decor["type"] = "fluting";
            q.decor["width"] = val.at("flute_width");
            q.decor["pitch"] = val.at("flute_pitch");
            q.decor["depth"] = val.at("flute_depth");
            q.decor["margin_u"] = val.at("flute_margin_u");
            q.decor["margin_v"] = val.at("flute_margin_v");
            q.decor["face"] = shape_flute_face.get();
            shaped_furniture::refresh_decor(q);
        } else shaped_furniture::remove_decor(q);
        result->validate();
        commit(app, result);
        app->selected = p.id;
        app->refresh();
        destroy();
    };
    auto* buttons = new ttk::Frame(footer);
    buttons->pack({{"fill", "x"}, {"pady", "8 0"}});
    shape_submit_button = new ttk::Button(buttons, {{"text", "Zapisz obrys w elemencie"}, {"command", guarded(app, shape_apply)}, {"style", "Accent.TButton"}});
    shape_submit_button->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Button(buttons, {{"text", "Zamknij"}, {"command", Cmd([this] { request_close(); })}}))->pack({{"side", "right"}, {"padx", "10 0"}});
    shape_canvas = canvas;
    schedule();
}

ShapeDialog* shape_dialog(App* app) {
    core::Part* p = app->selected_part();
    if (p == nullptr) throw ValueError("Zaznacz element, którego obrys chcesz zmienić.");
    return new ShapeDialog(app, *p);
}

// ---------------------------------------------------------------- style_library

StyleLibrary::StyleLibrary(App* app_) : dock_workspace::DockDocument(app_) {
    StyleLibrary* d = this;
    d->title("Meble obłe, drewniane i style");
    d->geometry("1020x750+40+40");
    (new ttk::Label(d, {{"text", "Biblioteka mebli obłych"}, {"style", "Title.TLabel"}, {"padding", 12}}))->pack({{"anchor", "w"}});
    auto* footer = new ttk::Frame(d, {{"padding", 10}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}});
    auto* content = new ttk::Frame(d);
    content->pack({{"fill", "both"}, {"expand", true}, {"padx", 10}});
    auto* left = new ttk::Frame(content);
    left->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}, {"padx", "0 10"}});
    auto* filterbox = new ttk::Frame(left);
    filterbox->pack({{"fill", "x"}, {"pady", "0 8"}});
    tk::StringVar style("Wszystkie"), search;
    std::vector<std::string> styles{"Wszystkie"};
    for (auto& t : TEMPLATES)
        if (std::find(styles.begin(), styles.end(), t.style) == styles.end()) styles.push_back(t.style);
    (new ttk::Combobox(filterbox, {{"textvariable", style}, {"values", styles}, {"state", "readonly"}, {"width", 18}}))->pack({{"side", "left"}});
    (new ttk::Entry(filterbox, {{"textvariable", search}}))->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}, {"padx", "8 0"}});
    (new ttk::Label(left, {{"text", "Filtruj stylem lub wpisz fragment nazwy."}, {"padding", "0 3"}}))->pack({{"anchor", "w"}});
    auto* tablebox = new ttk::Frame(left);
    tablebox->pack({{"fill", "both"}, {"expand", true}});
    auto* table = new ttk::Treeview(tablebox, {{"columns", std::vector<std::string>{"name", "style"}}, {"show", "headings"}, {"height", 12}});
    table->heading("name", {{"text", "Mebel"}});
    table->heading("style", {{"text", "Styl"}});
    table->column("name", {{"width", 300}});
    table->column("style", {{"width", 120}});
    table->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* sb = new ttk::Scrollbar(tablebox, {{"orient", "vertical"}, {"command", table->path() + " yview"}});
    sb->pack({{"side", "right"}, {"fill", "y"}});
    table->configure({{"yscrollcommand", sb->path() + " set"}});
    tk::StringVar note;
    (new ttk::Label(left, {{"textvariable", note}, {"wraplength", 560}, {"padding", "0 10"}}))->pack({{"fill", "x"}});
    (new ttk::Label(left, {{"text", shaped_furniture::CURVE_NOTE + " Wzorce krzeseł, nóg i ławek wymagają własnego doboru połączeń oraz oceny konstrukcji."},
                           {"wraplength", 560}, {"padding", "0 8"}}))->pack({{"fill", "x"}});
    auto [outer, form] = scroll_form(content, 360);
    outer->pack({{"side", "right"}, {"fill", "y"}});
    struct Spec { const char* key; const char* label; json value; };
    for (const Spec& spec : std::vector<Spec>{{"name", "Nazwa mebla", "Nowy mebel"}, {"w", "Szerokość X [mm]", 1600}, {"h", "Wysokość Z [mm]", 760},
                                              {"d", "Głębokość Y [mm]", 850}, {"t", "Grubość blatu / materiału [mm]", 28}, {"segments", "Odcinki ćwiartki (4–24)", 12},
                                              {"x", "Pozycja X [mm]", 0}, {"y", "Pozycja Y [mm]", 0}, {"z", "Pozycja Z [mm]", 0}})
        style_vars.insert_or_assign(spec.key, app->field(form, spec.label, spec.value));
    std::vector<std::string> material_names;
    for (auto& m : app->project->materials) material_names.push_back(m.name);
    (new ttk::Label(form, {{"text", "Materiał konstrukcji"}, {"padding", "15 5"}}))->pack({{"anchor", "w"}});
    auto* mat = new ttk::Combobox(form, {{"values", material_names}, {"state", "readonly"}});
    mat->pack({{"fill", "x"}, {"padx", 15}});
    {
        int index = 0;
        for (size_t i = 0; i < app->project->materials.size(); ++i)
            if (app->project->materials[i].kind == "drewno") { index = (int)i; break; }
        mat->current(index);
    }
    (new ttk::Label(form, {{"text", "Materiał frontów"}, {"padding", "15 5"}}))->pack({{"anchor", "w"}});
    auto* fronts = new ttk::Combobox(form, {{"values", material_names}, {"state", "readonly"}});
    fronts->pack({{"fill", "x"}, {"padx", 15}});
    fronts->current(mat->current());
    (new ttk::Label(form, {{"text", "Wymiary są parametrami wzorca. Po utworzeniu każdy element ma własny szkic, materiał i wymiary w projekcie."}, {"wraplength", 325}, {"padding", 15}}))
        ->pack({{"fill", "x"}});

    auto choose = [this, table, note]() mutable {
        if (table->selection().empty()) return;
        const auto& item = template_at(py::to_int(table->selection()[0]));
        style_vars.at("name").set(item.name);
        style_vars.at("w").set(g(item.width));
        style_vars.at("h").set(g(item.height));
        style_vars.at("d").set(g(item.depth));
        note.set(item.category + " · " + item.style + "\n" + item.description);
    };
    auto refresh = [table, style, search, note, choose]() mutable {
        table->delete_(table->get_children());
        for (size_t i = 0; i < TEMPLATES.size(); ++i) {
            const auto& item = TEMPLATES[i];
            if ((style.get() == "Wszystkie" || style.get() == item.style) &&
                casefold(item.category + " " + item.name + " " + item.style).find(casefold(search.get())) != std::string::npos)
                table->insert("", "end", {{"iid", std::to_string(i)}, {"values", std::vector<std::string>{item.name, item.style}}});
        }
        if (!table->get_children().empty()) {
            table->selection_set(table->get_children()[0]);
            choose();
        } else note.set("Brak wzorców pasujących do filtra.");
    };
    table->bind("<<TreeviewSelect>>", [choose](tk::Event&) mutable { choose(); return std::string(); });
    style.trace_add("write", Cmd(refresh));
    search.trace_add("write", Cmd(refresh));
    style_add = [this, table, mat, fronts]() {
        ensure_current_project(true);
        if (table->selection().empty()) throw ValueError("Wybierz wzorzec mebla.");
        long long index = py::to_int(table->selection()[0]);
        double w = app->number(style_vars.at("w")), h = app->number(style_vars.at("h")), dd = app->number(style_vars.at("d")), t = app->number(style_vars.at("t"));
        core::Point3 origin{app->number(style_vars.at("x")), app->number(style_vars.at("y")), app->number(style_vars.at("z"))};
        std::string group = style_vars.at("name").get();
        std::string material = app->project->materials.at((size_t)mat->current()).id;
        std::string front = app->project->materials.at((size_t)fronts->current()).id;
        double segments = app->number(style_vars.at("segments"));
        auto [result, group_name] = shaped_furniture::create_shaped_furniture(*app->project, index, w, h, dd, t, origin, group, material, front, segments);
        commit(app, result);
        auto chosen = std::find_if(app->project->parts.begin(), app->project->parts.end(), [&, g = group_name](const core::Part& q) { return q.group == g; });
        if (chosen == app->project->parts.end()) throw py::StopIteration();
        app->selected = chosen->id;
        app->refresh();
        destroy();
        app->workspace->select(app->design_page);
    };
    style_create_button = new ttk::Button(footer, {{"text", "Utwórz mebel o wybranych wymiarach"}, {"command", guarded(app, style_add)}, {"style", "Accent.TButton"}});
    style_create_button->pack({{"side", "left"}, {"fill", "x"}, {"expand", true}});
    (new ttk::Button(footer, {{"text", "Zamknij"}, {"command", Cmd([this] { request_close(); })}}))->pack({{"side", "right"}, {"padx", "10 0"}});
    style_table = table;
    refresh();
}

StyleLibrary* style_library(App* app) { return new StyleLibrary(app); }

// ---------------------------------------------------------------- menu

void install_menu(App* app) {
    auto* bar = dynamic_cast<tk::Menu*>(app->nametowidget(app->cget("menu")));
    auto* menu = new tk::Menu(bar, {{"tearoff", false}});
    bar->add_cascade({{"label", "Kształty i style"}, {"menu", menu}});
    menu->add_command({{"label", "Obrys: koło, elipsa, łuk, narożniki…"}, {"command", guarded(app, [app] { shape_dialog(app); })}});
    menu->add_command({{"label", "Biblioteka mebli obłych i drewnianych…"}, {"command", guarded(app, [app] { style_library(app); })}});
    app->shape_dialog = [app] { guarded(app, [app] { shape_dialog(app); })(); };
    app->style_library = [app] { guarded(app, [app] { style_library(app); })(); };
}

}  // namespace style_ui
