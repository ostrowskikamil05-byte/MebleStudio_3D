"""Mebel Studio 3D: shared geometry, project and manufacturing model (millimetres)."""
from __future__ import annotations
from dataclasses import dataclass, field, asdict
import copy
import csv
import html
import json
import math
import os
from pathlib import Path
import tempfile
import uuid

VERSION = 1

def uid():
    return uuid.uuid4().hex[:12]

def finite(x, lo=-100000, hi=100000):
    x = float(x)
    if not math.isfinite(x) or not lo <= x <= hi:
        raise ValueError(f"Wartość musi mieścić się w zakresie {lo:g}–{hi:g}.")
    return x

def polygon_area(points):
    return abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))) / 2

def validate_polygon(points):
    if not 3 <= len(points) <= 128:
        raise ValueError("Kontur wymaga od 3 do 128 punktów.")
    p = [(finite(x,0,10000),finite(y,0,10000)) for x,y in points]
    if polygon_area(p) < 1:
        raise ValueError("Kontur musi mieć dodatnią powierzchnię.")
    def orient(a,b,c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    def on(a,b,c):
        return abs(orient(a,b,c))<1e-8 and min(a[0],b[0])-1e-8<=c[0]<=max(a[0],b[0])+1e-8 and min(a[1],b[1])-1e-8<=c[1]<=max(a[1],b[1])+1e-8
    for i in range(len(p)):
        a,b=p[i],p[(i+1)%len(p)]
        if math.dist(a,b)<0.01:
            raise ValueError("Kolejne punkty konturu nie mogą być identyczne.")
        for j in range(i+1,len(p)):
            if j==i+1 or (i==0 and j==len(p)-1):
                continue
            c,d=p[j],p[(j+1)%len(p)]
            o=[orient(a,b,c),orient(a,b,d),orient(c,d,a),orient(c,d,b)]
            if o[0]*o[1]<0 and o[2]*o[3]<0 or on(a,b,c) or on(a,b,d) or on(c,d,a) or on(c,d,b):
                raise ValueError("Krawędzie konturu nie mogą się przecinać ani stykać.")
    return p

@dataclass
class Material:
    id: str = field(default_factory=uid)
    name: str = "Dąb naturalny"
    color: str = "#bd9565"
    price_m2: float = 95
    sheet_u: float = 2800
    sheet_v: float = 2070
    kind: str = "plyta"
    price_m3: float = 0

    def cost(self, part):
        return part.area() * (part.t / 1000 * self.price_m3 if self.kind == 'drewno' else self.price_m2)

@dataclass
class Part:
    id: str = field(default_factory=uid)
    name: str = "Nowy element"
    group: str = "Mebel"
    u: float = 600
    v: float = 400
    t: float = 18
    plane: str = "XY"
    x: float = 0
    y: float = 0
    z: float = 0
    rx: float = 0
    ry: float = 0
    rz: float = 0
    material: str = ""
    grain: bool = True
    edges: list = field(default_factory=lambda: [False,False,False,False])
    contour: list = field(default_factory=list)
    holes: list = field(default_factory=list)
    visible: bool = True
    role: str = 'other'
    assembly: dict = field(default_factory=dict)
    operations: list = field(default_factory=list)
    fillets: list = field(default_factory=list)
    chamfers: list = field(default_factory=list)
    machining: list = field(default_factory=list)
    motion: dict = field(default_factory=dict)
    # Exact, editable outline definition; contour remains the drawing tessellation.
    profile: dict = field(default_factory=dict)
    contour_edge: bool = False
    decor: dict = field(default_factory=dict)
    style_source: dict = field(default_factory=dict)
    # The quick configurator records provenance separately from assembly links.
    configurator_source: dict = field(default_factory=dict)
    # Furniture placement is separate from construction dimensions and motion.
    layout: dict = field(default_factory=dict)

    def points(self):
        return [tuple(p) for p in self.contour] if self.contour else [(0,0),(self.u,0),(self.u,self.v),(0,self.v)]

    def area(self):
        if self.profile:
            from shaped_furniture import profile_area
            return profile_area(self.profile,self.u,self.v)/1e6
        return polygon_area(self.points())/1e6

    def edge_m(self):
        if self.contour:
            if not self.contour_edge:return 0
            if self.profile:
                from shaped_furniture import profile_perimeter
                return profile_perimeter(self.profile,self.u,self.v)/1000
            pts=self.points()
            return sum(math.dist(a,b) for a,b in zip(pts,pts[1:]+pts[:1]))/1000
        return (self.v*(int(self.edges[0])+int(self.edges[1]))+self.u*(int(self.edges[2])+int(self.edges[3])))/1000

    def blank(self,edge_t):
        if self.contour:
            return self.u,self.v
        return self.u-edge_t*(int(self.edges[0])+int(self.edges[1])),self.v-edge_t*(int(self.edges[2])+int(self.edges[3]))

    def validate(self):
        if not isinstance(self.configurator_source,dict):
            raise ValueError('Nieprawidłowe dane kreatora mebla.')
        if self.configurator_source and any(key in self.configurator_source for key in ('schema','key','config','manifest')):
            source=self.configurator_source
            if source.get('schema')!=1 or not isinstance(source.get('key'),str):
                raise ValueError('Nieprawidłowa wersja lub część kreatora mebla.')
            if 'config' in source:
                if not isinstance(source['config'],dict) or not isinstance(source.get('manifest'),dict):
                    raise ValueError('Nieprawidłowy zapis konfiguracji mebla.')
                for entry in source['manifest'].values():
                    if not isinstance(entry,dict) or not isinstance(entry.get('id'),str) or not isinstance(entry.get('signature'),str):
                        raise ValueError('Nieprawidłowe powiązania konfiguracji mebla.')
        if not isinstance(self.layout,dict) or set(self.layout)-{'x','y','z','yaw'}:
            raise ValueError('Nieprawidłowe ustawienie mebla w zestawie.')
        for key in ('x','y','z','yaw'):
            if key in self.layout:self.layout[key]=finite(self.layout[key])
        if not isinstance(self.profile,dict) or not isinstance(self.decor,dict) or not isinstance(self.style_source,dict) or type(self.contour_edge) is not bool:
            raise ValueError('Nieprawidłowe parametry obrysu, obrzeża lub dekoracji.')
        if not isinstance(self.motion,dict):raise ValueError('Nieprawidłowe dane mechanizmu.')
        if self.motion:
            from mechanisms import MODES
            if self.motion.get('mode') not in MODES.values():raise ValueError('Nieznany mechanizm.')
            finite(self.motion.get('angle',100),0,180);finite(self.motion.get('travel',400),0,3000)
        if not isinstance(self.role,str) or not isinstance(self.assembly,dict) or not isinstance(self.operations,list):
            raise ValueError('Nieprawidłowe dane konstrukcyjne elementu.')
        if not isinstance(self.name,str) or not self.name.strip():
            raise ValueError("Wpisz nazwę elementu.")
        for key in ('u','v','t'):
            setattr(self,key,finite(getattr(self,key),0.1,10000))
        for key in ('x','y','z','rx','ry','rz'):
            setattr(self,key,finite(getattr(self,key)))
        if self.plane not in ('XY','XZ','YZ'):
            raise ValueError("Nieprawidłowa płaszczyzna elementu.")
        if len(self.edges)!=4 or any(type(e) is not bool for e in self.edges):
            raise ValueError("Nieprawidłowe obrzeża.")
        if self.profile:
            from shaped_furniture import shape_definition,contour
            self.profile=shape_definition(self.profile.get('kind'),self.u,self.v,**{k:v for k,v in self.profile.items() if k!='kind'})
            self.contour=contour(self.profile['kind'],self.u,self.v,**{k:v for k,v in self.profile.items() if k!='kind'})
        if self.contour:
            self.contour=validate_polygon(self.contour)
            if min(x for x,y in self.contour)!=0 or min(y for x,y in self.contour)!=0:
                raise ValueError("Kontur musi zaczynać się od minimum U=0 i V=0.")
            self.u=max(x for x,y in self.contour)
            self.v=max(y for x,y in self.contour)
            if any(self.edges):
                raise ValueError("Dla konturu nieregularnego obrzeża należy wyłączyć.")
        if self.decor:
            from shaped_furniture import refresh_decor
            refresh_decor(self)
        for hole in self.holes:
            if len(hole)!=4:
                raise ValueError("Otwór: U; V; średnica; głębokość.")
            a,b,d,depth=[finite(v,0,10000) for v in hole]
            if d<=0 or depth<=0 or depth>self.t or not d/2<=a<=self.u-d/2 or not d/2<=b<=self.v-d/2:
                raise ValueError("Otwór wykracza poza element lub jego grubość.")
            # Every sampled point on the circle must lie inside the contour.
            for i in range(72):
                q=(a+d/2*math.cos(i*math.tau/72),b+d/2*math.sin(i*math.tau/72))
                if not inside(q,self.points()):
                    raise ValueError("Otwór wykracza poza kontur.")
        for operation in self.operations:
            from features import validate_drill
            validate_drill(self,operation)
        valid_edges={f'{kind}:{i}' for kind in ('top','bottom','corner') for i in range(len(self.points()))}
        if self.profile and (self.fillets or self.chamfers):
            from cad_geometry import edge_specs
            valid_edges={spec[0] for spec in edge_specs(self.points(),self.t,self.profile,(self.u,self.v))}
        if not isinstance(self.fillets,list) or len({f['edge'] for f in self.fillets})!=len(self.fillets):
            raise ValueError('Zaokrąglenia wymagają unikalnych krawędzi.')
        for fillet in self.fillets:
            if fillet['edge'] not in valid_edges:raise ValueError('Zaokrąglenie wskazuje nieistniejącą krawędź.')
            finite(fillet['radius'],.01,10000)
        if not isinstance(self.chamfers,list) or len({c['edge'] for c in self.chamfers})!=len(self.chamfers):raise ValueError('Fazy wymagają unikalnych krawędzi.')
        if {f['edge'] for f in self.fillets}&{c['edge'] for c in self.chamfers}:raise ValueError('Jeden kant może mieć fazę albo zaokrąglenie.')
        for chamfer in self.chamfers:
            if chamfer['edge'] not in valid_edges:raise ValueError('Faza wskazuje nieistniejącą krawędź.')
            finite(chamfer['distance'],.01,10000)
        if not isinstance(self.machining,list) or len(self.machining)>200:raise ValueError('Maksymalnie 200 operacji frezowania na element.')
        for cut in self.machining:
            from features import validate_cut
            validate_cut(self,cut)
        if self.fillets or self.chamfers or self.machining or any(o['face'] not in ('top','bottom') for o in self.operations):
            from cad_geometry import validate_fillets
            validate_fillets(self)

def inside(q,p):
    x,y=q
    hit=False
    for a,b in zip(p,p[1:]+p[:1]):
        cross=(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])
        if abs(cross)<1e-7 and min(a[0],b[0])-1e-7<=x<=max(a[0],b[0])+1e-7 and min(a[1],b[1])-1e-7<=y<=max(a[1],b[1])+1e-7:
            return True
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:
            hit=not hit
    return hit

