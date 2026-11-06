"""Optional retained VTK/OpenGL renderer displayed in the existing Tk Canvas.

The offscreen framebuffer leaves Canvas input, detached tabs and annotation
overlays under Tk control. RGBA, real depth and an encoded visible-owner pass
are read from the same retained geometry. VTK is imported only on first use.
"""
import copy
import math
import time
import numpy as np
from PIL import Image,ImageTk
from gpu_scene import (camera_spec,z_to_depth,unproject,mesh_groups,ghost_ids,
                       visible_architecture,material_style,cheap_drawn,batch_groups)


class GPUUnavailable(RuntimeError):pass


def _vtk():
    try:
        from vtkmodules.vtkCommonCore import vtkPoints,vtkFloatArray,vtkUnsignedCharArray,vtkVersion
        from vtkmodules.vtkCommonDataModel import vtkPolyData,vtkCellArray
        from vtkmodules.vtkFiltersCore import vtkPolyDataNormals
        from vtkmodules.vtkRenderingCore import vtkRenderer,vtkRenderWindow,vtkPolyDataMapper,vtkActor,vtkLight
        from vtkmodules.vtkRenderingOpenGL2 import vtkSSAOPass,vtkRenderStepsPass
        from vtkmodules.util.numpy_support import numpy_to_vtk,numpy_to_vtkIdTypeArray,vtk_to_numpy
    except (ImportError,OSError) as exc:
        raise GPUUnavailable('VTK/OpenGL nie jest dostępny: '+str(exc)) from exc
    return locals()


def _rgb(color):return tuple(int(color[i:i+2],16)/255 for i in (1,3,5))


class ActorMesh:
    def __init__(self,api,group,project):
        self.group=group
        points=api['vtkPoints']();points.SetData(api['numpy_to_vtk'](group.vertices,deep=True))
        cells=api['vtkCellArray']();cells.SetData(
            api['numpy_to_vtkIdTypeArray'](group.offsets,deep=True),
            api['numpy_to_vtkIdTypeArray'](group.connectivity,deep=True))
        data=api['vtkPolyData']();data.SetPoints(points);data.SetPolys(cells)
        normals=api['vtkPolyDataNormals']();normals.SetInputData(data)
        normals.SetFeatureAngle(42);normals.SplittingOn();normals.ConsistencyOff()
        normals.AutoOrientNormalsOff();normals.ComputePointNormalsOn()
        normals.Update();data=normals.GetOutput()
        for name,values in (('Beauty',group.cell_colors),('TargetId',group.cell_ids)):
            colors=api['numpy_to_vtk'](values,deep=True);colors.SetName(name)
            data.GetCellData().AddArray(colors)
        mapper=api['vtkPolyDataMapper']();mapper.SetInputData(data)
        mapper.SetScalarModeToUseCellFieldData();mapper.SetColorModeToDirectScalars()
        mapper.SelectColorArray('Beauty');mapper.ScalarVisibilityOn()
        # World-space grain needs the actual model coordinates, rather than
        # the mapper's automatic [0..1] vertex compression coordinates.
        mapper.SetVBOShiftScaleMethod(0)
        actor=api['vtkActor']();actor.SetMapper(mapper)
        if material_style(project,group.source_groups[0])['wood']:
            # Procedural grain stays in world coordinates while orbiting. It
            # only changes beauty RGB; the separate ID actor stays untouched.
            shader=actor.GetShaderProperty()
            shader.AddVertexShaderReplacement('//VTK::PositionVC::Dec',True,
                '//VTK::PositionVC::Dec\nout vec3 furnitureGrain;\n',False)
            shader.AddVertexShaderReplacement('//VTK::PositionVC::Impl',True,
                '//VTK::PositionVC::Impl\nfurnitureGrain=vertexMC.xyz;\n',False)
            shader.AddFragmentShaderReplacement('//VTK::Light::Dec',True,
                '//VTK::Light::Dec\nin vec3 furnitureGrain;\n',False)
            shader.AddFragmentShaderReplacement('//VTK::Light::Impl',True,
                '//VTK::Light::Impl\nfloat along=furnitureGrain.z+.17*furnitureGrain.y;\n'
                'float ring=furnitureGrain.x+9.*sin(along*.008)+2.*sin(along*.025);\n'
                'float fiber=sin(ring*.24+3.*sin(ring*.02)+.45*sin(along*.11));\n'
                'gl_FragData[0].rgb *= .985+.028*fiber+.009*sin(ring*1.7);\n',False)
        id_mapper=api['vtkPolyDataMapper']();id_mapper.SetInputData(data)
        id_mapper.SetScalarModeToUseCellFieldData();id_mapper.SetColorModeToDirectScalars()
        id_mapper.SelectColorArray('TargetId');id_mapper.ScalarVisibilityOn()
        id_actor=api['vtkActor']();id_actor.SetMapper(id_mapper);id_actor.VisibilityOff()
        id_actor.GetProperty().LightingOff();id_actor.GetProperty().SetColor(1,1,1)
        self.data,self.normals,self.mapper,self.actor=data,normals,mapper,actor
        self.id_mapper,self.id_actor=id_mapper,id_actor


