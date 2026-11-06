"""Transactional front milling and purchased cabinet accessories, in millimetres.

The defaults are editable workshop dimensions, not manufacturer recipes. Legs
are purchased hardware, never fictitious panels in the cutting list. A plinth
is a genuine panel and is created by furniture_configurator, not this module.
"""
import copy
import math

from core import finite, local_world, inverse_layout_point, inside, uid

FRONT_STYLES = {'smooth': 'Gładki', 'frame': 'Ramka frezowana',
                'fluted': 'Ryflowany', 'finger': 'Uchwyt frezowany'}
MAX_FLUTES = 32


def _integer(value, low, high, label):
    number = finite(value, low, high)
    if int(number) != number:
        raise ValueError(f'{label}: wpisz liczbę całkowitą.')
    return int(number)


def _color(value):
    if not isinstance(value, str) or len(value) != 7 or value[0] != '#':
        raise ValueError('Kolor akcesorium: #RRGGBB.')
    try:
        int(value[1:], 16)
    except ValueError as exc:
        raise ValueError('Kolor akcesorium: #RRGGBB.') from exc
    return value.lower()


def normalize_front_style(style='smooth', cfg=None):
    """Validate requested dimensions; part-specific safe limits are applied later."""
    cfg = cfg or {}
    style = {'plain': 'smooth', 'slab': 'smooth', 'milled': 'frame'}.get(style, style)
    if style not in FRONT_STYLES:
        raise ValueError('Styl frontu: smooth, frame, fluted albo finger.')
    return dict(style=style,
                front_border=finite(cfg.get('front_border', 60), 5, 250),
                front_depth=finite(cfg.get('front_depth', 3), .1, 30),
                front_radius=finite(cfg.get('front_radius', 6), 0, 80),
                flute_width=finite(cfg.get('flute_width', 8), 2, 40),
                flute_spacing=finite(cfg.get('flute_spacing', 20), 3, 100),
                flute_count=_integer(cfg.get('flute_count', 0), 0, MAX_FLUTES, 'Liczba rowków'),
                finger_length=finite(cfg.get('finger_length', 120), 20, 500),
                finger_width=finite(cfg.get('finger_width', 24), 6, 80),
                finger_depth=finite(cfg.get('finger_depth', 6), .1, 30))


def _safe_depth(thickness, requested):
    # A decorative recess never consumes more than 45% of a front and always
    # leaves at least 6 mm. This is a geometry guard, not a strength approval.
    limit = min(thickness * .45, thickness - 6)
    if limit < .1:
        raise ValueError('Frezowany element musi być grubszy niż 6 mm.')
    return min(requested, limit)


def _cut(purpose, shape, u, v, length, width, depth, angle=0, feature='front_style', **extra):
    return dict(type='pocket' if feature == 'front_style' else 'groove',
                shape=shape, face='bottom', u=u, v=v, length=length,
                width=width, depth=depth, angle=angle, config_auto=True,
                configure_feature=feature, purpose=purpose, **extra)


def _overlaps_drill(cut, u, v, radius):
    """Circle/slot/rectangle overlap in local UV, independent of panel pose."""
    du, dv = u - cut['u'], v - cut['v']
    angle = math.radians(-cut.get('angle', 0))
    a, b = du * math.cos(angle) - dv * math.sin(angle), du * math.sin(angle) + dv * math.cos(angle)
    if cut['shape'] == 'circle':
        return math.hypot(a, b) <= cut['width'] / 2 + radius
    if cut['shape'] == 'slot':
        return math.hypot(max(abs(a) - (cut['length'] - cut['width']) / 2, 0), b) <= cut['width'] / 2 + radius
    return math.hypot(max(abs(a) - cut['length'] / 2, 0), max(abs(b) - cut['width'] / 2, 0)) <= radius