def _default_scene():
    from room_objects import default_scene
    return default_scene()

@dataclass
class Project:
    name: str = "Mój projekt"
    parts: list = field(default_factory=list)
    materials: list = field(default_factory=lambda: [Material(),Material(name="Biały mat",color="#eeeeea",price_m2=65),Material(name="Grafit",color="#4f5661",price_m2=85),Material(name="MDF",kind="mdf",color="#ba9871",price_m2=70),Material(name="Sklejka",kind="sklejka",color="#d4b58a",price_m2=110),Material(name="Drewno lite — dąb",kind="drewno",color="#b68a58",price_m3=4500,sheet_u=2500,sheet_v=200),Material(name="HDF",kind="hdf",color="#b69d80",price_m2=25)])
    hardware: list = field(default_factory=list)
    scene: dict = field(default_factory=_default_scene)
    kerf: float = 3.2
    margin: float = 10
    edge_t: float = 1
    edge_price: float = 3.5
    labor: float = 0
    markup: float = 20

    def mat(self,id):
        return next(m for m in self.materials if m.id==id)

    def validate(self):
        from room_objects import validate_scene
        self.scene=validate_scene(self.scene)
        if not self.materials or len(self.parts)>2000:
            raise ValueError("Projekt wymaga materiałów i może mieć najwyżej 2000 elementów.")
        ids=[m.id for m in self.materials]
        if len(set(ids))!=len(ids) or len({p.id for p in self.parts})!=len(self.parts):
            raise ValueError("Powtarzające się identyfikatory.")
        for m in self.materials:
            if m.kind not in ('plyta', 'mdf', 'sklejka', 'drewno', 'hdf', 'inne'):
                raise ValueError('Rodzaj materiału: plyta, mdf, sklejka, drewno, hdf lub inne.')
            finite(m.price_m3,0,1e9)
            if not isinstance(m.name,str) or not m.name.strip():
                raise ValueError("Materiał wymaga nazwy.")
            finite(m.price_m2,0,1e6)
            finite(m.sheet_u,1,10000);finite(m.sheet_v,1,10000)
            if not isinstance(m.color,str) or len(m.color)!=7 or m.color[0]!='#':
                raise ValueError("Kolor materiału: #RRGGBB.")
            int(m.color[1:],16)
        for key in ('kerf','margin','edge_t','edge_price','labor','markup'):
            finite(getattr(self,key),0,100000)
        for p in self.parts:
            p.validate()
            if p.material not in ids:
                raise ValueError("Element wskazuje nieznany materiał.")
            if min(p.blank(self.edge_t))<=0:
                raise ValueError("Obrzeże jest grubsze od wymiaru elementu.")
        for h in self.hardware:
            if not isinstance(h.get('name'),str) or not h['name'].strip():
                raise ValueError("Akcesorium wymaga nazwy.")
            finite(h['qty'],0,100000);finite(h['price'],0,100000)
            if h.get('owner_id') and h['owner_id'] not in {p.id for p in self.parts}:
                raise ValueError('Okucie wskazuje usunięty element.')
        part_ids={p.id for p in self.parts}
        for p in self.parts:
            for operation in p.operations:
                if operation.get('owner_id') and operation['owner_id'] not in part_ids:
                    raise ValueError('Wiercenie wskazuje usunięty element.')
            if any(i not in part_ids for i in p.assembly.get('anchors',[])):
                raise ValueError('Powiązanie konstrukcyjne wskazuje usunięty element.')

    def data(self):
        return {"version":VERSION,**asdict(self)}

    @classmethod
    def from_data(cls,data):
        data=copy.deepcopy(data)
        if data.pop('version',None)!=VERSION:
            raise ValueError("Nieobsługiwana wersja projektu.")
        mats=[Material(**m) for m in data.pop('materials')]
        parts=[Part(**p) for p in data.pop('parts')]
        obj=cls(materials=mats,parts=parts,**data)
        obj.validate()
        return obj

    def save(self,path):
        self.validate()
        path=Path(path)
        fd,tmp=tempfile.mkstemp(prefix=path.name+'.',dir=str(path.parent))
        try:
            with os.fdopen(fd,'w',encoding='utf-8') as f:
                json.dump(self.data(),f,ensure_ascii=False,indent=2)
            os.replace(tmp,path)
        finally:
            if os.path.exists(tmp):os.unlink(tmp)

    @classmethod
    def load(cls,path):
        if Path(path).stat().st_size>10_000_000:
            raise ValueError("Plik projektu jest zbyt duży.")
        return cls.from_data(json.loads(Path(path).read_text(encoding='utf-8')))

    def costs(self):
        panels=sum(self.mat(p.material).cost(p) for p in self.parts)
        edge=sum(p.edge_m() for p in self.parts)*self.edge_price
        hardware=sum(h['qty']*h['price'] for h in self.hardware)+sum(obj.get('price',0) for obj in self.scene.get('objects',[]))
        base=panels+edge+hardware+self.labor
        return dict(panels=panels,edges=edge,hardware=hardware,labor=self.labor,base=base,total=base*(1+self.markup/100))

