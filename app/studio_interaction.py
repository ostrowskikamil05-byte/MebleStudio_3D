"""Camera gestures, whole-cabinet shortcuts and cursor-centred axis controls."""
import copy
import math
import time
import tkinter as tk
from tkinter import ttk
from core import inside


def pick(app,x,y):
    from depth_raster import pick_visible
    if getattr(app.canvas,'_native_pick',None):return pick_visible(app.canvas,x,y)
    if getattr(app.canvas,'_id_buffer',None) is not None:return pick_visible(app.canvas,x,y)
    return next((face['id'] for face in reversed(getattr(app,'drawn',[])) if inside((x,y),face['points'])),None)


def select(app,identity):
    if hasattr(app,'flush_parameter_drafts') and not app.flush_parameter_drafts():return False
    if identity and identity.startswith('scene:room:'):return
    if identity and identity.startswith('scene:'):
        app.selected=None;app.selected_scene=identity[6:]
        app.inspector_tabs.select(app.room_scene_panel)
        app.room_scene_panel.refresh();app.draw();return
    app.selected_scene=None;app.selected=identity
    if identity and app.tree.exists(identity):app.tree.selection_set(identity);app.tree.see(identity)
    else:app.tree.selection_remove(*app.tree.selection())
    app.load_form();app.draw()


def configure_change(app,**changes):
    from furniture_configurator import read_config,configure
    part=app.selected_part()
    if not part:raise ValueError('Wybierz mebel w widoku 3D.')
    cfg=read_config(app.project,part.group)
    if not cfg:raise ValueError('Otwórz właściwości elementu lub edytor narożnika dla tego mebla.')
    cfg.update(changes);result,group=configure(app.project,cfg,part.group)
    app.snapshot();app.project=result;app.refresh()


def context_menu(app,event,*,show=True):
    identity=pick(app,event.x,event.y)
    if identity:select(app,identity)
    menu=tk.Menu(app.canvas,tearoff=False);app.context_menu=menu
    def action(label,fn,parent=menu):
        def run():
            menu.unpost()
            try:fn()
            except (ValueError,OSError) as error:
                app.log.warning('%s: %s',label,error);app.status.set(str(error))
        parent.add_command(label=label,command=run)
    if app.selected_scene:
        action('Ustawienia wyposażenia',lambda:app.inspector_tabs.select(app.room_scene_panel))
        action('Obróć wyposażenie +90°',lambda:app.room_scene_panel.rotate_selected(90))
        action('Usuń wyposażenie',app.room_scene_panel.delete_selected)
    else:
        from quick_studio_ui import focus_section
        from corner_furniture import read_corner_config
        part=app.selected_part();corner=read_corner_config(app.project,part.group) if part else None
        from workspace_worktops import read_shared_worktop,fit_shared_worktop
        shared=read_shared_worktop(app.project,part.group) if part else None
        if shared:
            def fit_top():
                result=fit_shared_worktop(app.project,part.group);app.snapshot();app.project=result;app.refresh()
            action('Dopasuj wspólny blat do szafek',fit_top)
            action('Wymiary i kontur blatu',lambda:app.inspector_tabs.select(1))
        action('Wymiary i typ mebla',lambda:app.inspector_tabs.select(app.corner_panel) if corner else focus_section(app,'type'))
        styles=tk.Menu(menu,tearoff=False);menu.add_cascade(label='Styl frontów',menu=styles)
        for label,style in [('Gładki','smooth'),('Ramka frezowana','frame'),('Ryflowany','fluted'),('Chwyt frezowany','finger')]:action(label,lambda s=style:configure_change(app,front_style=s),styles)
        fronts=tk.Menu(menu,tearoff=False);menu.add_cascade(label='Układ frontów',menu=fronts)
        for label,mode,count in [('Otwarty','open',0),('Drzwi','doors',1),('2 szuflady','drawers',2),('3 szuflady','drawers',3),('4 szuflady','drawers',4)]:action(label,lambda m=mode,n=count:app.quick_inspector.quick_front(m,n),fronts)
        action('Zawiasy / prowadnice',lambda:focus_section(app,'fittings'))
        action('Nóżki / blat / wzmocnienia',lambda:focus_section(app,'legs'))
        menu.add_separator()
        action('Rozciągaj cały mebel · osie przy kursorze',lambda:set_mode(app,'furniture',(event.x,event.y)))
        action('Przesuwaj mebel · magnes',lambda:set_mode(app,'layout'))
        action('Obróć mebel −90°',lambda:app.rotate_furniture(-90))
        action('Obróć mebel +90°',lambda:app.rotate_furniture(90))
        action('Połącz / ustaw układ L lub U…',app.layout_dialog)
        action('Narożnik L / ścięty / zaokrąglony…',app.corner_dialog)
        if part and not corner:
            def convert(shape,side):
                from corner_furniture import convert_to_corner
                result,group=convert_to_corner(app.project,part.group,shape=shape,side=side)
                app.snapshot();app.project=result;app.selected=next(p.id for p in result.parts if p.group==group);app.refresh();app.inspector_tabs.select(app.corner_panel);app.fit()
            corners=tk.Menu(menu,tearoff=False);menu.add_cascade(label='Zmień ten mebel na narożny',menu=corners)
            for shape,label in [('L','L'),('chamfer','Ścięty'),('rounded','Zaokrąglony')]:
                for side,sl in [('left','lewy'),('right','prawy')]:action(label+' · '+sl,lambda sh=shape,si=side:convert(sh,si),corners)
        action('Wspólny blat dla szafek…',app.shared_worktop_dialog)
        action('Kopia całego mebla',app.quick_inspector.duplicate_group)
    menu.add_separator();action('Dopasuj widok',app.fit);action('Logi developerskie',app.open_developer_logs)
    if show:
        try:menu.tk_popup(event.x_root,event.y_root)
        finally:menu.grab_release()
    return menu


