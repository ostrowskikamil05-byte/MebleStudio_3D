"""Presentation visibility does not alter furniture or manufacturing data."""
XRAY_MODES=('Cały zestaw','Wybrany mebel','Wybrany element','Fronty')
DRILL_MODES=('Także zasłonięte','Widoczne powierzchnie')


def xray_part_ids(project,enabled=False,scope='Cały zestaw',selected=None):
    if not enabled:return set()
    if scope not in XRAY_MODES:raise ValueError('Wybierz zakres X-ray z listy.')
    parts=[part for part in project.parts if part.visible]
    if scope=='Cały zestaw':return {part.id for part in parts}
    if scope=='Fronty':
        from assembly import inferred_role
        return {part.id for part in parts if inferred_role(part) in ('door','drawer')}
    chosen=next((part for part in parts if part.id==selected),None)
    if chosen is None:return set()
    if scope=='Wybrany element':return {chosen.id}
    return {part.id for part in parts if part.group==chosen.group}
