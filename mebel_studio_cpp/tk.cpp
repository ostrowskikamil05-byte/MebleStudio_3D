// Implementacja tk.h nad tkcore (Tcl/Tk 8.6). Kolejność i nazwy poleceń Tcl odpowiadają tkinter.
#include "tk.h"

#include "tkcore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace tk {

using tkcore::Args;

// ------------------------------------------------------------------------------ rejestr
struct Registry {
    Tk* root = nullptr;
    std::map<std::string, Widget*> widgets;
    std::map<std::string, std::map<std::string, int>> child_ids;
    std::map<std::string, std::vector<std::function<void(const std::string&, const std::string&)>>> observers;
    int variable_counter = 0;
    int font_counter = 0;
    int image_counter = 0;

    static Registry& get() {
        static Registry registry;
        return registry;
    }

    static std::string make_command(Widget* owner, std::function<void(const Args&)> fn, bool once) {
        std::string name = tkcore::define(
            "_cb",
            [fn = std::move(fn)](const Args& args) {
                fn(args);
                return std::string();
            },
            once);
        if (owner) {
            owner->commands_.push_back(name);
            tkcore::set_label(name, "polecenie " + owner->path());
        }
        return name;
    }

    // Usuwa wpisy widgetu i jego potomków (po destroy).
    static void forget(const std::string& path) {
        Registry& r = get();
        const std::string prefix = path == "." ? std::string() : path + ".";
        std::vector<std::string> removed;
        for (const auto& entry : r.widgets) {
            const std::string& key = entry.first;
            if (path == "." || key == path || (!prefix.empty() && key.compare(0, prefix.size(), prefix) == 0)) removed.push_back(key);
        }
        for (const std::string& key : removed) {
            Widget* widget = r.widgets[key];
            if (widget) {
                for (const std::string& command : widget->commands_) tkcore::remove(command);
                widget->commands_.clear();
                widget->exists_ = false;
            }
            r.widgets.erase(key);
            r.child_ids.erase(key);
        }
    }

    static void report(std::exception_ptr error) {
        Registry& r = get();
        if (r.root && r.root->report_callback_exception) {
            try {
                r.root->report_callback_exception(error);
                return;
            } catch (...) {
            }
        }
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& ex) {
            std::cerr << "Błąd funkcji interfejsu: " << ex.what() << '\n';
        } catch (...) {
            std::cerr << "Nieznany błąd funkcji interfejsu\n";
        }
    }
};

namespace {

std::string run(const Args& words) { return tkcore::eval(words); }

long parse_long(const std::string& text) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || end == text.c_str() || *end != '\0') {
        throw TclError("expected integer but got \"" + text + "\"");
    }
    return value;
}
int parse_int(const std::string& text) { return static_cast<int>(parse_long(text)); }

double parse_double(const std::string& text) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end == text.c_str() || *end != '\0') {
        throw TclError("expected floating-point number but got \"" + text + "\"");
    }
    return value;
}

bool parse_bool(const std::string& text) {
    std::string lower;
    for (char c : text) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
    throw TclError("expected boolean value but got \"" + text + "\"");
}

int lenient_int(const std::string& text) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 0);
    return (text.empty() || end == text.c_str() || *end != '\0') ? 0 : static_cast<int>(value);
}

std::vector<int> int_list(const std::string& text) {
    std::vector<int> result;
    for (const std::string& item : tkcore::split(text)) result.push_back(parse_int(item));
    return result;
}
std::vector<double> double_list(const std::string& text) {
    std::vector<double> result;
    for (const std::string& item : tkcore::split(text)) result.push_back(parse_double(item));
    return result;
}

}  // namespace

std::string option_flag(const std::string& name) {
    std::string n = name;
    while (!n.empty() && n.back() == '_') n.pop_back();
    return (!n.empty() && n.front() == '-') ? n : "-" + n;
}

std::string option_word(const Value& value, Widget* owner) {
    switch (value.kind) {
        case Value::Kind::None:
            return {};
        case Value::Kind::Text:
            return value.text;
        case Value::Kind::List: {
            Args parts;
            parts.reserve(value.items.size());
            for (const Value& item : value.items) parts.push_back(option_word(item, owner));
            return tkcore::merge(parts);
        }
        case Value::Kind::Command:
            return Registry::make_command(owner, value.command, false);
    }
    return {};
}

void append_options(std::vector<std::string>& words, const Opts& opts, Widget* owner) {
    for (const auto& [key, value] : opts) {
        if (value.kind == Value::Kind::None) continue;
        words.push_back(option_flag(key));
        words.push_back(option_word(value, owner));
    }
}

