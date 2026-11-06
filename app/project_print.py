"""Self-contained A4 project book: layout, assemblies, parts and sheet plan.

Presentation uses nominal polygon prisms, never CAD triangulation. All model
dimensions come from the closed source model; exploding only moves drawings.
"""
import copy
import html
import hashlib
import math
from pathlib import Path

from core import Camera, cut_plan, shade, world
from production_docs import part_svg


def _esc(value):
    return html.escape(str(value), quote=True)


def _num(value):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError('Wymiary do wydruku muszą być skończone.')
    if abs(value)<.005:
        value = 0
    return f'{value:.2f}'.rstrip('0').rstrip('.')


def _vertices(part):
    return [world(part, u, v, t) for t in (0, part.t) for u, v in part.points()]


def _accessory_project(project,parts):
    view=copy.copy(project);view.parts=copy.deepcopy(parts)
    for p in view.parts:p.visible=True
    ids={p.id for p in parts}
    view.hardware=[h for h in project.hardware if h.get('owner_id') in ids and h.get('kind') in ('leg','led_strip')]
    return view


def _bounds(parts,project=None):
    vertices = [v for p in parts for v in _vertices(p)]
    if project:
        from configurator_accessories import accessory_vertices
        vertices.extend(accessory_vertices(_accessory_project(project,parts)))
    if not vertices:
        raise ValueError('Projekt nie zawiera elementów do wydruku.')
    return (tuple(min(v[i] for v in vertices) for i in range(3)),
            tuple(max(v[i] for v in vertices) for i in range(3)))


def _size_text(bounds):
    lo, hi = bounds
    return ' × '.join(_num(hi[i]-lo[i]) for i in (0, 1, 2))+' mm (X × Y × Z)'


def _nominal_project(project, parts):
    """Cut-plan validation is independent of expensive 3D finishing features."""
    nominal = copy.deepcopy(project)
    nominal.parts = copy.deepcopy(parts)
    nominal.hardware = []
    for p in nominal.parts:
        p.fillets, p.chamfers, p.machining, p.operations, p.holes = [], [], [], [], []
        p.decor = {}
        p.assembly = {}
    nominal.validate()
    return nominal


def _edge_text(part):
    if part.contour:
        return 'Cały kontur' if getattr(part, 'contour_edge', False) else 'Brak'
    return ', '.join(label for label, enabled in zip(('U0', 'U1', 'V0', 'V1'), part.edges) if enabled) or 'Brak'


def _dimension(a, b, text, *, offset=20, color='#173b46'):
    ax, ay = a[:2]
    bx, by = b[:2]
    dx, dy = bx-ax, by-ay
    norm = math.hypot(dx, dy)
    if norm < 2:
        return ''
    nx, ny = -dy/norm*offset, dx/norm*offset
    aa, bb = (ax+nx, ay+ny), (bx+nx, by+ny)
    return (f'<g class="dim" stroke="{color}" fill="none" stroke-width="1.3">'
            f'<path d="M{ax:.2f},{ay:.2f}L{aa[0]:.2f},{aa[1]:.2f} M{bx:.2f},{by:.2f}L{bb[0]:.2f},{bb[1]:.2f}"/>'
            f'<path d="M{aa[0]:.2f},{aa[1]:.2f}L{bb[0]:.2f},{bb[1]:.2f}" marker-start="url(#arrow)" marker-end="url(#arrow)"/>'
            f'</g><text x="{(aa[0]+bb[0])/2:.2f}" y="{(aa[1]+bb[1])/2-5:.2f}" text-anchor="middle"'
            f' font-size="16" fill="{color}" class="dim-label">{_esc(text)}</text>')


