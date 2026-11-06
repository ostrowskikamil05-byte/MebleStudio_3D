"""Retained viewport geometry and camera math, independent of VTK/Tk.

Only presentation data is assembled here. CAD solids, materials, dimensions
and manufacturing records are read without mutation.
"""
from dataclasses import dataclass
import math
import numpy as np


@dataclass
class MeshGroup:
    key: tuple
    owner: str
    color: str
    records: tuple
    vertices: np.ndarray
    offsets: np.ndarray
    connectivity: np.ndarray
    fitting: bool = False
    scene_object: bool = False
    emissive: bool = False
    architecture: bool = False
    cell_colors: object = None
    cell_ids: object = None
    ghost: bool = False
    source_groups: tuple = ()
    opacity: float = 1
    visual_only: bool = False


def camera_basis(camera):
    ca,sa,cb,sb=camera._basis()
    return ((ca,-sa,0),(sa*sb,ca*sb,cb),(-sa*cb,-ca*cb,sb))


def camera_spec(camera,size,vertices=()):
    """Match Camera.project exactly, including orthographic pan and depth."""
    width,height=size;right,up,near=camera_basis(camera)
    center=tuple(camera.center)
    radius=max((math.dist(center,p) for p in vertices),default=1000)
    distance=max(10000,radius*2+2000)
    extent=max(1000,radius+1000)
    return dict(position=tuple(center[i]+distance*near[i] for i in range(3)),
        focal_point=center,up=up,parallel_scale=height/(2*max(camera.scale,1e-6)),
        window_center=(-2*camera.pan[0]/max(width,1),2*camera.pan[1]/max(height,1)),
        clipping=(distance-extent,distance+extent),distance=distance)


def z_to_depth(zbuffer,spec):
    near,far=spec['clipping']
    depth=np.asarray(spec['distance']-near-zbuffer*(far-near),dtype=np.float32)
    depth[zbuffer>=1-1e-7]=-np.inf
    return depth


def unproject(camera,size,x,y,depth):
    right,up,near=camera_basis(camera)
    u=(x-size[0]/2-camera.pan[0])/camera.scale
    v=-(y-size[1]/2-camera.pan[1])/camera.scale
    return tuple(camera.center[i]+u*right[i]+v*up[i]+depth*near[i] for i in range(3))


def record_key(record):
    # MeshGroup keeps records alive. Numeric identities cannot be recycled
    # while a retained actor still references the corresponding arrays.
    return (record.owner,id(record.vertices),id(record.indices),record.color,
        record.sign,record.fitting,record.scene_object,record.emissive,
        bool(getattr(record,'architecture',False)))


def mesh_groups(context,interactive=False):
    records=context.records(interactive)+context.architecture
    leds={(h.get('owner_id'),h.get('color','#fff3bf')) for h in getattr(context,'hardware',()) if h.get('kind')=='led_strip'}
    grouped={}
    for record in records:
        is_led=record.fitting and (record.owner,record.color) in leds
        color=record.color if not is_led or context.scene.get('led_enabled',True) else '#c7c6bb'
        flags=(record.owner,color,record.fitting,record.scene_object,
               record.emissive or is_led and context.scene.get('led_enabled',True),bool(getattr(record,'architecture',False)))
        grouped.setdefault(flags,[]).append(record)
    out=[]
    for flags,items in grouped.items():
        points=[];faces=[];base=0
        for record in items:
            points.extend(record.vertices)
            for face in record.indices:
                indices=face if record.sign>=0 else tuple(reversed(face))
                faces.append(tuple(i+base for i in indices))
            base+=len(record.vertices)
        sizes=np.fromiter((len(f) for f in faces),dtype=np.int64,count=len(faces))
        offsets=np.empty(len(faces)+1,dtype=np.int64);offsets[0]=0
        np.cumsum(sizes,out=offsets[1:])
        connectivity=np.fromiter((i for face in faces for i in face),dtype=np.int64,
                                 count=int(offsets[-1]))
        owner,color,fitting,scene_object,emissive,architecture=flags
        out.append(MeshGroup(tuple(record_key(r) for r in items),owner,color,tuple(items),
            np.asarray(points,dtype=np.float32).reshape((-1,3)),offsets,connectivity,
            fitting,scene_object,emissive,architecture))
    return out


def ghost_ids(project,xray=False,xray_ids=None):
    if not xray:return set()
    if xray_ids is not None:return set(xray_ids)
    from assembly import inferred_role
    return {p.id for p in project.parts if inferred_role(p) in ('door','drawer')}


def visible_architecture(project,camera):
    try:
        from room_architecture import world_faces
        return {f['id'] for f in world_faces(project,camera)}
    except ImportError:return set()