namespace {
void add_opts(Args& words, const Opts& opts, Widget* owner) { append_options(words, opts, owner); }
std::string option_name(const std::string& name) { return option_flag(name); }
std::string word(const Value& value, Widget* owner) { return option_word(value, owner); }
}  // namespace

// ------------------------------------------------------------------------------ pomocnicze (publiczne)
std::string number_text(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Inf" : "-Inf";
    if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";
    // Najkrótsza reprezentacja, która wraca do tej samej liczby, w zapisie jak repr(float) w Pythonie.
    char buffer[48];
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*e", digits - 1, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    std::string text = buffer;  // [-]d[.ddd]e[+-]xx
    const bool negative = text.front() == '-';
    if (negative) text.erase(0, 1);
    const std::size_t e_pos = text.find('e');
    std::string mantissa = text.substr(0, e_pos);
    const int exponent = std::atoi(text.c_str() + e_pos + 1);
    std::string digits;
    for (char c : mantissa) {
        if (c != '.') digits.push_back(c);
    }
    std::string out;
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            out = "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
        } else if (static_cast<std::size_t>(exponent) + 1 >= digits.size()) {
            out = digits + std::string(static_cast<std::size_t>(exponent) + 1 - digits.size(), '0') + ".0";
        } else {
            out = digits.substr(0, static_cast<std::size_t>(exponent) + 1) + "." + digits.substr(static_cast<std::size_t>(exponent) + 1);
        }
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char tail[16];
        std::snprintf(tail, sizeof tail, "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        out += tail;
    }
    return negative ? "-" + out : out;
}

std::vector<std::string> split_list(const std::string& list) { return tkcore::split(list); }

Widget* find_widget(const std::string& path) {
    Registry& r = Registry::get();
    auto it = r.widgets.find(path);
    return it == r.widgets.end() ? nullptr : it->second;
}

// ------------------------------------------------------------------------------ Value
Value::Value(double v) : kind(Kind::Text), text(number_text(v)) {}
Value::Value(const std::vector<std::string>& list) : kind(Kind::List) {
    for (const std::string& item : list) items.emplace_back(item);
}
Value::Value(const FileTypes& types) : kind(Kind::List) {
    for (const auto& [label, pattern] : types) {
        Value pair;
        pair.kind = Kind::List;
        pair.items.emplace_back(label);
        pair.items.emplace_back(pattern);
        items.push_back(std::move(pair));
    }
}
Value::Value(const StringVar& v) : kind(Kind::Text), text(v.name()) {}
Value::Value(const DoubleVar& v) : kind(Kind::Text), text(v.name()) {}
Value::Value(const BooleanVar& v) : kind(Kind::Text), text(v.name()) {}
Value::Value(const Widget* w) : kind(Kind::Text), text(w ? w->path() : std::string()) {}
Value::Value(const PhotoImage& image) : kind(Kind::Text), text(image.name()) {}
Value::Value(const font::Font& f) : kind(Kind::Text), text(f.name()) {}

// ------------------------------------------------------------------------------ zmienne
struct Variable::Impl {
    std::string name;
};

const std::string& Variable::name() const { return impl_->name; }

std::string Variable::get_text() const { return tkcore::get_var(impl_->name); }
void Variable::set_text(const std::string& value) { tkcore::set_var(impl_->name, value); }

std::shared_ptr<Variable::Impl> Variable::make(Widget*, const std::string& initial) {
    tkcore::init();
    auto impl = std::make_shared<Impl>();
    impl->name = "PY_VAR" + std::to_string(Registry::get().variable_counter++);
    tkcore::set_var(impl->name, initial);
    return impl;
}

void Variable::trace_add(const std::string& mode, std::function<void()> callback) {
    const std::string command = Registry::make_command(nullptr, [callback = std::move(callback)](const Args&) { callback(); }, false);
    run({"trace", "add", "variable", impl_->name, mode, command});
}

DoubleVar::DoubleVar(double v) : Variable(make(nullptr, number_text(v))) {}
DoubleVar::DoubleVar(Widget* master, double v) : Variable(make(master, number_text(v))) {}
double DoubleVar::get() const { return parse_double(get_text()); }
void DoubleVar::set(double v) { set_text(number_text(v)); }