def rotate(point,rx,ry,rz):
    x,y,z=point
    a,b,c=[math.radians(i) for i in (rx,ry,rz)]
    y,z=y*math.cos(a)-z*math.sin(a),y*math.sin(a)+z*math.cos(a)
    x,z=x*math.cos(b)+z*math.sin(b),-x*math.sin(b)+z*math.cos(b)
    x,y=x*math.cos(c)-y*math.sin(c),x*math.sin(c)+y*math.cos(c)
    return x,y,z

def local_world(p,u,v,w):
    local={'XY':(u,v,w),'XZ':(u,w,v),'YZ':(w,u,v)}[p.plane]
    a,b,c=rotate(local,p.rx,p.ry,p.rz)
    return a+p.x,b+p.y,c+p.z

def layout_point(p,point):
    placement=p.layout
    if not placement:return tuple(point)
    a,b,c=rotate(point,0,0,placement['yaw']) if placement.get('yaw',0) else point
    return a+placement.get('x',0),b+placement.get('y',0),c+placement.get('z',0)

def inverse_layout_point(p,point):
    placement=p.layout
    if not placement:return tuple(point)
    return rotate(tuple(point[i]-placement.get('xyz'[i],0) for i in range(3)),0,0,-placement.get('yaw',0))

def world(p,u,v,w):
    return layout_point(p,local_world(p,u,v,w))

