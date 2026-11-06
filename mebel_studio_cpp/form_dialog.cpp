// Scrollable forms with clipped content and fixed action footers.
#include "form_dialog.h"

#include <algorithm>
#include <sstream>

#include "app.h"

namespace form_dialog {

FormContent::FormContent(tk::Widget* parent, FormDocument* document_) : ttk::Frame(parent), document(document_) {}

bool FormContent::ensure_current_project(bool raise_error) { return document->ensure_current_project(raise_error); }
void FormContent::mark_clean() { document->mark_clean(); }
bool FormContent::has_changes() { return document->has_changes(); }
std::function<void()> FormContent::guard(std::function<void()> callback) { return document->guard(std::move(callback)); }
void FormContent::protocol(const std::string& name, std::function<void()> func) { document->protocol(name, std::move(func)); }
std::string FormContent::transient(tk::Widget* master) { return document->transient(master); }
void FormContent::grab_set() {}
void FormContent::grab_release() {}

void FormContent::destroy() {
    if (document->_destroying) ttk::Frame::destroy();
    else document->destroy();
}

FormDocument::FormDocument(App* app_, const std::string& title_, const std::string& size) : dock_workspace::DockDocument(app_) {
    _destroying = false;
    title(title_); geometry(size + "+30+30");
    footer = new ttk::Frame(this); footer->pack({{"side", "bottom"}, {"fill", "x"}});
    body = new ttk::Frame(this); body->pack({{"fill", "both"}, {"expand", true}});
    viewport = new tk::Canvas(body, {{"highlightthickness", 0}, {"bg", "#f6f3ee"}});
    viewport->pack({{"side", "left"}, {"fill", "both"}, {"expand", true}});
    auto* scrollbar = new ttk::Scrollbar(body, {{"command", viewport->path() + " yview"}}); scrollbar->pack({{"side", "right"}, {"fill", "y"}});
    viewport->configure({{"yscrollcommand", scrollbar->path() + " set"}});
    form = new FormContent(viewport, this);
    window = viewport->create_window(0, 0, {{"window", form->path()}, {"anchor", "nw"}});
    form->bind("<Configure>", [this](tk::Event&) {
        auto box = viewport->bbox("all");
        std::string region;
        if (box) {
            std::ostringstream out;
            for (size_t i = 0; i < box->size(); ++i) out << (i ? " " : "") << static_cast<long long>((*box)[i]);
            region = out.str();
        }
        viewport->configure({{"scrollregion", region}});
        return std::string();
    });
    viewport->bind("<Configure>", [this](tk::Event& e) { viewport->itemconfigure(window, {{"width", e.width}}); return std::string(); });
    after_idle([this] { arrange(); });
}

void FormDocument::arrange() {
    std::vector<tk::Widget*> actions;
    for (auto* w : form->pack_slaves()) if (w->winfo_class() == "TButton") actions.push_back(w);
    if (!actions.empty()) {
        tk::Widget* primary = actions.back(); primary->pack_forget();
        // Retain its registered Tcl callback until the document closes.
        (new ttk::Button(footer, {{"text", primary->cget("text")}, {"command", std::function<void()>([primary] { primary->call({primary->path(), "invoke"}); })},
                                  {"style", primary->cget("style")}, {"state", primary->cget("state")}}))->pack({{"fill", "x"}, {"padx", 12}, {"pady", 8}});
    }
    auto scroll = [this](tk::Event& event) { viewport->yview_scroll(event.delta > 0 ? -1 : 1, "units"); return std::string(); };
    std::function<void(tk::Widget*)> visit = [&](tk::Widget* widget) {
        for (auto* child : widget->winfo_children()) {
            std::string cls = child->winfo_class();
            if (cls != "TCombobox" && cls != "Treeview" && cls != "Text" && cls != "Listbox") child->bind("<MouseWheel>", scroll, true);
            visit(child);
        }
    };
    visit(form); mark_clean();
}

void FormDocument::destroy() {
    if (_destroying) return;
    _destroying = true;
    dock_workspace::DockDocument::destroy();
}

}  // namespace form_dialog