def _guard_opposed_drills(part, cuts):
    """Leave a 1 mm web where automatic recesses cross existing blind drilling.

    This is a geometric collision guard; structural suitability still depends
    on the actual panel and selected fittings. A pattern uses one uniform depth.
    """
    if not cuts:
        return None
    holes = [dict(face='top', u=u, v=v, diameter=d, depth=depth) for u, v, d, depth in part.holes]
    holes += [o for o in part.operations if o.get('face') in ('top', 'bottom')]
    depth = min(c['depth'] for c in cuts)
    for cut in cuts:
        opposite = 'top' if cut['face'] == 'bottom' else 'bottom'
        for op in holes:
            if op['face'] == opposite and _overlaps_drill(cut, op['u'], op['v'], op['diameter'] / 2):
                depth = min(depth, part.t - op['depth'] - 1)
    if depth < .1:
        raise ValueError('Frezowanie koliduje z wierceniem od przeciwnej powierzchni. Zmień wzór lub położenie otworu.')
    for cut in cuts:
        cut['depth'] = depth
    return depth


def _clear_front(project, part):
    part.machining = [cut for cut in part.machining
                      if not (cut.get('config_auto') and cut.get('configure_feature') == 'front_style')]
    project.hardware = [h for h in project.hardware
                        if not (h.get('owner_id') == part.id and h.get('config_auto')
                                and h.get('configure_feature') == 'front_style')]


def apply_front_to_part(project, part, style='smooth', config=None):
    """Batch helper: mutate a caller-owned project copy, without global validation.

    Callers must validate the finished project before committing it. The public
    apply_front_style below provides copying and validation for a single front.
    """
    cfg = normalize_front_style(style, config)
    if part.assembly.get('generated_by'):
        raise ValueError('Wybierz front, a nie ścianę skrzynki szuflady.')
    _clear_front(project, part)
    style = cfg['style']
    actual = dict(cfg)
    if style != 'smooth':
        if min(part.u, part.v) < 40:
            raise ValueError('Front jest zbyt mały na wybrany wzór frezowania.')
        border = min(cfg['front_border'], min(part.u, part.v) / 4)
        depth = _safe_depth(part.t, cfg['front_depth'])
        actual.update(front_border=border, front_depth=depth)
        length, width = part.u - 2 * border, part.v - 2 * border
        if style == 'frame':
            radius = min(cfg['front_radius'], min(length, width) / 2 - .1)
            actual['front_radius'] = radius
            purpose = 'Ramka frontu — wybranie środka, pozostawiony obwód'
            if radius < .1:
                part.machining.append(_cut(purpose, 'rect', part.u / 2, part.v / 2, length, width, depth))
            else:
                # Two rectangles and four circles form one exact rounded
                # rectangular pocket. These are real exported machining cuts.
                part.machining.extend([
                    _cut(purpose, 'rect', part.u / 2, part.v / 2, length - 2 * radius, width, depth),
                    _cut(purpose, 'rect', part.u / 2, part.v / 2, length, width - 2 * radius, depth)])
                for u in (border + radius, part.u - border - radius):
                    for v in (border + radius, part.v - border - radius):
                        part.machining.append(_cut(purpose, 'circle', u, v, 2 * radius, 2 * radius, depth))
        elif style == 'fluted':
            groove_width = min(cfg['flute_width'], length / 2, width)
            pitch = max(cfg['flute_spacing'], groove_width + 2)
            fit = max(1, int(math.floor((length - groove_width) / pitch)) + 1)
            requested = cfg['flute_count']
            count = requested or min(MAX_FLUTES, fit)
            if count > fit:
                raise ValueError(f'Na tym froncie mieści się najwyżej {fit} rowków przy podanym rozstawie.')
            # Keep requested pitch; if the auto count is capped the pattern is
            # centred, never overlaps grooves or silently changes dimensions.
            first = part.u / 2 - (count - 1) * pitch / 2
            for i in range(count):
                part.machining.append(_cut('Ryflowanie frontu', 'slot', first + i * pitch,
                                          part.v / 2, width, groove_width, depth, angle=90))
            actual.update(flute_width=groove_width, flute_spacing=pitch, flute_count=count)
        elif style == 'finger':
            from opening_options import clear, OPTIONS, refresh_openings
            clear(project, part.id)
            pull_width = min(cfg['finger_width'], part.v / 3)
            pull_length = min(cfg['finger_length'], part.u - 20)
            pull_depth = _safe_depth(part.t, cfg['finger_depth'])
            if pull_length < pull_width:
                raise ValueError('Front jest zbyt wąski na uchwyt frezowany.')
            level = part.v - max(pull_width / 2 + 8, min(40, part.v / 4))
            project.hardware.append(dict(kind='opening', owner_id=part.id,
                name=OPTIONS['finger_pocket']['label'], option='finger_pocket',
                qty=0, price=0, length=pull_length, width=pull_width, depth=pull_depth,
                level=level, level_ratio=level / part.v, center_u=part.u / 2,
                u_ratio=.5, angle=0, profile_edge='top', color='#242628', source='',
                note='Frezowanie we froncie; nie jest kupowanym okuciem.',
                config_auto=True, configure_feature='front_style'))
            refresh_openings(project, part.id)
            for cut in part.machining:
                if cut.get('opening_owner') == part.id:
                    cut.update(config_auto=True, configure_feature='front_style')
            actual.update(finger_length=pull_length, finger_width=pull_width, finger_depth=pull_depth)
        generated = [cut for cut in part.machining if cut.get('config_auto') and cut.get('configure_feature') == 'front_style']
        safe_depth = _guard_opposed_drills(part, generated)
        if safe_depth is not None:
            actual['finger_depth' if style == 'finger' else 'front_depth'] = safe_depth
            for h in project.hardware:
                if h.get('owner_id') == part.id and h.get('config_auto') and h.get('configure_feature') == 'front_style':
                    h['depth'] = safe_depth
    part.configurator_source['front_style'] = actual
    return part


