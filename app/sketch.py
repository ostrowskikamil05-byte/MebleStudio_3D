"""Interactive local U/V sketching; shared Part remains the production source."""
import copy
import math
import tkinter as tk
from tkinter import ttk, messagebox
from core import Part, finite, validate_polygon, world, local_world, uid
from assembly import ROLES, DEFAULTS, PARAMETERS, attach, suggested_size, inferred_role, rebuild_dependents, frame
from dock_workspace import DockDocument, VisualizationDocument


def normalized_part(part, points):
    if len(points) < 3:
        raise ValueError('Kontur wymaga przynajmniej trzech punktów.')
    points = [(finite(u), finite(v)) for u, v in points]
    u0, v0 = min(u for u,v in points), min(v for u,v in points)
    q = copy.deepcopy(part)
    if points != list(part.points()):
        old=list(part.points())
        orientation=lambda pts:sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(pts,pts[1:]+pts[:1]))
        if (part.fillets or part.chamfers) and (part.profile or len(points)!=len(old) or orientation(points)*orientation(old)<=0):
            raise ValueError('Przed edycją punktów obrysu usuń fazy i zaokrąglenia kantów w edytorze CAD; zmieni się numeracja krawędzi.')
        # Manual point editing replaces the analytic generator definition.
        # Otherwise Part.validate would silently restore the previous outline.
        q.profile = {}
    q.contour = validate_polygon([(u-u0,v-v0) for u,v in points])
    q.x,q.y,q.z = local_world(part,u0,v0,0)
    q.holes = [[u-u0,v-v0,d,h] for u,v,d,h in part.holes]
    from features import shift_features
    shift_features(q,u0,v0)
    canonical=[(0,0),(max(u for u,v in q.contour),0),(max(u for u,v in q.contour),max(v for u,v in q.contour)),(0,max(v for u,v in q.contour))]
    if any(part.edges) and q.contour==canonical:q.u=canonical[2][0];q.v=canonical[2][1];q.contour=[]
    else:q.edges=[False]*4
    q.validate()
    return q


