"""Dockable sourced planning calculators and handle alternatives."""
import tkinter as tk
from tkinter import ttk,filedialog
import webbrowser
from pathlib import Path
from dock_workspace import DockDocument
from studio_ui import selected,safe,commit

def _scroll_form(parent,width=330):
    holder=ttk.Frame(parent,width=width)
    viewport=tk.Canvas(holder,width=width,highlightthickness=0,bg='#f6f8fa')
    scrollbar=ttk.Scrollbar(holder,orient='vertical',command=viewport.yview)
    scrollbar.pack(side='right',fill='y');viewport.pack(side='left',fill='both',expand=True)
    viewport.configure(yscrollcommand=scrollbar.set)
    form=ttk.Frame(viewport)
    item=viewport.create_window(0,0,window=form,anchor='nw')
    form.bind('<Configure>',lambda e:viewport.configure(scrollregion=viewport.bbox('all')))
    viewport.bind('<Configure>',lambda e:viewport.itemconfigure(item,width=e.width))
    def wheel(event):
        viewport.yview_scroll(-1 if event.delta>0 else 1,'units');return 'break'
    viewport.bind('<MouseWheel>',wheel)
    def bind_children():
        def visit(widget):
            widget.bind('<MouseWheel>',wheel,add='+')
            for child in widget.winfo_children():visit(child)
        visit(form)
    return holder,form,viewport,bind_children

