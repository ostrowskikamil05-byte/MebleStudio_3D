"""Persistent curved outlines and dimension-driven furniture starting points.

Drawing contours are tessellated. CAD solids use exact circles/ellipses/arcs.
Furniture presets are design starting points, not tested structural plans.
"""
from copy import deepcopy
import hashlib
import json
import math
from core import Part, Material, finite, validate_polygon

SHAPES = {
    'Prostokąt': 'rectangle',
    'Prostokąt — cztery promienie': 'rounded',
    'Elipsa / owal': 'ellipse',
    'Koło': 'circle',
    'Kapsuła / obły blat': 'capsule',
    'Łuk na górnej krawędzi': 'arch',
    'Trapez / zwężana noga': 'taper',
    'Półkole / półelipsa': 'half_ellipse',
}
THICKNESSES = {
    'Płyta meblowa': (10, 12, 16, 18, 19, 22, 25, 28),
    'MDF': (6, 8, 10, 12, 16, 18, 19, 22, 25, 30),
    'Sklejka': (4, 6, 9, 12, 15, 18, 21, 24, 30),
    'Drewno lite': (20, 25, 28, 30, 35, 40, 45, 50, 60),
    'HDF / plecy': (3, 4, 6),
    'Blat': (28, 38, 40, 50),
}
CURVE_NOTE = ('Szkic, podgląd i DXF pokazują łuki jako odcinki. STEP zachowuje '
              'dokładne łuki kołowe i eliptyczne. Liczba odcinków zmienia podgląd, '
              'a parametry obrysu pozostają zapisane. Promienie dotyczą '
              'obrysu w U/V, a zaokrąglenia kantów 3D edytuje się narzędziem CAD.')


def _resolution(value):
    n = finite(value, 4, 24)
    if int(n) != n:
        raise ValueError('Liczba odcinków ćwiartki musi być całkowita: 4–24.')
    return int(n)


def _finish(points, u, v):
    """Remove coincident tangencies and pin floating point cardinal extrema."""
    clean = []
    for x, y in points:
        x = min(u, max(0., round(x, 9)))
        y = min(v, max(0., round(y, 9)))
        if abs(x-u) < 1e-7: x = u
        if abs(y-v) < 1e-7: y = v
        if not clean or math.dist(clean[-1], (x,y)) > 1e-7:
            clean.append((x,y))
    if len(clean)>1 and math.dist(clean[0],clean[-1])<=1e-7:
        clean.pop()
    # At submillimetre scale a sampled curve may create forbidden tiny edges.
    # Report this rather than silently changing its radius or outline.
    result=validate_polygon(clean)
    if min(x for x,y in result)!=0 or min(y for x,y in result)!=0:
        raise ValueError('Kontur jest zbyt mały dla podanej liczby odcinków.')
    return result


def contour(kind, u, v, radii=(30,30,30,30), segments=12,
            arch_rise=None, taper=10):
    """Return a local normalized profile; radii: LL, LR, UR, UL in U/V."""
    u, v = finite(u, .1, 10000), finite(v, .1, 10000)
    n = _resolution(segments)
    if kind == 'rectangle': return []
    if kind == 'circle':
        if abs(u-v)>1e-7: raise ValueError('Koło wymaga jednakowej szerokości i wysokości U/V.')
        kind = 'ellipse'
    if kind == 'ellipse':
        points=[(u/2+u/2*math.cos(i*math.tau/(4*n)),
                 v/2+v/2*math.sin(i*math.tau/(4*n))) for i in range(4*n)]
    elif kind in ('rounded','capsule'):
        if kind=='capsule': radii = [min(u,v)/2]*4
        if len(radii)!=4: raise ValueError('Podaj osobny promień dla czterech narożników.')
        r=[finite(x,0,min(u,v)) for x in radii]
        for a,b,length in ((0,1,u),(1,2,v),(2,3,u),(3,0,v)):
            if r[a]+r[b]>length+1e-7:
                raise ValueError('Promienie sąsiednich narożników przekraczają długość krawędzi.')
        points=[]
        for radius,cx,cy,start,corner in (
            (r[0],r[0],r[0],math.pi,(0,0)),
            (r[1],u-r[1],r[1],math.pi*1.5,(u,0)),
            (r[2],u-r[2],v-r[2],0,(u,v)),
            (r[3],r[3],v-r[3],math.pi/2,(0,v))):
            if radius==0: points.append(corner)
            else:
                points.extend((cx+radius*math.cos(start+i*math.pi/(2*n)),
                               cy+radius*math.sin(start+i*math.pi/(2*n))) for i in range(n+1))
    elif kind in ('arch','half_ellipse'):
        rise=v if kind=='half_ellipse' else finite(arch_rise if arch_rise is not None else min(v/3,u/2),.1,v)
        points=[(0,0),(u,0)]
        if rise<v: points.append((u,v-rise))
        points.extend((u/2+u/2*math.cos(i*math.pi/(2*n)),
                       v-rise+rise*math.sin(i*math.pi/(2*n))) for i in range(2*n+1))
        if rise<v: points.append((0,v-rise))
    elif kind=='taper':
        inset=finite(taper,0,u/2-.01)
        # The narrower bottom is centered; dimensions still include full top width.
        points=[(inset,0),(u-inset,0),(u,v),(0,v)]
    else: raise ValueError('Nieznany kształt konturu.')
    return _finish(points,u,v)


