"""Cross-language parity against unchanged 0.14 algorithms; no GUI/GPU.

Run with the diagnostic Python and -u -B. Numeric tolerance is 2e-8 absolute
and 2e-11 relative. Mutation ports reconstruct the original Part model;
normalized_part additionally invokes the retained full Part.validate boundary.
"""
import argparse
import copy
import ctypes
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import random
import re
import sys
import time

BASE=Path(__file__).resolve().parents[3]
ORACLE=BASE/'outputs'/'MebelStudio3D_0_14'
sys.path.insert(0,str(ORACLE))
import shaped_furniture as shapes
import features
from core import Part,Project
from sketch import normalized_part

MUTATIONS={'resize_profile','refresh_decor','remove_decor','shift_features'}

def serial(value):
    if isinstance(value,Part):return serial(asdict(value))
    if isinstance(value,Project):return serial(value.data())
    if isinstance(value,(list,tuple)):return [serial(item) for item in value]
    if isinstance(value,dict):return {key:serial(item) for key,item in value.items()}
    return value

def oracle_transform(points,action):
    if not points:raise ValueError('Najpierw narysuj kontur.')
    xmin,xmax=min(p[0] for p in points),max(p[0] for p in points)
    ymin,ymax=min(p[1] for p in points),max(p[1] for p in points)
    cx,cy=(xmin+xmax)/2,(ymin+ymax)/2
    return [(2*cx-u,v) if action=='mirror_u' else (u,2*cy-v) if action=='mirror_v' else (cx-(v-cy),cy+(u-cx)) for u,v in points]

def oracle_constrain(points,index,closed=True,kind=None,length=None):
    if index<0 or index>=len(points) or len(points)<2:raise ValueError('Wybierz punkt początkowy odcinka na liście.')
    j=(index+1)%len(points)
    if not closed and j==0:raise ValueError('Ostatni punkt otwartego konturu nie ma kolejnego odcinka.')
    a,b=points[index],points[j];result=points[:]
    if kind=='horizontal':result[j]=(b[0],a[1])
    elif kind=='vertical':result[j]=(a[0],b[1])
    else:
        if length is None:return None
        old=math.dist(a,b)
        if old==0:raise ValueError('Punkty się pokrywają.')
        result[j]=(a[0]+(b[0]-a[0])*length/old,a[1]+(b[1]-a[1])*length/old)
    return result

def oracle(case):
    module,function,args,kwargs=case['module'],case['function'],copy.deepcopy(case['args']),copy.deepcopy(case['kwargs'])
    try:
        if module=='shaped_furniture':fn=getattr(shapes,function)
        elif module=='features':fn=getattr(features,function)
        else:fn={'normalized_part':normalized_part,'transform_points':oracle_transform,'constrain_points':oracle_constrain}[function]
        result=fn(*args,**kwargs)
        if function in MUTATIONS:result=args[0]
        return dict(ok=True,result=serial(result))
    except Exception as error:return dict(ok=False,error=str(error),type=type(error).__name__)