def apply_front_style(project, part_id, style='smooth', config=None, *, validate=True):
    """Return a modified project copy; failed milling leaves the source unchanged."""
    result = copy.deepcopy(project)
    part = next((p for p in result.parts if p.id == getattr(part_id, 'id', part_id)), None)
    if part is None:
        raise ValueError('Nie znaleziono frontu w projekcie.')
    apply_front_to_part(result, part, style, config)
    if validate:
        result.validate()
    return result


def normalize_accessories(config=None):
    c = config or {}
    count = _integer(c.get('leg_count', 4), 4, 6, 'Liczba nóg')
    if count not in (4, 6):
        raise ValueError('Liczba nóg: 4 albo 6.')
    mount = c.get('led_mount', 'under_shelf')
    side = c.get('led_side', 'left')
    if mount not in ('under_shelf', 'side') or side not in ('left', 'right'):
        raise ValueError('LED: under_shelf albo side; bok left albo right.')
    return dict(leg_height=finite(c.get('leg_height', 0), 0, 500), leg_count=count,
        leg_diameter=finite(c.get('leg_diameter', 40), 10, 120),
        leg_setback=finite(c.get('leg_setback', 60), 0, 250),
        leg_color=_color(c.get('leg_color', '#242628')), leg_price=finite(c.get('leg_price', 0), 0, 100000),
        led=bool(c.get('led', False)), led_mount=mount, led_side=side,
        led_width=finite(c.get('led_width', 12), 4, 50), led_depth=finite(c.get('led_depth', 3), .1, 20),
        led_margin=finite(c.get('led_margin', 30), 5, 150),
        led_price_per_m=finite(c.get('led_price_per_m', 0), 0, 100000),
        led_profile=bool(c.get('led_profile', True)), led_diffuser=bool(c.get('led_diffuser', True)),
        led_profile_price_per_m=finite(c.get('led_profile_price_per_m', 0), 0, 100000),
        led_diffuser_price_per_m=finite(c.get('led_diffuser_price_per_m', 0), 0, 100000),
        led_source=str(c.get('led_source', '')).strip(), led_source_price=finite(c.get('led_source_price', 0), 0, 100000),
        led_color=_color(c.get('led_color', '#fff3bf')), led_targets=c.get('led_targets'))


