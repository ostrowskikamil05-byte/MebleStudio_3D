"""Furniture, fittings, palette and motion dialogs."""
import copy,tkinter as tk,webbrowser
from tkinter import ttk,messagebox
from core import Project,finite
from fittings import RECIPES,HANDLES,FINISHES,recipe,system32,add_handle,cabinet_connectors
from dimension_model import regenerate_hinges,fit_part
from mechanisms import MODES,set_motion,effective_motion
from furniture_library import TEMPLATES,create_furniture
from palette import PALETTE,install_palette
from dock_workspace import DockDocument

def commit(app,project):app.snapshot();app.project=project;app.refresh();app.fit()
def selected(app):
    p=app.selected_part()
    if not p:raise ValueError('Wybierz element w drzewie projektu.')
    return p

def safe(app,fn):
    def run():
        try:fn()
        except (ValueError,OSError,KeyError,StopIteration) as e:messagebox.showerror('Sprawdź ustawienia',str(e),parent=app)
    return run

def library_dialog(app):
    d=app.dialog('Biblioteka mebli — wymiary i wyposażenie','900x760');all_entries=list(range(len(TEMPLATES)))
    top=ttk.Frame(d,padding=12);top.pack(fill='x');cat=tk.StringVar(value='Wszystkie');search=tk.StringVar()
    ttk.Combobox(top,textvariable=cat,values=['Wszystkie']+list(dict.fromkeys(t[0] for t in TEMPLATES)),state='readonly',width=25).pack(side='left')
    ttk.Entry(top,textvariable=search,width=35).pack(side='left',padx=12);ttk.Label(top,text='Szukaj nazwy mebla').pack(side='left')
    content=ttk.Frame(d);content.pack(fill='both',expand=True,padx=12);table=ttk.Treeview(content,columns=('category','name'),show='headings',height=16);table.heading('category',text='Pomieszczenie');table.heading('name',text='Mebel');table.column('category',width=170);table.column('name',width=265);table.pack(side='left',fill='both',expand=True)
    sb=ttk.Scrollbar(content,orient='vertical',command=table.yview);sb.pack(side='left',fill='y');table.configure(yscrollcommand=sb.set)
    form=ttk.Frame(content,padding=10);form.pack(side='right',fill='y');vars={}
    for key,label,value in [('name','Nazwa / grupa','Nowy mebel'),('w','Szerokość X [mm]',600),('h','Wysokość Z [mm]',720),('d','Głębokość Y [mm]',560),('t','Grubość materiału [mm]',18),('x','Pozycja X [mm]',0),('y','Pozycja Y [mm]',0),('z','Pozycja Z [mm]',0)]:vars[key]=app.field(form,label,value)
    ttk.Label(form,text='Materiał konstrukcji').pack(anchor='w');mat=ttk.Combobox(form,values=[m.name for m in app.project.materials],state='readonly');mat.pack(fill='x');mat.current(0)
    ttk.Label(form,text='Materiał frontów').pack(anchor='w');front=ttk.Combobox(form,values=[m.name for m in app.project.materials],state='readonly');front.pack(fill='x');front.current(min(1,len(app.project.materials)-1))
    note=tk.StringVar();ttk.Label(d,textvariable=note,wraplength=840,padding=12).pack(fill='x')
    def refresh(*_):
        table.delete(*table.get_children())
        for i,t in enumerate(TEMPLATES):
            if (cat.get()=='Wszystkie' or cat.get()==t[0]) and search.get().casefold() in t[1].casefold():table.insert('','end',iid=str(i),values=t[:2])
        if table.get_children():table.selection_set(table.get_children()[0]);choose()
    def choose(*_):
        if not table.selection():return
        index=int(table.selection()[0]);category,name,w,h,depth,kind,shelves,fronts=TEMPLATES[index]
        for key,value in [('name',name),('w',w),('h',h),('d',depth)]:vars[key].set(str(value))
        note.set(f'{category} · {shelves} półek · {fronts} frontów. Zawiasy i uchwyty generują się dla drzwi. Cargo i klapy mają modele koncepcyjne mechanizmów. Wymiary i materiały są edytowalne.')
    table.bind('<<TreeviewSelect>>',choose);cat.trace_add('write',refresh);search.trace_add('write',refresh)
    def add():
        d.ensure_current_project(raise_error=True)
        if not table.selection():raise ValueError('Wybierz mebel.')
        values={k:v.get() for k,v in vars.items()};result,group=create_furniture(app.project,int(table.selection()[0]),*[float(values[k].replace(',','.')) for k in ('w','h','d','t')],origin=tuple(float(values[k].replace(',','.')) for k in ('x','y','z')),group=values['name'],material=app.project.materials[mat.current()].id,front_material=app.project.materials[front.current()].id)
        commit(app,result);app.selected=next(p.id for p in app.project.parts if p.group==group);app.refresh();d.destroy();app.workspace.select(app.design_page)
    ttk.Button(d,text='Utwórz mebel z wyposażeniem',command=safe(app,add),style='Accent.TButton').pack(fill='x',padx=12,pady=12);refresh();return d

