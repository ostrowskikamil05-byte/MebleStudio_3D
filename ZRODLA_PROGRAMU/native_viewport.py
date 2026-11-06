"""Native C++ DirectX/Vulkan viewport inside the existing CAD workspace.

The Tk canvas keeps its complete editor bindings and 2D drawing commands.
Only immutable presentation triangles and annotations cross the C ABI.
"""
from __future__ import annotations
import ctypes as C
import json
import math
from pathlib import Path
import sys
import tkinter as tk
import tkinter.font as tkfont

NATIVE_ENGINES=('auto','d3d11','d3d12','vulkan')
NONE=0xffffffff

class NativeUnavailable(RuntimeError):pass

class NativeCamera(C.Structure):
    _fields_=[('yaw',C.c_float),('pitch',C.c_float),('scale',C.c_float),
              ('panX',C.c_float),('panY',C.c_float),('center',C.c_float*3),
              ('brightness',C.c_float),('xray',C.c_uint32),('selected',C.c_int32)]

class NativeHit(C.Structure):
    _fields_=[('owner',C.c_int32),('kind',C.c_uint32),('flags',C.c_uint32),
              ('world',C.c_float*3),('depth',C.c_float)]

class NativeOverlay(C.Structure):
    _fields_=[('type',C.c_uint32),('outline',C.c_uint32),('fill',C.c_uint32),
              ('flags',C.c_uint32),('width',C.c_float),('coordinates',C.POINTER(C.c_float)),
              ('count',C.c_uint32),('text',C.c_wchar_p),('font',C.c_wchar_p),
              ('height',C.c_int32),('bold',C.c_int32),('anchor',C.c_int32)]

_api=None
def _library():
    global _api
    if _api is not None:return _api
    if sys.platform!='win32':raise NativeUnavailable('Silnik natywny wymaga systemu Windows.')
    root=Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent
    path=root/'MebelNative3D.dll'
    if not path.is_file():raise NativeUnavailable('Brak biblioteki MebelNative3D.dll w folderze programu.')
    try:api=C.CDLL(str(path),winmode=0x00000100|0x00000800)
    except OSError as exc:raise NativeUnavailable(f'Nie udało się wczytać silnika C++: {exc}') from exc
    error=(C.POINTER(C.c_wchar),C.c_uint32)
    signatures={
        'ms3d_create':([C.c_size_t,C.c_char_p,*error],C.c_void_p),
        'ms3d_upload':([C.c_void_p,C.POINTER(C.c_uint8),C.c_uint64,*error],C.c_int),
        'ms3d_frame':([C.c_void_p,C.POINTER(NativeCamera),*error],C.c_int),
        'ms3d_resize':([C.c_void_p,C.c_uint32,C.c_uint32,*error],C.c_int),
        'ms3d_overlay':([C.c_void_p,C.POINTER(NativeOverlay),C.c_uint32,*error],C.c_int),
        'ms3d_pick':([C.c_void_p,C.c_float,C.c_float,C.c_uint32,C.POINTER(NativeHit)],C.c_int),
        'ms3d_show':([C.c_void_p,C.c_int],C.c_int),
        'ms3d_info':([C.c_void_p,*error],C.c_int),
        'ms3d_close':([C.c_void_p],None),
    }
    try:
        for name,(args,result) in signatures.items():
            function=getattr(api,name);function.argtypes=args;function.restype=result
    except AttributeError as exc:raise NativeUnavailable('Biblioteka silnika C++ ma niezgodny interfejs.') from exc
    _api=api;return api