BooleanVar::BooleanVar(bool v) : Variable(make(nullptr, v ? "1" : "0")) {}
BooleanVar::BooleanVar(Widget* master, bool v) : Variable(make(master, v ? "1" : "0")) {}
bool BooleanVar::get() const { return parse_bool(get_text()); }
void BooleanVar::set(bool v) { set_text(v ? "1" : "0"); }

// ------------------------------------------------------------------------------ Widget
Widget::Widget(Widget* master, const Opts&) : master_(master) {}

Widget::~Widget() {
    Registry& r = Registry::get();
    auto it = r.widgets.find(path_);
    if (it != r.widgets.end() && it->second == this) it->second = nullptr;
}

void Widget::create(const std::string& kind, const std::string& tcl_command, const Opts& opts) {
    Registry& r = Registry::get();
    tkcore::init();
    if (!master_ && r.root) master_ = r.root;
    const std::string parent = master_ && !master_->path_.empty() ? master_->path_ : std::string(".");
    int& counter = r.child_ids[parent][kind];
    ++counter;
    std::string name = "!" + kind;
    if (counter > 1) name += std::to_string(counter);
    path_ = (parent == "." ? std::string() : parent) + "." + name;
    Args words{tcl_command, path_};
    add_opts(words, opts, this);
    try {
        run(words);
    } catch (...) {
        path_.clear();
        throw;
    }
    exists_ = true;
    r.widgets[path_] = this;
}

void Widget::pack(const Opts& opts) {
    Args words{"pack", "configure", path_};
    add_opts(words, opts, this);
    run(words);
}
void Widget::pack_forget() { run({"pack", "forget", path_}); }
std::vector<Widget*> Widget::pack_slaves() {
    std::vector<Widget*> result;
    for (const std::string& item : tkcore::split(run({"pack", "slaves", path_}))) {
        if (Widget* w = find_widget(item)) result.push_back(w);
    }
    return result;
}
std::map<std::string, std::string> Widget::pack_info() {
    std::map<std::string, std::string> result;
    const Args items = tkcore::split(run({"pack", "info", path_}));
    for (std::size_t i = 0; i + 1 < items.size(); i += 2) {
        std::string key = items[i];
        if (!key.empty() && key.front() == '-') key.erase(0, 1);
        result[key] = items[i + 1];
    }
    return result;
}
void Widget::pack_propagate(bool flag) { run({"pack", "propagate", path_, flag ? "1" : "0"}); }
void Widget::grid(const Opts& opts) {
    Args words{"grid", "configure", path_};
    add_opts(words, opts, this);
    run(words);
}
void Widget::grid_forget() { run({"grid", "forget", path_}); }
void Widget::columnconfigure(int index, const Opts& opts) {
    Args words{"grid", "columnconfigure", path_, std::to_string(index)};
    add_opts(words, opts, this);
    run(words);
}
void Widget::place(const Opts& opts) {
    Args words{"place", "configure", path_};
    add_opts(words, opts, this);
    run(words);
}
void Widget::place_forget() { run({"place", "forget", path_}); }

void Widget::configure(const Opts& opts) {
    Args words{path_, "configure"};
    add_opts(words, opts, this);
    run(words);
}
std::string Widget::cget(const std::string& option) const { return run({path_, "cget", option_name(option)}); }
void Widget::focus_set() { run({"focus", path_}); }

void Widget::bind(const std::string& sequence, std::function<std::string(Event&)> callback, bool add) {
    const std::string name = tkcore::define("_cb", [callback = std::move(callback)](const Args& a) {
        Event event;
        auto at = [&](std::size_t i) { return i < a.size() ? a[i] : std::string(); };
        event.x = lenient_int(at(0));
        event.y = lenient_int(at(1));
        event.delta = lenient_int(at(2));
        event.x_root = lenient_int(at(3));
        event.y_root = lenient_int(at(4));
        event.width = lenient_int(at(5));
        event.height = lenient_int(at(6));
        event.state = lenient_int(at(7));
        event.num = lenient_int(at(8));
        event.keysym = at(9) == "??" ? std::string() : at(9);
        event.widget = find_widget(at(10));
        return callback(event);
    });
    commands_.push_back(name);
    tkcore::set_label(name, "bind " + sequence + " " + path_);
    // Tak samo jak tkinter: wartość "break" z procedury przerywa dalsze wiązania.
    std::string script = "if {\"[" + name + " %x %y %D %X %Y %w %h %s %b %K %W]\" == \"break\"} break\n";
    if (add) script = "+" + script;
    run({"bind", path_, sequence, script});
}

