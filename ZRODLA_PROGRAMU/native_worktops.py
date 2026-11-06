"""Native shared-worktop solver with the original editor lifecycle retained."""
from functools import wraps


OPERATIONS = ('_distance', '_offset', '_union', '_geometry')


def _number(value):
    if not isinstance(value, (str, bool, int, float)) and hasattr(value, '__float__'):
        return float(value)
    return value


def _polygon(points):
    return [tuple(_number(value) for value in point) for point in points]


def install(namespace):
    import native_model
    if not native_model.available():
        return
    if native_model.call('native', 'info').get('worktop_api_version') != 1:
        raise RuntimeError('Biblioteka C++ nie obsługuje solvera wspólnego blatu. Użyj kompletnej paczki Etap 3.')
    def signed(points):
        return namespace['_signed'](points)

    def distance(a, b):
        if len(a) == 0 or len(b) == 0:
            return min(())
        return native_model.call('workspace_worktop', 'distance', _polygon(a), _polygon(b),
                                 a_ccw=signed(a) > 0, b_ccw=signed(b) > 0)

    def offset(poly, amount):
        if not amount:
            # This is intentionally a shallow copy, including Decimal points.
            return list(poly)
        value = native_model.call('workspace_worktop', 'offset', _polygon(poly), _number(amount),
                                  ccw=signed(poly) > 0)
        return [tuple(point) for point in value]

    def union(polygons):
        polygons = list(polygons)
        value = native_model.call('workspace_worktop', 'union', [_polygon(poly) for poly in polygons],
                                  ccw=[signed(poly) > 0 for poly in polygons])
        return [tuple(point) for point in value]

    def geometry(project, groups, config):
        rows = []
        for group in groups:
            polygon, z, anchor = namespace['_body'](project, group)
            rows.append(dict(polygon=_polygon(polygon), z=_number(z), anchor=anchor,
                             ccw=signed(polygon) > 0))
        if not rows:
            max(())
        result = native_model.call('workspace_worktop', 'geometry', rows, _number(config['overhang']))
        return [tuple(point) for point in result['contour']], tuple(result['origin']), result['anchors']

    implementations = dict(_distance=distance, _offset=offset, _union=union, _geometry=geometry)
    for name in OPERATIONS:
        function = wraps(namespace[name])(implementations[name])
        function._native_cpp = True
        function._native_worktop_solver = True
        namespace[name] = function
