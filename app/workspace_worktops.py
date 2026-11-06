"""One real, persistent worktop over connected cabinet footprints.

Polygon union and fitting run offline without an additional CAD package. A
linked top keeps its previous geometry when a support is missing, separated or
manually edited; its status records why an explicit fit cannot be completed.
"""
import copy
import hashlib
import json
import math
from core import Part,finite,world,layout_point,validate_polygon


def _signed(poly):
    return sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(poly,poly[1:]+poly[:1]))/2


def _ccw(poly):
    return poly if _signed(poly)>0 else list(reversed(poly))


def _cross(a,b):return a[0]*b[1]-a[1]*b[0]


def _point_segment(p,a,b):
    v=(b[0]-a[0],b[1]-a[1]);length=v[0]**2+v[1]**2
    t=max(0,min(1,((p[0]-a[0])*v[0]+(p[1]-a[1])*v[1])/length)) if length else 0
    return math.dist(p,(a[0]+t*v[0],a[1]+t*v[1]))


def _inside(point,poly,strict=False):
    if any(_point_segment(point,a,b)<1e-6 for a,b in zip(poly,poly[1:]+poly[:1])):return not strict
    from core import inside
    return inside(point,poly)


def polygons_overlap(a,b):
    """Positive area intersection, excluding a touching edge or a vertex."""
    for poly,other in ((a,b),(b,a)):
        ccw=_ccw(poly)
        for p,q in zip(ccw,ccw[1:]+ccw[:1]):
            if _inside(p,other,True):return True
            length=math.dist(p,q)
            if length:
                probe=((p[0]+q[0])/2-(q[1]-p[1])*1e-4/length,(p[1]+q[1])/2+(q[0]-p[0])*1e-4/length)
                if _inside(probe,other,True):return True
        for p,q in zip(poly,poly[1:]+poly[:1]):
            for r,s in zip(other,other[1:]+other[:1]):
                v=(q[0]-p[0],q[1]-p[1]);w=(s[0]-r[0],s[1]-r[1]);den=_cross(v,w)
                if abs(den)<1e-9:continue
                rp=(r[0]-p[0],r[1]-p[1]);t,u=_cross(rp,w)/den,_cross(rp,v)/den
                if 1e-7<t<1-1e-7 and 1e-7<u<1-1e-7:return True
    return False


def _distance(a,b):
    if polygons_overlap(a,b):return 0
    return min(_point_segment(p,r,s) for poly,other in ((a,b),(b,a)) for p in poly for r,s in zip(other,other[1:]+other[:1]))


def _offset(poly,amount):
    if not amount:return list(poly)
    poly=_ccw(poly);lines=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
        lines.append(((a[0]+dy*amount/length,a[1]-dx*amount/length),(dx,dy)))
    result=[]
    for index,(b,v) in enumerate(lines):
        a,u=lines[index-1];den=_cross(u,v)
        if abs(den)<1e-9:result.append(b);continue
        delta=(b[0]-a[0],b[1]-a[1]);t=_cross(delta,v)/den
        result.append((a[0]+t*u[0],a[1]+t*u[1]))
    return result


