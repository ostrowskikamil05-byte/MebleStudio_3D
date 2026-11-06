"""One persistent inspector and a compact ribbon for cabinet configuration."""
import copy
import tkinter as tk
from tkinter import ttk, messagebox


class ScrollPanel(ttk.Frame):
    def __init__(self,parent):
        super().__init__(parent)
        self.canvas=tk.Canvas(self,highlightthickness=0,bg='#f6f8fa',width=320)
        sb=ttk.Scrollbar(self,orient='vertical',command=self.canvas.yview)
        sb.pack(side='right',fill='y');self.canvas.pack(side='left',fill='both',expand=True)
        self.canvas.configure(yscrollcommand=sb.set)
        self.body=ttk.Frame(self.canvas,padding=(10,4))
        self.window=self.canvas.create_window(0,0,window=self.body,anchor='nw')
        self.body.bind('<Configure>',lambda e:self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.canvas.bind('<Configure>',self.resize)
        self.body.bind('<Map>',lambda e:self.install_wheel(self.body))

    def install_wheel(self,parent):
        parent.bind('<MouseWheel>',self.wheel,add='+')
        for child in parent.winfo_children():self.install_wheel(child)

    def resize(self,event):
        self.canvas.itemconfigure(self.window,width=event.width)
        def visit(widget):
            try:
                if widget.winfo_class()=='TLabel' and int(widget.cget('wraplength')):
                    widget.configure(wraplength=max(100,event.width-28))
            except (tk.TclError,ValueError):pass
            for child in widget.winfo_children():visit(child)
        visit(self.body)

    def wheel(self,event):
        self.canvas.yview_scroll(-1 if event.delta>0 else 1,'units')
        return 'break'


class Section(ttk.Frame):
    def __init__(self,parent,title,opened=False):
        super().__init__(parent);self.pack(fill='x',pady=(3,2));self.opened=opened
        self.toggle=ttk.Button(self,command=self.flip,style='Section.TButton');self.toggle.pack(fill='x')
        self.body=ttk.Frame(self,padding=(2,5));self.title=title;self.show()

    def show(self):
        self.toggle.configure(text=('▾  ' if self.opened else '▸  ')+self.title)
        if self.opened:self.body.pack(fill='x')
        else:self.body.pack_forget()

    def flip(self):self.opened=not self.opened;self.show()


class QuickInspector(ttk.Frame):
    def __init__(self,parent,app):
        super().__init__(parent);self.app=app;self.syncing=False;self.job=None;self.group=None
        self.generation=app.project_generation;self.config={};self.fields={};self.combos={};self.sections={};self.has_draft=False
        self.template_indices=[];self.saved={};self.clipboard=None
        self.note=tk.StringVar(value='Wybierz typ i dodaj mebel. Wymiary w mm.')
        self.groupvar=tk.StringVar();self.templatevar=tk.StringVar();self.presetvar=tk.StringVar()
        top=ttk.Frame(self,padding=(10,6));top.pack(fill='x')
        ttk.Label(top,text='KREATOR MEBLA',font=('Segoe UI',10,'bold'),foreground='#176a78').pack(anchor='w')
        self.groupcombo=ttk.Combobox(top,textvariable=self.groupvar,state='readonly');self.groupcombo.pack(fill='x',pady=4)
        self.groupcombo.bind('<<ComboboxSelected>>',self.choose_group)
        actions=ttk.Frame(top);actions.pack(fill='x')
        for label,fn in [('+ Nowy',self.new_furniture),('Kopia',self.duplicate_group),('Usuń',self.delete_group)]:
            ttk.Button(actions,text=label,command=fn,padding=(6,4)).pack(side='left',fill='x',expand=True,padx=1)
        bottom=ttk.Frame(self,padding=(10,6));bottom.pack(side='bottom',fill='x')
        actionrow=ttk.Frame(bottom);actionrow.pack(fill='x')
        self.apply_button=ttk.Button(actionrow,text='Zastosuj do mebla',command=lambda:self.apply(True),style='Accent.TButton');self.apply_button.pack(side='left',fill='x',expand=True)
        ttk.Button(actionrow,text='↺',width=3,command=self.reset_fields).pack(side='right',padx=(4,0))
        note_label=ttk.Label(bottom,textvariable=self.note,wraplength=295,font=('Segoe UI',9));note_label.pack(fill='x',pady=(4,0))
        note_label.bind('<Configure>',lambda e:note_label.configure(wraplength=max(100,e.width)))
        scroll=ScrollPanel(self);scroll.pack(fill='both',expand=True);self.scroll=scroll;body=scroll.body
        self.type_section=Section(body,'Typ i wymiary',True)
        self.sections['type']=self.type_section
        from furniture_library import TEMPLATES
        self.template_indices=[i for i,t in enumerate(TEMPLATES) if t[5] in ('cabinet','drawers','shelf','island')]
        names=[TEMPLATES[i][0]+' · '+TEMPLATES[i][1] for i in self.template_indices]
        self.templatecombo=ttk.Combobox(self.type_section.body,textvariable=self.templatevar,values=names,state='readonly');self.templatecombo.pack(fill='x',pady=3)
        self.templatecombo.bind('<<ComboboxSelected>>',self.change_template)
        self.field(self.type_section.body,'name','Nazwa',text=True)
        for key,label in [('width','Szerokość'),('height','Wysokość całkowita'),('depth','Głębokość'),('thickness','Grubość korpusu')]:self.field(self.type_section.body,key,label)
        ttk.Label(self.type_section.body,text='Wysokość obejmuje nóżki i blat.',font=('Segoe UI',9),foreground='#68788a').pack(anchor='w')
        # A visible shortcut strip changes geometry in one click, like the reference tool.
        quick=ttk.Frame(top);quick.pack(fill='x',pady=(5,0))
        for label,mode,count in [('Otwarty','open',0),('Drzwi','doors',1),('2 szuf.','drawers',2),('3 szuf.','drawers',3),('4 szuf.','drawers',4)]:
            ttk.Button(quick,text=label,command=lambda m=mode,n=count:self.quick_front(m,n),width=1,padding=(2,5)).pack(side='left',fill='x',expand=True)
        main=Section(body,'Podziały i wyposażenie',True);self.sections['main']=main
        self.section_fields(main.body,'main')
        for key,label in [('bottom','Strefa dolna'),('upper','Pawlacz / strefa górna')]:
            sec=Section(body,label);self.sections[key]=sec
            self.check(sec.body,key+'.enabled','Włącz strefę')
            self.field(sec.body,key+'.height','Wysokość strefy')
            self.section_fields(sec.body,key)
        fronts=Section(body,'Fronty i uchwyty');self.sections['fronts']=fronts
        self.combo(fronts.body,'front_style','Styl frontu',{'Gładki':'smooth','Ramka frezowana':'frame','Ryflowany':'fluted','Chwyt frezowany':'finger'})
        self.preview=tk.Canvas(fronts.body,height=112,bg='#edf1f4',highlightthickness=0);self.preview.pack(fill='x',pady=4)
        self.preview.bind('<Configure>',lambda e:self.draw_preview())
        self.field(fronts.body,'front_thickness','Grubość frontu');self.field(fronts.body,'gap','Szczelina frontów')
        from opening_options import OPTIONS
        openings={'Uchwyt / gałka':'handle','Bez uchwytu':'none'}
        openings.update({v['label']:k for k,v in OPTIONS.items() if k!='none'})
        self.combo(fronts.body,'handle_mode','Otwieranie / chwyt',openings)
        self.combo(fronts.body,'handle_spacing','Rozstaw uchwytu',{str(x)+' mm':x for x in (0,64,96,128,160,192,224,256,320)})
        self.combo(fronts.body,'handle_color','Kolor okucia',{'Czarny':'#242628','Stal':'#aab0b6','Złoty':'#bb9656','Biały':'#efefea'})
        for key,label in [('front_border','Szerokość ramki'),('front_depth','Głębokość frezu'),('front_radius','Promień frezu'),('flute_width','Szerokość ryfla'),('flute_spacing','Rozstaw ryfli'),('flute_count','Liczba ryfli (0 = auto)'),('finger_length','Długość chwytu'),('finger_width','Szerokość chwytu'),('finger_depth','Głębokość chwytu')]:self.field(fronts.body,key,label,integer=key=='flute_count')
        fittings=Section(body,'Zawiasy i prowadnice');self.sections['fittings']=fittings
        self.combo(fittings.body,'hinge_pattern','Wiercenie zawiasu',{'Puszka 35 / rozstaw 45':'hinge45','Puszka 35 / rozstaw 52':'hinge52','Hettich Sensys 9071205':'hinge_sensys52'})
        self.field(fittings.body,'hinge_count','Zawiasy na drzwi',integer=True)
        self.combo(fittings.body,'hinge_spring','Sprężyna',{'Nieokreślona':'unknown','Ze sprężyną':'sprung','Bez sprężyny':'unsprung'})
        self.field(fittings.body,'opening_angle','Kąt otwarcia')
        from runners_catalog import PROFILES
        self.combo(fittings.body,'runner_profile','Prowadnica',{'Uniwersalna boczna 12,7':'',**{p['label']:key for key,p in PROFILES.items()}})
        self.field(fittings.body,'runner_length','Długość prowadnicy');self.field(fittings.body,'drawer_side_thickness','Grubość boku skrzynki');self.field(fittings.body,'drawer_bottom_thickness','Grubość dna skrzynki')
        self.runner_note=tk.StringVar();ttk.Label(fittings.body,textvariable=self.runner_note,wraplength=280,font=('Segoe UI',9)).pack(fill='x',pady=4)
        legs=Section(body,'Nóżki, cokół i blat');self.sections['legs']=legs
        self.field(legs.body,'leg_height','Wysokość nóżek (0 = brak)')
        self.combo(legs.body,'leg_count','Liczba nóżek',{'4 sztuki':4,'6 sztuk':6})
        self.field(legs.body,'leg_diameter','Średnica nóżki');self.field(legs.body,'leg_setback','Odsunięcie nóżek')
        self.combo(legs.body,'leg_color','Kolor nóżek',{'Czarny':'#242628','Stal':'#aab0b6','Biały':'#efefea'})
        self.field(legs.body,'leg_price','Cena nóżki [zł / szt.]')
        self.check(legs.body,'plinth','Dodaj cokół');self.field(legs.body,'plinth_setback','Cofnięcie cokołu')
        self.check(legs.body,'worktop','Dodaj blat');self.field(legs.body,'worktop_thickness','Grubość blatu');self.field(legs.body,'worktop_overhang','Nawis blatu')
        self.combo(legs.body,'top_mode','Góra korpusu',{'Pełna płyta':'panel','Listwy wzmacniające pod wspólny blat':'rails'})
        materials=Section(body,'Materiały i plecy');self.sections['materials']=materials
        self.combo(materials.body,'material','Korpus',{});self.combo(materials.body,'front_material','Fronty',{})
        self.check(materials.body,'back','Plecy');self.field(materials.body,'back_thickness','Grubość pleców')
        led=Section(body,'LED i rowki');self.sections['led']=led
        self.check(led.body,'led','Rowek i profil pod półkami')
        self.combo(led.body,'led_mount','Umiejscowienie',{'Pod półkami':'under_shelf','W boku korpusu':'side'})
        self.field(led.body,'led_width','Szerokość rowka');self.field(led.body,'led_depth','Głębokość rowka');self.field(led.body,'led_margin','Cofnięcie od frontu')
        self.check(led.body,'led_diffuser','Dyfuzor profilu')
        self.field(led.body,'led_price_per_m','Cena taśmy [zł / m]')
        presets=Section(body,'Presety i kopiowanie ustawień');self.sections['presets']=presets
        self.presetcombo=ttk.Combobox(presets.body,textvariable=self.presetvar);self.presetcombo.pack(fill='x',pady=4)
        row=ttk.Frame(presets.body);row.pack(fill='x')
        for label,fn in [('Zapisz',self.save_preset),('Wczytaj',self.load_preset),('Usuń',self.delete_preset)]:ttk.Button(row,text=label,command=fn,padding=4).pack(side='left',expand=True,fill='x')
        row=ttk.Frame(presets.body);row.pack(fill='x',pady=4)
        ttk.Button(row,text='Kopiuj ustawienia',command=self.copy_settings,padding=4).pack(side='left',fill='x',expand=True)
        ttk.Button(row,text='Wklej do mebla',command=self.paste_settings,padding=4).pack(side='left',fill='x',expand=True)
        for var,kind in self.fields.values():var.trace_add('write',lambda *_:self.draft_changed())
        self.reload_presets();scroll.install_wheel(body)

    def draft_changed(self):
        if not self.syncing:self.has_draft=True

    def reset_fields(self):
        self.cancel_pending();self.set_config(self.config);self.note.set('Przywrócono pola do ostatnio zatwierdzonych wartości.')

    def flush_draft(self):
        from furniture_configurator import read_config
        if self.has_draft and self.group and read_config(self.app.project,self.group):return self.apply(True)
        self.cancel_pending();return True

    def field(self,parent,key,label,integer=False,text=False):
        from responsive_ui import FieldRow
        var=tk.StringVar();self.fields[key]=(var,'text' if text else 'int' if integer else 'number')
        row=FieldRow(parent,label,textvariable=var,width=22 if text else 10);row.pack(fill='x',pady=2);entry=row.entry
        entry.bind('<Return>',lambda e:self.apply(True));entry.bind('<FocusOut>',lambda e:self.schedule())
        entry.bind('<Escape>',lambda e:self.reset_fields())
        return var

    def combo(self,parent,key,label,choices):
        ttk.Label(parent,text=label,font=('Segoe UI',9)).pack(anchor='w',pady=(4,1))
        var=tk.StringVar();widget=ttk.Combobox(parent,textvariable=var,values=list(choices),state='readonly');widget.pack(fill='x')
        self.fields[key]=(var,'choice');self.combos[key]=(widget,dict(choices))
        widget.bind('<<ComboboxSelected>>',lambda e:self.choice_changed(key));return widget

    def choice_changed(self,key):
        if key.endswith('.front') and self.combos[key][1].get(self.fields[key][0].get())=='drawers':
            self.fields[key.split('.')[0]+'.shelves'][0].set('0')
        self.apply(False)

    def check(self,parent,key,label):
        var=tk.BooleanVar();self.fields[key]=(var,'bool')
        ttk.Checkbutton(parent,text=label,variable=var,command=lambda:self.apply(False)).pack(anchor='w',pady=3)

    def section_fields(self,parent,key):
        self.field(parent,key+'.columns','Komory pionowe',integer=True)
        self.field(parent,key+'.shelves','Półki na komorę',integer=True)
        self.combo(parent,key+'.front','Wypełnienie',{'Otwarte':'open','Drzwi':'doors','Szuflady':'drawers'})
        self.combo(parent,key+'.doors','Drzwi na komorę',{'1 skrzydło':1,'2 skrzydła':2})
        self.field(parent,key+'.drawers','Szuflady na komorę',integer=True)
        self.combo(parent,key+'.front_style','Styl tej strefy',{'Wspólny styl frontów':None,'Gładki':'smooth','Ramka frezowana':'frame','Ryflowany':'fluted','Chwyt frezowany':'finger'})

    def cancel_pending(self):
        if self.job:
            self.after_cancel(self.job);self.job=None

    def schedule(self):
        if self.syncing or not self.group:return
        self.cancel_pending();generation=self.app.project_generation;group=self.group
        self.job=self.after(250,lambda:self.apply(False) if generation==self.app.project_generation and group==self.group else None)

    def choose_group(self,event=None):
        group=self.groupvar.get()
        if not self.flush_draft():self.groupvar.set(self.group);return
        self.cancel_pending()
        p=next((p for p in self.app.project.parts if p.group==group),None)
        if p:
            self.app.selected=p.id;self.app.tree.selection_set(p.id)
            self.app.motion_value.set(self.app.open_amounts.get(p.id,0)*100)
            self.app.load_form();self.app.draw()

    def change_template(self,event=None):
        from furniture_configurator import default_config
        i=self.template_indices[self.templatecombo.current()]
        defaults=default_config(i,self.app.project)
        if self.group:
            # Explicit type choice retains dimensions, materials and layout.
            current=self.draft()
            for key in ('name','width','height','depth','thickness','front_thickness','material','front_material','leg_height','leg_count','leg_diameter','leg_setback','plinth','worktop','worktop_thickness','worktop_overhang'):
                if key in current:defaults[key]=current[key]
        self.set_config(defaults);self.apply(False)

    def draft(self):
        cfg=copy.deepcopy(self.config);sections={s['key']:s for s in cfg.get('sections',[])}
        for key,(var,kind) in self.fields.items():
            raw=var.get()
            if kind=='bool':value=bool(raw)
            elif kind=='choice':
                choices=self.combos[key][1]
                if raw not in choices:raise ValueError('Wybierz ustawienie: '+key)
                value=choices[raw]
            elif kind=='text':value=raw.strip()
            else:
                try:value=float(raw.replace(',','.'))
                except (ValueError,AttributeError):raise ValueError('Wpisz liczbę w polu '+key)
                if kind=='int':
                    if int(value)!=value:raise ValueError('Wpisz całkowitą liczbę: '+key)
                    value=int(value)
            if '.' in key:
                section,field=key.split('.',1)
                if field=='front_style' and value is None:sections[section].pop(field,None)
                else:sections[section][field]=value
            else:cfg[key]=value
        cfg['sections']=list(sections.values());return cfg

    def set_config(self,cfg):
        self.syncing=True;self.config=copy.deepcopy(cfg)
        sections={s['key']:s for s in cfg.get('sections',[])}
        defaults={'front_border':60,'front_depth':4,'flute_spacing':24,'hinge_spring':'unknown','leg_setback':55,'worktop':False,'worktop_thickness':38,'worktop_overhang':20,'led':False,'led_width':15,'led_depth':5,'led_margin':50,'led_diffuser':True,'handle_color':'#242628'}
        for key,(var,kind) in self.fields.items():
            value=sections.get(key.split('.')[0],{}).get(key.split('.')[1],0) if '.' in key else cfg.get(key,defaults.get(key,''))
            if kind=='choice':
                choices=self.combos[key][1];label=next((label for label,item in choices.items() if item==value),None)
                if label is None and key in ('material','front_material'):
                    label=next(iter(choices),'')
                if label is None:label=next(iter(choices),'')
                var.set(label)
            elif kind=='bool':var.set(bool(value))
            else:var.set(f'{value:g}' if isinstance(value,(int,float)) else str(value))
        idx=cfg.get('template_index',0)
        if idx in self.template_indices:self.templatecombo.current(self.template_indices.index(idx))
        self.syncing=False;self.has_draft=False;self.draw_preview()

    def refresh(self):
        from furniture_configurator import read_config,default_config,configuration_status
        self.cancel_pending()
        if self.generation!=self.app.project_generation:self.clipboard=None
        self.generation=self.app.project_generation
        groups=list(dict.fromkeys(p.group for p in self.app.project.parts));self.groupcombo.configure(values=groups)
        p=self.app.selected_part();group=p.group if p else None
        self.group=group;self.groupvar.set(group or 'Nowy mebel — wybierz typ')
        materials={m.name:m.id for m in self.app.project.materials}
        for key in ('material','front_material'):
            widget,_=self.combos[key];self.combos[key]=(widget,materials);widget.configure(values=list(materials))
        cfg=read_config(self.app.project,group) if group else None
        if cfg:
            self.set_config(cfg);status=configuration_status(self.app.project,group)
            self.note.set('Zmiana listy: od razu. Liczby: Enter lub wyjście z pola.' if status.get('editable',False) else status.get('reason','Mebel zmieniono ręcznie.'))
            self.apply_button.configure(text='Zastosuj do mebla',state='normal' if status.get('editable',False) else 'disabled')
        elif group:
            from corner_furniture import read_corner_config
            from workspace_worktops import read_shared_worktop
            corner=read_corner_config(self.app.project,group)
            shared=read_shared_worktop(self.app.project,group) if corner is None else None
            draft=default_config(0,self.app.project);draft['name']=group
            if corner:
                # Keep this regular-cabinet inspector informational for a
                # corner, but display the actual selected module parameters.
                for key in ('width','depth','height','thickness','material','front_material','front_thickness','leg_height','leg_count','leg_diameter','leg_setback','leg_color','leg_price','top_mode','rail_width','gap','front_style','handle_mode','handle_spacing','hinge_pattern','hinge_count','opening_angle'):
                    if key in corner:draft[key]=copy.deepcopy(corner[key])
                draft['sections'][1].update(shelves=corner.get('shelves',0),front='open' if corner.get('fronts','open')=='open' else 'doors',doors=2 if corner.get('fronts')=='orthogonal' else 1)
            elif shared:
                top=next(q for q in self.app.project.parts if q.group==group and q.assembly.get('shared_worktop'))
                draft.update(width=top.u,depth=top.v,height=top.t,thickness=top.t,front_thickness=top.t,material=top.material,front_material=top.material,leg_height=0,back=False)
                draft['sections'][1].update(shelves=0,front='open')
            self.set_config(draft)
            if corner:
                self.templatevar.set('Narożnik · '+('szafa' if corner.get('furniture_type')=='wardrobe' else 'szafka'))
                self.note.set('Narożnik: aktualne wymiary całego mebla. Edytuj ramiona, kierunek, obrys i fronty w zakładce Narożnik.')
                self.apply_button.configure(text='Edytuj w zakładce Narożnik',state='disabled')
            elif shared:
                self.templatevar.set('Wspólny blat')
                self.note.set(f'Wspólny blat: wymiary formatki, wysokość oznacza grubość. Poziom blatu {top.z:g} mm. PPM → Dopasuj blat lub panel Elementy / CAD.')
                self.apply_button.configure(text='Edytuj przez menu PPM',state='disabled')
            else:
                self.note.set('Mebel z wcześniejszej biblioteki. Przycisk spróbuje przejąć go do kreatora; szkice pozostają w panelu Elementy / CAD.')
                self.apply_button.configure(text='Przejmij mebel do kreatora',state='normal')
        else:
            self.set_config(default_config(0,self.app.project));self.note.set('Wybierz typ, ustaw wymiary i kliknij + Nowy.');self.apply_button.configure(text='Utwórz mebel',state='normal')
        summaries=[h.get('dimensions') for h in self.app.project.hardware if p and h.get('owner_id') in {q.id for q in self.app.project.parts if q.group==group} and h.get('kind')=='runner' and h.get('dimensions')]
        if summaries:
            d=summaries[0];self.runner_note.set('Skrzynka zewn. '+str(d.get('drawer_outer_width','—'))+' mm · wewn. '+str(d.get('drawer_inner_width','—'))+' mm\nLuz na stronę '+str(d.get('external_clearance_per_side','—'))+' mm')
        else:self.runner_note.set('Wymiary skrzynki i luz prowadnic przeliczają się po zmianie mebla.')

    def apply(self,explicit=False):
        if self.syncing:return False
        self.cancel_pending()
        from furniture_configurator import configure,read_config,infer_config,normalize_config
        try:
            if self.generation!=self.app.project_generation:raise ValueError('Projekt zmienił się. Wybierz mebel ponownie.')
            if self.group and read_config(self.app.project,self.group) is None:
                if not explicit:return False
                cfg=infer_config(self.app.project,self.group)
                self.set_config(cfg)
            cfg=self.draft()
            previous=read_config(self.app.project,self.group) if self.group else None
            if previous and normalize_config(cfg,self.app.project)==normalize_config(previous,self.app.project):
                self.has_draft=False;return True
            result,group=configure(self.app.project,cfg,self.group)
            if result.data()==self.app.project.data():return True
            self.app.snapshot();self.app.project=result;self.app.open_amounts={};self.app.motion_value.set(0)
            self.app.selected=next(p.id for p in result.parts if p.group==group)
            self.app.refresh();self.app.status.set('Przeliczono '+group+' · model, formatki i okucia zsynchronizowane')
            return True
        except (ValueError,KeyError,IndexError,StopIteration) as e:
            self.note.set(str(e));self.app.status.set('Kreator: '+str(e))
            if explicit:messagebox.showerror('Kreator mebla',str(e),parent=self.app)
            return False

    def new_furniture(self):
        from furniture_configurator import configure
        try:
            cfg=self.draft();result,group=configure(self.app.project,cfg)
            # Place a newly generated module next to the current set.
            if self.app.project.parts:
                from furniture_layout import group_bounds,move_group
                groups=list(dict.fromkeys(p.group for p in self.app.project.parts))
                right=max(group_bounds(self.app.project,g)[1][0] for g in groups)
                lo,_=group_bounds(result,group);result=move_group(result,group,right+50-lo[0])
            self.app.snapshot();self.app.project=result;self.app.selected=next(p.id for p in result.parts if p.group==group)
            self.app.refresh();self.app.fit();return group
        except (ValueError,KeyError,IndexError) as e:self.note.set(str(e));messagebox.showerror('Nowy mebel',str(e),parent=self.app)

    def quick_front(self,mode,count):
        self.fields['main.front'][0].set(next(k for k,v in self.combos['main.front'][1].items() if v==mode))
        if count:self.fields['main.drawers'][0].set(str(count))
        if mode=='drawers':self.fields['main.shelves'][0].set('0')
        return self.apply(False)

    def duplicate_group(self):
        if not self.group:return
        from productivity import extract_group,insert_template
        from furniture_layout import group_bounds
        try:
            lo,hi=group_bounds(self.app.project,self.group);name=self.group+' — kopia';n=2
            occupied={p.group for p in self.app.project.parts}
            while name in occupied:name=self.group+f' — kopia {n}';n+=1
            result=insert_template(self.app.project,extract_group(self.app.project,self.group),name,(hi[0]-lo[0]+50,0,0))
            self.app.snapshot();self.app.project=result;self.app.selected=next(p.id for p in result.parts if p.group==name);self.app.refresh();self.app.fit()
        except ValueError as e:self.note.set(str(e))

    def delete_group(self):
        if not self.group:return
        ids={p.id for p in self.app.project.parts if p.group==self.group}
        self.app.snapshot();self.app.project.parts=[p for p in self.app.project.parts if p.id not in ids]
        self.app.project.hardware=[h for h in self.app.project.hardware if h.get('owner_id') not in ids and h.get('group')!=self.group]
        self.app.selected=None;self.app.refresh();self.app.fit()

    def copy_settings(self):
        try:self.clipboard=self.draft();self.note.set('Skopiowano ustawienia. Wybierz inny mebel i wklej.')
        except ValueError as e:self.note.set(str(e))

    def paste_settings(self):
        if not self.clipboard:self.note.set('Najpierw skopiuj ustawienia mebla.');return
        cfg=copy.deepcopy(self.clipboard);cfg['name']=self.group or cfg['name'];self.set_config(cfg);self.apply(True)

    def reload_presets(self):
        from configurator_presets import load_presets
        try:self.saved=load_presets()
        except (OSError,ValueError) as e:self.saved={};self.note.set(str(e))
        self.presetcombo.configure(values=list(self.saved))

    def save_preset(self):
        from configurator_presets import save_preset
        try:
            cfg=self.draft();cfg['_material_names']={m.id:m.name for m in self.app.project.materials}
            save_preset(self.presetvar.get(),cfg);self.reload_presets();self.note.set('Zapisano własny preset lokalnie.')
        except (ValueError,OSError) as e:self.note.set(str(e))

    def load_preset(self):
        if self.presetvar.get() not in self.saved:self.note.set('Wybierz zapisany preset z listy.');return
        cfg=copy.deepcopy(self.saved[self.presetvar.get()]);cfg['name']=self.group or cfg['name']
        names=cfg.pop('_material_names',{});available={m.name:m.id for m in self.app.project.materials}
        for key in ('material','front_material'):
            if names.get(cfg.get(key)) in available:cfg[key]=available[names[cfg[key]]]
        self.set_config(cfg);self.apply(True)

    def delete_preset(self):
        from configurator_presets import delete_preset
        try:delete_preset(self.presetvar.get());self.reload_presets();self.presetvar.set('')
        except (ValueError,OSError) as e:self.note.set(str(e))

    def draw_preview(self):
        c=self.preview;c.delete('all');w=max(280,c.winfo_width())
        style=self.combos['front_style'][1].get(self.fields['front_style'][0].get(),'smooth')
        c.create_rectangle(20,10,100,102,fill='#c7b28c',outline='#807151',width=2)
        if style=='frame':c.create_rectangle(30,21,90,91,fill='#b49c77',outline='#8c795a',width=2)
        elif style=='fluted':
            for x in range(26,99,7):c.create_line(x,13,x,99,fill='#8c795a',width=2)
        elif style=='finger':c.create_rectangle(35,14,85,22,fill='#5e5341',outline='#5e5341')
        c.create_text(117,16,text='Podgląd frontu',anchor='w',fill='#243144',font=('Segoe UI',9,'bold'))
        profile=[(119,72),(w-18,72),(w-18,86),(119,86)]
        if style=='frame':profile=[(119,72),(143,72),(143,78),(w-43,78),(w-43,72),(w-18,72),(w-18,86),(119,86)]
        elif style=='finger':profile=[(119,72),(135,72),(135,80),(153,80),(153,72),(w-18,72),(w-18,86),(119,86)]
        c.create_polygon(*[v for p in profile for v in p],fill='#c7b28c',outline='#807151')
        c.create_text(117,99,text='Schemat przekroju · frez od frontu',anchor='w',fill='#647486',font=('Segoe UI',8))


def _button(parent,label,command,accent=False):
    button=ttk.Button(parent,text=label,command=command,width=max(3,len(label)),style='Accent.TButton' if accent else 'Ribbon.TButton',padding=(7,4))
    button.pack(side='left',padx=2,pady=3);return button


def build(app):
    from advanced_ui import install_menu
    from studio_ui import install
    from sketch import Sketch
    from responsive_ui import WrapToolbar, RibbonPage, FieldRow
    install_menu(app);install(app)
    style=ttk.Style(app)
    style.configure('TNotebook.Tab',padding=(12,6),font=('Segoe UI',9,'bold'))
    style.configure('Ribbon.TButton',padding=(7,4));style.configure('Section.TButton',anchor='w',padding=(6,5),font=('Segoe UI',9,'bold'))
    style.configure('Inspector.TNotebook.Tab',padding=(10,5),font=('Segoe UI',9,'bold'))
    style.configure('Header.TFrame',background='#172332')
    head=WrapToolbar(app,style='Header.TFrame');head.pack(fill='x')
    tk.Label(head,text='MEBEL STUDIO 3D',fg='white',bg='#172332',font=('Segoe UI',12,'bold')).pack(side='left',padx=14,pady=7)
    tk.Label(head,text='0.15 · CAD wbudowany',fg='#93adbf',bg='#172332',font=('Segoe UI',9)).pack(side='right',padx=14)
    for label,fn in [('Nowy',app.new),('Otwórz',app.open),('Zapisz',app.save),('Cofnij',app.undo),('Ponów',app.redo)]:_button(head,label,fn)
    def interface_settings():
        from ui_settings import open_settings
        return open_settings(app)
    _button(head,'⚙',interface_settings);app.header=head
    workspacepane=ttk.Panedwindow(app,orient='vertical');workspacepane.pack(fill='both',expand=True,padx=8,pady=(3,4));app.workspace_pane=workspacepane
    ribbon=ttk.Notebook(workspacepane,style='Inspector.TNotebook');workspacepane.add(ribbon,weight=0);app.ribbon=ribbon
    pages={}
    for name in ('Mebel','CAD','Układ','Scena','Produkcja','Widok','Ustawienia'):
        page=RibbonPage(ribbon);ribbon.add(page,text=name);pages[name]=page.body
    pages['Fronty i okucia']=pages['Mebel'];pages['Szkic / CAD']=pages['CAD']
    app.ribbon_pages=pages
    app.responsive_toolbars=list(dict.fromkeys(pages.values()))
    app.tabs=ttk.Notebook(workspacepane);workspacepane.add(app.tabs,weight=1)
    design=tk.Frame(app,bg='#f6f8fa');app.design_page=design;app.tabs.add(design,text='Projekt 3D')
    pane=ttk.Panedwindow(design,orient='horizontal');pane.pack(fill='both',expand=True)
    left=ttk.Frame(pane,width=330);middle=ttk.Frame(pane);pane.add(left,weight=0);pane.add(middle,weight=1)
    app.main_pane=pane;app.inspector_frame=left;app.view_frame=middle
    lefttabs=ttk.Notebook(left,style='Inspector.TNotebook');lefttabs.pack(fill='both',expand=True);app.inspector_tabs=lefttabs
    quick=QuickInspector(lefttabs,app);lefttabs.add(quick,text='Mebel');app.quick_inspector=quick
    elements=ttk.Frame(lefttabs);lefttabs.add(elements,text='Elementy / CAD')
    treebox=ttk.Frame(elements);treebox.pack(fill='x',padx=6,pady=4)
    app.tree=ttk.Treeview(treebox,show='tree',selectmode='browse',height=5);app.tree.pack(side='left',fill='x',expand=True)
    sb=ttk.Scrollbar(treebox,orient='vertical',command=app.tree.yview);sb.pack(side='right',fill='y');app.tree.configure(yscrollcommand=sb.set)
    app.tree.bind('<<TreeviewSelect>>',app.tree_select);app.tree.bind('<Delete>',lambda e:app.delete())
    actions=WrapToolbar(elements);actions.pack(fill='x',padx=4);app.responsive_toolbars.append(actions)
    for label,fn in [('+ Element',app.add_part),('Kopiuj',app.duplicate),('Usuń',app.delete)]:_button(actions,label,fn)
    formscroll=ScrollPanel(elements);formscroll.pack(fill='both',expand=True);form=formscroll.body;app.vars={}
    for key,label in [('name','Nazwa'),('group','Grupa / mebel'),('u','U · długość'),('v','V · szerokość'),('t','Grubość'),('x','Pozycja X'),('y','Pozycja Y'),('z','Pozycja Z'),('rx','Obrót X'),('ry','Obrót Y'),('rz','Obrót Z')]:
        var=tk.StringVar();app.vars[key]=var;row=FieldRow(form,label,textvariable=var,width=14);row.pack(fill='x',pady=2);row.entry.bind('<Return>',lambda e:app.apply())
    app.vars['plane']=tk.StringVar(value='XY');ttk.Combobox(form,textvariable=app.vars['plane'],values=['XY','XZ','YZ'],state='readonly').pack(fill='x',pady=4)
    app.matvar=tk.StringVar();app.matcombo=ttk.Combobox(form,textvariable=app.matvar,state='readonly');app.matcombo.pack(fill='x',pady=3)
    app.grain=tk.BooleanVar();app.visible=tk.BooleanVar(value=True)
    ttk.Checkbutton(form,text='Włókna wzdłuż U',variable=app.grain).pack(anchor='w');ttk.Checkbutton(form,text='Widoczny',variable=app.visible).pack(anchor='w')
    app.edgevars=[tk.BooleanVar() for _ in range(4)];edges=ttk.Frame(form);edges.pack(fill='x')
    for i,label in enumerate(['U=0','U=max','V=0','V=max']):ttk.Checkbutton(edges,text=label,variable=app.edgevars[i]).grid(row=i//2,column=i%2,sticky='w')
    for label,fn in [('Zastosuj zmiany',app.apply),('Edytuj szkic',app.edit_sketch),('Kontur i otwory…',app.contour),('Przesuń / powiel…',app.move),('Rozciągnij dokładnie…',app.exact_resize),('Zaokrąglij kanty…',app.round_edges)]:ttk.Button(form,text=label,command=fn,padding=4).pack(fill='x',pady=3)
    formscroll.install_wheel(form)
    view=WrapToolbar(middle);view.pack(fill='x',padx=4);app.view_toolbar=view;app.responsive_toolbars.append(view)
    for label,fn in [('3D',lambda:app.view(-38,24)),('Przód',lambda:app.view(0,0)),('Bok',lambda:app.view(-90,0)),('Góra',lambda:app.view(0,90)),('Dół',lambda:app.view(0,-90)),('Dopasuj',app.fit)]:_button(view,label,fn)
    for label,var in [('Rozstrzelony',app.explode),('Wymiary',app.dimensions),('X-ray',app.xray)]:
        ttk.Checkbutton(pages['Widok'],text=label,variable=var,command=app.draw).pack(side='left',padx=4)
    details=WrapToolbar(middle);details.pack(fill='x',padx=5);app.detail_toolbar=details;app.responsive_toolbars.append(details)
    tools=ttk.Combobox(details,state='readonly',width=14,values=['Wybierz / obrót','Przesuwaj','Rozciągaj mebel','Rozciągaj część','Kanty'])
    tools.current(0);tools.pack(side='left',padx=(0,4));app.view_tool_combo=tools
    tools.bind('<<ComboboxSelected>>',lambda e:(app.edit_mode.set(['camera','layout','furniture','resize','fillet'][tools.current()]),app.draw()))
    def sync_tool(*_):
        modes=['camera','layout','furniture','resize','fillet']
        if app.edit_mode.get() in modes:tools.current(modes.index(app.edit_mode.get()))
    app.edit_mode.trace_add('write',sync_tool)
    from viewport_annotations import MODES
    ttk.Label(pages['Widok'],text='Wymiary:').pack(side='left')
    dimcombo=ttk.Combobox(pages['Widok'],textvariable=app.dimension_mode,values=MODES,state='readonly',width=18);dimcombo.pack(side='left',padx=4)
    dimcombo.bind('<<ComboboxSelected>>',lambda e:app.draw())
    ttk.Checkbutton(pages['Widok'],text='Otwory montażowe',variable=app.show_drills,command=app.draw).pack(side='left',padx=5)
    trayframe=ttk.Frame(middle)
    def toggle_tray():
        if trayframe.winfo_manager():trayframe.pack_forget()
        else:trayframe.pack(fill='x',before=app.canvas)
        if hasattr(app,'ui_layout'):app.ui_layout.schedule()
    ttk.Button(details,text='Przeciągnij okucie / LED',command=toggle_tray,padding=(6,3)).pack(side='right')
    from hardware_placement_ui import HardwarePlacementPanel
    app.hardware_panel=HardwarePlacementPanel(trayframe,app);app.hardware_panel.pack(fill='x')
    app.toggle_hardware_tray=toggle_tray

    motion=ttk.Frame(middle);motion.pack(side='bottom',fill='x',padx=5,pady=3)
    app.motion_toolbar=motion
    ttk.Label(motion,text='Otwieranie').pack(side='left')
    app.motion_scale=ttk.Scale(motion,from_=0,to=100,variable=app.motion_value,command=app.open_motion);app.motion_scale.pack(side='left',fill='x',expand=True,padx=6)
    ttk.Checkbutton(motion,text='Cały mebel',variable=app.motion_all).pack(side='left')
    _button(motion,'▶',app.animate_motion);_button(motion,'Zamknij',app.close_motion)
    app.canvas=tk.Canvas(middle,bg='#edf1f4',highlightthickness=0,takefocus=True);app.canvas.pack(fill='both',expand=True)
    for event,fn in [('<Configure>',lambda e:app.draw()),('<ButtonPress-1>',app.mouse_down),('<B1-Motion>',app.orbit),('<ButtonRelease-1>',app.mouse_up),('<ButtonPress-3>',app.pan_start),('<B3-Motion>',app.pan),('<MouseWheel>',app.wheel),('<Button-4>',lambda e:app.zoom(1.12)),('<Button-5>',lambda e:app.zoom(1/1.12))]:app.canvas.bind(event,fn)
    # The ribbon only exposes common actions; detailed settings stay beside 3D.
    for label,fn in [('+ Mebel',quick.new_furniture),('Biblioteka wszystkich mebli',app.library_dialog),('Kształty i style',lambda:app.style_library()),('Kolory',app.palette_dialog)]:_button(pages['Mebel'],label,fn,label=='+ Mebel')
    for label,key in [('Fronty / uchwyty','fronts'),('Zawiasy / prowadnice','fittings'),('Nóżki / blat','legs'),('Materiały','materials'),('LED','led')]:_button(pages['Fronty i okucia'],label,lambda k=key:focus_section(app,k))
    _button(pages['Fronty i okucia'],'Okucia elementu…',app.fittings_dialog)
    for label,fn in [('+ Rysuj',lambda:Sketch(app)),('Edytuj szkic',app.edit_sketch),('Dopasuj element',app.fit_selected),('Zaokrąglenia',app.round_edges)]:_button(pages['Szkic / CAD'],label,fn)
    modes=ttk.Combobox(pages['Szkic / CAD'],state='readonly',width=24,values=['Wybór / obrót','Rozciągaj część','Rozciągaj mebel','Kanty']);modes.current(0);modes.pack(side='left',padx=5)
    modes.bind('<<ComboboxSelected>>',lambda e:(app.edit_mode.set(['camera','resize','furniture','fillet'][modes.current()]),app.draw()))
    ttk.Checkbutton(pages['Szkic / CAD'],text='Krok 32 mm',variable=app.resize_step).pack(side='left')
    ttk.Radiobutton(pages['Układ'],text='Wybór / obrót',variable=app.edit_mode,value='camera',command=app.draw).pack(side='left',padx=6)
    ttk.Radiobutton(pages['Układ'],text='Przesuwaj meble',variable=app.edit_mode,value='layout',command=app.draw).pack(side='left')
    ttk.Checkbutton(pages['Układ'],text='Magnes',variable=app.layout_magnet).pack(side='left',padx=6)
    for label,fn in [('−90°',lambda:app.rotate_furniture(-90)),('+90°',lambda:app.rotate_furniture(90)),('Łączenie i pozycja…',lambda:app.layout_dialog())]:_button(pages['Układ'],label,fn)
    for label,fn in [('Drukuj komplet…',lambda:app.print_project()),('Eksport CAD / CNC',app.export),('Materiały i ceny',app.settings),('Pomoc',app.help)]:_button(pages['Produkcja'],label,fn)
    for label,fn in [('Dopasuj 3D',app.fit),('Interfejs i jakość 3D',interface_settings)]:_button(pages['Widok'],label,fn)
    _button(pages['Ustawienia'],'Okno, skala, panele i silnik 3D',interface_settings,True)
    _button(pages['Ustawienia'],'Materiały i ceny produkcji',app.settings)
    _button(pages['Ustawienia'],'Pomoc',app.help)
    from migration_status import open_status as open_cpp_status
    _button(pages['Ustawienia'],'Silnik C++ i stan migracji',lambda:open_cpp_status(app))
    build_production(app)
    ttk.Label(app,textvariable=app.status,background='#172332',foreground='#cddbe5',padding=(12,5),font=('Segoe UI',9)).pack(side='bottom',fill='x',before=workspacepane)
    from dock_workspace import DockWorkspace
    app.workspace=DockWorkspace(app,app.tabs);app.workspace.add_existing(design,'Projekt 3D',allow_close=False)
    from hardware_ui import install as install_hardware
    from style_ui import install_menu as install_styles
    from furniture_layout_ui import install as install_layout
    install_hardware(app);install_styles(app);install_layout(app)


def focus_section(app,key):
    if hasattr(app,'ui_layout'):app.ui_layout.show_inspector(True)
    app.workspace.select(app.design_page);app.inspector_tabs.select(0)
    section=app.quick_inspector.sections[key];section.opened=True;section.show();app.update_idletasks()
    panel=app.quick_inspector.scroll
    y=section.winfo_y();height=max(1,panel.body.winfo_height());panel.canvas.yview_moveto(y/height)


def build_production(app):
    app.tables={}
    for key,title,cols in [('parts','Formatki',['Grupa','Element','Materiał','Gotowe U','Gotowe V','T','Cięcie U','Cięcie V','Obrzeża m','Kontur']),('costs','Kosztorys',['Pozycja','Wartość zł'])]:
        page=ttk.Frame(app.tabs,padding=10);app.tabs.add(page,text=title)
        if key=='parts':ttk.Label(page,text='Wymiary gotowe zawierają obrzeża; wymiary do cięcia odejmują ich grubość.',padding=(0,0,0,8)).pack(anchor='w')
        else:
            app.costnote=ttk.Label(page,text='',padding=(0,0,0,8));app.costnote.pack(anchor='w');ttk.Button(page,text='Zmień ceny, robociznę i narzut',command=app.settings).pack(anchor='w')
        table=ttk.Treeview(page,columns=cols,show='headings')
        for col in cols:table.heading(col,text=col);table.column(col,width=90 if key=='parts' else 250,anchor='w')
        table.pack(fill='both',expand=True);sb=ttk.Scrollbar(page,orient='horizontal',command=table.xview);sb.pack(fill='x');table.configure(xscrollcommand=sb.set);app.tables[key]=table
    cut=ttk.Frame(app.tabs,padding=10);app.tabs.add(cut,text='Rozkrój')
    app.cutlabel=ttk.Label(cut,text='');app.cutlabel.pack(anchor='w',pady=(0,8))
    app.sheetvar=tk.StringVar();app.sheetcombo=ttk.Combobox(cut,textvariable=app.sheetvar,state='readonly',width=65);app.sheetcombo.pack(anchor='w')
    app.sheetcombo.bind('<<ComboboxSelected>>',lambda e:app.draw_cut())
    app.cutcanvas=tk.Canvas(cut,bg='white',highlightthickness=0);app.cutcanvas.pack(fill='both',expand=True,pady=8);app.cutcanvas.bind('<Configure>',lambda e:app.draw_cut())
    ttk.Label(cut,text='Rozkrój gilotynowy jest przybliżony; nieregularne kontury używają prostokątnych półfabrykatów.').pack(anchor='w')
