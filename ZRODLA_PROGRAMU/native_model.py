"""Migration adapter for the C++20 furniture calculation library.

The editor and exact CAD validation stay in their original modules while the
algorithms listed in native/core/*_ports.json execute in MebelModel.dll.
This adapter never turns a native algorithm error into a silent Python retry.
"""
from __future__ import annotations
import ctypes
import dataclasses
from functools import wraps
import json
import os
from pathlib import Path
import sys
import threading
from collections import OrderedDict

_library = None
_directory_handles = []
_load_lock = threading.Lock()
_calls = {}
_vertex_buffers = OrderedDict()
_vertex_buffer_bytes = 0

class PartPose(ctypes.Structure):
    _fields_ = [('origin',ctypes.c_double*3),('angles',ctypes.c_double*3),
                ('placement',ctypes.c_double*4),('plane',ctypes.c_uint32)]

assert ctypes.sizeof(PartPose) == 88

def library_path():
    folder = Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent
    return folder/'MebelModel.dll'

def available():
    return os.name == 'nt' and library_path().is_file()

def library():
    global _library
    if _library is not None:return _library
    with _load_lock:
        if _library is not None:return _library
        path=library_path()
        if not path.is_file():raise RuntimeError(f'Brakuje biblioteki obliczeniowej C++: {path}. Rozpakuj cały ZIP programu.')
        if hasattr(os,'add_dll_directory'):_directory_handles.append(os.add_dll_directory(str(path.parent)))
        lib=ctypes.CDLL(str(path),winmode=0x00000100|0x00001000)
        lib.ms3d_model_version.argtypes=[];lib.ms3d_model_version.restype=ctypes.c_uint32
        if lib.ms3d_model_version()!=1:raise RuntimeError('Niezgodna wersja biblioteki obliczeniowej C++.')
        lib.ms3d_model_call.argtypes=[ctypes.c_char_p];lib.ms3d_model_call.restype=ctypes.c_void_p
        lib.ms3d_model_free.argtypes=[ctypes.c_void_p];lib.ms3d_model_free.restype=None
        doubles=ctypes.POINTER(ctypes.c_double)
        lib.ms3d_model_transform.argtypes=[ctypes.POINTER(PartPose),doubles,ctypes.c_uint64,doubles]
        lib.ms3d_model_transform.restype=ctypes.c_int
        _library=lib
        return lib

def _default(value):
    # JSON makes its own immutable snapshot before entering the DLL. Recursively
    # copying every dataclass/list first doubles work and is unnecessary here.
    if dataclasses.is_dataclass(value):return {f.name:getattr(value,f.name) for f in dataclasses.fields(value)}
    if isinstance(value,Path):return str(value)
    if isinstance(value,set):return sorted(value)
    if hasattr(value,'item'):return value.item()
    raise TypeError(f'Nie można przekazać {type(value).__name__} do silnika C++.')

def call(module,function,*args,**kwargs):
    request=json.dumps(dict(module=module,function=function,args=args,kwargs=kwargs),
                       ensure_ascii=False,allow_nan=False,separators=(',',':'),default=_default).encode('utf-8')
    if len(request)>16*1024*1024:raise ValueError('Żądanie silnika jest zbyt duże.')
    lib=library();pointer=lib.ms3d_model_call(request)
    if not pointer:raise MemoryError('Silnik C++ nie mógł przygotować odpowiedzi.')
    try:response=json.loads(ctypes.string_at(pointer).decode('utf-8'))
    finally:lib.ms3d_model_free(pointer)
    key=f'{module}.{function}';_calls[key]=_calls.get(key,0)+1
    if not response.get('ok'):
        if response.get('type')=='OSError':raise ctypes.WinError(response.get('winerror',1))
        error={'RuntimeError':RuntimeError,'ZeroDivisionError':ZeroDivisionError}.get(response.get('type'),ValueError)
        raise error(response.get('error','Błąd silnika C++.'))
    return response['result']

def tuple_tree(value):
    return tuple(tuple_tree(v) for v in value) if isinstance(value,list) else value

def bind(namespace,module,names,*,conversions=None,preprocess=None):
    """Install only verified ports; source-only checkout can run without DLL."""
    if not available():
        if getattr(sys,'frozen',False):library() # A packaged release must use its library.
        return
    conversions=conversions or {};preprocess=preprocess or {}
    for name in names:
        original=namespace[name]
        if getattr(original,'_native_cpp',False):continue
        def make(fn,op):
            @wraps(fn)
            def native(*args,**kwargs):
                if op in preprocess:preprocess[op](*args,**kwargs)
                value=call(module,op,*args,**kwargs)
                converter=conversions.get(op)
                return converter(value) if converter else value
            native._native_cpp=True
            return native
        namespace[name]=make(original,name)

