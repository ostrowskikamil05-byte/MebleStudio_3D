"""Cached native geometry for catalogue cards; no CAD kernel or image service.

Furniture cards are lightweight construction schematics using the same library
parameters. Room equipment uses its actual viewport mesh. Fittings show their
planning dimensions and are explicitly simplified, never manufacturer meshes.
"""
import json
import math
from functools import lru_cache
from core import Camera,shade
from fitting_visuals import box,cylinder,rod


def preview_faces(item):
    return _faces(json.dumps(dict(kind=item['kind'],preview=item.get('preview',{}),payload=item.get('payload',{})),sort_keys=True))


@lru_cache(maxsize=256)
def _faces(key):
    data=json.loads(key);kind=data['kind'];p=data['preview'];options=data['payload'];out=[]
    def put(faces,color):out.extend((tuple(tuple(v) for v in face),color) for face in faces)
    def bx(x,y,z,w,d,h,color='#bea174'):put(box(x,y,z,w,d,h),color)
    if kind=='furniture':
        w,d,h,t=p['width'],p['depth'],p['body_height'],p.get('thickness',18);family=p['family'];legs=p.get('leg_height',0);front='#e6ddd0';wood='#bd9b6d'
        if family in ('table','desk'):
            bx(0,0,h-t,w,d,t,wood);leg=max(40,t*2)
            for x,y in ((0,0),(w-leg,0),(0,d-leg),(w-leg,d-leg)):bx(x,y,0,leg,leg,h-t,wood)
        elif family=='bed':
            bx(0,t,h-200,t,d-2*t,200,wood);bx(w-t,t,h-200,t,d-2*t,200,wood);bx(0,d-t,0,w,t,h+400,wood);bx(0,0,0,w,t,h,wood)
            for i in range(12):bx(t,t+(d-2*t-70)*i/11,h-30,w-2*t,70,t,'#e2c699')
        elif family=='bench':
            seat=min(450,h*.6);bx(0,0,seat,w,d,t,wood);bx(0,d-t,seat,w,t,h-seat,wood)
            for x,y in ((0,0),(w-60,0),(0,d-60),(w-60,d-60)):bx(x,y,0,60,60,seat,wood)
        else:
            bx(0,0,legs,t,d,h,wood);bx(w-t,0,legs,t,d,h,wood);bx(t,0,legs,w-2*t,d,t,wood)
            if p.get('top_mode')=='rails':
                bx(t,0,legs+h-t,w-2*t,80,t,wood);bx(t,d-80,legs+h-t,w-2*t,80,t,wood)
            else:bx(t,0,legs+h-t,w-2*t,d,t,wood)
            if p.get('back',family not in ('shelf','chest')):bx(0,d,legs,w,3,h,'#92754e')
            if family=='chest':
                bx(t,0,t,w-2*t,t,h-t,wood);bx(t,d-t,t,w-2*t,t,h-t,wood);bx(0,0,h,w,d,t,wood)
            else:
                sections=p.get('sections') or [dict(height=h,shelves=p.get('shelves',0),columns=1,front='drawers' if family=='drawers' else 'doors' if p.get('fronts') else 'open',doors=p.get('fronts',1),drawers=p.get('fronts',3))]
                z=legs
                for section in sections:
                    sh=section['height'];cols=section.get('columns',1);colw=w/cols
                    if z>legs:bx(t,0,z-t/2,w-2*t,d,t,wood)
                    for col in range(cols):
                        if col:bx(col*colw-t/2,0,z+t,t,d,sh-2*t,wood)
                        n=section.get('shelves',0)
                        for i in range(n):bx(col*colw+t,4,z+t+(sh-3*t)*(i+1)/(n+1),colw-2*t, d-8,t,wood)
                        mode=section.get('front','open')
                        if mode=='doors':
                            count=max(1,section.get('doors',1));span=(colw-2*(count+1))/count
                            for i in range(count):
                                xx=col*colw+2+i*(span+2);bx(xx,-t,z+2,span,t,sh-4,front)
                                put(rod((xx+span-30,-t-12,z+sh*.43),(xx+span-30,-t-12,z+sh*.57),4),'#455356')
                        elif mode=='drawers':
                            count=max(1,section.get('drawers',3));fh=(sh-2*(count+1))/count
                            for i in range(count):
                                zz=z+2+i*(fh+2);bx(col*colw+2,-t,zz,colw-4,t,fh,front)
                                put(rod((col*colw+colw*.34,-t-12,zz+fh/2),(col*colw+colw*.66,-t-12,zz+fh/2),4),'#455356')
                    z+=sh
            if legs:
                for x,y in ((60,60),(w-60,60),(60,d-60),(w-60,d-60)):put(cylinder(x,y,0,20,legs,10),'#353f41')
            if family=='island':bx(-20,-20,h,w+40,d+40,38,'#7e6c59')
    elif kind=='appliance':
        from room_objects import _geometry
        spec={key:p[key] for key in ('type','width','depth','height','color')};spec['interactive']=True
        out.extend((face,color) for face,color,emissive,glass in _geometry(json.dumps(spec,sort_keys=True)))
    elif kind=='leg':
        foot=min(6,p['height']/4);color=p.get('color','#353f41')
        put(cylinder(30,30,0,p['diameter']*1.15/2,foot,14),color);put(cylinder(30,30,foot,p['diameter']/2,p['height']-foot,14),color)
    elif kind=='runner':
        length=p.get('length',400);clearance=p.get('clearance',12.7);height=p.get('height',46)
        for i in range(3):bx(i*length*.1,i*clearance*.27,i*height*.08,length*(1-i*.12),clearance*.3,height*(1-i*.14),('#aab5ba','#d6dcde','#73848b')[i])
        if p.get('mount')=='undermount':bx(length*.04,clearance,0,length*.88,36,8,'#aab5ba')
    elif kind=='hinge':
        diameter=p['diameter'];pitch=p['pitch'];offset=p['offset'];bx(0,-pitch/2-8,0,diameter+15,pitch+16,3,'#a8b3b7')
        put(cylinder(5+diameter/2,0,3,diameter/2,p.get('depth',12.5),18),'#9ba9b1');bx(diameter+8,-8,5,55,16,8,'#b8c3c7');bx(diameter+60,-20,0,22,40,4,'#809198')
        for y in (-pitch/2,pitch/2):put(cylinder(5+diameter/2-offset,y,3,2,4,8),'#44565d')
    elif kind=='handle':
        mode=options.get('option');spacing=options.get('spacing',128)
        if mode in ('finger_pocket','finger_slot'):
            bx(0,0,0,170,48,18,'#c5a475');bx(25,-.5,7,120,6,8,'#5b4636')
        elif mode=='edge_pull':bx(0,0,0,150,10,20,'#414e54');bx(0,-25,18,150,35,3,'#414e54')
        elif mode and mode.startswith(('tip','salice')):put(cylinder(12,12,0,5,p.get('depth',50),12),'#b3bec3');put(cylinder(12,12,p.get('depth',50),3,10,10),'#49555c')
        elif mode=='none':bx(0,0,0,150,8,90,'#d1bb9d')
        elif not spacing:put(cylinder(12,12,0,6,18,10),'#505c61');put(cylinder(12,12,18,12,10,14),'#87979f')
        else:
            for x in (0,spacing):put(rod((x,0,0),(x,0,25),5),'#53656d')
            put(rod((0,0,25),(spacing,0,25),5),'#a4b3ba')
    elif kind=='led':
        width=options['width'];bx(0,0,0,300,width,4,'#75848c')
        bx(0,1,4,300,max(1,width-2),1,'#eadfc0')
        for x in range(6,300,18):bx(x,width*.3,5,6,width*.4,.7,'#fff4bd')
    elif kind=='material':bx(0,0,0,180,120,12,p['color'])
    return tuple(out)