def shape_definition(kind,u,v,radii=(30,30,30,30),segments=12,arch_rise=None,taper=10):
    """Canonical JSON-safe definition. Corner radii are fixed millimetres."""
    if len(radii)!=4:raise ValueError('Obrys wymaga czterech wartości promieni.')
    radii=[finite(x,0,10000) for x in radii]
    arch_rise=finite(min(v/3,u/2) if arch_rise is None else arch_rise,0,10000)
    taper=finite(taper,0,10000)
    contour(kind,u,v,radii,segments,arch_rise,taper)
    return dict(kind=kind,radii=[float(x) for x in radii],segments=_resolution(segments),
                arch_rise=arch_rise,
                taper=float(taper))


def resize_profile(part,u,v):
    """Update outline before assigning U/V; preserve radii and arch rise."""
    if part.profile:
        opts={k:value for k,value in part.profile.items() if k!='kind'}
        part.profile=shape_definition(part.profile['kind'],u,v,**opts)
        part.contour=contour(part.profile['kind'],u,v,**opts)
    elif part.contour:
        part.contour=[(a*u/part.u,b*v/part.v) for a,b in part.contour]


def profile_area(profile,u,v):
    kind=profile['kind']
    if kind in ('ellipse','circle'):return math.pi*u*v/4
    if kind=='half_ellipse':return math.pi*u*v/4
    if kind=='arch':
        rise=profile['arch_rise'];return u*(v-rise)+math.pi*u*rise/4
    if kind in ('rounded','capsule'):
        r=profile['radii'] if kind=='rounded' else [min(u,v)/2]*4
        return u*v-(1-math.pi/4)*sum(x*x for x in r)
    if kind=='taper':return v*(u-profile['taper'])
    return u*v


def _ellipse_perimeter(a,b):
    # Simpson quadrature avoids a new scipy dependency and is accurate even for
    # elongated furniture profiles; CAD supplies exact analytic geometry.
    n=512;step=math.pi/(2*n)
    values=[math.hypot(a*math.sin(i*step),b*math.cos(i*step)) for i in range(n+1)]
    return 4*step/3*(values[0]+values[-1]+4*sum(values[1:-1:2])+2*sum(values[2:-1:2]))


def profile_perimeter(profile,u,v):
    kind=profile['kind']
    if kind in ('ellipse','circle'):return _ellipse_perimeter(u/2,v/2)
    if kind=='half_ellipse':return u+_ellipse_perimeter(u/2,v)/2
    if kind=='arch':
        rise=profile['arch_rise'];return u+2*(v-rise)+_ellipse_perimeter(u/2,rise)/2
    if kind in ('rounded','capsule'):
        r=profile['radii'] if kind=='rounded' else [min(u,v)/2]*4
        return 2*(u+v)+(math.pi/2-2)*sum(r)
    if kind=='taper':
        inset=profile['taper'];return 2*u-2*inset+2*math.hypot(v,inset)
    return 2*(u+v)


