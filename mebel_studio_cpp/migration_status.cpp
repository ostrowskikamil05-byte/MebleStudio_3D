// Read-only diagnostics for the ongoing, explicitly staged C++ migration.
#include "migration_status.h"

#include <algorithm>
#include <map>

#include "app.h"
#include "native_model.h"
#include "pyutil.h"

namespace migration_status {

namespace {

struct Hooks {
    std::function<void()> refresh;
    ttk::Treeview* table = nullptr;
};

std::map<form_dialog::Form*, Hooks> g_hooks;

}  // namespace

std::function<void()> refresh_cpp_status(form_dialog::Form* dialog) { return g_hooks.at(dialog).refresh; }

ttk::Treeview* cpp_status_table(form_dialog::Form* dialog) { return g_hooks.at(dialog).table; }

form_dialog::Form* open_status(App* app) {
    using json = core::json;
    if (app->_cpp_status_document != nullptr) {
        auto* existing = app->_cpp_status_document;
        try {
            if (existing->winfo_exists()) { app->workspace->select(existing->document); return existing; }
        } catch (const tk::TclError&) {
        }
    }
    form_dialog::Form* d = app->dialog("Silnik C++ i stan migracji", "950x650");
    app->_cpp_status_document = d;
    (new ttk::Label(d, {{"text", "Przenoszenie programu do C++"}, {"style", "Title.TLabel"}, {"padding", 12}}))->pack({{"fill", "x"}});
    (new ttk::Label(d, {{"text", "Obliczenia wymienione poniżej są już dostępne w natywnej bibliotece. Interfejs oraz dokładna walidacja CAD zachowują dotychczasowe moduły. Cały program pozostaje na etapie migracji."},
                        {"wraplength", 850}, {"padding", "12 0 12 8"}}))->pack({{"fill", "x"}});
    auto note = std::make_shared<tk::StringVar>();
    (new ttk::Label(d, {{"textvariable", *note}, {"padding", 12}, {"wraplength", 850}}))->pack({{"fill", "x"}});
    auto* frame = new ttk::Frame(d, {{"padding", 12}});
    frame->pack({{"fill", "both"}, {"expand", true}});
    auto* table = new ttk::Treeview(frame, {{"columns", std::vector<std::string>{"module", "native", "remaining"}}, {"show", "headings"}});
    struct Column { const char* key; const char* title; int width; };
    for (const Column& c : {Column{"module", "Moduł", 235}, Column{"native", "Operacje dostępne w C++", 245}, Column{"remaining", "Pozostała część modułu", 350}}) {
        table->heading(c.key, {{"text", c.title}});
        table->column(c.key, {{"width", c.width}, {"stretch", true}});
    }
    auto* scroll = new ttk::Scrollbar(frame, {{"orient", "vertical"}, {"command", table->path() + " yview"}});
    table->configure({{"yscrollcommand", scroll->path() + " set"}});
    scroll->pack({{"side", "right"}, {"fill", "y"}});
    table->pack({{"fill", "both"}, {"expand", true}});
    auto* details = new tk::Text(d, {{"height", 7}, {"wrap", "word"}, {"font", "Consolas 9"}, {"state", "disabled"}});
    details->pack({{"fill", "x"}, {"padx", 12}, {"pady", 6}});
    auto manifest_path = py::executable_dir() / "MIGRACJA_CPP.json";
    auto refresh = [d, note, table, details, manifest_path, app] {
        json info = native_model::diagnostics();
        json manifest = json::parse(py::read_text(py::path_str(manifest_path)));
        table->delete_(table->get_children());
        for (auto& module : manifest.at("modules")) {
            json operations = py::get(module, "cpp_operations", json::array());
            std::vector<std::string> names;
            for (auto& op : operations) names.push_back(op.get<std::string>());
            table->insert("", "end", {{"values", std::vector<std::string>{py::str(module.at("name")), std::to_string(names.size()) + ": " + py::join(names, ", "), py::str(module.at("remaining"))}}});
        }
        note->set("C++20 · biblioteka aktywna · " + py::str(info.at("calls")) + " wywołań natywnych · " + py::str(info.at("batch_transforms")) +
                  " transformacji części · " + py::str(info.at("transformed_vertices")) + " wierzchołków");
        details->configure({{"state", "normal"}});
        details->delete_("1.0", "end");
        std::vector<std::string> routed;
        for (auto& [key, value] : info.at("routed_functions").items()) routed.push_back(key);
        std::sort(routed.begin(), routed.end());
        std::string text = "Biblioteka: " + py::str(info.at("library")) + "\nFunkcje wykonane w tej sesji: " + py::join(routed, ", ");
        if (app->_native_document != nullptr) {
            json state = app->_native_document->info();
            text += "\nDokument edytora C++: wersja " + py::str(state.at("revision")) + " · Cofnij: " + py::str(state.at("undo_count")) + " · Ponów: " + py::str(state.at("redo_count"));
            text += "\nPełny dokument: " + py::str(state.at("byte_length")) + " bajtów · historia: " + py::str(state.at("history_bytes")) + " bajtów";
            text += "\nWalidacja dokładnej geometrii CAD i generatory mebli pozostają w zachowanych modułach.";
        }
        auto sum_prefix = [&](const std::string& prefix) {
            long long total = 0;
            for (auto& [key, value] : info.at("routed_functions").items()) if (key.rfind(prefix, 0) == 0) total += value.get<long long>();
            return total;
        };
        if (py::get(info, "layout_api_version") == 1)
            text += "\nUkład mebli C++: " + std::to_string(info.at("layout_operations").size()) + " operacji · " + std::to_string(sum_prefix("layout.")) + " wywołań zbiorczych";
        if (py::get(info, "worktop_api_version") == 1)
            text += "\nGeometria wspólnych blatów C++: " + std::to_string(info.at("worktop_operations").size()) + " operacje · " + std::to_string(sum_prefix("workspace_worktop.")) + " wywołań";
        details->insert("1.0", text);
        details->configure({{"state", "disabled"}});
        d->mark_clean();
    };
    auto* bar = new ttk::Frame(d, {{"padding", 12}});
    bar->pack({{"fill", "x"}});
    (new ttk::Button(bar, {{"text", "Odśwież diagnostykę"}, {"command", std::function<void()>(refresh)}}))->pack({{"side", "left"}});
    (new ttk::Button(bar, {{"text", "Logi programu"}, {"command", std::function<void()>([app] { app->open_developer_logs(); })}}))->pack({{"side", "left"}, {"padx", 8}});
    (new ttk::Button(bar, {{"text", "Zamknij"}, {"command", std::function<void()>([d] { d->destroy(); })}}))->pack({{"side", "right"}});
    refresh();
    g_hooks[d] = Hooks{refresh, table};
    return d;
}

}  // namespace migration_status
