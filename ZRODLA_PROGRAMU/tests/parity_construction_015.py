"""Cross-language construction regression: original 0.14 vs the 0.15 C++ DLL.

Run with a development Python; does not import the migrated Python modules,
Tk, or any GPU API. Reference code is read-only and bytecode is disabled.
"""
from __future__ import annotations
import argparse
import copy
import ctypes
import hashlib
import importlib
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import time

sys.dont_write_bytecode = True

class Native:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.lib = ctypes.CDLL(str(self.path))
        self.lib.ms3d_model_call.argtypes = [ctypes.c_char_p]
        self.lib.ms3d_model_call.restype = ctypes.c_void_p
        self.lib.ms3d_model_free.argtypes = [ctypes.c_void_p]
        self.lib.ms3d_model_free.restype = None
    def invoke(self, module, function, args, kwargs):
        request = json.dumps(dict(module=module, function=function, args=args, kwargs=kwargs),
                             ensure_ascii=False, allow_nan=False).encode('utf-8')
        pointer = self.lib.ms3d_model_call(request)
        if not pointer:
            raise AssertionError('Native call returned a null response')
        try:
            return json.loads(ctypes.string_at(pointer).decode('utf-8'))
        finally:
            self.lib.ms3d_model_free(pointer)

def serial(value):
    """Normalize only the unavoidable tuple and JSON object-key boundaries."""
    if isinstance(value, dict): return {str(k): serial(v) for k,v in value.items()}
    if isinstance(value, (tuple,list)): return [serial(v) for v in value]
    return value

def wire(value):
    """Retain Python key/tuple/Project types across the isolated bridge worker."""
    if isinstance(value,dict):return {'$pairs':[[wire(k),wire(v)] for k,v in value.items()]}
    if isinstance(value,tuple):return {'$tuple':[wire(v) for v in value]}
    if isinstance(value,list):return [wire(v) for v in value]
    if hasattr(value,'data'):return {'$project':wire(value.data())}
    return value

def unwire(value, core):
    if isinstance(value,dict):
        if '$pairs' in value:return {unwire(k,core):unwire(v,core) for k,v in value['$pairs']}
        if '$tuple' in value:return tuple(unwire(v,core) for v in value['$tuple'])
        if '$project' in value:return core.Project.from_data(unwire(value['$project'],core))
    if isinstance(value,list):return [unwire(v,core) for v in value]
    return value

def type_tree(value):
    if isinstance(value,dict):
        return ['dict', sorted([[type(k).__name__,repr(k),type_tree(v)] for k,v in value.items()],key=repr)]
    if isinstance(value,(tuple,list)):return [type(value).__name__,[type_tree(v) for v in value]]
    # The original helpers accept int arithmetic; JSON floats remain equivalent mm.
    return 'number' if type(value) in (int,float) else type(value).__name__

def bridge_worker(migrated):
    sys.path.insert(0,str(migrated.resolve()))
    modules={name:importlib.import_module(name) for name in ('core','assembly','runners_catalog','furniture_layout','corner_furniture')}
    for line in sys.stdin:
        request=json.loads(line)
        if request.get('quit'):break
        try:
            function=getattr(modules[request['module']],request['function'])
            # A four-value overlap calculation is faster locally than a JSON roundtrip.
            # Full compiled helpers remain parity-tested above, while the actual magnetic
            # placement loop retains these Python calls until the entire loop is batched.
            retained=request['module']=='furniture_layout' and request['function'] in ('_overlap','_intersects','_translated','_flat_faces')
            if retained and getattr(function,'_native_cpp',False):raise AssertionError('Hot helper must stay local until layout is batched')
            if not retained and not getattr(function,'_native_cpp',False):raise AssertionError('Function is not routed to C++')
            args=unwire(request['args'],modules['core']);kwargs=unwire(request['kwargs'],modules['core'])
            result=function(*args,**kwargs)
            response=dict(ok=True,result=serial(result),types=type_tree(result))
        except Exception as exc:response=dict(ok=False,error=str(exc),type=type(exc).__name__)
        print(json.dumps(response,ensure_ascii=False),flush=True)

