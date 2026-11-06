"""Editable open corner carcasses with real planar production parts.

The concave inner corner can remain square, be chamfered or be rounded.
Rounded outlines are explicitly tessellated; no diagonal hinge standard is
invented and this factory does not automatically attach a diagonal front.
"""
import copy
import math
from core import Part,finite,validate_polygon,local_world
from furniture_configurator import _geometry
from furniture_layout import group_layout


def default_corner_config(project=None,furniture_type='cabinet'):
    wardrobe=furniture_type=='wardrobe'
    return dict(name='Szafa narożna' if wardrobe else 'Korpus narożny',furniture_type=furniture_type,width=900,depth=900,height=2100 if wardrobe else 720,arm_depth=560,arm_depth_y=560,
                thickness=18,shape='L',side='left',radius=100,segments=12,
                shelves=4 if wardrobe else 1,top_mode='panel',rail_width=80,origin=[0,0,0],diagonal_angle=45,
                fronts='open',front_thickness=18,gap=2,hinge_pattern='hinge45',hinge_count=4 if wardrobe else 2,opening_angle=100,
                handle_mode='handle',handle_spacing=128,front_style='smooth',
                leg_height=0,leg_count=6,leg_diameter=40,leg_setback=60,leg_color='#242628',leg_price=0,
                material=project.materials[0].id if project else '',front_material=project.materials[min(1,len(project.materials)-1)].id if project else '')


def normalize_corner_config(config,project=None):
    if not isinstance(config,dict):raise ValueError('Podaj parametry narożnika.')
    c=default_corner_config(project,config.get('furniture_type','cabinet'));c.update(copy.deepcopy(config))
    if 'arm_depth_y' not in config:c['arm_depth_y']=c['arm_depth']
    if not isinstance(c['name'],str) or not c['name'].strip():raise ValueError('Wpisz nazwę narożnika.')
    c['name']=c['name'].strip()
    for key,low,high in (('width',150,10000),('depth',150,10000),('height',100,10000),('arm_depth',80,3000),('arm_depth_y',80,3000),
                         ('thickness',6,60),('radius',0,1500),('rail_width',20,300),('diagonal_angle',15,75),
                         ('front_thickness',14,40),('gap',.5,20),('opening_angle',0,170),('handle_spacing',0,1000)):
        c[key]=finite(c[key],low,high)
    for key,low,high in (('leg_height',0,500),('leg_diameter',15,100),('leg_setback',10,200),('leg_price',0,100000)):
        c[key]=finite(c[key],low,high)
    if c['leg_count'] not in (4,6):raise ValueError('Wybierz 4 albo 6 nóg narożnika.')
    for key,low,high in (('shelves',0,8),('segments',4,24),('hinge_count',2,8)):
        value=finite(c[key],low,high)
        if value!=int(value):raise ValueError('Liczba '+key+' musi być całkowita.')
        c[key]=int(value)
    if c['shape'] not in ('L','chamfer','rounded'):raise ValueError('Narożnik: L, ścięty albo zaokrąglony.')
    if c['side'] not in ('left','right'):raise ValueError('Wybierz lewy albo prawy narożnik.')
    if c['top_mode'] not in ('panel','rails'):raise ValueError('Góra narożnika: pełna płyta albo listwy.')
    if c['furniture_type'] not in ('cabinet','wardrobe'):raise ValueError('Typ narożnika: szafka albo szafa.')
    if c['fronts'] not in ('open','orthogonal','diagonal'):raise ValueError('Fronty: otwarte, dwa niezależne albo skośny.')
    if c['fronts']=='orthogonal' and c['shape']!='L':raise ValueError('Dwa niezależne fronty wymagają naroża L. Dla ścięcia wybierz front skośny, dla zaokrąglenia otwarte półki.')
    if c['fronts']=='diagonal' and c['shape']!='chamfer':raise ValueError('Front skośny wymaga naroża ściętego.')
    if c['handle_mode'] not in ('handle','none'):raise ValueError('Uchwyt narożnika: standardowy albo brak.')
    from fittings import recipe
    try:hinge=recipe(c['hinge_pattern'])
    except StopIteration:raise ValueError('Wybierz wzorzec zawiasu narożnika.') from None
    if c['hinge_pattern'] not in ('hinge45','hinge52','hinge_sensys52'):raise ValueError('Generator narożnika obsługuje standardowy wzór zawiasu nakładanego.')
    if not isinstance(c['origin'],(list,tuple)) or len(c['origin'])!=3:raise ValueError('Pozycja narożnika wymaga X, Y i Z.')
    c['origin']=[finite(v) for v in c['origin']]
    if c['arm_depth']>=c['depth']-2*c['thickness']-10 or c['arm_depth_y']>=c['width']-2*c['thickness']-10:raise ValueError('Ramiona muszą być płytsze od gabarytów narożnika, z miejscem na boki.')
    if min(c['arm_depth'],c['arm_depth_y'])<=2*c['thickness']+20 or c['height']-c['leg_height']<=2*c['thickness']+30:raise ValueError('Narożnik jest za mały dla grubości płyt i nóg.')
    rise=c['radius']*math.tan(math.radians(c['diagonal_angle'])) if c['shape']=='chamfer' else c['radius']
    if c['shape']!='L' and (c['radius']>c['width']-c['arm_depth_y']-2 or rise>c['depth']-c['arm_depth']-2):
        raise ValueError('Promień / ścięcie nie mieści się pomiędzy końcami ramion.')
    if c['top_mode']=='rails' and c['rail_width']>=min(c['arm_depth'],c['arm_depth_y'])-c['thickness']:
        raise ValueError('Listwa jest zbyt szeroka dla ramienia narożnika.')
    if project and any(c[key] not in {m.id for m in project.materials} for key in ('material','front_material')):raise ValueError('Wybierz materiał narożnika i frontu.')
    from configurator_accessories import normalize_front_style
    c['front_style']=normalize_front_style(c['front_style'],c)['style']
    return c