def _diagram(project, parts, *, view='iso', exploded=0, numbers=None, callout_parts=None,
             width=1000, height=570, overall_dimensions=True):
    bounds = _bounds(parts,project)
    marker_key = repr((view, exploded, [p.id for p in parts], [p.id for p in callout_parts or []]))
    marker_id = 'arrow-'+hashlib.sha256(marker_key.encode('utf-8')).hexdigest()[:16]
    lo, hi = bounds
    center = tuple((lo[i]+hi[i])/2 for i in range(3))
    angles = {'iso': (-38, 24), 'front': (0, 0), 'top': (0, 90)}
    camera = Camera(*angles[view], center=center, scale=1)
    shifts, faces, vertices, centers = {}, [], [], {}
    for part in parts:
        xyz = world(part, part.u/2, part.v/2, part.t/2)
        shift = tuple((xyz[i]-center[i])*exploded for i in range(3))
        shifts[part.id] = shift
        points = part.points()
        bottom = [tuple(x+shift[i] for i,x in enumerate(world(part,u,v,0))) for u,v in points]
        top = [tuple(x+shift[i] for i,x in enumerate(world(part,u,v,part.t))) for u,v in points]
        vertices.extend(bottom+top)
        centers[part.id] = tuple(xyz[i]+shift[i] for i in range(3))
        geometry = [bottom[::-1], top]
        geometry.extend([bottom[i],bottom[(i+1)%len(points)],top[(i+1)%len(points)],top[i]] for i in range(len(points)))
        color = project.mat(part.material).color
        for face in geometry:
            a,b,c = face[:3]
            ab,ac = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
            n = (ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
            norm = math.sqrt(sum(v*v for v in n)) or 1
            light = abs(sum(n[i]*(-.3,-.5,.81)[i] for i in range(3))/norm)
            faces.append((face, shade(color,.72+light*.28)))
    from fitting_visuals import fitting_faces
    for owner,geometry,color in fitting_faces(_accessory_project(project,parts)):
        shift=shifts.get(owner,(0,0,0))
        shifted=[tuple(point[i]+shift[i] for i in range(3)) for point in geometry]
        vertices.extend(shifted);faces.append((shifted,color))
    projected = [camera.view(p) for p in vertices]
    minx,maxx = min(p[0] for p in projected),max(p[0] for p in projected)
    miny,maxy = min(p[1] for p in projected),max(p[1] for p in projected)
    side_margin = 240 if callout_parts else 90
    camera.scale = min((width-2*side_margin)/max(maxx-minx,1), (height-130)/max(maxy-miny,1))
    camera.pan = (-camera.scale*(minx+maxx)/2, camera.scale*(miny+maxy)/2)
    items = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img"'
             f' aria-label="{_esc("Widok rozstrzelony" if exploded else "Widok "+view)}" data-explosion="{_num(exploded)}">',
             f'<defs><marker id="{marker_id}" markerWidth="5" markerHeight="5" refX="2.5" refY="2.5" orient="auto-start-reverse"><path d="M0,0 L5,2.5 L0,5" fill="#173b46"/></marker></defs>',
             '<rect width="100%" height="100%" fill="white"/>']
    drawn = []
    for face,color in faces:
        pts = [camera.project(p,width,height) for p in face]
        drawn.append((sum(p[2] for p in pts)/len(pts), pts,color))
    for _, pts, color in sorted(drawn,key=lambda f:f[0]):
        xy = ' '.join(f'{p[0]:.2f},{p[1]:.2f}' for p in pts)
        items.append(f'<polygon points="{xy}" fill="{color}" stroke="#647078" stroke-width=".65"/>')
    if overall_dimensions:
        # These endpoints always belong to the CLOSED bounds, independent of
        # the visual explosion displacement.
        for index,axis in enumerate('XYZ'):
            base = (hi[0],lo[1],lo[2]) if view=='iso' and index==1 else (lo[0],lo[1],lo[2])
            end = list(base); end[index] = hi[index]
            items.append(_dimension(camera.project(base,width,height),camera.project(end,width,height),
                                    f'{axis} {_num(hi[index]-lo[index])} mm',offset=-(25+index*7) if index==2 else 25+index*7))
    if numbers:
        for part in callout_parts or parts:
            if not callout_parts:
                x,y,_ = camera.project(centers[part.id],width,height)
                items.append(f'<g class="part-number" data-part-id="{_esc(part.id)}"><circle cx="{x:.2f}" cy="{y:.2f}" r="15" fill="white" stroke="#173b46"/><text x="{x:.2f}" y="{y+5:.2f}" text-anchor="middle" font-size="16">{numbers[part.id]}</text></g>')
        if callout_parts:
            sides = [[],[]]
            for part in callout_parts:
                x,y,_ = camera.project(centers[part.id],width,height)
                sides[int(x>=width/2)].append((y,x,part))
            for right,rows in enumerate(sides):
                rows.sort(key=lambda r:r[0])
                step = min(35,(height-70)/max(1,len(rows)))
                start = (height-(len(rows)-1)*step)/2
                label_x = width-220 if right else 18
                for index,(_,x,part) in enumerate(rows):
                    y = start+index*step
                    point = camera.project(centers[part.id],width,height)
                    edge_x = label_x-6 if right else label_x+205
                    label = f'#{numbers[part.id]} · {_num(part.u)} × {_num(part.v)} × {_num(part.t)} mm'
                    items.append(f'<g class="part-callout" data-part-id="{_esc(part.id)}" data-u="{_num(part.u)}" data-v="{_num(part.v)}" data-t="{_num(part.t)}">'
                        f'<path d="M{point[0]:.2f},{point[1]:.2f}L{edge_x:.2f},{y:.2f}" stroke="#50666d" fill="none"/>'
                        f'<circle cx="{point[0]:.2f}" cy="{point[1]:.2f}" r="2.5" fill="#173b46"/>'
                        f'<rect x="{label_x-3}" y="{y-13:.2f}" width="210" height="23" rx="3" fill="white" fill-opacity=".94"/>'
                        f'<text x="{label_x}" y="{y+4:.2f}" font-size="16" fill="#173b46">{_esc(label)}</text></g>')
    items.append('</svg>')
    return ''.join(items).replace('url(#arrow)',f'url(#{marker_id})')