void Widget::destroy() {
    if (path_.empty()) return;
    const bool existed = exists_;
    try {
        run({"destroy", path_});
    } catch (const TclError&) {
    }
    if (existed) {
        const std::string path = path_;
        Registry::forget(path);
        exists_ = false;
        // forget() wyrzuca wpis z rejestru; wskaźnik pozostaje poprawny dla kodu aplikacji.
    }
}

int Widget::winfo_width() const { return parse_int(run({"winfo", "width", path_})); }
int Widget::winfo_height() const { return parse_int(run({"winfo", "height", path_})); }
int Widget::winfo_rootx() const { return parse_int(run({"winfo", "rootx", path_})); }
int Widget::winfo_rooty() const { return parse_int(run({"winfo", "rooty", path_})); }
long long Widget::winfo_id() const { return std::strtoll(run({"winfo", "id", path_}).c_str(), nullptr, 0); }
std::string Widget::winfo_class() const { return run({"winfo", "class", path_}); }
bool Widget::winfo_exists() const {
    if (path_.empty()) return false;
    try {
        return run({"winfo", "exists", path_}) == "1";
    } catch (const TclError&) {
        return false;
    }
}
bool Widget::winfo_ismapped() const { return run({"winfo", "ismapped", path_}) == "1"; }
std::vector<Widget*> Widget::winfo_children() const {
    std::vector<Widget*> result;
    for (const std::string& item : tkcore::split(run({"winfo", "children", path_}))) {
        if (Widget* w = find_widget(item)) result.push_back(w);
    }
    return result;
}
Widget* Widget::winfo_toplevel() const { return find_widget(run({"winfo", "toplevel", path_})); }
int Widget::winfo_reqwidth() const { return parse_int(run({"winfo", "reqwidth", path_})); }
int Widget::winfo_reqheight() const { return parse_int(run({"winfo", "reqheight", path_})); }
std::string Widget::winfo_manager() const { return run({"winfo", "manager", path_}); }
int Widget::winfo_x() const { return parse_int(run({"winfo", "x", path_})); }
int Widget::winfo_y() const { return parse_int(run({"winfo", "y", path_})); }
int Widget::winfo_screenwidth() const { return parse_int(run({"winfo", "screenwidth", path_})); }
int Widget::winfo_screenheight() const { return parse_int(run({"winfo", "screenheight", path_})); }
std::array<int, 3> Widget::winfo_rgb(const std::string& color) const {
    const Args parts = tkcore::split(run({"winfo", "rgb", path_, color}));
    if (parts.size() != 3) throw TclError("winfo rgb: nieoczekiwany wynik");
    return {parse_int(parts[0]), parse_int(parts[1]), parse_int(parts[2])};
}
double Widget::winfo_fpixels(const std::string& spec) const { return parse_double(run({"winfo", "fpixels", path_, spec})); }

std::string Widget::after(int milliseconds, std::function<void()> callback) {
    const std::string name = Registry::make_command(this, [callback = std::move(callback)](const Args&) { callback(); }, true);
    tkcore::set_label(name, "after " + std::to_string(milliseconds) + " ms " + path_);
    return run({"after", std::to_string(milliseconds), name});
}
void Widget::after_cancel(const std::string& id) {
    if (id.empty()) return;
    try {
        const Args info = tkcore::split(run({"after", "info", id}));
        if (!info.empty()) tkcore::remove(info[0]);
    } catch (const TclError&) {
    }
    try {
        run({"after", "cancel", id});
    } catch (const TclError&) {
    }
}
std::string Widget::after_idle(std::function<void()> callback) {
    const std::string name = Registry::make_command(this, [callback = std::move(callback)](const Args&) { callback(); }, true);
    tkcore::set_label(name, "after_idle " + path_);
    return run({"after", "idle", name});
}
std::string Widget::call(const std::vector<std::string>& words) const { return run(words); }
std::vector<std::string> Widget::bindtags() const { return tkcore::split(run({"bindtags", path_})); }
void Widget::bindtags(const std::vector<std::string>& tags) { run({"bindtags", path_, tkcore::merge(tags)}); }
void Widget::event_generate(const std::string& sequence) { run({"event", "generate", path_, sequence}); }
void Widget::lift() { run({"raise", path_}); }
void Widget::update_idletasks() { run({"update", "idletasks"}); }
std::string Widget::register_command(std::function<void()> callback) {
    return Registry::make_command(this, [callback = std::move(callback)](const Args&) { callback(); }, false);
}
std::string Widget::getvar(const std::string& name) const { return tkcore::get_var(name); }
Widget* Widget::grab_current() const {
    const std::string current = run({"grab", "current"});
    return current.empty() ? nullptr : find_widget(current);
}
Widget* Widget::nametowidget(const std::string& name) const {
    if (Widget* w = find_widget(name)) return w;
    if (!name.empty() && name.front() != '.') {
        const std::string prefix = path_ == "." ? std::string() : path_;
        return find_widget(prefix + "." + name);
    }
    return nullptr;
}

