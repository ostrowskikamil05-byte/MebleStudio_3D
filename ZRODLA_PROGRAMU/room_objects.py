"""Editable purchased room objects, separate from manufactured furniture.

All catalogue sizes are illustrative workshop defaults, not branded appliances
or manufacturer installation clearances. No Part, holes, stock or toolpaths are
created. Geometry is deliberately light and supports an offline Tk viewport.
"""
import copy
import json
import math
import re
from functools import lru_cache

from core import finite, rotate, shade, uid
from fitting_visuals import box, cylinder, rod

ROOMS = {'neutral': 'Neutralne', 'kitchen': 'Kuchnia', 'bathroom': 'Łazienka',
         'living': 'Salon', 'bedroom': 'Sypialnia', 'garage': 'Garaż / warsztat', 'garden': 'Ogród'}

CATALOG = {
    'washer': dict(name='Pralka', width=600, depth=600, height=850, color='#f1f0eb'),
    'dryer': dict(name='Suszarka', width=600, depth=620, height=850, color='#e8e7e3'),
    'fridge': dict(name='Lodówka', width=600, depth=650, height=1850, color='#b8c0c4'),
    'oven': dict(name='Piekarnik', width=595, depth=560, height=595, color='#373d43', z=600),
    'hob': dict(name='Płyta grzewcza', width=600, depth=520, height=60, color='#242b30', z=800),
    'hood': dict(name='Okap', width=600, depth=450, height=650, color='#b7bec2', z=1400),
    'dishwasher': dict(name='Zmywarka', width=600, depth=570, height=820, color='#d7dadd'),
    'washbasin': dict(name='Umywalka', width=600, depth=450, height=180, color='#f0f1ed', z=760),
    'sink': dict(name='Zlew', width=780, depth=480, height=200, color='#a9b2b6', z=620),
    'faucet': dict(name='Kran', width=140, depth=220, height=350, color='#aab3b8', z=820),
    'shower': dict(name='Kabina prysznicowa', width=900, depth=900, height=2000, color='#b1c8c7'),
    'mirror': dict(name='Lustro', width=700, depth=30, height=900, color='#c0d1d6', z=900),
    'shelf': dict(name='Półka zakupowa z podpórkami', width=800, depth=250, height=120, color='#bc9970', z=1000),
    'hanger': dict(name='Wieszak', width=600, depth=80, height=160, color='#515f63', z=1250),
    'lamp': dict(name='Lampa wisząca', width=350, depth=350, height=600, color='#c5b497', z=1900),
    'bulb': dict(name='Żarówka', width=80, depth=80, height=120, color='#fff0bc', z=1950),
}

_OBJECT_KEYS = {'id', 'type', 'name', 'x', 'y', 'z', 'yaw', 'width', 'depth', 'height',
                'color', 'enabled', 'price', 'light_enabled'}
_SCENE_KEYS = {'room', 'brightness', 'led_enabled', 'lights_enabled', 'objects', 'architecture'}
MAX_OBJECTS = 200


def default_scene():
    from room_architecture import defaults
    return dict(room='neutral', brightness=1.0, led_enabled=True, lights_enabled=True, objects=[], architecture=defaults())


def _color(value):
    if not isinstance(value, str) or len(value) != 7 or value[0] != '#':
        raise ValueError('Kolor wyposażenia: #RRGGBB.')
    try:
        int(value[1:], 16)
    except ValueError as exc:
        raise ValueError('Kolor wyposażenia: #RRGGBB.') from exc
    return value.lower()


def _number(value, low=-100000, high=100000):
    if isinstance(value, bool):
        raise ValueError('Wpisz wartość liczbową wyposażenia.')
    try:
        return finite(value, low, high)
    except (TypeError, OverflowError) as exc:
        raise ValueError('Wpisz skończoną wartość liczbową wyposażenia.') from exc