def _body_bounds(project, group, bounds):
    if bounds is not None:
        if 'lower' in bounds and 'upper' in bounds:
            lo, hi = bounds['lower'], bounds['upper']
        else:
            lo = (bounds['body_x'], bounds['body_y'], bounds['body_z'])
            hi = (lo[0] + bounds['width'], lo[1] + bounds['depth'], lo[2] + bounds['body_height'])
        if len(lo) != 3 or len(hi) != 3:
            raise ValueError('Obrys korpusu wymaga trzech współrzędnych.')
        lo = tuple(finite(x) for x in lo)
        hi = tuple(finite(x) for x in hi)
        if any(b <= a for a, b in zip(lo, hi)):
            raise ValueError('Korpus musi mieć dodatnie wymiary.')
        return lo, hi
    body = [p for p in project.parts if p.group == group and p.role in ('left', 'right', 'bottom', 'top', 'partition')
            and not p.assembly.get('generated_by')]
    if not body:
        raise ValueError('Dodaj korpus, zanim dodasz nogi lub oświetlenie.')
    points = [local_world(p, u, v, w) for p in body for u, v in p.points() for w in (0, p.t)]
    return tuple(min(pt[i] for pt in points) for i in range(3)), tuple(max(pt[i] for pt in points) for i in range(3))


def _install_led(project, members, group, cfg):
    if cfg['led_targets'] is not None:
        if not isinstance(cfg['led_targets'], (list, tuple)):
            raise ValueError('Lista elementów LED musi zawierać identyfikatory elementów.')
        targets = [p for p in members if p.id in cfg['led_targets']]
        if len(targets) != len(set(cfg['led_targets'])):
            raise ValueError('Nie znaleziono wybranego elementu pod LED w tym meblu.')
    elif cfg['led_mount'] == 'under_shelf':
        targets = sorted((p for p in members if p.role == 'shelf' and p.plane == 'XY'), key=lambda p: (p.z, p.id))
        if not targets:
            targets = sorted((p for p in members if p.role == 'top' and p.plane == 'XY'), key=lambda p: (p.z, p.id))[:1]
    else:
        targets = sorted((p for p in members if p.role == cfg['led_side'] and p.plane == 'YZ'), key=lambda p: p.id)[:1]
    if not targets:
        raise ValueError('Nie znaleziono półki, wieńca lub boku dla wybranego montażu LED.')
    for p in targets:
        horizontal = cfg['led_mount'] == 'under_shelf'
        if (horizontal and p.plane != 'XY') or (not horizontal and p.plane != 'YZ'):
            raise ValueError('LED pod półką wymaga płaszczyzny XY, LED na boku płaszczyzny YZ.')
        major, minor = (p.u, p.v) if horizontal else (p.v, p.u)
        margin = min(cfg['led_margin'], major / 4, minor / 4)
        length, width = major - 2 * margin, cfg['led_width']
        if length < width or minor < 2 * margin + width:
            raise ValueError('Element jest zbyt mały na wybrany profil LED.')
        depth = _safe_depth(p.t, cfg['led_depth'])
        u, v = (p.u / 2, margin + width / 2) if horizontal else (margin + width / 2, p.v / 2)
        face = 'bottom' if horizontal or p.role == 'right' else 'top'
        cut = _cut('Rowek pod profil / taśmę LED', 'slot', u, v, length, width, depth,
                   angle=0 if horizontal else 90, feature='led')
        cut['face'] = face
        depth = _guard_opposed_drills(p, [cut])
        p.machining.append(cut)
        base = dict(owner_id=p.id, group=group, config_auto=True, configure_feature='led',
                    length=length, width=width, depth=depth, u=u, v=v, face=face,
                    axis='u' if horizontal else 'v', profile=cfg['led_profile'], diffuser=cfg['led_diffuser'],
                    source='', requires_review=True)
        note = f'Własny montaż: rowek {width:g} × {depth:g} mm, długość {length:g} mm. Dobierz konkretny profil, moc i zasilacz.'
        project.hardware.append(dict(base, kind='led_strip', name='Taśma LED — własny model',
                                     qty=length / 1000, unit='m', price=cfg['led_price_per_m'], color=cfg['led_color'], note=note))
        if cfg['led_profile']:
            project.hardware.append(dict(base, kind='led_profile', name='Profil LED — własny model',
                                         qty=length / 1000, unit='m', price=cfg['led_profile_price_per_m'], note=note))
        if cfg['led_diffuser']:
            project.hardware.append(dict(base, kind='led_diffuser', name='Dyfuzor LED — własny model',
                                         qty=length / 1000, unit='m', price=cfg['led_diffuser_price_per_m'], note=note))
    if cfg['led_source']:
        project.hardware.append(dict(kind='led_power', owner_id=targets[0].id, group=group,
            name='Zasilacz LED — ' + cfg['led_source'], qty=1, unit='szt.', price=cfg['led_source_price'],
            config_auto=True, configure_feature='led', source='', requires_review=True,
            note='Model własny. Dobierz moc, napięcie i sposób montażu do rzeczywistej taśmy LED.'))


