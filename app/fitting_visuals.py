"""Simple fitting geometry for visualisation; purchased items, not manufactured panels."""
from core import world,rotate,local_world,layout_point

def box(x,y,z,u,v,t):
    points=[(x+a*u,y+b*v,z+c*t) for a,b,c in ((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1))]
    return [[points[i] for i in inds] for inds in ((0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7))]

def fitting_faces(project,kinds=None,*,with_metadata=False):
    out=[];owners={p.id:p for p in project.parts}
    def append(owner,faces,color,construction=False,sign=None):
        if construction:faces=[[layout_point(owners[owner],point) for point in face] for face in faces]
        if sign is None:sign=1 if construction or owners[owner].plane!='XZ' else -1
        for face in faces:out.append((owner,face,color,sign) if with_metadata else (owner,face,color))
    for h in project.hardware:
        if kinds is not None and h.get('kind') not in kinds:continue
        p=owners.get(h.get('owner_id'))
        if not p or not p.visible:continue
        kind=h.get('kind');color=h.get('color','#aab0b6')
        if kind=='handle':
            u,v=h['u'],h['v'];spacing=h['spacing'];projection=h['projection'];vertical=h['vertical']
            local=[]
            if spacing:
                for k in (-spacing/2,spacing/2):local+=box(u-4+(0 if vertical else k),v-4+(k if vertical else 0),-projection,8,8,projection)
                local+=box(u-5 if vertical else u-spacing/2-5,v-spacing/2-5 if vertical else v-5,-projection-6,10 if vertical else spacing+10,spacing+10 if vertical else 10,8)
            else:local=box(u-10,v-10,-projection,20,20,projection)
            if h.get('mount_face','bottom')=='top':local=[[(u,v,p.t-w) for u,v,w in reversed(face)] for face in local]
            append(p.id,[[world(p,*point) for point in face] for face in local],color)
        elif kind=='opening':
            from opening_options import opening_faces
            append(p.id,opening_faces(project,p,h),color,sign=0)
        elif kind=='leg':
            height=h.get('height',0);diameter=h.get('diameter',40)
            if height<=0 or diameter<=0:continue
            foot_height=min(6,height/4)
            for position in h.get('positions',[]):
                x,y,z=(position[k] for k in ('x','y','z'))
                geometry=cylinder(x,y,z,h.get('foot_diameter',diameter*1.15)/2,foot_height)
                geometry+=cylinder(x,y,z+foot_height,diameter/2,height-foot_height)
                append(p.id,geometry,color,True)
        elif kind=='led_strip':
            from configurator_accessories import led_local_geometry
            for face,finish in led_local_geometry(p,h):
                append(p.id,[[world(p,*point) for point in face]],finish)
        elif kind=='hinge':
            cfg=p.assembly.get('config',{});side=cfg.get('hinge_side','left');cup=cfg.get('cup_edge',5)+17.5;u=cup if side=='left' else p.u-cup
            for v in h.get('levels',[]):
                local=box(u-14,v-16,p.t,28,32,3)+box(max(0,u-15 if side=='left' else u),v-6,p.t,15,12,12)
                if h.get('mount_face','top')=='bottom':local=[[(u,v,p.t-w) for u,v,w in reversed(face)] for face in local]
                append(p.id,[[world(p,*point) for point in face] for face in local],color)
        elif kind=='runner':
            for position in h.get('positions',[]):
                append(p.id,box(position['x']-5,position['y'],position['z']-15,10,h.get('length',400),30),color,True)
                shift=p.y-h.get('closed_front_y',p.y)
                append(p.id,box(position['x']-3,position['y']+shift,position['z']-8,6,h.get('length',400),16),'#6c747d',True)
        elif kind=='sliding':
            x,y,z=h['origin'];w=h['width']
            for top in (z,z+h['height']+90):
                for lane in (0,22):append(p.id,box(x,y-45-lane,top,w,6,8),color,True)
        elif kind=='cargo':
            x,y,z=h['origin'];w,d,height=h['width'],h['depth'],h['height'];y+=p.y-h.get('closed_y',p.y)
            if min(w,d,height)<=0:continue
            for i in range(h.get('baskets',3)):
                level=z+i*height/max(1,h.get('baskets',3)-1)
                geometry=box(x,y,level,w,d,4)
                for a in (x,x+w-4):geometry+=box(a,y,level,4,d,70)
                for b in (y,y+d-4):geometry+=box(x,b,level,w,4,70)
                append(p.id,geometry,color,True)
        elif kind=='lift':
            cfg=p.assembly.get('config',{});x,y,z=h['origin']
            for a in (x,x+h['width']-15):
                append(p.id,box(a,y,z+h['height']-80,15,100,80),color,True)
                target=local_world(p,20 if a==x else p.u-20,p.v-60,p.t)
                append(p.id,rod((a,y,z+h['height']-40),target,4),color,True)
    return out


def cylinder(x,y,z,r,height,segments=16):
    """Lightweight circular bought fitting; no CAD part or cutting blank."""
    import math
    if r<=0 or height<=0:return []
    rings=[[(x+r*math.cos(i*math.tau/segments),y+r*math.sin(i*math.tau/segments),z+level)
            for i in range(segments)] for level in (0,height)]
    return [rings[0][::-1],rings[1]]+[[rings[0][i],rings[0][(i+1)%segments],
        rings[1][(i+1)%segments],rings[1][i]] for i in range(segments)]


def slot_prism(u,v,z,length,width,height,vertical=False,segments=8):
    """Capsule prism in a fitting's local U,V,T, matching a slot-shaped groove."""
    import math
    radius=width/2;half=(length-width)/2
    if radius<=0 or height<=0 or half<0:return []
    outline=[]
    for center,start in ((half,-math.pi/2),(-half,math.pi/2)):
        for i in range(segments+1):
            angle=start+i*math.pi/segments
            a,b=center+radius*math.cos(angle),radius*math.sin(angle)
            outline.append((u-b,v+a) if vertical else (u+a,v+b))
    rings=[[(a,b,z+level) for a,b in outline] for level in (0,height)]
    n=len(outline)
    return [rings[0][::-1],rings[1]]+[[rings[0][i],rings[0][(i+1)%n],rings[1][(i+1)%n],rings[1][i]] for i in range(n)]


def rod(a,b,r):
    import math
    delta=[b[i]-a[i] for i in range(3)];length=math.sqrt(sum(x*x for x in delta))
    if length<1e-6:return []
    direction=[x/length for x in delta];up=(0,0,1) if abs(direction[2])<.9 else (1,0,0)
    u=[direction[1]*up[2]-direction[2]*up[1],direction[2]*up[0]-direction[0]*up[2],direction[0]*up[1]-direction[1]*up[0]];scale=math.sqrt(sum(x*x for x in u));u=[x/scale for x in u]
    v=[direction[1]*u[2]-direction[2]*u[1],direction[2]*u[0]-direction[0]*u[2],direction[0]*u[1]-direction[1]*u[0]]
    rings=[[tuple(point[i]+r*(u[i]*su+v[i]*sv) for i in range(3)) for su,sv in ((-1,-1),(1,-1),(1,1),(-1,1))] for point in (a,b)]
    return [rings[0][::-1],rings[1]]+[[rings[0][i],rings[0][(i+1)%4],rings[1][(i+1)%4],rings[1][i]] for i in range(4)]