def palette_dialog(app):
    p=selected(app);d=app.dialog('Paleta kolorów i dekorów','850x680');chosen=tk.StringVar(value=PALETTE[0][0]);scope=tk.StringVar(value='Wybrany element');frame=ttk.Frame(d,padding=15);frame.pack(fill='both',expand=True)
    for i,(name,color) in enumerate(PALETTE):
        button=tk.Button(frame,text=name,bg=color,fg='#fff' if sum(int(color[k:k+2],16) for k in (1,3,5))<330 else '#18212b',width=16,height=2,command=lambda n=name:chosen.set(n));button.grid(row=i//6,column=i%6,padx=3,pady=3,sticky='nsew')
    for col in range(6):frame.columnconfigure(col,weight=1)
    ttk.Label(d,textvariable=chosen,padding=10,font=('Segoe UI',12,'bold')).pack();ttk.Combobox(d,textvariable=scope,values=['Wybrany element','Cały mebel','Wszystkie fronty mebla'],state='readonly').pack(fill='x',padx=15)
    def apply():
        d.ensure_current_project(raise_error=True)
        result=copy.deepcopy(app.project);install_palette(result);material=next(m for m in result.materials if m.name=='Dekor — '+chosen.get())
        from assembly import inferred_role
        for q in result.parts:
            if q.id==p.id if scope.get()=='Wybrany element' else q.group==p.group and (scope.get()=='Cały mebel' or inferred_role(q) in ('door','drawer')):q.material=material.id
        result.validate();commit(app,result)
    ttk.Label(d,text='Kolory ekranowe są przybliżone. Ceny nowych dekorów wpisz w Materiały i ceny.',padding=8).pack()
    ttk.Button(d,text='Zastosuj kolor / dekor',command=safe(app,apply),style='Accent.TButton').pack(fill='x',padx=15,pady=12);return d

def motion_dialog(app):
    p=selected(app);motion=effective_motion(p);d=app.dialog('Mechanizm i otwieranie — '+p.name,'590x420');mode=tk.StringVar(value=next(label for label,value in MODES.items() if value==motion['mode']))
    ttk.Label(d,text='Wybierz ruch frontu lub pokrywy',padding=15).pack(anchor='w');ttk.Combobox(d,textvariable=mode,values=list(MODES),state='readonly').pack(fill='x',padx=15)
    angle=app.field(d,'Maksymalny kąt [°]',motion.get('angle',100));travel=app.field(d,'Maksymalny wysuw [mm]',motion.get('travel',400))
    ttk.Label(d,text='Suwak otwierania jest pod widokami 3D. Zaznacz front i porusz suwakiem lub uruchom animację. Klapy, drzwi przesuwne i cargo pokazują ruch koncepcyjny; nie dobierają nośności ani otworów producenta.',wraplength=540,padding=15).pack(fill='x')
    def apply():
        d.ensure_current_project(raise_error=True)
        commit(app,set_motion(app.project,p.id,MODES[mode.get()],app.number(angle),app.number(travel)));d.destroy()
    ttk.Button(d,text='Zapisz mechanizm',command=safe(app,apply),style='Accent.TButton').pack(fill='x',padx=15,pady=12);return d

class FittingsDialog(DockDocument):
    def __init__(self,app):
        super().__init__(app);self.app=app;self.part=selected(app);self.title('Okucia i standardy otworów — '+self.part.name);self.geometry(f'1000x{min(800,self.winfo_screenheight()-110)}+20+20');self.minsize(900,600);self.transient(app);self.grab_set()
        ttk.Button(self,text='Zamknij',command=self.destroy).pack(side='bottom',fill='x',padx=10,pady=8)
        tabs=ttk.Notebook(self);tabs.pack(fill='both',expand=True,padx=10,pady=10)
        self.pages={}
        for name in ('Katalog otworów','Zawiasy','System 32','Uchwyty','Połączenia korpusu','Prowadnice i cargo'):
            page=ttk.Frame(tabs,padding=14);tabs.add(page,text=name);self.pages[name]=page
        page=self.pages['Katalog otworów']
        footer=ttk.Frame(page);footer.pack(side='bottom',fill='x')
        ttk.Button(footer,text='Dodaj otwory według wzoru',command=safe(app,self.drill)).pack(side='left',fill='x',expand=True,pady=8)
        ttk.Button(footer,text='Dokumentacja źródłowa',command=self.source).pack(side='left',fill='x',expand=True,padx=8)
        scrollbody=ttk.Frame(page);scrollbody.pack(fill='both',expand=True)
        formcanvas=tk.Canvas(scrollbody,bg='#f6f8fa',highlightthickness=0);formcanvas.pack(side='left',fill='both',expand=True)
        formsb=ttk.Scrollbar(scrollbody,command=formcanvas.yview);formsb.pack(side='right',fill='y');formcanvas.configure(yscrollcommand=formsb.set)
        page=ttk.Frame(formcanvas);formwin=formcanvas.create_window((0,0),window=page,anchor='nw')
        page.bind('<Configure>',lambda e:formcanvas.configure(scrollregion=formcanvas.bbox('all')));formcanvas.bind('<Configure>',lambda e:formcanvas.itemconfigure(formwin,width=e.width))
        self.choice=tk.StringVar(value=RECIPES[0]['name']);ttk.Combobox(page,textvariable=self.choice,values=[r['name'] for r in RECIPES],state='readonly',width=80).pack(fill='x')
        self.info=tk.StringVar();ttk.Label(page,textvariable=self.info,wraplength=900,padding=(0,8)).pack(fill='x');self.choice.trace_add('write',self.describe);self.describe()
        self.fields={k:app.field(page,label,value) for k,label,value in [('u','Pierwsza współrzędna',37),('v','Druga współrzędna',100),('diameter','Średnica [mm]',5),('depth','Głębokość [mm]',10),('count','Liczba w rzędzie',1),('pitch','Rozstaw [mm]',32)]}
        from features import FACES
        self.face=tk.StringVar(value='Góra (+T)');ttk.Combobox(page,textvariable=self.face,values=list(FACES),state='readonly').pack(fill='x');ttk.Label(page,text='Na ±T: U,V. Na U=0/max: V,T. Na V=0/max: U,T. Wzory puszek: środek puszki, tylko ±T.',padding=8).pack(fill='x')
        page=self.pages['Zawiasy'];self.hinge=tk.StringVar(value='45 × 9,5');ttk.Combobox(page,textvariable=self.hinge,values=['45 × 9,5','52 × 9','Hettich Sensys TH52 (52 × 5,5)'],state='readonly').pack(fill='x');self.hinge_count=app.field(page,'Liczba zawiasów',2)
        self.side=tk.StringVar(value='Automatycznie');ttk.Combobox(page,textvariable=self.side,values=['Automatycznie','Lewy','Prawy'],state='readonly').pack(fill='x',pady=8)
        ttk.Label(page,text='Przelicza front, puszki, otwory mocowania i prowadniki na boku. Front musi być przy wybranym boku. Możesz użyć „Dopasuj element do korpusu”, jeśli szkic nie pasuje. Sprawdź konkretny wariant zawiasu i grubość frontu.',wraplength=900,padding=15).pack(fill='x')
        ttk.Button(page,text='Dodaj / napraw zawiasy na drzwiczkach',command=safe(app,self.hinges),style='Accent.TButton').pack(fill='x',pady=8)
        page=self.pages['System 32'];self.sys_fields={k:app.field(page,label,value) for k,label,value in [('front','Linia od przodu',37),('back','Linia od tyłu',37),('start','Początek od dołu',64),('end','Margines na końcu',64),('diameter','Średnica',5),('depth','Głębokość',10)]};self.both=tk.BooleanVar(value=True);ttk.Checkbutton(page,text='Dwa rzędy — przód i tył',variable=self.both).pack(anchor='w');self.sys_face=tk.StringVar(value='top');ttk.Combobox(page,textvariable=self.sys_face,values=['top','bottom'],state='readonly').pack(fill='x',pady=8)
        ttk.Label(page,text='U = głębokość boku, V = wysokość. Krok stały 32 mm. Dla boku lewego powierzchnia wewnętrzna zwykle top, dla prawego bottom.',wraplength=900,padding=10).pack(fill='x');ttk.Button(page,text='Generuj / zastąp rzędy System 32',command=safe(app,self.system)).pack(fill='x')
        page=self.pages['Uchwyty'];self.handle=tk.StringVar(value='Uchwyt 128');ttk.Combobox(page,textvariable=self.handle,values=[x[0] for x in HANDLES],state='readonly').pack(fill='x');self.handle_u=app.field(page,'Środek U',self.part.u-40);self.handle_v=app.field(page,'Środek V',self.part.v/2);self.vertical=tk.BooleanVar(value=True);ttk.Checkbutton(page,text='Pionowo (odznacz = poziomo)',variable=self.vertical).pack(anchor='w');self.finish=tk.StringVar(value='Czarny mat');ttk.Combobox(page,textvariable=self.finish,values=list(FINISHES),state='readonly').pack(fill='x',pady=8)
        ttk.Label(page,text='Uchwyt / gałka ma model 3D i przeloty Ø4,5 pod śrubę M4. Rozstaw oraz wygląd są wzorcem ogólnym.',padding=12).pack(fill='x');ttk.Button(page,text='Dodaj / zmień uchwyt i otwory',command=safe(app,self.handles)).pack(fill='x')
        page=self.pages['Połączenia korpusu'];self.connector=tk.StringVar(value='confirmat7');ttk.Combobox(page,textvariable=self.connector,values=['confirmat7','confirmat5','dowel6','dowel8','dowel10'],state='readonly').pack(fill='x');self.connector_front=app.field(page,'Od przodu [mm]',50);self.connector_back=app.field(page,'Od tyłu [mm]',50)
        ttk.Label(page,text='Tworzy pary otworów w bokach oraz krawędziach dna i wieńca wybranego korpusu. Konfirmat 7: przelot Ø7, pilot Ø5, pogłębienie Ø10 × 2. Wartości robocze wymagają doboru do łącznika.',wraplength=900,padding=15).pack(fill='x');ttk.Button(page,text='Połącz korpus i wygeneruj wiercenia',command=safe(app,self.connectors)).pack(fill='x')
        page=self.pages['Prowadnice i cargo'];ttk.Label(page,text='Katalog prowadnic producentów: dobór długości, grubości skrzynki, luzów i cofnięcia dna. Kalkulator odróżnia wymiary producenta od wartości warsztatowych. Otwory zaczepów i mocowania konkretnego artykułu wymagają jego dokumentacji.\n\nCargo i klapy: schemat ruchu i wizualizacja wyposażenia; dostawę okuć dobierz z katalogu producenta.\n\nWybierz front szuflady, aby zmienić parametry skrzynki. Wybierz front korpusu, aby dodać cargo.',wraplength=890,padding=12).pack(fill='x');ttk.Button(page,text='Katalog prowadnic producentów — przelicz szufladę',command=safe(app,self.runners)).pack(fill='x',pady=8);ttk.Button(page,text='Dodaj cargo i mechanizm wysuwu',command=safe(app,self.cargo)).pack(fill='x',pady=8);ttk.Button(page,text='Ustaw mechanizm klapy / przesuwu…',command=safe(app,lambda:motion_dialog(app))).pack(fill='x',pady=8)
        self.scroll_pages()
    def scroll_pages(self):
        """Keep actions visible while tall fitting forms scroll in a tab."""
        for name,page in self.pages.items():
            if name=='Katalog otworów':continue
            children=list(page.winfo_children());options={w:w.pack_info() for w in children}
            for widget in children:widget.pack_forget()
            footer=ttk.Frame(page);footer.pack(side='bottom',fill='x')
            body=ttk.Frame(page);body.pack(fill='both',expand=True)
            viewport=tk.Canvas(body,highlightthickness=0,bg='#f6f8fa');viewport.pack(side='left',fill='both',expand=True)
            scrollbar=ttk.Scrollbar(body,command=viewport.yview);scrollbar.pack(side='right',fill='y');viewport.configure(yscrollcommand=scrollbar.set)
            form=ttk.Frame(viewport);window=viewport.create_window((0,0),window=form,anchor='nw')
            form.bind('<Configure>',lambda event,c=viewport:c.configure(scrollregion=c.bbox('all')))
            viewport.bind('<Configure>',lambda event,c=viewport,win=window:c.itemconfigure(win,width=event.width))
            for widget in children:
                if widget.winfo_class()=='TButton':widget.pack(in_=footer,fill='x',pady=4)
                else:
                    info=dict(options[widget]);info.pop('in',None);widget.pack(in_=form,**info)
                    widget.bind('<MouseWheel>',lambda event,c=viewport:c.yview_scroll(-1 if event.delta>0 else 1,'units'),add='+')
                widget.lift()
    def describe(self,*_):
        r=next(r for r in RECIPES if r['name']==self.choice.get());self.info.set(r['verified']+'\n'+(r['source'] or 'Własny wzorzec roboczy — wartości edytowalne.'))
        if hasattr(self,'fields'):
            for k,v in [('diameter',r['diameter']),('depth',r['depth'] or self.part.t),('pitch',r.get('pitch',32))]:self.fields[k].set(str(v))
    def source(self):
        r=next(r for r in RECIPES if r['name']==self.choice.get())
        if r['source']:webbrowser.open(r['source'])
    def update_project(self,result):
        self.ensure_current_project(raise_error=True)
        commit(self.app,result);self.part=next(p for p in result.parts if p.id==self.part.id);self.mark_clean()
    def drill(self):
        from features import FACES,hole_array
        r=next(r for r in RECIPES if r['name']==self.choice.get());values={k:self.app.number(v) for k,v in self.fields.items()};count=values.pop('count');pitch=values.pop('pitch')
        if count!=int(count):raise ValueError('Liczba otworów musi być całkowita.')
        result=copy.deepcopy(self.app.project);p=next(p for p in result.parts if p.id==self.part.id);face=FACES[self.face.get()]
        operations=hole_array(p,**values,nu=1,nv=count,dv=pitch,face=face)
        if r['id'].startswith('hinge'):
            if face not in ('top','bottom'):raise ValueError('Wzór puszki wymaga powierzchni ±T.')
            for op in list(operations):
                for v in (op['v']-r['pitch']/2,op['v']+r['pitch']/2):operations+=hole_array(p,op['u']-r['offset'],v,r['pilot'],r['pilot_depth'],face=face)
        for op in operations:op.update(recipe_id=r['id'],source=r['source'],purpose=r['name'])
        p.operations.extend(operations);result.validate();self.update_project(result)
    def hinges(self):
        count=self.app.number(self.hinge_count)
        side=None if self.side.get()=='Automatycznie' else 'left' if self.side.get()=='Lewy' else 'right'
        self.update_project(regenerate_hinges(self.app.project,self.part.id,{'45 × 9,5':'hinge45','52 × 9':'hinge52','Hettich Sensys TH52 (52 × 5,5)':'hinge_sensys52'}[self.hinge.get()],count,side))
    def system(self):self.update_project(system32(self.app.project,self.part.id,**{k:self.app.number(v) for k,v in self.sys_fields.items()},both=self.both.get(),face=self.sys_face.get()))
    def handles(self):self.update_project(add_handle(self.app.project,self.part.id,dict(HANDLES)[self.handle.get()],self.app.number(self.handle_u),self.app.number(self.handle_v),self.vertical.get(),FINISHES[self.finish.get()]))
    def connectors(self):self.update_project(cabinet_connectors(self.app.project,self.part.group,self.connector.get(),self.app.number(self.connector_front),self.app.number(self.connector_back)))
    def runners(self):
        from hardware_ui import RunnerDialog
        self.destroy();RunnerDialog(self.app)
    def cargo(self):self.update_project(set_motion(self.app.project,self.part.id,'cargo',100,400))

def install(app):
    bar=app.nametowidget(app.cget('menu'));menu=tk.Menu(bar,tearoff=False);bar.add_cascade(label='Okucia i wyposażenie',menu=menu)
    for text,fn in [('Katalog otworów, zawiasy, uchwyty…',lambda:FittingsDialog(app)),('Paleta kolorów i dekorów…',lambda:palette_dialog(app)),('Mechanizm otwierania…',lambda:motion_dialog(app)),('Dopasuj element do korpusu',lambda:commit(app,fit_part(app.project,selected(app).id))),('Biblioteka mebli do pomieszczeń…',lambda:library_dialog(app))]:menu.add_command(label=text,command=safe(app,fn))
    app.fittings_dialog=lambda:safe(app,lambda:FittingsDialog(app))()
    app.palette_dialog=lambda:safe(app,lambda:palette_dialog(app))()
    app.motion_dialog=lambda:safe(app,lambda:motion_dialog(app))()
    app.library_dialog=lambda:library_dialog(app)
    app.fit_selected=lambda:safe(app,lambda:commit(app,fit_part(app.project,selected(app).id)))()