def _parts_table(project, parts, numbers):
    rows = ['<table class="parts"><thead><tr><th>Nr</th><th>Element / materiał</th><th>Gotowe U × V × T [mm]</th><th>Cięcie U × V [mm]</th><th>Szt.</th><th>Obrzeża</th></tr></thead><tbody>']
    for part in parts:
        a,b = part.blank(project.edge_t)
        rows.append(f'<tr data-part-id="{_esc(part.id)}"><td>{numbers[part.id]}</td><td>{_esc(part.name)}<br><small>{_esc(project.mat(part.material).name)}</small></td>'
                    f'<td>{_num(part.u)} × {_num(part.v)} × {_num(part.t)}</td><td>{_num(a)} × {_num(b)}</td><td>1</td><td>{_esc(_edge_text(part))}</td></tr>')
    return ''.join(rows)+'</tbody></table>'


def _operations(part):
    drills = [dict(u=u,v=v,diameter=d,depth=h,face='top') for u,v,d,h in part.holes]+part.operations
    out = []
    if drills:
        out.append('<h3>Wiercenia</h3><table><thead><tr><th>Nr</th><th>Strona</th><th>Współrzędne 1 / 2 [mm]</th><th>Ø / głębokość [mm]</th><th>Pogłębienie [mm]</th></tr></thead><tbody>')
        for i,op in enumerate(drills,1):
            cone = '—' if not op.get('countersink_diameter') else f'Ø{_num(op["countersink_diameter"])} × {_num(op["countersink_depth"])}'
            out.append(f'<tr><td>{i}</td><td>{_esc(op["face"])}</td><td>{_num(op["u"])} / {_num(op["v"])}</td><td>{_num(op["diameter"])} / {_num(op["depth"])}</td><td>{_esc(cone)}</td></tr>')
        out.append('</tbody></table>')
    if part.machining:
        out.append('<h3>Frezowanie</h3><table><thead><tr><th>Rodzaj / strona</th><th>Środek U,V</th><th>Długość × szerokość</th><th>Głębokość</th><th>Kąt</th></tr></thead><tbody>')
        for cut in part.machining:
            out.append(f'<tr><td>{_esc(cut["type"]+" / "+cut["shape"]+" / "+cut["face"])}</td><td>{_num(cut["u"])} / {_num(cut["v"])}</td><td>{_num(cut["length"])} × {_num(cut["width"])}</td><td>{_num(part.t if cut["type"]=="cutout" else cut["depth"])}</td><td>{_num(cut.get("angle",0))}°</td></tr>')
        out.append('</tbody></table>')
    for attr,label,param in (('fillets','Zaokrąglenia','radius'),('chamfers','Fazy 45°','distance')):
        values = getattr(part,attr)
        if values:
            out.append('<p>'+label+': '+_esc('; '.join(f'{item["edge"]}: {_num(item[param])} mm' for item in values))+'</p>')
    if part.profile:
        out.append('<p>Dokładny profil zapisany w modelu: '+_esc(part.profile.get('kind',''))+'. Krzywe w tym rzucie są próbkowane; geometria dokładna znajduje się w STEP.</p>')
    return ''.join(out)