def normalize_object(obj):
    if not isinstance(obj, dict) or set(obj) - _OBJECT_KEYS:
        raise ValueError('Nieprawidłowe dane obiektu wyposażenia.')
    kind = obj.get('type')
    if not isinstance(kind, str) or kind not in CATALOG:
        raise ValueError('Wybierz obiekt z katalogu wyposażenia pomieszczenia.')
    ident = obj.get('id')
    if not isinstance(ident, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,80}',ident):
        raise ValueError('Obiekt sceny wymaga identyfikatora długości do 80 znaków.')
    default = CATALOG[kind]
    result = dict(default, id=ident, type=kind, x=0, y=0, z=default.get('z', 0), yaw=0,
                  enabled=True, price=0, light_enabled=True)
    result.update(copy.deepcopy(obj))
    if not isinstance(result['name'], str) or not result['name'].strip() or len(result['name']) > 160:
        raise ValueError('Nazwa wyposażenia: od 1 do 160 znaków.')
    result['name'] = result['name'].strip()
    for key in ('x', 'y', 'z'):
        result[key] = _number(result[key])
    for key in ('width', 'depth', 'height'):
        result[key] = _number(result[key], 1, 15000)
    result['yaw'] = (_number(result['yaw'], -360000, 360000) + 180) % 360 - 180
    result['price'] = _number(result['price'], 0, 10000000)
    result['color'] = _color(result['color'])
    if type(result['enabled']) is not bool or type(result['light_enabled']) is not bool:
        raise ValueError('Widoczność i światło obiektu wymagają TAK / NIE.')
    return result


def validate_scene(scene):
    """Return a canonical copy, raise for invalid input, never mutate the input."""
    if not isinstance(scene, dict) or set(scene) - _SCENE_KEYS:
        raise ValueError('Nieprawidłowe ustawienia pomieszczenia.')
    result = default_scene()
    result.update(copy.deepcopy(scene))
    if not isinstance(result['room'],str) or result['room'] not in ROOMS:
        raise ValueError('Wybierz pomieszczenie z listy.')
    result['brightness'] = _number(result['brightness'], .1, 2)
    from room_architecture import validate
    result['architecture'] = validate(result['architecture'])
    if type(result['led_enabled']) is not bool or type(result['lights_enabled']) is not bool:
        raise ValueError('Przełączniki LED i lamp wymagają TAK / NIE.')
    if not isinstance(result['objects'], list) or len(result['objects']) > MAX_OBJECTS:
        raise ValueError(f'Scena może mieć najwyżej {MAX_OBJECTS} zakupowych obiektów.')
    result['objects'] = [normalize_object(obj) for obj in result['objects']]
    ids = [obj['id'] for obj in result['objects']]
    if len(set(ids)) != len(ids):
        raise ValueError('Obiekty sceny mają powtarzające się identyfikatory.')
    return result


def scene_of(project):
    return validate_scene(getattr(project, 'scene', {}))


def make_object(kind, **values):
    return normalize_object(dict(type=kind, id=values.pop('id', uid()), **values))


def add_object(project, kind, **values):
    result = copy.deepcopy(project)
    scene = scene_of(result)
    scene['objects'].append(make_object(kind, **values))
    result.scene = validate_scene(scene)
    return result


def update_object(project, object_id, **changes):
    if 'id' in changes:
        raise ValueError('Identyfikator obiektu jest stały; dodaj nowy obiekt, aby utworzyć kopię.')
    result = copy.deepcopy(project)
    scene = scene_of(result)
    index = next((i for i, obj in enumerate(scene['objects']) if obj['id'] == object_id), None)
    if index is None:
        raise ValueError('Obiekt został usunięty; wybierz istniejące wyposażenie.')
    scene['objects'][index] = normalize_object({**scene['objects'][index], **changes})
    result.scene = validate_scene(scene)
    return result


def delete_object(project, object_id):
    result = copy.deepcopy(project)
    scene = scene_of(result)
    if not any(obj['id'] == object_id for obj in scene['objects']):
        raise ValueError('Nie znaleziono wyposażenia do usunięcia.')
    scene['objects'] = [obj for obj in scene['objects'] if obj['id'] != object_id]
    result.scene = scene
    return result


def set_scene(project, **changes):
    if 'objects' in changes:
        raise ValueError('Obiekty dodawaj lub edytuj osobno.')
    result = copy.deepcopy(project)
    result.scene = validate_scene({**scene_of(result), **changes})
    return result


def object_point(obj, point):
    cx, cy = obj['width'] / 2, obj['depth'] / 2
    a, b, c = rotate((point[0] - cx, point[1] - cy, point[2]), 0, 0, obj['yaw'])
    return a + cx + obj['x'], b + cy + obj['y'], c + obj['z']


