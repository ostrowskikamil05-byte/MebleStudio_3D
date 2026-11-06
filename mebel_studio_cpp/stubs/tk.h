#pragma once
// Stub of the assumed C++ Tk wrapper (compile-check only).
#include <array>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace tk {
struct TclError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Value { template <class T> Value(const T&) {} Value(const char*) {} };
using Opts = std::vector<std::pair<std::string, Value>>;
using FileTypes = std::vector<std::pair<std::string, std::string>>;
class Widget;
struct Event { int x = 0, y = 0, delta = 0, x_root = 0, y_root = 0, width = 0, height = 0, state = 0, num = 0; Widget* widget = nullptr; std::string keysym; };
class StringVar { public: StringVar(std::string v = ""); StringVar(Widget*, std::string v); std::string get() const; void set(const std::string&); void trace_add(const std::string&, std::function<void()>); };
class DoubleVar { public: DoubleVar(double v = 0); DoubleVar(Widget*, double v); double get() const; void set(double); };
class BooleanVar { public: BooleanVar(bool v = false); BooleanVar(Widget*, bool v); bool get() const; void set(bool); void trace_add(const std::string&, std::function<void()>); };
class Widget {
public:
    Widget() = default; Widget(Widget* master, const Opts& = {});
    virtual ~Widget() = default;
    std::string path() const; Widget* master() const;
    void pack(const Opts& = {}); void pack_forget(); std::vector<Widget*> pack_slaves(); void grid(const Opts& = {}); void place(const Opts& = {});
    void configure(const Opts&); std::string cget(const std::string&) const; void focus_set();
    void bind(const std::string&, std::function<std::string(Event&)>, bool add = false);
    virtual void destroy();
    int winfo_width() const; int winfo_height() const; int winfo_rootx() const; int winfo_rooty() const; long long winfo_id() const;
    std::string winfo_class() const; bool winfo_exists() const; bool winfo_ismapped() const; std::vector<Widget*> winfo_children() const; Widget* winfo_toplevel() const;
    virtual std::string after(int, std::function<void()>); virtual void after_cancel(const std::string&); std::string after_idle(std::function<void()>);
    std::string call(const std::vector<std::string>&) const;
    std::vector<std::string> bindtags() const; void bindtags(const std::vector<std::string>&);
    void event_generate(const std::string&); void lift(); void update_idletasks();
    std::string register_command(std::function<void()>);
    std::string getvar(const std::string&) const;
    std::array<int, 3> winfo_rgb(const std::string&) const; double winfo_fpixels(const std::string&) const;
    Widget* grab_current() const; Widget* nametowidget(const std::string&) const;
    // --- dodatki: responsive_ui / quick_studio_ui / room_scene_ui
    int winfo_reqwidth() const; int winfo_reqheight() const; std::string winfo_manager() const; int winfo_x() const; int winfo_y() const;
    std::map<std::string, std::string> pack_info(); void pack_propagate(bool); void grid_forget(); void place_forget(); void columnconfigure(int, const Opts&);
    int winfo_screenwidth() const; int winfo_screenheight() const;
};
class Tk : public Widget {
public:
    Tk(); void withdraw(); void update(); void title(const std::string&); void geometry(const std::string&); void minsize(int, int);
    std::string state(); void protocol(const std::string&, std::function<void()>); void mainloop();
    std::function<void(std::exception_ptr)> report_callback_exception;
    void createcommand(const std::string&, std::function<std::string(const std::vector<std::string>&)>);
};
class Frame : public Widget { public: Frame(Widget*, const Opts& = {}); };
class Canvas : public Widget {
public:
    Canvas() = default; Canvas(Widget*, const Opts& = {});
    void delete_(const std::string&);
    int create_oval(double, double, double, double, const Opts&);
    int create_rectangle(double, double, double, double, const Opts&); int create_text(double, double, const Opts&);
    int create_line(const std::vector<double>&, const Opts&); int create_polygon(const std::vector<double>&, const Opts&);
    std::optional<std::vector<double>> bbox(const std::string&); std::optional<std::vector<double>> bbox(int); void tag_lower(const std::string&, const std::string& = ""); void tag_lower(int, int);
    std::vector<int> find_all(); std::vector<int> find_withtag(const std::string&); std::string type(int); std::vector<std::string> gettags(int); std::string itemcget(int, const std::string&); std::vector<double> coords(int);
    int create_window(double, double, const Opts&); void itemconfigure(int, const Opts&); void yview_scroll(int, const std::string&); int create_image(double, double, const Opts&);
    void yview_moveto(double);
};
class Label : public Widget { public: Label(Widget*, const Opts& = {}); };
class Text : public Widget {
public:
    Text(Widget*, const Opts&); std::string get(const std::string&, const std::string&);
    void insert(const std::string&, const std::string&, const std::string& tags = ""); void delete_(const std::string&, const std::string&);
    void see(const std::string&); std::string index(const std::string&); std::pair<double, double> yview(); void tag_configure(const std::string&, const Opts&);
};
namespace messagebox {
void showerror(const std::string&, const std::string&, Widget* = nullptr); void showinfo(const std::string&, const std::string&, Widget* = nullptr);
void showwarning(const std::string&, const std::string&, Widget* = nullptr);
bool askyesno(const std::string&, const std::string&, Widget* = nullptr);
std::optional<bool> askyesnocancel(const std::string&, const std::string&, Widget* = nullptr);
// Wywoływane przed wyświetleniem okna danej funkcji (showerror / showwarning).
void add_observer(const std::string& function, std::function<void(const std::string& title, const std::string& message)>);
}
namespace filedialog { std::string asksaveasfilename(const Opts&); std::string askopenfilename(const Opts&); }
namespace colorchooser { std::pair<std::optional<std::string>, std::optional<std::string>> askcolor(const std::string&, Widget*); }
}
// --- dodatki wymagane przez form_dialog / furniture_layout_ui / hardware_placement_ui / gpu_viewport
#include <cstdint>
namespace tk {
class Button : public Widget { public: Button(Widget*, const Opts& = {}); };  // klasyczny tk.Button
class Listbox : public Widget {
public:
    Listbox(Widget*, const Opts& = {}); std::vector<int> curselection() const; void insert(const std::string& index, const std::string& text); void delete_(const std::string& first, const std::string& last = "");
};
class Toplevel : public Widget {
public:
    Toplevel(Widget* master, const Opts& = {}); void title(const std::string&); void geometry(const std::string&); void transient(Widget*); void grab_set(); void grab_release();
    void protocol(const std::string&, std::function<void()>);
};
namespace simpledialog { std::optional<double> askfloat(const std::string& title, const std::string& prompt, const Opts& options = {}); }
class Menu : public Widget { public: Menu(Widget*, const Opts& = {}); void add_cascade(const Opts&); void add_command(const Opts&); void add_separator(); void unpost(); void tk_popup(int x, int y); void grab_release(); };
class PhotoImage { public: PhotoImage(Widget* master, int width, int height, const std::vector<uint8_t>& rgba); ~PhotoImage(); };
}
namespace tk::font { struct Actual { std::string family; int size = 0; std::string weight; std::string slant = "roman"; int underline = 0; int overstrike = 0; }; Actual actual(Widget* root, const std::string& spec); }

namespace tk::font {
// tkinter.font.Font(root=..., font=...|**actual)
class Font { public: Font(Widget* root, const std::string& spec); Font(Widget* root, const Opts& actual); int measure(const std::string&) const; Actual actual() const; std::string name() const; };
}
