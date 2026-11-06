"""Menus for manufacturing features and reusable construction tools."""
import tkinter as tk
from tkinter import ttk,filedialog,messagebox,simpledialog
from core import Project
from productivity import part_array,mirror_part,extract_group,insert_template,equal_shelves

def install_menu(app):
    bar=tk.Menu(app);app.configure(menu=bar)
    def guarded(fn):
        def run():
            try:fn()
            except (ValueError,OSError,KeyError,StopIteration) as e:messagebox.showerror('Narzędzia CAD',str(e),parent=app)
        return run
    def selected():
        p=app.selected_part()
        if not p:raise ValueError('Wybierz element w drzewie projektu.')
        return p
    def commit(project):app.snapshot();app.project=project;app.refresh();app.fit()
    def edge():
        from cad_geometry import kernel
        from edge_editor import EdgeEditor
        kernel();EdgeEditor(app,selected(),mode='chamfer')
    def machining():
        from cad_geometry import kernel
        from machining_editor import MachiningEditor
        kernel();MachiningEditor(app,selected())
    def form(title,fields,action):
        d=app.dialog(title,'530x'+str(150+len(fields)*55));variables=[app.field(d,label,value) for label,value in fields]
        def submit():
            try:d.ensure_current_project(raise_error=True);action([v.get() for v in variables]);d.destroy()
            except (ValueError,OSError,KeyError,StopIteration,IndexError) as e:messagebox.showerror(title,str(e) or 'Element został usunięty. Otwórz formularz ponownie.',parent=d)
        ttk.Button(d,text='Zastosuj',command=submit).pack(fill='x',padx=15,pady=15)
    def array():
        p=selected()
        form('Szyk elementów',[('Liczba nowych kopii',3),('Krok X [mm]',0),('Krok Y [mm]',0),('Krok Z [mm]',100)],lambda v:commit(part_array(app.project,p.id,float(v[0]),*[float(x.replace(',','.')) for x in v[1:]])))
    def mirror(axis):
        p=selected();q=mirror_part(p,axis);q.x+=p.u+50
        import copy
        result=copy.deepcopy(app.project);result.parts.append(q);result.validate();commit(result)
    def template(save=False):
        p=selected()
        if save:
            path=filedialog.asksaveasfilename(parent=app,defaultextension='.mebel',initialfile=p.group+'.mebel',filetypes=[('Biblioteka mebli','*.mebel')])
            if path:extract_group(app.project,p.group).save(path)
        else:
            group=extract_group(app.project,p.group)
            form('Kopiuj cały mebel',[('Nazwa',p.group+' — kopia'),('Przesunięcie X [mm]',1000),('Przesunięcie Y [mm]',0),('Przesunięcie Z [mm]',0)],lambda v:commit(insert_template(app.project,group,v[0],tuple(float(x.replace(',','.')) for x in v[1:]))))
    def load():
        path=filedialog.askopenfilename(parent=app,filetypes=[('Biblioteka mebli','*.mebel'),('Projekt','*.json')])
        if not path:return
        group=Project.load(path)
        form('Wstaw z biblioteki',[('Nazwa',group.name),('Przesunięcie X [mm]',1000),('Przesunięcie Y [mm]',0),('Przesunięcie Z [mm]',0)],lambda v:commit(insert_template(app.project,group,v[0],tuple(float(x.replace(',','.')) for x in v[1:]))))
    def shelves():
        p=selected()
        form('Dodaj równo rozmieszczone półki',[('Liczba nowych półek',3),('Grubość [mm]',18)],lambda v:commit(equal_shelves(app.project,p.group,float(v[0]),float(v[1]),p.material)))
    def visibility(isolate):
        p=selected() if isolate else None;app.snapshot()
        for q in app.project.parts:q.visible=q.id==p.id if p else True
        app.refresh();app.fit()
    for label,entries in [('CAD i obróbki',[('Zaokrąglenia kantów…',app.round_edges),('Fazowanie kantów 45°…',edge),('Kieszenie, rowki, wycięcia i wiercenia…',machining),('Szyk elementów…',array),('Odbicie elementu U',lambda:mirror('u')),('Odbicie elementu V',lambda:mirror('v'))]),('Konstrukcja i biblioteka',[('Równy podział na półki…',shelves),('Kopiuj cały mebel…',template),('Zapisz mebel do biblioteki…',lambda:template(True)),('Wstaw mebel z biblioteki…',load)]),('Widoczność',[('Pokaż tylko wybrany element',lambda:visibility(True)),('Pokaż wszystkie elementy',lambda:visibility(False))])]:
        menu=tk.Menu(bar,tearoff=False);bar.add_cascade(label=label,menu=menu)
        for text,fn in entries:menu.add_command(label=text,command=guarded(fn))
