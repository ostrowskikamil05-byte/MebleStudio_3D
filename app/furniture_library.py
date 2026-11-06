"""Parametric furniture library: closed production geometry and motion metadata."""
from copy import deepcopy
from core import Part,add_template,finite,uid
from assembly import attach,DEFAULTS,inferred_role,suggested_size
from dimension_model import regenerate_hinges
from mechanisms import set_motion
from fittings import add_handle

TEMPLATES=[
('Kuchnia','Szafka dolna',600,720,560,'cabinet',1,1),('Kuchnia','Szafka wisząca',800,720,320,'cabinet',1,2),('Kuchnia','Szafka pod zlew',900,720,560,'cabinet',0,2),('Kuchnia','Szafka 3 szuflady',600,720,560,'drawers',0,3),('Kuchnia','Słupek cargo',300,1800,560,'cargo',0,1),('Kuchnia','Słupek spiżarniany',600,2100,560,'cabinet',5,2),('Kuchnia','Moduł pod piekarnik',600,720,560,'cabinet',1,0),('Kuchnia','Wyspa z blatem',1200,900,700,'island',2,2),
('Sypialnia','Szafa dwudrzwiowa',1200,2100,600,'cabinet',4,2),('Sypialnia','Szafa przesuwna',1800,2200,650,'sliding',4,2),('Sypialnia','Komoda 3 szuflady',900,800,480,'drawers',0,3),('Sypialnia','Szafka nocna',450,500,400,'cabinet',1,1),('Sypialnia','Łóżko 160 × 200',1640,450,2040,'bed',0,0),('Sypialnia','Toaletka',1000,760,450,'desk',0,0),
('Salon','Szafka RTV',1600,550,450,'cabinet',1,2),('Salon','Witryna',800,1800,400,'cabinet',4,2),('Salon','Regał książkowy',900,1900,320,'shelf',5,0),('Salon','Stolik kawowy',1000,450,600,'table',0,0),('Salon','Komoda z drzwiami',1200,900,450,'cabinet',2,2),
('Łazienka','Szafka pod umywalkę',800,600,450,'cabinet',0,2),('Łazienka','Słupek łazienkowy',400,1800,350,'cabinet',4,1),('Łazienka','Szafka wisząca z klapą',600,400,250,'lift',0,1),('Łazienka','Regał na ręczniki',500,1500,300,'shelf',4,0),
('Garaż i warsztat','Regał warsztatowy',1200,1800,500,'shelf',4,0),('Garaż i warsztat','Stół warsztatowy',1600,900,700,'table',0,0),('Garaż i warsztat','Szafka narzędziowa',800,1000,500,'cabinet',2,2),('Garaż i warsztat','Szafka na części — szuflady',800,900,500,'drawers',0,3),
('Ogród','Stół ogrodowy',1600,760,800,'table',0,0),('Ogród','Ławka ogrodowa',1400,850,500,'bench',0,0),('Ogród','Skrzynia ogrodowa z pokrywą',1000,550,500,'chest',0,0),('Ogród','Regał na donice',800,1400,400,'shelf',3,0),
('Biuro i przedpokój','Biurko',1200,760,600,'desk',0,0),('Biuro i przedpokój','Szafka na buty',800,1000,350,'cabinet',4,2),('Biuro i przedpokój','Regał biurowy',800,1800,400,'shelf',4,0)]

