"""Cached software viewport. Geometry is independent of the orbit camera.

Exact CAD faces remain available to manufacturing and exports. The settled
viewport tessellates the same drilled solid at a visual tolerance. Movement
uses coarse solids, stock boards and separate bought-component proxies;
decorative cuts, fine recess walls and drilling return when movement ends.
Only presentation data is cached. Parts, their CNC geometry and project data
are never modified here.
"""
import json
import math
import copy
from types import SimpleNamespace
from collections import OrderedDict
from functools import lru_cache


def frozen(value):
    if isinstance(value, dict):
        return tuple(sorted((k, frozen(v)) for k, v in value.items()))
    if isinstance(value, (tuple, list)):
        return tuple(frozen(v) for v in value)
    return value


def mesh_part(part):
    return bool(part.fillets or part.chamfers or part.machining
                or any(o['face'] not in ('top', 'bottom') for o in part.operations))


def geometry_key(part):
    if mesh_part(part):
        from cad_geometry import shape_key
        return ('cad', shape_key(part))
    return ('stock', tuple(tuple(p) for p in part.points()), part.t)


def pose_key(part):
    return (part.plane, part.x, part.y, part.z, part.rx, part.ry, part.rz,
            frozen(part.layout))


def indexed(polygons):
    vertices, lookup, indices = [], {}, []
    for polygon in polygons:
        face = []
        for point in polygon:
            point = tuple(point)
            pointkey = tuple(round(v,9) for v in point)
            index = lookup.get(pointkey)
            if index is None:
                index = len(vertices); lookup[pointkey] = index; vertices.append(point)
            face.append(index)
        indices.append(tuple(face))
    return tuple(vertices), tuple(indices)


@lru_cache(maxsize=192)
def local_geometry(key, interactive=False):
    if key[0] == 'stock':
        points, thickness = key[1:]
        bot = [(u, v, 0) for u, v in points]
        top = [(u, v, thickness) for u, v in points]
        polygons = [bot[::-1], top]
        polygons += [[bot[i], bot[(i+1)%len(bot)], top[(i+1)%len(bot)], top[i]]
                     for i in range(len(bot))]
        return indexed(polygons)
    from cad_geometry import cached_mesh
    if interactive is False:
        return indexed(cached_mesh(key[1]))
    # Copy without mesh: OCCT otherwise reuses a previously finer triangulation
    # and ignores the requested preview tolerance. The manufacturing solid and
    # cad_geometry.cached_mesh remain untouched.
    solid = display_solid(key[1]).copy(mesh=False)
    vertices, triangles = solid.tessellate(1.0 if interactive is True else .45,
                                         .65 if interactive is True else .35)
    vertices = tuple(v.toTuple() for v in vertices)
    return merge_coplanar(*indexed([[vertices[i] for i in t] for t in triangles]))


@lru_cache(maxsize=96)
def display_solid(key):
    from cad_geometry import cached_shape, cut_drills
    data = json.loads(key)
    return cut_drills(cached_shape(key), data['drilling'],data['thickness'],data['points'])


def merge_coplanar(vertices, indices):
    """Merge only a single simple boundary; multiple loops retain true holes.

    Native Canvas cannot express a polygon with holes. Filling the outer loop
    would silently conceal drilling, so every such surface retains triangles.
    """
    groups={}
    for face in indices:
        normal,_center=normal_center(vertices,face)
        offset=sum(normal[i]*vertices[face[0]][i] for i in range(3))
        key=tuple(round(v,6) for v in normal)+(round(offset,5),)
        groups.setdefault(key,[]).append(face)
    result=[]
    for faces in groups.values():
        if len(faces)<2:result.extend(faces);continue
        edges={}
        for face in faces:
            for a,b in zip(face,face[1:]+face[:1]):
                key=tuple(sorted((a,b)))
                if key in edges:edges.pop(key)
                else:edges[key]=(a,b)
        following={a:b for a,b in edges.values()}
        if len(following)!=len(edges) or not edges:
            result.extend(faces);continue
        first=next(iter(following)); boundary=[first]; cursor=following[first]
        while cursor!=first and cursor in following and cursor not in boundary:
            boundary.append(cursor);cursor=following[cursor]
        if cursor!=first or len(boundary)!=len(edges):
            result.extend(faces);continue
        # Discard straight boundary subdivisions left by planar triangulation.
        simple=[]
        for i,b in enumerate(boundary):
            a,c=boundary[i-1],boundary[(i+1)%len(boundary)]
            u=tuple(vertices[b][j]-vertices[a][j] for j in range(3))
            v=tuple(vertices[c][j]-vertices[b][j] for j in range(3))
            cross=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
            if sum(x*x for x in cross)>1e-14:simple.append(b)
        result.append(tuple(simple)) if len(simple)>=3 else result.extend(faces)
    return vertices,tuple(result)


