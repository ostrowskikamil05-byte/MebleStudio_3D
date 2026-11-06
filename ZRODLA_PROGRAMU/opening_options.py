"""Handle alternatives; sourced ejector bores and editable finger-pull machining."""
import copy
from core import finite, world, local_world, layout_point
from features import hole_array

BLUM='https://d2.blum.com/services/BEC003/tip0001-ep-234_ep_dok_bau_%24sen_%24aof_%24v6.pdf'
SALICE='https://www.salice.com/downloads/2853/2143/Salice-Magnetic-Push-applications-ENG.pdf'
OPTIONS={
 'tip_short':dict(label='Blum TIP-ON 956.1004 — magnes, krótki',diameter=10,depth=50,gap=2.6,hinge_spring='unsprung',source=BLUM,note='Zawiasy bez sprężyny; front nakładany do ok. 1300 mm; płytka przyklejana. Nie dobiera numeru zawiasu.'),
 'tip_long':dict(label='Blum TIP-ON 956A1004 — magnes, długi',diameter=10,depth=76,gap=2.6,hinge_spring='unsprung',source=BLUM,note='Zawiasy bez sprężyny; wysokie fronty; płytka przyklejana. Program obsługuje fronty nakładane.'),
 'tip_bumper':dict(label='Blum TIP-ON 956A1006 — odbojnik',diameter=10,depth=76,gap=3.2,hinge_spring='sprung',source=BLUM,note='Wariant do zawiasów ze sprężyną; dobór siły wymaga sprawdzenia. Bez płytki magnesu.'),
 'salice_push':dict(label='Salice DPM — Magnetic Push',diameter=10,depth=40,gap=3,source=SALICE,note='Otwór Ø10 o głębokości minimum 40 mm. Płytka DP38XX91 przyklejana. Luz 3 mm jest ustawieniem roboczym.'),
 'edge_pull':dict(label='Uchwyt krawędziowy / profil — własny',source='',note='Własny model zakupowy; długość i wysunięcie edytowalne. Brak automatycznych wierceń.'),
 'finger_pocket':dict(label='Uchwyt frezowany — kieszeń podłużna',source='',note='Własna kieszeń na zewnętrznej powierzchni frontu. Wymiary i głębokość dobierz do materiału.'),
 'finger_slot':dict(label='Uchwyt wycięty — otwór podłużny',source='',note='Własne wycięcie przelotowe z końcami półkolistymi.'),
 'none':dict(label='Gładki front — usuń uchwyt',source='',note='Usuwa uchwyt i poprzednią obróbkę otwierania.')}

def clear(project,part_id):
    for p in project.parts:
        p.operations=[o for o in p.operations if o.get('opening_owner')!=part_id and not(p.id==part_id and o.get('recipe_id')=='handle')]
        if p.id==part_id:p.machining=[c for c in p.machining if c.get('opening_owner')!=part_id]
    project.hardware=[h for h in project.hardware if not(h.get('owner_id')==part_id and h.get('kind') in ('handle','opening'))]

def _spring_state(part, spec, requested=None):
    cfg=part.assembly.get('config',{})
    state=requested if requested is not None else cfg.get('hinge_spring','unknown')
    if state not in ('unknown','sprung','unsprung'):
        raise ValueError('Stan zawiasu: unknown, sprung albo unsprung.')
    # The sourced Sensys screw-on recipe is an integrated closing hinge, not
    # the generic 52 mm drilling pattern of an unsprung alternative.
    if cfg.get('hinge_pattern')=='hinge_sensys52':
        if state=='unsprung':raise ValueError('Wybrany Sensys 9071205 ma sprężynę zamykającą. Zmień zawias, zanim dobierzesz magnetyczny TIP-ON.')
        state='sprung'
    required=spec.get('hinge_spring')
    if required=='unsprung' and state!='unsprung':
        raise ValueError('Magnetyczny TIP-ON wymaga zawiasów bez sprężyny. Wybierz właściwy zawias i potwierdź „Bez sprężyny” w konfiguracji otwierania.')
    if required=='sprung' and state=='unsprung':
        raise ValueError('TIP-ON z odbojnikiem jest przeznaczony do zawiasów ze sprężyną. Dla zawiasu bez sprężyny wybierz wariant magnetyczny.')
    return state