def material_style(project,group):
    """Material finish affects reflection; model material data is untouched."""
    part=next((p for p in project.parts if p.id==group.owner),None)
    material=next((m for m in project.materials if part and m.id==part.material),None)
    name=(getattr(material,'name','')+' '+getattr(material,'kind','')).lower()
    metal=group.fitting and not group.scene_object and not group.emissive
    if metal:return dict(roughness=.3,metallic=.65,specular=.65,power=70,wood=False)
    glossy=any(word in name for word in ('połysk','polysk','gloss','lakier'))
    wood=any(word in name for word in ('dąb','oak','orzech','sosna','buk','jesion','brzoza','drewno','sklejka','wenge','mahoń'))
    if group.architecture and group.owner.endswith(':floor'):
        r,g,b=(int(group.color[i:i+2],16) for i in (1,3,5));wood=r>b*1.2
    return dict(roughness=.22 if glossy else .65,metallic=0,
                specular=.4 if glossy else .16,power=50 if glossy else 22,wood=wood)


def batch_groups(groups,project,selection,ghosts,explode,camera):
    """Few draw calls, with independent owner/finish colors per polygon."""
    from core import explosion_shift
    owners={p.id:p for p in project.parts};buckets={}
    for code,group in enumerate(groups,1):
        style=material_style(project,group)
        ghost=group.owner in ghosts and not group.fitting
        bucket=(group.owner if group.architecture else '',group.fitting and not group.scene_object,
                group.emissive,ghost,style['roughness'],style['specular'],style['wood'],group.opacity,group.visual_only)
        buckets.setdefault(bucket,[]).append((code,group))
    out=[]
    for bucket,items in buckets.items():
        points=[];indices=[];sizes=[];colors=[];ids=[];records=[];base=0
        for code,group in items:
            p=owners.get(group.owner);shift=explosion_shift(p,camera,explode) if explode and p else (0,0,0)
            points.append(group.vertices+np.asarray(shift,dtype=np.float32))
            indices.append(group.connectivity+base);sizes.append(np.diff(group.offsets));base+=len(group.vertices)
            color=np.array([int(group.color[i:i+2],16) for i in (1,3,5)],dtype=np.float32)
            if group.owner in selection:color=color*.9+np.array((.3,.62,.78))*25.5
            colors.append(np.tile(np.rint(color).astype(np.uint8),(len(group.offsets)-1,1)))
            ids.append(np.tile((code&255,(code>>8)&255,(code>>16)&255),(len(group.offsets)-1,1)).astype(np.uint8))
            records.extend(group.records)
        offsets=np.r_[np.int64(0),np.cumsum(np.concatenate(sizes),dtype=np.int64)]
        representative=items[0][1]
        key=(tuple(g.key for _,g in items),tuple(sorted(selection)),bool(bucket[3]),explode,
             tuple(camera.center) if explode else ())
        out.append(MeshGroup(key,representative.owner if representative.architecture else '@batch',
            '#ffffff',tuple(records),np.concatenate(points),offsets,np.concatenate(indices),
            representative.fitting,representative.scene_object,representative.emissive,
            representative.architecture,np.concatenate(colors),np.concatenate(ids),bucket[3],
            tuple(g for _,g in items),representative.opacity,representative.visual_only))
    return out


def cheap_drawn(context,groups,camera,size,selected_ids=(),explode=0,visible=()):
    """Small annotation-compatible silhouettes; exact hits use GPU depth.

    This does not pretend to be the manufacturing mesh. It supplies contour
    points for dimension layout and representative faces for existing UI;
    the real visible world coordinate comes from the framebuffer.
    """
    from core import world,explosion_shift
    from viewport_render import RenderedFaces,normal_center
    selected_ids=set(selected_ids);faces=[]
    def add(owner,points,color,**flags):
        q=[camera.project(point,*size) for point in points]
        normal,center=normal_center(points,tuple(range(len(points))))
        faces.append(dict(id=owner,world=tuple(points),points=[p[:2] for p in q],
            depths=tuple(p[2] for p in q),depth=sum(p[2] for p in q)/len(q),
            normal=normal,center=center,color=color,selected=owner in selected_ids,
            mesh=False,**flags))
    colors={g.owner:g.color for g in groups if not g.fitting}
    for part in context.owners.values():
        shift=explosion_shift(part,camera,explode) if explode else (0,0,0)
        rings=[]
        for w in (0,part.t):
            ring=[world(part,u,v,w) for u,v in part.points()]
            rings.append([tuple(p[i]+shift[i] for i in range(3)) for p in ring])
        add(part.id,rings[0][::-1],colors.get(part.id,'#c5b395'))
        add(part.id,rings[1],colors.get(part.id,'#c5b395'))
    for group in groups:
        if not group.fitting:continue
        if group.architecture and group.owner not in visible:continue
        record=group.records[0]
        if not record.indices:continue
        points=[record.vertices[i] for i in record.indices[0]]
        add(group.owner,points,group.color,fitting=True,scene_object=group.scene_object,
            architecture=group.architecture)
    result=RenderedFaces(faces);result.stats=dict(engine='vtk-opengl',
        faces=sum(len(g.offsets)-1 for g in groups),representative=True)
    return result
