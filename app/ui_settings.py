"""Interface and viewport preferences in an ordinary detachable workspace tab."""
import tkinter as tk
from tkinter import ttk
from ui_preferences import DEFAULTS, ENGINE_LABELS, validate_preferences
from responsive_ui import FieldRow, monitor_info


def open_settings(app):
    existing=getattr(app,'_interface_settings_document',None)
    if existing is not None:
        try:
            if existing.winfo_exists():app.workspace.select(existing.document);return existing
        except tk.TclError:pass
    d=app.dialog('Ustawienia interfejsu i 3D','620x740');app._interface_settings_document=d
    controller=app.ui_layout;current=controller.preferences();vars={};choices={}
    ttk.Label(d,text='Okno, panele i jakość widoku',style='Title.TLabel',padding=12).pack(fill='x')
    ttk.Label(d,text='Te ustawienia dotyczą programu i zapisują się na tym komputerze. Rozmiar okna nie zmienia rozdzielczości Windows.',wraplength=520,padding=(12,0,12,8)).pack(fill='x')
    info=monitor_info(app);l,t,r,b=info['workarea']
    ttk.Label(d,text=f'Obszar roboczy monitora: {r-l} × {b-t} px · DPI: {info["dpi"]}',padding=(12,0,12,8)).pack(fill='x')
    def combo(key,label,values):
        frame=ttk.Frame(d,padding=(12,4));frame.pack(fill='x')
        ttk.Label(frame,text=label).pack(anchor='w')
        if key=='ui_scale' and current[key] not in values.values():
            values={**values,f'{current[key]*100:g}% (zapisana skala)':current[key]}
        reverse={value:name for name,value in values.items()};var=tk.StringVar(value=reverse[current[key]])
        ttk.Combobox(frame,textvariable=var,values=list(values),state='readonly').pack(fill='x')
        vars[key]=var;choices[key]=values
    def number(key,label):
        var=tk.StringVar(value=str(current[key]));row=FieldRow(d,label,textvariable=var,width=15)
        row.pack(fill='x',padx=12,pady=4);vars[key]=var
    combo('window_mode','Rozmiar okna',{'Automatycznie do monitora':'auto','Własny / wybrany preset':'custom'})
    presets=ttk.Frame(d,padding=(12,2));presets.pack(fill='x')
    ttk.Label(presets,text='Presety:').pack(side='left')
    def preset(w,h):
        vars['window_mode'].set('Własny / wybrany preset');vars['window_width'].set(str(w));vars['window_height'].set(str(h))
    from responsive_ui import WrapToolbar
    buttons=WrapToolbar(d);buttons.pack(fill='x',padx=12)
    for w,h in ((900,600),(1280,720),(1366,768),(1920,1080)):
        ttk.Button(buttons,text=f'{w} × {h}',command=lambda w=w,h=h:preset(w,h)).pack(side='left')
    number('window_width','Szerokość okna [px]');number('window_height','Wysokość okna [px]')
    combo('ui_scale','Skala interfejsu',{'75%':.75,'90%':.9,'100%':1.0,'110%':1.1,'125%':1.25,'150%':1.5,'175%':1.75,'200%':2.0})
    number('inspector_width','Szerokość bocznego panelu [px]');number('ribbon_height','Wysokość wstążki [px]')
    ttk.Label(d,text='Przeciągnij separator obok inspektora lub pod wstążką. Limity zachowują miejsce na widok 3D.',wraplength=520,padding=12).pack(fill='x')
    for key,label in [('show_inspector','Pokaż boczny inspektor'),('show_ribbon','Pokaż górną wstążkę'),('show_toolbars','Pokaż paski skrótów nad 3D')]:
        var=tk.BooleanVar(value=current[key]);vars[key]=var;ttk.Checkbutton(d,text=label,variable=var).pack(anchor='w',padx=12,pady=3)
    combo('engine','Silnik widoku 3D',ENGINE_LABELS)
    combo('quality','Jakość 3D',{'Szybki podgląd':'draft','Zrównoważona':'balanced','Wysoka':'high'})
    ttk.Label(d,text='Automatyczny dobór uruchamia natywny silnik C++, a gdy jest niedostępny, wybiera OpenGL lub CPU. Przełączenie na niedostępny silnik zachowuje działający widok. OpenGL zachowuje dotychczasowy tryb wysokiej jakości. Jakość widoku nie zmienia wymiarów produkcyjnych.',wraplength=520,padding=12).pack(fill='x')
    note=tk.StringVar();ttk.Label(d,textvariable=note,wraplength=520,padding=(12,2)).pack(fill='x')
    def apply():
        try:
            result=controller.preferences()
            for key,var in vars.items():
                if key in choices:result[key]=choices[key][var.get()]
                elif key.startswith('show_'):result[key]=var.get()
                else:result[key]=int(var.get())
            controller.apply(validate_preferences(result),geometry=True)
            if controller.persist():note.set('Zapisano ustawienia interfejsu. Projekt mebla nie został zmieniony.')
            else:note.set('Ustawienia zastosowano, ale nie udało się zapisać ich na dysku.')
            d.mark_clean()
        except (ValueError,KeyError,OSError) as error:note.set(str(error))
    def reset():
        for key,var in vars.items():
            value=DEFAULTS[key]
            var.set(next(name for name,item in choices[key].items() if item==value) if key in choices else value)
        note.set('Przywrócono wartości domyślne w formularzu. Naciśnij Zastosuj, aby je zapisać.')
    ttk.Button(d,text='Wartości domyślne',command=reset).pack(fill='x',padx=12,pady=4)
    ttk.Button(d,text='Zastosuj i zapisz ustawienia',command=apply,style='Accent.TButton').pack(fill='x',padx=12,pady=8)
    d.interface_vars=vars;d.interface_choices=choices;d.apply_interface_preferences=apply
    return d