def install_accessories(project, group, config=None, bounds=None, *, validate=True):
    """Return a copy with deterministic replacement of this group's auto fittings.

    bounds uses construction coordinates: {lower, upper, floor?}, or
    {body_x,body_y,body_z,width,depth,body_height,floor?}. Height of the body is
    exclusive of legs. If floor is omitted it is body_z minus leg_height.
    """
    result = copy.deepcopy(project)
    cfg = normalize_accessories(config)
    members = [p for p in result.parts if p.group == group]
    if not members:
        raise ValueError('Nie znaleziono wybranego mebla.')
    ids = {p.id for p in members}
    for p in members:
        p.machining = [c for c in p.machining
                       if not (c.get('config_auto') and c.get('configure_feature') == 'led')]
    result.hardware = [h for h in result.hardware
                       if not (h.get('config_auto') and h.get('configure_feature') in ('legs', 'led')
                               and (h.get('group') == group or h.get('owner_id') in ids))]
    if cfg['leg_height']:
        lo, hi = _body_bounds(result, group, bounds)
        owner = min((p for p in members if p.role == 'bottom' and p.plane == 'XY'), key=lambda p: p.z, default=None)
        if owner is None:
            raise ValueError('Nogi wymagają dna mebla, do którego są przypisane.')
        floor = finite(bounds['floor']) if bounds is not None and 'floor' in bounds else lo[2] - cfg['leg_height']
        if abs(floor + cfg['leg_height'] - lo[2]) > .01:
            raise ValueError('Wysokość nóg musi odpowiadać odległości dna korpusu od podłogi.')
        radius = cfg['leg_diameter'] / 2
        # A slightly broader circular foot is part of the purchased leg.
        foot_radius = radius * 1.15
        setback = max(cfg['leg_setback'], foot_radius + 1)
        if min(hi[0] - lo[0], hi[1] - lo[1]) <= 2 * setback:
            raise ValueError('Korpus jest zbyt mały na wybrany rozstaw / średnicę nóg.')
        xs = [lo[0] + setback, hi[0] - setback]
        if cfg['leg_count'] == 6:
            xs.insert(1, (lo[0] + hi[0]) / 2)
        positions = [dict(x=x, y=y, z=floor) for x in xs for y in (lo[1] + setback, hi[1] - setback)]
        result.hardware.append(dict(kind='leg', owner_id=owner.id, group=group,
            name=f'Noga meblowa — własny model Ø{cfg["leg_diameter"]:g} × {cfg["leg_height"]:g} mm',
            qty=cfg['leg_count'], unit='szt.', price=cfg['leg_price'], height=cfg['leg_height'],
            diameter=cfg['leg_diameter'], setback=setback, positions=positions, color=cfg['leg_color'],
            foot_diameter=foot_radius * 2, config_auto=True, configure_feature='legs', source='', requires_review=True,
            note='Model zakupowy, bez automatycznego wiercenia mocowań. Dobierz artykuł, nośność i sposób mocowania.'))
    if cfg['led']:
        _install_led(result, members, group, cfg)
    if validate:
        result.validate()
    return result