def faces(p, *, interactive=False):
    from viewport_render import geometry_faces
    return geometry_faces(p, interactive)

class Camera:
    def __init__(self,yaw=-38,pitch=24,scale=0.4,center=(300,250,420),pan=(0,0)):
        self.yaw,self.pitch,self.scale,self.center,self.pan=yaw,pitch,scale,center,pan

    def _basis(self):
        key=(self.yaw,self.pitch)
        if getattr(self,'_basis_key',None)!=key:
            a,b=math.radians(self.yaw),math.radians(self.pitch)
            self._basis_key=key
            self._basis_value=(math.cos(a),math.sin(a),math.cos(b),math.sin(b))
        return self._basis_value

    def view(self,point):
        x,y,z=(point[i]-self.center[i] for i in range(3))
        ca,sa,cb,sb=self._basis()
        u=x*ca-y*sa
        d=x*sa+y*ca
        return u,z*cb+d*sb,-d*cb+z*sb

    def project(self,point,width,height):
        u,v,d=self.view(point)
        return width/2+self.pan[0]+u*self.scale,height/2+self.pan[1]-v*self.scale,d

    def fit(self,project,width,height):
        from viewport_scene import fit_scene
        fit_scene(self,project,(width,height),padding=120)

def shade(color,factor):
    r,g,b=[int(color[i:i+2],16) for i in (1,3,5)]
    return '#'+''.join(f'{min(255,max(0,round(v*factor))):02x}' for v in (r,g,b))

