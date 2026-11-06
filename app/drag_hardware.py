"""Atomic drag placement and pure, inexpensive fitting previews.

Supported sourced recipes retain their existing limitations. A runner planning
profile does not invent manufacturer clip drilling or notches. LED groove
machining is available only on the CAD engine's broad +T/-T surfaces.
"""
import copy
import math
import json
import time
from collections import OrderedDict
from types import SimpleNamespace

from core import finite, uid, world, inside
from configurator_accessories import install_manual_led, led_proposal, refresh_manual_led

KINDS={'hinge':'Zawiasy + prowadniki','runner':'Para prowadnic','led':'LED na powierzchni',
       'handle':'Uchwyt','lamp':'Lampa','bulb':'Żarówka'}


def _part(project,target):
    p=next((p for p in project.parts if p.id==getattr(target,'id',target)),None)
    if p is None:raise ValueError('Wskaż istniejący element mebla.')
    return p


def _creator_changed(project,owner,old_children=None):
    """Keep managed generated children stable after a legitimate runner rebuild."""
    from furniture_configurator import _geometry, _remap
    old_children=old_children or {}
    children=[p for p in project.parts if p.assembly.get('generated_by')==owner.id]
    mapping={}
    for p in children:
        old=old_children.get(p.name)
        if old is None and p.plane=='XY':old=next((q for q in old_children.values() if q.plane=='XY'),None)
        if old:
            mapping[p.id]=old.id;p.configurator_source=copy.deepcopy(old.configurator_source)
    if mapping:_remap(project,mapping)
    touched={owner.id}|{p.id for p in children}
    for p in project.parts:
        manifest=p.configurator_source.get('manifest',{})
        for key,entry in manifest.items():
            if entry['id'] in touched:
                q=next((q for q in project.parts if q.id==entry['id']),None)
                if q:entry['signature']=_geometry(q)


def _hinge_install(project,target,hit,options,ident):
    from assembly import hinges,frame_for_part,recipe
    from fittings import recipe as fitting_recipe
    p=_part(project,target)
    if p.role!='door' or not p.assembly:raise ValueError('Przeciągnij zawiasy na drzwi połączone z korpusem.')
    if p.assembly.get('corner_source') and not p.assembly.get('config',{}).get('corner_bay'):
        raise ValueError('Front skośny wymaga doboru rzeczywistego zawiasu; nie ma automatycznego wzorca montażu.')
    cfg=recipe(p.assembly.get('config',{}));pattern=options.get('recipe_id','hinge45')
    if pattern not in ('hinge45','hinge52','hinge_sensys52'):raise ValueError('Wybierz obsługiwany wzór zawiasu nakładanego.')
    spec=fitting_recipe(pattern);count=finite(options.get('count',cfg['hinge_count']),2,8)
    if int(count)!=count:raise ValueError('Liczba zawiasów musi być całkowita.')
    cfg.update(hinge_pattern=pattern,hinge_count=int(count),hinge_price=finite(options.get('price',0),0,100000),
               cup_diameter=spec['diameter'],cup_depth=spec['depth'])
    saved_options=dict(options,recipe_id=pattern,count=int(count),price=cfg['hinge_price'])
    if cfg.get('corner_bay'):
        from corner_furniture import regenerate_corner_hinges
        result=regenerate_corner_hinges(project,p.id,pattern,int(count),cfg['hinge_price'],manual=True,validate=False)
        for h in result.hardware:
            if h.get('owner_id')==p.id and h.get('kind')=='hinge':h.update(attachment_id=ident,manual=True,config_auto=False,
                attachment_kind='hinge',attachment_options=copy.deepcopy(saved_options),attachment_hit=copy.deepcopy(hit))
        for q in result.parts:
            for op in q.operations:
                if op.get('corner_hinge_owner')==p.id:op.update(attachment_id=ident,manual=True,config_auto=False)
        return result
    if options.get('side'):
        if options['side'] not in ('left','right'):raise ValueError('Strona zawiasów: left/right.')
        cfg['hinge_side']=options['side']
    purposes={'puszka zawiasu','mocowanie puszki zawiasu','prowadnik zawiasu'}
    for q in project.parts:q.operations=[o for o in q.operations if not(o.get('owner_id')==p.id and o.get('purpose') in purposes)]
    project.hardware=[h for h in project.hardware if not(h.get('owner_id')==p.id and h.get('kind')=='hinge')]
    before=len(project.hardware);hinges(project,p,frame_for_part(project,p),cfg)
    p.assembly['config']=cfg
    for h in project.hardware[before:]:h.update(attachment_id=ident,manual=True,config_auto=False,
        attachment_kind='hinge',attachment_options=copy.deepcopy(saved_options),attachment_hit=copy.deepcopy(hit))
    for q in project.parts:
        for op in q.operations:
            if op.get('owner_id')==p.id and op.get('purpose') in purposes:
                op.update(attachment_id=ident,manual=True,config_auto=False,source=spec['source'])
    return project