def bridge_cases(test,migrated):
    """Check the installed wrappers, since raw C ABI cannot encode Python key types."""
    worker=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--bridge-worker','--migrated',str(migrated)],
                            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                            encoding='utf-8')
    def check(module,function,*args,**kwargs):
        try:
            result=getattr(test.reference[module],function)(*copy.deepcopy(args),**copy.deepcopy(kwargs))
            expected=dict(ok=True,result=serial(result),types=type_tree(result))
        except Exception as exc:expected=dict(ok=False,error=str(exc),type=type(exc).__name__)
        request=dict(module=module,function=function,args=wire(args),kwargs=wire(kwargs))
        worker.stdin.write(json.dumps(request,ensure_ascii=False)+'\n');worker.stdin.flush()
        line=worker.stdout.readline()
        if not line:raise RuntimeError('Bridge worker ended: '+worker.stderr.read())
        actual=json.loads(line)
        if expected['ok']:
            mismatch=None if actual.get('ok') else f'bridge error: {actual}'
            if mismatch is None:mismatch=compare(expected['result'],actual['result'])
            if mismatch is None:mismatch=compare(expected['types'],actual['types'],'types')
        else:mismatch=None if actual.get('error')==expected['error'] else f'bridge error differs: {expected!r} / {actual!r}'
        test.checks.append(dict(name=f'bridge/{module}.{function}/{len(test.checks)}',passed=mismatch is None,details=mismatch))
    try:
        for profile_id,profile in test.reference['runners_catalog'].PROFILES.items():
            check('runners_catalog','get_profile',profile_id)
            check('runners_catalog','get_profile',profile)
            check('runners_catalog','calculate_runner',profile,564,profile['lengths'][0],16,900)
        base=test.reference['runners_catalog'].get_profile(next(iter(test.reference['runners_catalog'].PROFILES)))
        for mapping in ({500:530},{'500':530},{500.0:530},{500:530,'500':700},
                        {True:3},{1:3,'1':10},{500.5:530}):
            profile=copy.deepcopy(base);profile.update(lengths=[500] if 500 in mapping or '500' in mapping else [500.5] if 500.5 in mapping else [1],
                depth_extra=None,depth_by_length=mapping,article_by_length={k:f'article {k}' for k in mapping},
                source_by_length={k:f'source {k}' for k in mapping},load_by_length={k:45 for k in mapping},
                travel_by_length={k:450 for k in mapping})
            check('runners_catalog','get_profile',profile)
            check('runners_catalog','calculate_runner',profile,564,profile['lengths'][0],16,600)
            check('runners_catalog','available_lengths',profile,600)
            check('runners_catalog','calculate_runner',profile=profile,cabinet_inside_width=564,nominal_length=profile['lengths'][0],side_thickness=16,cabinet_depth=600)
        check('furniture_layout','_translated',((0,0,0),(100,100,100)),(25,50,-12))
        check('furniture_layout','_flat_faces',[(0,0),(900,0),(900,560),(560,560),(560,900),(0,900)])
        check('corner_furniture','corner_outline',900,900,560,'rounded',100,'right',12)
        project=test.reference['core'].Project()
        check('corner_furniture','normalize_corner_config',{},project)
        check('corner_furniture','normalize_corner_config',{'material':project.materials[2].id},project=project)
        check('assembly','recipe',{'gap':'2.5','hinge_count':4,'auto_hardware':False,'extra':{'names':['A']}})
        check('assembly','recipe',{'gap':-1,'hinge_count':1})
    finally:
        if worker.poll() is None:
            worker.stdin.write('{"quit":true}\n');worker.stdin.flush();worker.stdin.close()
        worker.wait(timeout=30)
        error=worker.stderr.read()
        if worker.returncode:raise RuntimeError(f'Bridge worker exit {worker.returncode}: {error}')

def fallback_worker(migrated, frozen):
    sys.path.insert(0,str(migrated.resolve()))
    import native_model
    native_model.available=lambda:False
    def forbidden_library():raise RuntimeError('TEST missing native library')
    native_model.library=forbidden_library
    if frozen:
        sys.frozen=True
        try:importlib.import_module('runners_catalog')
        except RuntimeError as exc:
            print(json.dumps(dict(passed=str(exc)=='TEST missing native library',details=str(exc))));return
        print(json.dumps(dict(passed=False,details='Packaged runtime silently fell back to Python')));return
    modules={name:importlib.import_module(name) for name in ('runners_catalog','assembly','corner_furniture')}
    routed=any(getattr(getattr(modules[module],function),'_native_cpp',False)
               for module,function in (('runners_catalog','get_profile'),('assembly','recipe'),('corner_furniture','corner_outline')))
    values=modules['runners_catalog'].calculate_runner('blum_movento_760h',564,500,16,600)
    outline=modules['corner_furniture'].corner_outline(900,900,560)
    recipe=modules['assembly'].recipe({'hinge_count':4})
    ok=not routed and values['min_cabinet_depth']==503 and len(outline)==6 and recipe['hinge_count']==4
    print(json.dumps(dict(passed=ok,details=None if ok else 'Source-only fallback did not preserve original functions')))