def object_vertices(project, include_disabled=False):
    return [object_point(obj, (a * obj['width'], b * obj['depth'], c * obj['height']))
            for obj in scene_of(project)['objects'] if include_disabled or obj['enabled']
            for a, b, c in ((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1))]


def object_bounds(obj):
    obj = normalize_object(obj)
    points = [object_point(obj, (a * obj['width'], b * obj['depth'], c * obj['height']))
              for a, b, c in ((0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1))]
    return tuple(min(p[i] for p in points) for i in range(3)), tuple(max(p[i] for p in points) for i in range(3))


def inventory(project):
    """One purchased item per object, including temporarily hidden objects."""
    return [dict(id=obj['id'], type=obj['type'], name=obj['name'], qty=1, unit='szt.',
                 price=obj['price'], total=obj['price'], enabled=obj['enabled'],
                 width=obj['width'], depth=obj['depth'], height=obj['height'], source='',
                 note='Własny model zakupowego wyposażenia; sprawdź artykuł i wymiary montażowe.')
            for obj in scene_of(project)['objects']]


def purchased_total(project):
    return sum(row['total'] for row in inventory(project))


def object_lights(project):
    scene = scene_of(project)
    if not scene['lights_enabled']:
        return []
    return [dict(id=obj['id'], position=object_point(obj, (obj['width']/2,obj['depth']/2,obj['height']*.18)),
                 color='#fff0ba' if obj['type'] == 'lamp' else obj['color'],
                 intensity=scene['brightness'], radius=min(3000,max(800,obj['width']*5)), kind=obj['type'])
            for obj in scene['objects'] if obj['enabled'] and obj['light_enabled'] and obj['type'] in ('lamp','bulb')]


def _ellipse(cx, cy, z, rx, ry, count=16):
    return [(cx+rx*math.cos(i*math.tau/count),cy+ry*math.sin(i*math.tau/count),z) for i in range(count)]


def _ring_xz(cx, y, cz, rx, rz, color, glass_color, count):
    outer=[(cx+rx*math.cos(i*math.tau/count),y,cz+rz*math.sin(i*math.tau/count)) for i in range(count)]
    inner=[(cx+rx*.82*math.cos(i*math.tau/count),y-.001,cz+rz*.82*math.sin(i*math.tau/count)) for i in range(count)]
    return [(inner,glass_color,False,True)]+[([outer[i],outer[(i+1)%count],inner[(i+1)%count],inner[i]],color,False,False) for i in range(count)]


def _bowl(cx, cy, w, d, h, color, count, rectangular_rim=False):
    outer=_ellipse(cx,cy,h,w/2,d/2,count)
    if rectangular_rim:
        outer=[]
        for i in range(count):
            a=i*math.tau/count;c,s=math.cos(a),math.sin(a);scale=max(abs(c),abs(s))
            outer.append((cx+w/2*c/scale,cy+d/2*s/scale,h))
    inner=_ellipse(cx,cy,h-.025*h,w*.41,d*.39,count)
    low=_ellipse(cx,cy,h*.1,w*.20,d*.20,count)
    shell=_ellipse(cx,cy,0,w*.24,d*.23,count)
    faces=[(low,shade(color,.70),False,False)]
    for i in range(count):
        j=(i+1)%count
        faces.extend([([outer[i],outer[j],inner[j],inner[i]],color,False,False),
                      ([inner[i],inner[j],low[j],low[i]],shade(color,.86),False,False),
                      ([outer[i],outer[j],shell[j],shell[i]],shade(color,.94),False,False)])
    return faces


