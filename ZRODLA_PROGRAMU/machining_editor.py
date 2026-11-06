import copy
import tkinter as tk
from tkinter import ttk,messagebox
from core import Camera,render_faces,finite,Project
from features import FACES,hole_array,replace_part
from dock_workspace import DockDocument

class MachiningEditor(DockDocument):
    def __init__(self,app,part):
        super().__init__(app);self.app=app;self.part=copy.deepcopy(part)
        self.editing_cut=None
        self.title('Obróbka części — '+part.name);self.geometry(f'1100x{min(760,self.winfo_screenheight()-100)}+15+15');self.minsize(950,600)
        self.transient(app);self.grab_set()
        ttk.Label(self,text='Kieszenie, rowki, wycięcia i szyki wierceń. Wymiary w lokalnym układzie części, w mm.\nFrezowanie i wiercenia krawędziowe wymagają modułu CAD. To geometria obróbki, bez ścieżek narzędzia.',padding=10).pack(fill='x')
        body=ttk.Frame(self);body.pack(fill='both',expand=True)
        controls=ttk.Notebook(body);controls.pack(side='left',fill='y',padx=8)
        cut=ttk.Frame(controls,padding=10);drill=ttk.Frame(controls,padding=10);history=ttk.Frame(controls,padding=10)
        self.controls=controls
        controls.add(cut,text='Frezowanie');controls.add(drill,text='Wiercenia');controls.add(history,text='Operacje')
        self.cut_vars={};self.drill_vars={}
        def combo(parent,label,values,default):
            ttk.Label(parent,text=label).pack(anchor='w');var=tk.StringVar(value=default)
            ttk.Combobox(parent,textvariable=var,values=values,state='readonly',width=33).pack(fill='x',pady=(0,5));return var
        self.kind=combo(cut,'Rodzaj',['Kieszeń','Wycięcie przelotowe','Rowek'],'Kieszeń')
        self.shape=combo(cut,'Kształt',['Prostokąt','Koło','Otwór podłużny'],'Prostokąt')
        self.cut_face=combo(cut,'Strona',['Góra (+T)','Dół (−T)'],'Góra (+T)')
        def fields(parent,container,rows):
            for key,label,value in rows:
                row=ttk.Frame(parent);row.pack(fill='x',pady=3)
                ttk.Label(row,text=label,width=23).pack(side='left')
                var=tk.StringVar(value=str(value));container[key]=var
                ttk.Entry(row,textvariable=var,width=10).pack(side='right')
        fields(cut,self.cut_vars,[('u','Środek U',part.u/2),('v','Środek V',part.v/2),('length','Długość',min(100,part.u/2)),('width','Szerokość / średnica',min(50,part.v/2)),('depth','Głębokość',min(6,part.t/2)),('angle','Kąt w płaszczyźnie [°]',0)])
        ttk.Label(cut,text='Koło: średnica w polu szerokości.\nPodłużny: długość całkowita, końce półokrągłe.\nRowek: ustaw długość równą formatce, jeśli ma\ndochodzić do obu brzegów.',wraplength=320).pack(anchor='w',pady=8)
        ttk.Button(cut,text='Dodaj obróbkę / podgląd',command=self.add_cut).pack(fill='x')
        self.drill_face=combo(drill,'Powierzchnia',list(FACES),'Góra (+T)')
        fields(drill,self.drill_vars,[('u','Pierwsza współrzędna',min(37,part.u/2)),('v','Druga współrzędna',min(37,part.v/2)),('diameter','Średnica',5),('depth','Głębokość',min(10,part.t/2)),('nu','Liczba w kierunku 1',1),('nv','Liczba w kierunku 2',1),('du','Krok kierunku 1',32),('dv','Krok kierunku 2',32)])
        ttk.Label(drill,text='Na +T/−T: współrzędne U,V.\nNa U=0/max: współrzędne V,T.\nNa V=0/max: współrzędne U,T.\nGłębokość liczona od wskazanej powierzchni.\nRozstaw 32 mm jest edytowalny.',wraplength=320).pack(anchor='w',pady=8)
        ttk.Button(drill,text='Dodaj szyk otworów / podgląd',command=self.add_drills).pack(fill='x')
        self.table=ttk.Treeview(history,columns=('description',),show='headings',height=20,selectmode='extended')
        self.table.heading('description',text='Operacja');self.table.column('description',width=310);self.table.pack(fill='both',expand=True)
        scroll=ttk.Scrollbar(history,orient='vertical',command=self.table.yview);scroll.pack(side='right',fill='y');self.table.configure(yscrollcommand=scroll.set)
        ttk.Button(history,text='Edytuj wybrane frezowanie',command=self.edit_cut).pack(fill='x',pady=4)
        ttk.Button(history,text='Usuń wybrane ręczne operacje',command=self.delete_selected).pack(fill='x',pady=8)
        self.canvas=tk.Canvas(body,bg='#eef1f5',highlightthickness=0);self.canvas.pack(fill='both',expand=True,padx=8)
        self.canvas.bind('<Configure>',lambda e:self.draw())
        footer=ttk.Frame(self,padding=8);footer.pack(side='bottom',fill='x',before=body)
        self.status=tk.StringVar();ttk.Label(footer,textvariable=self.status).pack(side='left')
        ttk.Button(footer,text='Zapisz obróbkę do projektu',command=self.save,style='Accent.TButton').pack(side='right')
        self.refresh()
    def edit_cut(self):
        items=self.table.selection()
        if len(items)!=1 or not items[0].startswith('cut:'):
            messagebox.showinfo('Obróbka','Wybierz jedno frezowanie na liście.',parent=self);return
        self.editing_cut=int(items[0].split(':')[1]);cut=self.part.machining[self.editing_cut]
        for key,var in self.cut_vars.items():var.set(str(cut.get(key,0)))
        self.kind.set({'pocket':'Kieszeń','cutout':'Wycięcie przelotowe','groove':'Rowek'}[cut['type']])
        self.shape.set({'rect':'Prostokąt','circle':'Koło','slot':'Otwór podłużny'}[cut['shape']])
        self.cut_face.set(next(label for label,face in FACES.items() if face==cut['face']));self.controls.select(0)
    def values(self,variables):return {k:finite(v.get().replace(',','.')) for k,v in variables.items()}
    def add_cut(self):
        try:
            values=self.values(self.cut_vars)
            cut=dict(type={'Kieszeń':'pocket','Wycięcie przelotowe':'cutout','Rowek':'groove'}[self.kind.get()],
                     shape={'Prostokąt':'rect','Koło':'circle','Otwór podłużny':'slot'}[self.shape.get()],face=FACES[self.cut_face.get()],**values)
            candidate=copy.deepcopy(self.part)
            if self.editing_cut is None:candidate.machining.append(cut)
            else:candidate.machining[self.editing_cut]=cut
            candidate.validate();self.editing_cut=None
            self.part=candidate;self.refresh();self.draw()
        except ValueError as e:messagebox.showerror('Obróbka',str(e),parent=self)
    def add_drills(self):
        try:
            values=self.values(self.drill_vars)
            if any(values[k]!=int(values[k]) for k in ('nu','nv')):raise ValueError('Liczby otworów muszą być całkowite.')
            candidate=copy.deepcopy(self.part);candidate.operations.extend(hole_array(candidate,face=FACES[self.drill_face.get()],**values));candidate.validate()
            self.part=candidate;self.refresh();self.draw()
        except ValueError as e:messagebox.showerror('Wiercenia',str(e),parent=self)
    def refresh(self):
        self.table.delete(*self.table.get_children())
        for i,cut in enumerate(self.part.machining):self.table.insert('','end',iid='cut:'+str(i),values=(f'{cut["type"]} {cut["shape"]} · {cut["face"]} · {cut["u"]:g},{cut["v"]:g}',))
        for i,op in enumerate(self.part.operations):self.table.insert('','end',iid='drill:'+str(i),values=(f'{"Auto" if op.get("owner_id") else "Otwór"} Ø{op["diameter"]:g} · {op["face"]} · {op["u"]:g},{op["v"]:g}',))
        self.status.set(f'{len(self.part.machining)} frezowań · {len(self.part.operations)} wierceń')
    def delete_selected(self):
        try:
            items=self.table.selection();candidate=copy.deepcopy(self.part)
            if any(item.startswith('drill:') and candidate.operations[int(item.split(':')[1])].get('owner_id') for item in items):
                raise ValueError('Automatyczne otwory okuć edytuj przez parametry montażu w szkicowniku.')
            for prefix,collection in [('cut',candidate.machining),('drill',candidate.operations)]:
                for index in sorted([int(item.split(':')[1]) for item in items if item.startswith(prefix+':')],reverse=True):collection.pop(index)
            candidate.validate();self.editing_cut=None;self.part=candidate;self.refresh();self.draw()
        except ValueError as e:messagebox.showerror('Operacje',str(e),parent=self)
    def draw(self):
        self.canvas.delete('all');w,h=self.canvas.winfo_width(),self.canvas.winfo_height()
        if w<10:return
        project=copy.deepcopy(self.app.project);project.parts=[self.part];camera=Camera();camera.fit(project,w,h)
        for face in render_faces(project,camera,w,h):
            self.canvas.create_polygon([value for p in face['points'] for value in p],fill=face['color'],outline=face['color'] if face.get('mesh') else '#66727f',width=0 if face.get('mesh') else 1)
    def save(self):
        try:
            result=replace_part(self.app.project,self.part)
            self.app.snapshot();self.app.project=result;self.app.refresh();self.app.fit();self.destroy()
        except ValueError as e:messagebox.showerror('Zapis obróbki',str(e),parent=self)
