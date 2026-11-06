"""Whole-furniture arrangement controls and screen-plane dragging."""
import math
from pathlib import Path
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import webbrowser
import copy
from dock_workspace import DockDocument
from form_dialog import FormDocument
from core import inside

SIDES={'Po prawej (+X)':'x+','Po lewej (−X)':'x-',
       'Z tyłu (+Y)':'y+','Z przodu (−Y)':'y-',
       'Nad meblem (+Z)':'z+','Pod meblem (−Z)':'z-'}
ALIGN={'Początek':'start','Środek':'center','Koniec':'end'}

def screen_move(camera, dx, dy, vertical=False):
    """Invert an orthographic screen delta on XY or Z, in world millimetres."""
    origin=camera.project((0,0,0),1,1)
    vectors=[tuple(camera.project(axis,1,1)[i]-origin[i] for i in (0,1))
             for axis in ((1,0,0),(0,1,0),(0,0,1))]
    if vertical:
        vx,vy=vectors[2];length=vx*vx+vy*vy
        if length<1e-10:raise ValueError('Aby przesuwać w pionie, wybierz widok 3D lub Przód.')
        return 0,0,(dx*vx+dy*vy)/length
    (ax,ay),(bx,by)=vectors[:2];det=ax*by-ay*bx
    if abs(det)<1e-8:
        raise ValueError('Aby układać na podłodze, wybierz widok 3D lub Góra.')
    return (dx*by-dy*bx)/det,(ax*dy-ay*dx)/det,0

def selected_group(app):
    p=app.selected_part()
    if p:return p.group
    selection=app.tree.selection()
    if selection:
        name=app.tree.item(selection[0],'text')
        if any(p.group==name for p in app.project.parts):return name
    raise ValueError('Wybierz mebel lub jego element w drzewie projektu.')

def commit(app, result, note='Zmieniono układ kompletu'):
    app.snapshot();app.project=result;app.refresh();app.status.set(note)

def guarded(app, fn):
    def run(*args):
        try:return fn(*args)
        except (ValueError,KeyError,StopIteration,OSError) as exc:
            messagebox.showerror('Układ mebli',str(exc),parent=app.canvas.winfo_toplevel())
    return run

