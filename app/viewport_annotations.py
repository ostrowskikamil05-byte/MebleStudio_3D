"""Sparse, view-dependent dimensions and depth-tested manufacturing marks."""
import itertools
import math
from core import world, local_world, layout_point, explosion_shift
from depth_raster import screen_visibility

MODES=('Wybrany mebel','Wybrany element','Cały zestaw')


def project_local(app,part,point,display=None):
    shift=explosion_shift(part,app.camera,.22 if app.explode.get() else 0)
    xyz=world(part,*point)
    return app.camera.project(tuple(xyz[i]+shift[i] for i in range(3)),app.canvas.winfo_width(),app.canvas.winfo_height())


def _box_edges(lo,hi,transform,camera,size,canvas):
    """One exposed edge per measurable axis, avoiding end-on dimensions."""
    result=[]
    for axis in range(3):
        candidates=[]
        other=[i for i in range(3) if i!=axis]
        for ends in itertools.product((0,1),repeat=2):
            a=list(lo);b=list(lo);b[axis]=hi[axis]
            for i,end in zip(other,ends):a[i]=b[i]=hi[i] if end else lo[i]
            qa=camera.project(transform(tuple(a)),*size);qb=camera.project(transform(tuple(b)),*size)
            length=math.hypot(qb[0]-qa[0],qb[1]-qa[1])
            if length<24:continue
            visible=sum(screen_visibility(canvas,qa[0]+(qb[0]-qa[0])*t,qa[1]+(qb[1]-qa[1])*t,qa[2]+(qb[2]-qa[2])*t,tolerance=2) for t in (.03,.25,.5,.75,.97))
            if visible>=2:candidates.append((visible,(qa[2]+qb[2])/2,qa,qb))
        if candidates:
            _,_,qa,qb=max(candidates,key=lambda c:(c[0],c[1]))
            result.append(dict(axis=axis,a=qa,b=qb,value=hi[axis]-lo[axis]))
    return result


def dimension_specs(app,display):
    selected=next((p for p in display.parts if p.id==app.selected and p.visible),None)
    mode=app.dimension_mode.get()
    if app.explode.get() or mode=='Wybrany element':
        if not selected:return []
        transform=lambda point:world(selected,*point)
        shift=explosion_shift(selected,app.camera,.22 if app.explode.get() else 0)
        transform=lambda point:tuple(world(selected,*point)[i]+shift[i] for i in range(3))
        specs=_box_edges((0,0,0),(selected.u,selected.v,selected.t),transform,app.camera,(app.canvas.winfo_width(),app.canvas.winfo_height()),app.canvas)
        for spec in specs:spec['label']='UVT'[spec['axis']]
        return specs
    parts=[p for p in display.parts if p.visible and (mode=='Cały zestaw' or selected and p.group==selected.group)]
    if not parts:return []
    if mode=='Cały zestaw':
        points=[world(p,u,v,w) for p in parts for u,v in p.points() for w in (0,p.t)]
        from configurator_accessories import accessory_vertices
        points.extend(accessory_vertices(display))
        transform=lambda point:point
    else:
        from furniture_configurator import read_config
        cfg=read_config(app.project,selected.group)
        if not cfg:
            from corner_furniture import read_corner_config
            cfg=read_corner_config(app.project,selected.group)
        if cfg:
            ox,oy,oz=cfg['origin']
            points=[(ox+x*cfg['width'],oy+y*cfg['depth'],oz+z*cfg['height']) for x,y,z in itertools.product((0,1),repeat=3)]
        else:points=[local_world(p,u,v,w) for p in parts for u,v in p.points() for w in (0,p.t)]
        transform=lambda point:layout_point(selected,point)
    lo=tuple(min(q[i] for q in points) for i in range(3));hi=tuple(max(q[i] for q in points) for i in range(3))
    specs=_box_edges(lo,hi,transform,app.camera,(app.canvas.winfo_width(),app.canvas.winfo_height()),app.canvas)
    for spec in specs:spec['label']='X Y Z'.split()[spec['axis']] if mode=='Cały zestaw' else ('S','G','H')[spec['axis']]
    return specs


