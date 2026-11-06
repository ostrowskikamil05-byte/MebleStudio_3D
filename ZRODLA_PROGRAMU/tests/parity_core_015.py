r"""Real cross-language parity: original 0.14 core.py versus 0.15 C++ DLL.

Run with developer Python (not the migrated modules), e.g.
  python -B tests/parity_core_015.py --dll C:\paczka\MebelModel.dll
No GUI, graphics context, frozen old source edits or mocked native API.
"""
from pathlib import Path
import argparse, copy, ctypes, dataclasses, hashlib, json, math, random, sys, time

SOURCE = Path(__file__).resolve().parents[1]
BASE = next(p for p in SOURCE.parents if (p/'outputs/MebelStudio3D_0_14').is_dir())
ORACLE = BASE/'outputs/MebelStudio3D_0_14'
sys.path.insert(0,str(ORACLE))
import core
import shaped_furniture
assert Path(core.__file__).resolve().parent == ORACLE.resolve()

def serialize(value):
    if isinstance(value,core.Project):return value.data()
    if isinstance(value,core.Camera):return {k:getattr(value,k) for k in ('yaw','pitch','scale','center','pan')}
    if dataclasses.is_dataclass(value):return dataclasses.asdict(value)
    if isinstance(value,dict):return {k:serialize(v) for k,v in value.items()}
    if isinstance(value,(tuple,list)):return [serialize(v) for v in value]
    return value

def compact_costs(project):
    data=project.data()
    result={k:data[k] for k in ('materials','hardware','scene','edge_price','labor','markup')}
    result['parts']=[{k:p[k] for k in ('material','u','v','t','contour','profile','contour_edge','edges')} for p in data['parts']]
    return result

def compact_cut(project):
    data=project.data()
    result={k:data[k] for k in ('materials','kerf','margin','edge_t')}
    result['parts']=[{k:p[k] for k in ('id','material','u','v','t','contour','grain','edges')} for p in data['parts']]
    return result

class Native:
    def __init__(self,path):
        self.path=Path(path).resolve()
        self.dll=ctypes.CDLL(str(self.path))
        self.dll.ms3d_model_call.argtypes=[ctypes.c_char_p]
        self.dll.ms3d_model_call.restype=ctypes.c_void_p
        self.dll.ms3d_model_free.argtypes=[ctypes.c_void_p]
        self.dll.ms3d_model_free.restype=None
    def raw(self,request):
        payload=json.dumps(serialize(request),ensure_ascii=False,allow_nan=False,separators=(',',':')).encode('utf-8')
        pointer=self.dll.ms3d_model_call(payload)
        assert pointer,'Native response allocation failed'
        try:return json.loads(ctypes.string_at(pointer).decode('utf-8'))
        finally:self.dll.ms3d_model_free(pointer)
    def call(self,function,args,kwargs=None):
        result=self.raw(dict(module='core',function=function,args=args,kwargs=kwargs or {}))
        if not result['ok']:
            cls={'ValueError':ValueError,'TypeError':TypeError,'OSError':OSError}.get(result.get('type','ValueError'),RuntimeError)
            raise cls(result['error'])
        return result['result']

def equal(actual,expected,path='result'):
    if isinstance(expected,bool):
        assert isinstance(actual,bool) and actual==expected,(path,actual,expected)
    elif isinstance(expected,(int,float)):
        assert isinstance(actual,(int,float)) and not isinstance(actual,bool),(path,actual,expected)
        assert math.isclose(actual,expected,rel_tol=1e-12,abs_tol=1e-8),(path,actual,expected,actual-expected)
    elif isinstance(expected,dict):
        assert isinstance(actual,dict) and actual.keys()==expected.keys(),(path,actual,expected)
        for key in expected:equal(actual[key],expected[key],path+'.'+str(key))
    elif isinstance(expected,(list,tuple)):
        assert isinstance(actual,list) and len(actual)==len(expected),(path,actual,expected)
        for index,(a,b) in enumerate(zip(actual,expected)):equal(a,b,path+f'[{index}]')
    else:assert actual==expected,(path,actual,expected)