def accessory_vertices(project, group=None, local=False, *, include_all=False):
    """Nominal fitting envelope, with no expensive panel CAD mesh generation.

    Includes new support/accessory geometry (legs and LED) by default. Set
    include_all=True for handles, hinges, tracks and every visible fitting.
    local=True reverses only the owner's final room placement; it preserves
    construction transforms and the supplied closed/posed state of the model.
    """
    from fitting_visuals import fitting_faces
    owners = {p.id: p for p in project.parts}
    vertices = []
    for owner, face, color in fitting_faces(project, None if include_all else {'leg', 'led_strip'}):
        p = owners.get(owner)
        if p is None or not p.visible or (group is not None and p.group != group):
            continue
        vertices.extend(inverse_layout_point(p, point) if local else tuple(point) for point in face)
    return vertices


LED_SURFACES = ('top', 'bottom', 'u0', 'u1', 'v0', 'v1')


def led_surface(part, face):
    """Face coordinates A,B with an exterior normal, in the part's UVW frame."""
    if face not in LED_SURFACES:raise ValueError('Wybierz powierzchnię LED: top/bottom/u0/u1/v0/v1.')
    return {'top':((0,0,part.t),(1,0,0),(0,1,0),(0,0,1),part.u,part.v),
            'bottom':((0,0,0),(1,0,0),(0,1,0),(0,0,-1),part.u,part.v),
            'u0':((0,0,0),(0,1,0),(0,0,1),(-1,0,0),part.v,part.t),
            'u1':((part.u,0,0),(0,1,0),(0,0,1),(1,0,0),part.v,part.t),
            'v0':((0,0,0),(1,0,0),(0,0,1),(0,-1,0),part.u,part.t),
            'v1':((0,part.v,0),(1,0,0),(0,0,1),(0,1,0),part.u,part.t)}[face]


