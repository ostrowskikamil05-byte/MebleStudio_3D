"""Presentation-only snapshots for the integrated native C++ viewport."""
from __future__ import annotations
import math
import struct
from collections import OrderedDict
from types import SimpleNamespace

MAGIC=b"MS3DNAT1"
VERSION=1
HEADER=struct.Struct("<III6ff")
OWNER=struct.Struct("<IIII6f")
VERTEX=struct.Struct("<10fII")
EMISSIVE,WOOD,SCENE,ARCHITECTURE,FITTING,GHOST,SELECTED=1,2,4,8,16,32,64
KIND_NAMES=("part","fitting","scene","architecture")
_TRIANGLES=OrderedDict()

def _kind(record):
    if getattr(record, "architecture", False):
        return 3
    if record.scene_object:
        return 2
    return 1 if record.fitting else 0


def _label(kind, identity, parts, objects):
    if kind == 0:
        return getattr(parts.get(identity), "name", identity)
    if kind == 1:
        return "Okucia — " + getattr(parts.get(identity), "name", identity)
    if kind == 2:
        return objects.get(identity, identity)
    return {
        "floor": "Podłoga", "ceiling": "Sufit", "west": "Ściana zachodnia",
        "east": "Ściana wschodnia", "north": "Ściana północna",
        "south": "Ściana południowa",
    }.get(identity.rsplit(":", 1)[-1], identity)


def _finite(values, what):
    if not all(math.isfinite(v) for v in values):
        raise ValueError(f"Dane widoku zawierają niefinitywne wartości: {what}")


def _face_triangles(record, face, triangulate):
    """Project onto a non-degenerate plane, preserving outward winding.

    CAD faces with holes are already triangles and retain their boundaries.
    Stock boards can have concave profiles, so a triangle fan is insufficient.
    """
    indices = tuple(face if record.sign >= 0 else reversed(face))
    points = [tuple(float(v) for v in record.vertices[i]) for i in indices]
    if len(points) < 3:
        return
    _finite((v for p in points for v in p), "wierzchołki")
    normal = [0.0, 0.0, 0.0]
    for a, b in zip(points, points[1:] + points[:1]):
        normal[0] += (a[1] - b[1]) * (a[2] + b[2])
        normal[1] += (a[2] - b[2]) * (a[0] + b[0])
        normal[2] += (a[0] - b[0]) * (a[1] + b[1])
    if max(map(abs, normal)) < 1e-12:
        return
    drop = max(range(3), key=lambda i: abs(normal[i]))
    axes = [i for i in range(3) if i != drop]
    flat = [(p[axes[0]], p[axes[1]]) for p in points]
    for triangle in triangulate(flat):
        vertices = tuple(points[i] for i in triangle)
        a, b, c = vertices
        u = tuple(b[i] - a[i] for i in range(3))
        v = tuple(c[i] - a[i] for i in range(3))
        n = (u[1] * v[2] - u[2] * v[1],
             u[2] * v[0] - u[0] * v[2],
             u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(value * value for value in n))
        if length < 1e-12:
            continue
        yield vertices, tuple(value / length for value in n)