class GPUViewport:
    def __init__(self):
        self.api=_vtk();a=self.api
        self.closed=False;self.window=None;self.renderer=None
        try:
            self.window=a['vtkRenderWindow']();self.window.SetOffScreenRendering(1)
            self.window.SetMultiSamples(0);self.window.SetAlphaBitPlanes(1)
            # No visible VTK window needs buffer presentation/vsync. Canvas owns
            # the displayed image and the readback always uses the back buffer.
            self.window.SetSwapBuffers(0)
            self.renderer=a['vtkRenderer']();self.renderer.SetBackground(0,0,0)
            self.renderer.SetBackgroundAlpha(0);self.renderer.SetAutomaticLightCreation(False)
            self.window.AddRenderer(self.renderer);self.window.SetSize(64,64)
            self.window.Render()
            if not self.window.SupportsOpenGL():
                raise GPUUnavailable('Sterownik nie obsługuje OpenGL wymaganego przez VTK.')
            self.capabilities=self.window.ReportCapabilities()
        except Exception:
            self.close();raise
        self.context=None;self.groups=[];self.meshes={};self.current=[]
        self.ids=();self.lookup={};self.camera=None;self.size=(64,64)
        self.rgba_array=a['vtkUnsignedCharArray']();self.z_array=a['vtkFloatArray']()
        self.lights=[];self._light_key=None;self._ssao=None;self._steps=None
        self.diagnostics={};self.depth_buffer=None;self.id_buffer=None

    def close(self):
        if getattr(self,'closed',False):return
        self.closed=True
        try:
            renderer=getattr(self,'renderer',None)
            if renderer is not None:
                renderer.RemoveAllViewProps();renderer.RemoveAllLights();renderer.SetPass(None)
        finally:
            try:
                window=getattr(self,'window',None)
                if window is not None:window.Finalize()
            finally:
                self.meshes={};self.current=[];self.groups=[];self.context=None
                self.lights=[];self.bounds=[];self.drawn=[];self.ids=();self.lookup={}
                self.depth_buffer=None;self.id_buffer=None;self.kind_buffer=None
                self.surface_owner=None;self.surface_kind=None
                self.rgba_array=None;self.z_array=None;self._ssao=None;self._steps=None
                self.camera=None;self.signature=None;self.diagnostics={}

    def sync(self,project,interactive,selection,ghosts,explode,camera):
        from viewport_render import prepare_scene
        context=prepare_scene(project)
        mode=bool(interactive)
        signature=(id(context),mode,tuple(sorted(selection)),tuple(sorted(ghosts)),explode,
                   tuple(camera.center) if explode else ())
        if getattr(self,'signature',None)==signature:return False
        groups=self.groups if self.context is context and getattr(self,'mode',None)==mode else mesh_groups(context,mode)
        batches=batch_groups(groups,project,selection,ghosts,explode,camera)
        previous=self.meshes;meshes={};current=[]
        for group in batches:
            mesh=previous.get(group.key)
            if mesh is None:mesh=ActorMesh(self.api,group,project)
            meshes[group.key]=mesh;current.append(mesh)
            if mesh not in self.current:
                self.renderer.AddActor(mesh.actor);self.renderer.AddActor(mesh.id_actor)
        for mesh in self.current:
            if mesh not in current:
                self.renderer.RemoveActor(mesh.actor);self.renderer.RemoveActor(mesh.id_actor)
        self.context,self.mode,self.groups,self.meshes,self.current=context,mode,groups,meshes,current
        self.signature=signature
        self.ids=tuple(sorted({g.owner for g in groups}));self.lookup={owner:i for i,owner in enumerate(self.ids)}
        self.surface_owner=np.array([-1]+[self.lookup[g.owner] for g in groups],dtype=np.int32)
        self.surface_kind=np.array([0]+[2 if g.scene_object else 1 if g.fitting else 0 for g in groups],dtype=np.uint8)
        # Read only the projected scene rectangle. Empty pixels outside it are
        # already known, without synchronous full-screen GPU readbacks.
        if batches:
            lo=np.min([g.vertices.min(axis=0) for g in batches],axis=0)
            hi=np.max([g.vertices.max(axis=0) for g in batches],axis=0)
            self.bounds=[tuple(hi[i] if bit[i] else lo[i] for i in range(3))
                for bit in ((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1))]
        else:self.bounds=[]
        return True

    def configure_camera(self,camera,size,sample_scale=1):
        self.output_size=tuple(max(1,int(v)) for v in size)
        self.size=tuple(max(1,round(v*sample_scale)) for v in self.output_size)
        self.sample_scale=min(self.size[i]/self.output_size[i] for i in range(2))
        self.window.SetSize(*self.size)
        vertices=self.context.stock+self.context.room_vertices+getattr(self.context,'architecture_vertices',[])
        spec=camera_spec(camera,self.output_size,vertices)
        active=self.renderer.GetActiveCamera();active.ParallelProjectionOn()
        active.SetPosition(*spec['position']);active.SetFocalPoint(*spec['focal_point'])
        active.SetViewUp(*spec['up']);active.SetParallelScale(spec['parallel_scale'])
        active.SetWindowCenter(*spec['window_center']);active.SetClippingRange(*spec['clipping'])
        active.SetUseExplicitAspectRatio(True)
        active.SetExplicitAspectRatio(self.output_size[0]/self.output_size[1])
        self.spec=spec;self.camera=copy.copy(camera)
        projected=[camera.project(point,*self.output_size) for point in self.bounds]
        if projected:
            padding=16
            x0=max(0,math.floor(min(p[0] for p in projected)*self.size[0]/self.output_size[0])-padding)
            y0=max(0,math.floor(min(p[1] for p in projected)*self.size[1]/self.output_size[1])-padding)
            x1=min(self.size[0]-1,math.ceil(max(p[0] for p in projected)*self.size[0]/self.output_size[0])+padding)
            y1=min(self.size[1]-1,math.ceil(max(p[1] for p in projected)*self.size[1]/self.output_size[1])+padding)
            if x1<x0 or y1<y0:x0=y0=x1=y1=0
        else:x0=y0=x1=y1=0
        self.roi=x0,y0,x1,y1

    def configure_lights(self,project,camera):
        scene=getattr(project,'scene',{});brightness=max(.1,min(2,scene.get('brightness',1)))
        lights=self.context.lights
        boost=1.8 if getattr(self,'quality','balanced')=='high' else 1
        key=(brightness,boost,tuple((tuple(l['position']),l.get('color'),l.get('intensity'),
                              tuple(l.get('direction',()))) for l in lights))
        if key!=self._light_key:
            self.renderer.RemoveAllLights();self.lights=[]
            # Stable studio sources in world space keep material highlights
            # coherent while orbiting rather than baking camera-based colors.
            center=camera.center
            for position,intensity,color in (((-3000,-4000,6000),.75,'#fff4df'),
                                              ((4000,1000,4500),.35,'#d9e8ff')):
                light=self.api['vtkLight']();light.SetLightTypeToSceneLight()
                light.SetPosition(*position);light.SetFocalPoint(*center)
                light.SetColor(*_rgb(color));light.SetIntensity(intensity*brightness*boost)
                self.renderer.AddLight(light);self.lights.append(light)
            # A compact LED sampling budget keeps real lighting interactive.
            for lamp in lights[:12]:
                light=self.api['vtkLight']();light.SetLightTypeToSceneLight();light.PositionalOn()
                p=lamp['position'];light.SetPosition(*p)
                direction=lamp.get('direction',(0,0,-1))
                light.SetFocalPoint(*(p[i]+direction[i]*100 for i in range(3)))
                light.SetConeAngle(85 if lamp.get('direction') else 180)
                light.SetColor(*_rgb(lamp.get('color','#fff3bf')))
                light.SetIntensity(lamp.get('intensity',1)*brightness*1.4)
                radius=max(1,lamp.get('radius',650));light.SetAttenuationValues(1,0,1/radius**2)
                self.renderer.AddLight(light);self.lights.append(light)
            self._light_key=key

    def quality_pass(self,quality,interactive):
        use_ssao=quality=='high' and not interactive
        if use_ssao:
            if self._ssao is None:
                self._steps=self.api['vtkRenderStepsPass']()
                self._ssao=self.api['vtkSSAOPass']();self._ssao.SetDelegatePass(self._steps)
                self._ssao.SetRadius(70);self._ssao.SetBias(1.5);self._ssao.SetKernelSize(12)
                self._ssao.SetIntensityScale(.35);self._ssao.SetIntensityShift(.65)
                self._ssao.BlurOn()
            self.renderer.SetPass(self._ssao)
        else:self.renderer.SetPass(None)

    def material_pass(self,project,selected,ghosts,explode,camera,visible):
        from core import explosion_shift
        owners=self.context.owners
        for mesh in self.current:
            group=mesh.group;actor=mesh.actor
            actor.SetVisibility(not group.architecture or group.owner in visible)
            mesh.id_actor.VisibilityOff()
            actor.SetPosition(0,0,0)
            prop=actor.GetProperty();prop.SetColor(*_rgb(group.color));prop.LightingOn()
            prop.SetAmbient(.2);prop.SetDiffuse(.78);prop.SetInterpolationToPhong()
            style=material_style(project,group.source_groups[0]);prop.SetSpecular(style['specular']);prop.SetSpecularPower(style['power'])
            if getattr(self,'quality','balanced')=='high' and not group.emissive:
                prop.SetInterpolationToPBR();prop.SetRoughness(style['roughness']);prop.SetMetallic(style['metallic'])
            prop.SetOpacity(.13 if group.ghost else group.opacity)
            prop.SetRepresentationToSurface();prop.EdgeVisibilityOff()
            if group.emissive:
                prop.SetAmbient(1);prop.SetDiffuse(0);prop.SetSpecular(0)

    def read_rgba(self,cropped=False):
        width,height=self.size
        x0,y0,x1,y1=self.roi
        if not self.window.GetRGBACharPixelData(x0,height-1-y1,x1,height-1-y0,0,self.rgba_array):
            raise GPUUnavailable('Nie udało się odczytać obrazu OpenGL.')
        result=self.api['vtk_to_numpy'](self.rgba_array).reshape((y1-y0+1,x1-x0+1,4))[::-1].copy()
        if cropped:return result
        full=np.zeros((height,width,4),dtype=np.uint8);full[y0:y1+1,x0:x1+1]=result
        return full

    def read_depth(self):
        width,height=self.size
        x0,y0,x1,y1=self.roi
        if not self.window.GetZbufferData(x0,height-1-y1,x1,height-1-y0,self.z_array):
            raise GPUUnavailable('Nie udało się odczytać bufora głębokości OpenGL.')
        z=self.api['vtk_to_numpy'](self.z_array).reshape((y1-y0+1,x1-x0+1))[::-1]
        depth=np.full((height,width),-np.inf,dtype=np.float32)
        depth[y0:y1+1,x0:x1+1]=z_to_depth(z,self.spec)
        return depth

    def owner_pass(self,ghosts):
        self.renderer.SetPass(None)
        for mesh in self.current:
            visible=mesh.actor.GetVisibility() and not mesh.group.ghost and not mesh.group.visual_only
            mesh.id_actor.SetVisibility(visible);mesh.actor.VisibilityOff()
        self.window.Render();return self.read_owner()

    def read_owner(self):
        colors=self.read_rgba(cropped=True)
        codes=(colors[:,:,0].astype(np.int32)|(colors[:,:,1].astype(np.int32)<<8)
               |(colors[:,:,2].astype(np.int32)<<16))
        codes[(codes<0)|(codes>=len(self.surface_owner))]=0
        x0,y0,x1,y1=self.roi
        owners=np.full((self.size[1],self.size[0]),-1,dtype=np.int32)
        owners[y0:y1+1,x0:x1+1]=self.surface_owner[codes]
        self.kind_buffer=np.zeros(owners.shape,dtype=np.uint8)
        self.kind_buffer[y0:y1+1,x0:x1+1]=self.surface_kind[codes]
        return owners

    def ghost_fallback(self,owner,depth):
        """Prefer opaque interiors; retain ghost hits where the image is empty."""
        if not any(mesh.group.ghost for mesh in self.current):return owner,depth
        kinds=self.kind_buffer.copy()
        for mesh in self.current:
            mesh.id_actor.SetVisibility(mesh.group.ghost)
        self.window.Render();ghost_owner=self.read_owner();ghost_depth=self.read_depth()
        background=owner<0
        for index,identity in enumerate(self.ids):
            if identity.startswith('scene:room:'):
                background |= (owner==index)&(ghost_depth>depth+.001)
        fill=background&(ghost_owner>=0)
        owner[fill]=ghost_owner[fill];depth[fill]=ghost_depth[fill]
        kinds[fill]=self.kind_buffer[fill];self.kind_buffer=kinds
        return owner,depth

    def frame(self,project,camera,size,*,interactive=False,selected_ids=(),selected=None,
              explode=0,xray=False,xray_ids=None,quality='balanced'):
        if self.closed:raise GPUUnavailable('Kontekst OpenGL jest zamknięty.')
        start=time.perf_counter()
        ghosts=ghost_ids(project,xray,xray_ids);selection=set(selected_ids)
        if selected:selection.add(selected)
        changed=self.sync(project,interactive,selection,ghosts,explode,camera)
        prepared=time.perf_counter()
        sample_scale=({'draft':.45,'balanced':.55,'high':.75}.get(quality,.55)
                      if interactive else .75 if quality=='draft' else 1)
        self.quality=quality
        self.configure_camera(camera,size,sample_scale)
        self.configure_lights(project,camera)
        visible=visible_architecture(project,camera)
        self.material_pass(project,selection,ghosts,explode,camera,visible)
        self.quality_pass(quality,interactive);configured=time.perf_counter();self.window.Render()
        rendered=time.perf_counter();color=self.read_rgba();colored=time.perf_counter()
        beauty=time.perf_counter()
        owner=self.owner_pass(ghosts)
        # The ID pass defines picking and annotation occlusion. Ghost surfaces
        # deliberately expose interior ownership, so use its matching depth.
        depth=self.read_depth()
        owner,depth=self.ghost_fallback(owner,depth)
        if self.size!=self.output_size:
            # Same nearest pixel-centre mapping as Pillow and the CPU renderer.
            x=np.minimum(self.size[0]-1,np.floor((np.arange(self.output_size[0])+.5)*self.size[0]/self.output_size[0]).astype(np.int32))
            y=np.minimum(self.size[1]-1,np.floor((np.arange(self.output_size[1])+.5)*self.size[1]/self.output_size[1]).astype(np.int32))
            depth=depth[y[:,None],x[None,:]];owner=owner[y[:,None],x[None,:]]
            self.kind_buffer=self.kind_buffer[y[:,None],x[None,:]]
            color=np.asarray(Image.fromarray(color).resize(self.output_size,Image.Resampling.NEAREST))
        self.depth_buffer,self.id_buffer=depth,owner
        self.material_pass(project,selection,ghosts,explode,camera,visible)
        self.diagnostics=dict(engine='vtk-opengl',geometry_changed=changed,
            interactive=bool(interactive),quality=quality,sample_scale=self.sample_scale,actors=len(self.current),
            faces=sum(len(g.offsets)-1 for g in self.groups),
            geometry_ms=(prepared-start)*1000,beauty_ms=(beauty-prepared)*1000,
            configure_ms=(configured-prepared)*1000,render_ms=(rendered-configured)*1000,
            rgba_read_ms=(colored-rendered)*1000,depth_read_ms=(beauty-colored)*1000,
            owner_pass_ms=(time.perf_counter()-beauty)*1000,
            total_ms=(time.perf_counter()-start)*1000)
        self.drawn=cheap_drawn(self.context,self.groups,camera,size,selection,explode,visible)
        self.output_roi=(max(0,math.floor(self.roi[0]*self.output_size[0]/self.size[0])),
            max(0,math.floor(self.roi[1]*self.output_size[1]/self.size[1])),
            min(self.output_size[0]-1,math.ceil((self.roi[2]+1)*self.output_size[0]/self.size[0])),
            min(self.output_size[1]-1,math.ceil((self.roi[3]+1)*self.output_size[1]/self.size[1])))
        return color


