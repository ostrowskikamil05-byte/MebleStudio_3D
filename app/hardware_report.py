"""Human-readable purchase list and installation planning dimensions."""
import html
from pathlib import Path

def hardware_report(project,path):
    esc=lambda x:html.escape(str(x))
    owners={p.id:p for p in project.parts}
    out=['<!doctype html><html lang="pl"><meta charset="utf-8"><title>Okucia — plan montażu</title><style>body{font:15px Segoe UI,sans-serif;margin:32px;color:#243144}table{border-collapse:collapse;width:100%}td,th{border:1px solid #bbc;padding:8px;text-align:left}h2{margin-top:30px}@media print{a{color:black}}</style><h1>Okucia i wymiary montażowe</h1>',
         '<p>Model produkcyjny w pozycji zamkniętej. Profil prowadnic zawiera wymiary planowania. Dobór zaczepów, rowków, mocowań i nośności jest opisany przy danej pozycji.</p><table><tr><th>Mebel / element</th><th>Okucie</th><th>Ilość</th><th>Informacja</th></tr>']
    for h in project.hardware:
        p=owners.get(h.get('owner_id'));owner=p.group+' / '+p.name if p else 'Projekt'
        info=h.get('note',h.get('installation_status','Wzorzec edytowalny; sprawdź wariant montażu.'))
        if h.get('compatibility_status'):info+=' '+h['compatibility_status']
        out.append(f'<tr><td>{esc(owner)}</td><td>{esc(h["name"])}</td><td>{esc(h.get("qty",1))}</td><td>{esc(info)}</td></tr>')
    out.append('</table>')
    for h in project.hardware:
        plan=h.get('dimensions')
        if not plan:continue
        p=owners.get(h.get('owner_id'));out.append('<h2>'+esc((p.name+' — ' if p else '')+h['name'])+'</h2><table>')
        for key,label in [('drawer_inner_width','Szerokość wewnętrzna skrzynki'),('drawer_outer_width','Szerokość zewnętrzna skrzynki'),('external_clearance_per_side','Luz zewnętrzny na stronę'),('drawer_length','Długość skrzynki'),('min_cabinet_depth','Minimalna głębokość korpusu'),('bottom_recess_min','Minimalne cofnięcie dna')]:
            val=plan.get(key);value=f'{val:g} mm' if isinstance(val,(int,float)) else 'Nieustalone dla wariantu';out.append(f'<tr><th>{label}</th><td>{esc(value)}</td></tr>')
        if plan.get('article'):out.append('<tr><th>Artykuł</th><td>'+esc(plan['article'])+'</td></tr>')
        low,high=plan.get('drawer_inner_width_range',[None,None])
        if low is not None:out.append(f'<tr><th>Dopuszczalne wnętrze skrzynki</th><td>{low:g}–{high:g} mm</td></tr>')
        out.append('</table>')
        if plan.get('length_status')=='workshop':out.append('<p>Długość skrzynki = długość nominalna prowadnicy jest ustawieniem warsztatowym. Dokumentacja artykułu nie definiuje tu długości drewnianej skrzynki.</p>')
        if plan.get('depth_status')=='user_confirmed':out.append('<p>Minimalną głębokość montażową podał i potwierdził użytkownik; dołączona karta nie określa tego wymiaru.</p>')
        out.append('<p>'+esc(' · '.join(plan.get('warnings',[])))+'</p>')
        for src in dict.fromkeys([plan.get('variant_source')]+plan.get('sources',[])):
            if src:out.append(f'<p><a href="{esc(src)}">Dokumentacja wariantu / producenta</a></p>')
    out.append('</html>');Path(path).write_text(''.join(out),encoding='utf-8')