def explosion_shift(p,camera,explode):
    center=world(p,p.u/2,p.v/2,p.t)
    return tuple((center[i]-camera.center[i])*explode for i in range(3))

def render_faces(project,camera,width,height,selected=None,explode=0,*,interactive=False,context=None):
    from viewport_render import render
    return render(project,camera,width,height,selected,explode,
                  interactive=interactive,context=context)

def svg_view(project,camera=None,width=1100,height=720,selected=None):
    camera=camera or Camera()
    if camera.scale<=0:camera.fit(project,width,height)
    items=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}"><rect width="100%" height="100%" fill="#eef1f5"/>']
    for f in render_faces(project,camera,width,height,selected):
        pts=' '.join(f'{x:.2f},{y:.2f}' for x,y in f['points'])
        border=f['color'] if f.get('mesh') else '#e68c36' if f['selected'] else '#59616b'
        items.append(f'<polygon points="{pts}" fill="{f["color"]}" stroke="{border}" stroke-width="{0 if f.get("mesh") else 1}"/>')
    items.append('</svg>')
    return ''.join(items)

def add_template(project,kind,w=600,h=800,d=500,t=18,shelves=2,front=True,origin=(0,0,0),group=None):
    for val in (w,h,d,t):finite(val,1,10000)
    if w<=2*t or h<=2*t or d<=t or shelves<0 or shelves>30:
        raise ValueError("Nieprawidłowe wymiary konstrukcji lub liczba półek.")
    group=group or {'cabinet':'Szafka','wardrobe':'Szafa','table':'Stół','shelf':'Regał'}[kind]
    ox,oy,oz=origin
    result=[]
    def add(name,u,v,thick,plane,x,y,z,mat=None,edges=None):
        p=Part(name=name,group=group,u=u,v=v,t=thick,plane=plane,x=x+ox,y=y+oy,z=z+oz,material=mat or project.materials[0].id,edges=edges or [False]*4)
        reference=next((q for q in project.parts if q.group==group),None)
        if reference:p.layout=copy.deepcopy(reference.layout)
        p.validate();result.append(p)
    if kind=='table':
        leg=max(40,t*2)
        if min(w,d)<=leg*2 or h<=t:
            raise ValueError("Stół jest zbyt mały dla nóg.")
        add('Blat',w,d,t,'XY',0,0,h-t,edges=[True]*4)
        for i,(x,y) in enumerate([(0,0),(w-leg,0),(0,d-leg),(w-leg,d-leg)]):
            add(f'Noga {i+1}',leg,h-t,leg,'XZ',x,y,0)
    else:
        add('Bok lewy',d,h,t,'YZ',0,0,0)
        add('Bok prawy',d,h,t,'YZ',w-t,0,0)
        add('Dno',w-2*t,d,t,'XY',t,0,0)
        add('Wieniec',w-2*t,d,t,'XY',t,0,h-t)
        for i in range(shelves):
            z=t+(h-2*t)*(i+1)/(shelves+1)-t/2
            add(f'Półka {i+1}',w-2*t-2,d-10,t,'XY',t+1,5,z,edges=[False,False,True,False])
        if kind!='shelf':
            add('Plecy nakładane HDF',w,h,3,'XZ',0,d,0,next((m.id for m in project.materials if m.kind=='hdf'),project.materials[0].id))
            if front:
                if w<=7:raise ValueError("Za mała szerokość frontów.")
                for i in range(2):
                    add(f'Front {i+1}',(w-6)/2,h-4,t,'XZ',2+i*(w/2-1),-t-2,2,project.materials[1].id,[True]*4)
    project.parts.extend(result)
    return result

