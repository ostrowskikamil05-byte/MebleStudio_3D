"""Synchronized dimension controls and debounced 3D sketch preview."""
import copy,tkinter as tk
from tkinter import ttk
from core import Camera,render_faces,finite,Part,Project

def install(sketch):
    s=sketch;body=s.canvas.master;s.preview_job=None;s.size_vars={};s.auto_fit=tk.BooleanVar(value=False)
    row=ttk.Frame(s,padding=(8,3));row.pack(fill='x',before=body)
    ttk.Label(row,text='Wymiary szkicu = wymiary 3D [mm]').pack(side='left',padx=5)
    for key,label in [('u','U'),('v','V')]:
        ttk.Label(row,text=label).pack(side='left');var=tk.StringVar(value='0');s.size_vars[key]=var
        entry=ttk.Entry(row,textvariable=var,width=10);entry.pack(side='left',padx=4);entry.bind('<Return>',lambda e:apply_sizes(s))
    ttk.Button(row,text='Zmień wymiary 2D i 3D',command=lambda:apply_sizes(s)).pack(side='left',padx=5)
    ttk.Checkbutton(row,text='Dopasuj po wyborze roli',variable=s.auto_fit).pack(side='left',padx=8)
    panel=ttk.Frame(body,width=255,padding=8);panel.pack(side='left',fill='y',before=s.canvas);panel.pack_propagate(False)
    ttk.Label(panel,text='Podgląd 3D na żywo',style='Title.TLabel').pack(anchor='w')
    s.live_canvas=tk.Canvas(panel,bg='#eef1f5',width=239,height=350,highlightthickness=0);s.live_canvas.pack(fill='both',expand=True,pady=8)
    s.live_note=tk.StringVar();ttk.Label(panel,textvariable=s.live_note,wraplength=225).pack(fill='x',pady=8)
    s.live_canvas.bind('<Configure>',lambda e:schedule(s))
    def preferences_changed():
        from native_viewport import reset_native_failure
        reset_native_failure(s.live_canvas);schedule(s)
    s.viewport_preferences_changed=preferences_changed
    for var in (s.fields['t'],s.level,s.hinge_side,s.automatic,s.target,s.role):var.trace_add('write',lambda *_:schedule(s))
    sync(s);schedule(s)

def sync(s):
    if not hasattr(s,'size_vars'):return
    if s.points:
        u=max(p[0] for p in s.points)-min(p[0] for p in s.points);v=max(p[1] for p in s.points)-min(p[1] for p in s.points)
        s.size_vars['u'].set(f'{u:g}');s.size_vars['v'].set(f'{v:g}')
    else:
        for var in s.size_vars.values():var.set('0')
    schedule(s)

def apply_sizes(s):
    from tkinter import messagebox
    try:
        if not s.closed:raise ValueError('Zamknij kontur przed zmianą jego wymiarów.')
        u,v=[finite(s.number(s.size_vars[k]),.1,10000) for k in ('u','v')]
        min_u,min_v=min(p[0] for p in s.points),min(p[1] for p in s.points)
        old_u=max(p[0] for p in s.points)-min_u;old_v=max(p[1] for p in s.points)-min_v
        if s.part.profile and s.points==list(s.part.points()):
            from shaped_furniture import resize_profile
            part=copy.deepcopy(s.part);resize_profile(part,u,v);part.u=u;part.v=v;part.validate()
            s.checkpoint();s.part=part;s.points=part.points()
        else:
            s.checkpoint();s.points=[(min_u+(a-min_u)*u/old_u,min_v+(b-min_v)*v/old_v) for a,b in s.points]
        s.update_list();s.fit()
    except ValueError as e:messagebox.showerror('Wymiary szkicu',str(e),parent=s)

def schedule(s):
    if not hasattr(s,'live_canvas'):return
    if s.preview_job:
        try:s.after_cancel(s.preview_job)
        except tk.TclError:pass
    s.preview_job=s.after(160,lambda:preview(s))

def preview(s,*,force_cpu=False):
    s.preview_job=None;c=s.live_canvas;c.delete('all');w,h=c.winfo_width(),c.winfo_height()
    if w<10:return
    if not s.closed:
        from native_viewport import close_native
        from gpu_viewport import close_gpu
        close_native(c);close_gpu(c)
        s.live_note.set('Narysuj prostokąt lub zamknij wielokąt. Podgląd przeliczy położenie, wymiary i okucia.');return
    try:
        project,p=s.candidate();camera=Camera();camera.fit(project,w,h)
        from native_viewport import paint_native,close_native,flush_native
        from gpu_viewport import close_gpu
        engine='cpu' if force_cpu else getattr(s.app,'viewport_engine','cpu')
        quality=getattr(s.app,'viewport_quality','balanced');result=None
        if engine in ('auto','d3d11','d3d12','vulkan'):
            result=paint_native(c,project,camera=camera,size=(w,h),selected=p.id,
                selected_ids=(),interactive=False,quality=quality,backend=engine)
            if result.get('ok'):close_gpu(c)
        else:close_native(c)
        if engine in ('auto','gpu') and not (result and result.get('ok')):
            try:
                from gpu_viewport import paint_gpu
                result=paint_gpu(c,project=project,camera=camera,size=(w,h),selected=p.id,
                    selected_ids=(),interactive=False,quality=quality)
            except (ImportError,RuntimeError,OSError,ValueError) as error:result=dict(ok=False,error=str(error))
        if result and result.get('ok'):
            if result.get('engine')=='native':flush_native(c)
            else:c._active_renderer='gpu'
        else:
            close_native(c);close_gpu(c);c._active_renderer='cpu'
            for face in render_faces(project,camera,w,h,p.id):c.create_polygon([value for point in face['points'] for value in point],fill=face['color'],outline=face['color'] if face.get('mesh') else '#ed8a32' if face['selected'] else '#66727f',width=0 if face.get('mesh') else 1)
        hardware=[x for x in project.hardware if x.get('owner_id')==p.id]
        s.live_note.set(f'{p.u:g} × {p.v:g} × {p.t:g} mm\n'+('Okucia: '+', '.join(f'{x["qty"]:g} × {x["name"]}' for x in hardware) if hardware else 'Element bez automatycznych okuć.')+'\nZmiany zapiszesz przyciskiem Dodaj / zapisz.')
    except (RuntimeError,OSError) as e:
        from native_viewport import close_native
        close_native(c)
        if not force_cpu:
            if hasattr(s.app,'log'):s.app.log.warning('Podgląd szkicu C++: %s; podgląd CPU.',e)
            preview(s,force_cpu=True)
        else:s.live_note.set(str(e))
    except (ValueError,KeyError,StopIteration) as e:
        from native_viewport import close_native
        from gpu_viewport import close_gpu
        close_native(c);close_gpu(c)
        s.live_note.set(str(e));c.create_text(w/2,h/2,text='Sprawdź dopasowanie\ni parametry montażu',fill='#a2542c',justify='center')