class LayoutDocument(FormDocument):
    def __init__(self, app):
        super().__init__(app,'Układ kompletu · obrót i magnes','940x660');self.minsize(720,510)
        self.groups=ttk.Combobox(self.form,state='readonly',width=55)
        ttk.Label(self.form,text='Wybierz cały mebel',padding=(14,8)).pack(anchor='w')
        self.groups.pack(fill='x',padx=14);self.groups.bind('<<ComboboxSelected>>',guarded(app,lambda e:self.read()))
        self.info=tk.StringVar();ttk.Label(self.form,textvariable=self.info,padding=14,wraplength=880).pack(fill='x')
        self.footer.configure(padding=12)
        ttk.Button(self.footer,text='Wróć do widoku 3D',command=guarded(app,self.show_design)).pack(side='right')
        body=ttk.Frame(self.form,padding=(14,0));body.pack(fill='both',expand=True)
        position=ttk.LabelFrame(body,text='Pozycja mebla w komplecie',padding=12);position.pack(fill='x',pady=6)
        self.vars={}
        for col,(key,label) in enumerate((('x','Minimum X [mm]'),('y','Minimum Y [mm]'),('z','Minimum Z [mm]'),('yaw','Obrót [°]'))):
            ttk.Label(position,text=label).grid(row=0,column=col,sticky='w',padx=5)
            self.vars[key]=tk.StringVar();ttk.Entry(position,textvariable=self.vars[key],width=15).grid(row=1,column=col,sticky='ew',padx=5,pady=5)
            position.columnconfigure(col,weight=1)
        row=ttk.Frame(position);row.grid(row=2,column=0,columnspan=4,sticky='ew')
        ttk.Button(row,text='Zastosuj pozycję i kąt',command=guarded(app,self.apply),style='Accent.TButton').pack(side='left')
        ttk.Button(row,text='↶ −90°',command=guarded(app,lambda:self.turn(-90))).pack(side='left',padx=8)
        ttk.Button(row,text='↷ +90°',command=guarded(app,lambda:self.turn(90))).pack(side='left')
        snap=ttk.LabelFrame(body,text='Połącz bokami / ułóż kolejny poziom',padding=12);snap.pack(fill='x',pady=10)
        self.target=ttk.Combobox(snap,state='readonly');self.target.pack(fill='x')
        row=ttk.Frame(snap);row.pack(fill='x',pady=8)
        self.side=tk.StringVar(value=next(iter(SIDES)));self.align=tk.StringVar(value='Początek');self.gap=tk.StringVar(value='0')
        ttk.Combobox(row,textvariable=self.side,values=list(SIDES),state='readonly',width=26).pack(side='left')
        ttk.Combobox(row,textvariable=self.align,values=list(ALIGN),state='readonly',width=12).pack(side='left',padx=8)
        ttk.Label(row,text='Odstęp [mm]').pack(side='left');ttk.Entry(row,textvariable=self.gap,width=9).pack(side='left',padx=6)
        ttk.Button(snap,text='Dosuń do wybranego mebla',command=guarded(app,self.join),style='Accent.TButton').pack(fill='x')
        self.contact=tk.StringVar();ttk.Label(body,textvariable=self.contact,padding=(0,4),wraplength=880).pack(fill='x')
        ttk.Label(body,text='W 3D wybierz „Układaj meble” i przeciągnij mebel. Shift przesuwa w pionie. Magnes dosuwa bliskie powierzchnie przy obrotach co 90°. Dowolny kąt ustawisz ręcznie. Obrót ±90° i dosuwanie tworzą układy L i U. Każdy mebel można ponownie przesunąć lub obrócić.',wraplength=880,padding=(0,8)).pack(fill='x')
        row=ttk.Frame(body);row.pack(fill='x')
        ttk.Checkbutton(row,text='Magnes w widoku 3D',variable=app.layout_magnet).pack(side='left')
        ttk.Label(row,text='Zasięg [mm]').pack(side='left',padx=8)
        ttk.Spinbox(row,textvariable=app.layout_tolerance,from_=1,to=200,width=8).pack(side='left')
        names=list(dict.fromkeys(p.group for p in app.project.parts));self.groups['values']=names
        try:chosen=selected_group(app)
        except ValueError:chosen=names[0] if names else ''
        self.groups.set(chosen);self.read()

    def current(self):
        self.ensure_current_project(raise_error=True)
        name=self.groups.get()
        if not any(p.group==name for p in self.app.project.parts):raise ValueError('Ten mebel został usunięty. Otwórz układ ponownie.')
        return name

    def read(self):
        from furniture_layout import group_bounds,group_layout,contacts
        names=list(dict.fromkeys(p.group for p in self.app.project.parts));self.groups['values']=names
        name=self.groups.get()
        if name not in names:
            name=names[0] if names else '';self.groups.set(name)
        if not name:self.info.set('Dodaj mebel, aby ustawić komplet.');self.target.set('');self.target['values']=[];return
        lo,hi=group_bounds(self.app.project,name);layout=group_layout(self.app.project,name)
        for axis,value in zip('xyz',lo):self.vars[axis].set(f'{value:.3f}'.rstrip('0').rstrip('.'))
        self.vars['yaw'].set(f'{layout.get("yaw",0):g}')
        self.info.set(f'{name} · gabaryt w komplecie X × Y × Z: '+ ' × '.join(f'{b-a:g}' for a,b in zip(lo,hi))+' mm')
        targets=[g for g in dict.fromkeys(p.group for p in self.app.project.parts) if g!=name]
        self.target['values']=targets
        if self.target.get() not in targets:self.target.set(targets[0] if targets else '')
        links=contacts(self.app.project,name)
        self.contact.set(f'Stykające się powierzchnie: {len(links)}. Dosunięcie ustala pozycję; nie generuje łączników między korpusami.')
        self.mark_clean()

    def apply(self):
        from furniture_layout import group_bounds,group_layout,rotate_group,move_group
        group=self.current();values={k:self.app.number(v) for k,v in self.vars.items()}
        result=rotate_group(self.app.project,group,values['yaw']-group_layout(self.app.project,group).get('yaw',0))
        lo,_=group_bounds(result,group);result=move_group(result,group,**{f'd{a}':values[a]-lo[i] for i,a in enumerate('xyz')})
        commit(self.app,result);self.read()

    def turn(self, angle):
        from furniture_layout import rotate_group
        commit(self.app,rotate_group(self.app.project,self.current(),angle));self.read()

    def join(self):
        from furniture_layout import snap_group
        result=snap_group(self.app.project,self.current(),self.target.get(),SIDES[self.side.get()],ALIGN[self.align.get()],self.app.number(self.gap))
        commit(self.app,result,'Dosunięto mebel · pozycje i wymiary kompletu przeliczone');self.read()

    def show_design(self):
        group=self.current();self.app.selected=next(p.id for p in self.app.project.parts if p.group==group)
        self.app.edit_mode.set('layout');self.app.refresh();self.app.fit()
        page=self.app.design_page
        if self.app.workspace.detached(page):page.lift()
        else:self.app.workspace.select(page)