def corner_outline(width,depth,arm_depth,shape='L',radius=100,side='left',segments=12,arm_depth_y=None,diagonal_angle=45):
    w,d,a=width,depth,arm_depth;b=arm_depth if arm_depth_y is None else arm_depth_y;points=[(0,0),(w,0),(w,a)]
    if shape=='L' or not radius:points.append((b,a))
    elif shape=='chamfer':points.extend(((b+radius,a),(b,a+radius*math.tan(math.radians(diagonal_angle)))))
    elif shape=='rounded':
        cx,cy=b+radius,a+radius
        points.extend((cx+radius*math.cos(-math.pi/2-i*math.pi/(2*segments)),cy+radius*math.sin(-math.pi/2-i*math.pi/(2*segments))) for i in range(segments+1))
    else:raise ValueError('Nieznany obrys narożnika.')
    points.extend(((b,d),(0,d)))
    if side=='right':points=[(w-x,y) for x,y in reversed(points)]
    return validate_polygon([(round(x,7),round(y,7)) for x,y in points])


def read_corner_config(project,group):
    part=next((p for p in project.parts if p.group==group and p.assembly.get('corner_source',{}).get('config')),None)
    return copy.deepcopy(part.assembly['corner_source']['config']) if part else None


def corner_bounds(project,group):
    config=read_corner_config(project,group)
    if config is None:return None
    lo=tuple(config['origin'])
    return dict(lower=lo,upper=(lo[0]+config['width'],lo[1]+config['depth'],lo[2]+config['height']))


def corner_status(project,group):
    parts=[p for p in project.parts if p.group==group];anchor=next((p for p in parts if p.assembly.get('corner_source',{}).get('manifest')),None)
    if not anchor:return dict(configured=False,editable=False,reason='Ten mebel nie pochodzi z kreatora narożników.')
    by_id={p.id:p for p in parts}
    for entry in anchor.assembly['corner_source']['manifest'].values():
        p=by_id.get(entry['id'])
        if p is None or _geometry(p)!=entry['signature']:return dict(configured=True,editable=False,reason='Geometrię narożnika zmieniono ręcznie. Automatyczna przebudowa jej nie nadpisze.')
    config=read_corner_config(project,group);reason='Dwa niezależne fronty z wzorcami zawiasów.' if config.get('fronts')=='orthogonal' else 'Front skośny bez automatycznego okucia; zawias dobierz osobno.' if config.get('fronts')=='diagonal' else 'Otwarty korpus narożny.'
    return dict(configured=True,editable=True,reason=reason)


