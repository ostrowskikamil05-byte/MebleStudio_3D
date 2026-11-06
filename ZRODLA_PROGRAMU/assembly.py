"""Explicit, editable construction recipes. No machine or vendor certification."""
import copy
import math
from core import Part, finite, faces, uid,local_world

ROLES = {'Inny / swobodny element':'other','Bok lewy':'left','Bok prawy':'right',
         'Ścianka / przegroda':'partition','Góra / wieniec':'top','Dół / dno':'bottom',
         'Blat':'worktop','Drzwiczki':'door','Półka':'shelf','Plecy':'back',
         'Szuflada z frontem':'drawer'}
DEFAULTS = dict(gap=2,level=300,offset=0,hinge_side='left',hinge_count=2,
    hinge_end=100,cup_diameter=35,cup_depth=12.5,cup_edge=5,
    plate_line=37,plate_spacing=32,screw_diameter=3,screw_depth=10,
    drawer_clearance=13,drawer_depth=400,drawer_height=120,drawer_t=16,
    drawer_bottom=6,drawer_raise=0,drawer_front_gap=20,slide_line=25,
    slide_first=37,slide_last=250,shelf_end=40,shelf_diameter=5,shelf_depth=10,
    hinge_price=0,slide_price=0,shelf_price=0,auto_hardware=True)
PARAMETERS = [
    ('hinge_count','Liczba zawiasów'),('hinge_end','Zawias od końca frontu [mm]'),
    ('cup_diameter','Średnica puszki [mm]'),('cup_depth','Głębokość puszki [mm]'),
    ('cup_edge','Puszka: od krawędzi do obwodu [mm]'),
    ('plate_line','Linia prowadnika od przodu korpusu [mm]'),
    ('plate_spacing','Rozstaw wkrętów prowadnika [mm]'),
    ('screw_diameter','Średnica otworów wkrętów [mm]'),('screw_depth','Głębokość otworów wkrętów [mm]'),
    ('hinge_price','Cena zawiasu z prowadnikiem [zł]'),
    ('drawer_clearance','Luz prowadnicy na stronę [mm]'),
    ('drawer_depth','Głębokość skrzynki [mm]'),('drawer_height','Wysokość skrzynki [mm]'),
    ('drawer_t','Grubość ścian skrzynki [mm]'),('drawer_bottom','Grubość dna skrzynki [mm]'),
    ('drawer_raise','Podniesienie skrzynki nad frontem [mm]'),
    ('drawer_front_gap','Skrzynka od przodu korpusu [mm]'),
    ('slide_line','Linia prowadnicy nad dnem skrzynki [mm]'),
    ('slide_first','Pierwszy wkręt prowadnicy od przodu [mm]'),
    ('slide_last','Drugi wkręt prowadnicy od przodu [mm]'),
    ('slide_price','Cena pary prowadnic [zł]'),
    ('shelf_end','Podpórka od końca półki [mm]'),
    ('shelf_diameter','Średnica otworu podpórki [mm]'),('shelf_depth','Głębokość otworu podpórki [mm]'),
    ('shelf_price','Cena podpórki [zł]')]

def inferred_role(part):
    if part.role!='other':return part.role
    if part.name.startswith('Front '):return 'door'
    if part.name.startswith('Półka '):return 'shelf'
    return {'Bok lewy':'left','Bok prawy':'right','Dno':'bottom','Wieniec':'top',
            'Blat':'worktop','Plecy nakładane HDF':'back'}.get(part.name,'other')

def bounds(part):
    # Assembly recipes always work in the cabinet's construction coordinates.
    vertices=[local_world(part,u,v,t) for u,v in part.points() for t in (0,part.t)]
    return tuple(min(p[i] for p in vertices) for i in range(3)),tuple(max(p[i] for p in vertices) for i in range(3))

