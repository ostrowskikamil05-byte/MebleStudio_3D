"""Profile editor and curved furniture library using the common project model."""
import tkinter as tk
from copy import deepcopy
from tkinter import ttk, messagebox
from core import polygon_area
from dock_workspace import DockDocument
from shaped_furniture import (SHAPES,THICKNESSES,CURVE_NOTE,TEMPLATES,contour,
                              apply_shape,create_shaped_furniture,profile_area,
                              shape_definition,refresh_decor,remove_decor)


def _commit(app,project):
    app.snapshot();app.project=project;app.refresh();app.fit()


def _guard(app,fn):
    def run():
        try:return fn()
        except (ValueError,OSError,KeyError,StopIteration) as e:
            messagebox.showerror('Sprawdź wymiary',str(e),parent=app)
    return run


def _scroll_form(parent,width=380):
    outer=ttk.Frame(parent);canvas=tk.Canvas(outer,width=width,bg='#f6f8fa',highlightthickness=0)
    canvas.pack(side='left',fill='both',expand=True)
    scrollbar=ttk.Scrollbar(outer,orient='vertical',command=canvas.yview)
    scrollbar.pack(side='right',fill='y');canvas.configure(yscrollcommand=scrollbar.set)
    form=ttk.Frame(canvas);window=canvas.create_window((0,0),window=form,anchor='nw')
    form.bind('<Configure>',lambda e:canvas.configure(scrollregion=canvas.bbox('all')))
    canvas.bind('<Configure>',lambda e:canvas.itemconfigure(window,width=e.width))
    def wheel(e):canvas.yview_scroll(-int(e.delta/120),'units')
    canvas.bind('<MouseWheel>',wheel);form.bind('<MouseWheel>',wheel)
    return outer,form