def install(project,part_id,option='tip_bumper',length=120,width=24,depth=8,level=None,color='#242628',
            *,hinge_spring=None,center_u=None,angle=0,profile_edge='top'):
    result=copy.deepcopy(project);p=next(q for q in result.parts if q.id==part_id)
    if p.assembly.get('generated_by'):raise ValueError('Wybierz front, a nie część skrzynki.')
    if option not in OPTIONS:raise ValueError('Nieznany sposób otwierania.')
    if p.plane!='XZ' and option not in ('none','finger_pocket','finger_slot'):raise ValueError('Wybierz pionowy front w płaszczyźnie XZ.')
    clear(result,part_id)
    if option=='none':result.validate();return result
    spec=OPTIONS[option]
    if 'diameter' in spec:
        if p.role!='door' or not p.assembly:raise ValueError('Wypychacz wymaga drzwiczek połączonych z korpusem.')
        from assembly import attach
        state=_spring_state(p,spec,hinge_spring)
        cfg={**p.assembly['config'],'opening_gap':spec['gap'],'hinge_spring':state}
        result,p=attach(result,p,p.group,'door',cfg)
        if option=='tip_short' and p.v>1300:raise ValueError('Krótki TIP-ON: front do ok. 1300 mm; wybierz długi wariant.')
    length=finite(length,10,2000);width=finite(width,4,200);depth=finite(depth,.1,200)
    level=finite(level if level is not None else p.v/2,0,p.v)
    center_u=finite(center_u if center_u is not None else p.u/2,0,p.u)
    angle=finite(angle,-360,360)
    if profile_edge not in ('top','bottom','left','right'):raise ValueError('Wybierz górną, dolną, lewą albo prawą krawędź profilu.')
    result.hardware.append(dict(kind='opening',manual=True,owner_id=part_id,name=spec['label'],qty=1,price=0,option=option,
        length=length,width=width,depth=depth,level=level,level_ratio=level/p.v,
        center_u=center_u,u_ratio=center_u/p.u,angle=angle,profile_edge=profile_edge,
        color=color,source=spec['source'],note=spec['note'],requires_review=True))
    refresh_openings(result,part_id);result.validate();return result

def refresh_openings(project,part_id):
    p=next((p for p in project.parts if p.id==part_id),None)
    if p is None:return
    hs=[h for h in project.hardware if h.get('owner_id')==part_id and h.get('kind')=='opening']
    if not hs:return
    for q in project.parts:q.operations=[o for o in q.operations if o.get('opening_owner')!=part_id]
    p.machining=[c for c in p.machining if c.get('opening_owner')!=part_id]
    for h in hs:
        h['level']=h.get('level_ratio',.5)*p.v;option=h['option'];spec=OPTIONS[option]
        h['center_u']=h.get('u_ratio',.5)*p.u
        if 'diameter' in spec:
            state=_spring_state(p,spec)
            h['hinge_spring']=state
            h['compatibility_status']='Zawias wymaga potwierdzenia sprężyny i sprawdzenia siły wypychania.' if state=='unknown' else 'Rodzaj sprężyny zgodny; dobierz konkretny artykuł zawiasu i siłę wypychania.'
            from assembly import frame_for_part,require_sides
            f=frame_for_part(project,p);left,right=require_sides(f);side=right if p.assembly.get('config',{}).get('hinge_side','left')=='left' else left
            level=p.z+h['level']-side.z
            op=hole_array(side,level,side.t/2,spec['diameter'],spec['depth'],face='u0')[0]
            op.update(opening_owner=part_id,owner_id=part_id,purpose=spec['label'],source=spec['source']);side.operations.append(op)
            h['anchors']=[side.id];h['origin']=local_world(side,0,level,side.t/2);h['length']=spec['depth'];h['width']=spec['diameter']
        elif option in ('finger_pocket','finger_slot'):
            cut=dict(type='cutout' if option=='finger_slot' else 'pocket',shape='slot',face='bottom',u=h['center_u'],v=h['level'],
                length=h['length'],width=h['width'],depth=p.t if option=='finger_slot' else h['depth'],angle=h.get('angle',0),opening_owner=part_id,purpose=spec['label'])
            p.machining.append(cut)

def opening_faces(project,p,h):
    from fitting_visuals import box,rod
    if h['option']=='edge_pull':
        edge=h.get('profile_edge','top');vertical=edge in ('left','right')
        length=min(h['length'],p.v if vertical else p.u)
        limit=p.v if vertical else p.u
        local=box((limit-length)/2,-3,-h['depth'],length,3,h['depth'])+box((limit-length)/2,-3,-h['depth'],length,18,3)
        if edge=='top':local=[[(u,p.v+v,t) for u,v,t in face] for face in local]
        elif edge=='bottom':local=[[(u,-v,t) for u,v,t in face] for face in local]
        elif edge=='left':local=[[(v,u,t) for u,v,t in face] for face in local]
        else:local=[[(p.u-v,u,t) for u,v,t in face] for face in local]
        return [[world(p,*pt) for pt in face] for face in local]
    if 'origin' in h:
        x,y,z=h['origin'];return [[layout_point(p,point) for point in face] for face in rod((x,y-1,z),(x,y+h['length'],z),h['width']/2)]
    return []