def _union(polygons):
    """Trace the exposed, split boundary of connected simple polygons."""
    polygons=[_ccw(poly) for poly in polygons];edges=set()
    for index,poly in enumerate(polygons):
        for a,b in zip(poly,poly[1:]+poly[:1]):
            v=(b[0]-a[0],b[1]-a[1]);length=v[0]**2+v[1]**2;cuts=[0.,1.]
            for other_index,other in enumerate(polygons):
                if other_index==index:continue
                for p,q in zip(other,other[1:]+other[:1]):
                    w=(q[0]-p[0],q[1]-p[1]);pa=(p[0]-a[0],p[1]-a[1]);den=_cross(v,w)
                    if abs(den)>1e-9:
                        t,u=_cross(pa,w)/den,_cross(pa,v)/den
                        if -1e-7<=t<=1+1e-7 and -1e-7<=u<=1+1e-7:cuts.append(max(0,min(1,t)))
                    elif abs(_cross(pa,v))<1e-5:
                        for point in (p,q):
                            t=((point[0]-a[0])*v[0]+(point[1]-a[1])*v[1])/length
                            if 0<t<1:cuts.append(t)
            cuts=sorted(set(round(t,10) for t in cuts))
            for start,end in zip(cuts,cuts[1:]):
                if end-start<1e-8:continue
                midpoint=(a[0]+(start+end)*v[0]/2,a[1]+(start+end)*v[1]/2)
                if any(i!=index and _inside(midpoint,other,True) for i,other in enumerate(polygons)):continue
                p=tuple(round(a[i]+start*v[i],6) for i in range(2));q=tuple(round(a[i]+end*v[i],6) for i in range(2))
                if p!=q:edges.add((p,q))
    edges={edge for edge in edges if (edge[1],edge[0]) not in edges}
    if not edges:raise ValueError('Nie można wyznaczyć obrysu wspólnego blatu.')
    loops=[]
    while edges:
        first=min(edges);edges.remove(first);loop=[first[0]];current=first[1]
        while current!=loop[0]:
            loop.append(current);following=[edge for edge in edges if edge[0]==current]
            if len(following)!=1:raise ValueError('Korpusy łączą się tylko punktem lub tworzą niejednoznaczny obrys blatu.')
            edge=following[0];edges.remove(edge);current=edge[1]
        # Remove collinear split points; meaningful corners remain stable.
        changed=True
        while changed and len(loop)>3:
            changed=False
            for i,p in enumerate(loop):
                a,b=loop[i-1],loop[(i+1)%len(loop)]
                if abs(_cross((p[0]-a[0],p[1]-a[1]),(b[0]-p[0],b[1]-p[1])))<1e-5:
                    loop.pop(i);changed=True;break
        loops.append(loop)
    if len(loops)!=1:raise ValueError('Wspólny blat wymaga jednego połączonego obrysu bez zamkniętego otworu. Rozdziel go na kilka blatów.')
    return _ccw(loops[0])


def _signature(part):
    data={key:copy.deepcopy(getattr(part,key)) for key in ('u','v','t','plane','x','y','z','rx','ry','rz','contour','profile','layout')}
    return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()


def _body(project,group):
    from furniture_layout import group_layout
    from furniture_configurator import read_config
    from assembly import frame
    parts=[p for p in project.parts if p.group==group]
    if not parts:raise ValueError('Usunięto szafkę pod wspólnym blatem.')
    layout=group_layout(project,group)
    if abs(math.remainder(layout['yaw'],90))>1e-7:raise ValueError('Wspólny blat wymaga obrotów korpusów o wielokrotność 90°.')
    corner=next((p for p in parts if p.assembly.get('corner_source') and p.role=='bottom'),None)
    if corner:
        cfg=corner.assembly['corner_source']['config'];polygon=[world(corner,u,v,0)[:2] for u,v in corner.points()]
        z=world(corner,0,0,cfg['height']-cfg.get('leg_height',0))[2];anchor=corner.id
    else:
        cfg=read_config(project,group)
        if cfg:
            lo=cfg['origin'][:];lo[2]+=cfg['leg_height'];hi=[lo[0]+cfg['width'],lo[1]+cfg['depth'],lo[2]+cfg['height']-cfg['leg_height']-(cfg['worktop_thickness'] if cfg['worktop'] else 0)]
        else:
            f=frame(project,group);lo,hi=f['lower'],f['upper']
        ref=parts[0];polygon=[layout_point(ref,(x,y,lo[2]))[:2] for x,y in ((lo[0],lo[1]),(hi[0],lo[1]),(hi[0],hi[1]),(lo[0],hi[1]))]
        z=layout_point(ref,(lo[0],lo[1],hi[2]))[2]
        anchor=next((p.id for p in parts if p.configurator_source.get('key')=='carcass.left'),parts[0].id)
    return _ccw(polygon),z,anchor