def frame(project,group,exclude=None,bay=None):
    if bay:
        if not isinstance(bay,dict):raise ValueError('Nieprawidłowe odniesienie komory mebla.')
        members={p.id:p for p in project.parts if p.group==group}
        try:left,right,bottom,top=(members[bay[key]] for key in ('left','right','bottom','top'))
        except (KeyError,TypeError):raise ValueError('Usunięto bok lub przegrodę komory. Przebuduj mebel w kreatorze.') from None
        if left.plane!='YZ' or right.plane!='YZ' or bottom.plane!='XY' or top.plane!='XY':raise ValueError('Komora wymaga pionowych boków i poziomych przegród.')
        if any(abs(a)>1e-8 for p in (left,right,bottom,top) for a in (p.rx,p.ry,p.rz)):raise ValueError('Boki komory wymagają zerowych lokalnych obrotów.')
        lo=(left.x+(left.t/2 if bay.get('left_mid') else 0),max(left.y,right.y),bottom.z+(bottom.t/2 if bay.get('bottom_mid') else 0))
        hi=(right.x+(right.t/2 if bay.get('right_mid') else right.t),min(left.y+left.u,right.y+right.u),top.z+(top.t/2 if bay.get('top_mid') else top.t))
        if any(b<=a for a,b in zip(lo,hi)):raise ValueError('Komora nie ma dodatnich wymiarów.')
        return dict(lower=lo,upper=hi,left=left,right=right,inner_left=left.x+left.t,inner_right=right.x,
                    inner_bottom=bottom.z+bottom.t,inner_top=top.z,parts=list({p.id:p for p in (left,right,bottom,top)}.values()))
    parts=[p for p in project.parts if p.group==group and p.id!=exclude
           and not p.assembly.get('generated_by') and inferred_role(p) not in ('door','drawer','back','worktop','shelf','accessory_panel')]
    if not parts:raise ValueError('Wybrany mebel nie ma konstrukcji odniesienia. Dodaj najpierw korpus lub własne boki.')
    if any(abs(angle)>1e-8 for p in parts for angle in (p.rx,p.ry,p.rz)):
        raise ValueError('Automatyczne dopasowanie wymaga korpusu bez obrotów. Dla obróconej konstrukcji wybierz element swobodny.')
    boxes=[bounds(p) for p in parts]
    lower=tuple(min(b[0][i] for b in boxes) for i in range(3))
    upper=tuple(max(b[1][i] for b in boxes) for i in range(3))
    left=min((p for p in parts if inferred_role(p)=='left'),key=lambda p:p.x,default=None)
    right=max((p for p in parts if inferred_role(p)=='right'),key=lambda p:p.x,default=None)
    # Legacy/custom side panels can also be identified geometrically.
    vertical=[p for p in parts if p.plane=='YZ']
    if left is None:left=min(vertical,key=lambda p:p.x,default=None)
    if right is None:right=max(vertical,key=lambda p:p.x,default=None)
    inner_left=left.x+left.t if left else lower[0]
    inner_right=right.x if right and right is not left else upper[0]
    bottoms=[p for p in parts if inferred_role(p)=='bottom']
    tops=[p for p in parts if inferred_role(p)=='top']
    inner_bottom=max((p.z+p.t for p in bottoms),default=lower[2])
    inner_top=min((p.z for p in tops),default=upper[2])
    return dict(lower=lower,upper=upper,left=left,right=right,inner_left=inner_left,
                inner_right=inner_right,inner_bottom=inner_bottom,inner_top=inner_top,parts=parts)

def frame_for_part(project,part):
    if part.assembly.get('config',{}).get('corner_bay'):
        from corner_furniture import corner_front_frame
        return corner_front_frame(project,part)
    return frame(project,part.group,bay=part.assembly.get('config',{}).get('bay'))

def recipe(values=None):
    config={**DEFAULTS,**(values or {})}
    for key in DEFAULTS:
        if key in ('hinge_side','auto_hardware'):continue
        config[key]=finite(config[key],0,10000 if not key.endswith('_price') else 1e6)
    if config['hinge_side'] not in ('left','right'):raise ValueError('Wybierz lewą lub prawą stronę zawiasów.')
    if config['hinge_count']!=int(config['hinge_count']) or not 2<=config['hinge_count']<=8:
        raise ValueError('Liczba zawiasów: od 2 do 8.')
    config['hinge_count']=int(config['hinge_count'])
    return config