def refresh_decor(part):
    """Rebuild tagged flutes; keep manually created machining unchanged."""
    cfg=part.decor
    if cfg.get('type')!='fluting':raise ValueError('Nieznana dekoracja parametryczna.')
    width=finite(cfg.get('width',8),.5,1000);pitch=finite(cfg.get('pitch',24),width+.5,1000)
    depth=finite(cfg.get('depth',3),.1,part.t-.1)
    margin_u=finite(cfg.get('margin_u',30),0,part.u/2)
    margin_v=finite(cfg.get('margin_v',20),0,part.v/2-.01)
    face=cfg.get('face','bottom')
    if face not in ('top','bottom'):raise ValueError('Ryflowanie wymaga górnej lub dolnej powierzchni.')
    usable=part.u-2*margin_u
    if usable<width or part.v-2*margin_v<.1:raise ValueError('Za mały element dla marginesów ryflowania.')
    count=int((usable-width)/pitch)+1
    manual=[c for c in part.machining if c.get('generated_decor')!='fluting']
    if len(manual)+count>200:raise ValueError('Ryflowanie przekracza 200 operacji. Zwiększ podziałkę.')
    start=(part.u-(count-1)*pitch)/2
    part.machining=manual+[dict(type='groove',shape='rect',face=face,
        u=start+k*pitch,v=part.v/2,length=width,width=part.v-2*margin_v,
        depth=depth,angle=0,generated_decor='fluting',purpose='Ryflowanie parametryczne') for k in range(count)]
    part.decor=dict(type='fluting',width=width,pitch=pitch,depth=depth,
                    margin_u=margin_u,margin_v=margin_v,face=face)


def remove_decor(part):
    part.machining=[c for c in part.machining if c.get('generated_decor')!='fluting']
    part.decor={}


def apply_shape(project, part_id, kind, u=None, v=None, thickness=None, **options):
    """Atomic edit, preserving meaningful drills, linked fittings and dependencies.

    Shape changes invalidate edge numbering, therefore existing fillets/chamfers
    must first be explicitly removed by the user. This avoids discarding work.
    """
    from dimension_model import resize_dimensions
    from features import replace_part
    p=next((p for p in project.parts if p.id==part_id),None)
    if p is None:raise ValueError('Element usunięto z projektu. Otwórz edytor dla istniejącego elementu.')
    if p.assembly.get('generated_by'):
        raise ValueError('Skrzynkę szuflady zmieniaj przez jej front i parametry prowadnic.')
    if p.fillets or p.chamfers:
        raise ValueError('Przed zmianą obrysu usuń fazy i zaokrąglenia kantów w edytorze CAD; numeracja krawędzi zmieni się.')
    u=finite(p.u if u is None else u,.1,10000)
    v=finite(p.v if v is None else v,.1,10000)
    t=finite(p.t if thickness is None else thickness,.1,10000)
    new_contour=contour(kind,u,v,**options)
    # Shared dimension logic regenerates linked holes and handle positions first.
    intermediate=deepcopy(project)
    next(q for q in intermediate.parts if q.id==part_id).profile={}
    result=resize_dimensions(intermediate,part_id,u,v,t)
    q=deepcopy(next(q for q in result.parts if q.id==part_id))
    q.profile=shape_definition(kind,u,v,**options) if kind!='rectangle' else {}
    q.contour=new_contour
    if new_contour:q.edges=[False]*4
    q.validate()
    return replace_part(result,q)


TEMPLATES = [
    ('Jadalnia','Stół obły — cztery nogi',1600,760,850,'rounded_table','Nowoczesny',
     'Obły blat, cztery okrągłe nogi oraz rama pod blatem.'),
    ('Salon','Stolik owalny — dwa postumenty',1100,440,600,'oval_table','Organiczny',
     'Eliptyczny blat i półka oraz dwie okrągłe podpory.'),
    ('Biuro','Biurko z obłym blatem',1300,760,650,'rounded_desk','Skandynawski',
     'Osobne promienie narożników i nogi z trapezowym profilem.'),
    ('Jadalnia','Krzesło drewniane z łukiem',480,840,460,'chair','Drewno lite',
     'Obłe siedzisko, łuk oparcia, okrągłe nogi i belki łączące.'),
    ('Kuchnia','Taboret okrągły',400,460,400,'stool','Drewno lite',
     'Okrągłe siedzisko, trzy nogi i belki łączące.'),
    ('Przedpokój','Konsola obła',1100,780,380,'console','Japandi',
     'Blat kapsułowy, dwie podpory z łukiem i dolna półka.'),
    ('Sypialnia','Toaletka z lustrem łukowym',1000,1400,450,'vanity','Art déco',
     'Obły blat z czterema nogami oraz lustro z łukowym obrysem.'),
    ('Salon','Komoda z ryflowanymi frontami',1200,820,430,'slatted','Japandi',
     'Korpus z dwoma frontami, obły blat i pionowe frezowane rowki.'),
    ('Sypialnia','Szafka nocna — obły blat',450,520,400,'nightstand','Nowoczesny',
     'Korpus z zawiasami i uchwytem oraz obły nakładany blat.'),
    ('Ogród','Ławka z łukowym oparciem',1400,850,500,'bench','Drewno lite',
     'Obłe siedzisko, łukowy profil oparcia i okrągłe nogi.'),
    ('Łazienka','Lustro łukowe na podkładzie',650,1000,24,'mirror','Art déco',
     'Dekoracyjny model lustra i obłego podkładu bez okuć do zawieszenia.'),
    ('Salon','Stolik przyścienny półowalny',900,750,380,'half_table','Organiczny',
     'Półeliptyczny blat, dwie tylne nogi i noga z przodu.'),
]