def paint_gpu(canvas,faces=None,project=None,*,camera,size,interactive=False,
              selected_ids=(),selected=None,explode=0,xray=False,xray_ids=None,
              quality='balanced',**kwargs):
    """Draw GPU image and publish existing depth/ID buffers; controlled fallback."""
    if project is None:raise ValueError('paint_gpu wymaga projektu.')
    viewport=getattr(canvas,'_gpu_viewport',None)
    try:
        if viewport is None or viewport.closed:
            viewport=GPUViewport();canvas._gpu_viewport=viewport
            if hasattr(canvas,'bind') and not getattr(canvas,'_gpu_destroy_bound',False):
                def destroyed(event):
                    if event.widget is canvas:close_gpu(canvas)
                canvas.bind('<Destroy>',destroyed,add='+');canvas._gpu_destroy_bound=True
        rgba=viewport.frame(project,camera,size,interactive=interactive,
            selected_ids=selected_ids,selected=selected,explode=explode,xray=xray,
            xray_ids=xray_ids,quality=quality)
        canvas._depth_buffer=viewport.depth_buffer;canvas._id_buffer=viewport.id_buffer
        canvas._depth_ids=viewport.ids;canvas._depth_sample_scale=viewport.sample_scale
        canvas._render_diagnostics=dict(viewport.diagnostics)
        x0,y0,x1,y1=viewport.output_roi
        canvas._gpu_photo=ImageTk.PhotoImage(Image.fromarray(rgba[y0:y1+1,x0:x1+1]),master=canvas)
        canvas.create_image(x0,y0,anchor='nw',image=canvas._gpu_photo,tags='viewport.gpu_model')
        canvas._gpu_error=None
        return dict(ok=True,engine='vtk-opengl',drawn=viewport.drawn,diagnostics=viewport.diagnostics)
    except GPUUnavailable as exc:
        close_gpu(canvas)
        canvas._gpu_error=str(exc)
        return dict(ok=False,engine='cpu',error=str(exc))


def pick_world(canvas,x,y):
    viewport=getattr(canvas,'_gpu_viewport',None)
    if viewport is None or viewport.closed:return None
    px,py=int(math.floor(x)),int(math.floor(y));height,width=viewport.id_buffer.shape
    if not (0<=px<width and 0<=py<height):return None
    index=int(viewport.id_buffer[py,px]);depth=float(viewport.depth_buffer[py,px])
    if index<0 or not math.isfinite(depth):return None
    kind=('part','fitting','scene')[int(viewport.kind_buffer[py,px])]
    return dict(id=viewport.ids[index],kind=kind,precise_surface=kind=='part',
        world=unproject(viewport.camera,viewport.output_size,px+.5,py+.5,depth),depth=depth)


def close_gpu(canvas):
    viewport=getattr(canvas,'_gpu_viewport',None)
    try:
        if viewport is not None:viewport.close()
    finally:canvas._gpu_viewport=None