class NativeViewport:
    def __init__(self,canvas,backend):
        self.canvas=canvas;self.api=_library();self.backend=backend;self.error=C.create_unicode_buffer(4096)
        self.handle=self.api.ms3d_create(canvas.winfo_id(),backend.encode('ascii'),self.error,len(self.error))
        if not self.handle:raise NativeUnavailable(self.error.value or 'Nie udało się uruchomić silnika C++.')
        self.closed=False;self.cache_key=None;self.payload=None;self.sources=();self.size=None
        self.info='';self.drawn=[];self.failed_request=None;self.warning=None
    def call(self,name,*args):
        self.error.value=''
        if not getattr(self.api,name)(self.handle,*args,self.error,len(self.error)):
            raise NativeUnavailable(self.error.value or 'Błąd silnika C++: '+name)
    def close(self):
        if not self.closed:
            self.closed=True;self.api.ms3d_close(self.handle);self.handle=None
    def show(self,visible):
        if not self.closed:self.api.ms3d_show(self.handle,int(visible))
    def frame(self,project,camera,size,*,interactive=False,selected_ids=(),selected=None,
              explode=0,xray=False,xray_ids=None,quality='balanced',**kwargs):
        from viewport_render import prepare_scene
        from native_scene_export import build_snapshot
        from gpu_scene import mesh_groups,cheap_drawn,visible_architecture,ghost_ids
        context=prepare_scene(project)
        ghosts=ghost_ids(project,xray,xray_ids)
        selection=set(selected_ids)
        if selected:selection.add(selected)
        key=(id(context),bool(interactive),explode,tuple(camera.center) if explode else (),
             tuple(sorted(selection)),tuple(sorted(ghosts)),quality)
        if self.size!=size:
            self.call('ms3d_resize',max(1,int(size[0])),max(1,int(size[1])));self.size=size
        if key!=self.cache_key:
            payload,report=build_snapshot(project,context=context,interactive=interactive,
                camera=camera,explode=explode,selected_ids=selection,xray_ids=ghosts)
            storage=(C.c_uint8*len(payload)).from_buffer_copy(payload)
            self.call('ms3d_upload',storage,len(payload))
            self.payload=payload;self.context=context;self.sources=report['owner_sources'];self.cache_key=key
            self.meshes=mesh_groups(context,interactive)
            self.diagnostics=report
        frame=NativeCamera(camera.yaw,camera.pitch,camera.scale,*camera.pan,
            (C.c_float*3)(*camera.center),float(context.scene.get('brightness',1)),2 if xray else 0,-1)
        self.call('ms3d_frame',C.byref(frame))
        self.drawn=cheap_drawn(context,self.meshes,camera,size,selection,explode,visible_architecture(project,camera))
        info=C.create_unicode_buffer(4096);self.api.ms3d_info(self.handle,info,len(info));self.info=info.value
        try:hardware=json.loads(self.info)
        except (ValueError,TypeError):hardware={'engine':self.info}
        return dict(ok=True,engine='native',drawn=self.drawn,
                    diagnostics={**self.diagnostics,**hardware,'native':True},warning=self.warning)
    def hit(self,x,y,mode=0):
        if self.closed:return None
        result=NativeHit()
        if not self.api.ms3d_pick(self.handle,float(x),float(y),mode,C.byref(result)):return None
        if not 0<=result.owner<len(self.sources):return None
        identity,kind=self.sources[result.owner]
        return dict(id=identity,kind=kind,world=tuple(result.world),depth=float(result.depth),
                    precise_surface=kind=='part',flags=result.flags)

def _attach(canvas):
    if getattr(canvas,'_native_bound',False):return
    canvas._native_bound=True
    def destroyed(event):
        if event.widget is canvas:close_native(canvas)
    def visibility(event):
        viewport=getattr(canvas,'_native_viewport',None)
        if viewport:viewport.show(event.type==tk.EventType.Map)
    canvas.bind('<Destroy>',destroyed,add='+')
    canvas.bind('<Map>',visibility,add='+');canvas.bind('<Unmap>',visibility,add='+')

def paint_native(canvas,project,*,backend='auto',camera,size,**options):
    canvas._native_background_items=set(canvas.find_all())
    canvas._native_requested_backend=backend
    if getattr(canvas,'_native_failed_backend',None)==backend:
        return dict(ok=False,engine='cpu',error=canvas._native_failed_reason)
    canvas._native_failed_backend=None
    viewport=getattr(canvas,'_native_viewport',None)
    candidate=None
    try:
        _attach(canvas)
        if viewport is None or viewport.closed or (viewport.backend!=backend and viewport.failed_request!=backend):
            try:
                candidate=NativeViewport(canvas,backend)
                result=candidate.frame(project,camera,size,**options)
            except (NativeUnavailable,OSError,ValueError,RuntimeError) as exc:
                if candidate:candidate.close()
                candidate=None
                if viewport is None or viewport.closed:raise
                viewport.failed_request=backend;viewport.warning=f'{backend}: {exc}; zachowano {viewport.backend}.'
                result=viewport.frame(project,camera,size,**options)
            else:
                if viewport:viewport.close()
                viewport=candidate;canvas._native_viewport=viewport;candidate=None
        else:
            if viewport.backend==backend:viewport.failed_request=None;viewport.warning=None
            result=viewport.frame(project,camera,size,**options)
        canvas._depth_buffer=canvas._id_buffer=None
        canvas._native_pick=lambda x,y:(hit['id'] if (hit:=viewport.hit(x,y)) else None)
        canvas._native_world=lambda x,y:viewport.hit(x,y,1)
        canvas._native_depth=lambda x,y:viewport.hit(x,y)
        canvas._render_diagnostics=result['diagnostics'];canvas._active_renderer='native';canvas._native_pending=True
        if not getattr(canvas,'_native_commit_job',None):
            def commit():
                canvas._native_commit_job=None
                try:flush_native(canvas)
                except (NativeUnavailable,tk.TclError,OSError,ValueError) as exc:
                    canvas._native_error=str(exc)
                    recover=getattr(canvas,'_native_recover',None)
                    if recover:recover()
            canvas._native_commit_job=canvas.after_idle(commit)
        return result
    except (NativeUnavailable,OSError,ValueError,RuntimeError) as exc:
        if candidate:candidate.close()
        close_native(canvas)
        canvas._native_failed_backend=backend;canvas._native_failed_reason=str(exc)
        return dict(ok=False,engine='cpu',error=str(exc))

