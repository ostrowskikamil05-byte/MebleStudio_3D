"""Fast room, shared worktop and corner controls in the existing workspace."""
import tkinter as tk
from tkinter import ttk


def _guard(app,title,fn):
    def run():
        try:return fn()
        except (ValueError,OSError,KeyError) as error:
            app.log.error('%s: %s',title,error);app.status.set(str(error))
    return run


def corner_dialog(app):
    from corner_furniture import default_corner_config,read_corner_config,create_corner
    current=app.selected_part();group=current.group if current else None
    cfg=read_corner_config(app.project,group) if group else None;editing=bool(cfg)
    cfg=cfg or default_corner_config(app.project)
    if not editing and app.project.parts:
        from furniture_layout import group_bounds,groups
        cfg['origin']=[max(group_bounds(app.project,g)[1][0] for g in groups(app.project))+100,0,0]
    doc=app.dialog('Narożnik — konstrukcja L / ścięta / zaokrąglona','560x750')
    ttk.Label(doc,text='Korpus narożny · rzeczywisty obrys dna i półek',padding=12,font=('Segoe UI',11,'bold')).pack(fill='x')
    fields={key:app.field(doc,label,cfg.get(key,value)) for key,label,value in [('name','Nazwa','Narożnik'),('width','Szerokość X [mm]',900),('depth','Głębokość Y [mm]',900),('height','Wysokość [mm]',720),('arm_depth','Głębokość ramion [mm]',560),('thickness','Grubość płyty [mm]',18),('radius','Promień naroża [mm]',100),('shelves','Liczba półek',1)]}
    combos={}
    for key,label,choices in [('shape','Kształt',{'L':'L','Ścięty':'chamfer','Zaokrąglony':'rounded'}),('side','Wariant',{'Lewy':'left','Prawy':'right'}),('top_mode','Góra',{'Pełna płyta':'panel','Listwy pod wspólny blat':'rails'})]:
        row=ttk.Frame(doc,padding=(15,3));row.pack(fill='x');ttk.Label(row,text=label).pack(side='left')
        var=tk.StringVar(value=next((k for k,v in choices.items() if v==cfg.get(key)),next(iter(choices))))
        ttk.Combobox(row,textvariable=var,values=list(choices),state='readonly',width=28).pack(side='right');combos[key]=(var,choices)
    ttk.Label(doc,text='Otwarty korpus. Fronty i okucia narożne dobierz do wybranego mechanizmu; ten generator nie przypisuje zawiasów skośnych.',wraplength=480,padding=15).pack(fill='x')
    def apply():
        doc.ensure_current_project(raise_error=True);draft=dict(cfg)
        for key,var in fields.items():draft[key]=var.get() if key=='name' else app.number(var)
        for key,(var,choices) in combos.items():draft[key]=choices[var.get()]
        model,actual=create_corner(app.project,draft,group if editing else None)
        app.snapshot();app.project=model;app.selected=next(p.id for p in model.parts if p.group==actual)
        app.selected_scene=None;app.refresh();app.fit();doc.destroy();app.workspace.select(app.design_page)
    ttk.Button(doc,text='Zapisz narożnik' if editing else 'Dodaj narożnik',command=_guard(app,'Narożnik',apply),style='Accent.TButton').pack(fill='x',padx=15,pady=8)
    return doc


def shared_worktop_dialog(app):
    from workspace_worktops import install_shared_worktop,read_shared_worktop
    from furniture_layout import groups
    options=[g for g in groups(app.project) if not read_shared_worktop(app.project,g)]
    if not options:raise ValueError('Najpierw dodaj szafki pod blat.')
    doc=app.dialog('Jeden wspólny blat dla szafek','580x710')
    ttk.Label(doc,text='Wybierz szafki, które tworzą połączony ciąg lub układ L / U.',padding=15,wraplength=500).pack(fill='x')
    choices={}
    for name in options:
        var=tk.BooleanVar(value=len(options)<=6);choices[name]=var
        ttk.Checkbutton(doc,text=name,variable=var).pack(anchor='w',padx=15,pady=2)
    thickness=app.field(doc,'Grubość [mm]',38);overhang=app.field(doc,'Nawis [mm]',20)
    name=app.field(doc,'Nazwa blatu','Wspólny blat')
    ttk.Label(doc,text='Usuwa indywidualne blaty kreatora i tworzy listwy wzmacniające. Niepołączone szafki lub różne wysokości wymagają najpierw wyrównania.',wraplength=500,padding=15).pack(fill='x')
    def apply():
        doc.ensure_current_project(raise_error=True)
        result,group=install_shared_worktop(app.project,[g for g,v in choices.items() if v.get()],thickness=app.number(thickness),overhang=app.number(overhang),name=name.get())
        app.snapshot();app.project=result;app.selected=next(p.id for p in result.parts if p.group==group);app.refresh();app.fit()
        doc.destroy();app.workspace.select(app.design_page)
    ttk.Button(doc,text='Utwórz wspólny blat',command=_guard(app,'Wspólny blat',apply),style='Accent.TButton').pack(fill='x',padx=15,pady=8)
    return doc