def _geometry(project,groups,config):
    bodies=[_body(project,group) for group in groups];heights=[body[1] for body in bodies]
    if max(heights)-min(heights)>.1:raise ValueError('Szafki pod wspólnym blatem muszą mieć tę samą wysokość górnej powierzchni.')
    polygons=[body[0] for body in bodies]
    if any(polygons_overlap(polygons[i],polygons[j]) for i in range(len(polygons)) for j in range(i)):
        raise ValueError('Korpusy pod wspólnym blatem nachodzą na siebie.')
    visited={0}
    while True:
        grown=visited|{i for i in range(len(polygons)) if any(_distance(polygons[i],polygons[j])<=.5 for j in visited)}
        if grown==visited:break
        visited=grown
    if len(visited)!=len(polygons):raise ValueError('Dosuń szafki do siebie magnesem. Wspólny blat wymaga sąsiadujących korpusów.')
    polygon=_union([_offset(poly,config['overhang']) for poly in polygons]);x=min(p[0] for p in polygon);y=min(p[1] for p in polygon)
    contour=validate_polygon([(round(p[0]-x,6),round(p[1]-y,6)) for p in polygon])
    if len(contour)>128:raise ValueError('Obrys wspólnego blatu ma zbyt wiele punktów. Zmniejsz liczbę modułów lub podziel blat.')
    return contour,(x,y,sum(heights)/len(heights)),[body[2] for body in bodies]


def _source(project,group):
    return next((p for p in project.parts if p.group==group and p.assembly.get('shared_worktop')),None)


def read_shared_worktop(project,group):
    part=_source(project,group)
    return copy.deepcopy(part.assembly['shared_worktop']) if part else None


def install_shared_worktop(project,groups,thickness=38,overhang=20,material=None,name='Wspólny blat',replace_individual=True):
    names=list(dict.fromkeys(groups));config=dict(thickness=finite(thickness,1,100),overhang=finite(overhang,0,300))
    if not names:raise ValueError('Zaznacz szafki pod wspólny blat.')
    result=copy.deepcopy(project);material=material or result.materials[0].id
    if material not in {m.id for m in result.materials}:raise ValueError('Wybierz materiał blatu.')
    if not isinstance(name,str) or not name.strip():raise ValueError('Wpisz nazwę blatu.')
    chosen_ids={p.id for p in result.parts if p.group in names}
    if any(chosen_ids.intersection(p.assembly.get('shared_worktop',{}).get('anchors',[])) for p in result.parts):
        raise ValueError('Wybrane szafki mają już wspólny blat. Dopasuj istniejący albo usuń go przed utworzeniem nowego.')
    from furniture_configurator import read_config,configure,infer_config
    from corner_furniture import read_corner_config,create_corner
    for group in names:
        cfg=read_config(result,group);corner=read_corner_config(result,group)
        if cfg is None and corner is None:
            try:cfg=infer_config(result,group)
            except ValueError:pass
        individual=[p for p in result.parts if p.group==group and p.role=='worktop']
        if individual:
            if not replace_individual:raise ValueError('Szafka ma własny blat. Wyłącz go albo wybierz zastąpienie blatów jednym wspólnym.')
            if not cfg:raise ValueError('Szafka zawiera ręczny blat. Usuń go świadomie przed dodaniem wspólnego.')
            cfg['height']-=cfg['worktop_thickness'];cfg['worktop']=False
        if cfg and (individual or cfg.get('top_mode','panel')!='rails' or read_config(result,group) is None):
            cfg['top_mode']='rails'
            result,_=configure(result,cfg,group)
        elif corner and corner['top_mode']!='rails':
            corner['top_mode']='rails';result,_=create_corner(result,corner,group)
        elif cfg is None and corner is None:
            from assembly import frame,inferred_role
            f=frame(result,group);depth=f['upper'][1]-f['lower'][1]
            if any(p.group==group and inferred_role(p)=='top' and p.plane=='XY' and p.v>depth/2 for p in result.parts):
                raise ValueError('Ręczna góra korpusu wymaga świadomej zamiany na listwy. Kreator wspólnego blatu jej nie usuwa.')
    contour,origin,anchors=_geometry(result,names,config)
    occupied={p.group for p in result.parts};base=name.strip();group=base;i=2
    while group in occupied:group=f'{base} ({i})';i+=1
    part=Part(name=base,group=group,u=max(p[0] for p in contour),v=max(p[1] for p in contour),t=config['thickness'],contour=contour,
              x=origin[0],y=origin[1],z=origin[2],material=material,role='worktop',contour_edge=True)
    part.validate();part.assembly['shared_worktop']=dict(schema=1,anchors=anchors,config=config,signature=_signature(part),status='ready',reason='')
    result.parts.append(part);result.validate();return result,group