def _sheet_svg(sheet, numbers):
    width,height = sheet['u'],sheet['v']
    font = max(width,height)/85
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-font*2:g} {-font*2:g} {width+font*4:g} {height+font*4:g}"><rect x="0" y="0" width="{width:g}" height="{height:g}" fill="#faf8f1" stroke="#444" stroke-width="{font/9:g}"/>']
    for piece in sheet['pieces']:
        x,y,u,v = [piece[k] for k in ('x','y','u','v')]
        label = f'#{numbers[piece["id"]]}'
        out.append(f'<g data-part-id="{_esc(piece["id"])}"><rect x="{x:g}" y="{y:g}" width="{u:g}" height="{v:g}" fill="#dce7e9" stroke="#47616a" stroke-width="{font/12:g}"/>'
                   f'<text x="{x+u/2:g}" y="{y+v/2:g}" text-anchor="middle" font-size="{min(font,max(5,min(u,v)/3)):g}" fill="#173b46">{label}</text></g>')
    out.append('</svg>')
    return ''.join(out)


CSS = '''@page{size:A4 portrait;margin:12mm;@bottom-right{font:9px Arial;color:#52636a;content:"Strona " counter(page) " / " counter(pages)}}*{box-sizing:border-box}body{font:11px Arial,sans-serif;color:#182f38;background:#e6ebee;margin:0;line-height:1.35}.toolbar{position:sticky;top:0;background:#173b46;color:white;padding:12px 20px;z-index:2}.toolbar button{padding:8px 14px;margin-right:12px}.book{max-width:210mm;margin:18px auto}.print-page{background:white;padding:12mm;margin:0 0 15px;break-after:page;page-break-after:always}.print-page:last-child{break-after:auto;page-break-after:auto}h1{font-size:22px;margin:0 0 10px}h2{font-size:17px;margin:0 0 7px}h3{font-size:12px;margin:9px 0 4px}p{margin:6px 0}.note{color:#52636a;font-size:10px}.meta{border-bottom:1px solid #bdcbd0;padding-bottom:7px;margin-bottom:9px}table{border-collapse:collapse;width:100%;margin:7px 0}th,td{border:1px solid #b5c2c7;padding:4px;vertical-align:top;text-align:left}th{background:#edf3f4}thead{display:table-header-group}tr{break-inside:avoid;page-break-inside:avoid}small{color:#54666c}.overview-view{margin:4px 0;break-inside:avoid}.overview-view svg{width:100%;height:85mm}.overview-view h3{margin:1px 0}.overview-iso svg{width:100%;height:155mm}.exploded svg{width:100%;height:110mm}.part-drawing svg{width:100%;height:116mm}.sheet-drawing svg{width:100%;height:130mm}.dimensions{font-weight:bold;color:#173b46}.page-footer{font-size:9px;color:#52636a;border-top:1px solid #d4dfe3;padding-top:5px;margin-top:8px}.dim-label{paint-order:stroke;stroke:white;stroke-width:5px;stroke-linejoin:round}a{color:#173b46;overflow-wrap:anywhere}@media print{body{background:white}.toolbar{display:none}.book{margin:0;max-width:none}.print-page{padding:0;margin:0;box-shadow:none}a{text-decoration:none}svg{print-color-adjust:exact;-webkit-print-color-adjust:exact}th{print-color-adjust:exact;-webkit-print-color-adjust:exact}}'''


