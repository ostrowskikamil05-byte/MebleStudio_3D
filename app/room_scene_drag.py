"""Direct purchased-object placement, one undo step and cheap move previews."""
import copy
import math
import time
from room_objects import object_bounds, update_object
from furniture_layout_ui import screen_move


def snap_object(project,obj,tolerance=80,vertical=False):
    from furniture_layout import group_bounds
    lo,hi=object_bounds(obj);best={}
    bounds=[group_bounds(project,g) for g in dict.fromkeys(p.group for p in project.parts)]
    bounds += [object_bounds(o) for o in project.scene.get('objects',[]) if o['id']!=obj['id'] and o['enabled']]
    for a,b in bounds:
        for axis in ((2,) if vertical else (0,1)):
            # Only align surfaces whose other horizontal range overlaps. This
            # keeps distant cabinets from attracting an appliance across space.
            other=1-axis if axis<2 else None
            if other is not None and (hi[other]<a[other]-tolerance or lo[other]>b[other]+tolerance):continue
            for source,target in ((lo[axis],b[axis]),(hi[axis],a[axis]),(lo[axis],a[axis]),(hi[axis],b[axis])):
                delta=target-source
                if abs(delta)<=tolerance and (axis not in best or abs(delta)<abs(best[axis])):best[axis]=delta
    result=dict(obj)
    for axis,delta in best.items():result['xyz'[axis]]+=delta
    return result,best or None


def begin(app,event,identity):
    objid=identity.removeprefix('scene:')
    obj=next((o for o in app.project.scene.get('objects',[]) if o['id']==objid),None)
    if obj is None:return False
    app.scene_drag=dict(base=app.project,obj=dict(obj),start=(event.x,event.y),candidate=None,last=0)
    app.start=app.last=(event.x,event.y);app.dragged=False
    return True


def move(app,event,force=False):
    data=getattr(app,'scene_drag',None)
    if not data:return False
    dx,dy=event.x-data['start'][0],event.y-data['start'][1]
    if math.hypot(dx,dy)<3 and data['candidate'] is None:return True
    if not force and time.monotonic()-data['last']<.033:return True
    data['last']=time.monotonic()
    try:
        movement=screen_move(app.camera,dx,dy,bool(event.state&1))
        obj=dict(data['obj'])
        for i,axis in enumerate('xyz'):obj[axis]+=movement[i]
        contact=None
        if app.layout_magnet.get():obj,contact=snap_object(data['base'],obj,app.layout_tolerance.get(),bool(event.state&1))
        # Share all manufactured parts, copy only the moving object and scene.
        preview=copy.copy(data['base']);preview.scene=dict(data['base'].scene)
        preview.scene['objects']=[obj if o['id']==obj['id'] else o for o in data['base'].scene['objects']]
        data['candidate']=obj;app.project=preview;app.dragged=True;app._view_interactive=True;app.draw()
        app.status.set(('Magnes: wyrównano krawędź · ' if contact else '')+'Przesuwanie wyposażenia · Shift: wysokość · puść, aby zapisać')
    except ValueError as error:
        data['candidate']=None;app.project=data['base'];app.status.set(str(error))
    return True


def end(app,event):
    data=getattr(app,'scene_drag',None)
    if not data:return False
    move(app,event,force=True);app.scene_drag=None;app.project=data['base']
    if data['candidate'] is not None:
        candidate=update_object(data['base'],data['obj']['id'],**{key:data['candidate'][key] for key in ('x','y','z')})
        candidate.validate()
        if candidate.data()!=data['base'].data():app.snapshot();app.project=candidate;app.refresh()
    app.status.set('Zapisano pozycję wyposażenia · Ctrl+Z cofa cały ruch')
    return True


def cancel(app):
    data=getattr(app,'scene_drag',None)
    if not data:return False
    app.project=data['base'];app.scene_drag=None;app.refresh();return True