def suggested_size(project,group,role,thickness,config=None,exclude=None):
    c=recipe(config);f=frame(project,group,exclude,bay=c.get('bay'))
    lo,hi=f['lower'],f['upper'];width=hi[0]-lo[0];depth=hi[1]-lo[1];height=hi[2]-lo[2]
    inner=f['inner_right']-f['inner_left'];gap=c['gap']
    if role in ('left','right'):return depth,height
    if role=='partition':return depth,f['inner_top']-f['inner_bottom']
    if role in ('top','bottom'):return inner,depth
    if role=='shelf':return inner-2*gap,depth-2*gap
    if role=='worktop':return width,depth
    if role=='door':return width-2*gap,height-2*gap
    if role=='drawer':return width-2*gap,c['drawer_height']+c['drawer_bottom']+2*gap+20
    if role=='back':return width,height
    raise ValueError('Dopasowanie jest dostępne dla elementów konstrukcyjnych.')

def clear_generated(project,owner_id):
    generated={p.id for p in project.parts if p.assembly.get('generated_by')==owner_id}
    removed=generated|{owner_id}
    project.parts[:]=[p for p in project.parts if p.id not in generated]
    project.hardware[:]=[h for h in project.hardware if h.get('owner_id') not in removed]
    for p in project.parts:
        p.operations[:]=[op for op in p.operations if op.get('owner_id') not in removed]

def delete_with_connections(project,part_id):
    affected=[p.id for p in project.parts if part_id in p.assembly.get('anchors',[])]
    for owner in affected:
        clear_generated(project,owner)
        p=next((p for p in project.parts if p.id==owner),None)
        if p:p.assembly={};p.role='other'
    clear_generated(project,part_id)
    project.parts[:]=[p for p in project.parts if p.id!=part_id]

def drill(part,owner,u,v,diameter,depth,face,purpose):
    if diameter<=0 or depth<=0:raise ValueError('Średnica i głębokość wiercenia muszą być dodatnie.')
    if depth>=part.t:raise ValueError(f'{part.name}: otwór {purpose} wymaga mniejszej głębokości niż grubość elementu.')
    operation=dict(type='drill',u=u,v=v,diameter=diameter,depth=depth,face=face,
                   owner_id=owner.id,purpose=purpose,requires_review=True)
    part.operations.append(operation)
    try:part.validate()
    except ValueError as e:raise ValueError(f'{part.name}, {purpose}: {e}') from e

def accessory(project,owner,name,qty,price,anchors,positions):
    project.hardware.append(dict(name=name,qty=qty,price=price,owner_id=owner.id,
        anchors=[p.id for p in anchors],positions=positions,requires_review=True,recipe='generic-editable-1'))

def rectangular(part):
    return abs(part.area()*1e6-part.u*part.v)<1e-5

def require_sides(f):
    left,right=f['left'],f['right']
    if not left or not right or left.id==right.id:
        raise ValueError('Automatyczne okucia wymagają dwóch boków korpusu. Nadaj im funkcje Bok lewy i Bok prawy.')
    return left,right

