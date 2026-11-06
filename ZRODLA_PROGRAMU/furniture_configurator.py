"""Atomic cabinet configurator with editable bays and provenance.

UI API: default_config(index,project=None), normalize_config(config,project),
read_config(project,group), infer_config(project,group),
configuration_status(project,group), configure(project,config,group=None,origin=None).
Height includes purchased legs and an optional worktop. Width/depth describe
the carcass; front/back thickness and worktop overhang extend its envelope.
"""
import copy
import hashlib
import json
from core import Project,Part,finite,uid,add_template
from assembly import attach,frame,frame_for_part,DEFAULTS,inferred_role,bounds
from furniture_library import TEMPLATES,create_furniture,template_options
from fittings import recipe,add_handle
from dimension_model import regenerate_hinges
from furniture_layout import group_layout

SUPPORTED={'cabinet','shelf','drawers','island'}


def _integer(value,lo,hi,label):
    value=finite(value,lo,hi)
    if value!=int(value):raise ValueError(label+' musi być liczbą całkowitą.')
    return int(value)


def default_config(template_index=0,project=None):
    template_index=_integer(template_index,0,len(TEMPLATES)-1,'Wzorzec')
    category,name,w,h,d,kind,shelves,fronts=TEMPLATES[template_index]
    top=38 if kind=='island' else 0
    c=dict(schema=1,template_index=template_index,name=name,width=w,height=h+100+top,depth=d,
           thickness=18,front_thickness=18,gap=2,material='',front_material='',back=kind!='shelf',back_thickness=3,
           worktop=bool(top),worktop_thickness=top or 28,worktop_overhang=20,top_mode='panel',rail_width=80,
           leg_height=100,leg_count=4,leg_diameter=40,leg_setback=60,leg_color='#242628',leg_price=0,
           plinth=False,plinth_setback=40,hinge_pattern='hinge45',hinge_count=2,hinge_spring='unknown',
           handle_mode='handle',handle_spacing=128,handle_color='#242628',opening_angle=100,
           runner_profile='',runner_length=min(400,d-30),drawer_side_thickness=16,drawer_bottom_thickness=6,
           front_style='smooth',front_border=60,front_depth=3,front_radius=6,
           flute_width=8,flute_spacing=20,flute_count=0,finger_length=120,finger_width=24,finger_depth=6,
           led=False,led_mount='under_shelf',led_width=12,led_depth=3,led_margin=30,
           led_price_per_m=0,led_profile=True,led_diffuser=True,led_color='#fff3bf',led_source='',
           origin=[0,0,0],sections=[])
    for key in ('bottom','main','upper'):
        c['sections'].append(dict(key=key,enabled=key=='main',height=200 if key!='main' else 0,
                                 columns=1,shelves=0 if kind=='drawers' else shelves,
                                 front='drawers' if kind=='drawers' else 'open' if not fronts else 'doors',
                                 doors=min(2,max(1,fronts)),drawers=fronts if kind=='drawers' else 3))
    if project:
        c['material']=project.materials[0].id;c['front_material']=project.materials[min(1,len(project.materials)-1)].id
    preset=template_options(template_index);sections=preset.pop('sections',[]);old_legs=c['leg_height'];c.update(preset)
    c['height']+=c['leg_height']-old_legs
    for options in sections:
        for section in c['sections']:
            if section['key']==options['key']:section.update(copy.deepcopy(options))
    return c