def fallback_cases(test,migrated):
    for frozen in (False,True):
        command=[sys.executable,str(Path(__file__).resolve()),'--fallback-worker','--migrated',str(migrated)]
        if frozen:command.append('--frozen-test')
        response=subprocess.run(command,encoding='utf-8',capture_output=True,timeout=30)
        if response.returncode:raise RuntimeError('Fallback worker error: '+response.stderr)
        value=json.loads(response.stdout)
        test.checks.append(dict(name='bridge/packaged_requires_DLL' if frozen else 'bridge/source_without_DLL',**value))

def compare(expected, actual, path='result'):
    if isinstance(expected, bool) or expected is None or isinstance(expected,str):
        if expected != actual or type(expected) is not type(actual):
            return f'{path}: {expected!r} != {actual!r}'
    elif isinstance(expected,(int,float)):
        if isinstance(actual,bool) or not isinstance(actual,(int,float)):
            return f'{path}: numeric type differs: {actual!r}'
        if not math.isclose(expected, actual, rel_tol=1e-12, abs_tol=1e-8):
            return f'{path}: {expected!r} != {actual!r}'
    elif isinstance(expected,dict):
        if not isinstance(actual,dict) or expected.keys()!=actual.keys():
            return f'{path}: object fields differ'
        for key,value in expected.items():
            mismatch=compare(value, actual[key], f'{path}.{key}')
            if mismatch:return mismatch
    elif isinstance(expected,list):
        if not isinstance(actual,list) or len(expected)!=len(actual):
            return f'{path}: list length differs: {len(expected)} / {len(actual) if isinstance(actual,list) else actual!r}'
        for i,(a,b) in enumerate(zip(expected,actual)):
            mismatch=compare(a,b,f'{path}[{i}]')
            if mismatch:return mismatch
    else:
        raise AssertionError(f'Unsupported reference result type: {type(expected)}')
    return None

class Parity:
    def __init__(self, reference, native):
        self.reference=reference
        self.native=native
        self.checks=[]
    def check(self, module,function,*args,label=None,native_args=None,native_kwargs=None,**kwargs):
        label=label or f'{module}.{function}'
        fn=getattr(self.reference[module],function)
        try:
            expected={'ok':True,'result':serial(fn(*copy.deepcopy(args),**copy.deepcopy(kwargs)))}
        except Exception as exc:
            expected={'ok':False,'error':str(exc),'type':type(exc).__name__}
        payload_args=args if native_args is None else native_args
        payload_kwargs=kwargs if native_kwargs is None else native_kwargs
        actual=self.native.invoke(module,function,serial(payload_args),serial(payload_kwargs))
        if expected['ok']:
            mismatch=None if actual.get('ok') else f"native error: {actual.get('error')}"
            if mismatch is None:mismatch=compare(expected['result'],actual.get('result'))
        else:
            mismatch=None if not actual.get('ok') and actual.get('error')==expected['error'] else f"error differs: {expected.get('error')!r} / {actual!r}"
        self.checks.append(dict(name=label,passed=mismatch is None,details=mismatch))
        return actual