def draw_preview(canvas,item,width=200,height=125):
    """Draw cached polygons on the card's Canvas, retaining item identity."""
    canvas.delete('preview');faces=preview_faces(item)
    if not faces:return
    camera=Camera(yaw=-38,pitch=22,center=(0,0,0),scale=1)
    vertices=[camera.view(v) for face,color in faces for v in face]
    lo=[min(v[i] for v in vertices) for i in (0,1)];hi=[max(v[i] for v in vertices) for i in (0,1)]
    scale=min((width-18)/max(hi[0]-lo[0],1),(height-14)/max(hi[1]-lo[1],1));cx,cy=(lo[0]+hi[0])/2,(lo[1]+hi[1])/2
    ordered=sorted(faces,key=lambda item:sum(camera.view(v)[2] for v in item[0])/len(item[0]))
    for face,color in ordered:
        coords=[value for v in face for x,y,depth in [camera.view(v)] for value in (width/2+(x-cx)*scale,height/2-(y-cy)*scale)]
        canvas.create_polygon(coords,fill=color,outline=shade(color,.78),width=.5,tags=('preview',item['id']))
    if item['kind']=='material':
        spec=item.get('preview',{});wood=spec.get('wood',False)
        if wood:
            for i in range(8):canvas.create_line(20,20+i*11,width-20,30+i*9,fill=shade(spec['color'],.91),width=1,tags='preview')