def normalize_config(config,project=None):
    if not isinstance(config,dict):raise ValueError('Konfiguracja musi zawierać parametry mebla.')
    c=default_config(config.get('template_index',0),project);c.update(copy.deepcopy(config));c['schema']=1
    c['template_index']=_integer(c['template_index'],0,len(TEMPLATES)-1,'Wzorzec')
    if TEMPLATES[c['template_index']][5] not in SUPPORTED:
        raise ValueError('Szybki kreator obsługuje korpusy, regały, komody i wyspy. Ten wzorzec dodaj z pełnej biblioteki mebli.')
    if not isinstance(c['name'],str) or not c['name'].strip():raise ValueError('Wpisz nazwę mebla.')
    c['name']=c['name'].strip()
    ranges={'width':(100,10000),'height':(100,10000),'depth':(80,10000),'thickness':(6,60),'front_thickness':(6,60),
            'gap':(.1,30),'back_thickness':(.5,40),'worktop_thickness':(1,100),'worktop_overhang':(0,300),'rail_width':(20,300),
            'leg_height':(0,1000),'leg_diameter':(10,200),'leg_setback':(0,500),'leg_price':(0,100000),
            'plinth_setback':(0,500),'handle_spacing':(0,2000),'opening_angle':(0,180),
            'runner_length':(50,1500),'drawer_side_thickness':(6,32),'drawer_bottom_thickness':(3,20)}
    for key,(lo,hi) in ranges.items():c[key]=finite(c[key],lo,hi)
    if c['top_mode'] not in ('panel','rails'):raise ValueError('Góra korpusu: pełna płyta albo listwy wzmacniające.')
    if c['top_mode']=='rails' and 2*c['rail_width']>c['depth']:raise ValueError('Dwie listwy nie mieszczą się w głębokości korpusu. Zmniejsz ich szerokość.')
    c['hinge_count']=_integer(c['hinge_count'],2,8,'Liczba zawiasów');c['leg_count']=_integer(c['leg_count'],4,6,'Liczba nóg')
    if c['leg_count'] not in (4,6):raise ValueError('Wybierz 4 albo 6 nóg.')
    for key in ('back','worktop','plinth','led'):
        if type(c[key]) is not bool:raise ValueError('Opcja '+key+' wymaga TAK / NIE.')
    if c['plinth'] and not c['leg_height']:raise ValueError('Cokół wymaga dodatniej wysokości nóg.')
    for key in ('material','front_material'):
        if project and c[key] not in {m.id for m in project.materials}:raise ValueError('Wybierz istniejący materiał korpusu i frontów.')
    try:r=recipe(c['hinge_pattern'])
    except (StopIteration,TypeError):raise ValueError('Wybierz obsługiwany wzorzec zawiasu.') from None
    if c['hinge_pattern']=='hinge_sensys_tb55' or 'hinge' not in c['hinge_pattern']:raise ValueError('Wybierz obsługiwany zawias nakładany.')
    if c['hinge_spring'] not in ('unknown','sprung','unsprung'):raise ValueError('Stan zawiasu: unknown, sprung albo unsprung.')
    if c['runner_profile']:
        from runners_catalog import PROFILES
        if c['runner_profile'] not in PROFILES:raise ValueError('Wybierz profil prowadnicy z katalogu.')
    from opening_options import OPTIONS
    if c['handle_mode'] not in ('handle','none','keep',*OPTIONS):raise ValueError('Nieznany sposób chwytania / otwierania frontu.')
    if not isinstance(c['origin'],(list,tuple)) or len(c['origin'])!=3:raise ValueError('Pozycja korpusu wymaga X, Y i Z.')
    c['origin']=[finite(v) for v in c['origin']]
    body_height=c['height']-c['leg_height']-(c['worktop_thickness'] if c['worktop'] else 0)
    if min(c['width'],c['depth'],body_height)<=2*c['thickness']+20:raise ValueError('Wymiary mebla są zbyt małe dla grubości płyt i nóg.')
    sections=config.get('sections',c['sections'])
    if not isinstance(sections,list) or not 1<=len(sections)<=3:raise ValueError('Kreator wymaga od jednej do trzech stref.')
    by_key={}
    for i,section in enumerate(sections):
        if not isinstance(section,dict):raise ValueError('Nieprawidłowa strefa mebla.')
        key=section.get('key','main' if len(sections)==1 else ('bottom','main','upper')[i])
        if key not in ('bottom','main','upper') or key in by_key:raise ValueError('Strefy: dół, główna i pawlacz, każda najwyżej raz.')
        defaults=next(s for s in default_config(c['template_index'],project)['sections'] if s['key']==key)
        s={**defaults,**copy.deepcopy(section),'key':key}
        if type(s['enabled']) is not bool:raise ValueError('Włączenie strefy wymaga TAK / NIE.')
        s['columns']=_integer(s['columns'],1,4,'Liczba kolumn');s['shelves']=_integer(s['shelves'],0,8,'Liczba półek')
        s['doors']=_integer(s['doors'],1,2,'Liczba drzwi');s['drawers']=_integer(s['drawers'],1,8,'Liczba szuflad')
        if s['front'] not in ('open','doors','drawers'):raise ValueError('Strefa: otwarta, drzwi albo szuflady.')
        s['height']=finite(s['height'],0,10000)
        by_key[key]=s
    if 'main' not in by_key or not by_key['main']['enabled']:raise ValueError('Włącz główną strefę mebla.')
    for key in ('bottom','upper'):
        if key not in by_key:by_key[key]={**next(s for s in default_config(c['template_index'],project)['sections'] if s['key']==key),'enabled':False}
    by_key['main']['height']=body_height-sum(by_key[key]['height'] for key in ('bottom','upper') if by_key[key]['enabled'])
    c['sections']=[by_key[key] for key in ('bottom','main','upper')]
    for s in c['sections']:
        if s['enabled'] and s['height']<=2*c['thickness']+30:raise ValueError('Strefa '+s['key']+' jest zbyt niska po odjęciu pozostałych stref.')
    from configurator_accessories import normalize_front_style,normalize_accessories
    c['front_style']=normalize_front_style(c['front_style'],c)['style']
    c.update(normalize_accessories(c))
    for s in c['sections']:
        if s.get('front_style'):s['front_style']=normalize_front_style(s['front_style'],c)['style']
    return c


