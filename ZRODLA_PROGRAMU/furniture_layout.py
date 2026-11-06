"""Rigid furniture placement and reversible, face-to-face magnetic alignment.

Construction coordinates remain unchanged. A cabinet can still regenerate its
drawers, fittings and machining after placement; production blanks stay local.
"""
import copy
import math
from core import finite, rotate, world, local_world

DIRECTIONS={'Prawo':'x+','Lewo':'x-','Tył':'y+','Przód':'y-','Góra':'z+','Dół':'z-'}


def groups(project):
    return list(dict.fromkeys(p.group for p in project.parts))


def _parts(project,group):
    found=[p for p in project.parts if p.group==group]
    if not found:raise ValueError('Wybierz istniejący mebel.')
    return found


def group_layout(project,group):
    parts=_parts(project,group)
    layout={key:parts[0].layout.get(key,0) for key in ('x','y','z','yaw')}
    if any(abs(p.layout.get(key,0)-value)>1e-7 for p in parts for key,value in layout.items()):
        raise ValueError('Elementy mebla mają różne ustawienia zestawu. Przypisz je do osobnych mebli lub wyrównaj ich ustawienie.')
    return layout


def _bounds(project,group,transform):
    vertices=[transform(p,u,v,t) for p in _parts(project,group) for u,v in p.points() for t in (0,p.t)]
    from configurator_accessories import accessory_vertices
    vertices.extend(accessory_vertices(project,group,local=transform is local_world))
    return tuple(min(p[i] for p in vertices) for i in range(3)),tuple(max(p[i] for p in vertices) for i in range(3))


def group_bounds(project,group):
    """Nominal closed cabinet envelope in the room, including fronts and backs."""
    return _bounds(project,group,world)


def local_group_bounds(project,group):
    return _bounds(project,group,local_world)


def _placed(project,group,layout,validate=True):
    result=copy.deepcopy(project)
    for p in _parts(result,group):p.layout=copy.deepcopy(layout)
    from workspace_worktops import refresh_shared_worktops
    result=refresh_shared_worktops(result,changed_groups={group},validate=False)
    if validate:result.validate()
    return result


def move_group(project,group,dx=0,dy=0,dz=0,*,validate=True):
    """Move one cabinet in room axes, atomically and without altering blanks."""
    layout=group_layout(project,group)
    for axis,delta in zip(('x','y','z'),(dx,dy,dz)):
        layout[axis]=finite(layout[axis]+finite(delta))
    return _placed(project,group,layout,validate)


def rotate_group(project,group,angle,pivot=None):
    """Rotate around room Z; default pivot is the cabinet's current centre."""
    layout=group_layout(project,group);angle=finite(angle)
    lo,hi=group_bounds(project,group)
    pivot=tuple((lo[i]+hi[i])/2 for i in range(3)) if pivot is None else tuple(pivot)
    if len(pivot)!=3:raise ValueError('Punkt obrotu wymaga współrzędnych X, Y, Z.')
    pivot=tuple(finite(v) for v in pivot)
    shift=rotate(tuple(layout['xyz'[i]]-pivot[i] for i in range(3)),0,0,angle)
    for i,axis in enumerate('xyz'):layout[axis]=finite(pivot[i]+shift[i])
    layout['yaw']=(layout['yaw']+angle+180)%360-180
    if abs(layout['yaw'])<1e-9:layout['yaw']=0
    return _placed(project,group,layout)


def _orthogonal(project,group):
    return abs(math.remainder(group_layout(project,group)['yaw'],90))<1e-7


def _overlap(a,b,axis):
    return min(a[1][axis],b[1][axis])-max(a[0][axis],b[0][axis])


def _intersects(a,b,tolerance=.01):
    return all(_overlap(a,b,i)>tolerance for i in range(3))


def _footprint(project,group,box=None):
    # Irregular tops and corner carcasses occupy their contour, including its
    # empty notch. Ordinary cabinets retain a full closed envelope.
    parts=_parts(project,group)
    irregular=next((p for p in parts if p.assembly.get('shared_worktop')),None)
    if irregular is None:irregular=next((p for p in parts if p.role=='bottom' and p.assembly.get('corner_source')),None)
    if irregular:return [world(irregular,u,v,0)[:2] for u,v in irregular.points()]
    lo,hi=box or group_bounds(project,group)
    return [(lo[0],lo[1]),(hi[0],lo[1]),(hi[0],hi[1]),(lo[0],hi[1])]