class RunnerDialog(DockDocument):
    def __init__(self,app):
        super().__init__(app);self.app=app;self.part_id=app.selected;self.title('Prowadnice — katalog i kalkulator');self.geometry('990x730')
        from runners_catalog import PROFILES
        self.profiles=PROFILES;self.choice=tk.StringVar(value=next(iter(PROFILES.values()))['label']);self.vars={};self.summary=tk.StringVar();self.note=tk.StringVar()
        content=ttk.Frame(self,padding=16);content.pack(fill='both',expand=True)
        ttk.Label(content,text='Seria i wariant prowadnicy',style='Title.TLabel').pack(anchor='w')
        ttk.Combobox(content,textvariable=self.choice,values=[p['label'] for p in PROFILES.values()],state='readonly',width=90).pack(fill='x',pady=8)
        # Reserve the actions before assigning the remaining height to forms.
        # A docked page is shorter than the old 730 px floating dialog.
        footer=ttk.Frame(content);footer.pack(side='bottom',fill='x')
        ttk.Label(footer,text='Skrzynka drewniana z czterema ściankami. Zastosowanie pobiera światło i głębokość z korpusu. Zaczepy, rowki i mocowania zależą od wybranego wariantu.',wraplength=1160,padding=(0,8)).pack(fill='x')
        actions=ttk.Frame(footer);actions.pack(fill='x',pady=(2,0))
        ttk.Button(actions,text='Dokumentacja wybranego wariantu',command=self.source).pack(side='left',padx=(0,10))
        ttk.Button(actions,text='Zastosuj do zaznaczonego frontu szuflady',command=safe(app,self.apply),style='Accent.TButton').pack(side='right',fill='x',expand=True)
        columns=ttk.Frame(content);columns.pack(fill='both',expand=True,pady=(0,6))
        form_holder,form,self.form_viewport,bind_form_wheel=_scroll_form(columns)
        form_holder.pack(side='left',fill='y',padx=(0,15))
        info=ttk.Notebook(columns);info.pack(side='left',fill='both',expand=True)
        dimensions=ttk.Frame(info);notes=ttk.Frame(info)
        info.add(dimensions,text='Wymiary planowania');info.add(notes,text='Uwagi i warunki montażu')
        self.result_tabs=info
        width,depth=564,560
        p=app.selected_part()
        if p:
            try:
                from assembly import frame
                from runner_installation import drawer_depth_context
                f=frame(app.project,p.group);width=f['inner_right']-f['inner_left'];depth=drawer_depth_context(app.project,p,{'drawer_front_gap':0})['usable_depth']
            except ValueError:pass
        for k,label,val in [('inside','Światło między bokami korpusu [mm]',width),('cabdepth','Użyteczna głębokość korpusu [mm]',depth),('side','Grubość boku skrzynki [mm]',16),('bottom','Grubość dna [mm]',6),('recess','Cofnięcie dna od dołu boku [mm]',12)]:self.vars[k]=app.field(form,label,val)
        ttk.Label(form,text='Długość nominalna prowadnicy [mm]').pack(anchor='w',pady=(8,2));self.nominal=tk.StringVar(value='400');self.length_combo=ttk.Combobox(form,textvariable=self.nominal,state='readonly');self.length_combo.pack(fill='x')
        ttk.Label(form,text='Potwierdzona min. głębokość [mm]\nWypełnij tylko przy braku danych w karcie',wraplength=300).pack(anchor='w',pady=(8,2))
        self.confirmed=tk.StringVar(value='');self.confirmed_entry=ttk.Entry(form,textvariable=self.confirmed);self.confirmed_entry.pack(fill='x')
        ttk.Label(dimensions,textvariable=self.summary,font=('Segoe UI',11),wraplength=650,padding=10).pack(fill='x')
        details=ttk.Frame(notes,padding=8);details.pack(fill='both',expand=True)
        note_text=tk.Text(details,wrap='word',height=5,font=('Segoe UI',10),relief='flat',bg='#f6f8fa');note_text.pack(side='left',fill='both',expand=True)
        scrollbar=ttk.Scrollbar(details,command=note_text.yview);scrollbar.pack(side='right',fill='y');note_text.configure(yscrollcommand=scrollbar.set,state='disabled')
        def write_note(*_):
            note_text.configure(state='normal');note_text.delete('1.0','end');note_text.insert('1.0',self.note.get());note_text.configure(state='disabled')
        self.note.trace_add('write',write_note)
        self.choice.trace_add('write',self.choose)
        for var in list(self.vars.values())+[self.nominal,self.confirmed]:var.trace_add('write',self.calculate)
        self.choose()
        self.after_idle(bind_form_wheel)
    def profile(self):
        from runners_catalog import get_profile
        return get_profile(next(k for k,p in self.profiles.items() if p['label']==self.choice.get()))
    def choose(self,*_):
        spec=self.profile();lengths=spec['lengths'];self.length_combo.configure(values=lengths)
        self.confirmed_entry.configure(state='normal' if spec['depth_status']=='unverified' else 'disabled')
        self.confirmed.set('')
        if self.nominal.get() not in [str(x) for x in lengths]:self.nominal.set(str(400 if 400 in lengths else lengths[0]))
        self.calculate()
    def calculate(self,*_):
        from runners_catalog import calculate_runner
        try:
            v={k:self.app.number(x) for k,x in self.vars.items()};profile=self.profile();plan=calculate_runner(profile['id'],v['inside'],self.app.number(self.nominal),v['side'],v['cabdepth'],construction='four_sided',drawer_front_thickness=v['side'],confirmed_min_depth=self.confirmed_depth())
            self.last_plan=plan
            def fmt(key):
                value=plan.get(key);return f'{value:g} mm' if isinstance(value,(int,float)) else 'Brak danych dla wariantu'
            details='\n'.join(label+': '+fmt(k) for k,label in [('drawer_inner_width','Wnętrze skrzynki'),('drawer_outer_width','Skrzynka z zewnątrz'),('external_clearance_per_side','Luz na jedną stronę'),('drawer_length','Długość skrzynki'),('min_cabinet_depth','Minimalna głębokość korpusu'),('bottom_recess_min','Cofnięcie dna min.')])
            low,high=plan['drawer_inner_width_range'];details+=f'\nDopuszczalne wnętrze: {low:g}–{high:g} mm'
            if plan.get('article'):details+='\nArtykuł: '+plan['article']
            if plan['length_status']=='workshop':details+='\nDługość skrzynki: ustawienie warsztatowe'
            if plan['depth_status']=='user_confirmed':details+='\nGłębokość: potwierdzona przez użytkownika'
            self.summary.set(details)
            prefix='Wymiary obrysu mieszczą się w podanej przestrzeni.\n' if plan['valid'] and plan['min_cabinet_depth'] is not None else 'Obrys pasuje; wpisz potwierdzoną głębokość montażową przed zastosowaniem.\n' if plan['valid'] else '\n'.join(plan['errors'])+'\n'
            self.note.set(prefix+plan['status']+'\n'+'\n'.join(plan['warnings']))
        except (ValueError,KeyError):self.last_plan=None;self.summary.set('Wpisz poprawne wymiary.');self.note.set('')
    def confirmed_depth(self):return self.app.number(self.confirmed) if self.confirmed.get().strip() else None
    def source(self):
        from runners_catalog import get_profile
        profile=self.profile();source=profile.get('source_by_length',{}).get(self.app.number(self.nominal),profile['source']);webbrowser.open(source)
    def apply(self):
        from runner_installation import apply_runner
        if not self.part_id:raise ValueError('Przed otwarciem kalkulatora wybierz front szuflady.')
        commit(self.app,apply_runner(self.app.project,self.part_id,self.profile()['id'],self.app.number(self.nominal),self.app.number(self.vars['side']),self.app.number(self.vars['bottom']),self.app.number(self.vars['recess']),confirmed_min_depth=self.confirmed_depth()));self.app.selected=self.part_id;self.app.refresh()
        from runner_installation import drawer_depth_context
        from assembly import frame
        part=next(p for p in self.app.project.parts if p.id==self.part_id);f=frame(self.app.project,part.group)
        self.vars['inside'].set(str(f['inner_right']-f['inner_left']));self.vars['cabdepth'].set(str(drawer_depth_context(self.app.project,part)['usable_depth']));self.calculate()
        self.mark_clean()

