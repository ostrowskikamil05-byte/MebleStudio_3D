"""Compact persistent corner inspector; no extra window or modal grab."""
import copy
import tkinter as tk
from tkinter import ttk
from corner_furniture import default_corner_config,read_corner_config,corner_status,create_corner,convert_to_corner,corner_outline,normalize_corner_config


class CornerPanel(ttk.Frame):
    def __init__(self,parent,app):
        super().__init__(parent,padding=8);self.app=app;self.group=None;self.config={};self.syncing=False;self.job=None;self.generation=app.project_generation;self.fields={};self.mapping={};self.signature=None;self.has_draft=False
        ttk.Label(self,text='NAROŻNIK',font=('Segoe UI',10,'bold'),foreground='#176a78').pack(anchor='w')
        self.note=tk.StringVar(value='Wybierz szafkę do konwersji lub dodaj nowy narożnik.')
        footer=ttk.Frame(self);footer.pack(side='bottom',fill='x')
        actions=ttk.Frame(footer);actions.pack(fill='x',pady=5)
        ttk.Button(actions,text='Zamień wybrany',command=self.convert_selected).pack(side='left',expand=True,fill='x')
        ttk.Button(actions,text='+ Nowy',command=self.new_corner).pack(side='left',expand=True,fill='x')
        self.apply_button=ttk.Button(footer,text='Zastosuj narożnik',command=self.apply);self.apply_button.pack(fill='x')
        ttk.Label(footer,textvariable=self.note,wraplength=280,font=('Segoe UI',9)).pack(fill='x',pady=4)
        from quick_studio_ui import ScrollPanel
        self.scroll=ScrollPanel(self);self.scroll.pack(fill='both',expand=True);self.body=self.scroll.body
        self.preview=tk.Canvas(self.body,height=104,bg='#edf1f4',highlightthickness=0);self.preview.pack(fill='x',pady=4)
        self.preview.bind('<Configure>',lambda e:self.draw_preview())
        row=ttk.Frame(self.body);row.pack(fill='x')
        for label,shape in [('L','L'),('Ścięty','chamfer'),('Okrągły','rounded')]:ttk.Button(row,text=label,command=lambda s=shape:self.quick_shape(s)).pack(side='left',expand=True,fill='x')
        self.combo('furniture_type','Typ',{'Szafka':'cabinet','Szafa':'wardrobe'})
        self.combo('side','Kierunek',{'Lewy':'left','Prawy':'right'})
        self.combo('fronts','Fronty',{'Otwarte':'open','Dwa niezależne':'orthogonal','Skośny bez okucia':'diagonal'})
        for key,label in [('width','Szerokość X'),('depth','Głębokość Y'),('height','Wysokość całkowita'),('arm_depth','Ramię X — głębokość'),('arm_depth_y','Ramię Y — szerokość'),('radius','Promień / długość ścięcia'),('diagonal_angle','Kąt ścięcia [°]'),('shelves','Liczba półek'),('thickness','Grubość korpusu'),('front_thickness','Grubość frontu'),('leg_height','Wysokość nóg')]:self.field(key,label)
        self.combo('top_mode','Góra',{'Pełna płyta':'panel','Listwy pod wspólny blat':'rails'})
        self.combo('front_style','Styl frontu',{'Gładki':'smooth','Ramka':'frame','Ryflowany':'fluted','Chwyt frezowany':'finger'})
        self.bind('<Destroy>',self.destroyed,add='+');self.set_config(default_corner_config(app.project));self.refresh()

    def field(self,key,label):
        row=ttk.Frame(self.body);row.pack(fill='x',pady=1);ttk.Label(row,text=label).pack(side='left')
        var=tk.StringVar();entry=ttk.Entry(row,textvariable=var,width=9);entry.pack(side='right');self.fields[key]=var
        var.trace_add('write',lambda *a:self.changed());entry.bind('<Return>',lambda e:self.apply());entry.bind('<FocusOut>',lambda e:self.changed())

    def combo(self,key,label,choices):
        row=ttk.Frame(self.body);row.pack(fill='x',pady=2);ttk.Label(row,text=label).pack(side='left')
        var=tk.StringVar();box=ttk.Combobox(row,textvariable=var,values=list(choices),state='readonly',width=19);box.pack(side='right');self.fields[key]=var;self.mapping[key]=choices
        box.bind('<<ComboboxSelected>>',lambda e:self.changed())

    def set_config(self,config):
        self.syncing=True;self.config=copy.deepcopy(config);self.has_draft=False
        try:
            for key,var in self.fields.items():
                value=config[key]
                if key in self.mapping:value=next(k for k,v in self.mapping[key].items() if v==value)
                elif isinstance(value,(float,int)):value=f'{value:g}'
                var.set(str(value))
        finally:self.syncing=False
        self.draw_preview()

    def draft(self):
        config=copy.deepcopy(self.config)
        for key,var in self.fields.items():
            text=var.get().strip()
            if key in self.mapping:config[key]=self.mapping[key][text]
            else:
                try:config[key]=float(text.replace(',','.'))
                except ValueError:raise ValueError('Wpisz liczbę: '+key) from None
        return config

    def cancel_pending(self):
        if self.job:
            try:self.after_cancel(self.job)
            except tk.TclError:pass
        self.job=None

    def destroyed(self,event):
        if event.widget is self:self.cancel_pending()

    def changed(self):
        if self.syncing:return
        self.has_draft=bool(self.group)
        self.cancel_pending();self.draw_preview()
        if self.group:self.job=self.after(450,self.apply)

    def quick_shape(self,shape):
        self.config['shape']=shape
        fronts='orthogonal' if shape=='L' else 'diagonal' if shape=='chamfer' else 'open'
        self.fields['fronts'].set(next(k for k,v in self.mapping['fronts'].items() if v==fronts));self.changed()

    def draw_preview(self):
        if not hasattr(self,'preview'):return
        self.preview.delete('all')
        try:
            c=self.draft();points=corner_outline(c['width'],c['depth'],c['arm_depth'],c['shape'],c['radius'],c['side'],12,c['arm_depth_y'],c['diagonal_angle'])
            w=max(100,self.preview.winfo_width());scale=min((w-18)/c['width'],86/c['depth']);coords=[value for x,y in points for value in (9+x*scale,9+y*scale)]
            self.preview.create_polygon(coords,fill='#8ebcc2',outline='#176a78',width=2)
        except (ValueError,KeyError,ZeroDivisionError):pass

    def refresh(self):
        from furniture_configurator import read_config
        part=self.app.selected_part();group=part.group if part else None;cfg=read_corner_config(self.app.project,group) if group else None
        regular=read_config(self.app.project,group) if group and not cfg else None
        marker=(self.app.project_generation,group,repr(cfg),repr(regular))
        if marker==self.signature:return
        self.cancel_pending();self.signature=marker;self.generation=self.app.project_generation;self.group=group if cfg else None
        if cfg:self.set_config(cfg);self.note.set(corner_status(self.app.project,group)['reason'])
        else:
            if regular:
                arm=regular['depth'];suggested=default_corner_config(self.app.project,'wardrobe' if regular['height']>=1500 else 'cabinet')
                suggested.update(width=max(regular['width'],arm+340),depth=arm+340,height=regular['height']-(regular['worktop_thickness'] if regular.get('worktop') else 0),arm_depth=arm,arm_depth_y=arm,fronts='orthogonal')
                for key in ('leg_height','front_thickness','thickness','material','front_material'):suggested[key]=regular[key]
                self.set_config(suggested)
            self.note.set('Konwersja użyje widocznych wymiarów narożnika. Ręczne obróbki i okucia są chronione.')
        self.apply_button.configure(state='normal' if cfg and corner_status(self.app.project,group)['editable'] else 'disabled')

    def commit(self,result,group):
        if result.data()==self.app.project.data():return True
        oldselected=self.app.selected;self.app.snapshot();self.app.project=result
        self.app.selected=oldselected if any(p.id==oldselected for p in result.parts) else next(p.id for p in result.parts if p.group==group)
        self.app.open_amounts={}
        if hasattr(self.app,'motion_value'):self.app.motion_value.set(0)
        self.app.refresh();self.refresh();self.app.status.set('Przeliczono narożnik: '+group);return True

    def apply(self):
        self.cancel_pending()
        if self.syncing or not self.group:return False
        try:
            if self.generation!=self.app.project_generation:raise ValueError('Projekt zmienił się. Wybierz narożnik ponownie.')
            c=normalize_corner_config(self.draft(),self.app.project);previous=read_corner_config(self.app.project,self.group)
            if c==normalize_corner_config(previous,self.app.project):self.has_draft=False;return True
            result,group=create_corner(self.app.project,c,self.group);return self.commit(result,group)
        except (ValueError,KeyError,StopIteration) as e:self.note.set(str(e));return False

    def flush_draft(self):
        self.cancel_pending()
        return self.apply() if self.has_draft and self.group else True

    def convert_selected(self):
        self.cancel_pending();part=self.app.selected_part()
        if not part:self.note.set('Wybierz szafkę albo szafę.');return False
        try:
            c=self.draft()
            options={key:c[key] for key in self.fields if key not in ('side','fronts','width','depth','arm_depth')}
            options['shape']=c['shape'];options['material']=c['material'];options['front_material']=c['front_material']
            result,group=convert_to_corner(self.app.project,part.group,shape=options.pop('shape'),side=c['side'],fronts=c['fronts'],width=c['width'],depth=c['depth'],arm_depth=c['arm_depth'],**options)
            return self.commit(result,group)
        except (ValueError,KeyError,StopIteration) as e:self.note.set(str(e));return False

    def new_corner(self):
        self.cancel_pending()
        try:
            c=self.draft();c['name']='Szafa narożna' if c['furniture_type']=='wardrobe' else 'Korpus narożny';c['origin']=[0,0,0]
            result,group=create_corner(self.app.project,c)
            if self.app.project.parts:
                from furniture_layout import group_bounds,move_group
                right=max(group_bounds(self.app.project,g)[1][0] for g in {p.group for p in self.app.project.parts});lo,_=group_bounds(result,group);result=move_group(result,group,right+50-lo[0])
            return self.commit(result,group)
        except (ValueError,KeyError,StopIteration) as e:self.note.set(str(e));return False