// ------------------------------------------------------------------------------ Tk
Tk::Tk() {
    tkcore::init();
    Registry& r = Registry::get();
    path_ = ".";
    exists_ = true;
    r.root = this;
    r.widgets["."] = this;
    tkcore::set_exception_sink(&Registry::report);
}
void Tk::withdraw() { run({"wm", "withdraw", "."}); }
void Tk::update() { run({"update"}); }
void Tk::title(const std::string& text) { run({"wm", "title", ".", text}); }
void Tk::geometry(const std::string& spec) { run({"wm", "geometry", ".", spec}); }
void Tk::minsize(int width, int height) { run({"wm", "minsize", ".", std::to_string(width), std::to_string(height)}); }
std::string Tk::state() { return run({"wm", "state", "."}); }
void Tk::protocol(const std::string& name, std::function<void()> callback) {
    run({"wm", "protocol", ".", name, register_command(std::move(callback))});
}
void Tk::mainloop() { tkcore::main_loop(); }
void Tk::createcommand(const std::string& name, std::function<std::string(const std::vector<std::string>&)> callback) {
    tkcore::define_named(name, std::move(callback));
}

// ------------------------------------------------------------------------------ widgety klasyczne
Frame::Frame(Widget* master, const Opts& opts) : Widget(master) { create("frame", "frame", opts); }
Label::Label(Widget* master, const Opts& opts) : Widget(master) { create("label", "label", opts); }
Button::Button(Widget* master, const Opts& opts) : Widget(master) { create("button", "button", opts); }
Listbox::Listbox(Widget* master, const Opts& opts) : Widget(master) { create("listbox", "listbox", opts); }
Toplevel::Toplevel(Widget* master, const Opts& opts) : Widget(master) { create("toplevel", "toplevel", opts); }
Menu::Menu(Widget* master, const Opts& opts) : Widget(master) { create("menu", "menu", opts); }
Text::Text(Widget* master, const Opts& opts) : Widget(master) { create("text", "text", opts); }
Canvas::Canvas(Widget* master, const Opts& opts) : Widget(master) { create("canvas", "canvas", opts); }

// Canvas
int Canvas::create_item(const std::string& type, const std::vector<double>& coordinates, const Opts& opts) {
    Args words{path_, "create", type};
    for (double c : coordinates) words.push_back(number_text(c));
    add_opts(words, opts, this);
    return parse_int(run(words));
}
void Canvas::delete_(const std::string& tag) { run({path_, "delete", tag}); }
int Canvas::create_oval(double x1, double y1, double x2, double y2, const Opts& o) { return create_item("oval", {x1, y1, x2, y2}, o); }
int Canvas::create_rectangle(double x1, double y1, double x2, double y2, const Opts& o) { return create_item("rectangle", {x1, y1, x2, y2}, o); }
int Canvas::create_text(double x, double y, const Opts& o) { return create_item("text", {x, y}, o); }
int Canvas::create_line(const std::vector<double>& points, const Opts& o) { return create_item("line", points, o); }
int Canvas::create_polygon(const std::vector<double>& points, const Opts& o) { return create_item("polygon", points, o); }
int Canvas::create_window(double x, double y, const Opts& o) { return create_item("window", {x, y}, o); }
int Canvas::create_image(double x, double y, const Opts& o) { return create_item("image", {x, y}, o); }
std::optional<std::vector<double>> Canvas::bbox(const std::string& tag) {
    const std::string result = run({path_, "bbox", tag});
    if (result.empty()) return std::nullopt;
    return double_list(result);
}
std::optional<std::vector<double>> Canvas::bbox(int id) { return bbox(std::to_string(id)); }
void Canvas::tag_lower(const std::string& tag, const std::string& below) {
    Args words{path_, "lower", tag};
    if (!below.empty()) words.push_back(below);
    run(words);
}
void Canvas::tag_lower(int id, int below) { run({path_, "lower", std::to_string(id), std::to_string(below)}); }
std::vector<int> Canvas::find_all() { return int_list(run({path_, "find", "all"})); }
std::vector<int> Canvas::find_withtag(const std::string& tag) { return int_list(run({path_, "find", "withtag", tag})); }
std::string Canvas::type(int id) { return run({path_, "type", std::to_string(id)}); }
std::vector<std::string> Canvas::gettags(int id) { return tkcore::split(run({path_, "gettags", std::to_string(id)})); }
std::string Canvas::itemcget(int id, const std::string& option) { return run({path_, "itemcget", std::to_string(id), option_name(option)}); }
std::vector<double> Canvas::coords(int id) { return double_list(run({path_, "coords", std::to_string(id)})); }
void Canvas::itemconfigure(int id, const Opts& opts) {
    Args words{path_, "itemconfigure", std::to_string(id)};
    add_opts(words, opts, this);
    run(words);
}
void Canvas::yview_scroll(int amount, const std::string& what) { run({path_, "yview", "scroll", std::to_string(amount), what}); }
void Canvas::yview_moveto(double fraction) { run({path_, "yview", "moveto", number_text(fraction)}); }

