"""Lightweight Tk scene dressing, independent from production geometry.

All model polygons and their hit-test coordinates remain those returned by
core.render_faces. The floor, shadows and grain are presentation only; no CAD
kernel, bitmap assets, network connection or third-party renderer is involved.
"""
import math
from functools import lru_cache

from core import world

SKY_TOP = '#edf1f3'
SKY_BOTTOM = '#faf8f3'
FLOOR = '#ece8e0'
GRID = '#d9d6cf'
SELECTION = '#df903f'


@lru_cache(maxsize=2048)
def mix_color(first, second, amount):
    amount = max(0, min(1, amount))
    a = [int(first[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(second[i:i + 2], 16) for i in (1, 3, 5)]
    return '#' + ''.join(f'{round(x + (y - x) * amount):02x}' for x, y in zip(a, b))


def scene_vertices(project, include_fittings=True, include_room=True, context=None):
    """Fast display bounds including purchased legs, handles and mechanisms.

    Machining and edge rounding remove stock, so the stock outline safely
    bounds their mesh. Avoiding tessellation here keeps camera fitting cheap.
    """
    from viewport_render import prepare_scene
    context = context or prepare_scene(project)
    points = list(context.stock)
    if include_fittings:
        points.extend(point for record in context.fittings for point in record.vertices)
    if include_room:
        points.extend(context.room_vertices)
    return points


def scene_bounds(project, include_fittings=True):
    points = scene_vertices(project, include_fittings)
    if not points:
        return None
    return (tuple(min(p[i] for p in points) for i in range(3)),
            tuple(max(p[i] for p in points) for i in range(3)))


def fit_scene(camera, project, size, padding=100, include_fittings=True):
    """Fit the visible furniture AND purchased accessories; preserve orientation."""
    width, height = size
    points = scene_vertices(project, include_fittings)
    if not points:
        camera.center, camera.scale, camera.pan = (0, 0, 0), .4, (0, 0)
        return
    camera.center = tuple((min(p[i] for p in points) + max(p[i] for p in points)) / 2
                          for i in range(3))
    viewed = [camera.view(p) for p in points]
    min_u, max_u = min(p[0] for p in viewed), max(p[0] for p in viewed)
    min_v, max_v = min(p[1] for p in viewed), max(p[1] for p in viewed)
    extent_u, extent_v = max_u - min_u, max_v - min_v
    # The centre of an XYZ bounding box need not centre a projected L/U set.
    # Shift in the camera's orthonormal screen basis, preserving its depth.
    yaw, pitch = math.radians(camera.yaw), math.radians(camera.pitch)
    screen_u = (math.cos(yaw), -math.sin(yaw), 0)
    screen_v = (math.sin(yaw) * math.sin(pitch),
                math.cos(yaw) * math.sin(pitch), math.cos(pitch))
    middle_u, middle_v = (min_u + max_u) / 2, (min_v + max_v) / 2
    camera.center = tuple(camera.center[i] + middle_u * screen_u[i] + middle_v * screen_v[i]
                          for i in range(3))
    camera.scale = min(max(width - padding, 50) / max(extent_u, 100),
                       max(height - padding, 50) / max(extent_v, 100))
    camera.pan = (0, 0)


def grid_spacing(scale):
    """A readable 1/2/5 series in millimetres, rather than a screen-space grid."""
    target = 44 / max(abs(scale), .0001)
    power = 10 ** math.floor(math.log10(target))
    return next(multiplier * power for multiplier in (1, 2, 5, 10)
                if multiplier * power >= target)


def _flat(points):
    return [coordinate for point in points for coordinate in point[:2]]


def _convex_hull(points):
    points = sorted(set((p[0], p[1]) for p in points))
    if len(points) < 3:
        return points
    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    def half(ordered):
        out = []
        for point in ordered:
            while len(out) > 1 and cross(out[-2], out[-1], point) <= 0:
                out.pop()
            out.append(point)
        return out
    return half(points)[:-1] + half(points[::-1])[:-1]


def _group_points(project, include_fittings, context=None):
    from viewport_render import prepare_scene
    context = context or prepare_scene(project)
    if include_fittings:
        return context.groups
    groups={}
    for p in project.parts:
        if p.visible:
            groups.setdefault(p.group, []).extend(world(p,u,v,t)
                for u,v in p.points() for t in (0,p.t))
    return groups


def paint_background(canvas, camera, project, size, *, grid=True, shadows=True,
                     include_fittings=True, interactive=False, context=None):
    """Draw before model polygons; return floor level and the displayed grid step.

    Call after canvas.delete('all'). The item count is bounded independently of
    furniture detail; changing the scene cannot create an unbounded grid.
    """
    width, height = size
    if width < 10 or height < 10:
        return dict(items=0, spacing=0, floor=0)
    from viewport_render import prepare_scene
    context = context or prepare_scene(project)
    brightness = getattr(project,'scene',{}).get('brightness',1)
    camera._scene_brightness = brightness
    if brightness < .65:
        sky_top,sky_bottom,floor,grid_color='#242c36','#45474b','#393a3b','#515150'
        shadow_colors=('#2f3031','#27282a')
    elif brightness > 1.1:
        sky_top,sky_bottom,floor,grid_color='#f3f7fa','#fffdf8','#f1ede5','#dfdcd5'
        shadow_colors=('#e3ded3','#d6cdbd')
    else:
        sky_top,sky_bottom,floor,grid_color=SKY_TOP,SKY_BOTTOM,FLOOR,GRID
        shadow_colors=('#dcd6ca','#cfc6b7')
    count = 0
    bands = 10 if interactive else 20
    for i in range(bands):
        y0, y1 = height * i / bands, height * (i + 1) / bands + 1
        canvas.create_rectangle(0, y0, width, y1, outline='',
            fill=mix_color(sky_top, sky_bottom, i / (bands - 1)), tags='viewport.background')
        count += 1
    groups = _group_points(project, include_fittings, context)
    points = [point for group in groups.values() for point in group]
    floor_z = min(0, min((p[2] for p in points), default=0))
    scale = max(abs(camera.scale), .0001)
    extent = max(width, height) / scale * 1.6
    center_x, center_y = camera.center[:2]
    # Include pan in the floor extent, so panning does not reveal its boundary.
    extent += math.hypot(*camera.pan) / scale
    floor_rect = [(center_x - extent, center_y - extent, floor_z),
                  (center_x + extent, center_y - extent, floor_z),
                  (center_x + extent, center_y + extent, floor_z),
                  (center_x - extent, center_y + extent, floor_z)]
    projected = [camera.project(p, width, height) for p in floor_rect]
    canvas.create_polygon(*_flat(projected), fill=floor, outline='', tags='viewport.floor')
    count += 1
    step = grid_spacing(scale)
    if grid and abs(math.sin(math.radians(camera.pitch))) > .035:
        # Up to 31 lines per axis, even with unusual camera scaling or pan.
        steps = min(15, max(4, math.ceil(max(width, height) / scale / step)))
        origin_x = math.floor(center_x / step) * step
        origin_y = math.floor(center_y / step) * step
        for i in range(-steps, steps + 1):
            x, y = origin_x + i * step, origin_y + i * step
            for a, b in (((x, origin_y - steps * step, floor_z),
                          (x, origin_y + steps * step, floor_z)),
                         ((origin_x - steps * step, y, floor_z),
                          (origin_x + steps * step, y, floor_z))):
                pa, pb = camera.project(a, width, height), camera.project(b, width, height)
                canvas.create_line(*pa[:2], *pb[:2], fill=grid_color, width=1,
                                   tags='viewport.grid')
                count += 1
    if shadows:
        # Contact shadows should not suggest a hanging cabinet touches the floor.
        if not hasattr(context,'shadow_hulls'):context.shadow_hulls={}
        if include_fittings not in context.shadow_hulls:
            context.shadow_hulls[include_fittings]=[(min(p[2] for p in group),_convex_hull(group)) for group in groups.values()]
        eligible=[hull for minimum,hull in context.shadow_hulls[include_fittings] if minimum-floor_z<=160]
        for hull in eligible[:40]:
            if len(hull) < 3:
                continue
            cx = sum(p[0] for p in hull) / len(hull)
            cy = sum(p[1] for p in hull) / len(hull)
            for expansion, offset, color in ((1.045, 10, shadow_colors[0]), (1.01, 4, shadow_colors[1])):
                shadow = [(cx + (x - cx) * expansion + offset,
                           cy + (y - cy) * expansion + offset, floor_z + .05) for x, y in hull]
                canvas.create_polygon(*_flat([camera.project(p, width, height) for p in shadow]),
                                      fill=color, outline='', tags='viewport.shadow')
                count += 1
    # Pools are a lightweight floor approximation of LED spill. Direction and
    # height decide whether a strip can illuminate the floor beneath it.
    for light in ([] if interactive else context.lights[:32]):
        position=light['position']; height_above=position[2]-floor_z
        if height_above<0 or height_above>light.get('radius',650):continue
        if light.get('direction',(0,0,-1))[2] > -.15:continue
        radius=min(300,max(70,light.get('radius',650)*.3))
        power=.14 if brightness>=.65 else .36
        for fraction in (1,.64):
            pool=[(position[0]+math.cos(i*math.tau/16)*radius*fraction,
                   position[1]+math.sin(i*math.tau/16)*radius*fraction,floor_z+.1)
                  for i in range(16)]
            canvas.create_polygon(*_flat([camera.project(p,width,height) for p in pool]),
                fill=mix_color(floor,light.get('color','#fff3bf'),power/fraction),
                outline='',tags='viewport.light_pool')
            count+=1
    return dict(items=count, spacing=step, floor=floor_z)


def polygon_area(points):
    return abs(sum(a[0] * b[1] - b[0] * a[1]
                   for a, b in zip(points, points[1:] + points[:1]))) / 2


def grain_segments(points, direction, count=5):
    """Clip parallel grain strokes to an arbitrary simple screen polygon.

    Pairing intersections also keeps grain inside concave outlines rather than
    painting across their cut-outs. Degenerate polygons/directions draw nothing.
    """
    length = math.hypot(*direction)
    if len(points) < 3 or length < .001:
        return []
    dx, dy = direction[0] / length, direction[1] / length
    nx, ny = -dy, dx
    values = [x * nx + y * ny for x, y in points]
    low, high = min(values), max(values)
    if high - low < 12:
        return []
    out = []
    edges = list(zip(points, points[1:] + points[:1]))
    for i in range(count):
        # Non-uniform, deterministic spacing avoids a ruler-like striped fill.
        fraction = (i + 1 + .12 * math.sin(i * 2.1)) / (count + 1)
        level = low + (high - low) * fraction
        hits = []
        for a, b in edges:
            ta, tb = a[0] * nx + a[1] * ny, b[0] * nx + b[1] * ny
            if (ta <= level < tb) or (tb <= level < ta):
                t = (level - ta) / (tb - ta)
                hits.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
        hits.sort(key=lambda p: p[0] * dx + p[1] * dy)
        out.extend((a, b) for a, b in zip(hits[::2], hits[1::2]) if math.dist(a, b) > 10)
    return out


def _wood(material):
    name = material.name.casefold()
    return material.kind in ('drewno', 'sklejka') or any(word in name for word in
        ('dąb', 'oak', 'orzech', 'walnut', 'jesion', 'ash', 'buk', 'beech',
         'sosna', 'pine', 'brzoza', 'birch', 'wenge', 'mahoń', 'teak', 'bambus'))


class _RasterFaces:
    """A local Pillow surface avoids one Tcl call for every CAD triangle.

    Picking remains in the original vector faces. Dimension labels, handles,
    grid and interaction stay on the native Tk canvas above/below this layer.
    """
    def __init__(self, canvas, size):
        from PIL import Image, ImageDraw
        self.canvas=canvas
        self.image=Image.new('RGBA',(max(1,int(size[0])),max(1,int(size[1]))),(0,0,0,0))
        self.draw=ImageDraw.Draw(self.image)

    @staticmethod
    def points(values):
        return [(values[i],values[i+1]) for i in range(0,len(values),2)]

    def create_polygon(self,*values,fill='',outline='',width=1,**kwargs):
        points=self.points(values)
        if fill:self.draw.polygon(points,fill=fill)
        if outline and width:
            self.draw.line(points+points[:1],fill=outline,width=max(1,int(width)),joint='curve')

    def create_line(self,*values,fill='#000000',width=1,**kwargs):
        self.draw.line(self.points(values),fill=fill,width=max(1,int(width)),joint='curve')

    def flush(self):
        from PIL import ImageTk
        self.canvas._viewport_photo=ImageTk.PhotoImage(self.image,master=self.canvas)
        self.canvas.create_image(0,0,anchor='nw',image=self.canvas._viewport_photo,
                                 tags='viewport.raster_model')


def _paint_depth_faces(canvas, faces, project, *, selected_ids, wood, camera,
                       size, xray, explode, interactive, xray_ids=None):
    from depth_raster import DepthRaster, triangulate
    from assembly import inferred_role
    from core import explosion_shift
    from PIL import ImageTk
    owners={p.id:p for p in project.parts}
    materials={m.id:m for m in project.materials}
    ghost_ids=(set(xray_ids) if xray_ids is not None else {p.id for p in project.parts if inferred_role(p) in ('door','drawer')}) if xray else set()
    selected_ids=set(selected_ids)
    scale=(.35 if len(faces)>400 else .6) if interactive else 1
    raster=DepthRaster(size,{f['id'] for f in faces},scale=scale)
    solid=[]
    for face in reversed(faces):
        if face['id'] in ghost_ids and not face.get('fitting'):continue
        depths=face.get('depths')
        if depths is None:depths=(face.get('depth',0),)*len(face['points'])
        raster.polygon(face['points'],depths,face['color'],face['id'])
        solid.append((face,depths))
    # Draw edges only where the shared z-buffer says their surface is visible.
    if interactive:
        # Hundreds of NumPy line allocations cost more than the low-resolution
        # fill itself. Visible ownership gives crisp motion silhouettes in one
        # array pass, and cannot reveal an edge through an occluding board.
        raster.outline_visible(selected_ids|{f['id'] for f in faces if f.get('selected')})
    else:
        for face,depths in solid:
            if face.get('mesh') or face.get('fitting'):continue
            selected=face.get('selected',False) or face['id'] in selected_ids
            border=SELECTION if selected else mix_color(face['color'],'#514b43',.3)
            raster.line(face['points']+face['points'][:1],tuple(depths)+tuple(depths[:1]),
                        border,face['id'],width=2 if selected else 1)
    grain=0
    if wood and not interactive:
        candidates=[]
        for face,depths in solid:
            part=owners.get(face['id']);material=materials.get(part.material) if part else None
            if not face.get('mesh') and not face.get('fitting') and material and _wood(material):
                area=polygon_area(face['points'])
                if area>1500:candidates.append((area,face,depths))
        candidates.sort(key=lambda item:item[0],reverse=True)
        for _area,face,depths in candidates[:24]:
            part=owners[face['id']]
            if camera:
                a=camera.project(world(part,0,0,0),*size)
                b=camera.project(world(part,part.u if part.grain else 0,0 if part.grain else part.v,0),*size)
                direction=(b[0]-a[0],b[1]-a[1])
            else:
                a,b=max(zip(face['points'],face['points'][1:]+face['points'][:1]),key=lambda pair:math.dist(*pair))
                direction=(b[0]-a[0],b[1]-a[1])
            triangles=triangulate(face['points'])
            if not triangles:continue
            i,j,k=triangles[0];a2,b2,c2=(face['points'][n] for n in (i,j,k))
            area=(b2[0]-a2[0])*(c2[1]-a2[1])-(b2[1]-a2[1])*(c2[0]-a2[0])
            if abs(area)<1e-8:continue
            def z_at(point):
                x,y=point[0]-a2[0],point[1]-a2[1]
                u=((c2[1]-a2[1])*x-(c2[0]-a2[0])*y)/area
                v=(-(b2[1]-a2[1])*x+(b2[0]-a2[0])*y)/area
                return depths[i]+u*(depths[j]-depths[i])+v*(depths[k]-depths[i])
            for start,end in grain_segments(face['points'],direction):
                raster.line([start,end],[z_at(start),z_at(end)],mix_color(face['color'],'#5b442f',.12),face['id'])
                grain+=1
                if grain>=120:break
            if grain>=120:break
    if ghost_ids and camera:
        face_selected={f['id'] for f in faces if f.get('selected')}
        for owner in ghost_ids & {f['id'] for f in faces}:
            part=owners[owner];shift=explosion_shift(part,camera,explode);layers=[]
            for thickness in (0,part.t):
                outline=[];depths=[]
                for u,v in part.points():
                    point=world(part,u,v,thickness)
                    x,y,z=camera.project(tuple(point[i]+shift[i] for i in range(3)),*size)
                    outline.append((x,y));depths.append(z)
                layers.append((outline,depths))
            selected=owner in selected_ids or owner in face_selected
            color='#259ec4' if selected else '#819bb3'
            for outline,depths in layers:
                raster.line(outline+outline[:1],depths+depths[:1],color,owner,width=2 if selected else 1,ghost=True)
            for i in range(len(layers[0][0])):
                raster.line([layers[0][0][i],layers[1][0][i]],[layers[0][1][i],layers[1][1][i]],color,owner,ghost=True)
    raster.attach(canvas)
    canvas._render_diagnostics=dict(getattr(faces,'stats',{}))
    canvas._viewport_photo=ImageTk.PhotoImage(raster.image(),master=canvas)
    canvas.create_image(0,0,anchor='nw',image=canvas._viewport_photo,tags='viewport.raster_model')
    return dict(items=1,grain=grain,raster=True,depth=True,triangles=raster.triangles)


def paint_faces(canvas, faces, project, *, selected_ids=(), wood=True,
                camera=None, size=None, xray=False, explode=0, interactive=False, xray_ids=None):
    """Paint already sorted model faces without changing selection geometry.

    At most 24 large non-mesh wooden faces receive grain, at most 120 strokes.
    Faces with holes/machining are already meshes and keep their actual cutouts.
    """
    raster=None
    if hasattr(canvas,'tk'):
        return _paint_depth_faces(canvas,faces,project,selected_ids=selected_ids,wood=wood,
            camera=camera,size=size or (canvas.winfo_width(),canvas.winfo_height()),
            xray=xray,explode=explode,interactive=interactive,xray_ids=xray_ids)
    owners = {p.id: p for p in project.parts}
    materials = {m.id: m for m in project.materials}
    wood_ids = {m.id for m in project.materials if _wood(m)}
    selected_ids = set(selected_ids)
    ghost_ids = set()
    if xray:
        from assembly import inferred_role
        ghost_ids = set(xray_ids) if xray_ids is not None else {p.id for p in project.parts if inferred_role(p) in ('door', 'drawer')}
    candidates = []
    if wood and not interactive:
        for index, face in enumerate(faces):
            part = owners.get(face['id'])
            material = materials.get(part.material) if part else None
            if (not face.get('mesh') and not face.get('fitting')
                    and face['id'] not in ghost_ids
                    and material and material.id in wood_ids):
                area = polygon_area(face['points'])
                if area > 1500:
                    candidates.append((area, index))
    textured = {index for _area, index in sorted(candidates, reverse=True)[:24]}
    items, grain_items = 0, 0
    for index, face in enumerate(faces):
        points = face['points']
        part = owners.get(face['id'])
        material = materials.get(part.material) if part else None
        color = face['color']
        if material and not face.get('fitting') and 'normal' not in face:
            color = mix_color(color, material.color, .18)
        selected = face.get('selected', False) or face['id'] in selected_ids
        border = SELECTION if selected else mix_color(color, '#514b43', .36)
        ghost = face['id'] in ghost_ids and not face.get('fitting')
        if ghost:
            # Preserve all faces in the caller's picking list, but avoid
            # thousands of invisible triangle items. A semantic front contour
            # below provides the readable ghost over the interior.
            if face.get('mesh'):
                continue
            canvas.create_polygon(*_flat(points),fill='',
                outline='#5290ad' if selected else '#91afbd',width=2 if selected else 1,
                tags=('model',face['id'],'viewport.ghost'))
        else:
            canvas.create_polygon(*_flat(points), fill=color,
                outline=color if face.get('mesh') else border,
                width=0 if face.get('mesh') else 2 if selected else 1,
                tags=('model', face['id']))
        items += 1
        if index not in textured:
            continue
        if camera is not None and size is not None:
            a = camera.project(world(part, 0, 0, 0), *size)
            b = camera.project(world(part, part.u if part.grain else 0,
                                           0 if part.grain else part.v, 0), *size)
            direction = b[0] - a[0], b[1] - a[1]
        else:
            a, b = max(zip(points, points[1:] + points[:1]), key=lambda pair: math.dist(*pair))
            direction = b[0] - a[0], b[1] - a[1]
        for a, b in grain_segments(points, direction):
            if grain_items >= 120:
                break
            canvas.create_line(*a, *b, fill=mix_color(color, '#5b442f', .12),
                               width=1, tags=('viewport.grain', face['id']))
            grain_items += 1
            items += 1
    if ghost_ids and camera is not None and size is not None:
        from core import explosion_shift
        mesh_owners = {f['id'] for f in faces if f['id'] in ghost_ids}
        face_selected = {f['id'] for f in faces if f.get('selected')}
        for part_id in mesh_owners:
            part = owners[part_id]
            shift = explosion_shift(part, camera, explode)
            outline = []
            for u, v in part.points():
                point = world(part, u, v, 0)
                outline.append(camera.project(tuple(point[i] + shift[i] for i in range(3)), *size))
            if len(outline) >= 3:
                selected = part_id in selected_ids or part_id in face_selected
                canvas.create_line(*_flat(outline + outline[:1]),
                    fill='#5290ad' if selected else '#91afbd', width=2 if selected else 1,
                    tags=('viewport.ghost_outline', part_id))
                items += 1
    if raster:raster.flush()
    return dict(items=1 if raster else items, grain=grain_items,raster=bool(raster))


def paint_overlay(canvas, camera, size, *, spacing=None, axes=True):
    """Small orientation triad and readable grid legend; draw after the model."""
    width, height = size
    if width < 100 or height < 100:
        return
    if axes:
        origin = (49, height - 46)
        center = camera.view(camera.center)
        for label, vector, color in [('X', (1, 0, 0), '#bd6559'),
                                    ('Y', (0, 1, 0), '#4a9275'),
                                    ('Z', (0, 0, 1), '#527cae')]:
            p = camera.view(tuple(camera.center[i] + vector[i] for i in range(3)))
            dx, dy = (p[0] - center[0]) * 29, -(p[1] - center[1]) * 29
            if math.hypot(dx, dy) < 3:
                continue
            end = origin[0] + dx, origin[1] + dy
            canvas.create_line(*origin, *end, fill=color, width=2, arrow='last',
                               tags='viewport.axes')
            canvas.create_text(end[0] + dx * .2, end[1] + dy * .2, text=label,
                fill=color, font=('Segoe UI', 8, 'bold'), tags='viewport.axes')
    if spacing:
        canvas.create_text(width - 15, height - 16, text=f'Siatka {spacing:g} mm',
            anchor='se', fill='#b6bec7' if getattr(camera,'_scene_brightness',1)<.65 else '#8b877f',
            font=('Segoe UI', 8), tags='viewport.legend')