def corner_front_frame(project,part):
    """The front's U/depth/Z frame, with explicit construction-space basis."""
    bay=part.assembly.get('config',{}).get('corner_bay')
    if not bay:raise ValueError('Front skośny nie ma automatycznej komory ani wzorca zawiasu.')
    members={p.id:p for p in project.parts};support=members.get(bay['support_id']);opposite=members.get(bay['opposite_id'])
    if support is None or opposite is None:raise ValueError('Usunięto bok lub słupek narożnika. Przebuduj korpus.')
    gap=part.assembly['config'].get('gap',2);origin=local_world(part,0,0,0)
    basis=[tuple(b-a for a,b in zip(origin,local_world(part,*point))) for point in ((1,0,0),(0,1,0),(0,0,1))]
    return dict(lower=(0,0,part.z-gap),upper=(part.u+2*gap,support.u,part.z+part.v+gap),left=support,right=opposite,
                inner_left=support.t,inner_right=part.u+2*gap-opposite.t,inner_bottom=part.z,inner_top=part.z+part.v,
                parts=[support,opposite],coordinate_system='front_local',basis=basis,origin=origin)


def _corner_hinges(project,front,pattern,count,price=0,manual=False):
    from fittings import recipe
    from features import hole_array
    count=finite(count,2,8)
    if count!=int(count):raise ValueError('Liczba zawiasów musi być całkowita.')
    count=int(count);price=finite(price,0,1000000);spec=recipe(pattern);bay=front.assembly['config'].get('corner_bay')
    if not bay:raise ValueError('Dla frontu skośnego dobierz rzeczywisty zawias osobno. Program nie tworzy jego automatycznych wierceń.')
    if pattern not in ('hinge45','hinge52','hinge_sensys52'):raise ValueError('Wybierz wzorzec standardowego zawiasu nakładanego.')
    if not spec.get('min_thickness',spec['depth']+1)<=front.t<=spec.get('max_thickness',10000):raise ValueError('Grubość frontu nie mieści się w zakresie wybranego zawiasu.')
    members={p.id:p for p in project.parts};support=members.get(bay['support_id'])
    if support is None:raise ValueError('Usunięto bok zawiasu narożnika.')
    current=local_world(front,0,0,front.t if bay['cup_face']=='top' else 0)
    if math.dist(current[:2],bay['hinge_point'])>.1:raise ValueError('Przesunięto krawędź zawiasową poza bok narożnika. Dopasuj front albo edytuj wiercenia ręcznie.')
    for p in project.parts:p.operations=[op for op in p.operations if op.get('corner_hinge_owner')!=front.id]
    project.hardware=[h for h in project.hardware if not(h.get('owner_id')==front.id and h.get('kind')=='hinge')]
    end=min(100,front.v/4);levels=[end+(front.v-2*end)*i/(count-1) for i in range(count)];cup=5+spec['diameter']/2;positions=[]
    def drill(part,u,v,diameter,depth,face,purpose):
        op=hole_array(part,u,v,diameter,depth,face=face)[0]
        op.update(owner_id=front.id,corner_hinge_owner=front.id,purpose=purpose,recipe_id=pattern,source=spec['source'],requires_review=True,config_auto=not manual,corner_auto=not manual)
        part.operations.append(op)
    for level in levels:
        drill(front,cup,level,spec['diameter'],spec['depth'],bay['cup_face'],'puszka zawiasu')
        for delta in (-spec['pitch']/2,spec['pitch']/2):drill(front,cup-spec['offset'],level+delta,spec['pilot'],spec['pilot_depth'],bay['cup_face'],'mocowanie puszki zawiasu')
        z=front.z+level
        for delta in (-16,16):drill(support,bay['support_u'],z+delta-support.z,3,min(10,support.t-2),bay['support_face'],'prowadnik zawiasu — wzorzec roboczy')
        point=local_world(front,cup,level,front.t if bay['cup_face']=='top' else 0);positions.append(dict(zip('xyz',point)))
    front.assembly['config'].update(hinge_side='left',hinge_pattern=pattern,hinge_count=count,cup_edge=5,cup_diameter=spec['diameter'],cup_depth=spec['depth'])
    project.hardware.append(dict(kind='hinge',owner_id=front.id,anchors=[support.id],name=spec['name']+' + prowadnik roboczy',qty=count,price=price,source=spec['source'],recipe_id=pattern,
                                 levels=levels,positions=positions,mount_face=bay['cup_face'],color='#aab0b6',requires_review=True,config_auto=not manual,corner_auto=not manual))
    front.validate();support.validate()