def create_shaped_furniture(project,index,width=None,height=None,depth=None,
                            thickness=28,origin=(0,0,0),group=None,material=None,
                            front_material=None,segments=12):
    """Build a furniture concept with dimensioned manufacturable planar parts."""
    template_index=index
    category,name,dw,dh,dd,kind,style,description=TEMPLATES[index]
    w,h,d,t=[finite(x,1,10000) for x in
             (dw if width is None else width,dh if height is None else height,
              dd if depth is None else depth,thickness)]
    n=_resolution(segments)
    result=deepcopy(project)
    material=material or next((m.id for m in result.materials if m.kind=='drewno'),result.materials[0].id)
    if material not in {m.id for m in result.materials}:raise ValueError('Wybierz istniejący materiał.')
    base=group or name;group=base;occupied={p.group for p in result.parts};i=2
    while group in occupied:group=f'{base} ({i})';i+=1
    if len(origin)!=3:raise ValueError('Pozycja wymaga współrzędnych X/Y/Z.')
    ox,oy,oz=[finite(a) for a in origin]
    if min(w,d)<=4 or h<=t:raise ValueError('Mebel musi mieć dodatnie wymiary i miejsce na grubość materiału.')

    def add(label,u,v,thick,plane,x,y,z,shape='rectangle',**opts):
        p=Part(name=label,group=group,u=u,v=v,t=thick,plane=plane,
               x=ox+x,y=oy+y,z=oz+z,material=material,
               contour=contour(shape,u,v,segments=n,**opts),
               profile=shape_definition(shape,u,v,segments=n,**opts) if shape!='rectangle' else {})
        p.validate();result.parts.append(p);return p

    def leg(label,x,y,height,diameter=50):
        return add(label,diameter,diameter,height,'XY',x,y,0,'circle')

    def four_round_legs(height,inset,diameter=50):
        if min(w,d)<2*(inset+diameter):raise ValueError('Mebel jest za mały dla czterech nóg; zwiększ szerokość i głębokość.')
        for k,(x,y) in enumerate(((inset,inset),(w-inset-diameter,inset),
                                 (inset,d-inset-diameter),(w-inset-diameter,d-inset-diameter))):
            leg(f'Noga okrągła {k+1}',x,y,height,diameter)

    def frame_rails(level,inset=70,diameter=50):
        rail_h=min(60,max(20,h/12));rail_t=min(t,25)
        for label,y in (('Rama przednia',inset+diameter/2-rail_t/2),
                        ('Rama tylna',d-inset-diameter/2-rail_t/2)):
            add(label,w-2*inset-diameter,rail_h,rail_t,'XZ',inset+diameter/2,y,level-rail_h)
        for label,x in (('Rama lewa',inset+diameter/2-rail_t/2),
                        ('Rama prawa',w-inset-diameter/2-rail_t/2)):
            add(label,d-2*inset-diameter,rail_h,rail_t,'YZ',x,inset+diameter/2,level-rail_h)

    if kind in ('rounded_table','rounded_desk','half_table'):
        top=h-t;radius=min(w,d)*.1
        shape='half_ellipse' if kind=='half_table' else 'rounded'
        add('Blat obły' if kind!='half_table' else 'Blat półowalny',w,d,t,'XY',0,0,top,shape,radii=(radius,)*4)
        inset=min(70,min(w,d)*.12);diameter=min(60,min(w,d)*.1)
        if kind=='rounded_desk':
            for k,(x,y) in enumerate(((inset,inset),(w-inset-diameter,inset),
                                     (inset,d-inset-diameter),(w-inset-diameter,d-inset-diameter))):
                add(f'Noga zwężana {k+1}',diameter,top,diameter,'XZ',x,y,0,'taper',taper=diameter*.18)
        elif kind=='half_table':
            for k,(x,y) in enumerate(((inset,0),(w-inset-diameter,0),(w/2-diameter/2,d-diameter-20))):
                leg(f'Noga okrągła {k+1}',x,y,top,diameter)
        else:four_round_legs(top,inset,diameter)
        if kind!='half_table':frame_rails(top,inset,diameter)
    elif kind=='oval_table':
        add('Blat eliptyczny',w,d,t,'XY',0,0,h-t,'ellipse')
        dia=min(d*.38,w*.2);baseheight=h-t
        for k,cx in enumerate((w*.28,w*.72)):leg(f'Postument okrągły {k+1}',cx-dia/2,d/2-dia/2,baseheight,dia)
        add('Półka owalna',w*.8,d*.8,min(t,22),'XY',w*.1,d*.1,min(h*.2,80),'ellipse')
    elif kind=='chair':
        seat_h=h*.54;seat_t=min(t,35);diameter=min(42,min(w,d)*.095);inset=min(32,min(w,d)*.07)
        if seat_h<=seat_t+50 or h-seat_h-seat_t<100:raise ValueError('Zwiększ wysokość krzesła.')
        add('Siedzisko obłe',w,d,seat_t,'XY',0,0,seat_h-seat_t,'rounded',radii=(min(w,d)*.13,)*4)
        four_round_legs(seat_h-seat_t,inset,diameter)
        frame_rails(seat_h-seat_t,inset,diameter)
        back_t=min(t,24);back_h=h-seat_h
        add('Oparcie łukowe',w-2*inset,back_h,back_t,'XZ',inset,d-back_t-inset,seat_h,'arch',arch_rise=min(back_h*.3,w*.25))
        # Extend two back supports from seat to near the arch, as separate blanks.
        for k,x in enumerate((inset,w-inset-diameter)):
            add(f'Słupek oparcia {k+1}',diameter,diameter,back_h*.85,'XY',x,d-inset-diameter,seat_h,'circle')
    elif kind=='stool':
        if abs(w-d)>1e-7:raise ValueError('Taboret okrągły wymaga równej szerokości i głębokości.')
        add('Siedzisko okrągłe',w,d,t,'XY',0,0,h-t,'circle')
        dia=min(w*.115,46);r=w*.30;positions=[]
        for k,angle in enumerate((30,150,270)):
            a=math.radians(angle);x=w/2+r*math.cos(a)-dia/2;y=d/2+r*math.sin(a)-dia/2
            leg(f'Noga taboretu {k+1}',x,y,h-t,dia);positions.append((x+dia/2,y+dia/2))
        # Chord rails are XY blanks with thickness along Z, then rotated in-plane.
        for k,(a,b) in enumerate(zip(positions,positions[1:]+positions[:1])):
            length=math.dist(a,b);beam=min(t,25)
            angle=math.degrees(math.atan2(b[1]-a[1],b[0]-a[0]));rad=math.radians(angle)
            p=add(f'Łączyna {k+1}',length,beam,beam,'XY',a[0]+beam/2*math.sin(rad),a[1]-beam/2*math.cos(rad),h*.3)
            p.rz=angle
    elif kind=='console':
        add('Blat kapsułowy',w,d,t,'XY',0,0,h-t,'capsule')
        support_t=min(t,35)
        for k,x in enumerate((w*.22-support_t/2,w*.78-support_t/2)):
            add(f'Podpora obła {k+1}',d*.7,h-t,support_t,'YZ',x,d*.15,0,'arch',arch_rise=min((h-t)*.15,d*.35))
        add('Półka kapsułowa',w*.9,d*.8,min(t,22),'XY',w*.05,d*.1,h*.15,'capsule')
    elif kind=='vanity':
        desk_h=min(760,h*.58)
        if h-desk_h<100:raise ValueError('Toaletka wymaga miejsca na lustro nad blatem.')
        add('Blat toaletki',w,d,t,'XY',0,0,desk_h-t,'rounded',radii=(min(w,d)*.1,)*4)
        four_round_legs(desk_h-t,min(w,d)*.1,min(50,min(w,d)*.12))
        mirror_h=h-desk_h;mirror_w=min(w*.65,mirror_h*1.2)
        panel=add('Podkład lustra łukowego',mirror_w,mirror_h,min(t,18),'XZ',(w-mirror_w)/2,d-30,desk_h,'arch',arch_rise=min(mirror_h*.35,mirror_w/2))
        mirror_mat=next((m for m in result.materials if m.name=='Lustro — model dekoracyjny'),None)
        if mirror_mat is None:
            mirror_mat=Material(name='Lustro — model dekoracyjny',kind='inne',color='#b8dce7',price_m2=0);result.materials.append(mirror_mat)
        mirror=add('Lustro łukowe',mirror_w-20,mirror_h-20,4,'XZ',panel.x-ox+10,panel.y-oy-4,panel.z-oz+10,'arch',arch_rise=min((mirror_h-20)*.35,(mirror_w-20)/2))
        mirror.material=mirror_mat.id
    elif kind in ('slatted','nightstand'):
        from furniture_library import create_furniture
        index=18 if kind=='slatted' else 11
        corpus_t=min(t,22);body_h=h-t
        result,group=create_furniture(result,index,w,body_h,d,corpus_t,origin,group,material,front_material or material)
        add('Nakładany blat obły',w+20,d+20,t,'XY',-10,-10,body_h,'rounded',radii=(min(w,d)*.08,)*4).role='worktop'
        if kind=='slatted':
            for front in [p for p in result.parts if p.group==group and p.role=='door']:
                front.decor=dict(type='fluting',width=8,pitch=24,depth=min(3,front.t*.2),
                    margin_u=front.u*.1,margin_v=front.v*.05,face='bottom')
                refresh_decor(front)
    elif kind=='bench':
        seat_h=min(450,h*.54);diameter=min(60,d*.13);inset=min(55,d*.1)
        add('Siedzisko ławki obłe',w,d,t,'XY',0,0,seat_h-t,'rounded',radii=(min(w,d)*.15,)*4)
        four_round_legs(seat_h-t,inset,diameter);frame_rails(seat_h-t,inset,diameter)
        add('Oparcie ławki łukowe',w,h-seat_h,min(t,25),'XZ',0,d-t,seat_h,'arch',arch_rise=min((h-seat_h)*.22,w*.15))
        for k,x in enumerate((inset,w-inset-diameter)):
            leg(f'Słupek oparcia ławki {k+1}',x,d-diameter,h-t,diameter)
    elif kind=='mirror':
        if d<=4:raise ValueError('Lustro z podkładem wymaga głębokości większej niż 4 mm.')
        rise=min(h*.3,w/2)
        panel=add('Podkład lustra — łuk',w,h,min(t,d-4),'XZ',0,4,0,'arch',arch_rise=rise)
        mirror_mat=next((m for m in result.materials if m.name=='Lustro — model dekoracyjny'),None)
        if mirror_mat is None:
            mirror_mat=Material(name='Lustro — model dekoracyjny',kind='inne',color='#b8dce7',price_m2=0);result.materials.append(mirror_mat)
        mirror=add('Tafla lustra — łuk',w-20,h-20,4,'XZ',10,0,10,'arch',arch_rise=min((h-20)*.3,(w-20)/2));mirror.material=mirror_mat.id
    else:raise ValueError('Nieznany wzorzec mebla obłego.')
    result.validate()
    source=dict(index=template_index,width=w,height=h,depth=d,thickness=t,origin=[ox,oy,oz],
                segments=n,material=material,front_material=front_material or material)
    for p in result.parts:
        if p.group==group:
            p.style_source={**deepcopy(source),'label':p.name,'signature':source_signature(p,source['origin'])}
    return result,group