def run(native,report_path):
    started=time.monotonic();checks=[];functions={};failures=[]
    source_before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in ORACLE.glob('*.py')}
    def check(function,args,oracle,kwargs=None,exact_error=False):
        expected_error=None
        try:expected=oracle()
        except Exception as error:expected_error=error
        try:actual=native.call(function,args,kwargs)
        except Exception as error:
            assert expected_error is not None,(function,args,error)
            assert type(error) is type(expected_error),(function,type(error),type(expected_error),str(error),str(expected_error))
            if exact_error:assert str(error)==str(expected_error),(function,str(error),str(expected_error))
        else:
            assert expected_error is None,(function,args,expected_error,actual)
            equal(actual,expected)
        checks.append(function);functions[function]=functions.get(function,0)+1

    try:
        for x in (-100000,-0.,0,100000,True,False,' 12.75 ','1_000.5','1e-400','-1e-400','\u00a012.5\u00a0','\u200312.5\u202f','١٢.٥','١_٢.٥','１２.５','𝟭𝟮.𝟯'):
            check('finite',[x],lambda x=x:core.finite(x))
        for x in (-100000.01,100000.01,'nan','+inf','-Infinity','1e999','abc','0x10','1__0'):
            check('finite',[x],lambda x=x:core.finite(x),exact_error=not x in ('abc','0x10','1__0'))
        for x,lo,hi in ((0,0,0),(.5,0,1),(-2,-1,4),(2,5,1),(10000.1,.1,10000)):
            check('finite',[x],lambda x=x,lo=lo,hi=hi:core.finite(x,lo,hi),kwargs=dict(lo=lo,hi=hi),exact_error=True)
        invalid=[[],[(0,0),(10,0)],[(0,0),(1,0),(0,1)],[(0,0),(10,10),(0,10),(10,0)],
                 [(0,0),(10001,0),(0,10)],[(0,0),(10,0),(10,.005),(0,10)],
                 [(0,0),(100,0),(100,100),(50,0),(0,100)],[(0,0),(0,100),(100,100),(100,0),(0,0)],
                 [(0,0),(100,0),(100,100),(-1,100)],[(0,0),(100,0),(100,100),(0,100)]*33]
        for polygon in invalid:check('validate_polygon',[polygon],lambda p=polygon:core.validate_polygon(p),exact_error=True)
        for polygon in ([],[(3,4)],[(0,0),(10,0),(10,10),(0,10)],[(0,0),(0,10),(10,10),(10,0)],
                        [('0','0'),('100','0'),('100','100'),('0','100')]):
            if not polygon or not isinstance(polygon[0][0],str):check('polygon_area',[polygon],lambda p=polygon:core.polygon_area(p))
            if len(polygon)>=3:check('validate_polygon',[polygon],lambda p=polygon:core.validate_polygon(p))
        rng=random.Random(150015)
        for index in range(60):
            count=rng.randint(3,45);radius=rng.uniform(10,3000)
            polygon=[(4000+radius*math.cos(i*math.tau/count),4000+radius*math.sin(i*math.tau/count)) for i in range(count)]
            check('polygon_area',[polygon],lambda p=polygon:core.polygon_area(p))
            check('validate_polygon',[polygon],lambda p=polygon:core.validate_polygon(p))
            for point in [(4000,4000),(0,0),polygon[0],tuple((a+b)/2 for a,b in zip(polygon[0],polygon[1])),(4000+radius+1e-6,4000)]:
                check('inside',[point,polygon],lambda q=point,p=polygon:core.inside(q,p))
        for index in range(180):
            point=tuple(rng.uniform(-10000,10000) for _ in range(3));angles=tuple(rng.uniform(-100000,100000) for _ in range(3))
            check('rotate',[point,*angles],lambda p=point,a=angles:core.rotate(p,*a))
            part=core.Part(plane=rng.choice(('XY','XZ','YZ')),x=rng.uniform(-3000,3000),y=rng.uniform(-3000,3000),z=rng.uniform(-3000,3000),rx=angles[0],ry=angles[1],rz=angles[2],
                           layout={} if index%7==0 else dict(x=rng.uniform(-9000,9000),y=rng.uniform(-9000,9000),z=rng.uniform(-1000,1000),yaw=rng.uniform(-720,720)))
            check('local_world',[part,*point],lambda p=part,q=point:core.local_world(p,*q))
            check('world',[part,*point],lambda p=part,q=point:core.world(p,*q))
            check('layout_point',[part,point],lambda p=part,q=point:core.layout_point(p,q))
            check('inverse_layout_point',[part,point],lambda p=part,q=point:core.inverse_layout_point(p,q))
            camera=core.Camera(yaw=rng.uniform(-720,720),pitch=rng.uniform(-180,180),scale=rng.uniform(.0001,10),center=point,pan=(rng.uniform(-200,200),rng.uniform(-200,200)))
            target=tuple(rng.uniform(-10000,10000) for _ in range(3))
            check('Camera._basis',[camera],lambda c=camera:c._basis())
            check('Camera.view',[camera,target],lambda c=camera,q=target:c.view(q))
            check('Camera.project',[camera,target,1366,768],lambda c=camera,q=target:c.project(q,1366,768))
            check('explosion_shift',[part,camera,.35],lambda p=part,c=camera:core.explosion_shift(p,c,.35))
        for color in ('#000000','#ffffff','#010305','#1f2f3f','#ABCDEF','#bd9565','#١f٢f٣f','#𝟭f𝟮f𝟯f'):
            for factor in (-1,0,.5,1,1.5,2,10000):check('shade',[color,factor],lambda c=color,f=factor:core.shade(c,f))
        for index in range(150):
            color='#'+''.join(f'{rng.randrange(256):02x}' for _ in range(3));factor=rng.uniform(-2,6)
            check('shade',[color,factor],lambda c=color,f=factor:core.shade(c,f))
        for color in ('#z00000','#12','#12-4xx'):check('shade',[color,1],lambda c=color:core.shade(c,1))

        profiles=[({},[])]
        for kind in shaped_furniture.SHAPES.values():
            u,v=(640,640) if kind=='circle' else (840,640)
            profile=shaped_furniture.shape_definition(kind,u,v,radii=[12,23,34,45],arch_rise=220,taper=75,segments=8)
            contour=shaped_furniture.contour(kind,u,v,radii=[12,23,34,45],arch_rise=220,taper=75,segments=8)
            profiles.append((profile,contour))
        metric_parts=[]
        for index,(profile,contour) in enumerate(profiles):
            u,v=(640,640) if profile.get('kind')=='circle' else (840,640)
            part=core.Part(u=u,v=v,t=28,edges=[False]*4 if contour else [True,False,True,True],profile=profile,contour=contour,contour_edge=True)
            metric_parts.append(part)
            for function,oracle in [('Part.points',part.points),('Part.area',part.area),('Part.edge_m',part.edge_m)]:check(function,[part],oracle)
            for edge_t in (0,.5,1,2.2):check('Part.blank',[part,edge_t],lambda p=part,t=edge_t:p.blank(t))
            for material in (core.Material(price_m2=123.45),core.Material(kind='drewno',price_m3=5678.9)):
                check('Material.cost',[material,part],lambda m=material,p=part:m.cost(p))
            check('part_metrics',[part,1.5],lambda p=part:dict(points=p.points(),area_m2=p.area(),edge_m=p.edge_m(),blank=p.blank(1.5),thickness_mm=p.t,world_origin=core.world(p,0,0,0)))

        for seed in range(40):
            local=random.Random(seed);project=core.Project(kerf=local.choice((0,.1,3.2,5)),margin=local.choice((0,10,25)),edge_t=local.choice((0,.5,1,2)),edge_price=local.uniform(0,8),labor=local.uniform(0,2000),markup=local.uniform(0,90))
            for i in range(local.randrange(10,80)):
                project.parts.append(core.Part(id=f'{seed}-{i}',u=local.randint(30,3000),v=local.randint(30,2300),t=local.choice((3,18,22)),grain=local.choice((True,False)),
                                  edges=[local.choice((True,False)) for _ in range(4)],material=project.materials[local.randrange(3)].id))
            project.hardware=[dict(name='Okucie',qty=local.randrange(10),price=local.uniform(0,50)) for _ in range(8)]
            project.scene['objects']=[dict(price=local.uniform(0,2000)) for _ in range(4)]
            # Costs does not validate the room schema in the original implementation.
            check('Project.costs',[project],project.costs)
            check('Project.costs',[compact_costs(project)],project.costs)
            project.scene['objects']=[];project.validate()
            check('cut_plan',[project],lambda p=project:core.cut_plan(p))
            check('cut_plan',[compact_cut(project)],lambda p=project:core.cut_plan(p))
        for grain,u,v,margin,kerf in ((True,1000,500,0,3.2),(True,500,1000,0,3.2),(False,500,1000,0,3.2),(False,1000,500,10,3.2),(False,1000,500,0,0)):
            project=core.Project(margin=margin,kerf=kerf);project.materials[0].sheet_u=1000;project.materials[0].sheet_v=500
            project.parts=[core.Part(id='exact-stock',u=u,v=v,grain=grain,material=project.materials[0].id)];project.validate()
            check('cut_plan',[project],lambda p=project:core.cut_plan(p))
        project=core.Project(margin=0,kerf=2.3)
        project.materials[0].sheet_u=500;project.materials[0].sheet_v=500
        project.parts=[core.Part(id=f'tie-{i}',u=100 if i%2 else 200,v=200 if i%2 else 100,t=18 if i%3 else 22,grain=bool(i%2),material=project.materials[i%2].id) for i in range(30)]
        project.validate();check('cut_plan',[project],lambda:core.cut_plan(project))
        profile_project=core.Project()
        for index,part in enumerate(metric_parts):
            part=copy.deepcopy(part);part.material=profile_project.materials[index%len(profile_project.materials)].id;profile_project.parts.append(part)
        profile_project.validate();check('Project.costs',[profile_project],profile_project.costs);check('cut_plan',[profile_project],lambda:core.cut_plan(profile_project))
        check('Project.costs',[compact_costs(profile_project)],profile_project.costs)
        check('cut_plan',[compact_cut(profile_project)],lambda:core.cut_plan(profile_project))
        benchmark_project=core.Project()
        local=random.Random(15)
        benchmark_project.parts=[core.Part(id=f'benchmark-{i}',u=local.randint(100,850),v=local.randint(80,700),
                                          material=benchmark_project.materials[0].id,grain=bool(i%2)) for i in range(160)]
        benchmark_project.validate()
        check('Project.costs',[compact_costs(benchmark_project)],benchmark_project.costs)
        check('cut_plan',[compact_cut(benchmark_project)],lambda:core.cut_plan(benchmark_project))
        # costs() itself does not validate dimensions; preserve abs polygon
        # area even for temporary invalid values before project validation.
        for u,v in ((-100,200),(100,-200),(-100,-200),(0,200)):
            project=core.Project()
            project.parts=[core.Part(u=u,v=v,material=project.materials[0].id)]
            check('Project.costs',[compact_costs(project)],project.costs)
        for u,v,edge_t in ((-1,20,1),(1,20,2),(10001,20,1)):
            project=core.Project(edge_t=edge_t);project.parts=[core.Part(u=u,v=v,edges=[True]*4,material=project.materials[0].id)]
            check('cut_plan',[project],lambda p=project:core.cut_plan(p),exact_error=True)
        info=native.raw(dict(module='native',function='info',args=[],kwargs={}))
        assert info['ok'] and info['result']['outstanding_buffers']==0,info
        assert source_before=={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in ORACLE.glob('*.py')},'Old oracle source changed'
    except Exception:
        import traceback
        failures.append(traceback.format_exc())
    report=dict(ok=not failures,checks=len(checks),functions=functions,ported_functions=len(functions),failures=failures,
                seconds=round(time.monotonic()-started,3),oracle=str(ORACLE),oracle_version='0.14',dll=str(native.path),
                dll_sha256=hashlib.sha256(native.path.read_bytes()).hexdigest(),source_014_unchanged=True,
                gui=False,absolute_tolerance=1e-8,relative_tolerance=1e-12,exception_type_parity=True,
                scope='Individual core calculations and full guillotine heuristic; complete core/CAD/UI module migration remains separate')
    Path(report_path).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
    return 0 if report['ok'] else 1

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--report',type=Path,default=BASE/'work/parity_core_015.json')
    args=parser.parse_args()
    raise SystemExit(run(Native(args.dll),args.report))
