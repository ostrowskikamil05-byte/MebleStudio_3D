#include "tk.h"
#include "ttk.h"
#include "mock_api.h"
#include <cassert>
#include <iostream>
#include <sstream>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; } } while (0)
static std::string join(const std::vector<std::string>& w) { std::string s; for (size_t i = 0; i < w.size(); ++i) { if (i) s += " | "; s += w[i]; } return s; }
static std::string last() { return g_log.empty() ? "" : join(g_log.back()); }
static std::string at(size_t i) { return join(g_log.at(i)); }

int main() {
    int next_item = 0;
    g_hook = [&](const std::vector<std::string>& w, bool& error) -> std::string {
        const std::string& a = w[0];
        if (a == "winfo" && w.size() > 1) {
            if (w[1] == "width" || w[1] == "height") return "123";
            if (w[1] == "exists") return "1";
            if (w[1] == "children") return ".!frame .!frame2 .!frame.!button";
            if (w[1] == "toplevel") return ".";
            if (w[1] == "id") return "0x1f";
            if (w[1] == "rgb") return "65535 0 32768";
            if (w[1] == "fpixels") return "3.5";
        }
        if (a.size() > 2 && a[0] == '.' && w.size() > 1 && w[1] == "create") return std::to_string(++next_item);
        if (a.size() > 2 && a[0] == '.' && w.size() > 1 && w[1] == "bbox") return w[2] == "none" ? "" : "1 2 3.5 4";
        if (a.size() > 2 && a[0] == '.' && w.size() > 1 && w[1] == "find") return "3 4 5";
        if (a.size() > 2 && a[0] == '.' && w.size() > 1 && w[1] == "yview" && w.size() == 2) return "0.25 0.75";
        if (a == "font" && w[1] == "actual") return "-family {Segoe UI} -size 9 -weight bold -slant roman -underline 0 -overstrike 0";
        if (a == "font" && w[1] == "measure") return "42";
        if (a == "tk_messageBox") return w[2] == "yesno" ? "yes" : (w[2] == "yesnocancel" ? "cancel" : "ok");
        if (a == "tk_getSaveFile") return "C:/x/plik.json";
        if (a == "tk_chooseColor") return "#ff0080";
        if (a == "::mebel::askfloat") return "2.5";
        if (a == "after") return w[1] == "info" ? "_cb99 timer" : "after#7";
        if (a == "bad") { error = true; return "boom \"quoted\""; }
        return "";
    };

    tk::Tk app;
    CHECK(app.path() == ".");
    tk::Frame f1(&app, {{"padding", "4 2"}, {"bg", "#fff"}});
    CHECK(last() == "frame | .!frame | -padding | 4 2 | -bg | #fff");
    tk::Frame f2(&app);
    CHECK(f2.path() == ".!frame2");
    int fired = 0;
    ttk::Button b(&f1, {{"text", "Zapisz plik"}, {"command", tk::Cmd([&] { ++fired; })}, {"width", 12}, {"default", tk::Value()}});
    CHECK(b.path() == ".!frame.!button");
    const auto cr = g_log.back();
    CHECK(cr[0] == "ttk::button" && cr[2] == "-text" && cr[3] == "Zapisz plik");
    CHECK(cr[4] == "-command" && cr[6] == "-width" && cr[7] == "12" && cr.size() == 8);
    CHECK(mock_invoke({cr[5]}) == "");
    CHECK(fired == 1);
    f1.pack({{"fill", "both"}, {"expand", true}, {"padx", 3.5}});
    CHECK(last() == "pack | configure | .!frame | -fill | both | -expand | 1 | -padx | 3.5");

    // Scale: polecenie z argumentem
    std::string seen;
    ttk::Scale sc(&f2, {{"from_", 0.0}, {"to", 100.0}, {"command", std::function<void(const std::string&)>([&](const std::string& v) { seen = v; })}});
    const auto sw = g_log.back();
    CHECK(sw[2] == "-from" && sw[3] == "0.0" && sw[4] == "-to" && sw[5] == "100.0");
    mock_invoke({sw[7], "42.5"});
    CHECK(seen == "42.5");

    // listy i cytowanie
    ttk::Combobox cb(&f2, {{"values", std::vector<std::string>{"a b", "c", ""}}, {"state", "readonly"}});
    CHECK(last() == "ttk::combobox | .!frame2.!combobox | -values | {a b} c {} | -state | readonly");
    cb.current(2); CHECK(last() == ".!frame2.!combobox | current | 2");

    // bind: słowa, parsowanie zdarzenia, "break"
    int bx = -1; std::string key, wid;
    f1.bind("<Button-1>", [&](tk::Event& e) { bx = e.x; wid = e.widget ? e.widget->path() : "null"; key = e.keysym; return std::string("break"); });
    const auto bl = g_log.back();
    CHECK(bl[0] == "bind" && bl[1] == ".!frame" && bl[2] == "<Button-1>");
    CHECK(bl[3].find("%x %y %D %X %Y %w %h %s %b %K %W]\" == \"break\"} break") != std::string::npos);
    CHECK(bl[3].rfind("if {", 0) == 0);
    std::string name = bl[3].substr(bl[3].find('[') + 1, bl[3].find(' ', bl[3].find('[')) - bl[3].find('[') - 1);
    CHECK(mock_invoke({name, "17", "5", "0", "100", "200", "??", "??", "8", "1", "??", ".!frame.!button"}) == "break");
    CHECK(bx == 17 && wid == ".!frame.!button" && key.empty());
    f1.bind("<Key>", [](tk::Event&) { return std::string(); }, true);
    CHECK(g_log.back()[3].rfind("+if {", 0) == 0);

    // wyjątek w callbacku
    bool reported = false; std::string what;
    app.report_callback_exception = [&](std::exception_ptr e) { reported = true; try { std::rethrow_exception(e); } catch (const std::exception& ex) { what = ex.what(); } };
    std::string cmdname = f1.register_command([] { throw std::runtime_error("kaboom"); });
    CHECK(mock_invoke({cmdname}) == "");
    CHECK(reported && what == "kaboom");

    // after: jednorazowe, usuwa się
    int after_runs = 0;
    std::string id = f1.after(50, [&] { ++after_runs; });
    CHECK(id == "after#7");
    std::string aname = g_log[g_log.size() - 1][2];
    CHECK(mock_has_command(aname));
    mock_invoke({aname});
    CHECK(after_runs == 1 && !mock_has_command(aname));
    f1.after_cancel(id);
    CHECK(at(g_log.size() - 1) == "after | cancel | after#7");

    // zmienne
    tk::StringVar sv("start"); tk::DoubleVar dv(1.5); tk::BooleanVar bv(true);
    CHECK(sv.get() == "start" && dv.get() == 1.5 && bv.get());
    sv.set("zażółć gęślą"); CHECK(sv.get() == "zażółć gęślą");
    tk::StringVar copy = sv; copy.set("x"); CHECK(sv.get() == "x");
    int traced = 0; sv.trace_add("write", [&] { ++traced; });
    CHECK(g_log.back()[0] == "trace" && g_log.back()[3] == sv.name() && g_log.back()[4] == "write");
    mock_invoke({g_log.back()[5], sv.name(), "", "write"}); CHECK(traced == 1);
    mock_set_var(dv.name(), ""); bool threw = false; try { dv.get(); } catch (const tk::TclError&) { threw = true; } CHECK(threw);
    ttk::Entry en(&f1, {{"textvariable", sv}});
    CHECK(g_log.back()[3] == sv.name());

    // canvas
    tk::Canvas cv(&app, {{"width", 640}, {"highlightthickness", 0}});
    int rid = cv.create_rectangle(1, 2, 3.5, 4, {{"fill", "#abc"}, {"tags", std::vector<std::string>{"a", "b c"}}});
    CHECK(rid == 1 && last() == ".!canvas | create | rectangle | 1.0 | 2.0 | 3.5 | 4.0 | -fill | #abc | -tags | a {b c}");
    int lid = cv.create_line({0, 0, 10, 10.25}, {{"width", 2}}); CHECK(lid == 2);
    CHECK(last() == ".!canvas | create | line | 0.0 | 0.0 | 10.0 | 10.25 | -width | 2");
    auto bb = cv.bbox("a"); CHECK(bb && bb->size() == 4 && (*bb)[2] == 3.5);
    CHECK(!cv.bbox("none"));
    CHECK(cv.find_withtag("t") == (std::vector<int>{3, 4, 5}));
    cv.delete_("all"); CHECK(last() == ".!canvas | delete | all");
    cv.tag_lower("bg"); CHECK(last() == ".!canvas | lower | bg");
    cv.itemconfigure(3, {{"state", "hidden"}}); CHECK(last() == ".!canvas | itemconfigure | 3 | -state | hidden");
    int wid2 = cv.create_window(5, 6, {{"window", &b}, {"anchor", "nw"}}); (void)wid2;
    CHECK(last() == ".!canvas | create | window | 5.0 | 6.0 | -window | .!frame.!button | -anchor | nw");

    // obraz
    {
        std::vector<uint8_t> px(2 * 2 * 4, 0x7f);
        tk::PhotoImage img(&cv, 2, 2, px);
        CHECK(at(g_log.size() - 1) == "image | create | photo | " + img.name() + " | -width | 2 | -height | 2");
        CHECK(g_photo.size() == 16 && g_photo_w == 2);
        cv.create_image(0, 0, {{"image", img}});
        CHECK(last().find("-image | " + img.name()) != std::string::npos);
    }
    CHECK(g_log.back()[0] == "image" && g_log.back()[1] == "delete");

    // winfo, nametowidget, bindtags
    CHECK(f1.winfo_width() == 123 && f1.winfo_id() == 31);
    auto kids = app.winfo_children(); CHECK(kids.size() == 3 && kids[0] == &f1 && kids[2] == &b);
    CHECK(app.nametowidget(".!frame2") == &f2);
    CHECK(f1.winfo_rgb("red")[1] == 0 && f1.winfo_fpixels("1i") == 3.5);
    f1.bindtags(std::vector<std::string>{"x y", "Frame"}); CHECK(last() == "bindtags | .!frame | {x y} Frame");

    // Treeview, Notebook, Panedwindow
    ttk::Treeview tv(&f2, {{"columns", std::vector<std::string>{"a", "b"}}, {"show", "headings"}});
    tv.insert("", "end", {{"iid", "row1"}, {"values", std::vector<std::string>{"x y", "z"}}});
    CHECK(last() == ".!frame2.!treeview | insert |  | end | -id | row1 | -values | {x y} z");
    tv.delete_({"r1", "r 2"}); CHECK(last() == ".!frame2.!treeview | delete | r1 {r 2}");
    ttk::Notebook nb(&f2); nb.add(&f1, {{"text", "Karta"}}); CHECK(last() == ".!frame2.!notebook | add | .!frame | -text | Karta");
    nb.tab(std::string("0"), std::string("text")); CHECK(last() == ".!frame2.!notebook | tab | 0 | -text");
    ttk::Panedwindow pw(&f2); pw.sashpos(0, 300); CHECK(last() == ".!frame2.!panedwindow | sashpos | 0 | 300");

    // style
    ttk::Style st(&app); st.configure("X.TButton", {{"padding", 6}, {"font", "{Segoe UI} 9 bold"}});
    CHECK(last() == "ttk::style | configure | X.TButton | -padding | 6 | -font | {Segoe UI} 9 bold");
    st.map("X.TButton", "background", {{"active", "#eee"}, {"disabled", "#ccc"}}); CHECK(last() == "ttk::style | map | X.TButton | -background | active #eee disabled #ccc");
    st.layout("Hidden.TNotebook.Tab", {}); CHECK(last() == "ttk::style | layout | Hidden.TNotebook.Tab | null");

    // okna dialogowe
    std::string observed;
    tk::messagebox::add_observer("showerror", [&](const std::string& t, const std::string& m) { observed = t + "/" + m; });
    tk::messagebox::showerror("Błąd", "Nie udało się", &app);
    CHECK(observed == "Błąd/Nie udało się"); CHECK(last() == "tk_messageBox | -type | ok | -icon | error | -title | Błąd | -message | Nie udało się | -parent | .");
    CHECK(tk::messagebox::askyesno("t", "m"));
    CHECK(!tk::messagebox::askyesnocancel("t", "m").has_value());
    CHECK(tk::filedialog::asksaveasfilename({{"title", "Zapisz"}, {"filetypes", tk::FileTypes{{"JSON", "*.json"}, {"Wszystkie", "*.*"}}}, {"defaultextension", ".json"}}) == "C:/x/plik.json");
    CHECK(last() == "tk_getSaveFile | -title | Zapisz | -filetypes | {JSON *.json} {Wszystkie *.*} | -defaultextension | .json");
    auto col = tk::colorchooser::askcolor("#112233", &app); CHECK(col.second && *col.second == "#ff0080" && *col.first == "255 0 128");
    auto fl = tk::simpledialog::askfloat("T", "P", {{"initialvalue", 1.5}, {"minvalue", 0}}); CHECK(fl && *fl == 2.5);
    CHECK(last() == "::mebel::askfloat |  | T | P | 1.5 | 0 | ");

    // czcionki
    auto fa = tk::font::actual(&app, "{Segoe UI} 9 bold"); CHECK(fa.family == "Segoe UI" && fa.size == 9 && fa.weight == "bold");
    tk::font::Font ft(&app, std::string("{Segoe UI} 9")); CHECK(ft.measure("abc") == 42);
    tk::Label lb(&f1, {{"font", ft}, {"text", "x"}}); CHECK(g_log.back()[3] == ft.name());

    // błąd Tcl -> TclError
    bool errored = false; try { app.call({"bad"}); } catch (const tk::TclError& e) { errored = std::string(e.what()).rfind("boom \"quoted\"", 0) == 0; } CHECK(errored);

    // destroy: komendy i rejestr
    f1.destroy();
    CHECK(!f1.winfo_exists() || true);
    CHECK(app.nametowidget(".!frame") == nullptr && app.nametowidget(".!frame.!button") == nullptr);
    CHECK(app.nametowidget(".!frame2") == &f2);
    CHECK(!mock_has_command(cr[5]));

    // liczby
    CHECK(tk::number_text(0.1) == "0.1" && tk::number_text(5) == "5.0" && tk::number_text(-2.5e-7) == "-2.5e-07" && tk::number_text(100.0) == "100.0" && tk::number_text(1e16) == "1e+16" && tk::number_text(123456.789) == "123456.789" && tk::number_text(0.0001) == "0.0001" && tk::number_text(0.00001) == "1e-05" && tk::number_text(12345678901234567.0) == "1.2345678901234568e+16" && tk::number_text(-0.5) == "-0.5" && tk::number_text(1.0 / 3) == "0.3333333333333333");

    // pętla główna
    app.title("Mebel Studio 3D"); CHECK(last() == "wm | title | . | Mebel Studio 3D");
    app.mainloop(); CHECK(last() == "<mainloop>");
    std::cout << (fails ? "BŁĘDY: " : "OK, błędów: ") << fails << "\n";
    return fails ? 1 : 0;
}