def set_placement(q,f,c):
    lo,hi=f['lower'],f['upper'];gap=c['gap'];role=q.role
    q.rx=q.ry=q.rz=0
    if role in ('left','right','partition'):
        q.plane='YZ';q.x=lo[0] if role=='left' else hi[0]-q.t if role=='right' else f['inner_left']+c['offset']
        q.y,q.z=lo[1],lo[2]
        if role=='partition':q.z=f['inner_bottom']
    elif role in ('top','bottom','worktop','shelf'):
        q.plane='XY';q.x=f['inner_left']+(gap if role=='shelf' else 0);q.y=lo[1]+(gap if role=='shelf' else 0)
        q.z=hi[2]-q.t if role=='top' else lo[2] if role=='bottom' else hi[2] if role=='worktop' else lo[2]+c['level']
        if role=='worktop':q.x=lo[0]
    else:
        q.plane='XZ';q.x=lo[0]+(gap if role in ('door','drawer') else 0)
        q.y=lo[1]-q.t-c.get('opening_gap',gap) if role in ('door','drawer') else hi[1]
        q.z=lo[2]+(gap if role=='door' else c['level'] if role=='drawer' else 0)
        if role=='door' and c['hinge_side']=='right':q.x=hi[0]-gap-q.u
        if role=='door':q.y-=c.get('slide_lane',0)
    # Sketch dimensions are authoritative; never silently rescale them.
    if role in ('shelf','top','bottom','partition'):
        if q.x<f['inner_left']-1e-6 or q.x+(q.t if role=='partition' else q.u)>f['inner_right']+1e-6:
            raise ValueError('Element nie mieści się między bokami. Użyj „Dopasuj prostokąt do mebla” lub zmień szkic.')
    if role=='shelf' and not f['inner_bottom']<=q.z<=f['inner_top']-q.t:
        raise ValueError('Wysokość półki wykracza poza wnętrze korpusu.')
    if role in ('door','drawer') and (q.x+q.u>hi[0]-gap+1e-6 or q.z+q.v>hi[2]-gap+1e-6):
        raise ValueError('Front wykracza poza mebel. Zmień jego wymiary lub wysokość montażu.')

def check_overlap(project,q):
    lo,hi=bounds(q)
    for p in project.parts:
        if p.id==q.id or p.group!=q.group or p.assembly.get('generated_by')==q.id:continue
        a,b=bounds(p)
        if all(min(hi[i],b[i])-max(lo[i],a[i])>0.01 for i in range(3)):
            raise ValueError(f'Element koliduje z „{p.name}”. Dla boku/góry/dna zaznacz zastępowanie, albo edytuj istniejący element.')

def hinges(project,q,f,c):
    left,right=require_sides(f);support=left if c['hinge_side']=='left' else right
    # An overlay door must reach the chosen carcass side.
    edge=q.x if c['hinge_side']=='left' else q.x+q.u
    expected=f['lower'][0]+c['gap'] if c['hinge_side']=='left' else f['upper'][0]-c['gap']
    if abs(edge-expected)>0.1:raise ValueError('Krawędź zawiasowa frontu musi być przy wybranym boku korpusu.')
    count=c['hinge_count'];diameter=c['cup_diameter'];end=min(c['hinge_end'],q.v/4)
    if q.v < diameter*2+8:raise ValueError('Front jest za niski dla dwóch puszek zawiasów Ø35.')
    if q.v<=2*end:raise ValueError('Front jest za niski dla odległości zawiasów od końców.')
    cup=c['cup_edge']+diameter/2
    u=cup if c['hinge_side']=='left' else q.u-cup
    positions=[];hinge_spec={}
    for i in range(count):
        v=end+(q.v-2*end)*i/(count-1)
        drill(q,q,u,v,diameter,c['cup_depth'],'top','puszka zawiasu')
        if c.get('hinge_pattern'):
            from fittings import recipe
            r=recipe(c['hinge_pattern']);hinge_spec=r;pilot_u=u-r['offset'] if c['hinge_side']=='left' else u+r['offset']
            if not r.get('min_thickness',0)<=q.t<=r.get('max_thickness',10000):raise ValueError('Grubość frontu nie mieści się w zakresie wybranego zawiasu.')
            for pilot_v in (v-r['pitch']/2,v+r['pitch']/2):
                drill(q,q,pilot_u,pilot_v,r['pilot'],r['pilot_depth'],'top','mocowanie puszki zawiasu')
        z=q.z+v
        for delta in (-c['plate_spacing']/2,c['plate_spacing']/2):
            drill(support,q,f['lower'][1]+c['plate_line']-support.y,z+delta-support.z,
                  c['screw_diameter'],c['screw_depth'],'top' if support is left else 'bottom','prowadnik zawiasu')
        positions.append(dict(x=edge,y=f['lower'][1],z=z))
    accessory(project,q,'Zawias puszkowy + prowadnik — wzorzec roboczy',count,c['hinge_price'],[support],positions)
    spring=hinge_spec.get('closing_spring',c.get('hinge_spring','unknown'))
    if spring=='sprung' and c.get('hinge_spring')=='unsprung':
        raise ValueError('Wybrany zawias ma sprężynę. Wybierz zawias bez sprężyny dla magnetycznego wypychacza.')
    project.hardware[-1].update(kind='hinge',levels=[end+(q.v-2*end)*i/(count-1) for i in range(count)],color='#aab0b6',closing_spring=spring)
    if hinge_spec.get('article'):
        project.hardware[-1].update(article=hinge_spec['article'],manufacturer=hinge_spec.get('manufacturer',''),source=hinge_spec['source'])

