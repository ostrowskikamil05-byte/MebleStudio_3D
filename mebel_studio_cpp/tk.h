// Cienka nakładka C++ na Tcl/Tk 8.6 (odpowiednik tkinter), implementacja w tk.cpp.
//
// Zasady zgodne z tkinter:
//  * ścieżki widgetów powstają jak w tkinter (".!frame", ".!frame2.!button"),
//  * opcje to pary {"nazwa", wartość}; wartość None (Value()) jest pomijana,
//  * bool -> 1/0, liczby zmiennoprzecinkowe -> repr Pythona, wektor tekstów -> lista Tcl,
//  * std::function<void()> (Cmd) jest rejestrowana jako polecenie Tcl,
//  * StringVar/DoubleVar/BooleanVar są uchwytami (kopie współdzielą zmienną Tcl),
//  * wyjątek w wywołaniu zwrotnym trafia do Tk::report_callback_exception.
// Widgety nie są zwalniane po destroy() (wskaźniki w kodzie aplikacji pozostają bezpieczne;
// winfo_exists() zwraca wtedy false).
#pragma once
#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tk {

struct TclError : std::runtime_error { using std::runtime_error::runtime_error; };

using Cmd = std::function<void()>;
class Widget;
class StringVar;
class DoubleVar;
class BooleanVar;
class PhotoImage;
namespace font { class Font; }

using FileTypes = std::vector<std::pair<std::string, std::string>>;

// Wartość opcji widgetu, już zamieniona na postać Tcl.
struct Value {
    enum class Kind { None, Text, List, Command };
    Kind kind = Kind::None;
    std::string text;
    std::vector<Value> items;
    // Polecenie Tcl; dostaje argumenty dopisane przez Tk (np. wartość ttk::Scale).
    std::function<void(const std::vector<std::string>&)> command;

