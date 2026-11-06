// Scrollable forms with clipped content and fixed action footers.
#pragma once
#include <functional>
#include <string>
#include "dock_workspace.h"
#include "tk.h"
#include "ttk.h"

class App;

namespace form_dialog {

class FormDocument;

class FormContent : public ttk::Frame {
public:
    FormDocument* document;

    FormContent(tk::Widget* parent, FormDocument* document);
    bool ensure_current_project(bool raise_error = false);
    void mark_clean();
    bool has_changes();
    std::function<void()> guard(std::function<void()> callback);
    void protocol(const std::string& name, std::function<void()> func);
    std::string transient(tk::Widget* master = nullptr);
    void grab_set();      // return None
    void grab_release();  // return None
    void destroy() override;
};

// W Pythonie: Form = FormContent (alias używany przez app.dialog()).
using Form = FormContent;

class FormDocument : public dock_workspace::DockDocument {
public:
    bool _destroying = false;
    ttk::Frame* footer = nullptr;
    ttk::Frame* body = nullptr;
    tk::Canvas* viewport = nullptr;
    FormContent* form = nullptr;
    int window = 0;

    FormDocument(App* app, const std::string& title, const std::string& size);
    // Python: form.document.has_changes = lambda: False (katalog graficzny — tylko stan prezentacji).
    bool presentation_only = false;
    bool has_changes() override { return presentation_only ? false : dock_workspace::DockDocument::has_changes(); }
    void arrange();
    void destroy() override;
};

}  // namespace form_dialog