# Editable design defaults, appended to preserve indices in older projects.
TEMPLATES += [
('Kuchnia','Słupek pod piekarnik ze schowkami',600,2100,580,'cabinet',0,0),
('Kuchnia','Szafka zlewowa — listwy i otwarte plecy',800,720,560,'cabinet',0,2),
('Kuchnia','Szafka wisząca wąska',400,900,320,'cabinet',2,1),
('Kuchnia','Szafka wisząca pozioma',1200,360,350,'cabinet',1,2),
('Kuchnia','Otwarty moduł pod urządzenie',600,820,580,'shelf',0,0),
('Sypialnia','Szafa garderobiana trzykomorowa',1800,2200,600,'cabinet',4,1),
('Biuro i przedpokój','Niska szafka na obuwie',1000,800,350,'cabinet',3,2),
('Biuro i przedpokój','Wysoka szafka na obuwie',600,1800,350,'cabinet',5,2),
('Sypialnia','Komoda na bieliznę — 4 szuflady',800,1400,450,'drawers',0,4),
('Salon','Regał dwustronny z przegrodą',1200,1600,350,'shelf',4,0),
('Biuro i przedpokój','Kontenerek biurowy — 3 szuflady',450,600,500,'drawers',0,3),
('Biuro i przedpokój','Szafka na segregatory',1200,1200,380,'cabinet',3,2),
('Garaż i warsztat','Szafa warsztatowa trzykomorowa',1200,2000,600,'cabinet',4,1),
('Garaż i warsztat','Szafka górna garażowa',1000,600,400,'cabinet',1,2),
('Garaż i warsztat','Komoda warsztatowa — 4 szuflady',800,850,500,'drawers',0,4),
('Łazienka','Szafka pralnicza — listwy i otwarte plecy',600,900,650,'cabinet',1,1),
('Łazienka','Regał łazienkowy wąski',300,1800,300,'shelf',5,0),
('Łazienka','Zabudowa łazienkowa wisząca',1200,500,450,'cabinet',1,2),
]
PRESET_OPTIONS={
    34:dict(leg_height=0,back=False,hinge_count=3,sections=[dict(key='bottom',enabled=True,height=600,front='doors',doors=1,shelves=0),dict(key='main',enabled=True,front='open',shelves=0),dict(key='upper',enabled=True,height=600,front='doors',doors=1,shelves=0)]),
    35:dict(top_mode='rails',back=False),36:dict(leg_height=0),37:dict(leg_height=0),
    38:dict(leg_height=0,top_mode='rails',back=False),
    39:dict(leg_height=0,hinge_count=4,sections=[dict(key='main',enabled=True,columns=3,doors=1)]),
    40:dict(leg_height=80),41:dict(leg_height=0,hinge_count=3),42:dict(hinge_count=3),
    43:dict(leg_height=0,back=False,sections=[dict(key='main',enabled=True,columns=2)]),
    44:dict(leg_height=0),45:dict(leg_height=0),
    46:dict(leg_height=0,hinge_count=4,sections=[dict(key='main',enabled=True,columns=3,doors=1)]),
    47:dict(leg_height=0),48:dict(leg_height=0),49:dict(top_mode='rails',back=False),
    50:dict(leg_height=0),51:dict(leg_height=0),
}

def template_options(index):return deepcopy(PRESET_OPTIONS.get(index,{}))