def shelf_supports(project,q,f,c):
    left,right=require_sides(f);end=c['shelf_end'];positions=[]
    if q.v<=2*end:raise ValueError('Półka jest za płytka dla rozstawu podpórek.')
    if abs(q.x-f['inner_left']-c['gap'])>.1 or abs(q.x+q.u-f['inner_right']+c['gap'])>.1:
        raise ValueError('Półka z podpórkami musi sięgać obu boków z wybranym luzem.')
    for side,face in ((left,'top'),(right,'bottom')):
        for v in (end,q.v-end):
            z=q.z-c['shelf_diameter']/2
            drill(side,q,q.y+v-side.y,z-side.z,c['shelf_diameter'],c['shelf_depth'],face,'podpórka półki')
            positions.append(dict(x=side.x+side.t if side is left else side.x,y=q.y+v,z=z))
    accessory(project,q,'Podpórka półki — wzorzec roboczy',4,c['shelf_price'],[left,right],positions)

def drawer_box(project,q,f,c):
    left,right=require_sides(f)
    if not rectangular(q):raise ValueError('Skrzynka szuflady wymaga prostokątnego frontu.')
    width=f['inner_right']-f['inner_left']-2*c['drawer_clearance']
    depth,height,t,bottom=(c[k] for k in ('drawer_depth','drawer_height','drawer_t','drawer_bottom'))
    from runner_installation import drawer_depth_context
    depth_context=drawer_depth_context(project,q,c)
    plan=None
    if c.get('runner_profile'):
        from runners_catalog import calculate_runner
        plan=calculate_runner(c['runner_profile'],f['inner_right']-f['inner_left'],c['runner_nominal'],t,
            depth_context['usable_depth'],construction='four_sided',drawer_front_thickness=t,
            confirmed_min_depth=c.get('runner_confirmed_min_depth'))
        if not plan['valid']:raise ValueError('\n'.join(plan['errors']))
        width=plan['drawer_outer_width'];depth=plan['drawer_length'];c['drawer_clearance']=plan['external_clearance_per_side'];c['drawer_depth']=depth
    if min(width-2*t,depth-2*t,height,t,bottom)<=0:raise ValueError('Nieprawidłowe wymiary skrzynki szuflady.')
    x=f['inner_left']+c['drawer_clearance'];y=depth_context['start_y'];z=q.z+c['gap']+c['drawer_raise']
    if y+depth>depth_context['end_y']+1e-8 or z<f['inner_bottom'] or z+bottom+height>f['inner_top']:
        raise ValueError('Skrzynka szuflady nie mieści się w korpusie. Zmień wysokość, głębokość lub poziom montażu.')
    if z+bottom+height>q.z+q.v:raise ValueError('Front jest za niski dla wysokości skrzynki.')
    # Check occupied volume against shelves and partitions already present.
    for p in project.parts:
        if p.group!=q.group or p.id==q.id or p.assembly.get('generated_by')==q.id:continue
        if inferred_role(p) in ('left','right','top','bottom','door'):continue
        lo,hi=bounds(p)
        if all(min(a,b)-max(c0,d)>0.01 for a,b,c0,d in zip((x+width,y+depth,z+bottom+height),hi,(x,y,z),lo)):
            raise ValueError(f'Skrzynka szuflady koliduje z elementem „{p.name}”. Przesuń go lub zmień poziom szuflady.')
    children=[]
    def child(name,u,v,thick,plane,px,py,pz,material=None):
        p=Part(name=q.name+' / '+name,group=q.group,u=u,v=v,t=thick,plane=plane,
               x=px,y=py,z=pz,material=material or q.material,role='drawer_box',
               assembly=dict(generated_by=q.id),layout=copy.deepcopy(q.layout))
        p.validate();project.parts.append(p);children.append(p);return p
    bottom_material=next((m.id for m in project.materials if m.kind=='hdf'),q.material)
    if plan and plan['mount']=='undermount':
        recess=max(plan['bottom_recess_min'] or 0,c.get('drawer_bottom_recess',12))
        if plan['bottom_recess_max'] is not None and recess>plan['bottom_recess_max']:
            raise ValueError(f'Cofnięcie dna: dla tej serii dopuszczalne maksymalnie {plan["bottom_recess_max"]:g} mm.')
        if height-recess-bottom<=0:raise ValueError('Bok szuflady jest za niski dla cofnięcia dna i jego grubości.')
        a=child('bok lewy',depth,height,t,'YZ',x,y,z)
        b=child('bok prawy',depth,height,t,'YZ',x+width-t,y,z)
        child('przód skrzynki',width-2*t,height-recess-bottom,t,'XZ',x+t,y,z+recess+bottom)
        child('tył skrzynki',width-2*t,height-recess-bottom,t,'XZ',x+t,y+depth-t,z+recess+bottom)
        child('dno wpuszczone — model skrzynki',width-2*t,depth-2*t,bottom,'XY',x+t,y+t,z+recess,bottom_material)
    else:
        a=child('bok lewy',depth,height,t,'YZ',x,y,z+bottom)
        b=child('bok prawy',depth,height,t,'YZ',x+width-t,y,z+bottom)
        child('przód skrzynki',width-2*t,height,t,'XZ',x+t,y,z+bottom)
        child('tył skrzynki',width-2*t,height,t,'XZ',x+t,y+depth-t,z+bottom)
        child('dno nakładane',width,depth,bottom,'XY',x,y,z,bottom_material)
    q.assembly['children']=[p.id for p in children]
    if plan:
        from runners_catalog import PROFILES
        profile=PROFILES[c['runner_profile']]
        positions=[dict(x=x,y=y,z=z),dict(x=x+width,y=y,z=z)] if plan['mount']=='undermount' else [dict(x=side.x+side.t if side is left else side.x,y=y,z=z+bottom+c['slide_line']) for side in (left,right)]
        accessory(project,q,profile['label']+' — para',1,c['slide_price'],[left,right],positions)
        project.hardware[-1].update(kind='runner',mount=plan['mount'],length=c['runner_nominal'],closed_front_y=q.y,color='#aab0b6',runner_profile=c['runner_profile'],dimensions=plan,source=plan.get('variant_source') or plan['sources'][0],requires_review=True,
            installation_status='Wymiary planowania; wiercenia mocujące, zaczepy, rowki i nośność wymagają doboru wariantu.')
        return
    if not c['auto_hardware']:return
    if c['slide_first']>=c['slide_last'] or c['slide_last']>=depth:
        raise ValueError('Otwory prowadnicy muszą być kolejno rozmieszczone wewnątrz głębokości skrzynki.')
    if not 0<c['slide_line']<height:raise ValueError('Linia prowadnicy musi leżeć na boku skrzynki.')
    positions=[]
    for side,box,face,box_face in ((left,a,'top','bottom'),(right,b,'bottom','top')):
        for distance in (c['slide_first'],c['slide_last']):
            level=z+bottom+c['slide_line'];world_y=y+distance
            drill(side,q,world_y-side.y,level-side.z,c['screw_diameter'],c['screw_depth'],face,'prowadnica korpusu')
            drill(box,q,distance,c['slide_line'],c['screw_diameter'],c['screw_depth'],box_face,'prowadnica skrzynki')
        positions.append(dict(x=side.x+side.t if side is left else side.x,y=y,z=z+bottom+c['slide_line']))
    accessory(project,q,'Para prowadnic bocznych — wzorzec roboczy',1,c['slide_price'],[left,right],positions)
    project.hardware[-1].update(kind='runner',length=depth,closed_front_y=q.y,color='#aab0b6')