def reset_native_failure(canvas):
    canvas._native_failed_backend=None
    viewport=getattr(canvas,'_native_viewport',None)
    if viewport:viewport.failed_request=None

def close_native(canvas):
    job=getattr(canvas,'_native_commit_job',None)
    if job:
        try:canvas.after_cancel(job)
        except tk.TclError:pass
        canvas._native_commit_job=None
    viewport=getattr(canvas,'_native_viewport',None)
    if viewport:viewport.close()
    canvas._native_viewport=None;canvas._native_pending=False
    for name in ('_native_pick','_native_world','_native_depth'):setattr(canvas,name,None)

def overlay_commands(canvas):
    """Mirror editor annotations; native GPU already draws model/background."""
    types={'line':1,'rectangle':2,'oval':3,'polygon':4,'text':5}
    anchors={'nw':0,'n':1,'ne':2,'w':3,'center':4,'e':5,'sw':6,'s':7,'se':8}
    commands=[];storage=[];colors={};fonts={}
    def color(value):
        if not value:return NONE
        if value not in colors:
            r,g,b=canvas.winfo_rgb(value);colors[value]=(r>>8)|((g>>8)<<8)|((b>>8)<<16)
        return colors[value]
    for item in canvas.find_all():
        if item in getattr(canvas,'_native_background_items',()):continue
        kind=canvas.type(item)
        if kind not in types:continue
        tags=canvas.gettags(item)
        if any(t.startswith(('viewport.background','viewport.floor','viewport.grid','viewport.shadow',
                             'viewport.light_pool','viewport.raster_model','viewport.gpu_model',
                             'viewport.grain','viewport.ghost')) for t in tags) or 'model' in tags:continue
        if canvas.itemcget(item,'state')=='hidden':continue
        coords=canvas.coords(item)
        if len(coords)<2:continue
        points=(C.c_float*len(coords))(*coords);storage.append(points)
        command=NativeOverlay();command.type=types[kind];command.coordinates=points;command.count=len(coords)
        command.width=1;command.outline=NONE;command.fill=NONE;command.anchor=4
        if kind=='text':
            command.fill=color(canvas.itemcget(item,'fill'));command.text=canvas.itemcget(item,'text')
            specification=canvas.itemcget(item,'font')
            if specification not in fonts:fonts[specification]=tkfont.Font(root=canvas,font=specification).actual()
            font=fonts[specification];command.font=font['family']
            command.width=float(canvas.itemcget(item,'width'))
            command.height=max(1,round(abs(font['size'])*(float(canvas.winfo_fpixels('1i'))/72 if font['size']>0 else 1)))
            command.bold=int(font['weight']=='bold');command.anchor=anchors.get(canvas.itemcget(item,'anchor'),4)
        else:
            command.width=float(canvas.itemcget(item,'width'))
            if kind=='line':
                command.outline=color(canvas.itemcget(item,'fill'))
                arrow=canvas.itemcget(item,'arrow');command.flags|=2 if arrow in ('first','both') else 0
                command.flags|=4 if arrow in ('last','both') else 0
            else:
                command.outline=color(canvas.itemcget(item,'outline'));command.fill=color(canvas.itemcget(item,'fill'))
            if canvas.itemcget(item,'dash'):command.flags|=1
        commands.append(command)
    return (NativeOverlay*len(commands))(*commands),storage

def flush_native(canvas):
    job=getattr(canvas,'_native_commit_job',None)
    if job:
        try:canvas.after_cancel(job)
        except tk.TclError:pass
        canvas._native_commit_job=None
    viewport=getattr(canvas,'_native_viewport',None)
    if viewport is None or viewport.closed or not getattr(canvas,'_native_pending',False):return
    try:
        commands,storage=overlay_commands(canvas)
        viewport.call('ms3d_overlay',commands,len(commands))
        viewport.show(canvas.winfo_ismapped());canvas._native_pending=False
    except (tk.TclError,NativeUnavailable,ValueError,OSError) as exc:
        backend=getattr(canvas,'_native_requested_backend',viewport.backend)
        close_native(canvas)
        canvas._native_failed_backend=backend;canvas._native_failed_reason=str(exc)
        raise

def install(app):
    """Commit overlays after the final editor draw wrapper, on the Tk thread."""
    original=app.draw
    def draw():
        result=original()
        try:flush_native(app.canvas)
        except (NativeUnavailable,tk.TclError,OSError,ValueError) as exc:
            if hasattr(app,'log'):app.log.warning('Nakładka silnika C++: %s',exc)
            saved=app.viewport_engine;app.viewport_engine='cpu'
            try:original()
            finally:app.viewport_engine=saved
        return result
    app.draw=draw
    def recover():
        saved=app.viewport_engine;app.viewport_engine='cpu'
        try:original()
        finally:app.viewport_engine=saved
    app.canvas._native_recover=recover

commit_native=flush_native
native_close=close_native