def set_mode(app,mode,origin=None):
    app.edit_mode.set(mode)
    if origin:app.gizmo_origin=origin
    app.draw()


def paint_gizmo(app):
    if app.edit_mode.get()!='furniture' or not app.handles:return
    c=app.canvas;w,h=c.winfo_width(),c.winfo_height()
    x,y=getattr(app,'gizmo_origin',(w*.56,h*.53));x=max(86,min(w-86,x));y=max(86,min(h-86,y))
    c.create_oval(x-64,y-64,x+64,y+64,fill='#e7edf2',outline='#889baa',width=2,stipple='gray50',tags='axis_gizmo')
    # Move the actual draggable controls to the wheel; their world-to-screen
    # derivative still determines the correct dimensional change in millimetres.
    for handle in app.handles:
        axis=handle['face'][0];end=handle['face'][1];vx,vy=handle['vector'];length=math.hypot(vx,vy)
        sign=-1 if end=='0' else 1;hx=x+vx/length*sign*51;hy=y+vy/length*sign*51
        color={'x':'#c95048','y':'#288563','z':'#3e77bd'}[axis]
        handle['x']=hx;handle['y']=hy
        c.create_line(x,y,hx,hy,fill=color,width=3,tags='axis_gizmo')
        c.create_oval(hx-8,hy-8,hx+8,hy+8,fill=color,outline='white',width=2,tags='axis_gizmo')
        c.create_text(hx,hy-15,text=axis.upper()+('−' if end=='0' else '+'),fill=color,font=('Segoe UI',9,'bold'),tags='axis_gizmo')
    c.create_text(x,y+80,text='Przeciągnij oś · PPM: opcje',fill='#243144',font=('Segoe UI',9),tags='axis_gizmo')