def cases(test, modules):
    runners=modules['runners_catalog']
    for profile_id,profile in runners.PROFILES.items():
        test.check('runners_catalog','get_profile',profile_id,label=f'profile/{profile_id}/id')
        test.check('runners_catalog','get_profile',profile,label=f'profile/{profile_id}/dict')
        resolved=runners.get_profile(profile_id)
        test.check('runners_catalog','get_profile',resolved,label=f'profile/{profile_id}/resolved')
        for depth in (200,400,500,503,600,750,1000):
            test.check('runners_catalog','available_lengths',profile_id,depth,label=f'available/{profile_id}/{depth}')
        for length in profile['lengths']:
            for width,side,depth in ((564,16,800),(40,18,length-15),(1000,12,length),(564,16,length+3)):
                test.check('runners_catalog','calculate_runner',profile_id,width,length,side,depth,
                           label=f'runner/{profile_id}/{length}/{width}/{side}/{depth}')
            test.check('runners_catalog','calculate_runner',profile_id,564,length,16,900,
                       construction='four_sided',drawer_front_thickness=18,
                       label=f'runner/{profile_id}/{length}/four_sided')
        # Both dictionary and id paths must preserve all manufacturer maps.
        test.check('runners_catalog','calculate_runner',resolved,'564',str(profile['lengths'][0]),'16','900',
                   label=f'runner/{profile_id}/resolved_strings')
    test.check('runners_catalog','get_profile','not_a_product')
    for invalid in (None,[],42,True):test.check('runners_catalog','get_profile',invalid)
    profile_id=next(iter(runners.PROFILES))
    for index in (1,2,3,4):
        for value in (True,None,'bad',0,-1,10001,'nan','inf'):
            args=[profile_id,564,500,16,600];args[index]=value
            test.check('runners_catalog','calculate_runner',*args,label=f'runner/invalid/{index}/{value!r}')
    test.check('runners_catalog','calculate_runner',profile_id,564,501,16,600)
    test.check('runners_catalog','calculate_runner',profile_id,564,500,16,600,construction='unknown')
    for profile_id,profile in runners.PROFILES.items():
        if profile.get('four_sided_add_front_thickness'):
            test.check('runners_catalog','calculate_runner',profile_id,564,profile['lengths'][0],16,600,construction='four_sided')
        if profile.get('depth_extra') is None and not profile.get('depth_by_length'):
            length=profile['lengths'][0]
            for confirmation in (length-1,length,length+30,'600',0,None):
                test.check('runners_catalog','calculate_runner',profile_id,564,length,16,650,confirmed_min_depth=confirmation)
    custom=runners.get_profile(next(iter(runners.PROFILES)))
    custom.update(id='workshop',notes=['user recipe'],inner_width_offset=39,
                  inner_width_tolerance=[-2,1],min_drawer_inner_width=100)
    test.check('runners_catalog','get_profile',custom)
    test.check('runners_catalog','calculate_runner',custom,120,500,22,498)

    test.check('assembly','recipe')
    for value in (None,{},False,[],dict(hinge_side='right',hinge_count=6,auto_hardware=False,
                                     bay='left',extra=dict(a=[1,2]),hinge_price='250.5',gap='2.5')):
        test.check('assembly','recipe',value)
    for key in modules['assembly'].DEFAULTS:
        if key in ('hinge_side','auto_hardware'):continue
        high=1e6 if key.endswith('_price') else 10000
        test.check('assembly','recipe',{key:high},label=f'assembly/{key}/limit')
        test.check('assembly','recipe',{key:high+1},label=f'assembly/{key}/over')
        test.check('assembly','recipe',{key:-1},label=f'assembly/{key}/negative')
    for value in ('x',None,1):test.check('assembly','recipe',{'hinge_side':value})
    for count in (0,1,2,8,9,2.5,'3',True):test.check('assembly','recipe',{'hinge_count':count})
    for cfg in ({'gap':-1,'cup_depth':-1}, {'hinge_count':-1,'cup_diameter':-1},
                {'drawer_depth':-1,'shelf_end':-1}, {'gap':None}, {'gap':'bad'},
                {'gap':'0x10'}, {'gap':'nan(payload)'}, {'gap':'  2_000.5  '}):
        test.check('assembly','recipe',cfg,label=f'assembly/validation_priority/{cfg!r}')

    rng=random.Random(415)
    for i in range(100):
        a=[rng.uniform(-1000,1000) for _ in range(3)]
        b=[x+rng.uniform(0,1000) for x in a]
        c=[rng.uniform(-1000,1000) for _ in range(3)]
        d=[x+rng.uniform(0,1000) for x in c]
        for axis in range(3):test.check('furniture_layout','_overlap',[a,b],[c,d],axis,label=f'layout/overlap/{i}/{axis}')
        test.check('furniture_layout','_intersects',[a,b],[c,d],label=f'layout/intersects/{i}')
        test.check('furniture_layout','_translated',[a,b],c,label=f'layout/translation/{i}')
    a=((0,0,0),(100,100,100))
    for separation in (-.1,0,.005,.01,.010000001,1):
        b=((100-separation,0,0),(200,100,100))
        test.check('furniture_layout','_intersects',a,b)
        test.check('furniture_layout','_intersects',a,b,tolerance=0)
    for poly in ([],[(0,0)],[(0,0),(100,0),(100,100),(0,100)],
                 [(0,0),(0,100),(100,100),(100,0)],
                 [(0,0),(900,0),(900,560),(560,560),(560,900),(0,900)],
                 [(0,0),(100,50),(0,100)],[(0,0),(100,0.0000001),(100,100),(0,100)]):
        test.check('furniture_layout','_flat_faces',poly)
    test.check('furniture_layout','_overlap',a,a,-1)

    corner=modules['corner_furniture']
    for shape in ('L','chamfer','rounded'):
        for side in ('left','right'):
            for segments in (4,12,24):
                for angle in (15,45,75):
                    test.check('corner_furniture','corner_outline',1200,1200,560,shape,100,side,segments,
                               arm_depth_y=620,diagonal_angle=angle,
                               label=f'outline/{shape}/{side}/{segments}/{angle}')
    for i in range(100):
        w,d=rng.uniform(700,1900),rng.uniform(700,1900)
        a,b=rng.uniform(250,550),rng.uniform(250,550)
        r=rng.uniform(20,90)
        shape=rng.choice(('L','rounded','chamfer'));side=rng.choice(('left','right'))
        test.check('corner_furniture','corner_outline',w,d,a,shape,r,side,rng.randint(4,24),b,rng.uniform(15,60),label=f'outline/random/{i}')
    for args in ((900,900,560,'bad'),(900,900,560,'bad',0),
                 (900,900,560,'L',None),(900,900,560,'bad',None),
                 (900,900,560,'rounded',100,'left',123),
                 (900,900,560,'rounded',100,'left',124),
                 (900,900,560,'chamfer',400),(900,900,900),
                 (10001,900,560),(900,900,-20),(0,0,0)):
        test.check('corner_furniture','corner_outline',*args)
    for furniture in ('cabinet','wardrobe'):
        for shape in ('L','chamfer','rounded'):
            for side in ('left','right'):
                for style in ('smooth','frame','fluted','finger','plain','slab','milled'):
                    cfg=dict(furniture_type=furniture,shape=shape,side=side,front_style=style,
                             name='  Mój narożnik  ',extra={'preserve':[1,2]},arm_depth=500)
                    test.check('corner_furniture','normalize_corner_config',cfg,
                               label=f'normalize/{furniture}/{shape}/{side}/{style}')
    test.check('corner_furniture','normalize_corner_config',{})
    for name in ('\u00a0Narożnik\u3000', '\u2003', '\u0085  Szafa\u202f', '\t🪑 Szafka\n'):
        test.check('corner_furniture','normalize_corner_config',{'name':name},label=f'normalize/unicode_name/{name!r}')
    project=modules['core'].Project()
    cfg={'material':project.materials[0].id,'front_material':project.materials[2].id,
         'origin':(-400,250,0),'fronts':'orthogonal','hinge_pattern':'hinge_sensys52'}
    test.check('corner_furniture','normalize_corner_config',cfg,project,
               native_args=(cfg,project.data()),label='normalize/project/materials')
    test.check('corner_furniture','normalize_corner_config',{},project,
               native_args=({},project.data()),label='normalize/project/defaults')
    test.check('corner_furniture','normalize_corner_config',{'material':'missing'},project,
               native_args=({'material':'missing'},project.data()),label='normalize/project/invalidmaterial')
    for value in (None,[],True):test.check('corner_furniture','normalize_corner_config',value)
    invalids=dict(name=['',1,'   '],width=[149,10001,None,'bad'],depth=[149],height=[99],arm_depth=[79,3001,870],
        arm_depth_y=[870],thickness=[5,61],radius=[-1,1501],rail_width=[19,301],
        diagonal_angle=[14,76],front_thickness=[13,41],gap=[0,21],opening_angle=[171],handle_spacing=[1001],
        leg_height=[501],leg_diameter=[14],leg_setback=[9],leg_price=[100001],leg_count=[5,'4'],
        shelves=[-1,9,2.5],segments=[3,25,5.5],hinge_count=[1,9,3.5],
        shape=['bad'],side=['bad'],top_mode=['bad'],furniture_type=['bad'],fronts=['bad'],
        handle_mode=['bad'],hinge_pattern=['missing','confirmat7','hinge_sensys_tb55'],origin=[[],[0,0],[100001,0,0]],
        front_style=['bad'],front_border=[4,251],front_depth=[0,31],front_radius=[81],flute_width=[1],
        flute_spacing=[2],flute_count=[33,2.5],finger_length=[19],finger_width=[5],finger_depth=[0])
    for key,values in invalids.items():
        for value in values:test.check('corner_furniture','normalize_corner_config',{key:value},label=f'normalize/invalid/{key}/{value!r}')
    for cfg in (dict(fronts='orthogonal',shape='rounded'),dict(fronts='diagonal',shape='L'),
                dict(shape='chamfer',radius=200,diagonal_angle=75),
                dict(shape='rounded',radius=340),dict(top_mode='rails',arm_depth=100,rail_width=82),
                dict(height=100,leg_height=20),dict(arm_depth=80,thickness=40)):
        test.check('corner_furniture','normalize_corner_config',cfg)