def begin_drag(app,event):
    if app.explode.get() or any(app.open_amounts.values()):
        app.status.set('Wyłącz rozstrzelenie i zamknij mechanizmy przed układaniem mebli.');return
    from studio_interaction import pick
    selected=pick(app,event.x,event.y)
    if selected is None or not any(p.id==selected for p in app.project.parts):return
    app.selected=selected;p=app.selected_part();app.tree.selection_set(selected);app.load_form()
    app.layout_drag=dict(base=copy.deepcopy(app.project),group=p.group,start=(event.x,event.y),candidate=None,last=0)
    app.draw()

def drag(app,event,force=False):
    data=app.layout_drag
    if not data:return
    dx,dy=event.x-data['start'][0],event.y-data['start'][1]
    if math.hypot(dx,dy)<3 and data['candidate'] is None:return
    if not force and time.monotonic()-data['last']<.05:return
    data['last']=time.monotonic();app.dragged=True
    try:
        from furniture_layout import move_group,nearest_snap
        movement=screen_move(app.camera,dx,dy,bool(event.state&1))
        result=move_group(data['base'],data['group'],*movement,validate=False);info=None
        if app.layout_magnet.get():result,info=nearest_snap(result,data['group'],tolerance=app.layout_tolerance.get(),validate=False)
        data['candidate']=result;app.project=result;app.draw()
        app.status.set('Magnes: dosunięto powierzchnie' if info else 'Przesunięcie X / Y / Z: '+ ' / '.join(f'{v:+.1f}' for v in movement)+' mm · puść, aby zapisać')
    except (ValueError,tk.TclError) as exc:
        data['candidate']=None;app.project=data['base'];app.draw();app.status.set(str(exc))

def end_drag(app,event):
    data=app.layout_drag
    if not data:return False
    drag(app,event,force=True);app.layout_drag=None;app.project=data['base']
    if data['candidate'] is not None:
        try:data['candidate'].validate();commit(app,data['candidate'],'Zapisano układ mebli · Ctrl+Z cofa cały ruch')
        except ValueError as exc:app.draw();app.status.set(str(exc))
    else:app.draw()
    return True

def cancel_drag(app):
    if app.layout_drag:
        app.project=app.layout_drag['base'];app.layout_drag=None;app.refresh();app.status.set('Anulowano przesuwanie mebla')

def print_project(app):
    if not app.flush_parameter_drafts():return False
    from project_print import export_project_print
    if not app.project.parts:raise ValueError('Dodaj meble do projektu przed wydrukiem.')
    path=filedialog.asksaveasfilename(parent=app.canvas.winfo_toplevel(),defaultextension='.html',initialfile=app.project.name+' — dokumentacja.html',filetypes=[('Projekt do druku / PDF','*.html')])
    if not path:return
    export_project_print(app.project,path)
    webbrowser.open(Path(path).resolve().as_uri());app.status.set('Dokumentacja gotowa · w przeglądarce wybierz Drukuj lub Zapisz jako PDF')

def install(app):
    app.layout_drag=None
    app.layout_dialog=guarded(app,lambda:LayoutDocument(app))
    app.print_project=guarded(app,lambda:print_project(app))
    def turn(angle):
        from furniture_layout import rotate_group
        commit(app,rotate_group(app.project,selected_group(app),angle))
    app.rotate_furniture=guarded(app,turn)
    bar=app.nametowidget(app.cget('menu'));menu=tk.Menu(bar,tearoff=False);bar.add_cascade(label='Układ i wydruk',menu=menu)
    for label,fn in [('Ustawianie kompletu — pozycja, obrót, magnes…',app.layout_dialog),('Obróć mebel −90°',lambda:app.rotate_furniture(-90)),('Obróć mebel +90°',lambda:app.rotate_furniture(90)),('Drukuj komplet i szafki z wymiarami…',app.print_project)]:menu.add_command(label=label,command=fn)
    app.canvas.bind('<Escape>',lambda e:cancel_drag(app))
