"""Persistent, searchable graphic catalogue and explicit backend action routing."""
import copy
import types
import unicodedata
import tkinter as tk
from tkinter import ttk
from catalogue_previews import draw_preview

KINDS={'furniture':'Meble','leg':'Nogi','hinge':'Zawiasy','runner':'Prowadnice','handle':'Uchwyty i otwieranie','led':'LED','appliance':'AGD i wyposażenie','material':'Materiały i kolory'}
ALIASES={'furnitures':'furniture','legs':'leg','hinges':'hinge','runners':'runner','handles':'handle','materials':'material','appliances':'appliance','room':'appliance','LED':'led'}


def canonical_kind(kind):
    kind=ALIASES.get(kind,kind)
    if kind not in KINDS:raise ValueError('Wybierz kategorię katalogu.')
    return kind


def _item(ident,kind,title,category,dimensions,payload,preview,tags=(),source='',note=''):
    return dict(id=f'{kind}:{ident}',kind=kind,title=title,category=category,dimensions=dimensions,payload=payload,preview=preview,tags=list(tags),source=source,note=note)


def catalogue_items(kind='furniture',project=None):
    """Return independent card descriptors; dimensions are mm, no CAD work."""
    kind=canonical_kind(kind);items=[]
    if kind=='furniture':
        from furniture_library import TEMPLATES,template_options
        from furniture_configurator import default_config
        for index,(category,title,w,h,d,family,shelves,fronts) in enumerate(TEMPLATES):
            preview=dict(width=w,depth=d,body_height=h,family=family,shelves=shelves,fronts=fronts,thickness=18)
            total=h
            if template_options(index):
                cfg=default_config(index,project);preview.update(leg_height=cfg['leg_height'],top_mode=cfg['top_mode'],back=cfg['back'],sections=[copy.deepcopy(s) for s in cfg['sections'] if s['enabled']]);total=cfg['height']
            if family=='island':total+=38
            if family=='bed':total+=400
            if family=='chest':total+=18
            items.append(_item(index,kind,title,category,dict(width=w,depth=d,height=total),dict(template_index=index),preview,[family,category,'parametryczny'],note='Edytowalne domyślne wymiary projektu. Podgląd konstrukcji, bez doboru modelu AGD.'))
    elif kind=='leg':
        for height in (80,100,150,200):
            for count in (4,6):
                payload=dict(leg_height=height,leg_count=count,leg_diameter=40,leg_setback=60,leg_color='#242628')
                items.append(_item(f'{height}-{count}',kind,f'Nogi Ø40 · {height} mm · {count} szt.','Własne nogi cylindryczne',dict(height=height,diameter=40,count=count),payload,dict(height=height,diameter=40,color='#242628'),['zakupowe','własny model'],note='Własny model nóg, bez deklaracji nośności producenta. Zmiana zachowuje wysokość korpusu.'))
    elif kind=='hinge':
        from fittings import RECIPES
        for recipe in RECIPES:
            if recipe['id'] not in ('hinge45','hinge52','hinge_sensys52'):continue
            dims={key:recipe[key] for key in ('diameter','depth','pitch','offset')}
            payload=dict(recipe_id=recipe['id'],count=2,price=0)
            items.append(_item(recipe['id'],kind,recipe['name'],recipe.get('manufacturer','Wzorce puszki'),dims,payload,dims,[recipe['verified'],'wymaga sprawdzenia'],recipe['source'],note='Wiercenia z wybranego wzorca. Prowadnik roboczy i pilot wymagają sprawdzenia z rzeczywistym okuciem.'))
    elif kind=='runner':
        from runners_catalog import PROFILES
        for ident,profile in PROFILES.items():
            lengths=profile['lengths'];length=min(lengths,key=lambda value:abs(value-400));clearance=profile.get('external_clearance')
            if clearance is None:clearance=(profile.get('inner_width_offset') or 42)/2
            payload=dict(profile_id=ident,length=length,price=0)
            dims=dict(lengths=list(lengths),clearance=profile.get('external_clearance'),inner_width_offset=profile.get('inner_width_offset'),max_side_thickness=profile.get('max_side_thickness'),load_kg=profile.get('load_kg'),mount=profile['mount'])
            preview=dict(length=length,clearance=clearance,height=46,mount=profile['mount'])
            note=profile['status']+'; bez kompletnych wzorców CNC.'
            items.append(_item(ident,kind,profile['label'],profile['manufacturer'],dims,payload,preview,[profile['family'],'podwieszana' if profile['mount']=='undermount' else 'boczna',profile['extension']],profile['source'],note))
    elif kind=='handle':
        from fittings import HANDLES
        from opening_options import OPTIONS
        for title,spacing in HANDLES:
            items.append(_item(f'spacing-{spacing}',kind,title,'Uchwyty i gałki',dict(spacing=spacing,projection=28),dict(spacing=spacing,color='#242628',projection=28,price=0),{},['zakupowy','własny model'],note='Rozstaw otworów w mm; przelot M4. Kolor i wysunięcie edytowalne.'))
        for option,spec in OPTIONS.items():
            dims={key:spec[key] for key in ('diameter','depth','gap') if key in spec}
            payload=dict(option=option,handle_mode=option)
            items.append(_item(option,kind,spec['label'],'Chwyt i push-to-open',dims,payload,dims,['frezowany' if option.startswith('finger') else 'bez uchwytu',spec.get('hinge_spring','')],spec['source'],spec['note']))
    elif kind=='led':
        for width in (8,12,16):
            for groove in (False,True):
                payload=dict(width=width,length=None,margin=30,groove=groove,depth=3,price_per_m=0,axis='auto')
                items.append(_item(f'{width}-{int(groove)}',kind,f'LED {width} mm · '+('z rowkiem' if groove else 'nakładany'),'Taśma i profil LED',dict(width=width,depth=3,margin=30),payload,{},['własny profil','rowek' if groove else 'nakładany'],note='Własna geometria taśmy i opcjonalnego rowka. Bez doboru zasilacza i parametrów elektrycznych.'))
    elif kind=='appliance':
        from room_objects import CATALOG
        for ident,spec in CATALOG.items():
            category='Oświetlenie' if ident in ('lamp','bulb') else 'Łazienka i zlew' if ident in ('washbasin','sink','faucet','shower','mirror') else 'Akcesoria pomieszczenia' if ident in ('hanger','shelf') else 'AGD'
            dims={key:spec[key] for key in ('width','depth','height')};preview=dict(type=ident,color=spec['color'],**dims)
            items.append(_item(ident,kind,spec['name'],category,dims,dict(object_type=ident),preview,['zakupowe','własne wymiary'],note='Edytowalny model sceny. Wymiary ilustracyjne, bez deklaracji gabarytów/odstępów konkretnego producenta.'))
    elif kind=='material':
        from palette import PALETTE
        material_types={'plyta':'Płyta','mdf':'MDF','hdf':'HDF','drewno':'Drewno'}
        existing=set()
        if project:
            for material in project.materials:
                existing.add(material.name);wood=material.kind=='drewno' or any(word in material.name.lower() for word in ('dąb','orzech','buk','jesion','sosna','brzoza','wenge','mahoń'))
                items.append(_item(material.id,kind,material.name,material_types.get(material.kind,material.kind),dict(sheet_width=material.sheet_u,sheet_height=material.sheet_v),dict(material_id=material.id),dict(color=material.color,wood=wood),[material.color,material.kind],note='Materiał projektu. Barwa ekranowa jest przybliżona.'))
        for index,(name,color) in enumerate(PALETTE):
            title='Dekor — '+name
            if title in existing:continue
            wood=any(word in name.lower() for word in ('dąb','orzech','buk','jesion','sosna','brzoza','wenge','mahoń'))
            items.append(_item(f'palette-{index}',kind,title,'Dekory drewna' if wood else 'Kolory i dekory',{},dict(name=title,color=color,kind='mdf'),dict(color=color,wood=wood),[color,'dekor'],note='Przybliżona barwa ekranu; własny dekor MDF, bez certyfikacji koloru producenta.'))
    return items


