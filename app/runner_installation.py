"""Apply a sourced planning profile to a managed wooden drawer."""
import copy
from assembly import attach, frame_for_part, DEFAULTS, bounds, inferred_role
from runners_catalog import calculate_runner, PROFILES


def drawer_depth_context(project, part, config=None):
    """Clear closed-box envelope, including inset backs, in world millimetres.

    The front offset and rear safety gap are deducted exactly once. A back
    outside the carcass does not reduce its depth. Only backs intersecting the
    planned drawer's X/Z envelope constrain that drawer.
    """
    cfg = {**DEFAULTS, **part.assembly.get('config', {}), **(config or {})}
    f = frame_for_part(project, part)
    start = f['lower'][1] + cfg['drawer_front_gap']
    rear = f['upper'][1]
    z0 = part.z + cfg['gap'] + cfg['drawer_raise']
    z1 = z0 + cfg['drawer_height'] + cfg['drawer_bottom']
    back_ids = []
    for candidate in project.parts:
        if candidate.group != part.group or inferred_role(candidate) != 'back':
            continue
        lo, hi = bounds(candidate)
        intersects = (min(hi[0], f['inner_right']) > max(lo[0], f['inner_left']) + 1e-8
                      and min(hi[2], z1) > max(lo[2], z0) + 1e-8)
        if intersects and lo[1] < rear and hi[1] > start:
            if any(abs(a) > 1e-8 for a in (candidate.rx, candidate.ry, candidate.rz)):
                raise ValueError('Przed doborem prowadnic ustaw plecy bez obrotu; obrócona przeszkoda wymaga osobnego dopasowania.')
            rear = min(rear, lo[1])
            back_ids.append(candidate.id)
    end = rear - cfg['gap']
    if end <= start:
        raise ValueError('Brak użytecznej głębokości szuflady po uwzględnieniu pleców i odstępów.')
    return dict(start_y=start, end_y=end, usable_depth=end-start,
                front_offset=cfg['drawer_front_gap'], rear_gap=cfg['gap'], back_ids=back_ids)


def apply_runner(project, part_id, profile_id, nominal_length, side_thickness=16,
                 bottom_thickness=6, recess=12, *, confirmed_min_depth=None):
    p = copy.deepcopy(next(p for p in project.parts if p.id == part_id))
    if p.role != 'drawer':
        raise ValueError('Wybierz front szuflady utworzonej w szkicu lub bibliotece.')
    f = frame_for_part(project, p)
    cfg = {**DEFAULTS, **p.assembly.get('config', {}),
           'drawer_front_gap': 0, 'runner_confirmed_min_depth': confirmed_min_depth}
    context = drawer_depth_context(project, p, cfg)
    plan = calculate_runner(profile_id, f['inner_right']-f['inner_left'],
                            nominal_length, side_thickness, context['usable_depth'],
                            construction='four_sided', drawer_front_thickness=side_thickness,
                            confirmed_min_depth=confirmed_min_depth)
    if plan['min_cabinet_depth'] is None:
        raise ValueError('Ta karta nie określa głębokości montażowej. Wpisz w kalkulatorze potwierdzoną minimalną głębokość konkretnego artykułu; podgląd samego obrysu nie wystarcza.')
    if not plan['valid']:
        raise ValueError('\n'.join(plan['errors']))
    spec = PROFILES[profile_id]
    opening_gap = spec.get('activation_gap_min') or DEFAULTS['gap']
    cfg.update(runner_profile=profile_id, runner_nominal=nominal_length,
               drawer_t=side_thickness, drawer_bottom=bottom_thickness,
               drawer_bottom_recess=recess, drawer_depth=plan['drawer_length'],
               drawer_clearance=plan['external_clearance_per_side'],
               opening_gap=max(DEFAULTS['gap'], opening_gap), auto_hardware=True)
    result, p = attach(project, p, p.group, 'drawer', cfg)
    travel = plan.get('documented_travel') or (nominal_length*.75 if plan['extension']=='partial' else nominal_length)
    p.motion = dict(mode='drawer', angle=0, travel=travel,
                    travel_status='Producent' if plan.get('documented_travel') else 'Wysuw wizualizacyjny; sprawdź drogę konkretnego artykułu')
    result.validate()
    return result