def _runner_install(project,target,hit,options,ident):
    from runner_installation import apply_runner
    from runners_catalog import PROFILES
    p=_part(project,target)
    if p.role!='drawer' or not p.assembly:raise ValueError('Przeciągnij prowadnicę na front zarządzanej szuflady.')
    profile=options.get('profile_id','hettich_ka5632')
    if profile not in PROFILES:raise ValueError('Wybierz profil prowadnicy z katalogu.')
    length=finite(options.get('length',400),50,1500)
    price=finite(options.get('price',0),0,100000)
    old_children={q.name:copy.deepcopy(q) for q in project.parts if q.assembly.get('generated_by')==p.id}
    child_ids={q.id for q in old_children.values()}
    retained=[copy.deepcopy(h) for h in project.hardware if h.get('owner_id') in child_ids and h.get('manual')]
    project.hardware=[h for h in project.hardware if h.get('owner_id') not in child_ids and not(h.get('owner_id')==p.id and h.get('kind')=='runner')]
    result=apply_runner(project,p.id,profile,length,options.get('side_thickness',16),options.get('bottom_thickness',6),
                        options.get('recess',12),confirmed_min_depth=options.get('confirmed_min_depth'))
    owner=_part(result,p.id);_creator_changed(result,owner,old_children)
    result.hardware.extend(retained)
    for h in result.hardware:
        if h.get('owner_id')==p.id and h.get('kind')=='runner':h.update(price=price,manual=True,config_auto=False,
            attachment_id=ident,attachment_kind='runner',attachment_options=copy.deepcopy(options),attachment_hit=copy.deepcopy(hit))
    refresh_manual_led(result)
    return result


def _light_values(project,kind,target,hit,options):
    hit=hit or {};kind='bulb' if kind=='light' else kind
    if isinstance(hit,dict) and 'world' in hit:point=hit['world']
    elif target is not None:
        p=_part(project,target)
        if not isinstance(hit,dict):hit=dict(zip(('u','v','w'),hit))
        point=world(p,hit.get('u',p.u/2),hit.get('v',p.v/2),hit.get('w',p.t))
    elif isinstance(hit,dict) and all(k in hit for k in ('x','y','z')):point=tuple(hit[k] for k in ('x','y','z'))
    else:raise ValueError('Wskaż miejsce światła na scenie lub powierzchni mebla.')
    point=[finite(v) for v in point]
    if len(point)!=3:raise ValueError('Pozycja światła wymaga XYZ.')
    width=finite(options.get('width',80 if kind=='bulb' else 350),1,15000)
    depth=finite(options.get('depth',80 if kind=='bulb' else 350),1,15000)
    return dict(x=point[0]-width/2,y=point[1]-depth/2,z=point[2]+finite(options.get('offset',0)),
        width=width,depth=depth,height=finite(options.get('height',120 if kind=='bulb' else 600),1,15000),
        color=options.get('color','#fff0bc' if kind=='bulb' else '#c5b497'),price=options.get('price',0))


def _light_install(project,kind,target,hit,options):
    from room_objects import add_object
    return add_object(project,kind,**_light_values(project,kind,target,hit,options))


