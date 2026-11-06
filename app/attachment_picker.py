"""Visible face hit to exact local U,V,W; no CAD work during pointer movement."""
import math
from core import inverse_layout_point, rotate, explosion_shift
from depth_raster import pick_visible, triangulate


def inverse_world(part,point):
    p=inverse_layout_point(part,point)
    p=tuple(p[i]-getattr(part,'xyz'[i]) for i in range(3))
    # Construction transform is Rx then Ry then Rz, invert in reverse order.
    p=rotate(p,0,0,-part.rz);p=rotate(p,0,-part.ry,0);p=rotate(p,-part.rx,0,0)
    return p if part.plane=='XY' else (p[0],p[2],p[1]) if part.plane=='XZ' else (p[1],p[2],p[0])


def _barycentric(point,triangle):
    (ax,ay),(bx,by),(cx,cy)=triangle;x,y=point
    den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
    if abs(den)<1e-9:return None
    a=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/den
    b=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/den;c=1-a-b
    return (a,b,c) if min(a,b,c)>=-1e-6 else None


def pick(app,x,y):
    identity=pick_visible(app.canvas,x,y)
    part=next((p for p in app.display_project().parts if p.id==identity),None)
    surface=None
    native=getattr(app.canvas,'_native_world',None)
    if native:
        hit=native(x,y)
        if hit and hit.get('kind')=='part':
            part=next((p for p in app.display_project().parts if p.id==hit['id']),None)
            if part is not None:
                shift=explosion_shift(part,app.camera,.22 if app.explode.get() else 0)
                surface=tuple(hit['world'][i]-shift[i] for i in range(3))
    if getattr(app.canvas,'_active_renderer','cpu')=='gpu':
        from gpu_viewport import pick_world
        hit=pick_world(app.canvas,x,y)
        if hit and hit.get('kind')=='part' and part is not None:
            surface=hit['world']
    if part:
        best=None
        for face in app.drawn if getattr(app.canvas,'_active_renderer','cpu') not in ('gpu','native') else ():
            if face['id']!=identity or face.get('fitting') or not face.get('world'):continue
            for indices in triangulate(face['points']):
                weights=_barycentric((x,y),[face['points'][i] for i in indices])
                if weights is None:continue
                xyz=tuple(sum(weights[j]*face['world'][idx][k] for j,idx in enumerate(indices)) for k in range(3))
                depth=app.camera.view(xyz)[2]
                if best is None or depth>best[0]:best=(depth,xyz)
        if best:surface=best[1]
        if surface is not None:
            local=inverse_world(part,surface)
            distances=[(abs(local[2]-part.t),'top'),(abs(local[2]),'bottom'),(abs(local[0]),'u0'),(abs(local[0]-part.u),'u1'),(abs(local[1]),'v0'),(abs(local[1]-part.v),'v1')]
            return dict(target_id=part.id,local_hit=dict(zip(('u','v','w'),local),face=min(distances)[1]))
    # Lamp placement can use the room floor even without a Part target.
    ca,sa,cb,sb=app.camera._basis();scale=app.camera.scale
    u=(x-app.canvas.winfo_width()/2-app.camera.pan[0])/scale
    v=-(y-app.canvas.winfo_height()/2-app.camera.pan[1])/scale
    floor=app.project.scene.get('architecture',{}).get('z',0);z=floor-app.camera.center[2]
    if abs(sb)<.01:return None
    d=(v-z*cb)/sb
    return dict(target_id=None,local_hit={'world':(app.camera.center[0]+u*ca+d*sa,app.camera.center[1]-u*sa+d*ca,floor)})