def _footprint_collision(a,pa,b,pb):
    from workspace_worktops import polygons_overlap
    return _intersects(a,b) and polygons_overlap(pa,pb)


def _face_contact(pa,pb,direction,gap=0):
    from workspace_worktops import polygons_overlap
    if direction[0]=='z':return polygons_overlap(pa,pb)
    axis='xy'.index(direction[0]);delta=[0.,0.];delta[axis]=gap+(-.02 if direction[1]=='+' else .02)
    return polygons_overlap([(p[0]+delta[0],p[1]+delta[1]) for p in pa],pb)


def _flat_faces(poly):
    from workspace_worktops import _ccw
    poly=_ccw(poly);result=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        dx,dy=b[0]-a[0],b[1]-a[1]
        if abs(dx)<1e-6 and abs(dy)>1e-6:result.append((0,'+' if dy>0 else '-',a[0],min(a[1],b[1]),max(a[1],b[1])))
        elif abs(dy)<1e-6 and abs(dx)>1e-6:result.append((1,'-' if dx>0 else '+',a[1],min(a[0],b[0]),max(a[0],b[0])))
    return result


def _check_collision(project,group,box):
    source=group_bounds(project,group);delta=[box[0][i]-source[0][i] for i in range(2)]
    polygon=[(p[0]+delta[0],p[1]+delta[1]) for p in _footprint(project,group,source)]
    for other in groups(project):
        other_box=group_bounds(project,other)
        if other!=group and _footprint_collision(box,polygon,other_box,_footprint(project,other,other_box)):
            raise ValueError(f'Ustawienie nachodzi na mebel „{other}”. Zmień stronę, wysokość lub odstęp.')


def _translated(box,delta):
    return tuple(tuple(point[i]+delta[i] for i in range(3)) for point in box)


def snap_group(project,group,target_group,direction='x+',align='start',gap=0):
    """Place a cabinet against a target face; sides refer to room axes.

    start/centre/end aligns the remaining axes; a dict can specify per-axis
    alignment. Only quarter-turn orientations have room-aligned flat faces.
    """
    if group==target_group:raise ValueError('Wybierz drugi mebel do połączenia.')
    if direction not in DIRECTIONS.values():raise ValueError('Strona połączenia: x+, x-, y+, y-, z+ albo z-.')
    if not _orthogonal(project,group) or not _orthogonal(project,target_group):
        raise ValueError('Magnes ścian wymaga obrotów mebli o wielokrotność 90°. Dla innego kąta ustaw pozycję ręcznie.')
    gap=finite(gap,0,10000);source=group_bounds(project,group);target=group_bounds(project,target_group)
    axis='xyz'.index(direction[0]);positive=direction[1]=='+'
    delta=[0.,0.,0.]
    delta[axis]=target[1 if positive else 0][axis]+(gap if positive else -gap)-source[0 if positive else 1][axis]
    for other_axis in range(3):
        if other_axis==axis:continue
        mode=align.get('xyz'[other_axis],'start') if isinstance(align,dict) else align
        if mode=='center':delta[other_axis]=sum(point[other_axis] for point in target)/2-sum(point[other_axis] for point in source)/2
        elif mode=='start':delta[other_axis]=target[0][other_axis]-source[0][other_axis]
        elif mode=='end':delta[other_axis]=target[1][other_axis]-source[1][other_axis]
        elif mode=='keep':pass
        else:raise ValueError('Wyrównanie: start, center, end albo keep.')
    moved=_translated(source,delta)
    _check_collision(project,group,moved)
    return move_group(project,group,*delta)


