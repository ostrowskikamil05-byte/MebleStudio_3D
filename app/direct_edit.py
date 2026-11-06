"""Atomic direct edits and furniture resizing with thickness preservation."""
import copy
from core import finite,local_world
from assembly import attach,rebuild_dependents,frame,inferred_role,DEFAULTS,clear_generated

def resize_part(project,part_id,face,distance):
    temp=copy.deepcopy(project);p=next(p for p in temp.parts if p.id==part_id)
    if p.assembly.get('generated_by'):raise ValueError('Edytuj front szuflady; skrzynka jest generowana z jej parametrów.')
    distance=finite(distance);axis=face[0];minimum=face.endswith('0')
    key={'u':'u','v':'v','t':'t'}[axis];old=getattr(p,key);new=finite(old+(-distance if minimum else distance),.1,10000)
    if axis in ('u','v') and p.contour:
        from shaped_furniture import resize_profile
        resize_profile(p,new if axis=='u' else p.u,new if axis=='v' else p.v)
    if minimum:
        local={'u':(distance,0,0),'v':(0,distance,0),'t':(0,0,distance)}[axis]
        p.x,p.y,p.z=local_world(p,*local)
        if axis in ('u','v'):
            index=0 if axis=='u' else 1
            p.holes=[tuple(value-distance if i==index else value for i,value in enumerate(hole)) for hole in p.holes]
        from features import shift_features
        shift_features(p,*local)
    setattr(p,key,new)
    if p.assembly and not p.assembly.get('corner_source'):
        p.operations=[op for op in p.operations if op.get('owner_id')!=p.id]
        temp,p=attach(temp,p,p.group,p.role,p.assembly.get('config'))
    else:
        from fittings import refresh_handles
        from opening_options import refresh_openings
        refresh_handles(temp,p.id);refresh_openings(temp,p.id);p.validate()
    temp=rebuild_dependents(temp,part_id)
    from drag_hardware import refresh_manual_attachments
    refresh_manual_attachments(temp)
    temp.validate();return temp

