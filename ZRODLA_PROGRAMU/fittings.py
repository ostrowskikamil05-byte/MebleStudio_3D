"""Source-linked fitting recipes. Generic recipes are explicitly marked."""
from copy import deepcopy
import math
from core import Part,finite,uid
from features import hole_array
SYSTEM32_SOURCE='https://web.hettich.com/fileadmin/Media_Center/09_Further_products/01_Catalogues/Verarbeitungstechnik_2018_16918_en.pdf'
HINGE_SOURCE='https://www.hettich.com/fileadmin/Media_Center/Catalogue/Hinges_Cat_HUK_Online__NEW_96dpi%20.pdf'
RUNNER_SOURCE='https://shop.hettich.com/pl_EN/-/KA-5632-full-extension-runner%2C-load-class-45-kg%2C-12-7-mm-installed-width/bp/variant9396199120838'
CONNECTOR_SOURCE='https://www.hafele.com/us/en/products/furniture-fittings-living-solutions/connectors-shelf-supports/connectors/50/'
RECIPES=[
 dict(id='system32',category='System 32',name='System 32 — Ø5 / krok 32 / linia 37',diameter=5,depth=10,pitch=32,line=37,verified='rozstaw i średnica; głębokość robocza',source=SYSTEM32_SOURCE),
 dict(id='plate_direct',category='Zawiasy',name='Hettich Direkt — Ø5 × 7,5 / 32',diameter=5,depth=7.5,pitch=32,line=37,verified='otwory prowadnika; nie pełny zawias',source='https://shop.hettich.com/fr_FR/-/Hettich-Direkt%2C-r%C3%A9glage-en-hauteur-direct/bp/variant70030644287'),
 dict(id='hinge45',category='Zawiasy',name='Puszka Ø35 — wzór mocowania 45 × 9,5',diameter=35,depth=12.5,pitch=45,offset=9.5,pilot=2.8,pilot_depth=8,verified='puszka i rozstaw z katalogu; pilot roboczy',source=HINGE_SOURCE),
 dict(id='hinge52',category='Zawiasy',name='Puszka Ø35 — własny wzór mocowania 52 × 9',diameter=35,depth=12.5,pitch=52,offset=9,pilot=3,pilot_depth=8,verified='własny wzorzec roboczy; nie wzór Sensys TH52',source=''),
 dict(id='hinge_sensys52',category='Zawiasy',name='Hettich Sensys 9071205 TH52 — Ø35 × 12,8 / 52 × 5,5',diameter=35,depth=12.8,pitch=52,offset=5.5,pilot=2.5,pilot_depth=8,min_thickness=15,max_thickness=24,article='9071205',manufacturer='Hettich',closing_spring='sprung',verified='nakładany, przykręcany; pilot maks. Ø2,5; głębokość pilota 8 i prowadnik robocze',source='https://shop.hettich.com/si_EN/Hinges/Sensys-hinge-series/Hinges/Sensys-110%C2%B0-hinge-with-integrated-silent-system-%28Sensys-8645i%29%2C-nickel-plated%2C-overlay%2C-Opening-angle-110%C2%B0%2C-TH-drilling-pattern-52-x-5-5-mm%2C-for-screwing-on-%28-%29/p/9071205'),
 dict(id='hinge_sensys_tb55',category='Zawiasy',name='Sensys 9073655 — wiercenie TB55 Ø8 × 6 (wpuszczany)',diameter=35,depth=12.8,pitch=45,offset=9.5,pilot=8,pilot_depth=6,min_thickness=15,max_thickness=24,verified='wiercenia puszki konkretnego zawiasu wpuszczanego; montaż w korpusie ręczny',source='https://shop.hettich.com/us_EN/Hinges/Sensys-hinge-series/Hinges/Sensys-110%C2%B0-hinge-without-integrated-silent-system-%28Sensys-8645%29%2C-nickel-plated%2C-inset%2C-Opening-angle-110%C2%B0%2C-TB-drilling-pattern-45-x-9-5-mm%2C-Fix-fast-assembly-%28%C3%B8-8-x-6%29/p/9073655'),
 dict(id='confirmat7',category='Połączenia',name='Konfirmat 7 × 50 — pilot Ø5 / przelot Ø7',diameter=5,depth=35,clearance=7,countersink=10,countersink_depth=2,verified='pilot Ø5; pozostałe wartości edytowalne robocze',source=CONNECTOR_SOURCE),
 dict(id='confirmat5',category='Połączenia',name='Konfirmat 5 × 50 — pilot Ø3,5 (roboczy)',diameter=3.5,depth=35,clearance=5,verified='wzorzec roboczy',source=''),
 dict(id='dowel8',category='Połączenia',name='Kołek drewniany Ø8 × 30',diameter=8,depth=15,verified='wzorzec roboczy; głębokość na jedną część',source=''),
 dict(id='dowel6',category='Połączenia',name='Kołek drewniany Ø6 × 30',diameter=6,depth=15,verified='wzorzec roboczy',source=''),
 dict(id='dowel10',category='Połączenia',name='Kołek drewniany Ø10 × 40',diameter=10,depth=20,verified='wzorzec roboczy',source=''),
 dict(id='cam15',category='Połączenia',name='Mimośród Ø15 — kieszeń robocza',diameter=15,depth=12,verified='sam otwór pod puszkę, bez kompletnego złącza',source='https://files.hafele.co.uk/catalogfiles/www/14CFC294.pdf'),
 dict(id='screw35',category='Wkręty i śruby',name='Wkręt Ø3,5 — pilot Ø2,8 × 8',diameter=2.8,depth=8,verified='pilot z katalogu Blum',source='https://publications.blum.com/2024/catalogue/en/160/'),
 dict(id='screw4',category='Wkręty i śruby',name='Wkręt Ø4 — pilot Ø3 × 12 (roboczy)',diameter=3,depth=12,verified='wzorzec roboczy',source=''),
 dict(id='screw5',category='Wkręty i śruby',name='Wkręt Ø5 — pilot Ø3,5 × 16 (roboczy)',diameter=3.5,depth=16,verified='wzorzec roboczy',source=''),
 dict(id='m4',category='Wkręty i śruby',name='Śruba M4 — przelot Ø4,5',diameter=4.5,depth=0,verified='luz roboczy, nie gwintowanie',source=''),
 dict(id='m6',category='Wkręty i śruby',name='Śruba M6 — przelot Ø6,5',diameter=6.5,depth=0,verified='luz roboczy, nie gwintowanie',source=''),
 dict(id='m8',category='Wkręty i śruby',name='Śruba M8 — przelot Ø9',diameter=9,depth=0,verified='luz roboczy, nie gwintowanie',source=''),
 dict(id='shelf5',category='Podpórki',name='Podpórka półki Ø5 × 10',diameter=5,depth=10,verified='wzorzec roboczy',source=SYSTEM32_SOURCE),
 dict(id='shelf3',category='Podpórki',name='Podpórka półki Ø3 × 8',diameter=3,depth=8,verified='wzorzec roboczy',source='')]