def transform(part, vertices):
    from core import world
    origin = world(part, 0, 0, 0)
    axes = [tuple(a-b for a, b in zip(world(part, *axis), origin))
            for axis in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
    a, b, c = axes
    result = tuple((origin[0]+a[0]*u+b[0]*v+c[0]*w,
                    origin[1]+a[1]*u+b[1]*v+c[1]*w,
                    origin[2]+a[2]*u+b[2]*v+c[2]*w) for u, v, w in vertices)
    determinant = (a[0]*(b[1]*c[2]-b[2]*c[1])
                   -b[0]*(a[1]*c[2]-a[2]*c[1])+c[0]*(a[1]*b[2]-a[2]*b[1]))
    return result, 1 if determinant >= 0 else -1


def preview_front(part, color):
    """Keep the front silhouette during motion; restore decorative cuts after."""
    style=part.configurator_source.get('front_style',{}).get('style')
    if style not in ('frame','fluted'):return None
    if part.profile or part.fillets or part.chamfers:return None
    if any(c.get('type')=='cutout' or c.get('depth',0)>=part.t for c in part.machining):return None
    spans={'top':part.t,'bottom':part.t,'u0':part.u,'u1':part.u,'v0':part.v,'v1':part.v}
    if any(o.get('depth',0)>=spans[o['face']] and not small_mounting_drill(o)
           for o in part.operations):return None
    if any(h[3]>=part.t for h in part.holes):return None
    stock=local_geometry(('stock',tuple(tuple(p) for p in part.points()),part.t))
    vertices,sign=transform(part,stock[0])
    out=[Geometry(vertices,stock[1],owner=part.id,color=color,mesh=True,sign=sign)]
    return out


def preview_board(part,key,color):
    """Small blind drilling stays in production; movement uses the stock board.

    Exact contours and through cut-outs are kept. Standard rectilinear boards
    with hinge/connector bores or shallow LED channels do not need hundreds of
    subpixel drill-wall triangles while being moved around the room.
    """
    if part.profile or part.contour or part.fillets or part.chamfers:return None
    if any(c.get('type')=='cutout' or c.get('depth',0)>=part.t for c in part.machining):return None
    spans={'top':part.t,'bottom':part.t,'u0':part.u,'u1':part.u,'v0':part.v,'v1':part.v}
    if any(o.get('depth',0)>=spans[o['face']] and not small_mounting_drill(o)
           for o in part.operations):return None
    if any(h[3]>=part.t for h in part.holes):return None
    stock=('stock',tuple(tuple(p) for p in part.points()),part.t)
    return [owner_record(part,stock,color)]


def small_mounting_drill(operation):
    """Tiny generated screw holes need no curved drill walls during motion.

    User drilling, large openings and the full CAD solid are always preserved.
    Handle M4 through bores previously forced every decorated front back into
    its dense CAD preview even though the holes sit beneath bought handles.
    """
    return (operation.get('type')=='drill' and operation.get('config_auto') is True
            and 0 < operation.get('diameter',0) <= 8)


def surface_patches(polygon):
    """Give large walls local light samples without drawing a visible tile grid."""
    if len(polygon)!=4:return [polygon]
    a,b,c,d=polygon
    u=max(math.dist(a,b),math.dist(c,d));v=max(math.dist(a,d),math.dist(b,c))
    if max(u,v)<700:return [polygon]
    nu,nv=min(8,max(1,math.ceil(u/400))),min(8,max(1,math.ceil(v/400)))
    def point(s,t):
        return tuple(a[i]*(1-s)*(1-t)+b[i]*s*(1-t)+c[i]*s*t+d[i]*(1-s)*t for i in range(3))
    return [[point(x/nu,y/nv),point((x+1)/nu,y/nv),
             point((x+1)/nu,(y+1)/nv),point(x/nu,(y+1)/nv)]
            for y in range(nv) for x in range(nu)]


def geometry_faces(part, interactive=False):
    """Compatibility API for core.faces; return fresh mutable world polygons."""
    vertices, indices = local_geometry(geometry_key(part), interactive)
    points, _sign = transform(part, vertices)
    return [[points[i] for i in face] for face in indices]


def normal_center(vertices, face):
    # A valid contour can begin with three collinear points. Newell's normal
    # uses the complete planar boundary, so it does not lose such a surface.
    normal=[0.,0.,0.]
    for ai,bi in zip(face,face[1:]+face[:1]):
        a,b=vertices[ai],vertices[bi]
        normal[0]+=(a[1]-b[1])*(a[2]+b[2])
        normal[1]+=(a[2]-b[2])*(a[0]+b[0])
        normal[2]+=(a[0]-b[0])*(a[1]+b[1])
    length = math.sqrt(sum(v*v for v in normal)) or 1
    normal = tuple(v/length for v in normal)
    center = tuple(sum(vertices[j][i] for j in face)/len(face) for i in range(3))
    return normal, center


class Geometry:
    def __init__(self, vertices, indices, *, owner, color, mesh=False,
                 fitting=False, scene_object=False, emissive=False, sign=1):
        self.vertices, self.indices = vertices, indices
        self.owner, self.color, self.mesh = owner, color, mesh
        self.fitting, self.scene_object, self.emissive = fitting, scene_object, emissive
        self.sign = sign
        self.normals, self.centers = zip(*(normal_center(vertices, f) for f in indices)) if indices else ((), ())
        self.lit_colors = None


_fitting_geometry=OrderedDict()


def fitting_geometry(project,part_keys):
    """Room-object drags must not regenerate stationary furniture fittings."""
    key=(tuple((p[0],p[1],p[2],p[3]) for p in part_keys),frozen(project.hardware))
    records=_fitting_geometry.get(key)
    if records is None:
        from fitting_visuals import fitting_faces
        grouped={}
        for owner,face,color,sign in fitting_faces(project,with_metadata=True):
            grouped.setdefault((owner,color,sign),[]).append(face)
        records=[]
        for (owner,color,sign),polygons in grouped.items():
            vertices,indices=indexed(polygons)
            records.append(Geometry(vertices,indices,owner=owner,color=color,fitting=True,sign=sign))
        _fitting_geometry[key]=records
        while len(_fitting_geometry)>8:_fitting_geometry.popitem(last=False)
    else:_fitting_geometry.move_to_end(key)
    # Each context owns its colors. Only immutable geometry arrays are shared.
    return [copy.copy(record) for record in records]


@lru_cache(maxsize=256)
def world_record(key,pose,mode=False,lighting_patches=False):
    plane,x,y,z,rx,ry,rz,layout=pose
    proxy=SimpleNamespace(plane=plane,x=x,y=y,z=z,rx=rx,ry=ry,rz=rz,layout=dict(layout or ()))
    vertices,indices=local_geometry(key,mode)
    vertices,sign=transform(proxy,vertices)
    patched=False
    if lighting_patches:
        polygons=[]
        for face in indices:
            patches=surface_patches([vertices[j] for j in face])
            patched=patched or len(patches)>1;polygons.extend(patches)
        if patched:vertices,indices=indexed(polygons)
    return Geometry(vertices,indices,owner='',color='#000000',
                    mesh=key[0]=='cad' or patched,sign=sign)


def owner_record(part,key,color,mode=False,lighting_patches=False):
    record=copy.copy(world_record(key,pose_key(part),mode,lighting_patches))
    record.owner,record.color=part.id,color
    return record


def fitting_preview(record,part,emissive=False):
    """One oriented box per connected bought component, never per whole door."""
    if emissive or len(record.indices)<20:return [record]
    parent=list(range(len(record.vertices)))
    def find(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]];i=parent[i]
        return i
    for face in record.indices:
        root=find(face[0])
        for index in face[1:]:parent[find(index)]=root
    groups={}
    for i,point in enumerate(record.vertices):groups.setdefault(find(i),[]).append(point)
    from core import world
    origin=world(part,0,0,0)
    basis=[tuple(a-b for a,b in zip(world(part,*axis),origin))
           for axis in ((1,0,0),(0,1,0),(0,0,1))]
    polygons=[]
    for component in groups.values():
        local=[tuple(sum((point[j]-origin[j])*axis[j] for j in range(3))
                     for axis in basis) for point in component]
        lo=tuple(min(p[i] for p in local) for i in range(3))
        hi=tuple(max(p[i] for p in local) for i in range(3))
        if any(hi[i]-lo[i]<.0001 for i in range(3)):
            # Single flat faces belong to thin track/extrusion purchases.
            # Preserve them rather than manufacturing a fake thickness.
            selected={i for i,p in enumerate(record.vertices) if p in component}
            polygons.extend([[record.vertices[i] for i in face] for face in record.indices if all(i in selected for i in face)])
            continue
        vertices=[tuple(lo[i]+(hi[i]-lo[i])*bit[i] for i in range(3))
                  for bit in ((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1))]
        vertices,_sign=transform(part,vertices)
        polygons.extend([[vertices[i] for i in face] for face in
            ((0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7))])
    vertices,indices=indexed(polygons)
    _unused,sign=transform(part,())
    return [Geometry(vertices,indices,owner=record.owner,color=record.color,fitting=True,sign=sign)]