def shape_dialog(app):
    p=app.selected_part()
    if p is None:raise ValueError('Zaznacz element, którego obrys chcesz zmienić.')
    d=DockDocument(app);d.title('Obłe kształty — '+p.name);d.geometry('980x740+40+40')
    ttk.Label(d,text='Kształt elementu i osobne narożniki',style='Title.TLabel',padding=12).pack(anchor='w')
    footer=ttk.Frame(d,padding=10);footer.pack(side='bottom',fill='x')
    body=ttk.Frame(d);body.pack(fill='both',expand=True,padx=10)
    left=ttk.Frame(body);left.pack(side='left',fill='both',expand=True,padx=(0,10))
    canvas=tk.Canvas(left,bg='#eef3f7',highlightthickness=0);canvas.pack(fill='both',expand=True)
    summary=tk.StringVar();ttk.Label(left,textvariable=summary,wraplength=470,padding=10).pack(fill='x')
    outer,form=_scroll_form(body,400);outer.pack(side='right',fill='y')
    saved=p.profile
    shape=tk.StringVar(value=next((label for label,kind in SHAPES.items() if kind==saved.get('kind')),'Prostokąt — cztery promienie'))
    ttk.Label(form,text='Kształt obrysu',padding=(15,6)).pack(anchor='w')
    combo=ttk.Combobox(form,textvariable=shape,values=list(SHAPES),state='readonly');combo.pack(fill='x',padx=15,pady=5)
    values={}
    for key,label,default in [('u','Szerokość U / średnica [mm]',p.u),('v','Wysokość V [mm]',p.v),('t','Grubość [mm]',p.t),
                              ('r0','R1 — lewy dolny',saved.get('radii',[min(p.u,p.v)*.1]*4)[0]),('r1','R2 — prawy dolny',saved.get('radii',[min(p.u,p.v)*.1]*4)[1]),
                              ('r2','R3 — prawy górny',saved.get('radii',[min(p.u,p.v)*.1]*4)[2]),('r3','R4 — lewy górny',saved.get('radii',[min(p.u,p.v)*.1]*4)[3]),
                              ('rise','Wysokość łuku [mm]',saved.get('arch_rise',min(p.v/3,p.u/2))),('taper','Zwężenie z każdej strony [mm]',saved.get('taper',min(10,p.u/8))),
                              ('segments','Odcinki ćwiartki (4–24)',saved.get('segments',12))]:
        values[key]=app.field(form,label,default)
    edge=tk.BooleanVar(value=p.contour_edge or all(p.edges))
    ttk.Checkbutton(form,text='Obrzeże całego obrysu — obwód i koszt',variable=edge).pack(anchor='w',padx=15,pady=8)
    ttk.Label(form,text='Dla obrysu krzywego formatka do rozkroju pozostaje nominalną obwiednią U/V; nie jest automatycznie pomniejszana o obrzeże.',wraplength=360,padding=15).pack(fill='x')
    flute=tk.BooleanVar(value=p.decor.get('type')=='fluting')
    ttk.Checkbutton(form,text='Ryflowanie parametryczne',variable=flute).pack(anchor='w',padx=15,pady=8)
    for key,label,default in [('flute_width','Szerokość rowka [mm]',p.decor.get('width',8)),
        ('flute_pitch','Podziałka rowków [mm]',p.decor.get('pitch',24)),
        ('flute_depth','Głębokość rowka [mm]',p.decor.get('depth',min(3,p.t*.2))),
        ('flute_margin_u','Margines po bokach [mm]',p.decor.get('margin_u',p.u*.1)),
        ('flute_margin_v','Margines dół / góra [mm]',p.decor.get('margin_v',p.v*.05))]:
        values[key]=app.field(form,label,default)
    flute_face=tk.StringVar(value=p.decor.get('face','bottom'))
    ttk.Label(form,text='Strona rowków (bottom = dolna / przód frontu)',wraplength=360,padding=15).pack(fill='x')
    ttk.Combobox(form,textvariable=flute_face,values=['bottom','top'],state='readonly').pack(fill='x',padx=15,pady=5)
    ttk.Label(form,text='Podpowiedź grubości materiału',padding=(15,6)).pack(anchor='w')
    kind=tk.StringVar(value='Płyta meblowa');stock=tk.StringVar(value=str(p.t))
    stock_kind=ttk.Combobox(form,textvariable=kind,values=list(THICKNESSES),state='readonly');stock_kind.pack(fill='x',padx=15)
    stock_pick=ttk.Combobox(form,textvariable=stock,values=THICKNESSES[kind.get()],state='readonly');stock_pick.pack(fill='x',padx=15,pady=5)
    stock_kind.bind('<<ComboboxSelected>>',lambda e:stock_pick.configure(values=THICKNESSES[kind.get()]))
    stock_pick.bind('<<ComboboxSelected>>',lambda e:values['t'].set(stock.get()))
    ttk.Label(form,text='Wartości są listą typowych grubości do wyboru. Dostępność i rzeczywistą grubość potwierdź u dostawcy.',wraplength=360,padding=15).pack(fill='x')
    ttk.Label(form,text=CURVE_NOTE,wraplength=360,padding=15).pack(fill='x')
    ttk.Label(form,text='R1…R4 odnoszą się do obrysu widocznego na szkicu. Otwory, rowki i okucia zostają zachowane; program sprawdza, czy mieszczą się w nowym kształcie.',wraplength=360,padding=15).pack(fill='x')
    error=tk.StringVar();ttk.Label(footer,textvariable=error,foreground='#a33232',wraplength=940).pack(fill='x')

    def parameters():
        parsed={k:app.number(v) for k,v in values.items()}
        opts=dict(radii=tuple(parsed[f'r{i}'] for i in range(4)),segments=parsed['segments'],
                  arch_rise=parsed['rise'],taper=parsed['taper'])
        return SHAPES[shape.get()],parsed,opts

    pending=[None]
    def preview():
        pending[0]=None;canvas.delete('all')
        try:
            which,val,opts=parameters()
            pts=contour(which,val['u'],val['v'],**opts) or [(0,0),(val['u'],0),(val['u'],val['v']),(0,val['v'])]
            w,h=max(100,canvas.winfo_width()),max(100,canvas.winfo_height())
            scale=min((w-70)/val['u'],(h-90)/val['v']);cx,cy=w/2,(h-35)/2
            flat=[]
            for x,y in pts:flat.extend((cx+(x-val['u']/2)*scale,cy-(y-val['v']/2)*scale))
            canvas.create_polygon(flat,fill=app.project.mat(p.material).color,outline='#244c61',width=2)
            canvas.create_text(w/2,h-24,text=f"U = {val['u']:g} mm",font=('Segoe UI',11,'bold'),fill='#244c61')
            canvas.create_text(12,15,anchor='nw',text=f"V = {val['v']:g} mm\nT = {val['t']:g} mm",fill='#244c61')
            for x,y,label in ((0,0,'R1'),(val['u'],0,'R2'),(val['u'],val['v'],'R3'),(0,val['v'],'R4')):
                canvas.create_text(cx+(x-val['u']/2)*scale,cy-(y-val['v']/2)*scale,
                    text=label,fill='#176a78',anchor='sw' if y else 'nw')
            exact=profile_area(shape_definition(which,val['u'],val['v'],**opts),val['u'],val['v'])
            summary.set(f"{shape.get()} · pole {exact/1e6:.4f} m² · {len(pts)} punktów podglądu.\nParametry obrysu są zapisane; STEP ma dokładne krzywe.")
            error.set('')
        except (ValueError,KeyError,ZeroDivisionError,StopIteration) as e:error.set(str(e));summary.set('Popraw parametry, aby zobaczyć kształt.')

    def schedule(*_):
        if SHAPES[shape.get()]=='circle' and values['v'].get()!=values['u'].get():
            values['v'].set(values['u'].get())
        if pending[0]:d.after_cancel(pending[0])
        pending[0]=d.after(100,preview)

    for var in [shape,*values.values()]:var.trace_add('write',schedule)
    canvas.bind('<Configure>',schedule)
    def apply():
        d.ensure_current_project(raise_error=True)
        which,val,opts=parameters()
        source=deepcopy(app.project)
        remove_decor(next(q for q in source.parts if q.id==p.id))
        result=apply_shape(source,p.id,which,val['u'],val['v'],val['t'],**opts)
        q=next(q for q in result.parts if q.id==p.id)
        q.contour_edge=bool(edge.get()) if q.contour else False
        if not q.contour:q.edges=[bool(edge.get())]*4
        if flute.get():
            q.decor=dict(type='fluting',width=val['flute_width'],pitch=val['flute_pitch'],depth=val['flute_depth'],
                margin_u=val['flute_margin_u'],margin_v=val['flute_margin_v'],face=flute_face.get())
            refresh_decor(q)
        else:remove_decor(q)
        result.validate()
        _commit(app,result);app.selected=p.id;app.refresh();d.destroy()
    buttons=ttk.Frame(footer);buttons.pack(fill='x',pady=(8,0))
    save_button=ttk.Button(buttons,text='Zapisz obrys w elemencie',command=_guard(app,apply),style='Accent.TButton');save_button.pack(side='left',fill='x',expand=True)
    ttk.Button(buttons,text='Zamknij',command=d.request_close).pack(side='right',padx=(10,0))
    d.shape_vars=values;d.shape_choice=shape;d.shape_apply=apply;d.shape_canvas=canvas
    d.shape_submit_button=save_button
    d.shape_edge=edge;d.shape_flute=flute;d.shape_flute_face=flute_face
    schedule();return d