def regenerate_corner_hinges(project,part_id,pattern='hinge45',count=2,price=0,*,manual=False,validate=True):
    result=copy.deepcopy(project);front=next(p for p in result.parts if p.id==part_id)
    _corner_hinges(result,front,pattern,count,price,manual)
    if validate:result.validate()
    return result


def _front_finish(project,front,config):
    from features import hole_array
    cup_face=front.assembly.get('config',{}).get('corner_bay',{}).get('cup_face','top');outside='bottom' if cup_face=='top' else 'top'
    if config['handle_mode']=='handle' and config['front_style']!='finger':
        spacing=config['handle_spacing'];u=front.u-35;v=front.v/2
        if spacing>front.v-20:raise ValueError('Rozstaw uchwytu jest za duży dla wysokości frontu.')
        for level in ([v] if not spacing else [v-spacing/2,v+spacing/2]):
            op=hole_array(front,u,level,4.5,front.t,face=outside)[0];op.update(recipe_id='handle',purpose='Przelot M4 uchwytu',config_auto=True,corner_auto=True);front.operations.append(op)
        project.hardware.append(dict(kind='handle',owner_id=front.id,name='Gałka' if not spacing else f'Uchwyt {spacing:g} mm',qty=1,price=0,u=u,v=v,spacing=spacing,vertical=True,color='#242628',projection=28,mount_face=outside,config_auto=True,corner_auto=True))
    from configurator_accessories import apply_front_to_part
    # The style helper's reference surface is bottom. Mirror its surface names
    # temporarily so its opposed-drill depth guard also covers this orientation.
    if outside=='top':
        for op in front.operations:
            if op['face'] in ('top','bottom'):op['face']='bottom' if op['face']=='top' else 'top'
    apply_front_to_part(project,front,config['front_style'],config)
    if outside=='top':
        for op in front.operations:
            if op['face'] in ('top','bottom'):op['face']='bottom' if op['face']=='top' else 'top'
        for cut in front.machining:
            if cut.get('config_auto') and cut.get('face')=='bottom':cut['face']='top'
    for item in project.hardware:
        if item.get('owner_id')==front.id and item.get('config_auto'):item['corner_auto']=True


def fit_corner_part(project,part_id):
    """Explicitly restore this managed component to its corner parameters."""
    result=copy.deepcopy(project);part=next(p for p in result.parts if p.id==part_id);config=read_corner_config(result,part.group)
    if config is None:raise ValueError('Wybierz część narożnika.')
    anchor=next(p for p in result.parts if p.group==part.group and p.assembly.get('corner_source',{}).get('manifest'))
    key=part.assembly.get('corner_source',{}).get('key')
    anchor.assembly['corner_source']['manifest'][key]['signature']=_geometry(part)
    return create_corner(result,config,part.group)[0]


