"""Presentation-only opening transforms: production model remains closed."""
import copy,math
from core import rotate,local_world,finite
MODES={'Bez mechanizmu':'none','Zawias lewy':'hinge_left','Zawias prawy':'hinge_right','Szuflada — wysuw':'drawer','Drzwi przesuwne w lewo':'slide_left','Drzwi przesuwne w prawo':'slide_right','Klapa do góry':'lift','Klapa w dół':'flap_down','Cargo — wysuw':'cargo','Pokrywa skrzyni':'lid'}

def effective_motion(part):
    from assembly import inferred_role
    if part.motion:return part.motion
    role=inferred_role(part)
    if role=='door':return dict(mode='hinge_'+part.assembly.get('config',{}).get('hinge_side','right' if part.name.endswith('2') else 'left'),angle=100,travel=part.u)
    if role=='drawer':return dict(mode='drawer',travel=part.assembly.get('config',{}).get('drawer_depth',400),angle=0)
    return dict(mode='none')

def posed_project(project,amounts):
    result=copy.deepcopy(project)
    for original in project.parts:
        amount=max(0,min(1,amounts.get(original.id,0)))
        if not amount:continue
        motion=effective_motion(original);mode=motion['mode'];p=next(p for p in result.parts if p.id==original.id)
        if mode in ('hinge_left','hinge_right','lift','flap_down','lid'):
            if mode.startswith('hinge'):
                if p.plane not in ('XZ','YZ') or any(abs(x)>1e-6 for x in (p.rx,p.ry)):continue
            elif any(abs(x)>1e-6 for x in (p.rx,p.ry,p.rz)) or p.plane!=('XY' if mode=='lid' else 'XZ'):continue
            angle=motion.get('angle',100)*amount
            if mode=='lid':axis='x';angle=-angle;pivot=local_world(p,0,p.v,0)
            elif mode.startswith('hinge'):
                axis='z';angle=-angle if mode=='hinge_left' else angle;pivot=local_world(p,0 if mode=='hinge_left' else p.u,0,p.t)
                if p.plane=='YZ':angle=-angle
                angle*=p.assembly.get('config',{}).get('corner_bay',{}).get('opening_sign',1)
                if p.assembly.get('config',{}).get('corner_bay',{}).get('cup_face')=='bottom':pivot=local_world(p,0 if mode=='hinge_left' else p.u,0,0)
            else:axis='x';angle=-angle if mode=='lift' else angle;pivot=local_world(p,0,p.v if mode=='lift' else 0,p.t)
            r={'x':(angle,0,0),'z':(0,0,angle)}[axis]
            delta=rotate(tuple(a-b for a,b in zip((p.x,p.y,p.z),pivot)),*r)
            p.x,p.y,p.z=tuple(a+b for a,b in zip(pivot,delta));p.rx+=r[0];p.rz+=r[2]
        elif mode in ('drawer','cargo','slide_left','slide_right'):
            travel=motion.get('travel',400)*amount;axis='x' if mode.startswith('slide') else 'y';sign=1 if mode=='slide_right' else -1
            setattr(p,axis,getattr(p,axis)+sign*travel)
            for child in result.parts:
                if child.assembly.get('generated_by')==p.id:setattr(child,axis,getattr(child,axis)+sign*travel)
    return result

def set_motion(project,part_id,mode,angle=100,travel=400):
    from assembly import inferred_role,attach,frame,accessory
    if mode not in MODES.values():raise ValueError('Nieznany mechanizm.')
    result=copy.deepcopy(project);p=next(p for p in result.parts if p.id==part_id)
    if p.assembly.get('generated_by'):raise ValueError('Wybierz front zamiast części skrzynki.')
    if p.assembly.get('config',{}).get('corner_bay'):
        if mode not in ('none','hinge_'+p.assembly['config'].get('hinge_side','left')):raise ValueError('Front narożnika otwiera się przy boku z wykonanym wzorcem zawiasów. Zmień kierunek całego narożnika w kreatorze.')
        p.motion=dict(mode=mode,angle=finite(angle,0,180),travel=0);result.validate();return result
    if p.assembly.get('config',{}).get('corner_diagonal'):
        if mode!='none':raise ValueError('Front skośny wymaga osobno dobranego rzeczywistego mechanizmu. Automatyczne otwieranie jest wyłączone.')
        p.motion=dict(mode='none');result.validate();return result
    if p.plane!=('XY' if mode=='lid' else 'XZ') or any(abs(x)>1e-6 for x in (p.rx,p.ry,p.rz)):raise ValueError('Mechanizm frontu wymaga płaszczyzny XZ i zerowych obrotów.')
    angle=finite(angle,0,180);travel=finite(travel,0,3000)
    result.hardware=[h for h in result.hardware if not(h.get('owner_id')==p.id and h.get('kind') in ('lift','sliding','cargo'))]
    if mode.startswith('hinge'):
        cfg={**p.assembly.get('config',{}),'hinge_side':'left' if mode=='hinge_left' else 'right','auto_hardware':True}
        result,p=attach(result,p,p.group,'door',cfg)
    elif mode in ('lift','flap_down','slide_left','slide_right','cargo'):
        if inferred_role(p)=='drawer':raise ValueError('Dla skrzynki szuflady wybierz wysuw. Do cargo użyj osobnego frontu.')
        for q in result.parts:q.operations=[o for o in q.operations if o.get('owner_id')!=p.id]
        result.hardware=[h for h in result.hardware if h.get('owner_id')!=p.id or h.get('manual')]
        if p.assembly:p.assembly['config']['auto_hardware']=False
        f=frame(result,p.group);kind='cargo' if mode=='cargo' else 'lift' if mode in ('lift','flap_down') else 'sliding'
        name={'cargo':'Cargo wysuwane — model koncepcyjny','lift':'Mechanizm klapy — model koncepcyjny','sliding':'Prowadzenie drzwi przesuwnych — model koncepcyjny'}[kind]
        accessory(result,p,name,1,0,[],[])
        result.hardware[-1].update(kind=kind,manual=True,model_only=True,width=f['inner_right']-f['inner_left']-30,depth=f['upper'][1]-f['lower'][1]-45,height=f['inner_top']-f['inner_bottom']-110,origin=[f['inner_left']+15,f['lower'][1]+20,f['inner_bottom']+20],baskets=3,closed_y=p.y,color='#a5abb3',source={'cargo':'https://www.kesseboehmer.com/en/storage-solutions/kitchen/larder-units/dispensa','lift':'https://www.blum.com/no/en/products/liftsystems/aventos-hk-top/downloads-videos/','sliding':'https://hta.hettich.com/en-de/sliding-door-systems/topline-l'}[kind])
    p.motion=dict(mode=mode,angle=angle,travel=travel);result.validate();return result


def refresh_mechanism_hardware(project,part_id):
    from assembly import frame
    p=next(q for q in project.parts if q.id==part_id)
    items=[h for h in project.hardware if h.get('owner_id')==p.id and h.get('kind') in ('cargo','lift','sliding')]
    if not items:return
    f=frame(project,p.group)
    for h in items:
        h.update(width=f['inner_right']-f['inner_left']-30,depth=f['upper'][1]-f['lower'][1]-45,height=f['inner_top']-f['inner_bottom']-110,origin=[f['inner_left']+15,f['lower'][1]+20,f['inner_bottom']+20],closed_y=p.y)
        if h['kind']=='cargo' and min(h['width'],h['depth'],h['height'])<=0:raise ValueError('Korpus jest za mały dla modelu cargo.')