class OpeningDialog(DockDocument):
    def __init__(self,app):
        super().__init__(app);self.app=app;p=selected(app);self.part_id=p.id;self.title('Otwieranie bez uchwytów — '+p.name);self.geometry('850x620')
        from opening_options import OPTIONS
        self.options=OPTIONS;self.choice=tk.StringVar(value=OPTIONS['tip_bumper']['label']);self.note=tk.StringVar();content=ttk.Frame(self,padding=18);content.pack(fill='both',expand=True)
        ttk.Combobox(content,textvariable=self.choice,values=[s['label'] for s in OPTIONS.values()],state='readonly',width=85).pack(fill='x')
        footer=ttk.Frame(content);footer.pack(side='bottom',fill='x')
        ttk.Label(footer,textvariable=self.note,wraplength=800,padding=10).pack(fill='x')
        ttk.Button(footer,text='Dokumentacja',command=self.source).pack(side='left',padx=(0,10));ttk.Button(footer,text='Zastosuj sposób otwierania',command=safe(app,self.apply),style='Accent.TButton').pack(side='right',fill='x',expand=True)
        holder,form,viewport,bind_wheel=_scroll_form(content,750);holder.pack(fill='both',expand=True,pady=8)
        self.fields={k:app.field(form,label,val) for k,label,val in [('length','Długość uchwytu / kieszeni [mm]',120),('width','Szerokość kieszeni [mm]',24),('depth','Głębokość kieszeni / wysunięcie profilu [mm]',8),('level','Poziom V / wysokość wypychacza na froncie [mm]',p.v/2),('center_u','Położenie U środka kieszeni [mm]',p.u/2),('angle','Kąt kieszeni [°]',0)]}
        self.spring_labels={'Nieznany — wymaga doboru':'unknown','Ze sprężyną zamykającą':'sprung','Bez sprężyny':'unsprung'}
        state=p.assembly.get('config',{}).get('hinge_spring','unknown');self.spring=tk.StringVar(value=next(k for k,v in self.spring_labels.items() if v==state))
        ttk.Label(form,text='Rodzaj zawiasu przy wypychaczu').pack(anchor='w',pady=(8,2));ttk.Combobox(form,textvariable=self.spring,values=list(self.spring_labels),state='readonly').pack(fill='x')
        self.edge_labels={'Górna':'top','Dolna':'bottom','Lewa':'left','Prawa':'right'};self.edge=tk.StringVar(value='Górna')
        ttk.Label(form,text='Krawędź własnego profilu uchwytu').pack(anchor='w',pady=(8,2));ttk.Combobox(form,textvariable=self.edge,values=list(self.edge_labels),state='readonly').pack(fill='x')
        self.choice.trace_add('write',self.describe);self.describe();self.after_idle(bind_wheel)
    def key(self):return next(k for k,v in self.options.items() if v['label']==self.choice.get())
    def describe(self,*_):self.note.set(self.options[self.key()]['note']+'\nU/V kieszeni zachowują proporcję po zmianie frontu. Zawias magnetycznego TIP-ON musi być jawnie bez sprężyny. Profil może leżeć na dowolnej z czterech krawędzi.')
    def source(self):
        src=self.options[self.key()]['source']
        if src:webbrowser.open(src)
    def apply(self):
        from opening_options import install
        commit(self.app,install(self.app.project,self.part_id,self.key(),**{k:self.app.number(v) for k,v in self.fields.items()},hinge_spring=self.spring_labels[self.spring.get()],profile_edge=self.edge_labels[self.edge.get()]));self.app.selected=self.part_id;self.app.refresh()
        self.mark_clean()