def create_corner(project,config,group=None):
    c=normalize_corner_config(config,project);result=copy.deepcopy(project);old_config=read_corner_config(project,group) if group else None
    existing=[p for p in project.parts if p.group==group] if old_config else []
    if existing:
        status=corner_status(project,group)
        if not status['editable']:raise ValueError(status['reason'])
        old={p.assembly['corner_source']['key']:p for p in existing if p.assembly.get('corner_source')};layout=group_layout(project,group)
        manual=[p for p in existing if not p.assembly.get('corner_source')]
        if manual:raise ValueError('Do narożnika dodano własne elementy. Zmień je osobno albo utwórz nowy narożnik, aby ich nie nadpisać.')
        if c['name']!=group and any(p.group==c['name'] for p in project.parts):raise ValueError('Mebel o tej nazwie już istnieje.')
        result.parts=[p for p in result.parts if p.group!=group];group=c['name']
    else:
        old={};layout={};base=group or c['name'];group=base;i=2;occupied={p.group for p in result.parts}
        while group in occupied:group=f'{base} ({i})';i+=1
    old_ids={p.id for p in existing};retained_attachments=[copy.deepcopy(item) for item in result.hardware if item.get('owner_id') in old_ids and item.get('attachment_kind')]
    result.hardware=[item for item in result.hardware if not(item.get('owner_id') in old_ids and item.get('corner_auto'))]
    for p in result.parts:p.operations=[op for op in p.operations if not(op.get('corner_hinge_owner') in old_ids and op.get('config_auto'))]
    c['name']=group;w,d,h,t,a,b=c['width'],c['depth'],c['height']-c['leg_height'],c['thickness'],c['arm_depth'],c['arm_depth_y'];ox,oy,floor=c['origin'];oz=floor+c['leg_height']
    outline=corner_outline(w,d,a,c['shape'],c['radius'],c['side'],c['segments'],b,c['diagonal_angle']);created=[]
    def add(key,name,u,v,plane,x,y,z,role='partition',contour=None,thick=None,material=None,mirror=True):
        p=Part(name=name,group=group,u=u,v=v,t=t if thick is None else thick,plane=plane,x=ox+x,y=oy+y,z=oz+z,role=role,material=material or c['material'],layout=copy.deepcopy(layout))
        if contour:p.contour=contour;p.contour_edge=True
        if c['side']=='right' and plane!='XY' and mirror:
            # Mirror rectangular vertical members without changing their local
            # rotations; ordinary manufacturing U/V remain positive.
            span=t if plane=='YZ' else u;p.x=ox+w-x-span
        previous=old.get(key)
        if previous:
            p.id=previous.id;p.name=previous.name;p.grain=previous.grain;p.visible=previous.visible;p.edges=copy.deepcopy(previous.edges);p.contour_edge=previous.contour_edge
            material_key='front_material' if role=='door' else 'material'
            if old_config.get(material_key,old_config['material'])==c[material_key]:p.material=previous.material
            for field in ('holes','fillets','chamfers'):setattr(p,field,copy.deepcopy(getattr(previous,field)))
            for field in ('operations','machining'):setattr(p,field,[copy.deepcopy(op) for op in getattr(previous,field) if not op.get('config_auto')])
        p.assembly['corner_source']=dict(schema=1,key=key);p.validate();created.append(p);result.parts.append(p);return p
    bottom=add('bottom','Dno narożne',w,d,'XY',0,0,0,'bottom',outline)
    add('back_x','Bok tylny X',w-t,h-2*t,'XZ',t,0,t,'partition')
    add('back_y','Bok tylny Y',d,h-2*t,'YZ',0,0,t,'left')
    end_x=add('end_x','Bok końcowy X',a-t,h-2*t,'YZ',w-t,t,t,'right')
    end_y=add('end_y','Bok końcowy Y',b-t,h-2*t,'XZ',t,d-t,t,'partition')
    if c['top_mode']=='panel':add('top','Góra narożna',w,d,'XY',0,0,h-t,'top',outline)
    else:
        r=c['rail_width'];x=t if c['side']=='left' else 0
        add('rail_x','Listwa narożna X',w-t,r,'XY',x,0,h-t,'top')
        x=0 if c['side']=='left' else w-r
        add('rail_y','Listwa narożna Y',r,d-r,'XY',x,r,h-t,'top')
    inset=t+2
    shelf_outline=corner_outline(w-2*inset,d-2*inset,a-2*inset,c['shape'],c['radius'],c['side'],c['segments'],b-2*inset,c['diagonal_angle'])
    for index in range(c['shelves']):
        level=t+(h-3*t)*(index+1)/(c['shelves']+1)
        add(f'shelf.{index}','Półka narożna '+str(index+1),w-2*inset,d-2*inset,'XY',inset,inset,level,'shelf',shelf_outline)
    if c['fronts']=='orthogonal':
        gap,ft=c['gap'],c['front_thickness'];span_x=w-b-ft-3*gap;span_y=d-a-2*gap
        if min(span_x,span_y)<80:raise ValueError('Ramiona są za krótkie dla dwóch niezależnych frontów. Powiększ gabaryty lub wybierz otwarty narożnik.')
        post_x=add('post_x','Słupek narożny X',70,h-2*t,'YZ',b-t,a-70,t)
        post_y=add('post_y','Słupek narożny Y',70,h-2*t,'XZ',b,a-t,t)
        right=c['side']=='right'
        front_x=add('front.x','Front ramienia X',span_x,h-2*gap,'XZ',gap if right else w-gap,a+gap if right else a+gap+ft,gap,'door',thick=ft,material=c['front_material'],mirror=False)
        front_x.rz=0 if right else 180
        front_y=add('front.y','Front ramienia Y',span_y,h-2*gap,'XZ' if right else 'YZ',w-b-gap-ft if right else b+gap+ft,d-gap,gap,'door',thick=ft,material=c['front_material'],mirror=False)
        front_y.rz=-90 if right else 180
        for front,support,opposite,support_u,support_face,cup_face,sign in ((front_x,end_x,post_x,a-37-t,'top' if right else 'bottom','bottom' if right else 'top',-1 if right else 1),
                                                                         (front_y,end_y,post_y,(w-b+37 if right else b-37)-(support_x:=end_y.x-ox),'bottom','top',1)):
            edge=local_world(front,0,0,front.t if cup_face=='top' else 0)
            bay=dict(support_id=support.id,opposite_id=opposite.id,support_face=support_face,support_u=support_u,cup_face=cup_face,hinge_point=list(edge[:2]),opening_sign=sign)
            front.assembly.update(group=group,role='door',config=dict(corner_bay=bay,gap=gap,hinge_side='left',hinge_count=c['hinge_count'],hinge_pattern=c['hinge_pattern']))
            front.motion=dict(mode='hinge_left',angle=c['opening_angle'],travel=0)
            _corner_hinges(result,front,c['hinge_pattern'],c['hinge_count'],0)
            _front_finish(result,front,c)
    elif c['fronts']=='diagonal':
        gap,ft=c['gap'],c['front_thickness'];rise=c['radius']*math.tan(math.radians(c['diagonal_angle']))
        p,q=(b+c['radius'],a),(b,a+rise)
        if c['side']=='right':p,q=(w-q[0],q[1]),(w-p[0],p[1])
        length=math.dist(p,q)
        if length-2*gap<40:raise ValueError('Ścięcie jest za krótkie dla frontu skośnego.')
        ux,uy=(q[0]-p[0])/length,(q[1]-p[1])/length
        front=add('front.diagonal','Front skośny — okucie dobierz osobno',length-2*gap,h-2*gap,'XZ',p[0]+uy*(gap+ft)+ux*gap,p[1]-ux*(gap+ft)+uy*gap,gap,'door',thick=ft,material=c['front_material'],mirror=False)
        front.rz=math.degrees(math.atan2(uy,ux));front.motion=dict(mode='none');front.assembly.update(group=group,role='door',config=dict(gap=gap,corner_diagonal=True))
        _front_finish(result,front,c)
    if c['leg_height']:
        from core import inside
        s=min(c['leg_setback'],a/3,b/3);positions=[(s,s),(w-s,a/2),(b/2,d-s),(b/2,a/2)]
        if c['leg_count']==6:positions.extend(((w-s,s),(s,d-s)))
        if c['side']=='right':positions=[(w-x,y) for x,y in positions]
        if any(math.dist(point,other)<c['leg_diameter']-1e-6 for i,point in enumerate(positions) for other in positions[i+1:]):raise ValueError('Nogi narożnika nakładają się. Zmniejsz ich średnicę albo wybierz 4 nogi.')
        for x,y in positions:
            if not all(inside((x+c['leg_diameter']/2*math.cos(i*math.tau/32),y+c['leg_diameter']/2*math.sin(i*math.tau/32)),outline) for i in range(32)):
                raise ValueError('Noga narożnika nie mieści się pod obrysem. Zmień średnicę lub odsunięcie.')
        result.hardware.append(dict(kind='leg',owner_id=bottom.id,name='Noga narożnika — własny model',qty=c['leg_count'],price=c['leg_price'],height=c['leg_height'],diameter=c['leg_diameter'],color=c['leg_color'],positions=[dict(x=ox+x,y=oy+y,z=floor) for x,y in positions],config_auto=True,corner_auto=True))
    new_keys={p.assembly['corner_source']['key'] for p in created}
    for key,p in old.items():
        if key not in new_keys and (p.holes or any(not op.get('config_auto') for op in (*p.operations,*p.machining)) or p.fillets or p.chamfers or any(item.get('owner_id')==p.id for item in result.hardware)):
            raise ValueError('Zmiana usuwa część narożnika z ręczną obróbką lub okuciem.')
    for item in result.hardware:
        if item.get('owner_id') in {p.id for p in existing} and item.get('group'):item['group']=group
        if item.get('corner_auto'):
            previous=next((h for h in project.hardware if h.get('owner_id')==item.get('owner_id') and h.get('kind')==item.get('kind') and h.get('corner_auto')),None)
            if previous:
                for key in ('price','supplier','note'):
                    if key in previous and not(key=='price' and item['kind']=='leg' and old_config and c['leg_price']!=old_config.get('leg_price',0)):item[key]=copy.deepcopy(previous[key])
    bottom.assembly['corner_source'].update(config=copy.deepcopy(c),manifest={p.assembly['corner_source']['key']:dict(id=p.id,signature=_geometry(p)) for p in created})
    # Rebuilding standard hinge recipes removes old hinge rows. Restore the
    # manual attachment descriptors before their final authoritative refresh.
    for item in retained_attachments:
        if not any(h.get('attachment_id')==item.get('attachment_id') for h in result.hardware):result.hardware.append(item)
    from workspace_worktops import refresh_shared_worktops
    result=refresh_shared_worktops(result,validate=False)
    from drag_hardware import refresh_manual_attachments
    refresh_manual_attachments(result);result.validate();return result,group