def cases():
    output=[]
    def add(module,function,*args,label=None,**kwargs):
        output.append(dict(id=label or f'{module}.{function}/{len(output)+1}',module=module,function=function,args=list(args),kwargs=kwargs))
    for value in (4,12,24,'8',4.5,3,25,True,'nan'):
        add('shaped_furniture','_resolution',value)
    for kind in shapes.SHAPES.values():
        for segments in (4,12,24):
            for u,v in ((400,400),(800,350),(270,910)):
                if kind=='circle' and u!=v:continue
                opts=dict(segments=segments,radii=(0,25,70,45),taper=10)
                add('shaped_furniture','contour',kind,u,v,**opts)
                add('shaped_furniture','shape_definition',kind,u,v,**opts)
    for kind in shapes.SHAPES.values():
        u,v=(600,600) if kind=='circle' else (800,450)
        profile=shapes.shape_definition(kind,u,v,radii=(0,25,80,40),arch_rise=100)
        add('shaped_furniture','profile_area',profile,u,v)
        add('shaped_furniture','profile_perimeter',profile,u,v)
    for a,b in ((200,200),(5000,.1),(12,890),(.5,.7),(0,100),(100,0)):
        add('shaped_furniture','_ellipse_perimeter',a,b)
    for args,kwargs in [
        (('circle',100,120),{}),(('rounded',100,100),dict(radii=(60,60,0,0))),
        (('ellipse',100,100),dict(segments=4.5)),(('arch',100,100),dict(arch_rise=101)),
        (('taper',100,100),dict(taper=50)),(('ellipse',.1,.1),{}),
        (('unknown',100,100),{}),(('rounded',200,100),dict(radii=[5,10])),
        (('rounded',200,100),dict(radii=[0,-1,10,20])),(('rectangle',100,100),dict(segments=3)),
        (('capsule',800,300),dict(radii=[5,10])),(('half_ellipse',400,300),dict(arch_rise=9000)),
    ]:add('shaped_furniture','contour',*args,**kwargs)
    add('shaped_furniture','shape_definition','rectangle',100,100,radii=[1,2])
    add('shaped_furniture','shape_definition','capsule',400,300,radii=[1,2])
    add('shaped_furniture','shape_definition','rounded',400,300,arch_rise=-1)
    add('shaped_furniture','_finish',[(0,0),(100,0),(100,100),(0,100),(0,0)],100,100)
    add('shaped_furniture','_finish',[(.01,.02),(100,0),(100,100),(0,100)],100,100)

    for kind in ('ellipse','rounded','arch','taper'):
        part=Part(u=400,v=600,t=20,profile=shapes.shape_definition(kind,400,600,radii=(0,10,60,20),arch_rise=90))
        part.contour=shapes.contour(kind,400,600,radii=(0,10,60,20),arch_rise=90)
        add('shaped_furniture','resize_profile',part,800,900)
    add('shaped_furniture','resize_profile',Part(u=400,v=300,contour=[(0,0),(400,0),(200,300)]),800,600)
    add('shaped_furniture','resize_profile',Part(),800,600)
    part=Part(u=400,v=600,t=18,decor=dict(type='fluting',width=8,pitch=32,depth=2,margin_u=30,margin_v=20,face='bottom'))
    part.machining=[dict(type='pocket',shape='circle',face='top',u=200,v=300,length=10,width=10,depth=2,angle=0),dict(generated_decor='fluting'),dict(generated_decor=None)]
    add('shaped_furniture','refresh_decor',part)
    for cfg in (dict(type='fluting'),dict(type='bad'),dict(type='fluting',face='u0'),dict(type='fluting',width=30,pitch=10),dict(type='fluting',margin_u=200),dict(type='fluting',depth=18)):
        q=copy.deepcopy(part);q.decor=cfg;add('shaped_furniture','refresh_decor',q)
    q=Part(u=10000,v=600,t=18,decor=dict(type='fluting',width=.5,pitch=1,margin_u=0));add('shaped_furniture','refresh_decor',q)
    add('shaped_furniture','remove_decor',part)
    project=Project();project.parts=[Part(group='shape',style_source=dict(origin=[20,-30,40],width=800,height=900,depth=450))]
    add('shaped_furniture','styled_bounds',project,'shape');add('shaped_furniture','styled_bounds',project,'missing')

    rectangle=Part(u=400,v=300,t=18)
    irregular=Part(u=400,v=300,t=18,contour=shapes.contour('ellipse',400,300))
    for face in ('top','bottom','u0','u1','v0','v1'):
        op=dict(type='drill',face=face,u=80,v=9,diameter=4,depth=8)
        add('features','validate_drill',rectangle,op);add('features','drill_location',rectangle,op)
        add('features','hole_array',rectangle,80,9,4,8,nu=3,nv=1,du=32,face=face)
    for changes in (dict(type='cut'),dict(face=None),dict(diameter=0),dict(depth=0),dict(u=-1),dict(v=2),dict(depth=30),dict(countersink_diameter=25,countersink_depth=2),dict(countersink_diameter=8,countersink_depth=9),dict(countersink_diameter=3,countersink_depth=2),dict(countersink_diameter=8)):
        op=dict(type='drill',face='top',u=200,v=150,diameter=5,depth=8);op.update(changes);add('features','validate_drill',rectangle,op)
    for u,v in ((200,150),(5,5),(20,150),(200,4)):
        add('features','validate_drill',irregular,dict(type='drill',face='top',u=u,v=v,diameter=10,depth=8))
    for kind in ('pocket','cutout','groove'):
        for shape in ('rect','circle','slot'):
            for face in ('top','bottom'):
                add('features','validate_cut',rectangle,dict(type=kind,shape=shape,face=face,u=200,v=150,length=60,width=20,depth=5,angle=35))
    for changes in (dict(type='bad'),dict(shape=None),dict(face='u0'),dict(u=-1),dict(width=0),dict(depth=18),dict(depth=19),dict(shape='slot',length=10,width=20),dict(u=5),dict(angle=361)):
        cut=dict(type='pocket',shape='rect',face='top',u=200,v=150,length=40,width=20,depth=5,angle=0);cut.update(changes);add('features','validate_cut',rectangle,cut)
    add('features','validate_cut',irregular,dict(type='groove',shape='rect',face='top',u=30,v=40,length=30,width=30,depth=5))
    for kwargs in (dict(nu=1.9,nv=2.2),dict(nu=100,nv=6),dict(nu=101),dict(du=-32,nu=2),dict(nv=3,dv=32)):
        add('features','hole_array',rectangle,100,80,4,8,**kwargs)
    shifting=Part(operations=[dict(type='drill',face=face,u=30,v=10,diameter=4,depth=5) for face in ('top','bottom','u0','u1','v0','v1')],machining=[dict(u=100,v=120)])
    add('features','shift_features',shifting,2,3,1)

    for plane in ('XY','XZ','YZ'):
        p=Part(plane=plane,rx=12,ry=23,rz=34,x=20,y=30,z=40,holes=[[50,50,5,10]])
        add('sketch','normalized_part',p,[(10,20),(110,20),(110,120),(10,120)])
    for outline in ([(-100,-50),(100,-50),(100,50),(-100,50)],[(0,0),(100,100),(0,100),(100,0)],[(0,0),(10,0),(20,0)],[(0,0),(10,0)]):
        add('sketch','normalized_part',Part(t=30),outline)
    add('sketch','normalized_part',Part(edges=[True,False,False,True]),[(10,20),(110,20),(110,120),(10,120)])
    p=Part(u=100,v=100,fillets=[dict(edge='top:0',radius=3)])
    add('sketch','normalized_part',p,[(0,0),(100,0),(80,100),(0,100)])
    add('sketch','normalized_part',p,[(0,100),(100,100),(100,0),(0,0)])
    for action in ('mirror_u','mirror_v','rotate90','any_other_action'):
        add('sketch','transform_points',[(-30,40),(180,80),(200,300)],action)
    add('sketch','transform_points',[],'mirror_u')
    for index,closed,kind,length in [(0,True,'horizontal',None),(1,True,'vertical',None),(2,True,'length',80),(2,False,'length',80),(0,True,'length',None),(0,True,'length',.1),(4,True,'horizontal',None)]:
        add('sketch','constrain_points',[(0,0),(120,80),(80,150)],index,closed,kind,length)
    add('sketch','constrain_points',[(0,0),(0,0)],0,True,'length',50)
    rng=random.Random(15019)
    for _ in range(24):
        u,v=rng.uniform(100,2500),rng.uniform(100,2500);radius=min(u,v)*rng.uniform(0,.45)
        add('shaped_furniture','contour','rounded',u,v,radii=(radius,)*4,segments=rng.choice((4,8,12,16,24)))
    return output