def _geometry(part):
    data={key:copy.deepcopy(getattr(part,key)) for key in ('u','v','t','plane','x','y','z','rx','ry','rz','contour','profile','decor','role')}
    return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()


def _source(project,group):
    return next((p.configurator_source for p in project.parts if p.group==group and p.configurator_source.get('config')),None)


def read_config(project,group):
    source=_source(project,group)
    return copy.deepcopy(source['config']) if source else None


def configuration_status(project,group):
    parts=[p for p in project.parts if p.group==group];source=_source(project,group)
    if not source:return dict(configured=False,editable=False,reason='Ten mebel nie pochodzi z szybkiego kreatora. Możesz rozpoznać wzorzec biblioteki przyciskiem Konwertuj.')
    by_id={p.id:p for p in parts}
    for key,entry in source.get('manifest',{}).items():
        p=by_id.get(entry['id'])
        if p is None:return dict(configured=True,editable=False,reason='Usunięto część kreatora: '+key+'. Utwórz nowy mebel lub użyj zwykłych edytorów.')
        if _geometry(p)!=entry['signature']:return dict(configured=True,editable=False,reason='Geometrię części „'+p.name+'” zmieniono ręcznie. Kreator jej nie nadpisze; użyj zwykłych edytorów lub utwórz nowy mebel.')
    return dict(configured=True,editable=True,reason='Konfiguracja dostępna; ręczne dodatki i obróbki zostaną zachowane.')


def _mark(part,key):
    part.configurator_source=dict(schema=1,key=key)