def rgb(color):
    return tuple(int(color[i:i+2], 16) for i in (1, 3, 5))


def color_hex(values):
    return '#' + ''.join(f'{min(255,max(0,round(v))):02x}' for v in values)


def lit_color(color, normal, center, lights, brightness=1, room='neutral', emissive=False):
    base = rgb(color)
    if emissive:
        return color
    ambient = {'light':.96, 'bright':.96, 'dark':.32}.get(room, .82)
    directional = abs(sum(normal[i]*(-.3,-.5,.81)[i] for i in range(3)))
    factor = min(1.3, max(.08, (ambient + .18*directional)*brightness))
    result = [v*factor for v in base]
    for light in lights:
        delta = tuple(light['position'][i]-center[i] for i in range(3))
        distance = math.sqrt(sum(v*v for v in delta))
        radius = light.get('radius', 650)
        if distance >= radius:
            continue
        direction = light.get('direction')
        if direction and sum(delta[i]*direction[i] for i in range(3)) > radius*.025:
            continue
        incidence = .28 + .72*abs(sum(normal[i]*delta[i] for i in range(3))/max(1,distance))
        power = min(.8, (1-distance/radius)**2*light.get('intensity',1)*incidence)
        lamp = rgb(light.get('color','#fff3bf'))
        # Diffuse light multiplies material reflectance. Adding white towards
        # 255 washed saturated MDF/paint into pastel even under a warm strip.
        result = [v + base[i]*power*(lamp[i]/255)*.7 for i,v in enumerate(result)]
    return color_hex(result)