def nearest_snap(project,group,tolerance=80,align=True,*,validate=True):
    """Snap a near face and optionally its nearby edges; never move far away.

    Returns (candidate, information) or an unchanged copy with None. Fittings
    and opening poses are intentionally excluded from magnetic construction.
    """
    tolerance=finite(tolerance,0,1000);source=group_bounds(project,group)
    if not _orthogonal(project,group):return copy.deepcopy(project),None
    envelopes={name:group_bounds(project,name) for name in groups(project)}
    orthogonal={name:_orthogonal(project,name) for name in envelopes}
    footprints={name:_footprint(project,name,box) for name,box in envelopes.items()}
    flat_faces={name:_flat_faces(poly) for name,poly in footprints.items()}
    candidates=[]
    for other,target in envelopes.items():
        if other==group or not orthogonal[other]:continue
        proposals=[]
        for axis,side,coordinate,start,end in flat_faces[other]:
            for source_axis,source_side,source_coordinate,source_start,source_end in flat_faces[group]:
                if axis!=source_axis or side==source_side:continue
                if min(end,source_end)-max(start,source_start)<-tolerance:continue
                proposals.append(('xyz'[axis]+side,coordinate-source_coordinate,(1-axis,start,end,source_start,source_end)))
        for direction in ('z+','z-'):
            proposals.append((direction,target[1 if direction[1]=='+' else 0][2]-source[0 if direction[1]=='+' else 1][2],None))
        for direction,distance,face_range in proposals:
            axis='xyz'.index(direction[0]);positive=direction[1]=='+'
            if abs(distance)>tolerance+1e-8:continue
            tangent=[i for i in range(3) if i!=axis]
            delta=[0.,0.,0.];delta[axis]=distance
            if any(_overlap(source,target,i)<-tolerance for i in tangent):continue
            if align:
                for i in tangent:
                    if face_range and face_range[0]==i:
                        _,start,end,source_start,source_end=face_range
                        possibilities=[start-source_start,end-source_end,(start+end-source_start-source_end)/2]
                    else:
                        possibilities=[target[end][i]-source[end][i] for end in (0,1)]
                        possibilities.append(sum(point[i] for point in target)/2-sum(point[i] for point in source)/2)
                    correction=min(possibilities,key=abs)
                    if abs(correction)<=tolerance:delta[i]=correction
            moved=_translated(source,delta)
            polygon=[(p[0]+delta[0],p[1]+delta[1]) for p in footprints[group]]
            if any(_overlap(moved,target,i)<=.01 for i in tangent):continue
            if not _face_contact(polygon,footprints[other],direction):continue
            if any(other_group!=group and _footprint_collision(moved,polygon,box,footprints[other_group]) for other_group,box in envelopes.items()):continue
            travel=math.sqrt(sum(v*v for v in delta))
            info=dict(group=group,target_group=other,direction=direction,delta=tuple(delta),distance=travel)
            candidates.append((travel,other,direction,delta,info))
    if not candidates:return copy.deepcopy(project),None
    _,_,_,delta,info=min(candidates,key=lambda entry:(entry[0],entry[1],entry[2]))
    return move_group(project,group,*delta,validate=validate),info


def contacts(project,group=None,tolerance=.1):
    """Current geometric contacts; separation or movement releases them freely."""
    tolerance=finite(tolerance,0,1000);names=groups(project);result=[]
    envelopes={name:group_bounds(project,name) for name in names}
    orthogonal={name:_orthogonal(project,name) for name in names}
    footprints={name:_footprint(project,name,envelopes[name]) for name in names}
    flat_faces={name:_flat_faces(poly) for name,poly in footprints.items()}
    for index,first in enumerate(names):
        for second in names[index+1:]:
            if group is not None and group not in (first,second):continue
            if not orthogonal[first] or not orthogonal[second]:continue
            a,b=envelopes[first],envelopes[second]
            proposals=[]
            for axis,side,coordinate,start,end in flat_faces[second]:
                for source_axis,source_side,source_coordinate,source_start,source_end in flat_faces[first]:
                    if axis!=source_axis or side==source_side:continue
                    if min(end,source_end)-max(start,source_start)<=.01:continue
                    proposals.append(('xyz'[axis]+side,coordinate-source_coordinate))
            for direction in ('z+','z-'):
                proposals.append((direction,b[1 if direction[1]=='+' else 0][2]-a[0 if direction[1]=='+' else 1][2]))
            seen=set()
            for direction,gap in proposals:
                axis='xyz'.index(direction[0]);positive=direction[1]=='+'
                if direction not in seen and abs(gap)<=tolerance and all(_overlap(a,b,i)>.01 for i in range(3) if i!=axis) and _face_contact(footprints[first],footprints[second],direction,gap):
                    result.append(dict(group=first,target_group=second,direction=direction,gap=abs(gap)))
                    seen.add(direction)
    return result


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_construction as _install_cpp
_install_cpp(globals(), 'furniture_layout')

# One native scene call owns each complete placement operation; publication
# still uses _placed so linked worktops and exact CAD validation remain intact.
from native_layout import install as _install_native_layout
_install_native_layout(globals())