class Sketch(DockDocument):
    def __init__(self, app, part=None):
        super().__init__(app)
        self.app, self.original = app, part
        self.part = copy.deepcopy(part) if part else Part(name='Własny element',material=app.project.materials[0].id)
        self.points = list(self.part.points()) if part else []
        self.closed = bool(part)
        self.history = []
        self.config={**DEFAULTS,**self.part.assembly.get('config',{})}
        if not part:self.config['hinge_pattern']='hinge45'
        if part and inferred_role(part)=='door' and not part.assembly:
            try:
                f=frame(app.project,part.group)
                if abs(part.x+part.u-f['upper'][0]+self.config['gap'])<1 and part.x>f['lower'][0]+self.config['gap']+1:
                    self.config['hinge_side']='right'
            except ValueError:pass
        self.cursor = None
        self.scale, self.origin = 0.7, [65,540]
        self.title('Szkic CAD 2D → element 3D')
        self.geometry(f'{min(1200,self.winfo_screenwidth()-60)}x{min(850,self.winfo_screenheight()-110)}+15+15')
        self.minsize(1180,600)
        self.transient(app); self.grab_set()
        toolbar=ttk.Frame(self,padding=8);toolbar.pack(fill='x')
        self.mode=tk.StringVar(value='Prostokąt' if not part else 'Wielokąt')
        for name in ('Wielokąt','Prostokąt'):
            ttk.Radiobutton(toolbar,text=name,variable=self.mode,value=name,command=self.change_mode).pack(side='left',padx=6)
        self.snap=tk.BooleanVar(value=True);self.ortho=tk.BooleanVar(value=False)
        self.point_snap=tk.BooleanVar(value=True);self.snap_reason='';self.guide_cache=None
        ttk.Checkbutton(toolbar,text='Siatka',variable=self.snap).pack(side='left')
        self.grid=tk.StringVar(value='10')
        ttk.Entry(toolbar,textvariable=self.grid,width=5).pack(side='left')
        ttk.Label(toolbar,text='mm').pack(side='left')
        ttk.Checkbutton(toolbar,text='Orto / Shift',variable=self.ortho).pack(side='left',padx=6)
        ttk.Checkbutton(toolbar,text='Punkty / środki',variable=self.point_snap).pack(side='left')
        for label,command in [('Cofnij',self.undo),('Wyczyść',self.clear),('Zamknij kontur',self.close_contour),('Dopasuj',self.fit)]:
            ttk.Button(toolbar,text=label,command=command).pack(side='left',padx=3)
        body=ttk.Frame(self);body.pack(fill='both',expand=True)
        sidebar=ttk.Frame(body);sidebar.pack(side='right',fill='y')
        viewport=tk.Canvas(sidebar,width=305,highlightthickness=0,bg='#f6f8fa')
        scrollbar=ttk.Scrollbar(sidebar,orient='vertical',command=viewport.yview)
        scrollbar.pack(side='right',fill='y');viewport.pack(side='left',fill='y',expand=True)
        viewport.configure(yscrollcommand=scrollbar.set)
        side=ttk.Frame(viewport,padding=10)
        side_window=viewport.create_window(0,0,window=side,anchor='nw')
        side.bind('<Configure>',lambda e:viewport.configure(scrollregion=viewport.bbox('all')))
        viewport.bind('<Configure>',lambda e:viewport.itemconfigure(side_window,width=e.width))
        self.canvas=tk.Canvas(body,bg='#f7fafc',highlightthickness=0,takefocus=True)
        self.canvas.pack(side='left',fill='both',expand=True)
        sketch_tools=ttk.Frame(self,padding=(8,2));sketch_tools.pack(fill='x',before=body)
        for label,action in [('Odbij U',lambda:self.transform_sketch('mirror_u')),('Odbij V',lambda:self.transform_sketch('mirror_v')),('Obróć 90°',lambda:self.transform_sketch('rotate')),('Odsuń kontur…',self.offset_sketch),('Odcinek poziomy',lambda:self.constrain_edge('horizontal')),('Odcinek pionowy',lambda:self.constrain_edge('vertical')),('Długość odcinka…',lambda:self.constrain_edge('length'))]:
            ttk.Button(sketch_tools,text=label,command=action).pack(side='left',padx=2)
        ttk.Label(side,text='1. Wybierz mebel i funkcję',style='Title.TLabel').pack(anchor='w',pady=(0,8))
        groups=list(dict.fromkeys(p.group for p in app.project.parts if not p.assembly.get('generated_by')))
        selected=app.selected_part()
        initial_group=self.part.group if part else selected.group if selected else groups[0] if groups else 'Mebel'
        self.target=tk.StringVar(value=initial_group)
        ttk.Combobox(side,textvariable=self.target,values=groups,state='readonly').pack(fill='x',pady=3)
        self.role=tk.StringVar(value=next((label for label,value in ROLES.items() if value==inferred_role(self.part)),'Inny / swobodny element'))
        self.role_combo=ttk.Combobox(side,textvariable=self.role,values=list(ROLES),state='readonly')
        self.role_combo.pack(fill='x',pady=3);self.role_combo.bind('<<ComboboxSelected>>',self.role_changed)
        self.automatic=tk.BooleanVar(value=self.config['auto_hardware'])
        ttk.Checkbutton(side,text='Generuj okucia i wiercenia',variable=self.automatic).pack(anchor='w')
        self.hinge_side=tk.StringVar(value='Lewy' if self.config['hinge_side']=='left' else 'Prawy')
        ttk.Label(side,text='Zawiasy na boku').pack(anchor='w')
        self.hinge_combo=ttk.Combobox(side,textvariable=self.hinge_side,values=['Lewy','Prawy'],state='readonly');self.hinge_combo.pack(fill='x')
        self.level=tk.StringVar(value=str(self.config['level']))
        ttk.Label(side,text='Poziom półki / szuflady od dołu [mm]').pack(anchor='w')
        self.level_entry=ttk.Entry(side,textvariable=self.level);self.level_entry.pack(fill='x')
        self.replace_existing=tk.BooleanVar(value=False)
        ttk.Checkbutton(side,text='Zastąp istniejący bok / górę / dół',variable=self.replace_existing).pack(anchor='w',pady=3)
        ttk.Button(side,text='Parametry okuć i szuflady…',command=self.recipe_settings).pack(fill='x',pady=3)
        ttk.Button(side,text='Dopasuj prostokąt do mebla',command=self.fit_to_furniture).pack(fill='x',pady=3)
        ttk.Label(side,text='2. Narysuj lub dopasuj kontur',style='Title.TLabel').pack(anchor='w',pady=(10,6))
        self.fields={}
        for key,label in [('name','Nazwa'),('group','Mebel / grupa'),('t','Grubość [mm]')]:
            ttk.Label(side,text=label).pack(anchor='w')
            var=tk.StringVar(value=str(getattr(self.part,key)));self.fields[key]=var
            ttk.Entry(side,textvariable=var).pack(fill='x',pady=(0,6))
        ttk.Label(side,text='Płaszczyzna (element swobodny)').pack(anchor='w')
        self.plane=tk.StringVar(value=self.part.plane)
        self.plane_combo=ttk.Combobox(side,textvariable=self.plane,values=['XY','XZ','YZ'],state='readonly');self.plane_combo.pack(fill='x')
        ttk.Label(side,text='Materiał').pack(anchor='w',pady=(8,0))
        self.material=ttk.Combobox(side,values=[m.name for m in app.project.materials],state='readonly')
        self.material.pack(fill='x');self.material.current(next(i for i,m in enumerate(app.project.materials) if m.id==self.part.material))
        ttk.Label(side,text='Punkty U, V [mm]').pack(anchor='w',pady=(12,0))
        self.listbox=tk.Listbox(side,height=7,exportselection=False)
        self.listbox.pack(fill='x');self.listbox.bind('<<ListboxSelect>>',self.select_point)
        row=ttk.Frame(side);row.pack(fill='x',pady=4)
        self.u=tk.StringVar(value='0');self.v=tk.StringVar(value='0')
        for label,var in [('U',self.u),('V',self.v)]:
            ttk.Label(row,text=label).pack(side='left');ttk.Entry(row,textvariable=var,width=10).pack(side='left',padx=3)
        row=ttk.Frame(side);row.pack(fill='x')
        ttk.Button(row,text='Dodaj punkt',command=self.numeric_point).pack(side='left')
        ttk.Button(row,text='Zmień wybrany',command=self.edit_point).pack(side='left')
        ttk.Label(side,text='Odcinek od ostatniego punktu').pack(anchor='w',pady=(12,0))
        self.length=tk.StringVar(value='100');self.angle=tk.StringVar(value='0')
        row=ttk.Frame(side);row.pack(fill='x')
        for label,var in [('mm',self.length),('°',self.angle)]:
            ttk.Entry(row,textvariable=var,width=10).pack(side='left');ttk.Label(row,text=label).pack(side='left',padx=3)
        ttk.Button(side,text='Dodaj odcinek: długość + kąt',command=self.segment).pack(fill='x',pady=5)
        ttk.Label(side,text='0° = +U, 90° = +V.\nRolka: zoom · PPM: przesuwanie\nEnter: zamknij · Backspace: cofnij\nKliknij pierwszy punkt, aby zamknąć.\nEdycja punktów: wybierz na liście.\nObrzeża konturu zostaną wyłączone.',wraplength=270).pack(anchor='w',pady=8)
        footer=ttk.Frame(self,padding=6);footer.pack(side='bottom',fill='x',before=body)
        ttk.Button(footer,text='Podgląd montażu',command=self.preview).pack(side='left',padx=4)
        ttk.Button(footer,text='Zapisz i rysuj następny',command=lambda:self.save(keep_open=True)).pack(side='left',padx=4)
        ttk.Button(footer,text='Nowy element',command=self.new_element).pack(side='left',padx=4)
        ttk.Button(footer,text='3. Dodaj do mebla / zapisz zmiany',command=self.save,style='Accent.TButton').pack(side='right',padx=4)
        self.status=tk.StringVar(value='Kliknij pierwszy punkt lub wpisz U i V. Wszystkie wymiary w mm.')
        ttk.Label(self,textvariable=self.status,padding=8).pack(side='bottom',fill='x',before=body)
        self.canvas.bind('<Motion>',self.motion)
        self.canvas.bind('<Button-1>',self.click)
        self.canvas.bind('<MouseWheel>',self.zoom)
        self.canvas.bind('<ButtonPress-3>',self.pan_start)
        self.canvas.bind('<B3-Motion>',self.pan)
        self.canvas.bind('<Configure>',lambda e:self.draw())
        self.canvas.bind('<Return>',lambda e:self.close_contour())
        self.canvas.bind('<BackSpace>',lambda e:self.undo())
        self.bind('<Control-z>',lambda e:(self.undo(),'break')[1])
        self.protocol('WM_DELETE_WINDOW',self.cancel)
        self.target.trace_add('write',lambda *_:self.fields['group'].set(self.target.get()))
        self.update_controls()
        def scroll_sidebar(event):viewport.yview_scroll(-1 if event.delta>0 else 1,'units');return 'break'
        def bind_scroll(widget):
            widget.bind('<MouseWheel>',scroll_sidebar)
            for child in widget.winfo_children():bind_scroll(child)
        bind_scroll(side);viewport.bind('<MouseWheel>',scroll_sidebar)
        from sketch_live import install
        install(self)
        self.mark_clean()
        self.after(80,self.fit)

    def _document_state(self):
        return (super()._document_state(), tuple(self.points), self.closed)

    def role_changed(self,event=None):
        if self.fields['name'].get()=='Własny element' or self.fields['name'].get() in ROLES:self.fields['name'].set(self.role.get())
        self.fields['group'].set(self.target.get())
        self.update_controls()
        if hasattr(self,'auto_fit') and self.auto_fit.get():self.fit_to_furniture()
        else:self.status.set('Narysuj kontur lub użyj Dopasuj prostokąt do mebla. Wymiary U/V zmieniają jednocześnie szkic i podgląd 3D.')

    def update_controls(self):
        role=ROLES[self.role.get()]
        self.hinge_combo.configure(state='readonly' if role=='door' else 'disabled')
        self.level_entry.configure(state='normal' if role in ('shelf','drawer') else 'disabled')
        self.plane_combo.configure(state='readonly' if role=='other' else 'disabled')

    def current_config(self):
        return {**self.config,'level':self.number(self.level),'hinge_side':'left' if self.hinge_side.get()=='Lewy' else 'right','auto_hardware':self.automatic.get()}

    def recipe_settings(self):
        dialog=tk.Toplevel(self);dialog.title('Wzorce robocze — edytowalne parametry');dialog.geometry('690x650')
        dialog.transient(self.winfo_toplevel());dialog.grab_set()
        ttk.Label(dialog,text='Parametry przykładowe. Dobierz je do konkretnego okucia.\nLiczba zawiasów nie jest automatycznym doborem nośności.',padding=12).pack(fill='x')
        holder=ttk.Frame(dialog);holder.pack(fill='both',expand=True)
        canvas=tk.Canvas(holder,highlightthickness=0);scroll=ttk.Scrollbar(holder,orient='vertical',command=canvas.yview)
        scroll.pack(side='right',fill='y');canvas.pack(fill='both',expand=True);canvas.configure(yscrollcommand=scroll.set)
        form=ttk.Frame(canvas,padding=10);item=canvas.create_window(0,0,window=form,anchor='nw')
        form.bind('<Configure>',lambda e:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda e:canvas.itemconfigure(item,width=e.width))
        variables={}
        for key,label in [('gap','Luz / szczelina [mm]'),('offset','Przegroda od lewego wnętrza [mm]')]+PARAMETERS:
            row=ttk.Frame(form);row.pack(fill='x',pady=3)
            ttk.Label(row,text=label).pack(side='left')
            var=tk.StringVar(value=str(self.config[key]));variables[key]=var
            ttk.Entry(row,textvariable=var,width=12).pack(side='right')
        def close():dialog.destroy();self.grab_set()
        def commit():
            try:
                from assembly import recipe
                self.config=recipe({**self.config,**{key:self.number(var) for key,var in variables.items()}})
                close()
            except ValueError as e:messagebox.showerror('Parametry',str(e),parent=dialog)
        ttk.Button(dialog,text='Zapisz parametry',command=commit).pack(fill='x',padx=12,pady=8)
        dialog.protocol('WM_DELETE_WINDOW',close)

    def fit_to_furniture(self):
        try:
            role=ROLES[self.role.get()]
            u,v=suggested_size(self.app.project,self.target.get(),role,self.number(self.fields['t']),self.current_config())
            if min(u,v)<=0:raise ValueError('Brak miejsca na element.')
            self.checkpoint();self.points=[(0,0),(u,0),(u,v),(0,v)];self.closed=True
            self.cursor=None;self.mode.set('Prostokąt');self.update_list();self.fit()
            self.status.set(f'Dopasowano {u:g} × {v:g} mm. Sprawdź podgląd montażu i zapisz.')
        except (ValueError,KeyError) as e:messagebox.showerror('Dopasowanie',str(e),parent=self)

    def candidate(self):
        self.ensure_current_project(raise_error=True)
        if self.original and not any(p.id==self.original.id for p in self.app.project.parts):
            raise ValueError('Element został usunięty z projektu. Otwórz szkic ponownie lub rozpocznij nowy element.')
        if not self.closed:raise ValueError('Najpierw zamknij kontur.')
        base=copy.deepcopy(self.part)
        role=ROLES[self.role.get()]
        # Generated drilling is recomputed from the new shape; manual drilling remains.
        if role!='other':base.operations=[op for op in base.operations if not op.get('owner_id')]
        for key,var in self.fields.items():setattr(base,key,self.number(var) if key=='t' else var.get())
        base.operations=[op for op in base.operations if op.get('recipe_id')!='handle']
        base.plane=self.plane.get();base.material=self.app.project.materials[self.material.current()].id
        q=normalized_part(base,self.points)
        group=self.target.get() if role!='other' else self.fields['group'].get()
        if not self.original and self.replace_existing.get() and role in ('left','right','top','bottom'):
            targets=[p for p in self.app.project.parts if p.group==group and inferred_role(p)==role]
            if len(targets)!=1:raise ValueError('Zastępowanie wymaga dokładnie jednego elementu danej funkcji w meblu.')
            q.id=targets[0].id
            if targets[0].holes:raise ValueError('Element ma ręczne wiercenia. Wybierz go i użyj „Edytuj szkic”, aby je zachować.')
        if role=='other':
            from assembly import clear_generated
            temp=copy.deepcopy(self.app.project);retained=[copy.deepcopy(h) for h in temp.hardware if h.get('owner_id')==q.id and h.get('manual')];clear_generated(temp,q.id)
            temp.parts=[p for p in temp.parts if p.id!=q.id];q.assembly={};q.role='other';q.operations=[op for op in q.operations if not op.get('owner_id')]
            peer=next((p for p in temp.parts if p.group==q.group),None)
            if peer:q.layout=copy.deepcopy(peer.layout)
            temp.parts.append(q);temp.hardware.extend(retained)
            from fittings import refresh_handles
            from opening_options import refresh_openings
            refresh_handles(temp,q.id);refresh_openings(temp,q.id);temp=rebuild_dependents(temp,q.id)
            return temp,next(p for p in temp.parts if p.id==q.id)
        temp,q=attach(self.app.project,q,group,role,self.current_config())
        temp=rebuild_dependents(temp,q.id)
        return temp,next(p for p in temp.parts if p.id==q.id)

    def preview(self):
        try:
            temp,q=self.candidate()
            count=sum(len(p.operations) for p in temp.parts)-sum(len(p.operations) for p in self.app.project.parts)
            hardware=[h for h in temp.hardware if h.get('owner_id')==q.id]
            text=f'{q.group} · {self.role.get()} · {q.u:g} × {q.v:g} × {q.t:g} mm\n'
            text+=f'Pozycja X,Y,Z: {q.x:g}, {q.y:g}, {q.z:g} · płaszczyzna {q.plane}\n'
            text+='Okucia: '+(', '.join(f'{h["qty"]:g} × {h["name"]}' for h in hardware) or 'bez okuć')
            text+=f'\nNowe formatki: {len(temp.parts)-len(self.app.project.parts):+d} · zmiana liczby wierceń: {count:+d}'
            dialog=VisualizationDocument(self.app,project=temp,selected=q.id,title='Podgląd montażu — '+q.name,note=text)
            def close():
                dialog.destroy()
                if self.winfo_exists():self.workspace.select(self)
            ttk.Button(dialog,text='Wróć do szkicu',command=close).pack(pady=8)
            dialog.protocol('WM_DELETE_WINDOW',close)
            return dialog
        except (ValueError,KeyError,IndexError) as e:messagebox.showerror('Montaż',str(e),parent=self)

    def number(self,var):return finite(var.get().replace(',','.'))
    def checkpoint(self):self.history.append((self.points[:],self.closed,copy.deepcopy(self.part)));self.history=self.history[-60:]
    def cancel(self):
        if self.has_changes() and not messagebox.askyesno('Zamknij szkic','Odrzucić niezapisane zmiany szkicu i jego parametrów?',parent=self.winfo_toplevel()):return False
        self.destroy();return True
    def change_mode(self):
        self.status.set('Wyczyść kontur, aby rozpocząć nowy kształt.' if self.points else 'Kliknij dwa przeciwległe narożniki prostokąta.' if self.mode.get()=='Prostokąt' else 'Klikaj kolejne punkty wielokąta.')
    def screen(self,p):return self.origin[0]+p[0]*self.scale,self.origin[1]-p[1]*self.scale
    def guide_geometry(self):
        key=(id(self.app.project),self.target.get(),self.role.get(),self.level.get(),self.plane.get(),self.fields['t'].get())
        if self.guide_cache and self.guide_cache[0]==key:return self.guide_cache[1]
        segments=[]
        try:
            from assembly import set_placement
            role=ROLES[self.role.get()];reference=copy.deepcopy(self.part);reference.plane=self.plane.get()
            if role!='other':
                reference.role=role;reference.u,reference.v=suggested_size(self.app.project,self.target.get(),role,reference.t,self.current_config())
                set_placement(reference,frame(self.app.project,self.target.get()),self.current_config())
                peer=next((p for p in self.app.project.parts if p.group==self.target.get()),None)
                if peer:reference.layout=copy.deepcopy(peer.layout)
            origin=world(reference,0,0,0)
            axes=[tuple(b-a for a,b in zip(origin,world(reference,*point))) for point in ((1,0,0),(0,1,0))]
            def local_point(point):
                difference=[b-a for a,b in zip(origin,point)]
                return tuple(sum(a*b for a,b in zip(difference,axis)) for axis in axes)
            for p in self.app.project.parts:
                if p.group!=self.target.get() or p.id==getattr(self.original,'id',None):continue
                points=p.points()
                for thickness in (0,p.t):
                    mapped=[local_point(world(p,u,v,thickness)) for u,v in points]
                    for a,b in zip(mapped,mapped[1:]+mapped[:1]):
                        if math.dist(a,b)>.01:segments.append((a,b))
        except ValueError:pass
        self.guide_cache=(key,segments);return segments
    def local(self,event):
        p=((event.x-self.origin[0])/self.scale,(self.origin[1]-event.y)/self.scale)
        self.snap_reason='';snap_point=None
        if self.point_snap.get():
            candidates=[(point,'punkt') for point in self.points]
            segments=list(zip(self.points,self.points[1:]))+self.guide_geometry()
            for a,b in segments:
                candidates.extend([(a,'punkt mebla'),(b,'punkt mebla'),(((a[0]+b[0])/2,(a[1]+b[1])/2),'środek')])
            if candidates:
                point,reason=min(candidates,key=lambda item:math.dist(p,item[0]))
                if math.dist(p,point)*self.scale<=9:snap_point=point;self.snap_reason=reason
        if snap_point is not None:p=snap_point
        elif self.snap.get():
            g=self.number(self.grid)
            if g<=0:raise ValueError('Krok siatki musi być dodatni.')
            p=tuple(round(x/g)*g for x in p)
        if self.points and not self.closed and self.mode.get()=='Wielokąt' and (self.ortho.get() or event.state & 1):
            a=self.points[-1];p=(p[0],a[1]) if abs(p[0]-a[0])>=abs(p[1]-a[1]) else (a[0],p[1])
        return p
    def motion(self,event):
        try:
            self.cursor=self.local(event);u,v=self.cursor
            text=f'U {u:.2f} · V {v:.2f} mm'
            if self.snap_reason:text+=' · przyciąganie: '+self.snap_reason
            if self.points and not self.closed:
                a=self.points[-1];du,dv=u-a[0],v-a[1]
                text+=f'   ΔU {du:.2f} · ΔV {dv:.2f} · długość {math.hypot(du,dv):.2f} mm · kąt {math.degrees(math.atan2(dv,du)):.1f}°'
            self.status.set(text);self.draw()
        except ValueError as e:self.status.set(str(e))
    def click(self,event):
        self.canvas.focus_set()
        try:
            if not self.closed and len(self.points)>=3 and math.dist((event.x,event.y),self.screen(self.points[0]))<12:
                self.close_contour();return
            self.add_point(self.local(event))
        except ValueError as e:messagebox.showerror('Szkic',str(e),parent=self)
    def add_point(self,p):
        if self.closed:
            if messagebox.askyesno('Następny element','Kontur jest gotowy. Zapisać go i rozpocząć następny element?',parent=self):
                if not self.save(keep_open=True):return
            else:return
        p=tuple(finite(x) for x in p)
        if len(self.points)>=128:raise ValueError('Maksymalnie 128 punktów.')
        if self.points and math.dist(self.points[-1],p)<0.01:raise ValueError('Punkt pokrywa się z poprzednim.')
        if self.mode.get()=='Prostokąt' and self.points:
            a=self.points[0]
            if a[0]==p[0] or a[1]==p[1]:raise ValueError('Prostokąt musi mieć dodatnią szerokość i wysokość.')
            self.checkpoint();self.points=[a,(p[0],a[1]),p,(a[0],p[1])];self.closed=True
        else:self.checkpoint();self.points.append(p)
        self.cursor=None;self.update_list();self.draw()
    def numeric_point(self):
        try:self.add_point((self.number(self.u),self.number(self.v)))
        except ValueError as e:messagebox.showerror('Punkt',str(e),parent=self)
    def segment(self):
        try:
            if not self.points:raise ValueError('Najpierw dodaj punkt początkowy.')
            length=self.number(self.length);angle=math.radians(self.number(self.angle))
            if length<=0:raise ValueError('Długość musi być dodatnia.')
            a=self.points[-1];self.add_point((a[0]+length*math.cos(angle),a[1]+length*math.sin(angle)))
        except ValueError as e:messagebox.showerror('Odcinek',str(e),parent=self)
    def select_point(self,event=None):
        selection=self.listbox.curselection()
        if selection:
            u,v=self.points[selection[0]];self.u.set(f'{u:.4f}');self.v.set(f'{v:.4f}')
            self.draw()
    def edit_point(self):
        try:
            selection=self.listbox.curselection()
            if not selection:raise ValueError('Wybierz punkt na liście.')
            points=self.points[:];points[selection[0]]=(self.number(self.u),self.number(self.v))
            if self.closed:
                base=copy.deepcopy(self.part);base.operations=[op for op in base.operations if not op.get('owner_id')]
                normalized_part(base,points)
            self.checkpoint();self.points=points;self.update_list();self.draw()
        except ValueError as e:messagebox.showerror('Punkt',str(e),parent=self)
    def close_contour(self):
        try:
            base=copy.deepcopy(self.part);base.operations=[op for op in base.operations if not op.get('owner_id')]
            normalized_part(base,self.points)
            if not self.closed:self.checkpoint();self.closed=True
            self.cursor=None;self.draw();self.status.set('Kontur zamknięty. Nadaj grubość i zapisz element do projektu.')
        except ValueError as e:messagebox.showerror('Kontur',str(e),parent=self)
    def undo(self):
        if self.history:self.points,self.closed,self.part=self.history.pop();self.cursor=None;self.update_list();self.draw()
    def clear(self):self.checkpoint();self.points=[];self.closed=False;self.cursor=None;self.update_list();self.draw()
    def update_list(self):
        from sketch_live import sync
        sync(self)
        self.listbox.delete(0,'end')
        for i,(u,v) in enumerate(self.points):self.listbox.insert('end',f'{i+1}.   {u:.2f} ; {v:.2f}')
    def fit(self):
        w,h=max(self.canvas.winfo_width(),100),max(self.canvas.winfo_height(),100)
        points=self.points or [(0,0),(800,600)]
        xmin,xmax=min(p[0] for p in points),max(p[0] for p in points)
        ymin,ymax=min(p[1] for p in points),max(p[1] for p in points)
        self.scale=min(max(w-120,30)/max(xmax-xmin,100),max(h-96,30)/max(ymax-ymin,100))
        self.origin=[w/2-(xmin+xmax)*self.scale/2,h/2+(ymin+ymax)*self.scale/2]
        self.update_list();self.draw()
    def zoom(self,event):
        u,v=(event.x-self.origin[0])/self.scale,(self.origin[1]-event.y)/self.scale
        self.scale=min(30,max(.015,self.scale*(1.15 if event.delta>0 else 1/1.15)))
        self.origin=[event.x-u*self.scale,event.y+v*self.scale];self.draw()
    def pan_start(self,event):self.pan_anchor=(event.x,event.y)
    def pan(self,event):
        self.origin[0]+=event.x-self.pan_anchor[0];self.origin[1]+=event.y-self.pan_anchor[1]
        self.pan_anchor=(event.x,event.y);self.draw()
    def label(self,x,y,text,color='#24586c'):
        item=self.canvas.create_text(x,y,text=text,fill=color,font=('Segoe UI',9,'bold'))
        box=self.canvas.bbox(item)
        if box:
            rect=self.canvas.create_rectangle(box,fill='#f7fafc',outline='');self.canvas.tag_lower(rect,item)
    def draw(self):
        c=self.canvas;c.delete('all');w,h=c.winfo_width(),c.winfo_height()
        try:g=max(.1,self.number(self.grid))
        except ValueError:g=10
        while g*self.scale<24:g*=2
        for screen_axis,limit,origin in [(0,w,self.origin[0]),(1,h,self.origin[1])]:
            step=g*self.scale
            for i in range(max(0,int(limit/step)+2)):
                p=origin%step+i*step
                if screen_axis==0:c.create_line(p,0,p,h,fill='#e4eaf0')
                else:c.create_line(0,p,w,p,fill='#e4eaf0')
        c.create_line(0,self.origin[1],w,self.origin[1],fill='#a93f3f')
        c.create_line(self.origin[0],0,self.origin[0],h,fill='#36805b')
        for a,b in self.guide_geometry():c.create_line(*self.screen(a),*self.screen(b),fill='#b4bec9',dash=(3,4))
        pts=self.points[:]
        if self.cursor and pts and not self.closed:
            if self.mode.get()=='Prostokąt':
                a,b=pts[0],self.cursor;pts=[a,(b[0],a[1]),b,(a[0],b[1])]
            else:pts.append(self.cursor)
        coords=[self.screen(p) for p in pts]
        if len(coords)>=3 and (self.closed or self.mode.get()=='Prostokąt'):
            c.create_polygon(coords,fill='#d7e9ec',outline='#176a78',width=2)
        pairs=list(zip(coords,coords[1:]))
        local_pairs=list(zip(pts,pts[1:]))
        if (self.closed or self.mode.get()=='Prostokąt') and len(coords)>=3:
            pairs.append((coords[-1],coords[0]));local_pairs.append((pts[-1],pts[0]))
        for (a,b),(pa,pb) in zip(pairs,local_pairs):
            c.create_line(*a,*b,fill='#176a78',width=2)
            if not self.part.profile and len(pts)<=20 and math.dist(a,b)>=45:
                self.label((a[0]+b[0])/2,(a[1]+b[1])/2-12,f'{math.dist(pa,pb):.2f} mm')
        selected_points=set(self.listbox.curselection())
        for i,p in enumerate(self.points):
            x,y=self.screen(p);c.create_oval(x-4,y-4,x+4,y+4,fill='#176a78',outline='white')
            if (not self.part.profile and len(self.points)<=20) or i in selected_points:c.create_text(x+9,y+10,text=str(i+1),anchor='w',fill='#345')
        if len(pts)>=2:
            xmin,xmax=min(p[0] for p in pts),max(p[0] for p in pts)
            ymin,ymax=min(p[1] for p in pts),max(p[1] for p in pts)
            a=self.screen((xmin,ymin));b=self.screen((xmax,ymax))
            c.create_line(a[0],a[1]+20,b[0],a[1]+20,fill='#b06d27',arrow='both')
            self.label((a[0]+b[0])/2,a[1]+33,f'U {xmax-xmin:.2f} mm','#986022')
            c.create_line(b[0]+22,a[1],b[0]+22,b[1],fill='#b06d27',arrow='both')
            self.label(b[0]+52,(a[1]+b[1])/2,f'V {ymax-ymin:.2f} mm','#986022')
    def new_element(self,discard_confirmed=False):
        if self.has_changes() and not discard_confirmed and not messagebox.askyesno('Nowy element','Odrzucić niezapisany szkic i rozpocząć nowy? Użyj „Zapisz i rysuj następny”, aby zachować go.',parent=self.winfo_toplevel()):return False
        group=self.target.get();material=self.app.project.materials[self.material.current()].id
        self.original=None;self.part=Part(name='Własny element',group=group,material=material,t=self.number(self.fields['t']))
        self.points=[];self.closed=False;self.cursor=None;self.history=[];self.replace_existing.set(False)
        self.fields['name'].set('Własny element');self.fields['group'].set(group);self.listbox.delete(0,'end')
        self.status.set('Nowy element: wybierz funkcję i rysuj. Poprzedni zapis pozostaje w meblu.');self.draw();self.mark_clean();return True

    def set_sketch_points(self,points):
        if self.part.holes or self.part.operations or self.part.fillets or self.part.chamfers or self.part.machining:
            raise ValueError('Ten szkic ma obróbkę. Zastosuj odbicie części z menu CAD, aby przeliczyć także otwory i profile.')
        if self.closed:normalized_part(self.part,points)
        self.checkpoint();self.points=points;self.cursor=None;self.update_list();self.fit()

    def transform_sketch(self,action):
        try:
            if not self.points:raise ValueError('Najpierw narysuj kontur.')
            from native_model import available,call
            if available():points=[tuple(p) for p in call('sketch','transform_points',self.points,action)]
            else:
                xmin,xmax=min(p[0] for p in self.points),max(p[0] for p in self.points)
                ymin,ymax=min(p[1] for p in self.points),max(p[1] for p in self.points)
                cx,cy=(xmin+xmax)/2,(ymin+ymax)/2
                points=[(2*cx-u,v) if action=='mirror_u' else (u,2*cy-v) if action=='mirror_v' else (cx-(v-cy),cy+(u-cx)) for u,v in self.points]
            self.set_sketch_points(points)
        except ValueError as e:messagebox.showerror('Szkic',str(e),parent=self)

    def offset_sketch(self):
        from tkinter import simpledialog
        from cad_geometry import kernel
        distance=simpledialog.askfloat('Odsunięcie konturu','Odległość w mm: dodatnia na zewnątrz, ujemna do środka.',parent=self)
        if distance is None:return
        try:
            if not self.closed:raise ValueError('Najpierw zamknij kontur.')
            cq=kernel();wires=cq.Workplane('XY').polyline(self.points).close().offset2D(distance,kind='intersection').vals()
            if len(wires)!=1:raise ValueError('Odsunięcie daje kilka konturów. Zmniejsz odległość.')
            points=[]
            for edge in wires[0].Edges():
                point=edge.startPoint().toTuple();points.append(point[:2])
            self.set_sketch_points(points)
        except Exception as e:messagebox.showerror('Odsunięcie',str(e),parent=self)

    def constrain_edge(self,kind):
        from tkinter import simpledialog
        try:
            selection=self.listbox.curselection()
            if not selection or len(self.points)<2:raise ValueError('Wybierz punkt początkowy odcinka na liście.')
            i=selection[0];j=(i+1)%len(self.points)
            if not self.closed and j==0:raise ValueError('Ostatni punkt otwartego konturu nie ma kolejnego odcinka.')
            a,b=self.points[i],self.points[j];points=self.points[:]
            if kind=='horizontal':points[j]=(b[0],a[1])
            elif kind=='vertical':points[j]=(a[0],b[1])
            else:
                length=simpledialog.askfloat('Długość odcinka','Nowa długość [mm]',initialvalue=math.dist(a,b),parent=self,minvalue=.1)
                if length is None:return
                old=math.dist(a,b)
                if old==0:raise ValueError('Punkty się pokrywają.')
                points[j]=(a[0]+(b[0]-a[0])*length/old,a[1]+(b[1]-a[1])*length/old)
            from native_model import available,call
            if available():
                result=call('sketch','constrain_points',self.points,i,self.closed,kind,None if kind in ('horizontal','vertical') else length)
                if result is None:return
                points=[tuple(p) for p in result]
            self.set_sketch_points(points)
        except ValueError as e:messagebox.showerror('Odcinek',str(e),parent=self)

    def save(self,keep_open=False):
        try:
            temp,q=self.candidate()
            temp.validate();self.app.snapshot();self.app.project=temp;self.app.selected=q.id
            self.app.refresh();self.app.fit();self.app.status.set('Zapisano szkic jako element 3D i przeliczono dane produkcyjne')
            if keep_open:
                self.history=[];self.new_element(discard_confirmed=True)
            else:self.destroy()
            return True
        except (ValueError,IndexError) as e:
            messagebox.showerror('Zapis szkicu',str(e),parent=self);return False


# Verified C++ geometry calculations; exact CAD validation is retained.
from native_model import install_shapes as _install_cpp
_install_cpp(globals(), 'sketch')