def _build(project,c,group):
    t=c['thickness'];w,d=c['width'],c['depth'];ox,oy,oz=c['origin'];floor=oz;oz+=c['leg_height']
    h=c['height']-c['leg_height']-(c['worktop_thickness'] if c['worktop'] else 0)
    result=copy.deepcopy(project)
    carcass=add_template(result,'shelf',w,h,d,t,shelves=0,front=False,origin=(ox,oy,oz),group=group)
    mapping={'Bok lewy':('carcass.left','left'),'Bok prawy':('carcass.right','right'),'Dno':('carcass.bottom','bottom'),'Wieniec':('carcass.top','top')}
    core={}
    for p in carcass:
        key,role=mapping[p.name];p.role=role;p.material=c['material'];_mark(p,key);core[key]=p
    if c['top_mode']=='rails':
        # The front rail remains the bay's upper support. Both real strips
        # reach the side panels and keep the carcass datum unchanged.
        top=core['carcass.top'];top.v=c['rail_width'];top.name='Listwa górna przednia'
        rear=copy.deepcopy(top);rear.id=uid();rear.name='Listwa górna tylna';rear.y=oy+d-c['rail_width']
        _mark(rear,'carcass.rail_back');result.parts.append(rear)
    def add(key,name,u,v,thick,plane,x,y,z,role='other',material=None):
        p=Part(name=name,group=group,u=u,v=v,t=thick,plane=plane,x=x,y=y,z=z,role=role,material=material or c['material'])
        _mark(p,key);p.validate();result.parts.append(p);return p
    if c['back']:
        back_material=next((m.id for m in result.materials if m.kind=='hdf'),c['material'])
        add('carcass.back','Plecy nakładane HDF',w,h,c['back_thickness'],'XZ',ox,oy+d,oz,'back',back_material)
    if c['worktop']:
        over=c['worktop_overhang'];add('carcass.worktop','Blat',w+2*over,d+2*over,c['worktop_thickness'],'XY',ox-over,oy-over,oz+h,'worktop')
    if c['plinth']:
        if c['plinth_setback']+t>d:raise ValueError('Cofnięcie cokołu wykracza poza korpus.')
        add('carcass.plinth','Cokół',w,c['leg_height'],t,'XZ',ox,oy+c['plinth_setback'],floor,'accessory_panel')
    active=[s for s in c['sections'] if s['enabled']];boards=[core['carcass.bottom']];boundary=oz
    for section in active[:-1]:
        boundary+=section['height']
        boards.append(add('separator.'+section['key'],'Przegroda pozioma — '+section['key'],w-2*t,d,t,'XY',ox+t,oy,boundary-t/2,'partition'))
    boards.append(core['carcass.top'])
    for row,section in enumerate(active):
        lower,upper=boards[row:row+2];key=section['key'];column_parts=[core['carcass.left']]
        for col in range(1,section['columns']):
            column_parts.append(add(key+f'.divider.{col}','Przegroda pionowa — '+key+f' {col}',d,upper.z-lower.z-lower.t,t,'YZ',ox+w*col/section['columns']-t/2,oy,lower.z+lower.t,'partition'))
        column_parts.append(core['carcass.right'])
        for col in range(section['columns']):
            bay=dict(left=column_parts[col].id,right=column_parts[col+1].id,bottom=lower.id,top=upper.id,
                     left_mid=col>0,right_mid=col<section['columns']-1,bottom_mid=row>0,top_mid=row<len(active)-1)
            f=frame(result,group,bay=bay);lo,hi=f['lower'],f['upper'];gap=c['gap'];bay_key=key+f'.bay.{col}'
            if section['front']=='drawers' and section['shelves']:raise ValueError('Strefa szuflad nie może jednocześnie zawierać półek. Wybierz inną strefę dla półek.')
            for index in range(1,section['shelves']+1):
                level=f['inner_bottom']-lo[2]+(f['inner_top']-f['inner_bottom']-t)*index/(section['shelves']+1)
                shelf=Part(name='Półka — '+key+f' {col+1}.{index}',u=f['inner_right']-f['inner_left']-2*gap,v=d-2*gap,t=t,material=c['material'])
                result,shelf=attach(result,shelf,group,'shelf',dict(bay=bay,gap=gap,level=level,bay_shelves=section['shelves'],bay_shelf_index=index))
                _mark(shelf,bay_key+f'.shelf.{index}')
            if section['front']=='open':continue
            count=section['doors'] if section['front']=='doors' else section['drawers']
            for index in range(count):
                is_door=section['front']=='doors';front_key=bay_key+('.door.' if is_door else '.drawer.')+str(index)
                width=(hi[0]-lo[0]-(count+1)*gap)/count if is_door else hi[0]-lo[0]-2*gap
                height=hi[2]-lo[2]-2*gap if is_door else (hi[2]-lo[2]-(count+1)*gap)/count
                cfg=dict(bay=bay,gap=gap,hinge_side='left' if index==0 else 'right',hinge_count=c['hinge_count'],hinge_pattern=c['hinge_pattern'],hinge_spring=c.get('hinge_spring','unknown'))
                front=Part(name=('Drzwi' if is_door else 'Front szuflady')+' — '+key+f' {col+1}.{index+1}',u=width,v=height,t=c['front_thickness'],material=c['front_material'])
                if is_door:
                    cfg['bay_doors']=count
                    result,front=attach(result,front,group,'door',cfg)
                    result=regenerate_hinges(result,front.id,c['hinge_pattern'],c['hinge_count'],cfg['hinge_side'])
                else:
                    level=gap+index*(height+gap);raise_by=max(0,f['inner_bottom']-(lo[2]+level+gap))+6
                    drawer_height=min(120,height-c['drawer_bottom_thickness']-2*gap-raise_by)
                    if drawer_height<20:raise ValueError('Front szuflady jest za niski dla skrzynki. Zmniejsz liczbę szuflad lub zwiększ strefę.')
                    cfg.update(level=level,drawer_raise=raise_by,drawer_height=drawer_height,drawer_t=c['drawer_side_thickness'],drawer_bottom=c['drawer_bottom_thickness'],drawer_clearance=12.7,
                               drawer_depth=c['runner_length'],bay_drawers=count,bay_drawer_index=index)
                    if c['runner_profile']:
                        cfg.update(auto_hardware=False,drawer_front_gap=0,runner_profile=c['runner_profile'],runner_nominal=c['runner_length'])
                    result,front=attach(result,front,group,'drawer',cfg)
                    if c['runner_profile']:
                        from runner_installation import apply_runner
                        result=apply_runner(result,front.id,c['runner_profile'],c['runner_length'],c['drawer_side_thickness'],c['drawer_bottom_thickness'])
                front=next(p for p in result.parts if p.id==front.id);_mark(front,front_key)
                for child in result.parts:
                    if child.assembly.get('generated_by')==front.id:_mark(child,front_key+'.box.'+child.name.split(' / ')[-1])
                if is_door:front.motion=dict(mode='hinge_'+cfg['hinge_side'],angle=c['opening_angle'],travel=0)
                elif not front.motion:front.motion=dict(mode='drawer',angle=0,travel=c['runner_length']*.9)
                from configurator_accessories import apply_front_to_part,normalize_front_style
                effective_style=normalize_front_style(section.get('front_style',c['front_style']),c)['style']
                if c['handle_mode']=='handle' and effective_style!='finger':
                    u=width/2 if not is_door else width-35 if cfg['hinge_side']=='left' else 35
                    spacing=c['handle_spacing'];vertical=is_door
                    if spacing and spacing>(height if vertical else width)-20:raise ValueError('Rozstaw uchwytu nie mieści się na froncie. Zmniejsz rozstaw lub wybierz gałkę.')
                    result=add_handle(result,front.id,spacing,u=u,v=height/2,vertical=vertical,color=c['handle_color'])
                elif c['handle_mode'] not in ('handle','none','keep') and effective_style!='finger':
                    from opening_options import install
                    result=install(result,front.id,c['handle_mode'],length=c['finger_length'],width=c['finger_width'],depth=c['finger_depth'],hinge_spring=c['hinge_spring'])
                front=next(p for p in result.parts if p.id==front.id)
                apply_front_to_part(result,front,effective_style,c)
    from configurator_accessories import install_accessories
    result=install_accessories(result,group,c,dict(floor=floor,body_x=ox,body_y=oy,body_z=oz,width=w,depth=d,body_height=h),validate=False)
    for p in result.parts:
        if p.group==group:
            for op in p.operations:op['config_auto']=True
    ids={p.id for p in result.parts if p.group==group}
    for item in result.hardware:
        if item.get('owner_id') in ids:item['config_auto']=True
    return result