def lit_colors(color,normals,centers,lights,brightness=1,room='neutral',emissive=False):
    """Shade a complete immutable geometry in NumPy, including multiple LEDs."""
    import numpy as np
    count=len(normals)
    if emissive:return (color,)*count
    if not count:return ()
    n=np.asarray(normals,dtype=np.float32);c=np.asarray(centers,dtype=np.float32)
    base=np.asarray(rgb(color),dtype=np.float32)
    ambient={'light':.96,'bright':.96,'dark':.32}.get(room,.82)
    directional=np.abs(n @ np.asarray((-.3,-.5,.81),dtype=np.float32))
    factor=np.clip((ambient+.18*directional)*brightness,.08,1.3)
    result=factor[:,None]*base
    for light in lights:
        delta=np.asarray(light['position'],dtype=np.float32)-c
        distance=np.sqrt(np.sum(delta*delta,axis=1));radius=light.get('radius',650)
        incidence=.28+.72*np.abs(np.sum(n*delta,axis=1)/np.maximum(1,distance))
        power=np.minimum(.8,(1-distance/radius)**2*light.get('intensity',1)*incidence)
        valid=distance<radius
        if light.get('direction') is not None:
            valid &= (delta @ np.asarray(light['direction'],dtype=np.float32))<=radius*.025
        power=np.where(valid,power,0)
        lamp=np.asarray(rgb(light.get('color','#fff3bf')),dtype=np.float32)
        result += base[None,:]*power[:,None]*(lamp[None,:]/255)*.7
    values=np.clip(np.rint(result),0,255).astype(np.uint8)
    return tuple('#%02x%02x%02x'%tuple(row) for row in values)