def attach(project,kind,target_id=None,local_hit=None,options=None,*,attachment_id=None):
    """Return one validated candidate; failure leaves the source entirely intact."""
    options=copy.deepcopy(options or {});hit=copy.deepcopy(local_hit or {})
    kind={'hinges':'hinge','slide':'runner','led_strip':'led','light':'bulb'}.get(kind,kind)
    if kind not in KINDS:raise ValueError('Nieznane okucie do przeciągania.')
    ident=attachment_id or uid();result=copy.deepcopy(project)
    if kind=='led':result=install_manual_led(result,target_id,hit,options,attachment_id=ident,validate=False)
    elif kind=='hinge':result=_hinge_install(result,target_id,hit,options,ident)
    elif kind=='runner':result=_runner_install(result,target_id,hit,options,ident)
    elif kind=='handle':
        from fittings import add_handle
        p=_part(result,target_id)
        if not isinstance(hit,dict):hit=dict(zip(('u','v','w'),hit))
        result=add_handle(result,p.id,options.get('spacing',128),u=hit.get('u',p.u/2),v=hit.get('v',p.v/2),
                          vertical=options.get('vertical',p.role=='door'),color=options.get('color','#242628'),projection=options.get('projection',28))
        face=options.get('face') or hit.get('face','bottom')
        if face not in ('top','bottom'):raise ValueError('Uchwyt wymaga powierzchni +T lub −T.')
        result.hardware[-1].update(attachment_id=ident,attachment_kind='handle',price=finite(options.get('price',0),0,100000),config_auto=False,mount_face=face)
        for op in _part(result,p.id).operations:
            if op.get('recipe_id')=='handle':op.update(face=face,attachment_id=ident,config_auto=False,manual=True)
    else:result=_light_install(result,kind,target_id,hit,options)
    if kind in ('hinge','handle'):refresh_manual_led(result)
    result.validate();return result


def preview_attachment(project,kind,target_id=None,local_hit=None,options=None):
    """Pure ghost proposal. An invalid target is a red proposal, not an exception."""
    data=dict(kind=kind,target_id=target_id,local_hit=copy.deepcopy(local_hit),options=copy.deepcopy(options or {}),
              valid=False,message='',world_faces=[],length=None)
    try:
        canonical={'hinges':'hinge','slide':'runner','led_strip':'led','light':'bulb'}.get(kind,kind)
        if canonical=='led':
            from configurator_accessories import led_local_geometry
            p=_part(project,target_id);h=led_proposal(p,local_hit,options)
            for key in ('price_per_m','profile_price_per_m','diffuser_price_per_m'):finite((options or {}).get(key,0),0,100000)
            data.update(valid=True,length=h['length'],world_faces=[[world(p,*point) for point in face] for face,color in led_local_geometry(p,h)],
                        message=f'LED {h["length"]:g} × {h["width"]:g} mm · '+('rowek' if h['groove'] else 'bez rowka'))
            return data
        if canonical in ('lamp','bulb'):
            from room_objects import default_scene,make_object,room_world_faces
            obj=make_object(canonical,**_light_values(project,canonical,target_id,local_hit,options or {}))
            scene=default_scene();scene['objects']=[obj]
            data.update(valid=True,message='Światło — pozycja w scenie',world_faces=[f['world'] for f in room_world_faces(SimpleNamespace(scene=scene),True)])
            return data
        if canonical in ('hinge','runner'):
            part=_part(project,target_id)
            if part.role!=('door' if canonical=='hinge' else 'drawer'):
                raise ValueError('Przeciągnij zawiasy na drzwi.' if canonical=='hinge' else 'Przeciągnij prowadnicę na front szuflady.')
        candidate=attach(project,kind,target_id,local_hit,options,attachment_id='preview_attachment')
        if canonical in ('lamp','bulb'):
            from room_objects import room_world_faces
            ident=candidate.scene['objects'][-1]['id'];data['world_faces']=[f['world'] for f in room_world_faces(candidate,True) if f['object_id']==ident]
            data['message']='Światło — pozycja w scenie'
        else:
            from fitting_visuals import fitting_faces
            fitted=[h for h in candidate.hardware if h.get('owner_id')==target_id and h.get('kind')==('led_strip' if canonical=='led' else canonical)]
            if fitted:
                h=fitted[-1];data['length']=h.get('length')
                data['message']=(f'LED {h["length"]:g} × {h["width"]:g} mm · '+('rowek' if h.get('groove') else 'bez rowka')) if canonical=='led' else h['name']
            candidate.hardware=[h for h in candidate.hardware if h.get('attachment_id')=='preview_attachment']
            data['world_faces']=[face for owner,face,color in fitting_faces(candidate,{canonical if canonical!='led' else 'led_strip'}) if owner==target_id]
        data['valid']=True
    except (ValueError,TypeError,KeyError,StopIteration,OverflowError) as error:data['message']=str(error) or 'Nie można dopasować tego okucia.'
    return data