class Native:
    def __init__(self,path):
        self.api=ctypes.CDLL(str(path.resolve()),winmode=0x100|0x800)
        self.api.ms3d_model_call.argtypes=[ctypes.c_char_p];self.api.ms3d_model_call.restype=ctypes.c_void_p
        self.api.ms3d_model_free.argtypes=[ctypes.c_void_p];self.api.ms3d_model_free.restype=None
    def call(self,case):
        request={key:serial(case[key]) for key in ('module','function','args','kwargs')}
        ptr=self.api.ms3d_model_call(json.dumps(request,ensure_ascii=False,allow_nan=False).encode('utf-8'))
        if not ptr:raise RuntimeError('Native model returned a null pointer')
        try:result=json.loads(ctypes.string_at(ptr).decode('utf-8'))
        finally:self.api.ms3d_model_free(ptr)
        if result['ok'] and case['module']=='sketch' and case['function']=='normalized_part':
            try:p=Part(**result['result']);p.validate();result['result']=serial(p)
            except Exception as error:result=dict(ok=False,type=type(error).__name__,error=str(error))
        return result

def compare(actual,expected,path='result'):
    if isinstance(expected,(int,float)) and not isinstance(expected,bool):
        if not isinstance(actual,(int,float)) or isinstance(actual,bool) or not math.isclose(actual,expected,rel_tol=2e-11,abs_tol=2e-8):raise AssertionError(f'{path}: {actual!r} != {expected!r}')
    elif isinstance(expected,list):
        if not isinstance(actual,list) or len(actual)!=len(expected):raise AssertionError(f'{path}: array length/type mismatch')
        for index,(a,e) in enumerate(zip(actual,expected)):compare(a,e,f'{path}[{index}]')
    elif isinstance(expected,dict):
        if not isinstance(actual,dict) or set(actual)!=set(expected):raise AssertionError(f'{path}: object keys mismatch')
        for key in expected:compare(actual[key],expected[key],path+'.'+key)
    elif actual!=expected:raise AssertionError(f'{path}: {actual!r} != {expected!r}')

