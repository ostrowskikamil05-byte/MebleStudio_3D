"""Independent part transforms, arrays and reusable furniture project templates."""
import copy
import math
from dataclasses import asdict
from core import Project,Part,uid,finite,rotate

def independent_copy(part):
    q=copy.deepcopy(part);q.id=uid();q.assembly={};q.role='other';q.style_source={};q.configurator_source={}
    q.operations=[{key:value for key,value in op.items() if key not in ('owner_id','opening_owner','corner_hinge_owner','attachment_id')} for op in q.operations]
    q.machining=[{key:value for key,value in cut.items() if key not in ('opening_owner','attachment_id')} for cut in q.machining]
    return q

def part_array(project,part_id,count,dx,dy,dz):
    if int(count)!=count or not 1<=count<=100:raise ValueError('Liczba kopii: od 1 do 100.')
    dx,dy,dz=(finite(v) for v in (dx,dy,dz))
    if (dx,dy,dz)==(0,0,0):raise ValueError('Podaj niezerowy krok szyku.')
    result=copy.deepcopy(project);part=next(p for p in result.parts if p.id==part_id)
    dx,dy,dz=rotate((dx,dy,dz),0,0,-part.layout.get('yaw',0))
    for i in range(1,int(count)+1):
        q=independent_copy(part);q.name+=f' — kopia {i}';q.x+=i*dx;q.y+=i*dy;q.z+=i*dz;result.parts.append(q)
    result.validate();return result

def mirror_part(part,axis):
    if axis not in ('u','v'):raise ValueError('Odbicie w osi lokalnej U lub V.')
    if part.profile and (part.fillets or part.chamfers):
        raise ValueError('Przed odbiciem obłego obrysu usuń jego fazy i zaokrąglenia kantów w edytorze CAD; odbicie zmienia numerację krawędzi.')
    from cad_geometry import edge_specs
    q=independent_copy(part);q.name+=' — odbicie';q.profile={};q.decor={};q.style_source={}
    def reflect(point):return (part.u-point[0],*point[1:]) if axis=='u' else (point[0],part.v-point[1],*point[2:])
    if part.contour:q.contour=[reflect(point) for point in part.points()]
    else:
        q.edges=part.edges[:]
        a,b=(0,1) if axis=='u' else (2,3);q.edges[a],q.edges[b]=q.edges[b],q.edges[a]
    for index,hole in enumerate(q.holes):
        a,b,d,h=hole;q.holes[index]=[part.u-a if axis=='u' else a,part.v-b if axis=='v' else b,d,h]
    for op in q.operations:
        face=op['face']
        if face in ('top','bottom'):op[axis]=getattr(part,axis)-op[axis]
        elif face.startswith(axis):op['face']=axis+('1' if face.endswith('0') else '0')
        else:op['u']=getattr(part,axis)-op['u']
    for cut in q.machining:
        cut[axis]=getattr(part,axis)-cut[axis];cut['angle']=(-cut.get('angle',0))%180
    new_specs=edge_specs(q.points(),q.t)
    old_specs={item[0]:item for item in edge_specs(part.points(),part.t)}
    for collection in (q.fillets,q.chamfers):
        for feature in collection:
            old=old_specs[feature['edge']];a,b=reflect(old[2]),reflect(old[3])
            feature['edge']=next(item[0] for item in new_specs if
                (math.dist(a,item[2])<1e-6 and math.dist(b,item[3])<1e-6) or
                (math.dist(b,item[2])<1e-6 and math.dist(a,item[3])<1e-6))
    q.validate();return q

def extract_group(project,group):
    result=copy.deepcopy(project);result.name=group;result.parts=[p for p in result.parts if p.group==group]
    ids={p.id for p in result.parts};result.hardware=[h for h in result.hardware if h.get('owner_id') in ids]
    for p in result.parts:
        source=p.assembly.get('shared_worktop')
        if source and any(anchor not in ids for anchor in source.get('anchors',[])):p.assembly.pop('shared_worktop')
        if any(i not in ids for i in p.assembly.get('anchors',[])):raise ValueError('Mebel ma powiązania z inną grupą. Odłącz je przed zapisem szablonu.')
    result.validate();return result