def refresh_manual_attachments(project,changed_ids=None):
    """Refresh a caller-owned candidate after assembly/configurator rebuilding."""
    ids=set(changed_ids) if changed_ids is not None else None
    records=[copy.deepcopy(h) for h in project.hardware if h.get('attachment_kind') in ('hinge','runner') and (ids is None or h['owner_id'] in ids)]
    for h in records:
        kind=h['attachment_kind'];func=_hinge_install if kind=='hinge' else _runner_install
        result=func(project,h['owner_id'],h.get('attachment_hit',{}),h.get('attachment_options',{}),h['attachment_id'])
        if result is not project:project.parts=result.parts;project.hardware=result.hardware
    from fittings import refresh_handles
    handles=[copy.deepcopy(h) for h in project.hardware if h.get('attachment_kind')=='handle' and (ids is None or h['owner_id'] in ids)]
    for h in handles:
        project.hardware=[item for item in project.hardware if not(item.get('owner_id')==h['owner_id'] and item.get('kind')=='handle') or item.get('attachment_id')==h['attachment_id']]
        refresh_handles(project,h['owner_id'])
        for op in _part(project,h['owner_id']).operations:
            if op.get('recipe_id')=='handle':op.update(face=h.get('mount_face','bottom'),attachment_id=h['attachment_id'],config_auto=False,manual=True)
    refresh_manual_led(project);return project