def export_project_print(project, path, *, group=None, include_cut_plan=True, include_parts=True, explosion=.35):
    """Export printable HTML and return path/counts; never alter source project.

    group is an exact group name or None for the whole set. Display visibility
    and animation are ignored: every manufactured part prints in closed state.
    Each source Part represents one item; numbers follow source part order.
    """
    explosion = float(explosion)
    if not math.isfinite(explosion) or not 0<=explosion<=2:
        raise ValueError('Rozstrzelenie do wydruku: wartość od 0 do 2.')
    snapshot = copy.deepcopy(project)
    numbers = {p.id:i for i,p in enumerate(snapshot.parts,1)}
    parts = [p for p in snapshot.parts if group is None or p.group==group]
    if not parts:
        raise ValueError('Wybrany projekt lub mebel nie zawiera elementów.')
    nominal = _nominal_project(snapshot,parts)
    groups = list(dict.fromkeys(p.group for p in parts))
    out = ['<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">',
           '<title>'+_esc(snapshot.name)+' — komplet do wydruku</title><style>'+CSS+'</style></head><body>',
           '<div class="toolbar"><button type="button" onclick="window.print()">Drukuj / zapisz PDF</button>Komplet A4 · wymiary w mm · druk w skali strony</div><main class="book">']
    footer = '<div class="page-footer">MebelStudio3D 0.12 · wszystkie podane wymiary w mm · rysunki nominalne, bez skali 1:1</div>'
    def section(title, body, attrs=''):
        out.append(f'<section class="print-page" {attrs}><h2>{_esc(title)}</h2>'+body+footer+'</section>')
    views = ''.join(f'<div class="overview-view"><h3>{label}</h3>'+_diagram(snapshot,parts,view=view,height=490)+'</div>' for view,label in (('top','Cały zestaw — z góry'),('front','Cały zestaw — z przodu')))
    section(snapshot.name+' — cały zestaw', '<div class="meta">'+_esc(f'{len(groups)} mebli / grup · {len(parts)} części')+'<p class="dimensions">Gabaryty zamkniętego zestawu: '+_esc(_size_text(_bounds(parts,snapshot)))+'</p></div>'+views,
            'data-kind="overview"')
    section('Cały zestaw — 3D', '<div class="meta dimensions">Gabaryty zamkniętego zestawu: '+_esc(_size_text(_bounds(parts,snapshot)))+'</div><div class="overview-iso">'+_diagram(snapshot,parts,view='iso',height=740)+'</div>', 'data-kind="overview-3d"')
    section('Spis mebli i części', '<p>'+_esc(snapshot.name)+'</p><ul>'+''.join('<li>'+_esc(name)+'</li>' for name in groups)+'</ul>'+_parts_table(snapshot,parts,numbers)+
            '<p class="note">Każdy wiersz oznacza jedną część. Cięcie uwzględnia obrzeża prostokątnych formatek o grubości '+_num(snapshot.edge_t)+' mm. Kontury nieregularne mają prostokątny półfabrykat. Ukryte części modelu również są ujęte.</p>', 'data-kind="parts-list"')
    for name in groups:
        assembly = [copy.deepcopy(p) for p in parts if p.group==name]
        # Furniture reports use the furniture's construction axes, while the
        # overview above retains rigid room/layout transforms through world().
        for p in assembly:
            if hasattr(p,'layout'):
                p.layout = {}
        for offset in range(0,len(assembly),20):
            chunk = assembly[offset:offset+20]
            suffix = f' · części {offset+1}–{offset+len(chunk)}' if len(assembly)>20 else ''
            body = '<div class="meta dimensions">Gabaryty zamkniętego mebla w jego osiach: '+_esc(_size_text(_bounds(assembly,snapshot)))+'</div>'
            body += '<div class="exploded">'+_diagram(snapshot,assembly,exploded=explosion,numbers=numbers,callout_parts=chunk,overall_dimensions=False)+'</div>'
            body += '<p class="note">Rozstrzelenie służy pokazaniu części. Opisy U × V × T są rzeczywistymi wymiarami nominalnymi części; nie obejmują odstępów widoku rozstrzelonego. Numery odpowiadają tabeli.</p>'
            body += _parts_table(snapshot,chunk,numbers)
            body += '<p class="note">Schemat brył konstrukcyjnych: otwory, kieszenie i fazy nie są pokazane jako geometria 3D. Nóżki i profile LED pokazano schematycznie. Ich dane są na rysunkach części i w zestawieniu okuć; dokładne bryły eksportuje STEP.</p>'
            section(name+' — rozstrzelony'+suffix,body,'data-kind="assembly" data-group="'+_esc(name)+'"')
    if include_parts:
        for part in parts:
            body = '<div class="meta">'+_esc(part.group)+' · ID '+_esc(part.id)+'<p class="dimensions">Gotowe: '+_esc(' × '.join(_num(x) for x in (part.u,part.v,part.t)))+' mm · 1 szt.</p></div>'
            body += '<div class="part-drawing">'+part_svg(snapshot,part)+'</div>'+_parts_table(snapshot,[part],numbers)
            body += '<p>Włókna: '+('wzdłuż U' if part.grain else 'obrót w rozkroju dozwolony')+' · Obrzeże: '+_num(part.edge_m())+' m.</p>'+_operations(part)
            body += '<p class="note">Rzut U/V. Wiercenia top/bottom: U,V; na krawędziach U: V,T; na krawędziach V: U,T. Numery otworów odnoszą się do tabeli. Rysunek nie przedstawia profilu faz i zaokrągleń 3D.</p>'
            section(f'#{numbers[part.id]} — {part.name}',body,'data-kind="part" data-part-id="'+_esc(part.id)+'"')
    selected_ids = {p.id for p in parts}
    hardware = [h for h in snapshot.hardware if not h.get('owner_id') or h['owner_id'] in selected_ids]
    if hardware:
        body = '<table><thead><tr><th>Okucie / komponent</th><th>Ilość</th><th>Element</th><th>Producent / profil</th></tr></thead><tbody>'
        by_id = {p.id:p for p in parts}
        for item in hardware:
            owner = by_id.get(item.get('owner_id'))
            metadata = item.get('meta',{})
            source = item.get('source') or metadata.get('source')
            link = '<br><a href="'+_esc(source)+'">Dokumentacja producenta</a>' if isinstance(source,str) and source.startswith(('https://','http://')) else ''
            body += '<tr><td>'+_esc(item['name'])+link+'</td><td>'+_num(item['qty'])+' '+_esc(item.get('unit','szt.'))+'</td><td>'+_esc(owner.group+' / '+owner.name if owner else 'Zestaw')+'</td><td>'+_esc(metadata.get('manufacturer') or metadata.get('profile_id') or item.get('kind',''))+'</td></tr>'
        section('Okucia i wyposażenie',body+'</tbody></table><p class="note">Komponenty kupowane są zestawione oddzielnie od formatek.</p>','data-kind="hardware"')
    if group is None and snapshot.scene.get('objects'):
        body='<table><thead><tr><th>Wyposażenie kupowane</th><th>S × G × H [mm]</th><th>Pozycja X / Y / Z [mm]</th><th>Cena własna [zł]</th></tr></thead><tbody>'
        for item in snapshot.scene['objects']:
            body+='<tr><td>'+_esc(item['name'])+'</td><td>'+_esc(' × '.join(_num(item[k]) for k in ('width','depth','height')))+'</td><td>'+_esc(' / '.join(_num(item[k]) for k in ('x','y','z')))+'</td><td>'+_num(item.get('price',0))+'</td></tr>'
        section('Wyposażenie pomieszczenia',body+'</tbody></table><p class="note">Modele ogólne do wizualizacji. Wymiary i prześwity montażowe wymagają danych konkretnego urządzenia. Te pozycje nie wchodzą do rozkroju płyt.</p>','data-kind="room-objects"')
    sheets,unplaced = cut_plan(nominal) if include_cut_plan else ([],[])
    for i,sheet in enumerate(sheets,1):
        material = nominal.mat(sheet['material'])
        body = '<div class="meta">'+_esc(material.name)+' · grubość '+_num(sheet['thickness'])+' mm · arkusz '+_num(sheet['u'])+' × '+_num(sheet['v'])+' mm</div>'
        body += '<div class="sheet-drawing">'+_sheet_svg(sheet,numbers)+'</div><p class="note">Przybliżony układ gilotynowy. Kontury nieregularne są pakowane jako prostokątne półfabrykaty. Rzaz: '+_num(snapshot.kerf)+' mm; margines: '+_num(snapshot.margin)+' mm.</p>'
        body += '<table><thead><tr><th>Nr / element</th><th>X / Y na arkuszu [mm]</th><th>Wymiary na arkuszu [mm]</th><th>Obrót</th></tr></thead><tbody>'
        by_id = {p.id:p for p in parts}
        for piece in sheet['pieces']:
            p = by_id[piece['id']]
            body += f'<tr><td>#{numbers[p.id]} {_esc(p.name)}</td><td>{_num(piece["x"])} / {_num(piece["y"])}</td><td>{_num(piece["u"])} × {_num(piece["v"])}</td><td>{"90°" if piece["rotated"] else "0°"}</td></tr>'
        section(f'Rozkrój — arkusz {i}',body+'</tbody></table>','data-kind="cut-plan"')
    if unplaced:
        section('Formatki nieumieszczone w rozkroju', '<p>Te elementy nie mieszczą się w wybranym półfabrykacie z uwzględnieniem marginesu i kierunku włókien:</p>'+_parts_table(snapshot,[p for p in parts if p.id in unplaced],numbers),'data-kind="unplaced"')
    out.append('</main></body></html>')
    target = Path(path)
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(''.join(out),encoding='utf-8')
    return dict(path=str(target.resolve()),groups=groups,parts=len(parts),sheets=len(sheets),unplaced=list(unplaced),sections=sum('<section class="print-page"' in block for block in out))