def install(app):
    app.selected_scene=None;app._view_interactive=False;app._view_last=0;app._view_finish_job=None
    from attachment_picker import pick as pick_attachment
    app.pick_attachment_target=lambda x,y:pick_attachment(app,x,y)
    original_draw=app.draw;app.viewport_stats={'last_ms':0,'frames':0};last_warning=[0]
    def draw():
        start=time.perf_counter();original_draw();paint_gizmo(app)
        if getattr(app,'hardware_drag',None):app.hardware_drag.draw_ghost()
        duration=(time.perf_counter()-start)*1000;app.viewport_stats.update(last_ms=duration,frames=app.viewport_stats['frames']+1)
        if duration>700 and time.monotonic()-last_warning[0]>15:
            last_warning[0]=time.monotonic();app.log.warning('Wolna klatka 3D: %.0f ms, %s części, %s ścian, tryb ruchu %s',duration,len(app.project.parts),len(getattr(app,'drawn',[])),app._view_interactive)
    app.draw=draw
    def finish(event=None):
        if app._view_finish_job:
            app.after_cancel(app._view_finish_job);app._view_finish_job=None
        app._view_interactive=False;app.draw();return 'break'
    def request_finish():
        if app._view_finish_job:app.after_cancel(app._view_finish_job)
        app._view_finish_job=app.after(160,finish)
    def begin(event):
        app.canvas.focus_set();app._camera_gesture=(event.x,event.y,bool(event.state&1));return 'break'
    def move(event):
        if not getattr(app,'_camera_gesture',None):return 'break'
        x,y,pan=app._camera_gesture;dx,dy=event.x-x,event.y-y;app._camera_gesture=(event.x,event.y,pan)
        if pan:app.camera.pan=(app.camera.pan[0]+dx,app.camera.pan[1]+dy)
        else:app.camera.yaw+=dx*.45;app.camera.pitch=max(-89,min(89,app.camera.pitch+dy*.4))
        app._view_interactive=True
        if time.monotonic()-app._view_last>.025:app._view_last=time.monotonic();app.draw()
        return 'break'
    def release(event):app._camera_gesture=None;return finish()
    for event,fn in [('<ButtonPress-2>',begin),('<B2-Motion>',move),('<ButtonRelease-2>',release),('<Alt-ButtonPress-1>',begin),('<Alt-B1-Motion>',move),('<Alt-ButtonRelease-1>',release)]:app.canvas.bind(event,fn)
    original_orbit=app.orbit
    def orbit(event):
        if getattr(app,'hardware_drag',None) and app.hardware_drag.active:return app.hardware_drag.move(event)
        if getattr(app,'scene_drag',None):
            from room_scene_drag import move
            move(app,event);return 'break'
        app._view_interactive=True;original_orbit(event);request_finish()
    app.canvas.bind('<B1-Motion>',orbit)
    original_down=app.mouse_down
    def down(event):
        if not app.flush_parameter_drafts():return 'break'
        if getattr(app,'hardware_drag',None) and app.hardware_drag.active:
            app._hardware_click=True;return app.hardware_drag.target_click(event)
        identity=pick(app,event.x,event.y)
        if identity and identity.startswith('scene:'):
            app.start=app.last=(event.x,event.y);app.dragged=False;select(app,identity)
            if app.edit_mode.get()=='layout':
                from room_scene_drag import begin
                begin(app,event,identity)
            return 'break'
        return original_down(event)
    app.canvas.bind('<ButtonPress-1>',down)
    original_up=app.mouse_up
    def up(event):
        if getattr(app,'_hardware_click',False):app._hardware_click=False;return 'break'
        if getattr(app,'scene_drag',None):
            from room_scene_drag import end
            end(app,event);return finish()
        identity=pick(app,event.x,event.y)
        if not getattr(app,'dragged',False) and not app.layout_drag and not app.drag_edit:
            app.gizmo_origin=(event.x,event.y)
            if identity and identity.startswith('scene:'):select(app,identity);finish();return 'break'
            app.selected_scene=None
        original_up(event);finish()
    app.canvas.bind('<ButtonRelease-1>',up)
    def pointer(event):
        if getattr(app,'hardware_drag',None) and app.hardware_drag.active:return app.hardware_drag.move(event)
        hit=next((d for d in getattr(app,'visible_drills',[]) if math.hypot(event.x-d['x'],event.y-d['y'])<8),None)
        if hit:
            op=hit['operation'];app.status.set(f'{op.get("purpose","Wiercenie")} · Ø {op["diameter"]:g} · głębokość {op["depth"]:g} mm · {op["face"]}')
    app.canvas.bind('<Motion>',pointer)
    def right_down(event):app._right_start=(event.x,event.y);app.pan_start(event);app._right_dragged=False
    def right_move(event):
        if math.hypot(event.x-app._right_start[0],event.y-app._right_start[1])>4:app._right_dragged=True
        if app._right_dragged:app._view_interactive=True;app.pan(event)
    def right_up(event):
        finish()
        if not getattr(app,'_right_dragged',False):context_menu(app,event)
    app.canvas.bind('<ButtonPress-3>',right_down);app.canvas.bind('<B3-Motion>',right_move);app.canvas.bind('<ButtonRelease-3>',right_up)
    def wheel(event):app._view_interactive=True;app.wheel(event);request_finish();return 'break'
    app.canvas.bind('<MouseWheel>',wheel)
    app.canvas.bind('<Escape>',lambda e:(app.cancel_active_edits(),finish(),'break')[-1])
    app.finish_view_interaction=finish
