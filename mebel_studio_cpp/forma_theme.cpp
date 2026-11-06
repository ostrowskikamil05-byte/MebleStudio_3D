#include "forma_theme.h"

#include <string>

#include "tkcore.h"
#include "ttk.h"

namespace forma_theme {

void apply(tk::Widget* root) {
    // Klasyczne widgety tk (nie ttk): domyślne kolory z bazy opcji Tk, żeby wszystkie okna i dialogi wyglądały spójnie.
    auto option = [](const char* pattern, const char* value) { tkcore::eval({"option", "add", pattern, value}); };
    for (const char* kind : {"Frame", "Label", "Toplevel", "Canvas"}) option((std::string("*") + kind + ".background").c_str(), BG);
    option("*Label.foreground", TEXT);
    option("*Text.background", CARD); option("*Text.foreground", TEXT); option("*Text.relief", "flat");
    option("*Text.highlightThickness", "1"); option("*Text.highlightBackground", BORDER); option("*Text.highlightColor", ACCENT);
    option("*Text.selectBackground", ACCENT_SOFT); option("*Text.selectForeground", TEXT);
    option("*Listbox.background", CARD); option("*Listbox.foreground", TEXT); option("*Listbox.relief", "flat");
    option("*Listbox.highlightThickness", "1"); option("*Listbox.highlightBackground", BORDER);
    option("*Listbox.selectBackground", ACCENT_SOFT); option("*Listbox.selectForeground", TEXT);
    option("*Entry.background", CARD); option("*Entry.relief", "flat"); option("*Entry.highlightThickness", "1"); option("*Entry.highlightBackground", BORDER);
    option("*Button.background", CARD); option("*Button.activeBackground", HOVER); option("*Button.relief", "flat"); option("*Button.borderWidth", "1");
    option("*Menu.background", CARD); option("*Menu.foreground", TEXT); option("*Menu.activeBackground", ACCENT_SOFT);
    option("*Menu.activeForeground", TEXT); option("*Menu.relief", "flat"); option("*Menu.borderWidth", "1");
    option("*Menu.font", "{Segoe UI} 10");
    root->configure({{"background", BG}});
    ttk::Style s(root);
    s.theme_use("clam");
    const std::string font = "{Segoe UI} 10", bold = "{Segoe UI} 10 bold";

    s.configure(".", {{"font", font}, {"background", BG}, {"foreground", TEXT}, {"bordercolor", BORDER}, {"lightcolor", BG}, {"darkcolor", BG},
                      {"troughcolor", HOVER}, {"focuscolor", ACCENT}, {"selectbackground", ACCENT_SOFT}, {"selectforeground", TEXT}});
    s.configure("TFrame", {{"background", BG}});
    s.configure("Panel.TFrame", {{"background", PANEL}});
    s.configure("Card.TFrame", {{"background", CARD}, {"relief", "solid"}, {"borderwidth", 1}});
    s.configure("Header.TFrame", {{"background", CARD}});
    s.configure("TLabel", {{"background", BG}, {"foreground", TEXT}});
    s.configure("Muted.TLabel", {{"foreground", MUTED}});
    s.configure("Title.TLabel", {{"font", "{Segoe UI} 14 bold"}, {"foreground", TEXT}});
    s.configure("Panel.TLabel", {{"background", PANEL}});
    s.configure("TSeparator", {{"background", BORDER}});

    // Przyciski: białe z cienką ramką; akcent zielony.
    s.configure("TButton", {{"padding", "10 6"}, {"background", CARD}, {"foreground", TEXT}, {"bordercolor", BORDER}, {"borderwidth", 1}, {"relief", "flat"}, {"focuscolor", CARD}});
    s.map("TButton", "background", {{"disabled", BG}, {"pressed", ACCENT_SOFT}, {"active", HOVER}});
    s.map("TButton", "foreground", {{"disabled", MUTED}});
    // Wiersz listy poleceń w panelu bocznym (jak "Rysuj", "Edytuj szkic" na makiecie).
    s.configure("Ribbon.TButton", {{"padding", "10 9"}, {"anchor", "w"}, {"background", PANEL}, {"bordercolor", PANEL}, {"borderwidth", 0}, {"font", "{Segoe UI} 10"}});
    s.map("Ribbon.TButton", "background", {{"pressed", ACCENT_SOFT}, {"active", HOVER}});
    s.configure("RibbonOn.TButton", {{"padding", "10 9"}, {"anchor", "w"}, {"background", ACCENT_SOFT}, {"bordercolor", ACCENT_SOFT}, {"borderwidth", 0}, {"font", "{Segoe UI} 10"}});
    s.map("RibbonOn.TButton", "background", {{"pressed", ACCENT_SOFT}, {"active", ACCENT_SOFT}});
    s.configure("Accent.TButton", {{"background", ACCENT}, {"foreground", "white"}, {"bordercolor", ACCENT}, {"font", bold}});
    s.map("Accent.TButton", "background", {{"disabled", "#a8b79c"}, {"pressed", "#3f5a33"}, {"active", ACCENT_HOVER}});
    s.map("Accent.TButton", "foreground", {{"disabled", "white"}});
    s.configure("Chip.TButton", {{"padding", "8 3"}, {"font", "{Segoe UI} 9"}, {"background", CARD}, {"bordercolor", BORDER}});
    s.map("Chip.TButton", "background", {{"pressed", ACCENT_SOFT}, {"active", HOVER}});
    s.configure("ChipOn.TButton", {{"padding", "8 3"}, {"font", "{Segoe UI} 9 bold"}, {"background", ACCENT}, {"foreground", "white"}, {"bordercolor", ACCENT}});
    s.map("ChipOn.TButton", "background", {{"active", ACCENT_HOVER}});
    s.configure("Tool.TButton", {{"padding", "6 6"}, {"background", PANEL}, {"bordercolor", PANEL}});
    s.map("Tool.TButton", "background", {{"pressed", ACCENT_SOFT}, {"active", HOVER}});
    s.configure("Section.TButton", {{"anchor", "w"}, {"padding", "8 6"}, {"font", bold}, {"background", PANEL}, {"bordercolor", PANEL}});
    s.map("Section.TButton", "background", {{"active", HOVER}});

    // Pola tekstowe i listy rozwijane.
    for (const char* name : {"TEntry", "TSpinbox", "TCombobox"})
        s.configure(name, {{"fieldbackground", CARD}, {"background", CARD}, {"foreground", TEXT}, {"bordercolor", BORDER}, {"padding", "6 5"}, {"arrowcolor", MUTED}, {"insertcolor", TEXT}});
    s.map("TEntry", "bordercolor", {{"focus", ACCENT}});
    s.map("TCombobox", "bordercolor", {{"focus", ACCENT}});
    s.map("TCombobox", "fieldbackground", {{"readonly", CARD}});
    s.map("TCombobox", "selectbackground", {{"readonly", CARD}});
    s.map("TCombobox", "selectforeground", {{"readonly", TEXT}});
    s.configure("TCheckbutton", {{"background", BG}, {"indicatorbackground", CARD}, {"indicatorforeground", ACCENT}, {"padding", 3}});
    s.configure("TRadiobutton", {{"background", BG}, {"indicatorbackground", CARD}, {"indicatorforeground", ACCENT}, {"padding", 3}});
    s.configure("Panel.TCheckbutton", {{"background", PANEL}, {"indicatorbackground", CARD}, {"indicatorforeground", ACCENT}, {"padding", 5}});
    s.configure("Panel.TRadiobutton", {{"background", PANEL}, {"indicatorbackground", CARD}, {"indicatorforeground", ACCENT}, {"padding", 5}});
    s.map("Panel.TCheckbutton", "background", {{"active", PANEL}});
    s.map("Panel.TRadiobutton", "background", {{"active", PANEL}});
    s.map("Panel.TCheckbutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});
    s.map("Panel.TRadiobutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});
    s.map("TCheckbutton", "background", {{"active", BG}});
    s.map("TRadiobutton", "background", {{"active", BG}});
    s.map("TCheckbutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});
    s.map("TRadiobutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});

    // Zakładki dokumentów (Projekt 3D / Formatki / Kosztorys / Rozkrój / Ustawienia): pastylki, aktywna zielona.
    s.configure("TNotebook", {{"background", BG}, {"bordercolor", BG}, {"tabmargins", "10 6 10 4"}, {"borderwidth", 0}, {"lightcolor", BG}, {"darkcolor", BG}});
    s.configure("TNotebook.Tab", {{"padding", "16 6"}, {"font", "{Segoe UI} 10"}, {"background", BG}, {"foreground", TEXT}, {"bordercolor", BG}, {"lightcolor", BG}, {"darkcolor", BG}, {"focuscolor", BG}});
    s.map("TNotebook.Tab", "background", {{"selected", ACCENT}, {"active", HOVER}});
    s.map("TNotebook.Tab", "foreground", {{"selected", "white"}});
    s.map("TNotebook.Tab", "bordercolor", {{"selected", ACCENT}});
    s.map("TNotebook.Tab", "lightcolor", {{"selected", ACCENT}});
    s.map("TNotebook.Tab", "darkcolor", {{"selected", ACCENT}});
    s.configure("Rail.TNotebook", {{"background", PANEL}, {"tabmargins", "0 0 0 0"}, {"borderwidth", 0}});
    s.configure("HiddenInspector.TNotebook", {{"background", PANEL}, {"tabmargins", "0 0 0 0"}, {"borderwidth", 0}});
    s.configure("Inspector.TNotebook", {{"background", PANEL}, {"tabmargins", "6 4 6 0"}, {"borderwidth", 0}});
    s.configure("Inspector.TNotebook.Tab", {{"padding", "10 6"}, {"font", bold}, {"background", PANEL}, {"foreground", MUTED}});
    s.map("Inspector.TNotebook.Tab", "background", {{"selected", CARD}, {"active", HOVER}});
    s.map("Inspector.TNotebook.Tab", "foreground", {{"selected", ACCENT}});

    // Tabele i drzewa.
    s.configure("Treeview", {{"rowheight", 29}, {"background", CARD}, {"fieldbackground", CARD}, {"foreground", TEXT}, {"bordercolor", BORDER}, {"borderwidth", 0}});
    s.map("Treeview", "background", {{"selected", ACCENT_SOFT}});
    s.map("Treeview", "foreground", {{"selected", TEXT}});
    s.configure("Treeview.Heading", {{"font", bold}, {"padding", 7}, {"background", PANEL}, {"foreground", MUTED}, {"relief", "flat"}});
    s.map("Treeview.Heading", "background", {{"active", HOVER}});

    // Ramki grup, paski przewijania, suwaki, separatory paneli.
    s.configure("TLabelframe", {{"background", BG}, {"bordercolor", BORDER}, {"relief", "solid"}, {"borderwidth", 1}});
    s.configure("TLabelframe.Label", {{"background", BG}, {"foreground", MUTED}, {"font", bold}});
    s.configure("TScrollbar", {{"background", BORDER}, {"troughcolor", BG}, {"bordercolor", BG}, {"arrowcolor", MUTED}, {"gripcount", 0}, {"relief", "flat"}});
    s.map("TScrollbar", "background", {{"active", "#cfcabf"}});
    s.configure("TScale", {{"background", BG}, {"troughcolor", BORDER}});
    s.configure("TProgressbar", {{"background", ACCENT}, {"troughcolor", BORDER}});
    s.configure("TPanedwindow", {{"background", BG}, {"sashwidth", 7}});
    s.configure("Sash", {{"sashthickness", 7}, {"gripcount", 0}});
    s.configure("RibbonGroup.TLabel", {{"foreground", MUTED}, {"font", "{Segoe UI} 8 bold"}});
}

}  // namespace forma_theme