@lru_cache(maxsize=256)
def _geometry(key):
    data=json.loads(key);kind=data['type'];w,d,h=(data[k] for k in ('width','depth','height'))
    color=data['color'];n=8 if data.get('interactive') else 16;out=[]
    def put(faces,col=color,emissive=False,glass=False):
        out.extend((face,col,emissive,glass) for face in faces)
    def bx(x,y,z,a,b,c,col=color):put(box(x,y,z,a,b,c),col)
    if kind in ('washer','dryer','fridge','oven','dishwasher'):
        bx(0,d*.01,0,w,d*.99,h)
        if kind in ('washer','dryer'):
            bx(w*.04,0,h*.82,w*.92,d*.012,h*.13,shade(color,.88))
            out.extend(_ring_xz(w*.5,d*.006,h*.43,w*.29,min(h*.26,w*.29),'#606b72','#384c58',n))
            out.extend(_ring_xz(w*.79,0,h*.886,w*.036,w*.036,'#87939a','#bdc9c7',n))
        elif kind=='fridge':
            bx(w*.025,0,h*.025,w*.95,d*.012,h*.29,shade(color,.97))
            bx(w*.025,0,h*.33,w*.95,d*.012,h*.64,shade(color,1.04))
            r=min(w*.012,d*.014)
            put(rod((w*.9,d*.04,h*.12),(w*.9,d*.04,h*.25),r),'#5c6c75')
            put(rod((w*.9,d*.04,h*.45),(w*.9,d*.04,h*.78),r),'#5c6c75')
        elif kind=='oven':
            bx(w*.05,0,h*.15,w*.9,d*.015,h*.58,'#1f2f38')
            bx(w*.08,0,h*.8,w*.84,d*.018,h*.13,'#8c999e')
            r=min(h*.012,d*.015)
            put(rod((w*.12,d*.04,h*.75),(w*.88,d*.04,h*.75),r),'#c2c9cb')
        else:
            bx(w*.025,0,h*.12,w*.95,d*.012,h*.76,shade(color,.98))
            bx(w*.025,0,h*.9,w*.95,d*.014,h*.065,'#626e74')
            bx(w*.06,d*.03,0,w*.88,d*.02,h*.09,'#455159')
    elif kind=='hob':
        bx(0,0,0,w,d,h,'#26323b')
        for cx,cy,r in ((w*.25,d*.3,w*.13),(w*.72,d*.3,w*.11),(w*.27,d*.73,w*.11),(w*.72,d*.73,w*.15)):
            r=min(r,d*.18)
            ring=_ellipse(cx,cy,h,r,r,n);inner=_ellipse(cx,cy,h,r*.94,r*.94,n)
            for i in range(n):put([[ring[i],ring[(i+1)%n],inner[(i+1)%n],inner[i]]],'#b7c2c6')
    elif kind=='hood':
        bx(w*.32,d*.24,h*.2,w*.36,d*.5,h*.8)
        rings=[[(0,0,0),(w,0,0),(w,d,0),(0,d,0)],
               [(w*.25,d*.18,h*.2),(w*.75,d*.18,h*.2),(w*.75,d*.82,h*.2),(w*.25,d*.82,h*.2)]]
        put([rings[0],rings[1]]+[[rings[0][i],rings[0][(i+1)%4],rings[1][(i+1)%4],rings[1][i]] for i in range(4)])
    elif kind in ('washbasin','sink'):
        if kind=='washbasin':out.extend(_bowl(w/2,d/2,w,d,h,color,n))
        else:
            out.extend(_bowl(w*.32,d/2,w*.64,d,h,color,n,True))
            bx(w*.64,0,h*.94,w*.36,d,h*.06,shade(color,.95))
            for k in range(5):bx(w*(.69+.055*k),d*.12,h*.995,w*.007,d*.76,h*.005,shade(color,.8))
    elif kind=='faucet':
        r=min(w*.12,d*.065,h*.05)
        cx,cy=w*.45,d*.74
        put(cylinder(cx,cy,0,r*1.3,h*.07,n))
        put(rod((cx,cy,h*.05),(cx,cy,h*.84),r))
        put(rod((cx,cy,h*.84),(cx,d*.15,h*.84),r))
        put(rod((cx,d*.15,h*.84),(cx,d*.15,h*.68),r))
        put(rod((cx,cy,h*.35),(w*.84,cy,h*.35),r*.55))
    elif kind=='shower':
        bx(0,0,0,w,d,h*.025,'#e4e9e6')
        thickness=min(w,d)*.013
        for x,y in ((thickness,thickness),(w-thickness,thickness),(w-thickness,d-thickness),(thickness,d-thickness)):
            put(rod((x,y,h*.025),(x,y,h-thickness),thickness*.45),'#788a91')
        put([[(thickness,thickness,h*.025),(w-thickness,thickness,h*.025),(w-thickness,thickness,h-thickness),(thickness,thickness,h-thickness)],
             [(w-thickness,thickness,h*.025),(w-thickness,d-thickness,h*.025),(w-thickness,d-thickness,h-thickness),(w-thickness,thickness,h-thickness)]],color,glass=True)
        for y in (thickness,d-thickness):put(rod((thickness,y,h-thickness),(w-thickness,y,h-thickness),thickness*.45),'#788a91')
    elif kind=='mirror':
        bx(0,d*.3,0,w,d*.7,h,'#4e626e')
        put([[(w*.025,0,h*.025),(w*.975,0,h*.025),(w*.975,0,h*.975),(w*.025,0,h*.975)]],color,glass=True)
    elif kind=='shelf':
        bx(0,0,h*.75,w,d,h*.25)
        r=min(h*.055,d*.018)
        for x in (w*.15,w*.85):
            put(rod((x,d*.94,r),(x,d*.94,h*.75),r),'#6a797c')
            put(rod((x,d*.94,r),(x,d*.12,h*.75),r),'#6a797c')
    elif kind=='hanger':
        bx(0,d*.75,h*.38,w,d*.25,h*.24)
        r=min(d*.04,h*.04,w*.012)
        for k in range(5):
            x=w*(.1+.2*k)
            put(rod((x,d*.78,h*.5),(x,d*.2,h*.5),r))
            put(rod((x,d*.2,h*.5),(x,d*.2,h*.73),r))
    elif kind in ('lamp','bulb'):
        if kind=='lamp':
            put(cylinder(w/2,d/2,h*.04,min(w,d)*.12,h*.26,n),'#fff0ba',emissive=True)
            put(cylinder(w/2,d/2,h*.38,min(w,d)*.035,h*.62,n),'#505759')
            rings=[_ellipse(w/2,d/2,h*.03,w*.49,d*.49,n),_ellipse(w/2,d/2,h*.42,w*.25,d*.25,n)]
            put([[rings[0][i],rings[0][(i+1)%n],rings[1][(i+1)%n],rings[1][i]] for i in range(n)])
        else:
            rings=[_ellipse(w/2,d/2,level,w*rx,d*rx,n) for level,rx in ((0,.1),(h*.2,.49),(h*.55,.45),(h*.75,.2))]
            put([rings[0],rings[-1]]+[[rings[k][i],rings[k][(i+1)%n],rings[k+1][(i+1)%n],rings[k+1][i]] for k in range(3) for i in range(n)],color,emissive=True)
            put(cylinder(w/2,d/2,h*.75,min(w,d)*.2,h*.25,n),'#7c878d')
    return tuple((tuple(tuple(point) for point in face),col,emissive,glass) for face,col,emissive,glass in out)