def _search(text):
    return ''.join(c for c in unicodedata.normalize('NFKD',str(text).casefold().replace('ł','l')) if not unicodedata.combining(c))


def filter_items(items,query='',category='Wszystkie'):
    tokens=_search(query).split()
    return [item for item in items if (category=='Wszystkie' or item['category']==category) and all(token in _search(' '.join([item['title'],item['category'],str(item['dimensions']),*item['tags']])) for token in tokens)]


def dimension_text(item):
    d=item['dimensions'];kind=item['kind']
    if all(key in d for key in ('width','depth','height')):return f"{d['width']:g} × {d['depth']:g} × {d['height']:g} mm"
    if kind=='leg':return f"H {d['height']:g} · Ø{d['diameter']:g} · {d['count']} szt."
    if kind=='hinge':return f"Ø{d['diameter']:g} × {d['depth']:g} · {d['pitch']:g} × {d['offset']:g} mm"
    if kind=='runner':
        detail='podwieszana' if d['mount']=='undermount' else f"luz {d['clearance']:g} mm / stronę" if d['clearance'] is not None else 'montaż boczny'
        if d['mount']=='undermount' and d['inner_width_offset'] is not None:detail+=f" · wewn. −{d['inner_width_offset']:g} mm"
        return f"{min(d['lengths']):g}–{max(d['lengths']):g} mm · "+detail
    if kind=='handle' and 'spacing' in d:return 'Gałka · 1 otwór' if not d['spacing'] else f"Rozstaw {d['spacing']:g} mm"
    if kind=='led':return f"Szer. {d['width']:g} mm · rowek {d['depth']:g} mm"
    if kind=='material':return item['preview']['color']
    return ' · '.join(f'{key}: {value:g} mm' for key,value in d.items() if isinstance(value,(int,float))) or 'Własne edytowalne wymiary'