def _remap(project,mapping):
    for p in project.parts:
        p.id=mapping.get(p.id,p.id)
        for key in ('anchors','children'):
            if key in p.assembly:p.assembly[key]=[mapping.get(i,i) for i in p.assembly[key]]
        if p.assembly.get('generated_by'):p.assembly['generated_by']=mapping.get(p.assembly['generated_by'],p.assembly['generated_by'])
        bay=p.assembly.get('config',{}).get('bay')
        if bay:
            for key in ('left','right','bottom','top'):bay[key]=mapping.get(bay[key],bay[key])
        for collection in (p.operations,p.machining):
            for op in collection:
                for key in ('owner_id','opening_owner'):
                    if op.get(key):op[key]=mapping.get(op[key],op[key])
    for item in project.hardware:
        if item.get('owner_id'):item['owner_id']=mapping.get(item['owner_id'],item['owner_id'])
        if 'anchors' in item:item['anchors']=[mapping.get(i,i) for i in item['anchors']]


def configure(project,config,group=None,*,origin=None):
    """Return a complete candidate and group. Input and Undo remain untouched."""
    c=normalize_config(config,project);original_group=group;existing=[p for p in project.parts if p.group==group] if group else []
    old_config=read_config(project,group) if existing else None;legacy=False
    if existing:
        if old_config:
            status=configuration_status(project,group)
            if not status['editable']:raise ValueError(status['reason'])
        else:
            old_config=infer_config(project,group);legacy=True
        c['origin']=copy.deepcopy(old_config['origin']) if origin is None else [finite(v) for v in origin]
        if len(c['origin'])!=3:raise ValueError('Pozycja korpusu wymaga X, Y i Z.')
        layout=group_layout(project,group)
        if c['name']!=group:
            if any(p.group==c['name'] for p in project.parts):raise ValueError('Mebel o tej nazwie już istnieje. Wpisz inną nazwę.')
            group=c['name']
    else:
        layout={};base=c['name'];group=base;number=2;occupied={p.group for p in project.parts}
        while group in occupied:group=base+f' ({number})';number+=1
    c['name']=group
    baseline=copy.deepcopy(project);old_by_key={p.configurator_source.get('key'):p for p in existing if p.configurator_source.get('key')}
    old_managed={p.id for p in old_by_key.values()}
    if legacy:old_managed={p.id for p in existing}
    manual=[copy.deepcopy(p) for p in existing if p.id not in old_managed]
    retained=[copy.deepcopy(h) for h in baseline.hardware if h.get('owner_id') in {p.id for p in existing} and not h.get('config_auto')]
    if legacy:retained=[]
    baseline.parts=[p for p in baseline.parts if p.group!=(original_group if existing else group)]
    baseline.hardware=[h for h in baseline.hardware if h.get('owner_id') not in {p.id for p in existing}]
    result=_build(baseline,c,group);built=[p for p in result.parts if p.group==group]
    if legacy:
        # Explicit conversion retains IDs of matching structural roles and fronts.
        available=list(existing)
        for p in built:
            match=next((q for q in available if q.name==p.name),None)
            part_key=p.configurator_source['key']
            if match is None and '.shelf.' in part_key:
                label='Półka '+part_key.rsplit('.shelf.',1)[1]
                match=next((q for q in available if q.name==label),None)
            if match is None and '.box.' in part_key:
                parent_key,suffix=part_key.split('.box.',1);parent=old_by_key.get(parent_key)
                if parent:match=next((q for q in available if q.assembly.get('generated_by')==parent.id and q.name.split(' / ')[-1]==suffix),None)
            if match is None:match=next((q for q in available if inferred_role(q)==p.role and q.plane==p.plane and abs(q.x-p.x)<1e-6 and abs(q.z-p.z)<1e-6),None)
            if match:old_by_key[p.configurator_source['key']]=match;available.remove(match)
    new_keys={p.configurator_source.get('key') for p in built}
    for key,p in old_by_key.items():
        if key in new_keys:continue
        has_manual=p.holes or p.fillets or p.chamfers or any(not o.get('config_auto') for o in (*p.operations,*p.machining)) or any(h.get('owner_id')==p.id for h in retained)
        if has_manual:raise ValueError('Zmiana usuwa część „'+p.name+'” z ręczną obróbką lub okuciem. Zachowaj tę część albo usuń jej dodatki świadomie w zwykłym edytorze.')
    mapping={p.id:old_by_key[p.configurator_source['key']].id for p in built if p.configurator_source.get('key') in old_by_key}
    _remap(result,mapping);built=[p for p in result.parts if p.group==group]
    for p in built:
        p.layout=copy.deepcopy(layout);old=old_by_key.get(p.configurator_source.get('key'))
        if old:
            if p.configurator_source.get('key')!='carcass.top' or old.name not in ('Wieniec','Listwa górna przednia'):p.name=old.name
            p.visible=old.visible;p.grain=old.grain;p.edges=copy.deepcopy(old.edges);p.contour_edge=old.contour_edge
            material_key='front_material' if p.role in ('door','drawer') else 'material'
            if old_config.get(material_key)==c[material_key]:p.material=old.material
            p.holes=copy.deepcopy(old.holes);p.fillets=copy.deepcopy(old.fillets);p.chamfers=copy.deepcopy(old.chamfers)
            if not legacy:
                p.operations.extend(copy.deepcopy(o) for o in old.operations if not o.get('config_auto'))
                p.machining.extend(copy.deepcopy(o) for o in old.machining if not o.get('config_auto'))
    for p in manual:
        p.group=group
        if p.assembly.get('group'):p.assembly['group']=group
    result.parts.extend(manual);result.hardware.extend(retained)
    for p in result.parts:
        if p.group!=group:continue
        if p.assembly.get('group'):p.assembly['group']=group
        for op in p.operations:
            if op.get('connection_tag')=='connector:'+str(original_group):op['connection_tag']='connector:'+group
    for item in result.hardware:
        if item.get('owner_id') in {p.id for p in existing} or item.get('group')==original_group:
            if 'group' in item:item['group']=group
            if item.get('connection_tag')=='connector:'+str(original_group):item['connection_tag']='connector:'+group
    managed_ids={p.id for p in built}
    for item in result.hardware:
        if item.get('owner_id') not in managed_ids:continue
        old_item=next((h for h in project.hardware if h.get('owner_id')==item.get('owner_id') and h.get('kind')==item.get('kind') and (h.get('config_auto') or legacy)),None)
        if old_item:
            for key in ('price','article','manufacturer','supplier','note'):
                price_key={'leg':'leg_price','led_strip':'led_price_per_m','led_profile':'led_profile_price_per_m','led_diffuser':'led_diffuser_price_per_m','led_power':'led_source_price'}.get(item.get('kind'))
                if key=='price' and price_key and old_config.get(price_key)!=c.get(price_key):continue
                if key in ('article','manufacturer') and any(old_item.get(k)!=item.get(k) for k in ('runner_profile','source','spacing','recipe_id')):continue
                if key in old_item:item[key]=copy.deepcopy(old_item[key])
    for p in manual:
        lo,hi=bounds(p)
        for q in built:
            a,b=bounds(q)
            if all(min(hi[i],b[i])-max(lo[i],a[i])>.01 for i in range(3)):
                raise ValueError('Nowa konstrukcja koliduje z ręcznym elementem „'+p.name+'”. Przesuń dodatek przed przebudową.')
    manifest={p.configurator_source['key']:dict(id=p.id,signature=_geometry(p)) for p in built}
    anchor=next(p for p in built if p.configurator_source.get('key')=='carcass.left')
    anchor.configurator_source.update(config=copy.deepcopy(c),manifest=manifest)
    from workspace_worktops import refresh_shared_worktops
    result=refresh_shared_worktops(result,changed_groups={original_group,group},validate=False)
    from drag_hardware import refresh_manual_attachments
    refresh_manual_attachments(result)
    result.validate();return result,group