def create_furniture(project,index,width=None,height=None,depth=None,thickness=18,origin=(0,0,0),group=None,material=None,front_material=None):
    category,name,w,h,d,kind,shelves,fronts=TEMPLATES[index]
    if index in PRESET_OPTIONS:
        from furniture_configurator import default_config,configure
        c=default_config(index,project)
        c.update(name=group or name,width=w if width is None else width,depth=d if depth is None else depth,thickness=thickness,origin=list(origin),
                 material=material or c['material'],front_material=front_material or c['front_material'])
        c['height']=(h if height is None else height)+c['leg_height']+(c['worktop_thickness'] if c['worktop'] else 0)
        return configure(project,c)
    w,h,d,t=[finite(x,1,10000) for x in (width or w,height or h,depth or d,thickness)]
    result=deepcopy(project);group=group or name;occupied={p.group for p in result.parts};base=group;n=2
    while group in occupied:group=base+f' ({n})';n+=1
    material=material or result.materials[0].id;front_material=front_material or result.materials[1].id
    ox,oy,oz=origin;created=[]
    def add(name,u,v,thick,plane,x,y,z,role='other'):
        p=Part(name=name,group=group,u=u,v=v,t=thick,plane=plane,x=ox+x,y=oy+y,z=oz+z,role=role,material=material);p.validate();result.parts.append(p);created.append(p);return p
    if kind in ('table','desk'):
        created=add_template(result,'table',w,h,d,t,shelves=0,front=False,origin=origin,group=group)
    elif kind=='bed':
        if min(w,d)<=t*4 or h<=100:raise ValueError('Zbyt małe wymiary łóżka.')
        add('Bok łóżka lewy',d-2*t,200,t,'YZ',0,t,h-200)
        add('Bok łóżka prawy',d-2*t,200,t,'YZ',w-t,t,h-200)
        add('Wezgłowie',w,h+400,t,'XZ',0,d-t,0)
        add('Zanóżek',w,h,t,'XZ',0,0,0)
        add('Belka środkowa',d-2*t,100,t,'YZ',w/2-t/2,t,h-120)
        for i in range(12):add(f'Listwa stelaża {i+1}',w-2*t,70,t,'XY',t,t+(d-2*t-70)*i/11,h-30)
    elif kind=='bench':
        seat_h=min(450,h*.6);leg=60
        add('Siedzisko',w,d,t,'XY',0,0,seat_h)
        for i,(x,y) in enumerate(((0,0),(w-leg,0),(0,d-leg),(w-leg,d-leg))):add(f'Noga {i+1}',leg,seat_h,leg,'XZ',x,y,0)
        add('Oparcie',w,h-seat_h,t,'XZ',0,d-t,seat_h)
    elif kind=='chest':
        created=add_template(result,'shelf',w,h,d,t,shelves=0,front=False,origin=origin,group=group)
        top=next(p for p in created if p.name=='Wieniec');result.parts.remove(top)
        add('Ścianka przednia',w-2*t,h-t,t,'XZ',t,0,t)
        add('Ścianka tylna',w-2*t,h-t,t,'XZ',t,d-t,t)
        lid=add('Pokrywa',w,d,t,'XY',0,0,h,role='other');lid.motion=dict(mode='lid',angle=95,travel=0)
    else:
        created=add_template(result,'shelf' if kind=='shelf' else 'cabinet',w,h,d,t,shelves=shelves,front=False,origin=origin,group=group)
        for p in created:p.material=material if p.name!='Plecy nakładane HDF' else next(m.id for m in result.materials if m.kind=='hdf')
        # Replace default shelf placement with linked shelf supports, maintaining their current level.
        for p in list(created):
            if inferred_role(p)=='shelf':
                p.u,p.v=suggested_size(result,group,'shelf',t)
                result,p=attach(result,p,group,'shelf',dict(level=p.z-oz))
        if kind=='drawers':
            cfg={**DEFAULTS,'drawer_depth':min(400,d-30),'drawer_height':min(120,(h-2*t)/fronts-40),'drawer_clearance':12.7,'drawer_raise':t+6}
            front_h=(h-2-2-fronts*2+2)/fronts
            for i in range(fronts):
                p=Part(name=f'Front szuflady {i+1}',group=group,u=w-4,v=front_h,t=t,material=front_material)
                result,p=attach(result,p,group,'drawer',{**cfg,'level':2+i*(front_h+2)})
                p.motion=dict(mode='drawer',travel=cfg['drawer_depth']*.9,angle=0)
                result=add_handle(result,p.id,128,u=p.u/2,v=p.v/2,vertical=False)
        elif fronts:
            for i in range(fronts):
                p=Part(name=f'Front {i+1}',group=group,u=(w-4-2*(fronts-1))/fronts,v=h-4,t=t,material=front_material)
                result,p=attach(result,p,group,'door',dict(hinge_side='left' if i==0 else 'right',auto_hardware=kind not in ('cargo','sliding','lift')))
                if kind in ('cargo','sliding','lift'):
                    mode='cargo' if kind=='cargo' else 'lift' if kind=='lift' else 'slide_right' if i==0 else 'slide_left'
                    result=set_motion(result,p.id,mode,100,min(d-50,450) if kind=='cargo' else p.u*.85)
                    if kind=='sliding':
                        q=next(q for q in result.parts if q.id==p.id);q.y-=i*(t+3);q.assembly['config']['slide_lane']=i*(t+3)
                else:result=regenerate_hinges(result,p.id,count=4 if h>1800 else 3 if h>1200 else 2)
                p=next(q for q in result.parts if q.id==p.id)
                result=add_handle(result,p.id,96 if p.u<140 else 128,u=p.u/2 if kind=='lift' else p.u-35 if i==0 else 35,v=40 if kind=='lift' else min(p.v-70,max(70,p.v/2)),vertical=kind!='lift')
        if kind=='island':add('Blat wyspy',w+40,d+40,38,'XY',-20,-20,h)
    if kind in ('table','desk'):
        for p in created:p.material=material
    result.validate();return result,group