def _commit(app,candidate,selected=None,scene_selected=None,fit=False):
    candidate.validate()
    if candidate.data()==app.project.data():return True
    app.snapshot();app.project=candidate
    if selected is not None:app.selected=selected;app.selected_scene=None
    if scene_selected is not None:app.selected_scene=scene_selected;app.selected=None
    app.refresh()
    if fit and hasattr(app,'fit'):app.fit()
    return True


def select_design(app):
    """Return to the existing main 3D page without closing the catalogue."""
    if hasattr(app,'workspace') and hasattr(app,'design_page'):app.workspace.select(app.design_page)
    if hasattr(app,'canvas'):app.canvas.focus_set()


def apply_item(app,item):
    """Use existing transactional factory/controller commands for a card."""
    if hasattr(app,'flush_parameter_drafts') and not app.flush_parameter_drafts():return False
    kind=canonical_kind(item['kind']);payload=copy.deepcopy(item['payload'])
    if kind=='furniture':
        from furniture_library import create_furniture
        from furniture_layout import group_bounds,move_group
        candidate,group=create_furniture(app.project,payload['template_index'])
        if app.project.parts:
            right=max(group_bounds(app.project,g)[1][0] for g in {p.group for p in app.project.parts});lo,_=group_bounds(candidate,group);candidate=move_group(candidate,group,right+50-lo[0])
        return _commit(app,candidate,next(p.id for p in candidate.parts if p.group==group),fit=True)
    if kind in ('hinge','runner','led') or kind=='handle' and 'option' not in payload:
        controller=getattr(app,'hardware_drag',None)
        if controller is None:raise ValueError('Panel przeciągania okuć nie jest dostępny. Otwórz panel wyposażenia mebla.')
        controller.begin(kind,payload);select_design(app);return True
    if kind=='appliance':
        from room_objects import add_object,object_vertices
        from furniture_layout import group_bounds
        points=object_vertices(app.project);right=max((p[0] for p in points),default=-50)
        if app.project.parts:right=max(right,max(group_bounds(app.project,g)[1][0] for g in {p.group for p in app.project.parts}))
        candidate=add_object(app.project,payload['object_type'],x=right+50);object_id=candidate.scene['objects'][-1]['id']
        return _commit(app,candidate,scene_selected=object_id,fit=True)
    part=app.selected_part()
    if part is None:raise ValueError('Najpierw zaznacz mebel lub jego część w projekcie.')
    if kind=='leg':
        from furniture_configurator import read_config,configure
        from corner_furniture import read_corner_config,create_corner
        corner=read_corner_config(app.project,part.group);cfg=corner or read_config(app.project,part.group)
        if cfg is None:raise ValueError('Nogi z katalogu wymagają parametrycznego mebla lub narożnika. Przejmij mebel do kreatora.')
        cfg['height']+=payload['leg_height']-cfg.get('leg_height',0);cfg.update(payload)
        candidate,group=create_corner(app.project,cfg,part.group) if corner else configure(app.project,cfg,part.group)
        return _commit(app,candidate,part.id)
    if kind=='handle':
        from opening_options import install
        candidate=install(app.project,part.id,payload['option']);return _commit(app,candidate,part.id)
    if kind=='material':
        from core import Material
        candidate=copy.deepcopy(app.project)
        if 'material_id' in payload:
            ident=payload['material_id']
            if ident not in {m.id for m in candidate.materials}:raise ValueError('Materiał pochodzi z poprzedniego projektu. Otwórz katalog ponownie.')
        else:
            match=next((m for m in candidate.materials if m.name==payload['name'] and m.color==payload['color']),None)
            if match is None:match=Material(name=payload['name'],color=payload['color'],kind=payload['kind'],price_m2=0);candidate.materials.append(match)
            ident=match.id
        next(p for p in candidate.parts if p.id==part.id).material=ident
        return _commit(app,candidate,part.id)
    raise ValueError('Brak obsługi wybranej karty katalogu.')