def material_dialog(app):
    from material_presets import PRESETS,apply_material_preset
    p=selected(app);d=DockDocument(app);d.title('Materiał i grubość — '+p.name);form=ttk.Frame(d,padding=18);form.pack(fill='both',expand=True)
    choice=tk.StringVar(value=next(iter(PRESETS)));thick=tk.StringVar(value='18');info=tk.StringVar()
    ttk.Combobox(form,textvariable=choice,values=list(PRESETS),state='readonly',width=70).pack(fill='x',pady=8);combo=ttk.Combobox(form,textvariable=thick,state='readonly');combo.pack(fill='x',pady=8);ttk.Label(form,textvariable=info,wraplength=800,padding=15).pack(fill='x')
    def choose(*_):
        spec=PRESETS[choice.get()];combo.configure(values=spec['thicknesses'])
        if thick.get() not in [str(x) for x in spec['thicknesses']]:thick.set(str(spec.get('stock_thicknesses',spec['thicknesses'])[0]))
        info.set(f'Format półfabrykatu: {spec["sheet"][0]} × {spec["sheet"][1]} mm. '+spec.get('note','Edytowalne ustawienia warsztatowe; nie są normą dla wszystkich płyt i dostawców.')+' Cena 0: wpisz cenę w Materiały i ceny. Zmiana grubości przelicza powiązane elementy i może wymagać korekty okuć.')
    choice.trace_add('write',choose);choose()
    ttk.Button(form,text='Źródło producenta',command=lambda:webbrowser.open(PRESETS[choice.get()]['source']) if PRESETS[choice.get()]['source'] else None).pack(fill='x')
    def apply():
        commit(app,apply_material_preset(app.project,p.id,choice.get(),float(thick.get())));d.mark_clean()
    ttk.Button(form,text='Zastosuj do elementu',command=safe(app,d.guard(apply)),style='Accent.TButton').pack(fill='x',pady=12);return d

def export_hardware(app):
    from hardware_report import hardware_report
    path=filedialog.asksaveasfilename(parent=app,title='Plan montażu i lista okuć',defaultextension='.html',filetypes=[('Raport HTML','*.html')])
    if path:hardware_report(app.project,path);webbrowser.open(Path(path).resolve().as_uri())

def install(app):
    from dock_workspace import VisualizationDocument
    bar=app.nametowidget(app.cget('menu'));menu=tk.Menu(bar,tearoff=False);bar.add_cascade(label='Kreator',menu=menu)
    for label,fn in [('Prowadnice producentów — kalkulator…',lambda:RunnerDialog(app)),('Fronty bez uchwytów / frezowanie…',lambda:OpeningDialog(app)),('Materiały i grubości…',lambda:material_dialog(app)),('Plan montażu i lista okuć…',lambda:export_hardware(app)),('Dodatkowa zakładka wizualizacji',lambda:VisualizationDocument(app))]:menu.add_command(label=label,command=safe(app,fn))