def cut_plan(project):
    """Area-sorted guillotine heuristic, with independent stock for material/thickness."""
    project.validate()
    sheets=[];unplaced=[]
    parts=sorted(project.parts,key=lambda p:p.blank(project.edge_t)[0]*p.blank(project.edge_t)[1],reverse=True)
    for p in parts:
        u,v=p.blank(project.edge_t);k=project.kerf;m=project.mat(p.material)
        options=[(u,v,False)]+([] if p.grain else [(v,u,True)])
        chosen=None
        for si,s in enumerate(sheets):
            if s['material']!=p.material or s['thickness']!=p.t:continue
            for ri,r in enumerate(s['free']):
                for a,b,rot in options:
                    if a+k<=r[2]+1e-8 and b+k<=r[3]+1e-8:
                        score=r[2]*r[3]-(a+k)*(b+k)
                        if chosen is None or score<chosen[0]:chosen=(score,si,ri,a,b,rot)
        if chosen is None:
            sw,sh=m.sheet_u-2*project.margin,m.sheet_v-2*project.margin
            possible=[o for o in options if o[0]<=sw+1e-8 and o[1]<=sh+1e-8]
            if not possible:
                unplaced.append(p.id);continue
            a,b,rot=possible[0]
            sheets.append(dict(material=p.material,thickness=p.t,u=m.sheet_u,v=m.sheet_v,free=[(project.margin,project.margin,sw+k,sh+k)],pieces=[]))
            chosen=(0,len(sheets)-1,0,a,b,rot)
        _,si,ri,a,b,rot=chosen;s=sheets[si];x,y,fw,fh=s['free'].pop(ri)
        s['pieces'].append(dict(id=p.id,x=x,y=y,u=a,v=b,rotated=rot))
        rw=fw-a-k;bh=fh-b-k
        # A vertical guillotine cut, then a horizontal cut in the left strip.
        if rw>1e-8:s['free'].append((x+a+k,y,rw,fh))
        if bh>1e-8:s['free'].append((x,y+b+k,a+k,bh))
    return sheets,unplaced