class VisualCatalogue(ttk.Frame):
    def __init__(self,parent,app,kind='furniture',on_select=None):
        super().__init__(parent,padding=10);self.app=app;self.form=parent;self.kind=canonical_kind(kind);self.on_select=on_select;self.selected=None;self.job=None;self.columns=0;self.cards={};self.items=[];self.generation=app.project_generation
        self.kindvar=tk.StringVar(value=KINDS[self.kind]);self.query=tk.StringVar();self.category=tk.StringVar(value='Wszystkie');self.note=tk.StringVar(value='Kliknij obraz lub Wybierz. Wymiary są w mm.');self.count=tk.StringVar()
        head=ttk.Frame(self);head.pack(fill='x',pady=(0,7));ttk.Label(head,text='KATALOG GRAFICZNY',font=('Segoe UI',12,'bold'),foreground='#176a78').pack(side='left')
        self.kindcombo=ttk.Combobox(head,textvariable=self.kindvar,values=list(KINDS.values()),state='readonly',width=22);self.kindcombo.pack(side='right');self.kindcombo.bind('<<ComboboxSelected>>',lambda e:self.set_kind(next(k for k,v in KINDS.items() if v==self.kindvar.get())))
        row=ttk.Frame(self);row.pack(fill='x',pady=(0,7));ttk.Label(row,text='Szukaj').pack(side='left');self.search=ttk.Entry(row,textvariable=self.query);self.search.pack(side='left',fill='x',expand=True,padx=7)
        self.categorycombo=ttk.Combobox(row,textvariable=self.category,state='readonly',width=25);self.categorycombo.pack(side='right');self.categorycombo.bind('<<ComboboxSelected>>',lambda e:self.render())
        self.query.trace_add('write',lambda *a:self.schedule());ttk.Label(self,textvariable=self.count,foreground='#68788a').pack(anchor='w')
        bottom=ttk.Frame(self);bottom.pack(side='bottom',fill='x',pady=(7,0));self.choosebutton=ttk.Button(bottom,text='Zastosuj wybrane',command=self.activate_selected,state='disabled');self.choosebutton.pack(side='right',padx=(8,0))
        self.sourcebutton=ttk.Button(bottom,text='Źródło',command=self.open_source,state='disabled');self.sourcebutton.pack(side='right')
        ttk.Label(bottom,textvariable=self.note,wraplength=650).pack(side='left',fill='x',expand=True)
        content=ttk.Frame(self);content.pack(fill='both',expand=True);self.canvas=tk.Canvas(content,height=410,highlightthickness=0,bg='#f2f5f6');self.canvas.pack(side='left',fill='both',expand=True)
        sb=ttk.Scrollbar(content,orient='vertical',command=self.canvas.yview);sb.pack(side='right',fill='y');self.canvas.configure(yscrollcommand=sb.set)
        self.gridbody=ttk.Frame(self.canvas);self.window=self.canvas.create_window((0,0),window=self.gridbody,anchor='nw');self.gridbody.bind('<Configure>',lambda e:self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.canvas.bind('<Configure>',self.resize);self.canvas.bind('<MouseWheel>',self.wheel);self.bind('<Destroy>',self.destroyed,add='+');self.set_kind(self.kind,on_select)

    def wheel(self,event):self.canvas.yview_scroll(-1 if event.delta>0 else 1,'units');return 'break'
    def destroyed(self,event):
        if event.widget is self and self.job:
            self.after_cancel(self.job);self.job=None
    def schedule(self):
        if self.job:self.after_cancel(self.job)
        self.job=self.after(130,self.render)
    def resize(self,event):
        self.canvas.itemconfigure(self.window,width=event.width)
        columns=max(1,event.width//215)
        if columns!=self.columns:self.columns=columns;self.schedule()
    def set_kind(self,kind,on_select=None):
        self.kind=canonical_kind(kind);self.on_select=on_select;self.kindvar.set(KINDS[self.kind]);self.generation=self.app.project_generation;self.items=catalogue_items(self.kind,self.app.project);self.category.set('Wszystkie');self.query.set('');self.selected=None
        self.kindcombo.configure(state='disabled' if on_select else 'readonly')
        self.sourcebutton.configure(state='disabled')
        self.categorycombo.configure(values=['Wszystkie']+sorted({i['category'] for i in self.items}));self.render();self.canvas.yview_moveto(0)
    def render(self):
        if self.job:self.after_cancel(self.job);self.job=None
        for child in self.gridbody.winfo_children():child.destroy()
        self.cards={};filtered=filter_items(self.items,self.query.get(),self.category.get());self.count.set(f'{len(filtered)} / {len(self.items)} pozycji · '+KINDS[self.kind]);columns=max(1,self.columns)
        for index,item in enumerate(filtered):
            card=ttk.Frame(self.gridbody,padding=7,relief='ridge');card.grid(row=index//columns,column=index%columns,padx=4,pady=4,sticky='nsew');self.gridbody.columnconfigure(index%columns,weight=1)
            image=tk.Canvas(card,width=190,height=118,bg='#fafbf9',highlightthickness=0,cursor='hand2');image.pack(fill='x');draw_preview(image,item,190,118)
            title=ttk.Label(card,text=item['title'],wraplength=185,font=('Segoe UI',9,'bold'));title.pack(fill='x',pady=(3,0));ttk.Label(card,text=dimension_text(item),wraplength=185,foreground='#596c78').pack(fill='x')
            ttk.Label(card,text=item['category'],wraplength=185,foreground='#176a78',font=('Segoe UI',8)).pack(fill='x')
            ttk.Button(card,text='Wybierz',command=lambda i=item:self.activate(i)).pack(fill='x',pady=(5,0));image.bind('<ButtonRelease-1>',lambda e,i=item:self.activate(i));title.bind('<ButtonRelease-1>',lambda e,i=item:self.activate(i))
            for widget in (card,image,title):widget.bind('<MouseWheel>',self.wheel)
            self.cards[item['id']]=card
        self.choosebutton.configure(state='normal' if self.selected and any(i['id']==self.selected['id'] for i in filtered) else 'disabled')
        if hasattr(self.form,'mark_clean'):self.form.mark_clean()
    def activate_selected(self):return self.activate(self.selected) if self.selected else False
    def open_source(self):
        if self.selected and self.selected.get('source','').startswith('https://'):
            import webbrowser
            webbrowser.open(self.selected['source'])
    def activate(self,item):
        if not item:return False
        try:
            if self.generation!=self.app.project_generation:raise ValueError('Projekt zmienił się. Otwórz katalog ponownie.')
            if hasattr(self.form,'ensure_current_project'):self.form.ensure_current_project(raise_error=True)
            if self.on_select and hasattr(self.app,'flush_parameter_drafts') and not self.app.flush_parameter_drafts():return False
            result=self.on_select(copy.deepcopy(item)) if self.on_select else apply_item(self.app,item)
            if result is False:return False
            if not self.winfo_exists():return True
            self.selected=item;self.choosebutton.configure(state='normal');self.sourcebutton.configure(state='normal' if item.get('source') else 'disabled')
            action='Wskaż miejsce na meblu, aby zamontować.' if self.on_select is None and item['kind'] in ('hinge','runner','led','handle') and 'option' not in item['payload'] else 'Wybrano.'
            self.note.set(item['title']+' · '+action+' '+item['note'])
            if hasattr(self.app,'status'):self.app.status.set('Katalog: '+item['title']+' · '+action)
            if hasattr(self.form,'mark_clean'):self.form.mark_clean()
            return True
        except (ValueError,KeyError,TypeError,StopIteration) as error:self.note.set(str(error));return False


def show(app,kind='furniture',on_select=None):
    """Reuse one live dock tab; callbacks receive a complete card descriptor."""
    kind=canonical_kind(kind);panel=getattr(app,'_visual_catalogue_panel',None)
    if panel is not None:
        try:
            if panel.winfo_exists() and panel.generation==app.project_generation:
                panel.set_kind(kind,on_select);panel.form.document.workspace.select(panel.form.document);return panel
        except tk.TclError:pass
    form=app.dialog('Katalog graficzny','900x680');panel=VisualCatalogue(form,app,kind,on_select);panel.pack(fill='both',expand=True);app._visual_catalogue_panel=panel
    # Filters and card selection are presentation state; the actual backend
    # action commits immediately and is tracked by the project Undo stack.
    if hasattr(form,'document'):form.document.has_changes=lambda:False
    return panel


def install(app):
    """Expose the catalogue through explicit discoverable ribbon commands."""
    app.visual_catalogue=types.MethodType(lambda self,kind='furniture',on_select=None:show(self,kind,on_select),app)
    if getattr(app,'_catalogue_buttons_installed',False):return app
    pages=getattr(app,'ribbon_pages',{});app.catalogue_buttons={}
    for kind,label,page in [('furniture','Katalog mebli','Mebel'),('hinge','Katalog okuć','Mebel'),('material','Katalog materiałów','Mebel'),('appliance','Katalog AGD','Scena')]:
        parent=pages.get(page)
        if parent is None:continue
        button=ttk.Button(parent,text=label,command=lambda k=kind:app.visual_catalogue(k),style='Ribbon.TButton',padding=(7,4));button.pack(side='left',padx=2,pady=3);app.catalogue_buttons[kind]=button
    app._catalogue_buttons_installed=bool(app.catalogue_buttons)
    return app