def install(app):
    from room_scene_ui import RoomScenePanel
    from quick_studio_ui import _button
    panel=RoomScenePanel(app.inspector_tabs,app);app.room_scene_panel=panel;app.inspector_tabs.add(panel,text='Pomieszczenie')
    app.corner_dialog=_guard(app,'Narożnik',lambda:corner_dialog(app))
    app.shared_worktop_dialog=_guard(app,'Wspólny blat',lambda:shared_worktop_dialog(app))
    from corner_quick_ui import CornerPanel
    app.corner_panel=CornerPanel(app.inspector_tabs,app)
    app.inspector_tabs.add(app.corner_panel,text='Narożnik')
    app.inspector_tabs.tab(1,text='CAD');app.inspector_tabs.tab(panel,text='Scena')
    def show_corner():
        if hasattr(app,'ui_layout'):app.ui_layout.show_inspector(True)
        app.workspace.select(app.design_page);app.corner_panel.refresh();app.inspector_tabs.select(app.corner_panel)
        return app.corner_panel
    app.corner_dialog=show_corner
    _button(app.ribbon_pages['Układ'],'Narożnik…',app.corner_dialog)
    _button(app.ribbon_pages['Układ'],'Wspólny blat…',app.shared_worktop_dialog)
    tolerance=ttk.Spinbox(app.ribbon_pages['Układ'],from_=5,to=200,increment=5,width=4,textvariable=app.layout_tolerance)
    tolerance.pack(side='left',padx=3);ttk.Label(app.ribbon_pages['Układ'],text='mm').pack(side='left')
    def show_scene():
        if hasattr(app,'ui_layout'):app.ui_layout.show_inspector(True)
        app.workspace.select(app.design_page);app.inspector_tabs.select(panel)
    _button(app.ribbon_pages['Scena'],'Wyposażenie i oświetlenie',show_scene)
    def set_brightness(value):
        if app.project.scene.get('brightness',1)==value:return
        app.snapshot();app.project.scene['brightness']=value;panel.refresh();app.draw()
    for label,value in [('Jasne pomieszczenie',1.25),('Dzienne',1.0),('Ciemne',.35)]:_button(app.ribbon_pages['Scena'],label,lambda v=value:set_brightness(v))
    def toggle(key):
        app.snapshot();app.project.scene[key]=not app.project.scene.get(key,True);panel.refresh();app.draw()
    _button(app.ribbon_pages['Scena'],'LED wł. / wył.',lambda:toggle('led_enabled'))
    _button(app.ribbon_pages['Scena'],'Lampy wł. / wył.',lambda:toggle('lights_enabled'))
    _button(app.ribbon_pages['Scena'],'Logi',app.open_developer_logs)
    _button(app.ribbon_pages['Produkcja'],'Logi',app.open_developer_logs)
    menu=app.nametowidget(app.cget('menu'));tools=tk.Menu(menu,tearoff=False);menu.add_cascade(label='Scena i diagnostyka',menu=tools)
    for label,fn in [('Wyposażenie pomieszczenia',show_scene),('Narożnik…',app.corner_dialog),('Wspólny blat…',app.shared_worktop_dialog),('Otwórz / pokaż logi',app.open_developer_logs)]:tools.add_command(label=label,command=fn)
    app.status.set('ŚPM / Alt+LPM: obrót · Shift+ŚPM: przesuń widok · PPM: szybkie menu · rolka: przybliżenie')