def led_proposal(part, local_hit=None, options=None):
    """Pure fitted LED description; no stock item or machining is created here."""
    options=options or {};hit=local_hit or {}
    if not isinstance(hit,dict):
        hit=dict(zip(('u','v','w'),hit))
    face=options.get('face') or hit.get('face','bottom')
    origin,a,b,n,la,lb=led_surface(part,face)
    if face in ('u0','u1','v0','v1') and (part.contour or part.profile):
        raise ValueError('Montaż na krawędzi konturu wymaga ręcznego doboru; wybierz powierzchnię +T lub −T.')
    axis=options.get('axis','auto')
    if axis not in ('auto','u','v'):raise ValueError('Kierunek LED: auto/u/v.')
    axis=('u' if la>=lb else 'v') if axis=='auto' else axis
    width=finite(options.get('width',12),1,100)
    margin=finite(options.get('margin',30),0,500)
    major,minor=(la,lb) if axis=='u' else (lb,la)
    margin=min(margin,major/4)
    if minor < width+1 or major-2*margin < width:raise ValueError('Profil LED nie mieści się na tej powierzchni. Zmniejsz szerokość lub wybierz inną powierzchnię.')
    point=(finite(hit.get('u',part.u/2)),finite(hit.get('v',part.v/2)),finite(hit.get('w',part.t/2)))
    coords=[sum((point[i]-origin[i])*v[i] for i in range(3)) for v in (a,b)]
    for c,size in zip(coords,(la,lb)):
        if not -.01<=c<=size+.01:raise ValueError('Punkt montażu leży poza wybraną powierzchnią.')
    requested=options.get('length')
    length=major-2*margin if requested in (None,'',0) else min(finite(requested,width,10000),major-2*margin)
    along=major/2 if requested in (None,'',0) else min(major-margin-length/2,max(margin+length/2,coords[0 if axis=='u' else 1]))
    across=min(minor-width/2-.5,max(width/2+.5,coords[1 if axis=='u' else 0]))
    u,v=(along,across) if axis=='u' else (across,along)
    if face in ('top','bottom'):
        def contained(length):
            return all(inside((u+(da*length/2 if axis=='u' else db*width/2),v+(db*width/2 if axis=='u' else da*length/2)),part.points()) for da,db in ((-1,-1),(-1,1),(1,-1),(1,1)))
        # Start at the cursor for shaped parts, then shorten without leaving the actual contour.
        if (part.contour or part.profile) and not contained(length):
            u,v=point[0],point[1]
            for _ in range(60):
                if contained(length):break
                length*=.9
            if length<width or not contained(length):raise ValueError('LED nie mieści się w konturze przy tym punkcie montażu.')
    groove=options.get('groove',False)
    if type(groove) is not bool:raise ValueError('Rowek LED wymaga TAK / NIE.')
    if groove and face not in ('top','bottom'):raise ValueError('Rowek CAD obsługuje powierzchnie +T i −T. Na krawędzi wybierz montaż bez rowka.')
    depth=_safe_depth(part.t,finite(options.get('depth',3),.1,20)) if groove else finite(options.get('depth',2),.1,20)
    cut=dict(type='groove',shape='slot',face=face,u=u,v=v,length=length,width=width,depth=depth,
             angle=0 if axis=='u' else 90,purpose='Rowek LED — ręczne rozmieszczenie',config_auto=False)
    if groove:
        for op in [dict(face='top',u=x,v=y,diameter=d,depth=h) for x,y,d,h in part.holes]+part.operations:
            if op.get('face')==face and _overlaps_drill(cut,op['u'],op['v'],op['diameter']/2):
                raise ValueError('Rowek LED koliduje z otworem od tej samej powierzchni. Przesuń LED lub wyłącz rowek.')
        _guard_opposed_drills(part,[cut]);depth=cut['depth']
        from features import validate_cut
        validate_cut(part,cut)
    center=tuple(origin[i]+u*a[i]+v*b[i] for i in range(3))
    return dict(face=face,local_face=face,u=u,v=v,axis=axis,length=length,width=width,depth=depth,
                margin=margin,groove=groove,local_center=center,
                local_tangent=a if axis=='u' else b,local_cross=b if axis=='u' else a,local_normal=n,
                a_ratio=u/la,b_ratio=v/lb,length_auto=requested in (None,'',0),
                requested_length=None if requested in (None,'',0) else requested,
                profile=bool(options.get('profile',True)),diffuser=bool(options.get('diffuser',True)),
                color=_color(options.get('color','#fff3bf')),cut=cut if groove else None)


def install_manual_led(project, part_id, local_hit=None, options=None, *, attachment_id=None, validate=True):
    """Atomic purchased LED placement on any planar local face; groove is opt-in."""
    result=copy.deepcopy(project);part=next((p for p in result.parts if p.id==part_id),None)
    if part is None:raise ValueError('Nie znaleziono elementu dla LED.')
    cfg=led_proposal(part,local_hit,options);options=options or {};ident=attachment_id or uid()
    result.hardware=[h for h in result.hardware if h.get('attachment_id')!=ident]
    for p in result.parts:p.machining=[c for c in p.machining if c.get('attachment_id')!=ident]
    cut=cfg.pop('cut')
    if cut:part.machining.append(dict(cut,attachment_id=ident,manual=True))
    common=dict(cfg,owner_id=part.id,group=part.group,attachment_id=ident,manual=True,config_auto=False,
                configure_feature='manual_led',source='',requires_review=True)
    note=('Rowek' if cfg['groove'] else 'Montaż powierzchniowy bez rowka')+f'; {cfg["length"]:g} × {cfg["width"]:g} mm. Własny model; dobierz artykuł, moc i mocowanie.'
    for kind,label,price in [('led_strip','Taśma LED','price_per_m'),('led_profile','Profil LED','profile_price_per_m'),('led_diffuser','Dyfuzor LED','diffuser_price_per_m')]:
        if kind=='led_profile' and not cfg['profile'] or kind=='led_diffuser' and not cfg['diffuser']:continue
        result.hardware.append(dict(common,kind=kind,name=label+' — ręczne rozmieszczenie',qty=cfg['length']/1000,unit='m',price=finite(options.get(price,0),0,100000),note=note,default_note=note))
    if validate:result.validate()
    return result


