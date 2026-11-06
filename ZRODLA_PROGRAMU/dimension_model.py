"""Shared dimension edits and fit operations for sketch, forms and 3D."""
import copy
from core import finite
from assembly import inferred_role,suggested_size,attach,rebuild_dependents,DEFAULTS

def resize_dimensions(project,part_id,u,v,t):
    result=copy.deepcopy(project);p=next(p for p in result.parts if p.id==part_id)
    if p.assembly.get('generated_by'):raise ValueError('Wymiary skrzynki zmień przez jej front.')
    u,v,t=[finite(x,.1,10000) for x in (u,v,t)]
    if getattr(p,'profile',None):
        from shaped_furniture import resize_profile
        resize_profile(p,u,v)
    elif p.contour:p.contour=[(a*u/p.u,b*v/p.v) for a,b in p.contour]
    p.u,p.v,p.t=u,v,t
    for op in p.operations:
        if op.get('recipe_id')=='handle':op['depth']=t
    if p.assembly and not p.assembly.get('corner_source'):result,p=attach(result,p,p.group,p.role,p.assembly.get('config'))
    else:
        from fittings import refresh_handles
        from opening_options import refresh_openings
        refresh_handles(result,p.id);refresh_openings(result,p.id);p.validate()
    result=rebuild_dependents(result,p.id);result.validate();return result

def fit_part(project,part_id):
    from assembly import frame_for_part
    p=next(p for p in project.parts if p.id==part_id);role=inferred_role(p)
    if p.assembly.get('corner_source'):
        from corner_furniture import fit_corner_part
        return fit_corner_part(project,part_id)
    if role=='other':raise ValueError('Nadaj elementowi funkcję w szkicu (bok, góra, dół, półka, front), aby dopasować go do korpusu.')
    cfg={**DEFAULTS,**p.assembly.get('config',{})}
    if role=='door':
        f=frame_for_part(project,p);full=f['upper'][0]-f['lower'][0]-2*cfg['gap']
        siblings=[q for q in project.parts if q.group==p.group and q.id!=p.id and inferred_role(q)=='door' and q.assembly.get('config',{}).get('bay')==cfg.get('bay')]
        u,v=suggested_size(project,p.group,role,p.t,cfg)
        if siblings:
            side='right' if p.x>f['lower'][0]+full/3 else 'left';cfg['hinge_side']=side;u=(full-cfg['gap'])/2
    else:u,v=suggested_size(project,p.group,role,p.t,cfg)
    result=copy.deepcopy(project);q=next(q for q in result.parts if q.id==p.id)
    if getattr(q,'profile',None):
        from shaped_furniture import resize_profile
        resize_profile(q,u,v)
    elif q.contour:q.contour=[(a*u/q.u,b*v/q.v) for a,b in q.contour]
    q.u,q.v=u,v
    result,q=attach(result,q,q.group,role,cfg);result=rebuild_dependents(result,q.id);result.validate();return result

def regenerate_hinges(project,part_id,pattern='hinge45',count=2,side=None):
    target=next(p for p in project.parts if p.id==part_id)
    if target.assembly.get('config',{}).get('corner_bay'):
        if side not in (None,'left'):raise ValueError('Krawędź zawiasową narożnika zmień przez kierunek narożnika w kreatorze.')
        from corner_furniture import regenerate_corner_hinges
        return regenerate_corner_hinges(project,part_id,pattern,count)
    if target.assembly.get('config',{}).get('corner_diagonal'):raise ValueError('Front skośny wymaga osobno dobranego zawiasu; automatyczne wiercenia są wyłączone.')
    from fittings import recipe
    from assembly import frame_for_part
    p=copy.deepcopy(next(p for p in project.parts if p.id==part_id));r=recipe(pattern);f=frame_for_part(project,p)
    if pattern=='hinge_sensys_tb55':raise ValueError('Ten zawias jest wpuszczany. Wybierz wiercenie ręczne z katalogu; generator frontów obsługuje nakładane drzwiczki.')
    if not r.get('min_thickness',0)<=p.t<=r.get('max_thickness',10000):raise ValueError('Grubość frontu nie mieści się w zakresie tego zawiasu.')
    side=side or p.assembly.get('config',{}).get('hinge_side') or ('right' if abs(p.x+p.u-f['upper'][0]+2)<1 and p.x>f['lower'][0]+2 else 'left')
    cfg={**DEFAULTS,**p.assembly.get('config',{}),'auto_hardware':True,'hinge_side':side,'hinge_count':count,'cup_diameter':r['diameter'],'cup_depth':r['depth'],'hinge_pattern':pattern}
    if r.get('closing_spring'):cfg['hinge_spring']=r['closing_spring']
    # Remove mounting-pilot operations of the previous catalog selection.
    p.operations=[o for o in p.operations if o.get('recipe_id') not in ('hinge45','hinge52')]
    result,p=attach(project,p,p.group,'door',cfg)
    p.motion=dict(mode='hinge_'+side,angle=100,travel=0)
    for h in result.hardware:
        if h.get('owner_id')==p.id and h.get('kind')=='hinge':h.update(name=r['name']+' + prowadnik roboczy',source=r['source'])
    p.assembly['config']['hinge_pattern']=pattern
    result.validate();return result