HANDLES=[('Gałka',0),('Uchwyt 64',64),('Uchwyt 96',96),('Uchwyt 128',128),('Uchwyt 160',160),('Uchwyt 192',192),('Uchwyt 224',224),('Uchwyt 256',256),('Uchwyt 320',320)]
FINISHES={'Czarny mat':'#242628','Stal szczotkowana':'#adb4bb','Chrom':'#d3d9df','Złoty':'#b39a5e','Biały':'#f0efeb','Miedź':'#b87957'}

def recipe(recipe_id):return deepcopy(next(r for r in RECIPES if r['id']==recipe_id))

def system32(project,part_id,front=37,back=37,start=64,end=64,diameter=5,depth=10,both=True,face='top'):
    result=deepcopy(project);p=next(p for p in result.parts if p.id==part_id)
    start,end,front,back,diameter,depth=[finite(x,0,10000) for x in (start,end,front,back,diameter,depth)]
    if p.v-start-end<0:raise ValueError('Początek i koniec rzędu nie mieszczą się na boku.')
    p.operations=[o for o in p.operations if o.get('recipe_id')!='system32']
    for u in dict.fromkeys([front,p.u-back] if both else [front]):
        operations=hole_array(p,u,start,diameter,depth,nu=1,nv=math.floor((p.v-start-end)/32)+1,dv=32,face=face)
        for op in operations:op.update(recipe_id='system32',purpose='System 32',source=SYSTEM32_SOURCE)
        p.operations.extend(operations)
    result.validate();return result