def manufacturing_export(project,path):
    project.validate()
    data = dict(schema='mebelstudio-manufacturing-1', units='mm',
                project=project.name, machine_program=False,
                coordinate_system='Local U,V; W=0 at bottom, W=thickness at top. Drilling face selects entry and local coordinates. Edge U faces use V,T; edge V faces use U,T. Assembly position/rotation and hardware positions use furniture construction coordinates; room_placement is applied last. world_origin/world_basis map local U,V,W to the room.',
                materials=[asdict(m) for m in project.materials], hardware=copy.deepcopy(project.hardware), parts=[])
    for p in project.parts:
        edge_legend=[]
        if p.fillets or p.chamfers:
            from cad_geometry import edge_specs,edge_path
            for spec in edge_specs(p.points(),p.t,p.profile,(p.u,p.v)):
                path=edge_path(spec)
                midpoint=path[len(path)//2] if len(path)>2 else tuple((a+b)/2 for a,b in zip(path[0],path[-1]))
                edge_legend.append(dict(id=spec[0],name=spec[1],
                    geometry=getattr(spec[2],'geom_type','LINE'),start=list(path[0]),
                    midpoint=list(midpoint),end=list(path[-1]),
                    tangent_chain=list(getattr(spec[2],'chain',(spec[0],)))))
        data['parts'].append(dict(id=p.id,name=p.name,group=p.group,material=p.material,
            thickness=p.t,finished_size=[p.u,p.v],cut_size=list(p.blank(project.edge_t)),
            contour=p.points(),edges=p.edges,grain_along_u=p.grain,
            outline_definition=copy.deepcopy(p.profile),full_contour_edge=p.contour_edge,
            contour_edge_length_m=p.edge_m(),decoration=copy.deepcopy(p.decor),
            outline_geometry='exact_in_STEP_drawing_tessellation' if p.profile else 'polygon',
            edge_numbering='exact_CAD_outline' if p.profile else 'polygon_vertices',edge_legend=edge_legend,
            assembly=dict(plane=p.plane,position=[p.x,p.y,p.z],rotation=[p.rx,p.ry,p.rz],
                room_placement=copy.deepcopy(p.layout),world_origin=list(world(p,0,0,0)),
                world_basis=[[b-a for a,b in zip(world(p,0,0,0),world(p,*axis))] for axis in ((1,0,0),(0,1,0),(0,0,1))]),
            role=p.role,construction=copy.deepcopy(p.assembly),motion=copy.deepcopy(p.motion),
            fillets=copy.deepcopy(p.fillets),profile_status='nominal_before_3d_features' if p.fillets or p.chamfers or p.machining else 'nominal',
            chamfers=copy.deepcopy(p.chamfers),machining=copy.deepcopy(p.machining),
            drilling=[dict(u=u,v=v,diameter=d,depth=depth,face='top') for u,v,d,depth in p.holes]+copy.deepcopy(p.operations)))
    Path(path).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

def csv_export(project,path):
    with open(path,'w',encoding='utf-8-sig',newline='') as f:
        writer=csv.writer(f,delimiter=';')
        writer.writerow(['ID','Grupa','Element','Materiał','U gotowe mm','V gotowe mm','Grubość mm','U do cięcia mm','V do cięcia mm','Obrzeże U0','Obrzeże U1','Obrzeże V0','Obrzeże V1','Włókna wzdłuż U','Kontur','Otwory U,V,D,głębokość mm','Funkcja','Wiercenia i standardy JSON','Zaokrąglenia 3D JSON','Fazy 3D JSON','Frezowanie JSON','Obrys parametryczny JSON','Obrzeże całego obrysu','Długość obrzeża m'])
        for p in project.parts:
            writer.writerow([p.id,p.group,p.name,project.mat(p.material).name,p.u,p.v,p.t,*p.blank(project.edge_t),*['TAK' if x else '' for x in p.edges],'TAK' if p.grain else '',json.dumps(p.contour),json.dumps(p.holes),p.role,json.dumps(p.operations,ensure_ascii=False),json.dumps(p.fillets),json.dumps(p.chamfers),json.dumps(p.machining),json.dumps(p.profile),'TAK' if p.contour_edge else '',p.edge_m()])

def dxf_export(project,path):
    if any(p.fillets or p.chamfers or p.machining or any(o['face'] not in ('top','bottom') or o.get('countersink_diameter') for o in p.operations) for p in project.parts):
        raise ValueError('Projekt ma obróbki bryłowe 3D lub wiercenia krawędziowe. Użyj eksportu STEP; zwykły DXF nie opisuje tej obróbki.')
    lines=['0','SECTION','2','HEADER','9','$ACADVER','1','AC1015','9','$INSUNITS','70','4','0','ENDSEC','0','SECTION','2','ENTITIES'];offset=0
    def pair(code,value):lines.extend([str(code),str(value)])
    for p in project.parts:
        layer='P_'+p.id
        pair(0,'LWPOLYLINE');pair(8,layer);pair(90,len(p.points()));pair(70,1)
        for x,y in p.points():pair(10,x+offset);pair(20,y)
        for x,y,d,depth in p.holes:
            pair(0,'CIRCLE');pair(8,'H_'+p.id);pair(10,x+offset);pair(20,y);pair(30,0);pair(40,d/2)
        for op in p.operations:
            pair(0,'CIRCLE');pair(8,'AUTO_'+op['face'].upper()+'_'+p.id)
            pair(10,op['u']+offset);pair(20,op['v']);pair(30,0);pair(40,op['diameter']/2)
        offset+=p.u+100
    lines+=['0','ENDSEC','0','EOF']
    Path(path).write_text('\n'.join(lines)+'\n',encoding='ascii')

def report_export(project,path):
    esc=html.escape;sheets,bad=cut_plan(project);c=project.costs()
    cam=Camera(scale=0);cam.fit(project,1100,720)
    out=[f'<!doctype html><html lang="pl"><meta charset="utf-8"><title>{esc(project.name)}</title><style>body{{font:15px Arial;color:#202938;max-width:1100px;margin:32px auto;padding:20px}}table{{border-collapse:collapse;width:100%;font-size:13px}}td,th{{border:1px solid #ccd3dc;padding:8px;text-align:left}}th{{background:#eef1f5}}svg{{max-width:100%;height:auto}}h2{{margin-top:32px}}@media print{{button{{display:none}}body{{margin:0}}svg{{max-height:500px}}h2{{break-after:avoid}}}}</style><button onclick="print()">Drukuj / zapisz PDF</button><h1>{esc(project.name)}</h1><p>Mebel Studio 3D · Wymiary w mm · Dokument roboczy</p>',svg_view(project,cam)]
    out.append('<h2>Formatki</h2><p>Wymiary gotowe uwzględniają obrzeża. Wymiar do cięcia odejmuje grubość wskazanych obrzeży. Kontury nieregularne wymagają osobnej obróbki.</p><table><tr><th>Grupa / element</th><th>Materiał</th><th>Gotowe U × V × T</th><th>Do cięcia U × V</th><th>Obrzeża m</th></tr>')
    for p in project.parts:
        a,b=p.blank(project.edge_t)
        out.append(f'<tr><td>{esc(p.group)} / {esc(p.name)}</td><td>{esc(project.mat(p.material).name)}</td><td>{p.u:g} × {p.v:g} × {p.t:g}</td><td>{a:g} × {b:g}{" · kontur" if p.contour else ""}</td><td>{p.edge_m():.2f}</td></tr>')
    out.append('</table><h2>Rzuty konstrukcji</h2>')
    for name,yaw,pitch in [('Przód',0,0),('Bok',-90,0),('Góra',0,90)]:
        cam=Camera(yaw,pitch,scale=0);cam.fit(project,800,500)
        out.append(f'<h3>{name}</h3>'+svg_view(project,cam,800,500))
    out.append('<h2>Kontury i otwory</h2>')
    for p in project.parts:
        if p.contour or p.holes:
            out.append(f'<p><b>{esc(p.name)}</b> · ID {p.id} · kontur U,V: {esc(str(p.contour))} · otwory U,V,średnica,głębokość: {esc(str(p.holes))}</p>')
        if p.operations:
            out.append(f'<p><b>{esc(p.name)}</b> · wiercenia automatyczne (wzorzec roboczy): {esc(json.dumps(p.operations,ensure_ascii=False))}</p>')
        if p.chamfers or p.machining:
            out.append(f'<p><b>{esc(p.name)}</b> · fazy: {esc(json.dumps(p.chamfers))}; frezowanie: {esc(json.dumps(p.machining))}. Dokładna geometria w STEP.</p>')
        if p.fillets:
            numbering='rzeczywiste łuki i odcinki obrysu CAD' if p.profile else 'punkty konturu'
            out.append(f'<p><b>{esc(p.name)}</b> · zaokrąglenia bryłowe: {esc(json.dumps(p.fillets))}. Numeracja krawędzi: {numbering}; dokładna geometria w STEP.</p>')
    out.append('<h2>Okucia i powiązania</h2><table><tr><th>Okucie</th><th>Ilość</th><th>Powiązanie</th><th>Status</th></tr>')
    for item in project.hardware:
        owner=next((p.name for p in project.parts if p.id==item.get('owner_id')),'Ręczne')
        out.append(f'<tr><td>{esc(item["name"])}</td><td>{item["qty"]:g}</td><td>{esc(owner)}</td><td>{"Wzorzec roboczy — do sprawdzenia" if item.get("owner_id") else "Ręczne"}</td></tr>')
    out.append('</table>')
    out.append('<h2>Układ rozkroju</h2><p>Heurystyka gilotynowa; nie gwarantuje minimum płyt. Grupowanie według materiału i grubości. Kierunek włókien wzdłuż U. Rzaz '+f'{project.kerf:g} mm; margines {project.margin:g} mm.</p>')
    for i,s in enumerate(sheets):
        out.append(f'<h3>Płyta {i+1}: {esc(project.mat(s["material"]).name)}, {s["thickness"]:g} mm</h3><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {s["u"]} {s["v"]}"><rect width="{s["u"]}" height="{s["v"]}" fill="#f2f4f7" stroke="#333"/>')
        for n,r in enumerate(s['pieces']):
            p=next(p for p in project.parts if p.id==r['id'])
            out.append(f'<rect x="{r["x"]}" y="{r["y"]}" width="{r["u"]}" height="{r["v"]}" fill="#c4a276" stroke="#444"/><text x="{r["x"]+8}" y="{r["y"]+35}" font-size="28">{n+1}</text>')
        out.append('</svg><p>'+ '; '.join(f'{n+1}: {esc(next(p.name for p in project.parts if p.id==r["id"]))} {r["u"]:g}×{r["v"]:g}' for n,r in enumerate(s['pieces']))+'</p>')
    if bad:out.append('<p>Nie mieszczą się na płycie: '+', '.join(esc(next(p.name for p in project.parts if p.id==i)) for i in bad)+'</p>')
    out.append('<h2>Kosztorys orientacyjny</h2><table>')
    for label,key in [('Materiał według powierzchni netto','panels'),('Obrzeża','edges'),('Akcesoria','hardware'),('Robocizna','labor'),('Koszt bazowy','base'),(f'Cena z narzutem {project.markup:g}%','total')]:
        out.append(f'<tr><td>{label}</td><td>{c[key]:.2f} zł</td></tr>')
    stock=sum(project.mat(s['material']).cost(Part(u=s['u'],v=s['v'],t=s['thickness'])) for s in sheets)
    out.append(f'</table><p>Zakup pełnych płyt z układu: {stock:.2f} zł. Kosztorys netto powierzchni nie obejmuje odpadów, podatku, transportu ani dodatkowej obróbki.</p><p>DXF zawiera nominalne kontury i koła otworów, bez głębokości, kompensacji narzędzia ani programu CNC. Model nie sprawdza wytrzymałości i wszystkich kolizji. Przed produkcją zweryfikuj konstrukcję oraz dokumentację.</p></html>')
    Path(path).write_text(''.join(out),encoding='utf-8')


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_core as _install_cpp
_install_cpp(globals())
