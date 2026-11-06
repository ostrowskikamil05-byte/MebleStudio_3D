"""Manufacturing geometry descriptions in a part's local coordinates."""
import copy
import math
from core import finite,inside,uid

FACES={'Góra (+T)':'top','Dół (−T)':'bottom','Krawędź U=0':'u0','Krawędź U=max':'u1','Krawędź V=0':'v0','Krawędź V=max':'v1'}

def validate_drill(part,op):
    face=op.get('face')
    if op.get('type')!='drill' or face not in FACES.values():raise ValueError('Nieprawidłowy typ lub strona wiercenia.')
    a,b,d,h=[finite(op[k],0,10000) for k in ('u','v','diameter','depth')]
    if d<=0 or h<=0:raise ValueError('Średnica i głębokość muszą być dodatnie.')
    if op.get('countersink_diameter'):
        outer=finite(op['countersink_diameter'],d,10000);height=finite(op.get('countersink_depth',0),.1,h)
        d=outer
    limits={'top':(part.u,part.v,part.t),'bottom':(part.u,part.v,part.t),
            'u0':(part.v,part.t,part.u),'u1':(part.v,part.t,part.u),
            'v0':(part.u,part.t,part.v),'v1':(part.u,part.t,part.v)}[face]
    if not d/2<=a<=limits[0]-d/2 or not d/2<=b<=limits[1]-d/2 or h>limits[2]:
        raise ValueError('Wiercenie wykracza poza powierzchnię lub grubość w kierunku wiercenia.')
    if face in ('top','bottom'):
        for i in range(72):
            if not inside((a+d/2*math.cos(i*math.tau/72),b+d/2*math.sin(i*math.tau/72)),part.points()):
                raise ValueError('Wiercenie wykracza poza kontur.')

def drill_location(part,op):
    a,b=op['u'],op['v'];face=op['face']
    return {'top':((a,b,part.t),(0,0,-1)),'bottom':((a,b,0),(0,0,1)),
        'u0':((0,a,b),(1,0,0)),'u1':((part.u,a,b),(-1,0,0)),
        'v0':((a,0,b),(0,1,0)),'v1':((a,part.v,b),(0,-1,0))}[face]

def validate_cut(part,cut):
    if cut.get('type') not in ('pocket','cutout','groove') or cut.get('shape') not in ('rect','circle','slot'):
        raise ValueError('Obróbka: kieszeń, otwór przelotowy lub rowek; kształt prostokątny, kołowy lub podłużny.')
    if cut.get('face') not in ('top','bottom'):raise ValueError('Frezowanie obsługuje powierzchnie +T i −T.')
    u,v=finite(cut['u'],0,10000),finite(cut['v'],0,10000)
    length,width=finite(cut['length'],.1,10000),finite(cut['width'],.1,10000)
    depth=part.t if cut['type']=='cutout' else finite(cut['depth'],.1,part.t)
    if cut['type']!='cutout' and depth>=part.t:raise ValueError('Kieszeń i rowek muszą mieć głębokość mniejszą od grubości. Dla otworu wybierz wycięcie przelotowe.')
    if cut['shape']=='circle':length=width
    if cut['shape']=='slot' and length<width:raise ValueError('Długość otworu podłużnego musi być co najmniej równa jego szerokości.')
    if cut['shape']=='circle':
        samples=[(u+width/2*math.cos(i*math.tau/72),v+width/2*math.sin(i*math.tau/72)) for i in range(72)]
    else:samples=[(u+du*length/2,v+dv*width/2) for du,dv in ((-1,-1),(1,-1),(1,1),(-1,1))]
    angle=math.radians(finite(cut.get('angle',0),-360,360))
    samples=[(u+(x-u)*math.cos(angle)-(y-v)*math.sin(angle),v+(x-u)*math.sin(angle)+(y-v)*math.cos(angle)) for x,y in samples]
    if any(not -1e-7<=x<=part.u+1e-7 or not -1e-7<=y<=part.v+1e-7 for x,y in samples):raise ValueError('Kontur obróbki wykracza poza wymiar formatki.')
    if not all(inside(point,part.points()) for point in samples):raise ValueError('Obróbka wykracza poza kontur części.')

def hole_array(part,u,v,diameter,depth,nu=1,nv=1,du=32,dv=32,face='top'):
    nu,nv=int(finite(nu,1,100)),int(finite(nv,1,100))
    if nu*nv>500:raise ValueError('Maksymalnie 500 otworów w jednym szyku.')
    batch=uid();operations=[]
    for j in range(nv):
        for i in range(nu):
            op=dict(type='drill',u=u+i*du,v=v+j*dv,diameter=diameter,depth=depth,face=face,batch=batch,purpose='szyk ręczny')
            validate_drill(part,op);operations.append(op)
    return operations

def replace_part(project,part):
    from assembly import rebuild_dependents
    result=copy.deepcopy(project);index=next(i for i,p in enumerate(result.parts) if p.id==part.id)
    result.parts[index]=copy.deepcopy(part);result=rebuild_dependents(result,part.id);result.validate();return result


def shift_features(part,du=0,dv=0,dt=0):
    """Keep drilling coordinates attached to material when a local origin moves."""
    for op in part.operations:
        face=op['face']
        a,b=(du,dv) if face in ('top','bottom') else (dv,dt) if face.startswith('u') else (du,dt)
        op['u']-=a;op['v']-=b
    for cut in part.machining:cut['u']-=du;cut['v']-=dv


# Verified C++ geometry calculations; exact CAD validation is retained.
from native_model import install_shapes as _install_cpp
_install_cpp(globals(), 'features')
