// Implementacja ttk.h (tkinter.ttk) nad tkcore.
#include "ttk.h"

#include "tkcore.h"

#include <cstdlib>

namespace ttk {

using tkcore::Args;

namespace {

std::string run(const Args& words) { return tkcore::eval(words); }

std::string option_name(const std::string& name) { return tk::option_flag(name); }
std::string word(const tk::Value& value, tk::Widget* owner) { return tk::option_word(value, owner); }
void add_opts(Args& words, const Opts& opts, tk::Widget* owner) { tk::append_options(words, opts, owner); }

int to_int(const std::string& text) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end == text.c_str() || *end != '\0') throw tk::TclError("expected integer but got \"" + text + "\"");
    return static_cast<int>(value);
}

}  // namespace

// Style
Style::Style(tk::Widget* master) : master_(master) { tkcore::init(); }
void Style::theme_use(const std::string& name) { run({"ttk::style", "theme", "use", name}); }
void Style::configure(const std::string& style, const Opts& opts) {
    Args words{"ttk::style", "configure", style};
    add_opts(words, opts, nullptr);
    run(words);
}
void Style::layout(const std::string& style, const std::vector<std::string>& spec) {
    // Jak ttk.Style.layout w Pythonie: pusta lista wyłącza układ (specjalna nazwa "null"), a nie "{}".
    run({"ttk::style", "layout", style, spec.empty() ? std::string("null") : tkcore::merge(spec)});
}
std::string Style::lookup(const std::string& style, const std::string& option) {
    return run({"ttk::style", "lookup", style, option_name(option)});
}
void Style::map(const std::string& style, const std::string& option, const std::vector<std::pair<std::string, std::string>>& states) {
    Args flat;
    for (const auto& [state, value] : states) {
        flat.push_back(state);
        flat.push_back(value);
    }
    run({"ttk::style", "map", style, option_name(option), tkcore::merge(flat)});
}

// Proste widgety
Button::Button(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("button", "ttk::button", opts); }
std::string Button::invoke() { return run({path(), "invoke"}); }
Label::Label(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("label", "ttk::label", opts); }
Entry::Entry(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("entry", "ttk::entry", opts); }
Frame::Frame(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("frame", "ttk::frame", opts); }
Checkbutton::Checkbutton(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("checkbutton", "ttk::checkbutton", opts); }
Separator::Separator(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("separator", "ttk::separator", opts); }
Scrollbar::Scrollbar(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("scrollbar", "ttk::scrollbar", opts); }
LabelFrame::LabelFrame(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("labelframe", "ttk::labelframe", opts); }
Radiobutton::Radiobutton(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("radiobutton", "ttk::radiobutton", opts); }
Scale::Scale(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("scale", "ttk::scale", opts); }
Spinbox::Spinbox(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("spinbox", "ttk::spinbox", opts); }

// Combobox
Combobox::Combobox(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("combobox", "ttk::combobox", opts); }
int Combobox::current() { return to_int(run({path(), "current"})); }
void Combobox::current(int index) { run({path(), "current", std::to_string(index)}); }
std::string Combobox::get() const { return run({path(), "get"}); }
void Combobox::set(const std::string& value) { run({path(), "set", value}); }

// Panedwindow
Panedwindow::Panedwindow(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("panedwindow", "ttk::panedwindow", opts); }
void Panedwindow::add(tk::Widget* child, const Opts& opts) {
    Args words{path(), "add", child->path()};
    add_opts(words, opts, this);
    run(words);
}
void Panedwindow::insert(int position, tk::Widget* child, const Opts& opts) {
    Args words{path(), "insert", std::to_string(position), child->path()};
    add_opts(words, opts, this);
    run(words);
}
void Panedwindow::forget(tk::Widget* child) { run({path(), "forget", child->path()}); }
std::vector<std::string> Panedwindow::panes() { return tkcore::split(run({path(), "panes"})); }
int Panedwindow::sashpos(int index) { return to_int(run({path(), "sashpos", std::to_string(index)})); }
void Panedwindow::sashpos(int index, int position) { run({path(), "sashpos", std::to_string(index), std::to_string(position)}); }

// Notebook
Notebook::Notebook(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("notebook", "ttk::notebook", opts); }
int Notebook::index(const std::string& tab_id) { return to_int(run({path(), "index", tab_id})); }
std::string Notebook::select() { return run({path(), "select"}); }
void Notebook::select(tk::Widget* child) { run({path(), "select", child->path()}); }
void Notebook::select(const std::string& tab_id) { run({path(), "select", tab_id}); }
void Notebook::add(tk::Widget* child, const Opts& opts) {
    Args words{path(), "add", child->path()};
    add_opts(words, opts, this);
    run(words);
}
void Notebook::forget(tk::Widget* child) { run({path(), "forget", child->path()}); }
void Notebook::insert(const std::string& position, tk::Widget* child, const Opts& opts) {
    Args words{path(), "insert", position, child->path()};
    add_opts(words, opts, this);
    run(words);
}
void Notebook::tab(tk::Widget* child, const Opts& opts) {
    Args words{path(), "tab", child->path()};
    add_opts(words, opts, this);
    run(words);
}
void Notebook::tab(const std::string& tab_id, const Opts& opts) {
    Args words{path(), "tab", tab_id};
    add_opts(words, opts, this);
    run(words);
}
std::string Notebook::tab(const std::string& tab_id, const std::string& option) { return run({path(), "tab", tab_id, option_name(option)}); }
std::vector<std::string> Notebook::tabs() { return tkcore::split(run({path(), "tabs"})); }

// Treeview
Treeview::Treeview(tk::Widget* master, const Opts& opts) : tk::Widget(master) { create("treeview", "ttk::treeview", opts); }
void Treeview::delete_(const std::vector<std::string>& items) {
    if (items.empty()) return;
    Args words{path(), "delete"};
    words.push_back(tkcore::merge(items));
    run(words);
}
std::vector<std::string> Treeview::get_children() { return tkcore::split(run({path(), "children", ""})); }
std::string Treeview::insert(const std::string& parent, const std::string& index, const Opts& opts) {
    Args words{path(), "insert", parent, index};
    for (const auto& [key, value] : opts) {
        if (value.kind == tk::Value::Kind::None) continue;
        words.push_back(key == "iid" ? "-id" : option_name(key));  // jak ttk.Treeview.insert(iid=...)
        words.push_back(word(value, this));
    }
    return run(words);
}
bool Treeview::exists(const std::string& item) { return run({path(), "exists", item}) == "1"; }
void Treeview::selection_set(const std::string& item) { run({path(), "selection", "set", item}); }
void Treeview::selection_add(const std::string& item) { run({path(), "selection", "add", item}); }
std::vector<std::string> Treeview::selection() { return tkcore::split(run({path(), "selection"})); }
void Treeview::see(const std::string& item) { run({path(), "see", item}); }
void Treeview::selection_remove(const std::vector<std::string>& items) {
    if (items.empty()) return;
    run({path(), "selection", "remove", tkcore::merge(items)});
}
std::string Treeview::item(const std::string& item, const std::string& option) { return run({path(), "item", item, option_name(option)}); }
void Treeview::heading(const std::string& column, const Opts& opts) {
    Args words{path(), "heading", column};
    add_opts(words, opts, this);
    run(words);
}
void Treeview::tag_configure(const std::string& tag, const Opts& opts) {
    Args words{path(), "tag", "configure", tag};
    add_opts(words, opts, this);
    run(words);
}
void Treeview::column(const std::string& column, const Opts& opts) {
    Args words{path(), "column", column};
    add_opts(words, opts, this);
    run(words);
}

}  // namespace ttk