def resize_corner_furniture(project,group,width,height,depth,origin=None):
    config=read_corner_config(project,group)
    if config is None:return None
    config.update(width=width,height=height+config.get('leg_height',0),depth=depth)
    if origin is not None:config['origin']=list(origin)
    return create_corner(project,config,group)[0]


def convert_to_corner(project,group,shape='L',side='left',arm_depth=None,width=None,depth=None,fronts='orthogonal',**changes):
    """Convert a recognised, unmodified cabinet in one atomic transaction.

    IDs of matching structural members/fronts survive. Manual geometry,
    machining, additional panels and fittings require a separate new corner.
    Existing corner groups are rebuilt using their own persistent recipe.
    """
    from furniture_configurator import read_config,configuration_status,infer_config
    from assembly import inferred_role
    current=read_corner_config(project,group)
    if current:
        current.update(shape=shape,side=side,fronts=fronts,**changes)
        for key,value in (('arm_depth',arm_depth),('width',width),('depth',depth)):
            if value is not None:current[key]=value
        return create_corner(project,current,group)
    members=[p for p in project.parts if p.group==group]
    if not members:raise ValueError('Wybierz szafkę albo szafę do konwersji.')
    oldcfg=read_config(project,group);legacy=oldcfg is None
    if legacy:oldcfg=infer_config(project,group)
    else:
        status=configuration_status(project,group)
        if not status['editable']:raise ValueError(status['reason'])
        if any(not p.configurator_source.get('key') for p in members):raise ValueError('Mebel zawiera własne elementy. Utwórz osobny narożnik, aby ich nie nadpisać.')
    if any(inferred_role(p)=='drawer' for p in members):raise ValueError('Konwersja narożna obsługuje szafki i szafy z półkami lub drzwiami. Zachowaj szuflady w oddzielnym module.')
    ids={p.id for p in members}
    if any(p.holes or p.fillets or p.chamfers or (not legacy and any(not op.get('config_auto') for op in (*p.operations,*p.machining))) for p in members):
        raise ValueError('Mebel zawiera ręczną obróbkę. Utwórz nowy narożnik, aby jej nie nadpisać.')
    if not legacy and any(h.get('owner_id') in ids and not h.get('config_auto') for h in project.hardware):raise ValueError('Mebel zawiera ręczne okucia. Utwórz osobny narożnik, aby je zachować.')
    c=default_corner_config(project,'wardrobe' if oldcfg['height']>=1500 else 'cabinet')
    for key in ('thickness','front_thickness','gap','material','front_material','hinge_pattern','hinge_count','opening_angle','leg_height','leg_count','leg_diameter','leg_setback','leg_color','leg_price','handle_spacing','front_style','rail_width','top_mode'):
        if key in oldcfg:c[key]=copy.deepcopy(oldcfg[key])
    c.update(name=group,origin=copy.deepcopy(oldcfg['origin']),height=oldcfg['height']-(oldcfg['worktop_thickness'] if oldcfg.get('worktop') else 0),shape=shape,side=side,fronts=fronts)
    c['handle_mode']='none' if oldcfg.get('handle_mode')=='none' else 'handle'
    c['shelves']=max((s['shelves'] for s in oldcfg['sections'] if s['enabled']),default=0)
    arm=oldcfg['depth'] if arm_depth is None else arm_depth
    c.update(arm_depth=arm,arm_depth_y=arm,width=max(oldcfg['width'],arm+340) if width is None else width,depth=arm+340 if depth is None else depth)
    c.update(changes);c=normalize_corner_config(c,project)
    layout=group_layout(project,group)
    baseline=copy.deepcopy(project);baseline.parts=[p for p in baseline.parts if p.id not in ids]
    baseline.hardware=[h for h in baseline.hardware if h.get('owner_id') not in ids]
    for p in baseline.parts:p.operations=[op for op in p.operations if op.get('owner_id') not in ids]
    result,newgroup=create_corner(baseline,c,group)
    old_by_key={p.configurator_source.get('key'):p for p in members if p.configurator_source.get('key')}
    old_by_name={p.name:p for p in members}
    names={'carcass.left':'Bok lewy','carcass.right':'Bok prawy','carcass.bottom':'Dno','carcass.top':'Wieniec','carcass.back':'Plecy nakładane HDF','main.bay.0.door.0':'Front 1','main.bay.0.door.1':'Front 2'}
    matches={'back_y':'carcass.left','end_x':'carcass.right','bottom':'carcass.bottom','top':'carcass.top','rail_x':'carcass.top','rail_y':'carcass.rail_back','back_x':'carcass.back','front.x':'main.bay.0.door.0','front.y':'main.bay.0.door.1'}
    changed_ids={};newparts=[p for p in result.parts if p.group==newgroup]
    for p in newparts:
        key=p.assembly['corner_source']['key'];oldkey=matches.get(key)
        if key.startswith('shelf.'):oldkey='main.bay.0.shelf.'+str(int(key.split('.')[1])+1)
        previous=old_by_key.get(oldkey) or old_by_name.get(names.get(oldkey,''))
        if not previous and key.startswith('shelf.'):previous=old_by_name.get('Półka '+str(int(key.split('.')[1])+1))
        if previous:
            changed_ids[p.id]=previous.id;p.id=previous.id;p.visible=previous.visible;p.grain=previous.grain
            # HDF backing becomes a structural side; its old material is not
            # reused at the new panel thickness.
            if key!='back_x':p.material=previous.material
        p.layout=copy.deepcopy(layout)
    def remap(value):
        if isinstance(value,dict):return {k:remap(v) for k,v in value.items()}
        if isinstance(value,(list,tuple)):return [remap(v) for v in value]
        if isinstance(value,str):return changed_ids.get(value,value)
        return value
    for p in newparts:
        p.assembly=remap(p.assembly);p.operations=remap(p.operations);p.machining=remap(p.machining)
    result.hardware=remap(result.hardware)
    bottom=next(p for p in newparts if p.role=='bottom');bottom.assembly['corner_source']['manifest']={p.assembly['corner_source']['key']:dict(id=p.id,signature=_geometry(p)) for p in newparts}
    from workspace_worktops import refresh_shared_worktops
    result=refresh_shared_worktops(result,validate=False)
    result.validate();return result,newgroup


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_construction as _install_cpp
_install_cpp(globals(), 'corner_furniture')