def style_library(app):
    d=DockDocument(app);d.title('Meble obłe, drewniane i style');d.geometry('1020x750+40+40')
    ttk.Label(d,text='Biblioteka mebli obłych',style='Title.TLabel',padding=12).pack(anchor='w')
    footer=ttk.Frame(d,padding=10);footer.pack(side='bottom',fill='x')
    content=ttk.Frame(d);content.pack(fill='both',expand=True,padx=10)
    left=ttk.Frame(content);left.pack(side='left',fill='both',expand=True,padx=(0,10))
    filterbox=ttk.Frame(left);filterbox.pack(fill='x',pady=(0,8))
    style=tk.StringVar(value='Wszystkie');search=tk.StringVar()
    ttk.Combobox(filterbox,textvariable=style,values=['Wszystkie']+list(dict.fromkeys(t[6] for t in TEMPLATES)),state='readonly',width=18).pack(side='left')
    ttk.Entry(filterbox,textvariable=search).pack(side='left',fill='x',expand=True,padx=(8,0))
    ttk.Label(left,text='Filtruj stylem lub wpisz fragment nazwy.',padding=(0,3)).pack(anchor='w')
    tablebox=ttk.Frame(left);tablebox.pack(fill='both',expand=True)
    table=ttk.Treeview(tablebox,columns=('name','style'),show='headings',height=12)
    table.heading('name',text='Mebel');table.heading('style',text='Styl');table.column('name',width=300);table.column('style',width=120)
    table.pack(side='left',fill='both',expand=True);sb=ttk.Scrollbar(tablebox,orient='vertical',command=table.yview);sb.pack(side='right',fill='y');table.configure(yscrollcommand=sb.set)
    note=tk.StringVar();ttk.Label(left,textvariable=note,wraplength=560,padding=(0,10)).pack(fill='x')
    ttk.Label(left,text=CURVE_NOTE+' Wzorce krzeseł, nóg i ławek wymagają własnego doboru połączeń oraz oceny konstrukcji.',wraplength=560,padding=(0,8)).pack(fill='x')
    outer,form=_scroll_form(content,360);outer.pack(side='right',fill='y')
    values={k:app.field(form,label,value) for k,label,value in
        [('name','Nazwa mebla','Nowy mebel'),('w','Szerokość X [mm]',1600),('h','Wysokość Z [mm]',760),('d','Głębokość Y [mm]',850),
         ('t','Grubość blatu / materiału [mm]',28),('segments','Odcinki ćwiartki (4–24)',12),('x','Pozycja X [mm]',0),('y','Pozycja Y [mm]',0),('z','Pozycja Z [mm]',0)]}
    ttk.Label(form,text='Materiał konstrukcji',padding=(15,5)).pack(anchor='w')
    mat=ttk.Combobox(form,values=[m.name for m in app.project.materials],state='readonly');mat.pack(fill='x',padx=15)
    mat.current(next((i for i,m in enumerate(app.project.materials) if m.kind=='drewno'),0))
    ttk.Label(form,text='Materiał frontów',padding=(15,5)).pack(anchor='w')
    fronts=ttk.Combobox(form,values=[m.name for m in app.project.materials],state='readonly');fronts.pack(fill='x',padx=15);fronts.current(mat.current())
    ttk.Label(form,text='Wymiary są parametrami wzorca. Po utworzeniu każdy element ma własny szkic, materiał i wymiary w projekcie.',wraplength=325,padding=15).pack(fill='x')
    def choose(*_):
        if not table.selection():return
        item=TEMPLATES[int(table.selection()[0])]
        for key,value in [('name',item[1]),('w',item[2]),('h',item[3]),('d',item[4])]:values[key].set(str(value))
        note.set(f'{item[0]} · {item[6]}\n{item[7]}')
    def refresh(*_):
        table.delete(*table.get_children())
        for i,item in enumerate(TEMPLATES):
            if (style.get()=='Wszystkie' or style.get()==item[6]) and search.get().casefold() in (' '.join((item[0],item[1],item[6]))).casefold():
                table.insert('','end',iid=str(i),values=(item[1],item[6]))
        if table.get_children():table.selection_set(table.get_children()[0]);choose()
        else:note.set('Brak wzorców pasujących do filtra.')
    table.bind('<<TreeviewSelect>>',choose);style.trace_add('write',refresh);search.trace_add('write',refresh)
    def create():
        d.ensure_current_project(raise_error=True)
        if not table.selection():raise ValueError('Wybierz wzorzec mebla.')
        result,group=create_shaped_furniture(app.project,int(table.selection()[0]),
            *[app.number(values[k]) for k in ('w','h','d','t')],
            origin=tuple(app.number(values[k]) for k in ('x','y','z')),
            group=values['name'].get(),material=app.project.materials[mat.current()].id,
            front_material=app.project.materials[fronts.current()].id,segments=app.number(values['segments']))
        _commit(app,result);app.selected=next(p.id for p in result.parts if p.group==group);app.refresh();d.destroy();app.workspace.select(app.design_page)
    create_button=ttk.Button(footer,text='Utwórz mebel o wybranych wymiarach',command=_guard(app,create),style='Accent.TButton');create_button.pack(side='left',fill='x',expand=True)
    ttk.Button(footer,text='Zamknij',command=d.request_close).pack(side='right',padx=(10,0))
    d.style_vars=values;d.style_table=table;d.style_add=create
    d.style_create_button=create_button
    refresh();return d


def install_menu(app):
    bar=app.nametowidget(app.cget('menu'));menu=tk.Menu(bar,tearoff=False)
    bar.add_cascade(label='Kształty i style',menu=menu)
    menu.add_command(label='Obrys: koło, elipsa, łuk, narożniki…',command=_guard(app,lambda:shape_dialog(app)))
    menu.add_command(label='Biblioteka mebli obłych i drewnianych…',command=_guard(app,lambda:style_library(app)))
    app.shape_dialog=lambda:_guard(app,lambda:shape_dialog(app))()
    app.style_library=lambda:_guard(app,lambda:style_library(app))()