def furniture_lights(project, owners):
    from core import world
    lights=[]
    if not getattr(project,'scene',{}).get('led_enabled',True):
        return lights
    for h in project.hardware:
        p=owners.get(h.get('owner_id'))
        if h.get('kind')!='led_strip' or p is None:
            continue
        u,v=h.get('u',p.u/2),h.get('v',p.v/2)
        top=h.get('face')=='top'; z=p.t if top else 0
        local=h.get('local_center',(u,v,z))
        normal=h.get('local_normal',(0,0,1 if top else -1))
        tangent=h.get('local_tangent',(0,1,0) if h.get('axis')=='v' else (1,0,0))
        origin=world(p,*local)
        tip=world(p,*(local[i]+normal[i] for i in range(3)))
        direction=tuple(tip[i]-origin[i] for i in range(3))
        length=h.get('length',p.u)
        count=max(1,min(6,math.ceil(length/240)))
        for i in range(count):
            delta=length*((i+.5)/count-.5)
            position=world(p,*(local[j]+delta*tangent[j] for j in range(3)))
            lights.append(dict(position=position,direction=direction,
                color=h.get('color','#fff3bf'),radius=650,intensity=.65/count**.35,led=True))
    return lights


class SceneContext:
    def __init__(self, project, part_keys):
        from fitting_visuals import fitting_faces
        self.owners={}
        for p in project.parts:
            if not p.visible:continue
            snapshot=copy.copy(p)
            snapshot.layout=copy.deepcopy(p.layout)
            snapshot.machining=copy.deepcopy(p.machining)
            snapshot.configurator_source={'front_style':copy.deepcopy(p.configurator_source.get('front_style',{}))}
            self.owners[p.id]=snapshot
        self.groups={}; self.stock=[]; self.parts=[]; self.fittings=[]; self.room=[]; self.full={}
        materials={m.id:m.color for m in project.materials}
        for p,key in zip(project.parts,part_keys):
            if not p.visible: continue
            stockkey=('stock',tuple(tuple(q) for q in p.points()),p.t)
            stockvertices,_indices=local_geometry(stockkey)
            stock,_sign=transform(p,stockvertices)
            self.stock.extend(stock); self.groups.setdefault(p.group,[]).extend(stock)
            self.parts.append((p.id,key[1],materials.get(p.material,'#c5b395'),mesh_part(p)))
        for record in fitting_geometry(project,part_keys):
            if record.owner not in self.owners:continue
            self.fittings.append(record)
            self.groups.setdefault(self.owners[record.owner].group,[]).extend(record.vertices)
        try:
            from room_objects import room_world_faces, object_lights, object_vertices
            for f in room_world_faces(project):
                if f.get('architecture'):continue
                patches=surface_patches(f['world'])
                vertices,indices=indexed(patches)
                self.room.append(Geometry(vertices,indices,owner=f['id'],color=f['color'],
                    mesh=len(patches)>1,fitting=True,scene_object=True,emissive=f.get('emissive',False)))
            self.room_vertices=object_vertices(project)
            self.room_lights=object_lights(project)
        except ImportError:
            self.room_vertices=[]; self.room_lights=[]
        self.architecture=[];self.architecture_vertices=[]
        try:
            from room_architecture import world_faces,vertices as room_vertices
            for f in world_faces(project):
                points,indices=indexed(surface_patches(f['world']))
                record=Geometry(points,indices,owner=f['id'],color=f['color'],
                    mesh=True,fitting=True,scene_object=True)
                record.architecture=True
                self.architecture.append(record)
            # Camera "Fit" targets furniture and purchased objects. The room
            # inspector has a separate action to fit the enclosing walls.
            self.architecture_vertices=room_vertices(project)
        except ImportError:
            pass
        self.lights=furniture_lights(project,self.owners)+self.room_lights
        self.scene=dict(getattr(project,'scene',{})); self.preview={}
        leds={(h.get('owner_id'),h.get('color','#fff3bf')) for h in project.hardware if h.get('kind')=='led_strip'}
        self.fitting_previews=[preview for record in self.fittings
            for preview in fitting_preview(record,self.owners[record.owner],(record.owner,record.color) in leds)]

    def records(self, interactive=False):
        result=[]
        for owner,key,color,is_mesh in self.parts:
            if interactive:
                preview=self.preview.get(owner)
                if preview is None:
                    if is_mesh:
                        preview=preview_front(self.owners[owner],color) or preview_board(self.owners[owner],key,color)
                    else:
                        # Full stock can contain dozens of LED light patches.
                        # Never reuse that record during motion, regardless of
                        # which LOD was rendered first.
                        preview=[owner_record(self.owners[owner],key,color,False,False)]
                    if preview is None:
                        preview=[owner_record(self.owners[owner],key,color,True)]
                    self.preview[owner]=preview
                result.extend(preview)
            else:
                full=self.full.get(owner)
                if full is None:
                    full=owner_record(self.owners[owner],key,color,
                        'display' if is_mesh else False,bool(self.lights) and not is_mesh)
                    self.full[owner]=full
                    # Build the camera-independent preview once while the
                    # edited shape is first displayed, so the first drag never
                    # enters OCCT or waits for a new tessellation.
                    if is_mesh and owner not in self.preview:
                        proxy=preview_front(self.owners[owner],color) or preview_board(self.owners[owner],key,color)
                        if proxy is None:
                            proxy=[owner_record(self.owners[owner],key,color,True)]
                        self.preview[owner]=proxy
                result.append(full)
        return result+(self.fitting_previews if interactive else self.fittings)+self.room

    def face_colors(self, record):
        # An integer id can be recycled when cold preview is replaced by full
        # LOD. Cache on the actual object, so geometry and colors live together.
        if record.lit_colors is None:
            room=self.scene.get('lighting',self.scene.get('ambient','neutral'))
            brightness=self.scene.get('brightness',1)
            # Purchase metal retains its selected finish, while room walls and
            # furniture receive light. LED diffuser itself visibly switches off.
            if record.fitting and not record.scene_object:
                leds={(h.get('owner_id'),h.get('color','#fff3bf')) for h in getattr(self,'hardware',[])
                      if h.get('kind')=='led_strip'}
                base=record.color
                if (record.owner,base) in leds:
                    if not self.scene.get('led_enabled',True):base='#c7c6bb'
                    record.lit_colors=(base,)*len(record.indices)
                else:
                    record.lit_colors=tuple(color_hex(v*(.74+.26*abs(sum(n[i]*(-.3,-.5,.81)[i] for i in range(3))))
                        for v in rgb(base)) for n in record.normals)
            else:
                record.lit_colors=lit_colors(record.color,record.normals,record.centers,
                    self.lights,brightness,room,record.emissive)
        if not (len(record.indices)==len(record.normals)==len(record.centers)==len(record.lit_colors)):
            raise ValueError('Niespójna geometria widoku: liczba powierzchni, normalnych i kolorów musi być jednakowa.')
        return record.lit_colors