def infer_config(project,group):
    """Recognise an unchanged library cabinet for an explicitly requested conversion."""
    parts=[p for p in project.parts if p.group==group]
    if not parts:raise ValueError('Wybierz mebel do rozpoznania.')
    if any(p.style_source or p.contour or p.profile or p.decor or p.fillets or p.chamfers or p.machining for p in parts):
        raise ValueError('Ten mebel zawiera własną geometrię lub obróbki. Konwertowanie mogłoby ją nadpisać; utwórz nowy korpus w kreatorze.')
    try:f=frame(project,group)
    except ValueError:raise ValueError('Nie rozpoznano bibliotecznego korpusu. Utwórz nowy mebel w kreatorze.') from None
    lo,hi=f['lower'],f['upper'];w,d,h=(hi[i]-lo[i] for i in range(3));left=f['left']
    if left is None:raise ValueError('Nie rozpoznano boku bibliotecznego korpusu.')
    material=left.material;front_material=next((p.material for p in parts if inferred_role(p) in ('door','drawer')),material)
    for index,template in enumerate(TEMPLATES):
        if template[5] not in SUPPORTED or template_options(index):continue
        try:
            clean=Project(materials=copy.deepcopy(project.materials));expected,_=create_furniture(clean,index,w,h,d,left.t,origin=lo,group=group,material=material,front_material=front_material)
        except ValueError:continue
        expected_parts={p.name:p for p in expected.parts}
        if len(expected_parts)!=len(parts) or {p.name for p in parts}!=set(expected_parts):continue
        # Legacy structural roles were inferred from names; geometry is compared
        # after normalising only that legacy role information.
        matches=True
        for p in parts:
            q=copy.deepcopy(p);e=copy.deepcopy(expected_parts[p.name]);q.role=e.role='other';q.decor=e.decor={}
            if _geometry(q)!=_geometry(e):matches=False;break
            def operation_signature(op):return {k:v for k,v in op.items() if k not in ('owner_id','opening_owner','batch')}
            if [operation_signature(op) for op in p.operations]!=[operation_signature(op) for op in e.operations]:matches=False;break
        if not matches:continue
        expected_owners={p.id:p.name for p in expected.parts};actual_owners={p.id:p.name for p in parts}
        def hardware_signature(items,owners):
            result=[]
            for item in items:
                if item.get('owner_id') not in owners:continue
                data={k:v for k,v in item.items() if k not in ('owner_id','anchors','price','article','manufacturer','supplier','note')}
                data['owner_name']=owners[item['owner_id']];result.append(json.dumps(data,sort_keys=True))
            return sorted(result)
        if hardware_signature(project.hardware,actual_owners)!=hardware_signature(expected.hardware,expected_owners):continue
        c=default_config(index,project);c.update(name=group,width=w,height=h+(38 if template[5]=='island' else 0),depth=d,thickness=left.t,
                                               front_thickness=next((p.t for p in parts if inferred_role(p) in ('door','drawer')),left.t),leg_height=0,
                                               material=material,front_material=front_material,origin=list(lo))
        return normalize_config(c,project)
    raise ValueError('Ten korpus różni się od niezmienionego wzorca biblioteki. Nie przebudowano ręcznych części; dodaj nowy mebel w kreatorze.')