def source_signature(part,origin):
    """Track geometry changed manually; colors and machining may be retained."""
    data=dict(size=[part.u,part.v,part.t],plane=part.plane,rotation=[part.rx,part.ry,part.rz],
              position=[round(a-b,7) for a,b in zip((part.x,part.y,part.z),origin)],
              contour=part.contour,profile=part.profile,role=part.role)
    return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()


def styled_bounds(project,group):
    """Nominal envelope for preset handles, including rotated components."""
    part=next((p for p in project.parts if p.group==group and p.style_source),None)
    if part is None:return None
    source=part.style_source;lo=tuple(source['origin'])
    return dict(lower=lo,upper=(lo[0]+source['width'],lo[1]+source['depth'],lo[2]+source['height']))


def resize_shaped_furniture(project,group,width,height,depth):
    """Rebuild an unchanged preset with ID retention and real support lengths.

    Edited component geometry cannot be silently overwritten; callers may still
    edit such components individually. Manually added holes/cuts are preserved
    and revalidated in the new geometry.
    """
    old_parts=[p for p in project.parts if p.group==group]
    if not old_parts or not any(p.style_source for p in old_parts):return None
    if any(not p.style_source for p in old_parts):
        raise ValueError('Do wzorca dodano własne elementy. Zmieniaj ich wymiary osobno lub utwórz wzorzec ponownie z biblioteki.')
    for p in old_parts:
        source=p.style_source
        if source_signature(p,source['origin'])!=source.get('signature'):
            raise ValueError('Geometrię wzorca zmieniono ręcznie. Zmiana całego wzorca nadpisałaby szkic lub pozycję; edytuj element osobno albo utwórz nowy wzorzec.')
    source=old_parts[0].style_source
    old_by_label={p.style_source['label']:p for p in old_parts}
    if len(old_by_label)!=len(old_parts):raise ValueError('Wzorzec zawiera kopie elementów; edytuj je oddzielnie.')
    result=deepcopy(project);old_ids={p.id for p in old_parts}
    manuals=[deepcopy(h) for h in result.hardware if h.get('owner_id') in old_ids and h.get('manual')]
    result.parts=[p for p in result.parts if p.group!=group]
    result.hardware=[h for h in result.hardware if h.get('owner_id') not in old_ids]
    result,actual=create_shaped_furniture(result,source['index'],width,height,depth,
        source['thickness'],source['origin'],group,source['material'],source['front_material'],source['segments'])
    new_parts=[p for p in result.parts if p.group==actual]
    if {p.name for p in new_parts}!=set(old_by_label):raise ValueError('Nie można dopasować elementów starego wzorca.')
    ids={p.id:old_by_label[p.name].id for p in new_parts}
    for p in new_parts:
        old=old_by_label[p.name];p.id=ids[p.id];p.name=old.name;p.material=old.material
        p.layout=deepcopy(old.layout)
        p.contour_edge=old.contour_edge;p.edges=deepcopy(old.edges);p.motion=deepcopy(old.motion)
        p.holes=deepcopy(old.holes);p.fillets=deepcopy(old.fillets);p.chamfers=deepcopy(old.chamfers)
        # The definition, not previously generated groove positions, is retained.
        p.decor=deepcopy(old.decor)
        p.machining=[deepcopy(c) for c in old.machining if not c.get('generated_decor')]
        p.operations.extend(deepcopy(op) for op in old.operations if not op.get('owner_id') and not op.get('opening_owner'))
        for key in ('anchors','children'):
            if key in p.assembly:p.assembly[key]=[ids.get(i,i) for i in p.assembly[key]]
        if p.assembly.get('generated_by'):p.assembly['generated_by']=ids.get(p.assembly['generated_by'],p.assembly['generated_by'])
        for op in p.operations:
            if op.get('owner_id'):op['owner_id']=ids.get(op['owner_id'],op['owner_id'])
    result.hardware=[h for h in result.hardware if not (h.get('owner_id') in ids and h.get('manual'))]
    for h in result.hardware:
        if h.get('owner_id') in ids:h['owner_id']=ids[h['owner_id']]
        if 'anchors' in h:h['anchors']=[ids.get(i,i) for i in h['anchors']]
    result.hardware.extend(manuals)
    from assembly import attach
    for p in list(new_parts):
        old=next(q for q in old_parts if q.id==p.id)
        if old.assembly.get('role') in ('door','shelf','drawer','back','worktop'):
            cfg=deepcopy(old.assembly.get('config',{}))
            if 'level' in cfg:cfg['level']*=height/source['height']
            result,_=attach(result,p,group,p.role,cfg)
    from fittings import refresh_handles
    from opening_options import refresh_openings
    for p in [p for p in result.parts if p.group==group]:
        refresh_handles(result,p.id);refresh_openings(result,p.id);p.validate()
        p.style_source['signature']=source_signature(p,p.style_source['origin'])
    result.validate();return result


# Verified C++ geometry calculations; exact CAD validation is retained.
from native_model import install_shapes as _install_cpp
_install_cpp(globals(), 'shaped_furniture')