// Text
std::string Text::get(const std::string& first, const std::string& last) { return run({path_, "get", first, last}); }
void Text::insert(const std::string& index, const std::string& text, const std::string& tags) {
    Args words{path_, "insert", index, text};
    if (!tags.empty()) words.push_back(tags);
    run(words);
}
void Text::delete_(const std::string& first, const std::string& last) { run({path_, "delete", first, last}); }
void Text::see(const std::string& index) { run({path_, "see", index}); }
std::string Text::index(const std::string& index) { return run({path_, "index", index}); }
std::pair<double, double> Text::yview() {
    const std::vector<double> view = double_list(run({path_, "yview"}));
    if (view.size() != 2) throw TclError("yview: nieoczekiwany wynik");
    return {view[0], view[1]};
}
void Text::tag_configure(const std::string& tag, const Opts& opts) {
    Args words{path_, "tag", "configure", tag};
    add_opts(words, opts, this);
    run(words);
}

// Listbox
std::vector<int> Listbox::curselection() const { return int_list(run({path_, "curselection"})); }
void Listbox::insert(const std::string& index, const std::string& text) { run({path_, "insert", index, text}); }
void Listbox::delete_(const std::string& first, const std::string& last) {
    Args words{path_, "delete", first};
    if (!last.empty()) words.push_back(last);
    run(words);
}

// Toplevel
void Toplevel::title(const std::string& text) { run({"wm", "title", path_, text}); }
void Toplevel::geometry(const std::string& spec) { run({"wm", "geometry", path_, spec}); }
void Toplevel::transient(Widget* master) { run({"wm", "transient", path_, master ? master->path() : std::string()}); }
void Toplevel::grab_set() { run({"grab", "set", path_}); }
void Toplevel::grab_release() { run({"grab", "release", path_}); }
void Toplevel::protocol(const std::string& name, std::function<void()> callback) {
    run({"wm", "protocol", path_, name, register_command(std::move(callback))});
}

// Menu
void Menu::add_cascade(const Opts& opts) {
    Args words{path_, "add", "cascade"};
    add_opts(words, opts, this);
    run(words);
}
void Menu::add_command(const Opts& opts) {
    Args words{path_, "add", "command"};
    add_opts(words, opts, this);
    run(words);
}
void Menu::add_separator() { run({path_, "add", "separator"}); }
std::vector<Menu::Entry> Menu::entries() {
    std::vector<Entry> out;
    std::string last = run({path_, "index", "end"});
    if (last.empty() || last == "none") return out;
    int count = std::stoi(last);
    for (int i = 0; i <= count; ++i) {
        std::string index = std::to_string(i);
        Entry entry;
        entry.type = run({path_, "type", index});
        if (entry.type != "separator" && entry.type != "tearoff") {
            entry.label = run({path_, "entrycget", index, "-label"});
            if (entry.type == "cascade") entry.menu = run({path_, "entrycget", index, "-menu"});
        }
        out.push_back(std::move(entry));
    }
    return out;
}
void Menu::unpost() { run({path_, "unpost"}); }
void Menu::tk_popup(int x, int y) { run({"tk_popup", path_, std::to_string(x), std::to_string(y)}); }
void Menu::grab_release() { run({"grab", "release", path_}); }

