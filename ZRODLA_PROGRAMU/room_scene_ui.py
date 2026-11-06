"""Persistent room/equipment panel; root mounts it beside the 3D inspector."""
import tkinter as tk
from tkinter import ttk

from room_objects import (CATALOG, ROOMS, add_object, update_object, delete_object,
                          set_scene, scene_of, purchased_total, object_vertices)


class RoomScenePanel(ttk.Frame):
    """Requires app.project, snapshot(), refresh(), draw(); selected_scene is an ID."""
    def __init__(self, parent, app):
        super().__init__(parent)
        self.app = app
        self.syncing = False
        self.selected = None
        self.note = tk.StringVar(value='Zakupowe wyposażenie: własne wymiary, bez rozkroju płyt.')
        self.room = tk.StringVar()
        self.brightness = tk.StringVar(value='1')
        self.led = tk.BooleanVar(value=True)
        self.lights = tk.BooleanVar(value=True)
        self.walls = tk.BooleanVar(value=False)
        self.cutaway = tk.BooleanVar(value=True)
        self.ceiling = tk.BooleanVar(value=False)
        self.room_values = {}
        self.catalogue = tk.StringVar(value=CATALOG['washer']['name'])
        self.values = {}
        self.enabled = tk.BooleanVar(value=True)
        self.light_enabled = tk.BooleanVar(value=True)
        self.total = tk.StringVar()
        top = ttk.Frame(self, padding=(10,6));top.pack(fill='x')
        ttk.Label(top,text='POMIESZCZENIE I WYPOSAŻENIE',foreground='#176a78',
                  font=('Segoe UI',10,'bold')).pack(anchor='w')
        self.roomcombo = ttk.Combobox(top,textvariable=self.room,values=list(ROOMS.values()),state='readonly')
        self.roomcombo.pack(fill='x',pady=4)
        self.roomcombo.bind('<<ComboboxSelected>>',lambda e:self.apply_scene())
        row = ttk.Frame(top);row.pack(fill='x')
        ttk.Label(row,text='Jasność (0,1–2)').pack(side='left')
        entry=ttk.Entry(row,textvariable=self.brightness,width=8);entry.pack(side='right')
        entry.bind('<Return>',lambda e:self.apply_scene());entry.bind('<FocusOut>',lambda e:self.apply_scene())
        row = ttk.Frame(top);row.pack(fill='x',pady=3)
        ttk.Checkbutton(row,text='LED mebli',variable=self.led,command=self.apply_scene).pack(side='left')
        ttk.Checkbutton(row,text='Lampy / żarówki',variable=self.lights,command=self.apply_scene).pack(side='left',padx=6)
        ttk.Checkbutton(top,text='Pokaż podłogę i ściany',variable=self.walls,command=self.apply_room).pack(anchor='w')
        footer=ttk.Frame(self,padding=(10,6));footer.pack(side='bottom',fill='x')
        self.apply_button=ttk.Button(footer,text='Zastosuj do obiektu',command=self.apply_selected,style='Accent.TButton')
        self.apply_button.pack(fill='x')
        row=ttk.Frame(footer);row.pack(fill='x',pady=4)
        ttk.Button(row,text='−90°',command=lambda:self.rotate_selected(-90)).pack(side='left',expand=True,fill='x')
        ttk.Button(row,text='+90°',command=lambda:self.rotate_selected(90)).pack(side='left',expand=True,fill='x')
        ttk.Button(row,text='Usuń obiekt',command=self.delete_selected).pack(side='left',expand=True,fill='x')
        ttk.Label(footer,textvariable=self.total,font=('Segoe UI',9)).pack(fill='x')
        ttk.Label(footer,textvariable=self.note,wraplength=295,font=('Segoe UI',9)).pack(fill='x',pady=(3,0))
        canvas=tk.Canvas(self,bg='#f6f8fa',highlightthickness=0,width=320)
        scrollbar=ttk.Scrollbar(self,orient='vertical',command=canvas.yview)
        scrollbar.pack(side='right',fill='y');canvas.pack(fill='both',expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        body=ttk.Frame(canvas,padding=(10,3));window=canvas.create_window(0,0,window=body,anchor='nw')
        body.bind('<Configure>',lambda e:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda e:canvas.itemconfigure(window,width=e.width))
        self.canvas=canvas
        architecture=ttk.LabelFrame(body,text='Wymiary pomieszczenia',padding=5);architecture.pack(fill='x',pady=6)
        from room_architecture import defaults
        for key,label in [('width','Szerokość X'),('depth','Głębokość Y'),('height','Wysokość Z'),('x','Początek X'),('y','Początek Y'),('z','Podłoga Z'),('wall_color','Kolor ścian'),('floor_color','Kolor podłogi')]:
            row=ttk.Frame(architecture);row.pack(fill='x',pady=2)
            ttk.Label(row,text=label).pack(side='left')
            var=tk.StringVar(value=str(defaults()[key]));self.room_values[key]=var
            entry=ttk.Entry(row,textvariable=var,width=11);entry.pack(side='right');entry.bind('<Return>',lambda e:self.apply_room())
        ttk.Checkbutton(architecture,text='Odsłaniaj ściany od strony kamery',variable=self.cutaway,command=self.apply_room).pack(anchor='w')
        ttk.Checkbutton(architecture,text='Sufit',variable=self.ceiling,command=self.apply_room).pack(anchor='w')
        row=ttk.Frame(architecture);row.pack(fill='x')
        ttk.Button(row,text='Zastosuj',command=self.apply_room).pack(side='left',expand=True,fill='x')
        ttk.Button(row,text='Pokaż całe pomieszczenie',command=self.fit_room).pack(side='left',expand=True,fill='x')
        ttk.Label(body,text='Wyposażenie przeciągniesz w 3D w trybie Układaj. Shift: wysokość. Środkowy przycisk: obrót widoku.',wraplength=285,foreground='#176a78').pack(fill='x',pady=5)
        ttk.Label(body,text='Dodaj z katalogu',font=('Segoe UI',9,'bold')).pack(anchor='w')
        self.cataloguecombo=ttk.Combobox(body,textvariable=self.catalogue,values=[c['name'] for c in CATALOG.values()],state='readonly')
        self.cataloguecombo.pack(fill='x',pady=3)
        ttk.Button(body,text='+ Dodaj wyposażenie',command=self.add_selected).pack(fill='x',pady=3)
        treeframe=ttk.Frame(body);treeframe.pack(fill='x',pady=5)
        self.tree=ttk.Treeview(treeframe,show='tree',selectmode='browse',height=5)
        self.tree.pack(side='left',fill='x',expand=True)
        sb=ttk.Scrollbar(treeframe,orient='vertical',command=self.tree.yview);sb.pack(side='right',fill='y')
        self.tree.configure(yscrollcommand=sb.set);self.tree.bind('<<TreeviewSelect>>',self.tree_select)
        self.tree.bind('<Delete>',lambda e:self.delete_selected())
        for key,label in [('name','Nazwa'),('width','Szerokość [mm]'),('depth','Głębokość [mm]'),
                          ('height','Wysokość [mm]'),('x','Pozycja X [mm]'),('y','Pozycja Y [mm]'),
                          ('z','Pozycja Z [mm]'),('yaw','Obrót Z [°]'),('color','Kolor #RRGGBB'),('price','Cena zakupu [zł]')]:
            row=ttk.Frame(body);row.pack(fill='x',pady=2)
            ttk.Label(row,text=label,font=('Segoe UI',9)).pack(side='left')
            var=tk.StringVar();self.values[key]=var
            entry=ttk.Entry(row,textvariable=var,width=20 if key=='name' else 11)
            entry.pack(side='right');entry.bind('<Return>',lambda e:self.apply_selected())
        ttk.Checkbutton(body,text='Widoczny w scenie',variable=self.enabled).pack(anchor='w',pady=4)
        ttk.Checkbutton(body,text='Światło obiektu (lampa / żarówka)',variable=self.light_enabled).pack(anchor='w')
        ttk.Label(body,text='Gabaryty są ustawieniem własnym. Nie tworzą otworów w meblach; dobierz artykuł i miejsce montażu.',
                  wraplength=285,font=('Segoe UI',9),foreground='#68788a').pack(fill='x',pady=5)
        def wheel(event):canvas.yview_scroll(-1 if event.delta>0 else 1,'units');return 'break'
        def bind_wheel(widget):
            widget.bind('<MouseWheel>',wheel,add='+')
            for child in widget.winfo_children():bind_wheel(child)
        bind_wheel(body)
        self.refresh()

    def _error(self, error):
        self.note.set(str(error))
        if hasattr(self.app,'status'):self.app.status.set('Wyposażenie: '+str(error))
        return False

    def _commit(self, candidate, selection=None):
        if candidate.data() == self.app.project.data():return True
        candidate.validate()
        self.app.snapshot()
        self.app.project=candidate
        if selection is not None:
            self.app.selected_scene=selection
            if hasattr(self.app,'selected'):self.app.selected=None
        self.app.refresh()
        self.refresh()
        self.app.draw()
        return True

    def refresh(self):
        self.syncing=True
        try:
            scene=scene_of(self.app.project)
            self.room.set(ROOMS[scene['room']]);self.brightness.set(f'{scene["brightness"]:g}')
            self.led.set(scene['led_enabled']);self.lights.set(scene['lights_enabled'])
            architecture=scene['architecture'];self.walls.set(architecture['enabled']);self.cutaway.set(architecture['cutaway']);self.ceiling.set(architecture['ceiling'])
            for key,var in self.room_values.items():
                value=architecture[key];var.set(f'{value:g}' if isinstance(value,(float,int)) else value)
            self.tree.delete(*self.tree.get_children())
            for obj in scene['objects']:
                self.tree.insert('','end',iid=obj['id'],text=('◉ ' if obj['enabled'] else '○ ')+obj['name'])
            selected=getattr(self.app,'selected_scene',None)
            selected=selected.removeprefix('scene:') if isinstance(selected,str) else selected
            self.selected=selected if any(obj['id']==selected for obj in scene['objects']) else None
            self.app.selected_scene=self.selected
            obj=next((o for o in scene['objects'] if o['id']==self.selected),None)
            for key,var in self.values.items():
                value=obj[key] if obj else ''
                var.set(f'{value:g}' if isinstance(value,(float,int)) else value)
            self.enabled.set(obj['enabled'] if obj else True)
            self.light_enabled.set(obj['light_enabled'] if obj else True)
            if obj:self.tree.selection_set(obj['id']);self.tree.see(obj['id'])
            self.apply_button.configure(state='normal' if obj else 'disabled')
            self.total.set(f'Zakupowe obiekty: {len(scene["objects"])} · {purchased_total(self.app.project):.2f} zł')
        finally:self.syncing=False

    def select_object(self, object_id):
        if isinstance(object_id,str):object_id=object_id.removeprefix('scene:')
        if not any(obj['id']==object_id for obj in scene_of(self.app.project)['objects']):
            return self._error(ValueError('Wybierz istniejący obiekt wyposażenia.'))
        self.app.selected_scene=object_id
        if hasattr(self.app,'selected'):self.app.selected=None
        self.refresh();self.app.draw()
        return True

    def tree_select(self,event=None):
        if self.syncing:return
        selected=self.tree.selection()
        if selected and selected[0]!=self.selected:self.select_object(selected[0])

    def apply_scene(self):
        if self.syncing:return False
        try:
            mode=next(key for key,label in ROOMS.items() if label==self.room.get())
            brightness=float(self.brightness.get().replace(',','.'))
            result=set_scene(self.app.project,room=mode,brightness=brightness,
                             led_enabled=self.led.get(),lights_enabled=self.lights.get())
            return self._commit(result)
        except (ValueError,TypeError,KeyError,StopIteration) as error:return self._error(error)

    def add_selected(self,kind=None):
        try:
            kind=kind or next(k for k,v in CATALOG.items() if v['name']==self.catalogue.get())
            # Keep newly inserted standalone equipment beside the current scene.
            points=object_vertices(self.app.project)
            from core import world
            points += [world(p,u,v,w) for p in self.app.project.parts for u,v in p.points() for w in (0,p.t)]
            x=max((p[0] for p in points),default=-50)+50
            result=add_object(self.app.project,kind,x=x)
            ident=result.scene['objects'][-1]['id']
            self._commit(result,ident)
            if hasattr(self.app,'edit_mode'):self.app.edit_mode.set('layout')
            self.note.set('Dodano wyposażenie. Przeciągnij obiekt w 3D; Shift zmienia wysokość.')
            return ident
        except (ValueError,TypeError,KeyError,StopIteration) as error:return self._error(error)

    def apply_selected(self):
        if self.syncing or self.selected is None:return False
        try:
            values={key:var.get().strip() for key,var in self.values.items()}
            for key in ('width','depth','height','x','y','z','yaw','price'):
                values[key]=float(values[key].replace(',','.'))
            values.update(enabled=self.enabled.get(),light_enabled=self.light_enabled.get())
            result=update_object(self.app.project,self.selected,**values)
            self._commit(result)
            self.note.set('Zaktualizowano zakupowy obiekt. Formatki mebli pozostały zachowane.')
            return True
        except (ValueError,TypeError,KeyError) as error:return self._error(error)

    def rotate_selected(self, angle):
        if self.selected is None:return False
        try:
            obj=next(o for o in scene_of(self.app.project)['objects'] if o['id']==self.selected)
            return self._commit(update_object(self.app.project,self.selected,yaw=obj['yaw']+angle))
        except (ValueError,TypeError,KeyError,StopIteration) as error:return self._error(error)

    def delete_selected(self):
        if self.selected is None:return False
        try:
            result=delete_object(self.app.project,self.selected)
            self.app.selected_scene=None
            return self._commit(result)
        except (ValueError,TypeError,KeyError) as error:return self._error(error)

    def apply_room(self):
        if self.syncing:return False
        try:
            values={key:var.get().strip() for key,var in self.room_values.items()}
            for key in ('width','depth','height','x','y','z'):values[key]=float(values[key].replace(',','.'))
            values.update(enabled=self.walls.get(),cutaway=self.cutaway.get(),ceiling=self.ceiling.get())
            return self._commit(set_scene(self.app.project,architecture=values))
        except (ValueError,TypeError,KeyError) as error:return self._error(error)

    def fit_room(self):
        from room_architecture import vertices
        points=vertices(self.app.project)
        if not points:return self.app.fit()
        self.app.camera.center=tuple((min(p[i] for p in points)+max(p[i] for p in points))/2 for i in range(3))
        self.app.camera.pan=(0,0)
        projected=[self.app.camera.view(p) for p in points]
        width=max(p[0] for p in projected)-min(p[0] for p in projected)
        height=max(p[1] for p in projected)-min(p[1] for p in projected)
        self.app.camera.scale=min((self.app.canvas.winfo_width()-80)/max(width,1),(self.app.canvas.winfo_height()-80)/max(height,1))
        self.app.draw()
