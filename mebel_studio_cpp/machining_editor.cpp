// Edycja obróbki części: kieszenie, rowki, wycięcia i szyki wierceń.
#include "machining_editor.h"

#include <algorithm>
#include <format>
#include <memory>

#include "app.h"
#include "features.h"
#include "pyutil.h"

namespace machining_editor {

using py::ValueError;

namespace {

// f'{x:g}' dla liczby z JSON-a (KeyError/TypeError jak w Pythonie przy braku)
std::string g(const json& value) { return std::format("{:g}", value.get<double>()); }

// FACES[label]
const std::string& face_of(const std::string& label) {
    for (const auto& [k, v] : features::FACES) if (k == label) return v;
    throw py::KeyError(label);
}

template <class T>
const T& lookup(const std::vector<std::pair<std::string, T>>& table, const std::string& key) {
    for (const auto& [k, v] : table) if (k == key) return v;
    throw py::KeyError(key);
}

double value_of(const std::vector<std::pair<std::string, double>>& values, const std::string& key) {
    for (const auto& [k, v] : values) if (k == key) return v;
    throw py::KeyError(key);
}

// str(min(a, b)) z całkowitym a: Python zwraca b tylko gdy b < a
std::string min_text(int a, double b) { return b < a ? py::str(json(b)) : std::to_string(a); }

bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

int index_after_colon(const std::string& item) { return static_cast<int>(py::to_int(item.substr(item.find(':') + 1))); }

}  // namespace

MachiningEditor::MachiningEditor(App* app_, const core::Part& part_) : dock_workspace::DockDocument(app_), part(part_) {
    editing_cut.reset();
    title("Obróbka części — " + part_.name);
    geometry(std::format("1100x{}+15+15", std::min(760, app->winfo_screenheight() - 100)));
    minsize(950, 600);
    transient(app); grab_set();
    (new ttk::Label(this, {{"text", "Kieszenie, rowki, wycięcia i szyki wierceń. Wymiary w lokalnym układzie części, w mm.\nFrezowanie i wiercenia krawędziowe wymagają modułu CAD. To geometria obróbki, bez ścieżek narzędzia."},
                           {"padding", 10}}))->pack({{"fill", "x"}});
    auto* body = new ttk::Frame(this);
    body->pack({{"fill", "both"}, {"expand", true}});
    controls = new ttk::Notebook(body);
    controls->pack({{"side", "left"}, {"fill", "y"}, {"padx", 8}});
    auto* cut = new ttk::Frame(controls, {{"padding", 10}});
    auto* drill = new ttk::Frame(controls, {{"padding", 10}});
    auto* history = new ttk::Frame(controls, {{"padding", 10}});
    cut_tab = cut;
    controls->add(cut, {{"text", "Frezowanie"}});
    controls->add(drill, {{"text", "Wiercenia"}});
    controls->add(history, {{"text", "Operacje"}});
    auto combo = [](tk::Widget* parent, const std::string& label, const std::vector<std::string>& values, const std::string& default_) {
        (new ttk::Label(parent, {{"text", label}}))->pack({{"anchor", "w"}});
        tk::StringVar var(default_);
        (new ttk::Combobox(parent, {{"textvariable", var}, {"values", values}, {"state", "readonly"}, {"width", 33}}))->pack({{"fill", "x"}, {"pady", "0 5"}});
        return var;
    };
    kind = combo(cut, "Rodzaj", {"Kieszeń", "Wycięcie przelotowe", "Rowek"}, "Kieszeń");
    shape = combo(cut, "Kształt", {"Prostokąt", "Koło", "Otwór podłużny"}, "Prostokąt");
    cut_face = combo(cut, "Strona", {"Góra (+T)", "Dół (−T)"}, "Góra (+T)");
    struct Row { const char* key; const char* label; std::string value; };
    auto fields = [](tk::Widget* parent, std::vector<std::pair<std::string, tk::StringVar>>& container, const std::vector<Row>& rows) {
        for (const auto& row_ : rows) {
            auto* row = new ttk::Frame(parent);
            row->pack({{"fill", "x"}, {"pady", 3}});
            (new ttk::Label(row, {{"text", row_.label}, {"width", 23}}))->pack({{"side", "left"}});
            tk::StringVar var(row_.value);
            container.emplace_back(row_.key, var);
            (new ttk::Entry(row, {{"textvariable", var}, {"width", 10}}))->pack({{"side", "right"}});
        }
    };
    fields(cut, cut_vars, {{"u", "Środek U", py::str(json(part_.u / 2))}, {"v", "Środek V", py::str(json(part_.v / 2))},
                           {"length", "Długość", min_text(100, part_.u / 2)}, {"width", "Szerokość / średnica", min_text(50, part_.v / 2)},
                           {"depth", "Głębokość", min_text(6, part_.t / 2)}, {"angle", "Kąt w płaszczyźnie [°]", "0"}});
    (new ttk::Label(cut, {{"text", "Koło: średnica w polu szerokości.\nPodłużny: długość całkowita, końce półokrągłe.\nRowek: ustaw długość równą formatce, jeśli ma\ndochodzić do obu brzegów."}, {"wraplength", 320}}))
        ->pack({{"anchor", "w"}, {"pady", 8}});
    (new ttk::Button(cut, {{"text", "Dodaj obróbkę / podgląd"}, {"command", std::function<void()>([this] { add_cut(); })}}))->pack({{"fill", "x"}});
    std::vector<std::string> face_labels;
    for (const auto& [k, v] : features::FACES) face_labels.push_back(k);
    drill_face = combo(drill, "Powierzchnia", face_labels, "Góra (+T)");
    fields(drill, drill_vars, {{"u", "Pierwsza współrzędna", min_text(37, part_.u / 2)}, {"v", "Druga współrzędna", min_text(37, part_.v / 2)}, {"diameter", "Średnica", "5"},
                               {"depth", "Głębokość", min_text(10, part_.t / 2)}, {"nu", "Liczba w kierunku 1", "1"}, {"nv", "Liczba w kierunku 2", "1"},
                               {"du", "Krok kierunku 1", "32"}, {"dv", "Krok kierunku 2", "32"}});
    (new ttk::Label(drill, {{"text", "Na +T/−T: współrzędne U,V.\nNa U=0/max: współrzędne V,T.\nNa V=0/max: współrzędne U,T.\nGłębokość liczona od wskazanej powierzchni.\nRozstaw 32 mm jest edytowalny."}, {"wraplength", 320}}))
        ->pack({{"anchor", "w"}, {"pady", 8}});
    (new ttk::Button(drill, {{"text", "Dodaj szyk otworów / podgląd"}, {"command", std::function<void()>([this] { add_drills(); })}}))->pack({{"fill", "x"}});
    table = new ttk::Treeview(history, {{"columns", std::vector<std::string>{"description"}}, {"show", "headings"}, {"height", 20}, {"selectmode", "extended"}});
    table->heading("description", {{"text", "Operacja"}});
    table->column("description", {{"width", 310}});
    table->pack({{"fill", "both"}, {"expand", true}});
    auto* scroll = new ttk::Scrollbar(history, {{"orient", "vertical"}, {"command", table->path() + " yview"}});
    scroll->pack({{"side", "right"}, {"fill", "y"}});
    table->configure({{"yscrollcommand", scroll->path() + " set"}});
    (new ttk::Button(history, {{"text", "Edytuj wybrane frezowanie"}, {"command", std::function<void()>([this] { edit_cut(); })}}))->pack({{"fill", "x"}, {"pady", 4}});
    (new ttk::Button(history, {{"text", "Usuń wybrane ręczne operacje"}, {"command", std::function<void()>([this] { delete_selected(); })}}))->pack({{"fill", "x"}, {"pady", 8}});
    canvas = new tk::Canvas(body, {{"bg", "#eef1f5"}, {"highlightthickness", 0}});
    canvas->pack({{"fill", "both"}, {"expand", true}, {"padx", 8}});
    canvas->bind("<Configure>", [this](tk::Event&) { draw(); return std::string(); });
    auto* footer = new ttk::Frame(this, {{"padding", 8}});
    footer->pack({{"side", "bottom"}, {"fill", "x"}, {"before", body->path()}});
    status = tk::StringVar();
    (new ttk::Label(footer, {{"textvariable", status}}))->pack({{"side", "left"}});
    (new ttk::Button(footer, {{"text", "Zapisz obróbkę do projektu"}, {"command", std::function<void()>([this] { save(); })}, {"style", "Accent.TButton"}}))->pack({{"side", "right"}});
    refresh();
}

void MachiningEditor::edit_cut() {
    auto items = table->selection();
    if (items.size() != 1 || !starts_with(items[0], "cut:")) {
        tk::messagebox::showinfo("Obróbka", "Wybierz jedno frezowanie na liście.", this);
        return;
    }
    editing_cut = index_after_colon(items[0]);
    if (*editing_cut >= static_cast<int>(part.machining.size())) throw py::IndexError("list index out of range");
    json cut = part.machining[static_cast<size_t>(*editing_cut)];
    for (auto& [key, var] : cut_vars) var.set(py::str(py::get(cut, key, 0)));
    static const std::vector<std::pair<std::string, std::string>> kinds = {{"pocket", "Kieszeń"}, {"cutout", "Wycięcie przelotowe"}, {"groove", "Rowek"}};
    static const std::vector<std::pair<std::string, std::string>> shapes = {{"rect", "Prostokąt"}, {"circle", "Koło"}, {"slot", "Otwór podłużny"}};
    kind.set(lookup(kinds, cut.at("type").get<std::string>()));
    shape.set(lookup(shapes, cut.at("shape").get<std::string>()));
    std::string face = cut.at("face").get<std::string>();
    bool found = false;
    for (const auto& [label, code] : features::FACES) if (code == face) { cut_face.set(label); found = true; break; }
    if (!found) throw py::StopIteration();
    controls->select(cut_tab);
}

std::vector<std::pair<std::string, double>> MachiningEditor::values(const std::vector<std::pair<std::string, tk::StringVar>>& variables) {
    std::vector<std::pair<std::string, double>> out;
    for (const auto& [k, v] : variables) out.emplace_back(k, core::finite(py::replace_all(v.get(), ",", ".")));
    return out;
}

void MachiningEditor::add_cut() {
    try {
        auto vals = values(cut_vars);
        static const std::vector<std::pair<std::string, std::string>> kinds = {{"Kieszeń", "pocket"}, {"Wycięcie przelotowe", "cutout"}, {"Rowek", "groove"}};
        static const std::vector<std::pair<std::string, std::string>> shapes = {{"Prostokąt", "rect"}, {"Koło", "circle"}, {"Otwór podłużny", "slot"}};
        json cut = {{"type", lookup(kinds, kind.get())}, {"shape", lookup(shapes, shape.get())}, {"face", face_of(cut_face.get())}};
        for (const auto& [k, v] : vals) cut[k] = v;
        core::Part candidate = part;
        if (!editing_cut) {
            candidate.machining.push_back(cut);
        } else {
            if (*editing_cut >= static_cast<int>(candidate.machining.size())) throw py::IndexError("list assignment index out of range");
            candidate.machining[static_cast<size_t>(*editing_cut)] = cut;
        }
        candidate.validate();
        editing_cut.reset();
        part = candidate;
        refresh();
        draw();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Obróbka", e.what(), this);
    }
}

void MachiningEditor::add_drills() {
    try {
        auto vals = values(drill_vars);
        for (const char* k : {"nu", "nv"}) {
            double x = value_of(vals, k);
            if (x != std::trunc(x)) throw ValueError("Liczby otworów muszą być całkowite.");
        }
        core::Part candidate = part;
        json made = features::hole_array(candidate, value_of(vals, "u"), value_of(vals, "v"), value_of(vals, "diameter"), value_of(vals, "depth"),
                                         value_of(vals, "nu"), value_of(vals, "nv"), value_of(vals, "du"), value_of(vals, "dv"), face_of(drill_face.get()));
        for (const auto& op : made) candidate.operations.push_back(op);
        candidate.validate();
        part = candidate;
        refresh();
        draw();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Wiercenia", e.what(), this);
    }
}

void MachiningEditor::refresh() {
    table->delete_(table->get_children());
    int i = 0;
    for (const auto& cut : part.machining) {
        table->insert("", "end", {{"iid", "cut:" + std::to_string(i)},
                                  {"values", std::vector<std::string>{cut.at("type").get<std::string>() + " " + cut.at("shape").get<std::string>() + " · " + cut.at("face").get<std::string>() +
                                                                      " · " + g(cut.at("u")) + "," + g(cut.at("v"))}}});
        ++i;
    }
    i = 0;
    for (const auto& op : part.operations) {
        table->insert("", "end", {{"iid", "drill:" + std::to_string(i)},
                                  {"values", std::vector<std::string>{std::string(py::truthy(py::get(op, "owner_id")) ? "Auto" : "Otwór") + " Ø" + g(op.at("diameter")) + " · " +
                                                                      op.at("face").get<std::string>() + " · " + g(op.at("u")) + "," + g(op.at("v"))}}});
        ++i;
    }
    status.set(std::to_string(part.machining.size()) + " frezowań · " + std::to_string(part.operations.size()) + " wierceń");
}

void MachiningEditor::delete_selected() {
    try {
        auto items = table->selection();
        core::Part candidate = part;
        for (const auto& item : items) {
            if (!starts_with(item, "drill:")) continue;
            int index = index_after_colon(item);
            if (index >= static_cast<int>(candidate.operations.size())) throw py::IndexError("list index out of range");
            if (py::truthy(py::get(candidate.operations[static_cast<size_t>(index)], "owner_id")))
                throw ValueError("Automatyczne otwory okuć edytuj przez parametry montażu w szkicowniku.");
        }
        for (const std::string prefix : {"cut", "drill"}) {
            json& collection = prefix == "cut" ? candidate.machining : candidate.operations;
            std::vector<int> indices;
            for (const auto& item : items) if (starts_with(item, prefix + ":")) indices.push_back(index_after_colon(item));
            std::sort(indices.begin(), indices.end(), std::greater<int>());
            for (int index : indices) {
                if (index < 0 || index >= static_cast<int>(collection.size())) throw py::IndexError("pop index out of range");
                collection.erase(static_cast<size_t>(index));
            }
        }
        candidate.validate();
        editing_cut.reset();
        part = candidate;
        refresh();
        draw();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Operacje", e.what(), this);
    }
}

void MachiningEditor::draw() {
    canvas->delete_("all");
    int w = canvas->winfo_width(), h = canvas->winfo_height();
    if (w < 10) return;
    core::Project project = *app->project;
    project.parts = {part};
    core::Camera camera;
    camera.fit(project, w, h);
    for (const auto& face : core::render_faces(project, camera, w, h)) {
        std::vector<double> flat;
        for (const auto& p : face.at("points")) for (const auto& value : p) flat.push_back(value.get<double>());
        bool mesh = py::truthy(py::get(face, "mesh"));
        std::string color = face.at("color").get<std::string>();
        canvas->create_polygon(flat, {{"fill", color}, {"outline", mesh ? color : std::string("#66727f")}, {"width", mesh ? 0 : 1}});
    }
}

void MachiningEditor::save() {
    try {
        core::ProjectPtr result = features::replace_part(*app->project, part);
        app->snapshot();
        app->project = result;
        app->refresh();
        app->fit();
        destroy();
    } catch (const ValueError& e) {
        tk::messagebox::showerror("Zapis obróbki", e.what(), this);
    }
}

}  // namespace machining_editor