// ------------------------------------------------------------------------------ obrazy
PhotoImage::PhotoImage(Widget*, int width, int height, const std::vector<uint8_t>& rgba) {
    tkcore::init();
    name_ = "pyimage" + std::to_string(++Registry::get().image_counter);
    run({"image", "create", "photo", name_, "-width", std::to_string(width), "-height", std::to_string(height)});
    try {
        tkcore::put_photo(name_, width, height, rgba);
    } catch (...) {
        try {
            run({"image", "delete", name_});
        } catch (...) {
        }
        throw;
    }
}
PhotoImage::~PhotoImage() {
    if (name_.empty()) return;
    try {
        run({"image", "delete", name_});
    } catch (...) {
    }
}

// ------------------------------------------------------------------------------ okna dialogowe
namespace messagebox {
namespace {
bool g_headless = false;                    // tryb testu: okna modalne nie są pokazywane (blokowałyby test bez człowieka)
std::vector<std::string> g_intercepted;     // „funkcja | tytuł | treść” każdego pominiętego okna
}  // namespace
void set_headless(bool on) { g_headless = on; if (on) g_intercepted.clear(); }
bool headless() { return g_headless; }
std::vector<std::string> take_intercepted() { return std::exchange(g_intercepted, {}); }
namespace {
std::string show(const char* function, const char* type, const char* icon, const std::string& title, const std::string& message,
                 Widget* parent) {
    Registry& r = Registry::get();
    auto observers = r.observers.find(function);
    if (observers != r.observers.end()) {
        for (const auto& observer : std::vector<std::function<void(const std::string&, const std::string&)>>(observers->second)) {
            observer(title, message);
        }
    }
    if (g_headless) {
        g_intercepted.push_back(std::string(function) + " | " + title + " | " + message);
        return std::string(type) == "yesno" ? "yes" : std::string(type) == "yesnocancel" ? "no" : "ok";
    }
    Args words{"tk_messageBox", "-type", type, "-icon", icon, "-title", title, "-message", message};
    if (parent) {
        words.push_back("-parent");
        words.push_back(parent->path());
    }
    return run(words);
}
}  // namespace
void showerror(const std::string& title, const std::string& message, Widget* parent) { show("showerror", "ok", "error", title, message, parent); }
void showinfo(const std::string& title, const std::string& message, Widget* parent) { show("showinfo", "ok", "info", title, message, parent); }
void showwarning(const std::string& title, const std::string& message, Widget* parent) { show("showwarning", "ok", "warning", title, message, parent); }
bool askyesno(const std::string& title, const std::string& message, Widget* parent) {
    return show("askyesno", "yesno", "question", title, message, parent) == "yes";
}
std::optional<bool> askyesnocancel(const std::string& title, const std::string& message, Widget* parent) {
    const std::string answer = show("askyesnocancel", "yesnocancel", "question", title, message, parent);
    if (answer == "yes") return true;
    if (answer == "no") return false;
    return std::nullopt;
}
void add_observer(const std::string& function, std::function<void(const std::string&, const std::string&)> observer) {
    Registry::get().observers[function].push_back(std::move(observer));
}
}  // namespace messagebox

namespace filedialog {
std::string asksaveasfilename(const Opts& opts) {
    if (messagebox::headless()) return "";
    Args words{"tk_getSaveFile"};
    add_opts(words, opts, nullptr);
    return run(words);
}
std::string askopenfilename(const Opts& opts) {
    if (messagebox::headless()) return "";
    Args words{"tk_getOpenFile"};
    add_opts(words, opts, nullptr);
    return run(words);
}
}  // namespace filedialog

namespace colorchooser {
std::pair<std::optional<std::string>, std::optional<std::string>> askcolor(const std::string& color, Widget* parent) {
    if (messagebox::headless()) return {std::nullopt, std::nullopt};
    Args words{"tk_chooseColor"};
    if (!color.empty()) {
        words.push_back("-initialcolor");
        words.push_back(color);
    }
    if (parent) {
        words.push_back("-parent");
        words.push_back(parent->path());
    }
    const std::string chosen = run(words);
    if (chosen.empty()) return {std::nullopt, std::nullopt};
    const Args rgb = tkcore::split(run({"winfo", "rgb", ".", chosen}));
    std::string triple;
    if (rgb.size() == 3) triple = std::to_string(parse_int(rgb[0]) / 256) + " " + std::to_string(parse_int(rgb[1]) / 256) + " " + std::to_string(parse_int(rgb[2]) / 256);
    return {triple, chosen};
}
}  // namespace colorchooser