def normalize_batch(result):
    if not result['ok']:return result
    data=copy.deepcopy(result)
    operations=data['result'];batches={op['batch'] for op in operations}
    if len(batches)!=1 or not re.fullmatch('[0-9a-f]{12}',next(iter(batches))):raise AssertionError('Batch ID is not one shared 12-digit hex value')
    for op in operations:op['batch']='<generated>'
    return data

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--dll',type=Path,default=BASE/'outputs'/'MebelStudio3D_0_15'/'MebelModel.dll')
    parser.add_argument('--report',type=Path,default=BASE/'work'/'parity_shapes_015.json')
    parser.add_argument('--emit-only',action='store_true');opts=parser.parse_args();start=time.monotonic();suite=cases()
    if opts.emit_only:
        opts.report.write_text(json.dumps([dict(case=serial(c),expected=oracle(c)) for c in suite],ensure_ascii=False,indent=2),encoding='utf-8');print('Emitted',len(suite),'oracle cases');return 0
    engine=Native(opts.dll);checks=[]
    for index,case in enumerate(suite):
        wanted=oracle(case)
        try:
            actual=engine.call(case)
            if case['function']=='hole_array':actual,wanted=normalize_batch(actual),normalize_batch(wanted)
            if actual['ok']!=wanted['ok']:raise AssertionError(f'Outcome mismatch: {actual} versus {wanted}')
            if wanted['ok']:compare(actual['result'],wanted['result'])
            elif wanted.get('type')=='ValueError':
                if actual.get('type')!='ValueError' or actual.get('error')!=wanted['error']:raise AssertionError(f'Validation mismatch: {actual} versus {wanted}')
            checks.append(dict(id=case['id'],module=case['module'],function=case['function'],ok=True,validation_error=not wanted['ok']))
        except Exception as error:checks.append(dict(id=case['id'],module=case['module'],function=case['function'],ok=False,error=str(error),expected=wanted))
        if (index+1)%50==0:print(f'{index+1}/{len(suite)} checks; failures {sum(not c["ok"] for c in checks)}',flush=True)
    report=dict(ok=all(c['ok'] for c in checks),checks=len(checks),passed=sum(c['ok'] for c in checks),failures=[c for c in checks if not c['ok']],
        oracle=str(ORACLE),dll=str(opts.dll.resolve()),dll_sha256=hashlib.sha256(opts.dll.read_bytes()).hexdigest(),elapsed_seconds=round(time.monotonic()-start,3),
        absolute_tolerance=2e-8,relative_tolerance=2e-11,full_cad_validation_retained=True,records=checks)
    opts.report.parent.mkdir(parents=True,exist_ok=True);opts.report.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({key:value for key,value in report.items() if key!='records'},ensure_ascii=False,indent=2),flush=True)
    return 0 if report['ok'] else 1

if __name__=='__main__':raise SystemExit(main())
