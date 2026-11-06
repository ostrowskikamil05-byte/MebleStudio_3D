"""Optional OpenCascade CAD kernel, exact edge fillets and STEP solids."""
from functools import lru_cache
import json
import math

def kernel():
    try:
        import cadquery as cq
        return cq
    except ImportError as e:
        raise ValueError('Brak modułu CAD. Wersję Windows rozpakuj w całości razem z folderem _internal. Źródła uruchamiaj w środowisku z wymaganiami requirements-cad.txt.') from e

class EdgeAnchor(tuple):
    """Coordinate compatible anchor carrying an exact curve descriptor."""
    def __new__(cls,point,geom_type,samples,length):
        item=super().__new__(cls,point)
        item.geom_type=geom_type;item.samples=samples;item.length=length
        return item


def edge_path(spec):
    """Curve polyline for selection highlighting, not manufacturing geometry."""
    if getattr(spec[2],'geom_type',None)=='LINE':return (spec[2],spec[3])
    return getattr(spec[2],'samples',(spec[2],spec[3]))


def _solid_edge_specs(shape,thickness):
    """Actual rim edges and sharp vertical corners, excluding smooth seams."""
    edges=shape.Edges();bottom=[];top=[];vertical=[]
    for edge in edges:
        steps=64 if edge.geomType()!='LINE' and len(edge.Vertices())==1 else 16
        samples=tuple(edge.positionAt(i/steps).toTuple() for i in range(steps+1))
        zs=[p[2] for p in samples]
        if max(abs(z) for z in zs)<1e-6:bottom.append((edge,samples))
        elif max(abs(z-thickness) for z in zs)<1e-6:top.append((edge,samples))
        elif edge.geomType()=='LINE' and abs(edge.Length()-thickness)<1e-6 and math.dist(samples[0][:2],samples[-1][:2])<1e-6:
            vertical.append((edge,samples))
    box=shape.BoundingBox();cx,cy=(box.xmin+box.xmax)/2,(box.ymin+box.ymax)/2
    def order(item):
        edge,samples=item;p=samples[len(samples)//2]
        angle=math.atan2(p[1]-cy,p[0]-cx)
        if abs(abs(angle)-math.pi)<1e-8:angle=-math.pi
        if abs(angle)<1e-8:angle=0
        return (round(angle,10),edge.geomType(),round(p[0],7),round(p[1],7))
    bottom.sort(key=order);top.sort(key=order)
    labels={'LINE':'odcinek prosty','CIRCLE':'łuk kołowy','ELLIPSE':'łuk eliptyczny'}
    result=[]
    for kind,title,collection in (('bottom','Dolna',bottom),('top','Górna',top)):
        parent=list(range(len(collection)))
        def find(i):
            while parent[i]!=i:i=parent[i]
            return i
        for i,(left,_) in enumerate(collection):
            for j,(right,_) in enumerate(collection[i+1:],i+1):
                for li in (0,1):
                    for ri in (0,1):
                        if math.dist(left.positionAt(li).toTuple(),right.positionAt(ri).toTuple())>1e-6:continue
                        lt,rt=left.tangentAt(li).toTuple(),right.tangentAt(ri).toTuple()
                        if abs(sum(a*b for a,b in zip(lt,rt)))>1-1e-7:parent[find(j)]=find(i)
        for i,(edge,samples) in enumerate(collection):
            a=EdgeAnchor(samples[0],edge.geomType(),samples,edge.Length())
            a.chain=tuple(f'{kind}:{j}' for j in range(len(collection)) if find(j)==find(i))
            result.append((f'{kind}:{i}',f'{title} krawędź {i+1} · {labels.get(edge.geomType(),edge.geomType())}',a,samples[-1]))
    sharp=[]
    for edge,samples in vertical:
        xy=samples[0][:2];directions=[]
        for rim,_ in bottom:
            for parameter in (0,1):
                point=rim.positionAt(parameter).toTuple()
                if math.dist(xy,point[:2])<1e-6:
                    directions.append(rim.tangentAt(parameter).toTuple())
        if len(directions)>=2:
            a,b=directions[:2]
            dot=abs(sum(a[i]*b[i] for i in range(3)))
            if dot<1-1e-7:sharp.append((edge,samples))
    sharp.sort(key=order)
    for i,(_,samples) in enumerate(sharp):
        a,b=samples[0],samples[-1]
        if a[2]>b[2]:a,b=b,a
        result.append((f'corner:{i}',f'Ostry narożnik {i+1} przez grubość',a,b))
    return result


@lru_cache(maxsize=128)
def _profile_edge_specs(key):
    data=json.loads(key)
    return tuple(_solid_edge_specs(outline_solid(data),data['thickness']))


def edge_specs(points,thickness,profile=None,size=None):
    if profile and profile['kind'] not in ('rectangle','taper'):
        size=size or (max(p[0] for p in points),max(p[1] for p in points))
        key=json.dumps(dict(points=points,profile=profile,size=size,thickness=thickness),sort_keys=True)
        return list(_profile_edge_specs(key))
    result=[]
    for i,p in enumerate(points):
        q=points[(i+1)%len(points)]
        result.extend([
            (f'bottom:{i}',f'Dolna krawędź {i+1}',(*p,0),(*q,0)),
            (f'top:{i}',f'Górna krawędź {i+1}',(*p,thickness),(*q,thickness)),
            (f'corner:{i}',f'Narożnik {i+1} przez grubość',(*p,0),(*p,thickness))])
    return result

def match_edge(shape,a,b):
    if isinstance(a,EdgeAnchor) and a.geom_type!='LINE':
        cq=kernel()
        for edge in shape.Edges():
            if edge.geomType()!=a.geom_type or abs(edge.Length()-a.length)>max(1e-5,a.length*1e-7):continue
            if all(edge.distance(cq.Vertex.makeVertex(*point))<1e-5 for point in a.samples[::((len(a.samples)-1)//4)]):return edge
        raise ValueError('Wybrany łuk zmienił się po wcześniejszej obróbce. Zmień zestaw faz lub zaokrągleń.')
    direction=tuple(y-x for x,y in zip(a,b));length2=sum(x*x for x in direction)
    for edge in shape.Edges():
        if edge.geomType()!='LINE':continue
        vertices=edge.Vertices()
        if len(vertices)!=2:continue
        u,v=(vertex.toTuple() for vertex in vertices)
        def on_line(p):
            parameter=sum((p[i]-a[i])*direction[i] for i in range(3))/length2
            nearest=tuple(a[i]+parameter*direction[i] for i in range(3))
            return -.00001<=parameter<=1.00001 and math.dist(p,nearest)<1e-5
        if on_line(u) and on_line(v):return edge
    raise ValueError('Kant nie istnieje po wcześniejszej operacji. Zmień zestaw faz lub zaokrągleń.')


def grouped_edge_features(specifications,fillets,chamfers):
    """OCCT propagates a constant treatment through a tangent edge chain."""
    used={};result={'fillets':[],'chamfers':[]}
    for kind,items,parameter in (('fillets',fillets,'radius'),('chamfers',chamfers,'distance')):
        for item in items:
            spec=specifications.get(item['edge'])
            if spec is None:raise ValueError('Obróbka wskazuje nieistniejącą krawędź.')
            chain=getattr(spec[2],'chain',(item['edge'],))
            value=float(item[parameter])
            if chain in used:
                old_kind,old_value=used[chain]
                if kind!=old_kind or abs(value-old_value)>1e-8:
                    raise ValueError('Krawędzie połączone stycznie tworzą wspólny ciąg. Ustaw dla całego ciągu ten sam promień lub tę samą fazę; różne wartości wymagają osobnego profilu przejścia.')
                continue
            used[chain]=(kind,value);result[kind].append(item)
    return result['fillets'],result['chamfers']


def outline_solid(data):
    """Extrude exact parametric outline; polygon points remain a GUI proxy."""
    cq=kernel();profile=data.get('profile',{});thickness=data['thickness']
    if not profile or profile['kind'] in ('rectangle','taper'):
        return cq.Workplane('XY').polyline(data['points']).close().extrude(thickness).val()
    kind=profile['kind'];u,v=data['size'];edges=[]
    def line(a,b):
        if math.dist(a,b)>1e-8:edges.append(cq.Edge.makeLine((*a,0),(*b,0)))
    if kind in ('ellipse','circle'):
        edge=cq.Edge.makeEllipse(u/2,v/2,(u/2,v/2,0)) if kind=='ellipse' else cq.Edge.makeCircle(u/2,(u/2,v/2,0))
        wire=cq.Wire.assembleEdges([edge])
    elif kind in ('arch','half_ellipse'):
        rise=v if kind=='half_ellipse' else profile['arch_rise'];spring=v-rise
        line((0,0),(u,0));line((u,0),(u,spring))
        edges.append(cq.Edge.makeEllipse(u/2,rise,(u/2,spring,0),angle1=0,angle2=180))
        line((0,spring),(0,0));wire=cq.Wire.assembleEdges(edges)
    elif kind in ('rounded','capsule'):
        radii=profile['radii'] if kind=='rounded' else [min(u,v)/2]*4
        segments=[]
        for radius,cx,cy,start,corner in (
            (radii[0],radii[0],radii[0],math.pi,(0,0)),
            (radii[1],u-radii[1],radii[1],math.pi*1.5,(u,0)),
            (radii[2],u-radii[2],v-radii[2],0,(u,v)),
            (radii[3],radii[3],v-radii[3],math.pi/2,(0,v))):
            if radius==0:segments.append((corner,corner,None));continue
            def point(angle):return (cx+radius*math.cos(angle),cy+radius*math.sin(angle))
            a,m,b=point(start),point(start+math.pi/4),point(start+math.pi/2)
            segments.append((a,b,cq.Edge.makeThreePointArc((*a,0),(*m,0),(*b,0))))
        for i,(a,b,arc) in enumerate(segments):
            if arc is not None:edges.append(arc)
            line(b,segments[(i+1)%len(segments)][0])
        wire=cq.Wire.assembleEdges(edges)
    else:raise ValueError('Nieznany dokładny obrys CAD.')
    solid=cq.Solid.extrudeLinear(wire,[],(0,0,thickness))
    if not solid.isValid():raise ValueError('Parametry obrysu nie tworzą poprawnej bryły CAD.')
    return solid

@lru_cache(maxsize=128)
def cached_shape(key):
    cq=kernel();data=json.loads(key);points=data['points'];thickness=data['thickness']
    shape=outline_solid(data)
    nominal=shape
    specifications={spec[0]:spec for spec in edge_specs(points,thickness,data.get('profile'),data.get('size'))}
    fillets,chamfers=grouped_edge_features(specifications,data['fillets'],data.get('chamfers',[]))
    if chamfers:
        from OCP.BRepFilletAPI import BRepFilletAPI_MakeChamfer
        builder=BRepFilletAPI_MakeChamfer(shape.wrapped)
        try:
            for item in chamfers:
                spec=specifications[item['edge']];edge=match_edge(shape,*spec[2:])
                builder.Add(item['distance'],edge.wrapped)
            builder.Build()
            if not builder.IsDone():raise ValueError('Nieprawidłowa faza.')
            shape=cq.Shape.cast(builder.Shape())
        except Exception as e:raise ValueError('Nie można wykonać faz. Zmniejsz odległość lub zmień krawędzie.') from e
    if fillets:
        from OCP.BRepFilletAPI import BRepFilletAPI_MakeFillet
        builder=BRepFilletAPI_MakeFillet(shape.wrapped)
        for item in fillets:
            spec=specifications.get(item['edge'])
            if spec is None:raise ValueError('Zaokrąglenie wskazuje nieistniejącą krawędź.')
            selected=match_edge(shape,*spec[2:])
            builder.Add(item['radius'],selected.wrapped)
        try:
            builder.Build()
            if not builder.IsDone():raise ValueError('Promienie nie pasują do tej geometrii.')
            shape=cq.Shape.cast(builder.Shape())
            if not shape.isValid() or not shape.Solids():raise ValueError('Zaokrąglenie tworzy nieprawidłową bryłę.')
        except Exception as e:
            raise ValueError('Nie można wykonać tych zaokrągleń. Zmniejsz promień lub zmień zestaw krawędzi.') from e
    for cut in data.get('machining',[]):
        depth=thickness if cut['type']=='cutout' else cut['depth'];u,v=cut['u'],cut['v']
        z=thickness-depth if cut['face']=='top' else 0
        if cut['shape']=='circle':tool=cq.Solid.makeCylinder(cut['width']/2,depth,cq.Vector(u,v,z))
        elif cut['shape']=='slot':tool=cq.Workplane('XY').slot2D(cut['length'],cut['width']).extrude(depth).val().translate((u,v,z))
        else:tool=cq.Solid.makeBox(cut['length'],cut['width'],depth,cq.Vector(u-cut['length']/2,v-cut['width']/2,z))
        if cut.get('angle',0):tool=tool.rotate((u,v,0),(u,v,1),cut['angle'])
        volume=tool.Volume()
        if abs(nominal.intersect(tool).Volume()-volume)>max(.001,volume*1e-6):raise ValueError('Obróbka wykracza poza kontur części.')
        if shape.intersect(tool).Volume()<.001:raise ValueError('Obróbka nie przecina materiału.')
        shape=shape.cut(tool)
    if not shape.isValid() or shape.Volume()<=0 or len(shape.Solids())!=1:
        raise ValueError('Obróbka usuwa cały materiał lub dzieli część na oddzielne bryły.')
    return shape

def shape_key(part):
    return json.dumps(dict(points=part.points(),size=[part.u,part.v],profile=part.profile,thickness=part.t,fillets=sorted(part.fillets,key=lambda f:f['edge']),
        chamfers=sorted(part.chamfers,key=lambda c:c['edge']),machining=part.machining,
        drilling=[dict(u=u,v=v,diameter=d,depth=h,face='top') for u,v,d,h in part.holes]+part.operations),sort_keys=True)

def local_shape(part):return cached_shape(shape_key(part))

@lru_cache(maxsize=128)
def cached_mesh(key):
    shape=cached_shape(key)
    data=json.loads(key);shape=cut_drills(shape,data['drilling'],data['thickness'],data['points'])
    vertices,triangles=shape.tessellate(.2,.15)
    vertices=[v.toTuple() for v in vertices]
    return tuple(tuple(vertices[i] for i in triangle) for triangle in triangles)

def mesh(part):return cached_mesh(shape_key(part))

def cut_drills(shape,drilling,thickness,points,check_only=False,check_boundary=False):
    from types import SimpleNamespace
    from features import drill_location
    cq=kernel();proxy=SimpleNamespace(t=thickness,u=max(p[0] for p in points),v=max(p[1] for p in points))
    for op in drilling:
        position,direction=drill_location(proxy,op)
        cylinder=cq.Solid.makeCylinder(op['diameter']/2,op['depth'],cq.Vector(*position),cq.Vector(*direction))
        if check_boundary:
            volume=cylinder.Volume();overlap=shape.intersect(cylinder).Volume()
            if abs(volume-overlap)>max(.001,volume*1e-6):
                raise ValueError('To wiercenie koliduje z profilem krawędzi lub wykracza poza kontur.')
        if not check_only:
            shape=shape.cut(cylinder)
            if op.get('countersink_diameter'):
                cone=cq.Solid.makeCone(op['countersink_diameter']/2,op['diameter']/2,op['countersink_depth'],cq.Vector(*position),cq.Vector(*direction))
                shape=shape.cut(cone)
    return shape

def drilled_shape(part,check_only=False):
    data=json.loads(shape_key(part))
    return cut_drills(local_shape(part),data['drilling'],part.t,part.points(),check_only,
        bool((part.fillets or part.chamfers or any(op['face'] not in ('top','bottom') for op in part.operations)) and not part.machining))

def validate_fillets(part):
    if not (part.fillets or part.chamfers or part.machining or any(o['face'] not in ('top','bottom') for o in part.operations)):return
    shape=local_shape(part)
    if shape.Volume()<=0:raise ValueError('Bryła po zaokrągleniu nie ma objętości.')
    drilled_shape(part,check_only=True)

def step_export(project,path,export_type='STEP'):
    from core import world
    cq=kernel();project.validate();solids=[]
    for part in project.parts:
        shape=drilled_shape(part)
        origin=world(part,0,0,0)
        basis=[tuple(p-o for p,o in zip(world(part,*axis),origin)) for axis in ((1,0,0),(0,1,0),(0,0,1))]
        from OCP.gp import gp_Trsf
        transform=gp_Trsf()
        transform.SetValues(*[value for i in range(3) for value in [basis[j][i] for j in range(3)]+[origin[i]]])
        matrix=cq.Matrix(transform)
        solids.append(shape.transformShape(matrix))
    if not solids:raise ValueError('Projekt nie zawiera elementów do eksportu.')
    cq.exporters.export(cq.Compound.makeCompound(solids),str(path),exportType=export_type)

def stl_export(project,path):return step_export(project,path,'STL')
