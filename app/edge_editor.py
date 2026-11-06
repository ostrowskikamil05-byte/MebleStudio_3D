import copy
import tkinter as tk
from tkinter import ttk,messagebox
from core import Camera,render_faces,finite
from cad_geometry import edge_specs,edge_path
from assembly import rebuild_dependents
from dock_workspace import DockDocument

class EdgeEditor(DockDocument):
    def __init__(self,app,part,edge=None,mode='fillet'):
        super().__init__(app);self.app=app;self.part=part
        self.attribute='chamfers' if mode=='chamfer' else 'fillets';self.parameter='distance' if mode=='chamfer' else 'radius'
        self.values={item['edge']:item[self.parameter] for item in getattr(part,self.attribute)}
        self.specs=edge_specs(part.points(),part.t,part.profile,(part.u,part.v))
        for spec in self.specs:
            if spec[0] in self.values:
                for key in getattr(spec[2],'chain',(spec[0],)):self.values[key]=self.values[spec[0]]
        self.title(('Fazy 45° — ' if mode=='chamfer' else 'Zaokrąglenia krawędzi — ')+part.name);self.geometry('1050x650')
        self.transient(app);self.grab_set()
        ttk.Label(self,text='Każdy ostry kant ma własną wartość w mm. 0 usuwa obróbkę.\nŁuki i odcinki połączone stycznie są obrabiane wspólnie — zaznaczenie jednego ustawia cały ciąg.\nFaza i zaokrąglenie nie mogą dotyczyć tej samej krawędzi lub ciągu.',padding=12).pack(fill='x')
        footer=ttk.Frame(self,padding=8);footer.pack(side='bottom',fill='x')
        ttk.Button(footer,text='Zapisz obróbkę krawędzi',command=self.save,style='Accent.TButton').pack(side='right')
        self.summary=tk.StringVar();ttk.Label(footer,textvariable=self.summary).pack(side='left')
        body=ttk.Frame(self);body.pack(fill='both',expand=True)
        side=ttk.Frame(body,padding=10);side.pack(side='left',fill='y')
        controls=ttk.Frame(side);controls.pack(side='bottom',fill='x')
        self.radius=tk.StringVar(value='3')
        ttk.Label(controls,text='Odległość fazy / promień [mm]').pack(anchor='w',pady=(8,0))
        ttk.Entry(controls,textvariable=self.radius).pack(fill='x')
        ttk.Button(controls,text='Ustaw wartość / podgląd',command=self.set_radius).pack(fill='x',pady=6)
        ttk.Button(controls,text='Usuń wszystkie z tej operacji',command=self.reset).pack(fill='x')
        self.table=ttk.Treeview(side,columns=('edge','radius'),show='headings',height=15,selectmode='extended')
        self.table.heading('edge',text='Wybrana krawędź');self.table.heading('radius',text='C mm' if mode=='chamfer' else 'R mm')
        self.table.column('edge',width=240);self.table.column('radius',width=65)
        self.table.pack(fill='both',expand=True)
        scroll=ttk.Scrollbar(side,orient='vertical',command=self.table.yview);scroll.pack(side='right',fill='y');self.table.configure(yscrollcommand=scroll.set)
        self.canvas=tk.Canvas(body,bg='#eef1f5',highlightthickness=0);self.canvas.pack(fill='both',expand=True)
        self.canvas.bind('<Configure>',lambda e:self.draw())
        self.table.bind('<<TreeviewSelect>>',self.select)
        self.candidate=copy.deepcopy(part);self.refresh()
        if edge:self.table.selection_set(edge);self.table.see(edge)
        self.after(80,self.draw)
    def refresh(self):
        selection=self.table.selection();self.table.delete(*self.table.get_children())
        for key,label,*_ in self.specs:self.table.insert('','end',iid=key,values=(label,f'{self.values.get(key,0):g}'))
        for key in selection:
            if self.table.exists(key):self.table.selection_add(key)
        self.summary.set(f'{len(self.values)} kantów z parametrem')
    def select(self,event=None):
        keys=self.table.selection()
        if len(keys)==1:self.radius.set(str(self.values.get(keys[0],0)))
        self.draw()
    def set_radius(self):
        try:
            radius=finite(self.radius.get().replace(',','.'),0,10000)
            keys=self.table.selection()
            if not keys:raise ValueError('Wybierz jeden lub kilka kantów (Ctrl / Shift).')
            selected=set(keys)
            for spec in self.specs:
                if spec[0] in keys:selected.update(getattr(spec[2],'chain',(spec[0],)))
            keys=sorted(selected)
            values=self.values.copy()
            for key in keys:
                if radius:values[key]=radius
                else:values.pop(key,None)
            candidate=copy.deepcopy(self.part);setattr(candidate,self.attribute,[{'edge':key,self.parameter:value} for key,value in values.items()])
            candidate.validate();self.values=values;self.candidate=candidate;self.refresh();self.draw()
        except ValueError as e:messagebox.showerror('Zaokrąglenia',str(e),parent=self)
    def reset(self):
        self.values={};self.candidate=copy.deepcopy(self.part);setattr(self.candidate,self.attribute,[]);self.refresh();self.draw()
    def draw(self):
        from core import Project,world
        self.canvas.delete('all');w,h=self.canvas.winfo_width(),self.canvas.winfo_height()
        if w<10:return
        project=copy.deepcopy(self.app.project);project.parts=[self.candidate]
        camera=Camera();camera.fit(project,w,h)
        for face in render_faces(project,camera,w,h):
            self.canvas.create_polygon([v for p in face['points'] for v in p],fill=face['color'],outline=face['color'] if face.get('mesh') else '#66727f',width=0 if face.get('mesh') else 1)
        for key,label,a,b in self.specs:
            if key not in self.table.selection():continue
            spec=next(item for item in self.specs if item[0]==key)
            path=[camera.project(world(self.candidate,*point),w,h)[:2] for point in edge_path(spec)]
            self.canvas.create_line(*(value for point in path for value in point),fill='#dc6b24',width=3,dash=(4,3))
    def save(self):
        try:
            project=copy.deepcopy(self.app.project)
            index=next(i for i,p in enumerate(project.parts) if p.id==self.part.id)
            project.parts[index]=copy.deepcopy(self.candidate)
            project=rebuild_dependents(project,self.part.id);project.validate()
            self.app.snapshot();self.app.project=project;self.app.refresh();self.app.fit();self.destroy()
        except ValueError as e:messagebox.showerror('Zaokrąglenia',str(e),parent=self)
