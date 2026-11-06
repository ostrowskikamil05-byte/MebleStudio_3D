import html
import math
from pathlib import Path
from features import drill_location

def part_svg(project,part):
    esc=html.escape;margin=max(part.u,part.v)*.12+30;font=max(part.u,part.v)/45+5
    width,height=part.u+margin*2,part.v+margin*2
    def screen(u,v):return u+margin,margin+part.v-v
    items=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:g} {height:g}"><rect width="100%" height="100%" fill="white"/>']
    points=' '.join(f'{x:g},{y:g}' for x,y in (screen(*p) for p in part.points()))
    items.append(f'<polygon points="{points}" fill="#f3eee4" stroke="#333" stroke-width="{font/10:g}"/>')
    for cut in part.machining:
        x,y=screen(cut['u'],cut['v']);depth=part.t if cut['type']=='cutout' else cut['depth']
        if cut['shape']=='circle':items.append(f'<circle cx="{x:g}" cy="{y:g}" r="{cut["width"]/2:g}" fill="none" stroke="#c54b35"/>')
        else:
            length,width_cut=cut['length'],cut['width'];radius=width_cut/2 if cut['shape']=='slot' else 0
            items.append(f'<rect x="{x-length/2:g}" y="{y-width_cut/2:g}" width="{length:g}" height="{width_cut:g}" rx="{radius:g}" fill="none" stroke="#c54b35" transform="rotate({-cut.get("angle",0):g} {x:g} {y:g})"/>')
    drills=[dict(u=u,v=v,diameter=d,depth=h,face='top') for u,v,d,h in part.holes]+part.operations
    for i,op in enumerate(drills,1):
        position,_=drill_location(part,op);x,y=screen(*position[:2]);radius=op['diameter']/2
        if op['face'] in ('top','bottom'):items.append(f'<circle cx="{x:g}" cy="{y:g}" r="{radius:g}" fill="none" stroke="#246698"/>')
        else:items.append(f'<rect x="{x-font/5:g}" y="{y-font/5:g}" width="{font*2/5:g}" height="{font*2/5:g}" fill="#246698"/>')
        if op.get('countersink_diameter') and op['face'] in ('top','bottom'):items.append(f'<circle cx="{x:g}" cy="{y:g}" r="{op["countersink_diameter"]/2:g}" fill="none" stroke="#246698" stroke-dasharray="3 2"/>')
        items.append(f'<text x="{x+radius+font/5:g}" y="{y:g}" font-size="{font*.6:g}" fill="#246698">{i}</text>')
    x0,y0=screen(0,0);x1,y1=screen(part.u,part.v)
    items.append(f'<path d="M{x0:g},{y0+margin*.35:g}H{x1:g} M{x1+margin*.35:g},{y0:g}V{y1:g}" fill="none" stroke="#333"/>')
    items.append(f'<text x="{(x0+x1)/2:g}" y="{y0+margin*.7:g}" text-anchor="middle" font-size="{font:g}">U {part.u:g} mm</text>')
    items.append(f'<text x="{x1+margin*.5:g}" y="{(y0+y1)/2:g}" font-size="{font*.7:g}">V {part.v:g}</text>')
    items.append(f'<text x="{margin:g}" y="{margin*.5:g}" font-size="{font*.7:g}">{esc(part.name)} · T {part.t:g} mm</text></svg>')
    return ''.join(items)

def drawings_export(project,path):
    esc=html.escape;project.validate()
    out=['<!doctype html><html lang="pl"><meta charset="utf-8"><title>Rysunki części</title><style>body{font:14px Segoe UI;margin:20px}svg{width:100%;max-height:650px}table{border-collapse:collapse;width:100%}td,th{border:1px solid #aaa;padding:5px}.part{break-after:page}@media print{button{display:none}}</style><button onclick="print()">Drukuj / PDF</button><h1>'+esc(project.name)+'</h1><p>Rzuty nominalne U/V w mm. Numery odnoszą się do tabeli wierceń. Strony krawędziowe: U=0/max używają V,T; V=0/max używają U,T. Zaokrąglenia i fazy 3D opisuje STEP. Rysunki są dokumentacją roboczą.</p>']
    for part in project.parts:
        out.append('<section class="part"><h2>'+esc(part.group+' / '+part.name)+'</h2><p>ID '+esc(part.id)+' · '+esc(project.mat(part.material).name)+'</p>'+part_svg(project,part))
        out.append('<table><tr><th>Nr</th><th>Strona</th><th>Wsp. 1</th><th>Wsp. 2</th><th>Ø</th><th>Głębokość</th><th>Pogłębienie stożkowe</th></tr>')
        drills=[dict(u=u,v=v,diameter=d,depth=h,face='top') for u,v,d,h in part.holes]+part.operations
        for i,op in enumerate(drills,1):
            countersink=f'Ø{op["countersink_diameter"]:g} × {op["countersink_depth"]:g}' if op.get('countersink_diameter') else '—'
            out.append(f'<tr><td>{i}</td><td>{esc(op["face"])}</td><td>{op["u"]:g}</td><td>{op["v"]:g}</td><td>{op["diameter"]:g}</td><td>{op["depth"]:g}</td><td>{countersink}</td></tr>')
        out.append('</table>')
        if part.machining:
            out.append('<h3>Frezowanie</h3><table><tr><th>Rodzaj / kształt / strona</th><th>Środek U,V</th><th>Długość × szerokość</th><th>Głębokość</th><th>Kąt</th></tr>')
            for cut in part.machining:out.append(f'<tr><td>{esc(cut["type"]+" / "+cut["shape"]+" / "+cut["face"])}</td><td>{cut["u"]:g}, {cut["v"]:g}</td><td>{cut["length"]:g} × {cut["width"]:g}</td><td>{part.t if cut["type"]=="cutout" else cut["depth"]:g}</td><td>{cut.get("angle",0):g}°</td></tr>')
            out.append('</table>')
        out.append('<p>Obróbka krawędzi: '+esc(str(part.fillets+part.chamfers))+'</p></section>')
    out.append('</html>');Path(path).write_text(''.join(out),encoding='utf-8')

def labels_export(project,path):
    esc=html.escape;project.validate()
    out=['<!doctype html><html lang="pl"><meta charset="utf-8"><title>Etykiety części</title><style>@page{size:A4;margin:10mm}body{font:12px Segoe UI}.labels{display:grid;grid-template-columns:repeat(2,90mm);gap:5mm}.label{border:1px dashed #888;box-sizing:border-box;width:90mm;min-height:55mm;padding:4mm;break-inside:avoid}h3{margin:2mm 0}.id{font:bold 15px Consolas}svg{width:28mm;height:20mm;float:right}@media print{button{display:none}}</style><button onclick="print()">Drukuj etykiety</button><div class="labels">']
    for index,part in enumerate(project.parts,1):
        a,b=part.blank(project.edge_t)
        out.append(f'<section class="label">{part_svg(project,part)}<small>{esc(project.name)} · #{index}</small><h3>{esc(part.name)}</h3><div>{esc(part.group)}</div><div>{esc(project.mat(part.material).name)}</div><div>Gotowe: {part.u:g} × {part.v:g} × {part.t:g} mm</div><div>Cięcie: {a:g} × {b:g} mm · włókna: {"U" if part.grain else "dowolnie"}</div><div>Obrzeża U0/U1/V0/V1: {" / ".join("tak" if v else "—" for v in part.edges)}</div><div class="id">{esc(part.id)}</div></section>')
    out.append('</div></html>');Path(path).write_text(''.join(out),encoding='utf-8')