def attach(project,part,group,role,config=None):
    """Return an atomic candidate project; validation failure never mutates input."""
    candidate=copy.deepcopy(project);q=copy.deepcopy(part);c=recipe(config)
    retained_hardware=[copy.deepcopy(h) for h in candidate.hardware if h.get('owner_id')==q.id and h.get('manual')]
    saved_features={p.name:{key:copy.deepcopy(getattr(p,key)) for key in ('fillets','chamfers','machining','holes','operations')} for p in candidate.parts if p.assembly.get('generated_by')==q.id}
    clear_generated(candidate,q.id)
    f=frame(candidate,group,bay=c.get('bay')) if role!='other' else None
    candidate.parts[:]=[p for p in candidate.parts if p.id!=q.id]
    q.operations=[op for op in q.operations if op.get('owner_id')!=q.id]
    q.group=group;q.role=role
    reference=next((p for p in candidate.parts if p.group==group and p.id!=q.id),None)
    if reference:q.layout=copy.deepcopy(reference.layout)
    if role=='other':
        q.assembly={};candidate.parts.append(q);candidate.validate();return candidate,q
    if role not in ROLES.values():raise ValueError('Nieznana funkcja elementu.')
    set_placement(q,f,c)
    check_overlap(candidate,q)
    q.assembly=dict(group=group,role=role,config=c,anchors=[p.id for p in f['parts'] if p.id!=q.id],requires_review=True)
    candidate.parts.append(q)
    if role=='door' and c['auto_hardware']:hinges(candidate,q,f,c)
    elif role=='shelf' and c['auto_hardware']:shelf_supports(candidate,q,f,c)
    elif role=='drawer':drawer_box(candidate,q,f,c)
    for p in candidate.parts:
        if p.assembly.get('generated_by')==q.id and p.name in saved_features:
            for key,value in saved_features[p.name].items():
                if key=='operations':p.operations.extend(op for op in value if not op.get('owner_id'))
                else:setattr(p,key,value)
    candidate.hardware.extend(retained_hardware)
    from fittings import refresh_handles
    refresh_handles(candidate,q.id)
    from mechanisms import refresh_mechanism_hardware
    refresh_mechanism_hardware(candidate,q.id)
    from opening_options import refresh_openings
    refresh_openings(candidate,q.id)
    candidate.validate();return candidate,q

def rebuild_dependents(project,changed_id):
    result=copy.deepcopy(project)
    owners=[p.id for p in result.parts if changed_id in p.assembly.get('anchors',[])]
    for owner in owners:
        p=next(p for p in result.parts if p.id==owner)
        if p.assembly.get('config',{}).get('corner_bay'):
            from corner_furniture import regenerate_corner_hinges
            cfg=p.assembly['config'];result=regenerate_corner_hinges(result,p.id,cfg['hinge_pattern'],cfg['hinge_count'],validate=False)
        else:result,_=attach(result,p,p.group,p.role,p.assembly.get('config'))
    from drag_hardware import refresh_manual_attachments
    refresh_manual_attachments(result)
    result.validate();return result


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_construction as _install_cpp
_install_cpp(globals(), 'assembly')