class HardwareDragController:
    """Physical tray drag and click-tool/click-target share one atomic engine."""
    def __init__(self,app,panel=None):
        self.app=app;self.panel=panel;self.kind=None;self.options={};self.proposal=None
        self.pointer=None;self.start=None;self.dragging=False
        self._cache=OrderedDict();self._last_move=0
        self._cache_project=None

    @property
    def active(self):return self.kind is not None

    @property
    def active_tool(self):return self.kind

    def _note(self,text):
        if self.panel is not None:self.panel.note.set(text)
        if hasattr(self.app,'status'):self.app.status.set(text)

    def begin(self,kind,options=None,event=None):
        if hasattr(self.app,'flush_parameter_drafts') and not self.app.flush_parameter_drafts():
            self._note('Popraw parametry mebla przed montażem wyposażenia.');return 'break'
        self.cancel();self.kind=kind;self.options=copy.deepcopy(options or {})
        self.project_generation=getattr(self.app,'project_generation',0)
        self.start=(event.x_root,event.y_root) if event is not None else None
        self._note('Przeciągnij na mebel albo kliknij wybrany element. Esc anuluje.')
        return 'break'

    def _position(self,event):
        canvas=self.app.canvas
        if hasattr(event,'x_root'):return event.x_root-canvas.winfo_rootx(),event.y_root-canvas.winfo_rooty()
        return event.x,event.y

    def _target(self,x,y):
        if not 0<=x<self.app.canvas.winfo_width() or not 0<=y<self.app.canvas.winfo_height():return None
        if hasattr(self.app,'pick_attachment_target'):return self.app.pick_attachment_target(x,y)
        face=next((f for f in reversed(getattr(self.app,'drawn',[])) if inside((x,y),f['points']) and any(p.id==f['id'] for p in self.app.project.parts)),None)
        if face:
            p=_part(self.app.project,face['id'])
            return dict(target_id=p.id,local_hit=dict(u=p.u/2,v=p.v/2,w=p.t,face='top'))
        return None

    def move(self,event,force=False):
        if not self.active:return None
        if getattr(self.app,'project_generation',0)!=self.project_generation:
            self.cancel();self._note('Wczytano inny projekt. Wybierz kafelek ponownie.');return 'break'
        if self.start and hasattr(event,'x_root') and math.dist(self.start,(event.x_root,event.y_root))>5:self.dragging=True
        x,y=self._position(event);self.pointer=(x,y)
        now=time.monotonic()
        if not force and now-self._last_move<.033:return 'break'
        self._last_move=now
        exploded=getattr(self.app,'explode',None)
        if (exploded is not None and exploded.get()) or any(float(v)>1e-7 for v in getattr(self.app,'open_amounts',{}).values()):
            self.proposal=dict(valid=False,message='Zamknij mechanizmy i wyłącz widok rozstrzelony przed montażem.',world_faces=[])
            self.draw_ghost();return 'break'
        target=self._target(x,y)
        if target:
            if self._cache_project is not self.app.project:
                self._cache.clear();self._cache_project=self.app.project
            hit=copy.deepcopy(target.get('local_hit'))
            if isinstance(hit,dict) and target.get('face'):hit['face']=target['face']
            quantized=copy.deepcopy(hit)
            if isinstance(quantized,dict):
                quantized={k:round(v/2)*2 if isinstance(v,(int,float)) else v for k,v in quantized.items()}
            static_kind=self.kind in ('hinge','hinges','runner','slide')
            key=(self.kind,target.get('target_id'),json.dumps(self.options,sort_keys=True),
                 '' if static_kind else json.dumps(quantized,sort_keys=True))
            if force or key not in self._cache:
                self.proposal=preview_attachment(self.app.project,self.kind,target.get('target_id'),hit if force else quantized,self.options)
                if not force:self._cache[key]=self.proposal
                while len(self._cache)>64:self._cache.popitem(last=False)
            else:self.proposal=dict(self._cache[key])
            self.proposal['local_hit']=copy.deepcopy(hit)
        else:self.proposal=dict(valid=False,message='Wskaż powierzchnię mebla lub miejsce światła.',world_faces=[])
        self.draw_ghost();return 'break'

    def draw_ghost(self):
        canvas=self.app.canvas;canvas.delete('hardware_ghost')
        if not self.active or self.proposal is None:return
        proposal=self.proposal;color='#23986a' if proposal['valid'] else '#c6574b';w,h=canvas.winfo_width(),canvas.winfo_height()
        for face in proposal['world_faces']:
            points=[self.app.camera.project(point,w,h) for point in face]
            canvas.create_polygon(*[value for x,y,d in points for value in (x,y)],fill='',outline=color,width=2,tags='hardware_ghost')
        if self.pointer:
            x,y=self.pointer;label=proposal['message'][:130]
            item=canvas.create_text(min(w-8,max(8,x+12)),min(h-10,max(10,y+18)),text=label,anchor='nw',fill=color,width=300,tags='hardware_ghost')
            bb=canvas.bbox(item)
            if bb:
                bg=canvas.create_rectangle(bb[0]-5,bb[1]-4,bb[2]+5,bb[3]+4,fill='#ffffff',outline=color,tags='hardware_ghost');canvas.tag_lower(bg,item)

    def target_click(self,event):
        if not self.active:return None
        self.move(event,force=True)
        if not self.active:return 'break'
        if not self.proposal or not self.proposal['valid']:
            self._note(self.proposal['message'] if self.proposal else 'Wskaż element.');return 'break'
        try:
            p=self.proposal;candidate=attach(self.app.project,self.kind,p.get('target_id'),p.get('local_hit'),self.options)
            self.app.snapshot();self.app.project=candidate
            target=p.get('target_id');message=p['message'];commit_kind=self.kind;self.cancel()
            if commit_kind in ('lamp','bulb','light'):
                self.app.selected=None;self.app.selected_scene=candidate.scene['objects'][-1]['id']
                if hasattr(self.app,'edit_mode'):self.app.edit_mode.set('layout')
            elif target is not None:self.app.selected=target
            self.app.refresh();self.app.draw();self._note('Dodano: '+message)
        except (ValueError,TypeError,KeyError) as error:self._note(str(error))
        return 'break'

    def end(self,event):
        if not self.active:return None
        x,y=self._position(event)
        if self.dragging:return self.target_click(event)
        # Releasing a clicked tray tile keeps the tool armed for a canvas click.
        if not self.start and 0<=x<self.app.canvas.winfo_width() and 0<=y<self.app.canvas.winfo_height():return self.target_click(event)
        return 'break'

    def cancel(self,event=None):
        self.kind=None;self.options={};self.proposal=None;self.pointer=None;self.start=None;self.dragging=False
        self._last_move=0
        self._cache.clear();self._cache_project=None
        if hasattr(self.app,'canvas'):self.app.canvas.delete('hardware_ghost')
        return 'break'