def build_snapshot(project, *, context=None, interactive=False, explode=0,
                   camera=None, selected_ids=(), selected=None, xray_ids=()):
    """Return (binary snapshot, diagnostics); never alter project/CNC data."""
    from depth_raster import triangulate
    from gpu_scene import material_style
    from viewport_render import prepare_scene

    from core import explosion_shift
    context = context or prepare_scene(project)
    selected_ids=set(selected_ids)
    if selected:selected_ids.add(selected)
    xray_ids=set(xray_ids)
    parts = {p.id: p for p in project.parts}
    objects = {"scene:" + obj["id"]: obj["name"]
               for obj in project.scene.get("objects", [])}
    leds = {(h.get("owner_id"), h.get("color", "#fff3bf"))
            for h in project.hardware if h.get("kind") == "led_strip"}
    brightness = float(context.scene.get("brightness", 1.0))
    _finite((brightness,), "jasność")
    owner_table, owner_lookup, vertices = [], {}, bytearray()
    vertex_count = 0
    scene_lo, scene_hi = [math.inf] * 3, [-math.inf] * 3
    kinds = [0, 0, 0, 0]

    for record in context.records(interactive) + context.architecture:
        kind = _kind(record)
        identity = str(record.owner)
        key = (kind, identity)
        color = record.color
        is_led = kind == 1 and (identity, color) in leds
        emitted = bool(record.emissive or
                       is_led and context.scene.get("led_enabled", True))
        if is_led and not context.scene.get("led_enabled", True):
            color = "#c7c6bb"
        flags = EMISSIVE if emitted else 0
        if kind == 1:
            flags |= FITTING
        elif kind == 2:
            flags |= SCENE
        elif kind == 3:
            flags |= ARCHITECTURE
        style = material_style(project, SimpleNamespace(
            owner=identity, fitting=kind == 1, scene_object=kind == 2,
            emissive=emitted, architecture=kind == 3, color=color))
        if style["wood"]:
            flags |= WOOD
        if kind==0 and identity in xray_ids:flags|=GHOST
        if identity in selected_ids:flags|=SELECTED
        rgba = tuple(int(color[i:i + 2], 16) / 255.0 for i in (1, 3, 5)) + (1.0,)
        shift=explosion_shift(parts[identity],camera,explode) if explode and identity in parts else (0,0,0)
        key_mesh=(id(record.vertices),id(record.indices),record.sign)
        triangles=_TRIANGLES.get(key_mesh)
        if triangles is None:
            triangles=(record,tuple(t for face in record.indices for t in _face_triangles(record,face,triangulate)))
            _TRIANGLES[key_mesh]=triangles
            if len(_TRIANGLES)>256:_TRIANGLES.popitem(last=False)
        else:_TRIANGLES.move_to_end(key_mesh)
        for positions, normal in triangles[1]:
                positions=tuple(tuple(point[i]+shift[i] for i in range(3)) for point in positions) if explode else positions
                if key not in owner_lookup:
                    owner_lookup[key] = len(owner_table)
                    owner_table.append(dict(kind=kind, flags=0,
                        id=KIND_NAMES[kind] + ":" + identity,
                        label=_label(kind, identity, parts, objects),
                        lo=[math.inf] * 3, hi=[-math.inf] * 3))
                    kinds[kind] += 1
                owner_index = owner_lookup[key]
                owner = owner_table[owner_index]
                owner["flags"] |= flags
                for position in positions:
                    try:
                        packed = VERTEX.pack(*position, *normal, *rgba, owner_index, flags)
                    except (OverflowError, struct.error) as exc:
                        raise ValueError("Geometria przekracza zakres formatu widoku.") from exc
                    # Bounds match the float32 coordinates seen by the native renderer.
                    point = struct.unpack_from("<3f", packed)
                    _finite(point, "współrzędne float32")
                    for axis in range(3):
                        owner["lo"][axis] = min(owner["lo"][axis], point[axis])
                        owner["hi"][axis] = max(owner["hi"][axis], point[axis])
                        scene_lo[axis] = min(scene_lo[axis], point[axis])
                        scene_hi[axis] = max(scene_hi[axis], point[axis])
                    vertices.extend(packed)
                    vertex_count += 1

    if not vertex_count:
        scene_lo = scene_hi = [0.0, 0.0, 0.0]
    if vertex_count > 0xFFFFFFFF or len(owner_table) > 0xFFFFFFFF:
        raise ValueError("Widok ma zbyt wiele elementów.")
    result = bytearray(MAGIC)
    result.extend(HEADER.pack(VERSION, vertex_count, len(owner_table),
                              *scene_lo, *scene_hi, brightness))
    for owner in owner_table:
        identity, label = owner["id"].encode("utf-8"), owner["label"].encode("utf-8")
        result.extend(OWNER.pack(owner["kind"], owner["flags"], len(identity),
                                 len(label), *owner["lo"], *owner["hi"]))
        result.extend(identity)
        result.extend(label)
    result.extend(vertices)
    return bytes(result), dict(format="MS3DNAT1", version=VERSION,
        vertices=vertex_count, triangles=vertex_count // 3, owners=len(owner_table),
        kinds=dict(zip(KIND_NAMES, kinds)), bounds=[scene_lo, scene_hi],
        brightness=brightness, bytes=len(result), project_unchanged=True,
        owner_sources=[(owner['id'].split(':',1)[1],KIND_NAMES[owner['kind']]) for owner in owner_table])