namespace simpledialog {
namespace {
const char* kAskFloatBody = R"TCL(
set w .mebel_askfloat
catch {destroy $w}
toplevel $w
wm title $w $title
wm resizable $w 0 0
if {$parent ne "" && [winfo exists $parent]} {
    wm transient $w [winfo toplevel $parent]
}
ttk::label $w.prompt -text $prompt
ttk::entry $w.entry -textvariable ::mebel::askfloat_text -width 24
ttk::frame $w.buttons
ttk::button $w.buttons.ok -text OK -command {set ::mebel::askfloat_state ok}
ttk::button $w.buttons.cancel -text Cancel -command {set ::mebel::askfloat_state cancel}
pack $w.prompt -padx 12 -pady {12 4} -anchor w
pack $w.entry -padx 12 -pady 4 -fill x
pack $w.buttons -padx 12 -pady 12 -anchor e
pack $w.buttons.cancel -side right
pack $w.buttons.ok -side right -padx {0 6}
bind $w <Return> {set ::mebel::askfloat_state ok}
bind $w <Escape> {set ::mebel::askfloat_state cancel}
wm protocol $w WM_DELETE_WINDOW {set ::mebel::askfloat_state cancel}
set ::mebel::askfloat_text $initial
$w.entry selection range 0 end
focus $w.entry
update idletasks
catch {grab set $w}
set result {}
while {1} {
    set ::mebel::askfloat_state {}
    vwait ::mebel::askfloat_state
    if {$::mebel::askfloat_state ne "ok"} break
    set text [string trim $::mebel::askfloat_text]
    if {![string is double -strict $text]} {
        tk_messageBox -parent $w -icon warning -title "Illegal value" -message "Not a floating point value.\nPlease try again"
        continue
    }
    if {$min ne "" && $text < $min} {
        tk_messageBox -parent $w -icon warning -title "Too small" -message "The allowed minimum value is $min.\nPlease try again."
        continue
    }
    if {$max ne "" && $text > $max} {
        tk_messageBox -parent $w -icon warning -title "Too large" -message "The allowed maximum value is $max.\nPlease try again."
        continue
    }
    set result $text
    break
}
catch {grab release $w}
destroy $w
return $result
)TCL";

std::string option_text(const Opts& opts, const char* key) {
    for (const auto& [name, value] : opts) {
        if (name == key) return word(value, nullptr);
    }
    return {};
}
}  // namespace

std::optional<double> askfloat(const std::string& title, const std::string& prompt, const Opts& options) {
    if (messagebox::headless()) return std::nullopt;
    static bool defined = false;
    if (!defined) {
        run({"namespace", "eval", "::mebel", ""});
        run({"proc", "::mebel::askfloat", "parent title prompt initial min max", kAskFloatBody});
        defined = true;
    }
    const std::string result = run({"::mebel::askfloat", option_text(options, "parent"), title, prompt, option_text(options, "initialvalue"),
                                    option_text(options, "minvalue"), option_text(options, "maxvalue")});
    if (result.empty()) return std::nullopt;
    return parse_double(result);
}
}  // namespace simpledialog

// ------------------------------------------------------------------------------ czcionki
namespace font {
namespace {
Actual parse_actual(const std::string& text) {
    Actual result;
    const Args items = tkcore::split(text);
    for (std::size_t i = 0; i + 1 < items.size(); i += 2) {
        const std::string& key = items[i];
        const std::string& value = items[i + 1];
        if (key == "-family") result.family = value;
        else if (key == "-size") result.size = parse_int(value);
        else if (key == "-weight") result.weight = value;
        else if (key == "-slant") result.slant = value;
        else if (key == "-underline") result.underline = parse_int(value);
        else if (key == "-overstrike") result.overstrike = parse_int(value);
    }
    return result;
}
}  // namespace

Actual actual(Widget*, const std::string& spec) { return parse_actual(run({"font", "actual", spec})); }

Font::Font(Widget* root, const std::string& spec) {
    const Actual a = tk::font::actual(root, spec);
    name_ = "pyfont" + std::to_string(++Registry::get().font_counter);
    run({"font", "create", name_, "-family", a.family, "-size", std::to_string(a.size), "-weight", a.weight, "-slant", a.slant,
         "-underline", std::to_string(a.underline), "-overstrike", std::to_string(a.overstrike)});
}
Font::Font(Widget*, const Opts& options) {
    name_ = "pyfont" + std::to_string(++Registry::get().font_counter);
    Args words{"font", "create", name_};
    add_opts(words, options, nullptr);
    run(words);
}
int Font::measure(const std::string& text) const { return parse_int(run({"font", "measure", name_, text})); }
Actual Font::actual() const { return parse_actual(run({"font", "actual", name_})); }
std::string Font::name() const { return name_; }
}  // namespace font

}  // namespace tk