def paint_dimensions(app,display):
    app.visible_dimensions=[]
    if not app.dimensions.get():return
    c=app.canvas;size=(c.winfo_width(),c.winfo_height());specs=dimension_specs(app,display)
    if not specs:return
    group=app.selected_part().group if app.selected_part() else None
    selected_ids={p.id for p in display.parts if p.group==group}
    points=[f['points'] for f in app.drawn if f['id']==app.selected or f['id'] in selected_ids]
    flat=[q for face in points for q in face]
    center=(sum(q[0] for q in flat)/len(flat),sum(q[1] for q in flat)/len(flat)) if flat else (size[0]/2,size[1]/2)
    placed=[]
    for spec in specs:
        a,b=spec['a'],spec['b'];mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2
        dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy);nx,ny=-dy/length,dx/length
        if nx*(mx-center[0])+ny*(my-center[1])<0:nx,ny=-nx,-ny
        offset=32
        x,y=mx+nx*offset,my+ny*offset
        text=f'{spec["label"]} {spec["value"]:g} mm';half=max(40,len(text)*3.4)
        for _ in range(6):
            if not any(abs(x-px)<half+ph+8 and abs(y-py)<24 for px,py,ph in placed):break
            offset+=23;x,y=mx+nx*offset,my+ny*offset
        x=max(half+5,min(size[0]-half-5,x));y=max(45,min(size[1]-22,y));placed.append((x,y,half))
        ax,ay=a[0]+nx*offset,a[1]+ny*offset;bx,by=b[0]+nx*offset,b[1]+ny*offset
        c.create_line(a[0],a[1],ax,ay,fill='#23889c',dash=(2,3),tags='dimensions')
        c.create_line(b[0],b[1],bx,by,fill='#23889c',dash=(2,3),tags='dimensions')
        c.create_line(ax,ay,bx,by,fill='#137b94',width=1,arrow='both',tags='dimensions')
        c.create_rectangle(x-half,y-11,x+half,y+11,fill='#eefbff',outline='#91c6d2',tags='dimensions')
        c.create_text(x,y,text=text,fill='#123c49',font=('Segoe UI',9,'bold'),tags='dimensions')
        app.visible_dimensions.append(dict(spec,x=x,y=y,text=text))
    if app.explode.get() and app.selected_part() and app.dimension_mode.get()!='Wybrany element':
        group=app.selected_part().group;parts=[p for p in display.parts if p.group==group and p.visible]
        rows=max(1,min(12,(size[1]-135)//23));visible=[]
        for part in parts:
            q=project_local(app,part,(part.u/2,part.v/2,part.t/2))
            # A label table is outside the model. No text is drawn over every
            # shelf of every cabinet, so exploded assemblies remain legible.
            visible.append((part,q))
        x=size[0]-260;y=55
        c.create_rectangle(x,y,size[0]-8,y+46+min(rows,len(visible))*23,fill='#f6fbff',outline='#a7c5d5',tags='dimensions')
        c.create_text(x+10,y+13,text='Formatki wybranego mebla [mm]',anchor='w',fill='#123c49',font=('Segoe UI',9,'bold'),tags='dimensions')
        for n,(part,q) in enumerate(visible[:rows],1):
            c.create_text(x+10,y+16+n*23,text=f'{n}. {part.name[:18]} · {part.u:g}×{part.v:g}×{part.t:g}',anchor='w',fill='#25394c',font=('Segoe UI',9),tags='dimensions')
            if screen_visibility(c,*q,tolerance=part.t):
                c.create_oval(q[0]-8,q[1]-8,q[0]+8,q[1]+8,fill='#eefbff',outline='#137b94',tags='dimensions')
                c.create_text(q[0],q[1],text=str(n),fill='#123c49',font=('Segoe UI',8,'bold'),tags='dimensions')
        footer='Wszystkie formatki: zakładka Formatki / Drukuj' if len(visible)>rows else 'Wymiary nominalne U × V × grubość'
        c.create_text(x+10,y+34+min(rows,len(visible))*23,text=footer,anchor='w',fill='#56717f',font=('Segoe UI',8),tags='dimensions')


def paint_drills(app,display):
    app.visible_drills=[]
    if not app.show_drills.get() or getattr(app,'_view_interactive',False):return
    from features import drill_location
    c=app.canvas
    mode=getattr(app,'drill_mode',None)
    through=bool(mode and mode.get()=='Także zasłonięte')
    if through:
        c.create_text(12,c.winfo_height()-16,text='Otwory: wszystkie strony · niebieskie przerywane = zasłonięte',anchor='sw',fill='#3476b3',font=('Segoe UI',9,'bold'),tags='mounting_holes')
    for part in display.parts:
        if not part.visible:continue
        operations=list(part.operations)+[dict(type='drill',face='top',u=u,v=v,diameter=d,depth=h,purpose='otwór ręczny') for u,v,d,h in part.holes]
        for operation in operations:
            if operation.get('type')!='drill':continue
            origin,direction=drill_location(part,operation);q=project_local(app,part,origin)
            if not (0<=q[0]<c.winfo_width() and 0<=q[1]<c.winfo_height()):continue
            outward=tuple(origin[i]-direction[i] for i in range(3));qn=project_local(app,part,outward)
            exposed=qn[2]-q[2]>=.01 and screen_visibility(c,*q,tolerance=1.5)
            if not through and not exposed:continue
            color='#287fc2' if exposed else '#4487bc'
            if through:
                # A minimum-size target is a legible annotation, not a change
                # to drill diameter. Hover retains the exact CNC dimensions.
                radius=max(5,min(12,operation['diameter']*app.camera.scale/2))
                dash=() if exposed else (3,2)
                c.create_oval(q[0]-radius,q[1]-radius,q[0]+radius,q[1]+radius,outline='white',width=5,tags='mounting_holes')
                c.create_oval(q[0]-radius,q[1]-radius,q[0]+radius,q[1]+radius,outline=color,width=3,dash=dash,tags='mounting_holes')
                c.create_line(q[0]-4,q[1],q[0]+4,q[1],fill=color,width=2,tags='mounting_holes')
                c.create_line(q[0],q[1]-4,q[0],q[1]+4,fill=color,width=2,tags='mounting_holes')
                app.visible_drills.append(dict(part_id=part.id,x=q[0],y=q[1],operation=operation,hidden=not exposed))
                continue
            axes=(0,1) if operation['face'] in ('top','bottom') else (1,2) if operation['face'].startswith('u') else (0,2)
            ring=[];radius=operation['diameter']/2
            for i in range(25):
                point=list(origin);angle=i*math.tau/24
                point[axes[0]]+=radius*math.cos(angle);point[axes[1]]+=radius*math.sin(angle)
                ring.append(project_local(app,part,point))
            for a,b in zip(ring,ring[1:]):
                mid=tuple((a[i]+b[i])/2 for i in range(3))
                if screen_visibility(c,*mid,tolerance=1.5):
                    c.create_line(a[0],a[1],b[0],b[1],fill='white',width=5,tags='mounting_holes')
                    c.create_line(a[0],a[1],b[0],b[1],fill=color,width=3,tags='mounting_holes')
            radius=5
            c.create_oval(q[0]-radius,q[1]-radius,q[0]+radius,q[1]+radius,outline='white',width=4,tags='mounting_holes')
            c.create_line(q[0]-4,q[1],q[0]+4,q[1],fill=color,width=2,tags='mounting_holes')
            c.create_line(q[0],q[1]-4,q[0],q[1]+4,fill=color,width=2,tags='mounting_holes')
            app.visible_drills.append(dict(part_id=part.id,x=q[0],y=q[1],operation=operation,hidden=False))


def paint(app,display):
    paint_dimensions(app,display);paint_drills(app,display)