def refresh_manual_led(project):
    """Refresh dimensions on a caller-owned candidate; retain attachment IDs/prices."""
    records=[copy.deepcopy(h) for h in project.hardware if h.get('kind')=='led_strip' and h.get('configure_feature')=='manual_led']
    owners={p.id:p for p in project.parts}
    for h in records:
        p=owners.get(h['owner_id'])
        if p is None:raise ValueError('Ręczny LED wskazuje usunięty element; usuń montaż razem z właścicielem.')
        face=h['local_face'];origin,a,b,n,la,lb=led_surface(p,face)
        center=tuple(origin[i]+la*h['a_ratio']*a[i]+lb*h['b_ratio']*b[i] for i in range(3))
        options={k:h[k] for k in ('axis','width','depth','margin','groove','profile','diffuser','color')}
        options.update(face=face,length=None if h.get('length_auto') else h.get('requested_length',h['length']),price_per_m=h['price'])
        for item in project.hardware:
            if item.get('attachment_id')==h['attachment_id']:
                if item.get('kind')=='led_profile':options['profile_price_per_m']=item['price']
                if item.get('kind')=='led_diffuser':options['diffuser_price_per_m']=item['price']
        # Remove old groove before checking collisions against the refreshed host.
        candidate=install_manual_led(project,p.id,dict(zip(('u','v','w'),center)),options,attachment_id=h['attachment_id'],validate=False)
        prior={item['kind']:item for item in project.hardware if item.get('attachment_id')==h['attachment_id']}
        for item in candidate.hardware:
            old=prior.get(item.get('kind')) if item.get('attachment_id')==h['attachment_id'] else None
            if old:
                for key in ('name','price','article','manufacturer','supplier','source','note'):
                    if key=='note' and old.get('note')==old.get('default_note'):continue
                    if key in old:item[key]=copy.deepcopy(old[key])
        project.parts=candidate.parts;project.hardware=candidate.hardware;owners={p.id:p for p in project.parts}
    return project


def led_local_geometry(part, hardware):
    """Lightweight geometry/color pairs; supports legacy automatic LED records."""
    from fitting_visuals import slot_prism
    h=hardware;length,width,depth=(h.get(k,0) for k in ('length','width','depth'))
    if min(length,width,depth)<=0:return []
    if h.get('local_center') is None:
        top=h.get('face')=='top';center=(h['u'],h['v'],part.t if top else 0)
        a=(0,1,0) if h.get('axis')=='v' else (1,0,0);b=(1,0,0) if h.get('axis')=='v' else (0,1,0)
        normal=(0,0,1) if top else (0,0,-1);groove=True
    else:center=h['local_center'];a=h['local_tangent'];b=h['local_cross'];normal=h['local_normal'];groove=h.get('groove',False)
    def geometry(l,w,z,height):
        cross=(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
        flip=sum(cross[i]*normal[i] for i in range(3))<0
        return [[tuple(center[i]+x*a[i]+y*b[i]+t*normal[i] for i in range(3)) for x,y,t in (reversed(face) if flip else face)] for face in slot_prism(0,0,z,l,w,height)]
    out=[]
    if h.get('profile',True):out.extend((f,'#949ba2') for f in geometry(length,width,-depth if groove else 0,depth))
    light_width=width*.75 if h.get('profile',True) else width;light_length=max(light_width,length-width*.25)
    out.extend((f,h.get('color','#fff3bf')) for f in geometry(light_length,light_width,0 if groove else depth,.3))
    return out