def transform(part,vertices):
    """One binary C++ call per part, preserving the original winding sign."""
    import numpy as np
    global _vertex_buffer_bytes
    key=id(vertices);cached=_vertex_buffers.get(key)
    if cached is not None and cached[0] is vertices:
        points=cached[1];_vertex_buffers.move_to_end(key)
    else:
        points=np.asarray(vertices,dtype=np.float64)
        if points.size==0:points=np.empty((0,3),dtype=np.float64)
        if points.ndim!=2 or points.shape[1]!=3:raise ValueError('Wierzchołek wymaga trzech współrzędnych.')
        points=np.ascontiguousarray(points)
        # Local CAD meshes are immutable tuples shared by pose changes. Keep
        # one binary buffer, with its original object alive to prevent id reuse.
        # Mutable lists/NumPy inputs are always read afresh.
        if isinstance(vertices,tuple) and all(isinstance(p,tuple) for p in vertices) and points.nbytes<=32*1024*1024:
            points.flags.writeable=False;_vertex_buffers[key]=(vertices,points);_vertex_buffer_bytes+=points.nbytes
            while len(_vertex_buffers)>192 or _vertex_buffer_bytes>32*1024*1024:
                _,old=_vertex_buffers.popitem(last=False);_vertex_buffer_bytes-=old[1].nbytes
    output=np.empty_like(points)
    layout=part.layout
    pose=PartPose((part.x,part.y,part.z),(part.rx,part.ry,part.rz),
                  tuple(layout.get(k,0) for k in ('x','y','z','yaw')),
                  {'XY':0,'XZ':1,'YZ':2}[part.plane])
    pointer=ctypes.POINTER(ctypes.c_double)
    sign=library().ms3d_model_transform(ctypes.byref(pose),points.ctypes.data_as(pointer),
                                      len(points),output.ctypes.data_as(pointer))
    if not sign:raise ValueError('Nieprawidłowa geometria lub ustawienie elementu w silniku C++.')
    _calls['viewport_render.transform']=_calls.get('viewport_render.transform',0)+1
    return tuple(map(tuple,output.tolist())),sign

def diagnostics():
    return dict(call('native','info'),library=str(library_path()),routed_functions=dict(_calls))

def save_project(project,path):
    project.validate()
    snapshot=json.dumps(project.data(),ensure_ascii=False,allow_nan=False,indent=2)
    call('project_io','save',str(Path(path).absolute()),snapshot)

def load_project(cls,path):
    return cls.from_data(json.loads(call('project_io','read',str(Path(path).absolute()),preserve_order=True)))

def install_core(namespace):
    def cut_result(value):
        for sheet in value[0]:sheet['free']=[tuple(p) for p in sheet['free']]
        return value[0],value[1]
    bind(namespace,'core',['validate_polygon','shade'],
         conversions={'validate_polygon':lambda v:[tuple(p) for p in v]})
    if not available():return
    project=namespace['Project']
    def stock_snapshot(self,costs=False):
        fields=('id','u','v','t','material','grain','edges','contour')
        if costs:fields+=('profile','contour_edge')
        snapshot=dict(parts=[{k:getattr(p,k) for k in fields} for p in self.parts],
                      materials=self.materials,kerf=self.kerf,margin=self.margin,edge_t=self.edge_t)
        if costs:snapshot.update(edge_price=self.edge_price,labor=self.labor,markup=self.markup,
                                 hardware=[dict(qty=h['qty'],price=h['price']) for h in self.hardware],
                                 scene=dict(objects=[dict(price=o.get('price',0)) for o in self.scene.get('objects',[])]))
        return snapshot
    original_cut=namespace['cut_plan']
    @wraps(original_cut)
    def cut_plan(self):
        self.validate()
        return cut_result(call('core','cut_plan',stock_snapshot(self)))
    cut_plan._native_cpp=True;namespace['cut_plan']=cut_plan
    original_costs=project.costs;original_save=project.save;original_load=project.load.__func__
    @wraps(original_costs)
    def costs(self):return call('core','Project.costs',stock_snapshot(self,True))
    @wraps(original_save)
    def save(self,path):return save_project(self,path)
    @wraps(original_load)
    def load(cls,path):return load_project(cls,path)
    costs._native_cpp=save._native_cpp=load._native_cpp=True
    project.costs=costs;project.save=save;project.load=classmethod(load)

def runner_profile(value):
    for key in ('depth_by_length','article_by_length','source_by_length','load_by_length','travel_by_length'):
        if key in value:value[key]={int(k):v for k,v in value[key].items()}
    return value