def main():
    parser=argparse.ArgumentParser()
    current=Path(__file__).resolve().parents[1]
    parser.add_argument('--reference',type=Path,default=current.parent/'MebelStudio3D_0_14')
    parser.add_argument('--dll',type=Path,default=current/'MebelModel.dll')
    parser.add_argument('--report',type=Path,default=current/'PARITY_CONSTRUCTION_015.json')
    parser.add_argument('--migrated',type=Path,default=current)
    parser.add_argument('--bridge-worker',action='store_true')
    parser.add_argument('--skip-bridge',action='store_true')
    parser.add_argument('--fallback-worker',action='store_true')
    parser.add_argument('--frozen-test',action='store_true')
    options=parser.parse_args()
    if options.bridge_worker:bridge_worker(options.migrated);return 0
    if options.fallback_worker:fallback_worker(options.migrated,options.frozen_test);return 0
    reference=options.reference.resolve()
    sys.path.insert(0,str(reference))
    modules={name:importlib.import_module(name) for name in ('core','assembly','runners_catalog','furniture_layout','corner_furniture')}
    for module in modules.values():
        if Path(module.__file__).resolve().parent!=reference:raise RuntimeError('Reference Python module was imported from the wrong version')
    before={name:hashlib.sha256((reference/f'{name}.py').read_bytes()).hexdigest() for name in modules}
    start=time.perf_counter();native=Native(options.dll);test=Parity(modules,native)
    cases(test,modules)
    if not options.skip_bridge:
        bridge_cases(test,options.migrated)
        fallback_cases(test,options.migrated)
    stats=native.invoke('native','info',[],{})
    buffer_ok=stats.get('ok') and stats['result']['outstanding_buffers']==0
    test.checks.append(dict(name='ABI/buffer_ownership',passed=bool(buffer_ok),details=None if buffer_ok else repr(stats)))
    after={name:hashlib.sha256((reference/f'{name}.py').read_bytes()).hexdigest() for name in modules}
    test.checks.append(dict(name='reference_0_14_immutable',passed=before==after,details=None if before==after else 'Reference hashes changed'))
    failed=[check for check in test.checks if not check['passed']]
    report=dict(reference=str(reference),library=str(native.path),library_sha256=hashlib.sha256(native.path.read_bytes()).hexdigest(),
                checks=len(test.checks),passed=len(test.checks)-len(failed),failed=len(failed),
                duration_seconds=round(time.perf_counter()-start,3),reference_sha256=before,gui_used=False,
                tolerance=dict(relative=1e-12,absolute=1e-8),results=test.checks)
    options.report.parent.mkdir(parents=True,exist_ok=True)
    options.report.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'Construction parity: {report["passed"]}/{report["checks"]} PASS; {report["failed"]} failed; {report["duration_seconds"]} s')
    for check in failed[:20]:print(check['name'],check['details'])
    print(options.report.resolve())
    return 1 if failed else 0

if __name__=='__main__':raise SystemExit(main())
