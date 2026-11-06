"""Batch placement geometry in C++; retain editable project/CAD publication.

The wire scene contains nominal panel points and one owner-tagged accessory
geometry pass. Transform, envelope, footprint, magnet and contact loops execute
inside the native library. No mutable Project receives adapter attributes.
"""
import copy
from functools import wraps
import math


OPERATIONS = ('groups', 'group_layout', 'group_bounds', 'local_group_bounds',
              'move_group', 'rotate_group', 'snap_group', 'nearest_snap', 'contacts')


def _scalar(value):
    # JSON rejects NaN before entering the DLL. A string lets the native finite
    # check reject it at the same point in the operation as the legacy code.
    if not isinstance(value, (str, bool, int, float)) and hasattr(value, '__float__'):
        value = float(value)
    if isinstance(value, float) and not math.isfinite(value):
        return str(value)
    return value


def scene(project, *, geometry=False, group=None):
    """Extract once, without validation, world transforms or panel CAD meshes."""
    parts = [part for part in project.parts if group is None or part.group == group]
    rows = []
    for part in parts:
        row = dict(id=part.id, group=part.group, layout=part.layout)
        if geometry:
            row.update({key: getattr(part, key) for key in
                        ('u', 'v', 't', 'plane', 'x', 'y', 'z', 'rx', 'ry', 'rz')})
            row.update(points=part.points(), shared_worktop=bool(part.assembly.get('shared_worktop')),
                       corner_bottom=part.role == 'bottom' and bool(part.assembly.get('corner_source')))
        rows.append(row)
    result = dict(parts=rows)
    if geometry:
        from fitting_visuals import fitting_faces
        owners = {part.id: part for part in parts}
        vertices = {}
        for owner, face, unused_color in fitting_faces(project, {'leg', 'led_strip'}):
            part = owners.get(owner)
            if part is not None and part.visible:
                vertices.setdefault(owner, []).extend(face)
        result['accessories'] = [dict(owner=owner, vertices=points) for owner, points in vertices.items()]
    return result


def _call(function, project, *args, geometry=False, group=None):
    import native_model
    return native_model.call('layout', function, scene(project, geometry=geometry, group=group), *args)


def _layout(value):
    return {key: value[key] for key in ('x', 'y', 'z', 'yaw')}


def install(namespace):
    import native_model
    if not native_model.available():
        return
    if native_model.call('native', 'info').get('layout_api_version') != 1:
        raise RuntimeError('Biblioteka C++ nie obsługuje pełnego układu mebli. Użyj kompletnej paczki Etap 2.')
    placed = namespace['_placed']

    def groups(project):
        return _call('groups', project)

    def group_layout(project, group):
        return _layout(_call('group_layout', project, group, group=group))

    def group_bounds(project, group):
        return native_model.tuple_tree(_call('group_bounds', project, group, geometry=True, group=group))

    def local_group_bounds(project, group):
        return native_model.tuple_tree(_call('local_group_bounds', project, group, geometry=True, group=group))

    def move_group(project, group, dx=0, dy=0, dz=0, *, validate=True):
        layout = _call('move_group', project, group, *map(_scalar, (dx, dy, dz)), group=group)
        return placed(project, group, _layout(layout), validate)

    def rotate_group(project, group, angle, pivot=None):
        pivot_error = None
        if pivot is not None:
            try:
                pivot = tuple(pivot)
                pivot = tuple(_scalar(value) for value in pivot) if len(pivot) == 3 else ()
            except Exception as error:
                # Preserve legacy ordering: group/layout, angle and envelope
                # errors precede conversion of an invalid pivot iterable.
                pivot_error, pivot = error, ()
        try:
            layout = _call('rotate_group', project, group, _scalar(angle), pivot, geometry=True, group=group)
        except ValueError as error:
            if pivot_error is not None and str(error) == 'Punkt obrotu wymaga współrzędnych X, Y, Z.':
                raise pivot_error from None
            raise
        return placed(project, group, _layout(layout))

    def snap_group(project, group, target_group, direction='x+', align='start', gap=0):
        layout = _call('snap_group', project, group, target_group, direction, align, _scalar(gap), geometry=True)
        return placed(project, group, _layout(layout))

    def nearest_snap(project, group, tolerance=80, align=True, *, validate=True):
        result = _call('nearest_snap', project, group, _scalar(tolerance), bool(align), geometry=True)
        if result['layout'] is None:
            return copy.deepcopy(project), None
        info = result['info']
        if info is not None:
            info = {key: info[key] for key in ('group', 'target_group', 'direction', 'delta', 'distance')}
            info['delta'] = tuple(info['delta'])
        return placed(project, group, _layout(result['layout']), validate), info

    def contacts(project, group=None, tolerance=.1):
        return [{key: item[key] for key in ('group', 'target_group', 'direction', 'gap')}
                for item in _call('contacts', project, group, _scalar(tolerance), geometry=True)]

    implementations = locals()
    for name in OPERATIONS:
        original = namespace[name]
        implementation = wraps(original)(implementations[name])
        implementation._native_cpp = True
        implementation._native_batch_layout = True
        namespace[name] = implementation