def add_handle(project,part_id,spacing=128,u=None,v=None,vertical=True,color='#242628',projection=28):
    from assembly import accessory
    result=deepcopy(project);p=next(p for p in result.parts if p.id==part_id)
    spacing=finite(spacing,0,1000);projection=finite(projection,1,200)
    u=finite(u if u is not None else p.u-40,0,p.u);v=finite(v if v is not None else p.v/2,0,p.v)
    result.hardware=[h for h in result.hardware if not(h.get('owner_id')==p.id and h.get('kind')=='handle')]
    p.operations=[o for o in p.operations if o.get('recipe_id')!='handle']
    centers=[(u,v)] if spacing==0 else [(u,v-spacing/2),(u,v+spacing/2)] if vertical else [(u-spacing/2,v),(u+spacing/2,v)]
    for a,b in centers:
        op=hole_array(p,a,b,4.5,p.t,face='bottom')[0];op.update(recipe_id='handle',purpose='Przelot pod śrubę M4 uchwytu');p.operations.append(op)
    accessory(result,p,'Gałka' if spacing==0 else f'Uchwyt {spacing:g} mm',1,0,[],[])
    result.hardware[-1].update(kind='handle',manual=True,design_u=p.u,design_v=p.v,u_anchor='right' if p.u-u<80 else 'left' if u<80 else 'center' if abs(u-p.u/2)<.1 else 'fixed',v_anchor='center' if abs(v-p.v/2)<.1 else 'fixed',u=u,v=v,spacing=spacing,vertical=bool(vertical),color=color,projection=projection)
    result.validate();return result

def cabinet_connectors(project,group,recipe_id='confirmat7',front=50,back=50):
    """Drill mating structural panels in world coordinates, axis aligned cabinet only."""
    from assembly import frame,inferred_role,require_sides,accessory
    result=deepcopy(project);f=frame(result,group);left,right=require_sides(f);r=recipe(recipe_id)
    if recipe_id not in ('confirmat7','confirmat5','dowel8','dowel6','dowel10'):raise ValueError('Ten wzór nie obsługuje automatycznego połączenia korpusu.')
    tag='connector:'+group
    result.hardware=[h for h in result.hardware if h.get('connection_tag')!=tag]
    for p in result.parts:p.operations=[o for o in p.operations if o.get('connection_tag')!=tag]
    panels=[p for p in f['parts'] if inferred_role(p) in ('top','bottom') and p.plane=='XY']
    if not panels:raise ValueError('Brak dna lub wieńca między bokami.')
    for panel in panels:
        if abs(panel.x-f['inner_left'])>.1 or abs(panel.x+panel.u-f['inner_right'])>.1:raise ValueError('Dno i wieniec muszą przylegać do boków.')
        qty=0
        for side,outer,edge in ((left,'bottom','u0'),(right,'top','u1')):
            for distance in dict.fromkeys([front,panel.v-back]):
                y=panel.y+distance;z=panel.z+panel.t/2
                if recipe_id.startswith('dowel'):
                    diameter=r['diameter'];side_depth=min(r['depth'],side.t-2);side_face='top' if side is left else 'bottom'
                else:diameter=r['clearance'];side_depth=side.t;side_face=outer
                a=hole_array(side,y-side.y,z-side.z,diameter,side_depth,face=side_face)[0]
                b=hole_array(panel,distance,panel.t/2,r['diameter'],r['depth'],face=edge)[0]
                if recipe_id=='confirmat7':a.update(countersink_diameter=r['countersink'],countersink_depth=r['countersink_depth'])
                for op,p in ((a,side),(b,panel)):
                    op.update(recipe_id=recipe_id,connection_tag=tag,owner_id=panel.id,purpose=r['name'],source=r['source']);p.operations.append(op)
                qty+=1
        accessory(result,panel,r['name'],qty,0,[left,right],[]);result.hardware[-1].update(kind='connector',connection_tag=tag,recipe_id=recipe_id,front=front,back=back,source=r['source'])
    result.validate();return result


def refresh_handles(project,part_id):
    p=next(q for q in project.parts if q.id==part_id)
    handles=[h for h in project.hardware if h.get('owner_id')==part_id and h.get('kind')=='handle']
    if not handles:return
    p.operations=[o for o in p.operations if o.get('recipe_id')!='handle']
    for h in handles:
        if h.get('u_anchor')=='right':h['u']=p.u-(h.get('design_u',p.u)-h['u'])
        elif h.get('u_anchor')=='center':h['u']=p.u/2
        if h.get('v_anchor')=='center':h['v']=p.v/2
        h['design_u']=p.u;h['design_v']=p.v
        spacing=h['spacing'];u,v=h['u'],h['v']
        centers=[(u,v)] if not spacing else [(u,v-spacing/2),(u,v+spacing/2)] if h['vertical'] else [(u-spacing/2,v),(u+spacing/2,v)]
        for a,b in centers:
            op=hole_array(p,a,b,4.5,p.t,face='bottom')[0];op.update(recipe_id='handle',purpose='Przelot pod śrubę M4 uchwytu');p.operations.append(op)