def _fit(project,part):
    source=part.assembly['shared_worktop']
    if source.get('signature')!=_signature(part):raise ValueError('Geometrię lub pozycję wspólnego blatu zmieniono ręcznie. Automatyczne dopasowanie jej nie nadpisze.')
    by_id={p.id:p for p in project.parts};anchors=source['anchors']
    if any(anchor not in by_id for anchor in anchors):raise ValueError('Usunięto szafkę pod wspólnym blatem. Utwórz blat dla pozostałych szafek.')
    names=list(dict.fromkeys(by_id[anchor].group for anchor in anchors));contour,origin,new_anchors=_geometry(project,names,source['config'])
    part.contour=contour;part.u=max(p[0] for p in contour);part.v=max(p[1] for p in contour);part.x,part.y,part.z=origin
    part.validate();source.update(anchors=new_anchors,status='ready',reason='',signature=_signature(part))


def fit_shared_worktop(project,group):
    result=copy.deepcopy(project);part=_source(result,group)
    if part is None:raise ValueError('Wybierz wspólny blat.')
    _fit(result,part);result.validate();return result


def refresh_shared_worktops(project,changed_groups=None,validate=True):
    linked=[p for p in project.parts if p.assembly.get('shared_worktop')]
    if not linked:return project
    result=copy.deepcopy(project);by_id={p.id:p for p in result.parts}
    for old in linked:
        part=by_id[old.id];source=part.assembly['shared_worktop']
        owners={by_id[anchor].group for anchor in source.get('anchors',[]) if anchor in by_id}
        if changed_groups is not None and part.group not in changed_groups and owners.isdisjoint(changed_groups) and all(anchor in by_id for anchor in source.get('anchors',[])):continue
        candidate=copy.deepcopy(part)
        try:_fit(result,candidate)
        except (ValueError,KeyError,TypeError) as error:source.update(status='needs_fit',reason=str(error));continue
        result.parts[result.parts.index(part)]=candidate
    if validate:result.validate()
    return result


def _install_native_geometry():
    from functools import wraps
    import native_model
    if not native_model.available():
        return
    info = native_model.call('native', 'info')
    if info.get('layout_api_version') != 1 or info.get('workspace_geometry_api_version') != 1:
        raise RuntimeError('Biblioteka C++ nie obsługuje układu mebli. Użyj kompletnej paczki bieżącego etapu migracji.')
    original = polygons_overlap
    @wraps(original)
    def overlap(a, b):
        # Python's mixed int/float sum has a distinct accumulator transition.
        # Preserve its winding at the adapter boundary; all intersection tests
        # and the complete native layout loop remain inside C++.
        return native_model.call('workspace_geometry', 'polygons_overlap', a, b,
                                 a_ccw=_signed(a) > 0, b_ccw=_signed(b) > 0)
    overlap._native_cpp = True
    globals()['polygons_overlap'] = overlap


_install_native_geometry()

# Solver calls return geometry only. Source signatures, IDs, manual-feature
# guards, project copies and exact CAD publication retain their existing code.
from native_worktops import install as _install_native_worktops
_install_native_worktops(globals())
