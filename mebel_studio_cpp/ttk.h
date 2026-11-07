// Nakładka na ttk (tkinter.ttk), implementacja w ttk.cpp. Zobacz tk.h.
#pragma once
#include "tk.h"

namespace ttk {
using tk::Opts;

class Style {
public:
    Style(tk::Widget* master);
    void theme_use(const std::string&);
    void configure(const std::string&, const Opts&);
    void layout(const std::string&, const std::vector<std::string>&);
    std::string lookup(const std::string&, const std::string&);
    void map(const std::string&, const std::string&, const std::vector<std::pair<std::string, std::string>>&);
private:
    tk::Widget* master_;
};
class Button : public tk::Widget { public: Button(tk::Widget*, const Opts&); std::string invoke(); };
class Label : public tk::Widget { public: Label(tk::Widget*, const Opts&); };
class Entry : public tk::Widget { public: Entry(tk::Widget*, const Opts&); };
class Frame : public tk::Widget { public: Frame(tk::Widget*, const Opts& = {}); };
class Checkbutton : public tk::Widget { public: Checkbutton(tk::Widget*, const Opts&); };
class Separator : public tk::Widget { public: Separator(tk::Widget*, const Opts& = {}); };
class Scrollbar : public tk::Widget { public: Scrollbar(tk::Widget*, const Opts& = {}); };
class Combobox : public tk::Widget {
public:
    Combobox(tk::Widget*, const Opts&);
    int current();
    void current(int);
    std::string get() const;
    void set(const std::string&);
};
class LabelFrame : public tk::Widget { public: LabelFrame(tk::Widget*, const Opts&); };
class Radiobutton : public tk::Widget { public: Radiobutton(tk::Widget*, const Opts&); };
class Scale : public tk::Widget { public: Scale(tk::Widget*, const Opts&); };
class Panedwindow : public tk::Widget {
public:
    Panedwindow(tk::Widget*, const Opts& = {});
    void add(tk::Widget*, const Opts& = {});
    void insert(int, tk::Widget*, const Opts& = {});
    void forget(tk::Widget*);
    std::vector<std::string> panes();
    int sashpos(int);
    void sashpos(int, int);
};
class Spinbox : public tk::Widget { public: Spinbox(tk::Widget*, const Opts&); };
class Notebook : public tk::Widget {
public:
    Notebook(tk::Widget*, const Opts& = {});
    int index(const std::string&);
    std::string select();
    void select(tk::Widget*);
    void select(const std::string&);
    void add(tk::Widget*, const Opts& = {});
    void forget(tk::Widget*);
    void insert(const std::string&, tk::Widget*, const Opts& = {});
    void tab(tk::Widget*, const Opts&);
    void tab(const std::string&, const Opts&);
    std::string tab(const std::string&, const std::string&);
    std::vector<std::string> tabs();
};
class Treeview : public tk::Widget {
public:
    Treeview(tk::Widget*, const Opts& = {});
    void delete_(const std::vector<std::string>&);
    std::vector<std::string> get_children();
    std::string insert(const std::string&, const std::string&, const Opts&);
    bool exists(const std::string&);
    void selection_set(const std::string&);
    void selection_add(const std::string&);
    std::vector<std::string> selection();
    void see(const std::string&);
    void selection_remove(const std::vector<std::string>&);
    std::string item(const std::string&, const std::string&);
    void heading(const std::string&, const Opts&);
    void column(const std::string&, const Opts&);
    void tag_configure(const std::string&, const Opts&);  // $tree tag configure tag -background … (np. naprzemienne wiersze)
};
}  // namespace ttk