    Value() = default;
    Value(const char* s) : kind(Kind::Text), text(s) {}
    Value(const std::string& s) : kind(Kind::Text), text(s) {}
    Value(bool b) : kind(Kind::Text), text(b ? "1" : "0") {}
    Value(int v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(long v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(long long v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(unsigned v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(unsigned long v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(unsigned long long v) : kind(Kind::Text), text(std::to_string(v)) {}
    Value(double v);
    Value(float v) : Value(static_cast<double>(v)) {}
    Value(const std::vector<std::string>& list);
    Value(const FileTypes& types);
    Value(Cmd fn) : kind(Kind::Command), command([fn = std::move(fn)](const std::vector<std::string>&) { fn(); }) {}
    Value(std::function<void(const std::string&)> fn)
        : kind(Kind::Command), command([fn = std::move(fn)](const std::vector<std::string>& args) { fn(args.empty() ? std::string() : args[0]); }) {}
    Value(const StringVar& v);
    Value(const DoubleVar& v);
    Value(const BooleanVar& v);
    Value(const Widget* w);
    Value(const PhotoImage& image);
    Value(const font::Font& font);
};
using Opts = std::vector<std::pair<std::string, Value>>;

struct Event {
    int x = 0, y = 0, delta = 0, x_root = 0, y_root = 0, width = 0, height = 0, state = 0, num = 0;
    Widget* widget = nullptr;
    std::string keysym;
};

// Zmienne Tcl (tkinter.StringVar itd.)
class Variable {
public:
    const std::string& name() const;
    void trace_add(const std::string& mode, std::function<void()> callback);
protected:
    struct Impl;
    explicit Variable(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
    std::string get_text() const;
    void set_text(const std::string& value);
    static std::shared_ptr<Impl> make(Widget* master, const std::string& initial);
    std::shared_ptr<Impl> impl_;
};
class StringVar : public Variable {
public:
    StringVar(std::string v = "") : Variable(make(nullptr, v)) {}
    StringVar(Widget* master, std::string v) : Variable(make(master, v)) {}
    std::string get() const { return get_text(); }
    void set(const std::string& v) { set_text(v); }
};
class DoubleVar : public Variable {
public:
    DoubleVar(double v = 0);
    DoubleVar(Widget* master, double v);
    double get() const;
    void set(double v);
};
class BooleanVar : public Variable {
public:
    BooleanVar(bool v = false);
    BooleanVar(Widget* master, bool v);
    bool get() const;
    void set(bool v);
};

class Widget {
public:
    Widget() = default;
    Widget(Widget* master, const Opts& = {});
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    virtual ~Widget();

    const std::string& path() const { return path_; }
    Widget* master() const { return master_; }

    void pack(const Opts& = {});
    void pack_forget();
    std::vector<Widget*> pack_slaves();
    void grid(const Opts& = {});
    void place(const Opts& = {});
    void configure(const Opts&);
    std::string cget(const std::string&) const;
    void focus_set();
    // Wywołanie zwrotne zwraca "break", aby przerwać dalsze wiązania (jak w tkinter).
    void bind(const std::string&, std::function<std::string(Event&)>, bool add = false);
    virtual void destroy();
    int winfo_width() const;
    int winfo_height() const;
    int winfo_rootx() const;
    int winfo_rooty() const;
    long long winfo_id() const;
    std::string winfo_class() const;
    bool winfo_exists() const;
    bool winfo_ismapped() const;
    std::vector<Widget*> winfo_children() const;
    Widget* winfo_toplevel() const;
    virtual std::string after(int, std::function<void()>);
    virtual void after_cancel(const std::string&);
    std::string after_idle(std::function<void()>);
    std::string call(const std::vector<std::string>&) const;
    std::vector<std::string> bindtags() const;
    void bindtags(const std::vector<std::string>&);
    void event_generate(const std::string&);
    void lift();
    void update_idletasks();
    std::string register_command(std::function<void()>);
    std::string getvar(const std::string&) const;
    std::array<int, 3> winfo_rgb(const std::string&) const;
    double winfo_fpixels(const std::string&) const;
    Widget* grab_current() const;
    Widget* nametowidget(const std::string&) const;
    int winfo_reqwidth() const;
    int winfo_reqheight() const;
    std::string winfo_manager() const;
    int winfo_x() const;
    int winfo_y() const;
    std::map<std::string, std::string> pack_info();
    void pack_propagate(bool);
    void grid_forget();
    void place_forget();
    void columnconfigure(int, const Opts&);
    int winfo_screenwidth() const;
    int winfo_screenheight() const;

protected:
    // Tworzy widget Tcl: `command path -opcje`. `kind` służy do nazwy (".!kind").
    void create(const std::string& kind, const std::string& tcl_command, const Opts& opts);
    std::string path_;
    Widget* master_ = nullptr;
    bool exists_ = false;
    std::vector<std::string> commands_;  // polecenia Tcl powiązane z widgetem
    friend struct Registry;
};

class Tk : public Widget {
public:
    Tk();
    void withdraw();
    void update();
    void title(const std::string&);
    void geometry(const std::string&);
    void minsize(int, int);
    std::string state();
    void protocol(const std::string&, std::function<void()>);
    void mainloop();
    std::function<void(std::exception_ptr)> report_callback_exception;
    void createcommand(const std::string&, std::function<std::string(const std::vector<std::string>&)>);
};

class Frame : public Widget { public: Frame(Widget*, const Opts& = {}); };
class Canvas : public Widget {
public:
    Canvas() = default;
    Canvas(Widget*, const Opts& = {});
    void delete_(const std::string&);
    int create_oval(double, double, double, double, const Opts&);
    int create_rectangle(double, double, double, double, const Opts&);
    int create_text(double, double, const Opts&);
    int create_line(const std::vector<double>&, const Opts&);
    int create_polygon(const std::vector<double>&, const Opts&);
    std::optional<std::vector<double>> bbox(const std::string&);
    std::optional<std::vector<double>> bbox(int);
    void tag_lower(const std::string&, const std::string& = "");
    void tag_lower(int, int);
    std::vector<int> find_all();
    std::vector<int> find_withtag(const std::string&);
    std::string type(int);
    std::vector<std::string> gettags(int);
    std::string itemcget(int, const std::string&);
    std::vector<double> coords(int);
    int create_window(double, double, const Opts&);
    void itemconfigure(int, const Opts&);
    void yview_scroll(int, const std::string&);
    int create_image(double, double, const Opts&);
    void yview_moveto(double);
private:
    int create_item(const std::string& type, const std::vector<double>& coordinates, const Opts& opts);
};
class Label : public Widget { public: Label(Widget*, const Opts& = {}); };
class Text : public Widget {
public:
    Text(Widget*, const Opts&);
    std::string get(const std::string&, const std::string&);
    void insert(const std::string&, const std::string&, const std::string& tags = "");
    void delete_(const std::string&, const std::string&);
    void see(const std::string&);
    std::string index(const std::string&);
    std::pair<double, double> yview();
    void tag_configure(const std::string&, const Opts&);
};
class Button : public Widget { public: Button(Widget*, const Opts& = {}); };  // klasyczny tk.Button
class Listbox : public Widget {
public:
    Listbox(Widget*, const Opts& = {});
    std::vector<int> curselection() const;
    void insert(const std::string& index, const std::string& text);
    void delete_(const std::string& first, const std::string& last = "");
};
class Toplevel : public Widget {
public:
    Toplevel(Widget* master, const Opts& = {});
    void title(const std::string&);
    void geometry(const std::string&);
    void transient(Widget*);
    void grab_set();
    void grab_release();
    void protocol(const std::string&, std::function<void()>);
};
class Menu : public Widget {
public:
    Menu(Widget*, const Opts& = {});
    void add_cascade(const Opts&);
    void add_command(const Opts&);
    void add_separator();
    struct Entry { std::string type, label, menu; };
    std::vector<Entry> entries();  // typ, etykieta i (dla cascade) ścieżka podmenu
    void unpost();
    void tk_popup(int x, int y);
    void grab_release();
};

// Obraz RGBA (Tk photo). Obiekt musi żyć tak długo jak element obrazu na płótnie.
class PhotoImage {
public:
    PhotoImage(Widget* master, int width, int height, const std::vector<uint8_t>& rgba);
    PhotoImage(const PhotoImage&) = delete;
    PhotoImage& operator=(const PhotoImage&) = delete;
    ~PhotoImage();
    const std::string& name() const { return name_; }
private:
    std::string name_;
};

namespace messagebox {
void showerror(const std::string&, const std::string&, Widget* = nullptr);
void showinfo(const std::string&, const std::string&, Widget* = nullptr);
void showwarning(const std::string&, const std::string&, Widget* = nullptr);
bool askyesno(const std::string&, const std::string&, Widget* = nullptr);
std::optional<bool> askyesnocancel(const std::string&, const std::string&, Widget* = nullptr);
// Wywoływane przed wyświetleniem okna danej funkcji (showerror / showwarning).
void add_observer(const std::string& function, std::function<void(const std::string& title, const std::string& message)>);
}  // namespace messagebox
namespace filedialog {
std::string asksaveasfilename(const Opts&);
std::string askopenfilename(const Opts&);
}  // namespace filedialog
namespace colorchooser {
// (rgb "r g b", "#rrggbb"); oba puste po anulowaniu
std::pair<std::optional<std::string>, std::optional<std::string>> askcolor(const std::string&, Widget*);
}  // namespace colorchooser
namespace simpledialog {
std::optional<double> askfloat(const std::string& title, const std::string& prompt, const Opts& options = {});
}

namespace font {
struct Actual {
    std::string family;
    int size = 0;
    std::string weight;
    std::string slant = "roman";
    int underline = 0;
    int overstrike = 0;
};
Actual actual(Widget* root, const std::string& spec);
// tkinter.font.Font(root=..., font=...|**actual)
class Font {
public:
    Font(Widget* root, const std::string& spec);
    Font(Widget* root, const Opts& actual);
    int measure(const std::string&) const;
    Actual actual() const;
    std::string name() const;
private:
    std::string name_;
};
}  // namespace font

// --- niskopoziomowe pomocnicze (tk.cpp) ---------------------------------------
// Postać tekstowa liczby jak w Pythonie (repr)
std::string number_text(double value);
// Rozbicie listy Tcl na elementy
std::vector<std::string> split_list(const std::string& list);
// Widget o danej ścieżce (nullptr, jeśli nie istnieje)
Widget* find_widget(const std::string& path);
// Słowa polecenia Tcl z opcji: "-nazwa" wartość ... (None jest pomijane, polecenia rejestruje `owner`)
std::string option_word(const Value& value, Widget* owner);
std::string option_flag(const std::string& name);
void append_options(std::vector<std::string>& words, const Opts& opts, Widget* owner);

}  // namespace tk
