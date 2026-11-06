#include "forma_theme.h"

#include "ttk.h"

namespace forma_theme {

void apply(tk::Widget* root) {
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
    s.configure("Ribbon.TButton", {{"padding", "8 5"}});
    s.configure("Accent.TButton", {{"background", ACCENT}, {"foreground", "white"}, {"bordercolor", ACCENT}, {"font", bold}});
    s.map("Accent.TButton", "background", {{"disabled", "#a8b79c"}, {"pressed", "#3f5a33"}, {"active", ACCENT_HOVER}});
    s.map("Accent.TButton", "foreground", {{"disabled", "white"}});
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
    s.map("TCheckbutton", "background", {{"active", BG}});
    s.map("TRadiobutton", "background", {{"active", BG}});
    s.map("TCheckbutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});
    s.map("TRadiobutton", "indicatorbackground", {{"selected", ACCENT}, {"pressed", ACCENT_SOFT}});

    // Zakładki: płaskie, aktywna wyróżniona kolorem akcentu.
    s.configure("TNotebook", {{"background", BG}, {"bordercolor", BORDER}, {"tabmargins", "0 0 0 0"}, {"borderwidth", 0}});
    s.configure("TNotebook.Tab", {{"padding", "14 8"}, {"font", bold}, {"background", BG}, {"foreground", MUTED}, {"bordercolor", BG}});
    s.map("TNotebook.Tab", "background", {{"selected", CARD}, {"active", HOVER}});
    s.map("TNotebook.Tab", "foreground", {{"selected", ACCENT}});
    s.configure("Inspector.TNotebook", {{"background", PANEL}});
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