_scenes=OrderedDict()


class RenderedFaces(list):
    """List compatibility plus diagnostics which never enter project/CNC data."""
    pass


def clear_caches():
    _scenes.clear(); _fitting_geometry.clear();world_record.cache_clear(); local_geometry.cache_clear(); display_solid.cache_clear()


def prepare_scene(project, *, interactive=False):
    part_keys=tuple((p.id,geometry_key(p),pose_key(p),p.visible,p.group,p.material)
                    for p in project.parts)
    key=(part_keys,tuple((m.id,m.color) for m in project.materials),
         frozen(project.hardware),frozen(getattr(project,'scene',{})))
    context=_scenes.get(key)
    if context is None:
        context=SceneContext(project,part_keys)
        context.hardware=copy.deepcopy(project.hardware)
        _scenes[key]=context
        while len(_scenes)>6:_scenes.popitem(last=False)
    else:_scenes.move_to_end(key)
    return context


def render(project,camera,width,height,selected=None,explode=0,*,interactive=False,context=None):
    before=local_geometry.cache_info().misses
    context=context or prepare_scene(project,interactive=interactive)
    # A camera batch projects each shared vertex once, without per-vertex trig.
    ca,sa,cb,sb=camera._basis()
    cx,cy,cz=camera.center; scale=camera.scale
    ox,oy=width/2+camera.pan[0],height/2+camera.pan[1]
    out=[]
    records=context.records(interactive)
    if context.architecture:
        from room_architecture import world_faces
        visible={f['id'] for f in world_faces(project,camera)}
        records=records+[r for r in context.architecture if r.owner in visible]
    for record in records:
        owner=context.owners.get(record.owner)
        if explode and owner:
            from core import explosion_shift
            sx,sy,sz=explosion_shift(owner,camera,explode)
        else:sx=sy=sz=0
        projected=[]
        for x,y,z in record.vertices:
            x,y,z=x+sx-cx,y+sy-cy,z+sz-cz
            u=x*ca-y*sa; d=x*sa+y*ca
            projected.append((ox+u*scale,oy-(z*cb+d*sb)*scale,-d*cb+z*sb))
        colors=context.face_colors(record)
        for i,indices in enumerate(record.indices):
            if (record.mesh or record.fitting or interactive) and not record.scene_object and record.sign:
                n=record.normals[i]
                facing=(-n[0]*sa*cb-n[1]*ca*cb+n[2]*sb)*record.sign
                if facing < -1e-8:continue
            q=[projected[j] for j in indices]
            # Tiny mesh triangles are subpixel detail during movement. Removing
            # those reduces Tk pressure without hiding large pockets or grooves.
            if record.mesh and len(indices)==3:
                area=abs((q[1][0]-q[0][0])*(q[2][1]-q[0][1])
                         -(q[1][1]-q[0][1])*(q[2][0]-q[0][0]))
                if area < (.45 if interactive else .08):continue
            face=dict(points=[(u,v) for u,v,d in q],depth=sum(p[2] for p in q)/len(q),
                depths=tuple(p[2] for p in q),
                color=colors[i],id=record.owner,selected=record.owner==selected,mesh=record.mesh,
                normal=record.normals[i],center=record.centers[i],
                world=tuple(record.vertices[j] for j in indices))
            if record.fitting:face['fitting']=True
            if record.scene_object:face['scene_object']=True
            if record.emissive:face['emissive']=True
            if getattr(record,'architecture',False):face['architecture']=True
            out.append(face)
    result=RenderedFaces(sorted(out,key=lambda f:f['depth']))
    result.stats=dict(cold_geometry=local_geometry.cache_info().misses>before,
                      interactive=bool(interactive),faces=len(result))
    return result


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_viewport as _install_cpp
_install_cpp(globals())