def room_world_faces(project, interactive=False):
    scene=scene_of(project);faces=[]
    for obj in scene['objects']:
        if not obj['enabled']:continue
        key=json.dumps({k:obj[k] for k in ('type','width','depth','height','color')}|{'interactive':bool(interactive)},sort_keys=True)
        lit=scene['lights_enabled'] and obj['light_enabled']
        for points,color,emissive,glass in _geometry(key):
            faces.append(dict(world=[object_point(obj,point) for point in points],color=color,
                id='scene:'+obj['id'],object_id=obj['id'],scene_object=True,fitting=True,
                emissive=bool(emissive and lit),glass=glass))
    return faces


def room_faces(project, camera, size, selected=None, *, interactive=False):
    out=[];width,height=size
    for face in room_world_faces(project,interactive):
        points=face['world'];a,b,c=points[:3]
        ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
        normal=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        norm=math.sqrt(sum(v*v for v in normal)) or 1
        normal=tuple(v/norm for v in normal)
        light=abs(sum(normal[i]*(-.3,-.5,.81)[i] for i in range(3)))
        projected=[camera.project(point,width,height) for point in points]
        out.append(dict(face,points=[(u,v) for u,v,depth in projected],
            depth=sum(q[2] for q in projected)/len(projected),normal=normal,
            center=tuple(sum(p[i] for p in points)/len(points) for i in range(3)),
            color=face['color'] if face['emissive'] else shade(face['color'],.7+.3*light),
            selected=selected in (face['id'],face['object_id']),mesh=False))
    return sorted(out,key=lambda face:face['depth'])