def insert_template(project,template,name,offset=(0,0,0)):
    if not name.strip():raise ValueError('Podaj nazwę wstawianego mebla.')
    offset=tuple(finite(v) for v in offset);result=copy.deepcopy(project)
    template=copy.deepcopy(template);template.validate()
    ids={p.id:uid() for p in template.parts};material_ids={}
    # One new purchase attachment per copied installation, shared by its
    # hardware rows and manufacturing features but independent of the source.
    def attachment_values(value):
        if isinstance(value,dict):
            for key,item in value.items():
                if key=='attachment_id' and isinstance(item,str):yield item
                else:yield from attachment_values(item)
        elif isinstance(value,(list,tuple)):
            for item in value:yield from attachment_values(item)
    attachments={value:uid() for value in set(attachment_values([
        template.hardware,*[dict(operations=p.operations,machining=p.machining,assembly=p.assembly,source=p.configurator_source) for p in template.parts]]))}
    def remap_attachments(value):
        if isinstance(value,dict):return {key:attachments.get(item,item) if key=='attachment_id' and isinstance(item,str) else remap_attachments(item) for key,item in value.items()}
        if isinstance(value,list):return [remap_attachments(item) for item in value]
        if isinstance(value,tuple):return tuple(remap_attachments(item) for item in value)
        return value
    for p in template.parts:
        p.operations=remap_attachments(p.operations);p.machining=remap_attachments(p.machining)
        p.assembly=remap_attachments(p.assembly);p.configurator_source=remap_attachments(p.configurator_source)
    template.hardware=remap_attachments(template.hardware)
    by_old_id={p.id:p for p in template.parts};verified_manifests=set();verified_worktops=set()
    from furniture_configurator import _geometry
    from workspace_worktops import _signature as worktop_signature
    for p in template.parts:
        manifest=p.configurator_source.get('manifest') or p.assembly.get('corner_source',{}).get('manifest')
        if manifest and all(entry.get('id') in by_old_id and _geometry(by_old_id[entry['id']])==entry.get('signature') for entry in manifest.values()):verified_manifests.add(p.id)
        source=p.assembly.get('shared_worktop')
        if source and source.get('signature')==worktop_signature(p):verified_worktops.add(p.id)
    for material in template.materials:
        data=asdict(material);data.pop('id')
        match=next((m for m in result.materials if {k:v for k,v in asdict(m).items() if k!='id'}==data),None)
        if not match:
            match=copy.deepcopy(material);match.id=uid()
            occupied={m.name for m in result.materials}
            if match.name in occupied:match.name+=' — import '+match.id[:4]
            result.materials.append(match)
        material_ids[material.id]=match.id
    groups=list(dict.fromkeys(p.group for p in template.parts));group_names={}
    occupied={p.group for p in result.parts}
    for group in groups:
        target=name if len(groups)==1 else name+' / '+group
        base=target;number=2
        while target in occupied:target=base+f' ({number})';number+=1
        occupied.add(target);group_names[group]=target
    placed_ids={p.id for p in template.parts if p.layout}
    def remap(value):
        if isinstance(value,dict):return {k:remap(v) for k,v in value.items()}
        if isinstance(value,list):return [remap(v) for v in value]
        if isinstance(value,tuple):return tuple(remap(v) for v in value)
        if isinstance(value,str):return ids.get(value,value)
        return value
    for p in template.parts:
        old=p.id;p.id=ids[old];p.material=material_ids[p.material];p.group=group_names[p.group]
        if old in placed_ids:
            for axis,value in zip(('x','y','z'),offset):p.layout[axis]=p.layout.get(axis,0)+value
        else:p.x+=offset[0];p.y+=offset[1];p.z+=offset[2]
        if p.style_source:
            if old not in placed_ids:p.style_source['origin']=[a+b for a,b in zip(p.style_source['origin'],offset)]
            for key in ('material','front_material'):
                if p.style_source.get(key):p.style_source[key]=material_ids[p.style_source[key]]
        if p.configurator_source:
            p.configurator_source=remap(p.configurator_source)
            cfg=p.configurator_source.get('config')
            if cfg:
                cfg['name']=p.group
                if old not in placed_ids:cfg['origin']=[a+b for a,b in zip(cfg['origin'],offset)]
                for key in ('material','front_material'):
                    if cfg.get(key):cfg[key]=material_ids[cfg[key]]
        if p.assembly.get('corner_source'):
            p.assembly['corner_source']=remap(p.assembly['corner_source'])
            cfg=p.assembly['corner_source'].get('config')
            if cfg:
                cfg['name']=p.group
                if old not in placed_ids:cfg['origin']=[a+b for a,b in zip(cfg['origin'],offset)]
                if cfg.get('material'):cfg['material']=material_ids[cfg['material']]
                if cfg.get('front_material'):cfg['front_material']=material_ids[cfg['front_material']]
        source=p.assembly.get('shared_worktop')
        if source:
            if all(anchor in ids for anchor in source.get('anchors',[])):
                p.assembly['shared_worktop']=remap(source)
                if old in verified_worktops:p.assembly['shared_worktop']['signature']=worktop_signature(p)
            else:p.assembly.pop('shared_worktop')
        if p.assembly.get('config',{}).get('bay'):
            p.assembly['config']['bay']=remap(p.assembly['config']['bay'])
        if p.assembly.get('config',{}).get('corner_bay'):
            bay=remap(p.assembly['config']['corner_bay'])
            if old not in placed_ids:bay['hinge_point']=[a+b for a,b in zip(bay['hinge_point'],offset[:2])]
            p.assembly['config']['corner_bay']=bay
        for key in ('anchors','children'):
            if key in p.assembly:p.assembly[key]=[ids[i] for i in p.assembly[key]]
        if 'generated_by' in p.assembly:p.assembly['generated_by']=ids[p.assembly['generated_by']]
        if 'group' in p.assembly:p.assembly['group']=p.group
        for op in p.operations:
            if op.get('connection_tag'):op['connection_tag']='connector:'+p.group
            if op.get('owner_id'):op['owner_id']=ids[op['owner_id']]
            if op.get('opening_owner'):op['opening_owner']=ids[op['opening_owner']]
            if op.get('corner_hinge_owner'):op['corner_hinge_owner']=ids[op['corner_hinge_owner']]
        for cut in p.machining:
            if cut.get('opening_owner'):cut['opening_owner']=ids[cut['opening_owner']]
        result.parts.append(p)
    for item in template.hardware:
        placement_offset=(0,0,0) if item.get('owner_id') in placed_ids else offset
        if item.get('group') in group_names:item['group']=group_names[item['group']]
        if item.get('owner_id'):item['owner_id']=ids[item['owner_id']]
        if 'anchors' in item:item['anchors']=[ids[i] for i in item['anchors']]
        if 'origin' in item:item['origin']=[a+b for a,b in zip(item['origin'],placement_offset)]
        if 'closed_y' in item:item['closed_y']+=placement_offset[1]
        if 'closed_front_y' in item:item['closed_front_y']+=placement_offset[1]
        if 'connection_tag' in item:
            owner=next((p for p in result.parts if p.id==item.get('owner_id')),None)
            if owner:item['connection_tag']='connector:'+owner.group
        for position in item.get('positions',[]):
            for axis,value in zip(('x','y','z'),placement_offset):position[axis]+=value
        result.hardware.append(item)
    configured=[p for p in result.parts if p.id in {ids[key] for key in verified_manifests} and (p.configurator_source.get('manifest') or p.assembly.get('corner_source',{}).get('manifest'))]
    if configured:
        from furniture_configurator import _geometry
        by_id={p.id:p for p in result.parts}
        for p in configured:
            manifest=p.configurator_source.get('manifest') or p.assembly['corner_source']['manifest']
            for entry in manifest.values():
                entry['signature']=_geometry(by_id[entry['id']])
    from workspace_worktops import refresh_shared_worktops
    result=refresh_shared_worktops(result,validate=False);result.validate();return result

def equal_shelves(project,group,count,thickness,material):
    from assembly import frame,attach,suggested_size
    if int(count)!=count or not 1<=count<=30:raise ValueError('Liczba półek: od 1 do 30.')
    f=frame(project,group);clear_height=f['inner_top']-f['inner_bottom']
    if clear_height<=count*thickness:raise ValueError('Brak miejsca na półki.')
    result=copy.deepcopy(project);space=(clear_height-count*thickness)/(count+1)
    for i in range(int(count)):
        level=f['inner_bottom']-f['lower'][2]+space+i*(space+thickness)
        u,v=suggested_size(result,group,'shelf',thickness)
        result,_=attach(result,Part(name=f'Półka automatyczna {i+1}',u=u,v=v,t=thickness,material=material),group,'shelf',{'level':level})
    return result