def resize_furniture(project,group,width,height,depth,*,origin=None):
    from corner_furniture import resize_corner_furniture
    corner=resize_corner_furniture(project,group,width,height,depth,origin=origin)
    if corner is not None:return corner
    from furniture_configurator import read_config,configure
    config=read_config(project,group)
    if config:
        config['width']=finite(width,.1,10000);config['depth']=finite(depth,.1,10000)
        config['height']=finite(height,.1,10000)+config['leg_height']+(config['worktop_thickness'] if config.get('worktop') else 0)
        return configure(project,config,group,origin=origin)[0]
    from shaped_furniture import resize_shaped_furniture
    shaped=resize_shaped_furniture(project,group,width,height,depth)
    if shaped is not None:return shaped
    original=frame(project,group);lo,hi=original['lower'],original['upper']
    sizes=[finite(value,.1,10000) for value in (width,depth,height)]
    ratios=[sizes[i]/(hi[i]-lo[i]) for i in range(3)]
    temp=copy.deepcopy(project)
    retained_hardware={p.id:[copy.deepcopy(h) for h in temp.hardware if h.get('owner_id')==p.id and h.get('manual')] for p in temp.parts}
    connections=[copy.deepcopy(h) for h in temp.hardware if h.get('kind')=='connector' and h.get('connection_tag')=='connector:'+group]
    if connections:
        for p in temp.parts:p.operations=[op for op in p.operations if op.get('connection_tag')!='connector:'+group]
        temp.hardware=[h for h in temp.hardware if h.get('connection_tag')!='connector:'+group]
    managed=[copy.deepcopy(p) for p in temp.parts if p.group==group and p.assembly.get('role') in ('door','shelf','drawer','back','worktop')]
    for p in managed:clear_generated(temp,p.id)
    managed_ids={p.id for p in managed}
    temp.parts=[p for p in temp.parts if p.id not in managed_ids]
    for p in temp.parts:
        if p.group!=group:continue
        if any(abs(angle)>1e-8 for angle in (p.rx,p.ry,p.rz)):raise ValueError('Zmiana całego mebla wymaga elementów bez obrotów.')
        axes={'XY':(0,1,2),'XZ':(0,2,1),'YZ':(1,2,0)}[p.plane]
        position=[p.x,p.y,p.z];new_position=[lo[i]+(position[i]-lo[i])*ratios[i] for i in range(3)]
        thick_axis=axes[2]
        if abs(position[thick_axis]+p.t-hi[thick_axis])<.01:new_position[thick_axis]=lo[thick_axis]+sizes[thick_axis]-p.t
        dimensions=[]
        for index,key in enumerate(('u','v')):
            axis=axes[index];start=position[axis];end=start+getattr(p,key)
            def anchored(value):
                if abs(value-lo[axis])<50:return lo[axis]+value-lo[axis]
                if abs(hi[axis]-value)<50:return lo[axis]+sizes[axis]-(hi[axis]-value)
                return lo[axis]+(value-lo[axis])*ratios[axis]
            new_position[axis]=anchored(start);dimensions.append(anchored(end)-new_position[axis])
        if min(dimensions)<=0:raise ValueError('Mebel jest za mały dla grubości elementów i zachowanych luzów.')
        if p.contour:
            from shaped_furniture import resize_profile
            resize_profile(p,*dimensions)
        p.u,p.v=dimensions;p.x,p.y,p.z=new_position
        p.operations=[op for op in p.operations if op.get('owner_id') not in managed_ids]
        if p.assembly.get('role'):
            p.assembly['config']['level']*=ratios[2]
            p.assembly['config']['offset']*=ratios[0]
    for p in managed:
        cfg={**DEFAULTS,**p.assembly.get('config',{})}
        cfg['level']*=ratios[2]
        role=p.role
        if cfg.get('bay'):
            from assembly import frame_for_part,suggested_size
            current_frame=frame(temp,group,bay=cfg['bay']);a,b=current_frame['lower'],current_frame['upper'];bay_w=b[0]-a[0];bay_h=b[2]-a[2]
            if role=='door':new_u=(bay_w-(cfg.get('bay_doors',1)+1)*cfg['gap'])/cfg.get('bay_doors',1);new_v=bay_h-2*cfg['gap']
            elif role=='drawer':
                count=cfg.get('bay_drawers',1);new_u=bay_w-2*cfg['gap'];new_v=(bay_h-(count+1)*cfg['gap'])/count
                cfg['level']=cfg['gap']+cfg.get('bay_drawer_index',0)*(new_v+cfg['gap'])
                cfg['drawer_raise']=max(0,current_frame['inner_bottom']-(a[2]+cfg['level']+cfg['gap']))+6
                cfg['drawer_height']=min(cfg['drawer_height'],new_v-cfg['drawer_bottom']-2*cfg['gap']-cfg['drawer_raise'])
            else:
                new_u,new_v=suggested_size(temp,group,role,p.t,cfg)
                if role=='shelf' and cfg.get('bay_shelves'):
                    cfg['level']=current_frame['inner_bottom']-a[2]+(current_frame['inner_top']-current_frame['inner_bottom']-p.t)*cfg.get('bay_shelf_index',1)/(cfg['bay_shelves']+1)
        elif role in ('shelf','top','bottom'):
            new_frame=frame(temp,group)
            new_u=new_frame['inner_right']-new_frame['inner_left']-(2*cfg['gap'] if role=='shelf' else 0)
            new_v=depth-(2*cfg['gap'] if role=='shelf' else 0)
        elif role=='door':
            # Preserve full-width vs split-door proportions and fixed edge gaps.
            old_inner_width=hi[0]-lo[0]-2*cfg['gap']
            new_u=p.u*(width-2*cfg['gap'])/old_inner_width
            new_v=p.v*(height-2*cfg['gap'])/(hi[2]-lo[2]-2*cfg['gap'])
        elif role=='drawer':new_u=width-2*cfg['gap'];new_v=p.v
        elif role in ('left','right'):new_u=depth;new_v=height
        elif role=='back':new_u=width;new_v=height
        elif role=='worktop':new_u=width;new_v=depth
        else:new_u=p.u*ratios[1];new_v=p.v*ratios[2]
        if p.contour:
            from shaped_furniture import resize_profile
            resize_profile(p,new_u,new_v)
        p.u,p.v=new_u,new_v;p.operations=[]
        temp.hardware.extend(retained_hardware.get(p.id,[]))
        temp,p=attach(temp,p,group,role,cfg)
    if connections:
        from fittings import cabinet_connectors
        h=connections[0];temp=cabinet_connectors(temp,group,h.get('recipe_id','confirmat7'),h.get('front',50),h.get('back',50))
    temp.validate();return temp


def resize_furniture_face(project,group,face,distance):
    from shaped_furniture import styled_bounds
    f=styled_bounds(project,group) or frame(project,group);lo,hi=f['lower'],f['upper'];axis='xyz'.index(face[0]);minimum=face.endswith('0');distance=finite(distance)
    size=[hi[i]-lo[i] for i in range(3)];size[axis]+= -distance if minimum else distance
    from corner_furniture import read_corner_config
    corner=read_corner_config(project,group)
    if corner:
        origin=corner['origin'][:]
        if minimum:origin[axis]+=distance
        return resize_furniture(project,group,size[0],size[2],size[1],origin=origin)
    from furniture_configurator import read_config
    config=read_config(project,group)
    if config:
        origin=config['origin'][:]
        if minimum:origin[axis]+=distance
        return resize_furniture(project,group,size[0],size[2],size[1],origin=origin)
    result=resize_furniture(project,group,size[0],size[2],size[1])
    if minimum:
        ids={p.id for p in result.parts if p.group==group}
        for p in result.parts:
            if p.id in ids:
                setattr(p,face[0],getattr(p,face[0])+distance)
                if p.style_source:
                    from shaped_furniture import source_signature
                    p.style_source['origin'][axis]+=distance
                    p.style_source['signature']=source_signature(p,p.style_source['origin'])
        for h in result.hardware:
            if h.get('owner_id') not in ids:continue
            for position in h.get('positions',[]):position[face[0]]+=distance
            if 'origin' in h:h['origin'][axis]+=distance
            if axis==1:
                for key in ('closed_y','closed_front_y'):
                    if key in h:h[key]+=distance
    result.validate();return result
