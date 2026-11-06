"""Presentation room enclosure; never creates manufactured furniture parts."""
import math
from core import finite


def defaults():
    return dict(enabled=False,width=6000,depth=4500,height=2800,x=-600,y=-600,z=0,
                wall_color='#c8d8dd',floor_color='#bc9567',cutaway=True,ceiling=False)


def validate(value):
    if not isinstance(value,dict) or set(value)-set(defaults()):raise ValueError('Nieprawidłowe parametry ścian pomieszczenia.')
    result=defaults();result.update(value)
    for key in ('enabled','cutaway','ceiling'):
        if type(result[key]) is not bool:raise ValueError('Widoczność ścian i sufitu wymaga TAK / NIE.')
    for key in ('width','depth','height'):
        if type(result[key]) not in (int,float):raise ValueError('Podaj liczbowy wymiar pomieszczenia.')
        result[key]=finite(result[key],100,50000)
    for key in ('x','y','z'):
        if type(result[key]) not in (int,float):raise ValueError('Podaj liczbową pozycję pomieszczenia.')
        result[key]=finite(result[key],-100000,100000)
    for key in ('wall_color','floor_color'):
        c=result[key]
        if not isinstance(c,str) or len(c)!=7 or c[0]!='#':raise ValueError('Kolor pomieszczenia: #RRGGBB.')
        try:int(c[1:],16)
        except ValueError:raise ValueError('Kolor pomieszczenia: #RRGGBB.')
    return result


def world_faces(project,camera=None):
    cfg=validate(project.scene.get('architecture',{}))
    if not cfg['enabled']:return []
    x,y,z=cfg['x'],cfg['y'],cfg['z'];w,d,h=cfg['width'],cfg['depth'],cfg['height']
    surfaces=[('floor',[(x,y,z),(x+w,y,z),(x+w,y+d,z),(x,y+d,z)],(0,0,1),cfg['floor_color']),
              ('west',[(x,y+d,z),(x,y,z),(x,y,z+h),(x,y+d,z+h)],(1,0,0),cfg['wall_color']),
              ('east',[(x+w,y,z),(x+w,y+d,z),(x+w,y+d,z+h),(x+w,y,z+h)],(-1,0,0),cfg['wall_color']),
              ('north',[(x+w,y+d,z),(x,y+d,z),(x,y+d,z+h),(x+w,y+d,z+h)],(0,-1,0),cfg['wall_color']),
              ('south',[(x,y,z),(x+w,y,z),(x+w,y,z+h),(x,y,z+h)],(0,1,0),cfg['wall_color'])]
    if cfg['ceiling']:surfaces.append(('ceiling',[(x,y+d,z+h),(x+w,y+d,z+h),(x+w,y,z+h),(x,y,z+h)],(0,0,-1),cfg['wall_color']))
    near=None
    if camera is not None:
        yaw,pitch=math.radians(camera.yaw),math.radians(camera.pitch)
        near=(-math.sin(yaw)*math.cos(pitch),-math.cos(yaw)*math.cos(pitch),math.sin(pitch))
    result=[]
    for name,points,normal,color in surfaces:
        if cfg['cutaway'] and near is not None and sum(a*b for a,b in zip(normal,near))<=.001:continue
        result.append(dict(id='scene:room:'+name,world=tuple(points),color=color,normal=normal,
                           center=tuple(sum(p[i] for p in points)/4 for i in range(3)),
                           scene_object=True,architecture=True,emissive=False))
    return result


def vertices(project):
    cfg=validate(project.scene.get('architecture',{}))
    if not cfg['enabled']:return []
    return [(cfg['x']+i*cfg['width'],cfg['y']+j*cfg['depth'],cfg['z']+k*cfg['height'])
            for i in (0,1) for j in (0,1) for k in (0,1)]