def install_construction(namespace,module):
    if not available():
        if getattr(sys,'frozen',False):library()
        return
    ports={
        'runners_catalog':['get_profile','calculate_runner','available_lengths'],
        # Legacy helper entry points remain available. Complete placement loops
        # are now batched by native_layout; individual comparisons never cross
        # the JSON boundary inside the C++ magnet/collision implementation.
        'furniture_layout':[],
        'corner_furniture':['corner_outline','normalize_corner_config'],
        'assembly':['recipe'],
    }
    if module=='runners_catalog':
        import copy
        maps=('depth_by_length','article_by_length','source_by_length','load_by_length','travel_by_length')
        def profile_input(profile,calculation=False):
            if not isinstance(profile,dict):return profile
            value=copy.deepcopy(profile)
            value.setdefault('id',next((key for key,p in namespace['PROFILES'].items() if p==profile),'custom'))
            if calculation:
                for key in maps:
                    if key in value:
                        import math
                        value[key]={int(k) if isinstance(k,bool) else k:v for k,v in value[key].items()
                                    if isinstance(k,(int,float)) and math.isfinite(k)}
            return value
        original=namespace['get_profile']
        @wraps(original)
        def get_profile(profile):
            value=call(module,'get_profile',profile_input(profile))
            if isinstance(profile,dict):
                for key in maps:
                    if key in profile:value[key]=copy.deepcopy(profile[key])
                return value
            return runner_profile(value)
        get_profile._native_cpp=True;namespace['get_profile']=get_profile
        for name in ('calculate_runner','available_lengths'):
            fn=namespace[name]
            def make_runner(fn,op):
                @wraps(fn)
                def run(*args,**kwargs):
                    if args:args=(profile_input(args[0],True),)+args[1:]
                    elif 'profile' in kwargs:kwargs={**kwargs,'profile':profile_input(kwargs['profile'],True)}
                    return call(module,op,*args,**kwargs)
                run._native_cpp=True
                return run
            namespace[name]=make_runner(fn,name)
        return
    bind(namespace,module,ports[module],conversions={
        'get_profile':runner_profile,'_translated':tuple_tree,
        '_flat_faces':lambda v:[tuple(p) for p in v],
        'corner_outline':lambda v:[tuple(p) for p in v]})

def install_viewport(namespace):
    if not available():return
    original=namespace['transform']
    wrapped=wraps(original)(transform)
    wrapped._native_cpp=True
    namespace['transform']=wrapped

def install_services(namespace,module):
    if module=='responsive_ui':
        def pixels(value):
            # Tk expects integer strings for widget heights; JSON numbers from
            # the C++ coordinate kernel may be floating-point integral values.
            return int(value) if isinstance(value,float) and value.is_integer() else value
        bind(namespace,module,['flow_layout','clamp_inspector'],
             conversions={'flow_layout':lambda v:([tuple(pixels(n) for n in p) for p in v[0]],pixels(v[1])),
                          'clamp_inspector':pixels})
    elif module=='depth_raster':
        bind(namespace,module,['triangulate'],conversions={'triangulate':tuple_tree})

def install_shapes(namespace,module):
    ports={
        'shaped_furniture':['_resolution','_finish','contour','shape_definition','profile_area','_ellipse_perimeter','profile_perimeter','styled_bounds'],
        'features':['validate_drill','drill_location','validate_cut','hole_array'],
        'sketch':[],
    }
    def styled(value):
        if value is None:return None
        return {k:tuple(v) for k,v in value.items()}
    bind(namespace,module,ports[module],conversions={
        '_finish':lambda v:[tuple(p) for p in v],
        'contour':lambda v:[tuple(p) for p in v],
        'styled_bounds':styled,'drill_location':tuple_tree})
    if not available():return
    if module=='features':
        for name in ('validate_drill','drill_location','validate_cut','hole_array'):
            original=namespace[name].__wrapped__
            def make_feature(fn,op):
                @wraps(fn)
                def run(*args,**kwargs):
                    part=args[0] if args else kwargs['part']
                    geometry={key:getattr(part,key) for key in ('u','v','t')}
                    # Exact CAD drilling also calls this with a stock proxy
                    # exposing dimensions only; drill_location needs no contour.
                    geometry['contour']=getattr(part,'contour',[])
                    if args:args=(geometry,)+args[1:]
                    else:kwargs={**kwargs,'part':geometry}
                    value=call(module,op,*args,**kwargs)
                    return tuple_tree(value) if op=='drill_location' else value
                run._native_cpp=True
                return run
            namespace[name]=make_feature(original,name)
    mutations={
        'shaped_furniture':{'resize_profile':('profile','contour'),'refresh_decor':('machining','decor'),'remove_decor':('machining','decor')},
        'features':{'shift_features':('operations','machining')},
        'sketch':{},
    }
    for name,fields in mutations[module].items():
        original=namespace[name]
        def make_mutation(fn,op,fields):
            @wraps(fn)
            def run(*args,**kwargs):
                part=args[0] if args else kwargs['part']
                value=call(module,op,*args,**kwargs)
                for field in fields:
                    data=value[field]
                    if field=='contour':data=[tuple(p) for p in data]
                    setattr(part,field,data)
            run._native_cpp=True
            return run
        namespace[name]=make_mutation(original,name,fields)
    if module=='sketch':
        original=namespace['normalized_part']
        @wraps(original)
        def normalized_part(part,points):
            data=call('sketch','normalized_part',part,points)
            data['contour']=[tuple(p) for p in data.get('contour',[])]
            result=namespace['Part'](**data);result.validate();return result
        normalized_part._native_cpp=True;namespace['normalized_part']=normalized_part
