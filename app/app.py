"""Offline desktop application. Python 3.10+, standard library only."""
import copy
import json
import math
from pathlib import Path
import sys
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, colorchooser
import webbrowser
import time
from sketch import Sketch
from assembly import delete_with_connections, rebuild_dependents
from direct_edit import resize_part, resize_furniture
from core import (Project,Part,Material,Camera,add_template,render_faces,world,
                  csv_export,dxf_export,report_export,svg_view,cut_plan,uid,inside,manufacturing_export,explosion_shift)

ROOT=Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Mebel Studio 3D · projektowanie i produkcja')
        self.geometry(f'{min(1440,self.winfo_screenwidth()-40)}x{min(900,self.winfo_screenheight()-100)}+10+10');self.minsize(1180,600)
        self.configure(bg='#eff2f6')
        self._native_document=None;self._native_pending=False
        self.project_generation=0
        self.project=Project();
        from palette import install_palette
        install_palette(self.project)
        self.open_amounts={};self.animation_job=None;self.motion_value=tk.DoubleVar(value=0);self.motion_all=tk.BooleanVar(value=False);self.resize_step=tk.BooleanVar(value=False)
        self.path=None;self.dirty=False
        self.selected=None;self.undo_stack=[];self.redo_stack=[];self.loading=False
        self.xray=tk.BooleanVar(value=False)
        self.xray_scope=tk.StringVar(value='Cały zestaw');self.drill_mode=tk.StringVar(value='Także zasłonięte')
        self.viewport_engine='cpu';self.viewport_quality='balanced'
        self.camera=Camera();self.explode=tk.BooleanVar(value=False);self.dimensions=tk.BooleanVar(value=True)
        self.dimension_mode=tk.StringVar(value='Wybrany mebel');self.show_drills=tk.BooleanVar(value=False);self.scene_drag=None
        self.edit_mode=tk.StringVar(value='camera');self.handles=[];self.edge_handles=[];self.drag_edit=None
        self.layout_magnet=tk.BooleanVar(value=True);self.layout_tolerance=tk.DoubleVar(value=80);self.layout_drag=None
        self.status=tk.StringVar(value='Gotowy · wszystkie wymiary w milimetrach')
        style=ttk.Style(self);style.theme_use('clam')
        style.configure('.',font=('Segoe UI',10),background='#f6f8fa',foreground='#243144')
        style.configure('TButton',padding=(10,7),background='#e6ebf1')
        style.map('TButton',background=[('active','#d7e0eb')])
        style.configure('Accent.TButton',background='#176a78',foreground='white')
        style.configure('TNotebook.Tab',padding=(16,9),font=('Segoe UI',10,'bold'))
        style.configure('Treeview',rowheight=29,background='white',fieldbackground='white')
        style.configure('Treeview.Heading',font=('Segoe UI',10,'bold'),padding=7)
        style.configure('Title.TLabel',font=('Segoe UI',13,'bold'))
        self.ui()
        from developer_logs import install as install_logs
        from workspace_controls import install as install_workspace_controls
        from studio_interaction import install as install_interaction
        install_logs(self);install_workspace_controls(self);install_interaction(self)
        from view_modes_ui import install as install_view_modes
        install_view_modes(self)
        from visual_catalogue import install as install_catalogue
        install_catalogue(self)
        from responsive_ui import install as install_responsive
        install_responsive(self)
        from native_viewport import install as install_native_viewport
        install_native_viewport(self)
        from furniture_configurator import default_config,configure
        self.project,group=configure(self.project,default_config(0,self.project))
        from native_document import create_if_available,NativeHistoryView
        self._native_document=create_if_available(self.project)
        if self._native_document is not None:
            self.undo_stack=NativeHistoryView(self._native_document,'undo')
            self.redo_stack=NativeHistoryView(self._native_document,'redo')
        self.selected=next(p.id for p in self.project.parts if p.group==group)
        self.refresh();self.after(100,self.fit)
        self.protocol('WM_DELETE_WINDOW',self.close)
        for key,fn in [('<Control-s>',self.save),('<Control-o>',self.open),('<Control-z>',self.undo),('<Control-y>',self.redo),('<Control-n>',self.new)]:
            self.workspace.bind_app_shortcut(key,fn)
        self.canvas.bind('<KeyPress-f>',lambda e:(self.fit(),'break')[1])
        self.canvas.bind('<KeyPress-F>',lambda e:(self.fit(),'break')[1])
        # File association arguments are projects; unittest/build arguments
        # must never create a delayed missing-file modal dialog.
        if len(sys.argv)>1 and not sys.argv[1].startswith('--') and Path(sys.argv[1]).suffix.lower()=='.mebel':
            self.after(120,lambda path=sys.argv[1]:self.load_path(path))

    def button(self,parent,text,command,accent=False):
        b=ttk.Button(parent,text=text,command=command,style='Accent.TButton' if accent else 'TButton');b.pack(side='left',padx=3,pady=3);return b

    def ui(self):
        from quick_studio_ui import build
        build(self)

    def selected_part(self):return next((p for p in self.project.parts if p.id==self.selected),None)

    def apply_viewport_preferences(self,prefs):
        self.viewport_engine=prefs.get('engine','auto')
        self.viewport_quality=prefs.get('quality','balanced')
        if hasattr(self,'canvas'):
            from native_viewport import reset_native_failure
            reset_native_failure(self.canvas)
        if hasattr(self,'canvas'):self.draw()

    def require_part(self,part_id):
        part=next((p for p in self.project.parts if p.id==part_id),None)
        if part is None:raise ValueError('Element został usunięty. Otwórz formularz ponownie dla istniejącego elementu.')
        return part

    def edit_sketch(self):
        part=self.selected_part()
        if part and part.assembly.get('generated_by'):
            part=next((p for p in self.project.parts if p.id==part.assembly['generated_by']),part)
        if part:Sketch(self,part)
        else:messagebox.showinfo('Szkic','Wybierz element w drzewie projektu.',parent=self)

    def snapshot(self):
        if self._native_document is not None:
            self._sync_native_document()
            self._native_pending=True;self.changed();return
        self.undo_stack.append(copy.deepcopy(self.project.data()));self.undo_stack=self.undo_stack[-60:]
        self.redo_stack.clear();self.changed()

    def _native_dirty_state(self):
        document=self._native_document
        if document is None:return
        self.dirty=document.dirty
        state=(self.project.name,self.dirty)
        if state!=getattr(self,'_native_title_state',None):
            self._native_title_state=state
            self.title(f'Mebel Studio 3D · {self.project.name}'+(' *' if self.dirty else ''))

    def _sync_native_document(self):
        document=self._native_document
        if document is None:return
        if any(getattr(self,key,None) for key in ('drag_edit','layout_drag','scene_drag')):return
        try:
            if not document.matches(self.project):
                from workspace_worktops import refresh_shared_worktops
                candidate=refresh_shared_worktops(self.project,validate=False)
                document.commit(candidate,record_history=self._native_pending)
                self.project=candidate
            self._native_pending=False
            self._native_dirty_state()
        except Exception:
            self.project=document.project();self._native_pending=False
            self._native_dirty_state()
            raise

    def commit_project(self,project,*,fit=False):
        """Publish one validated edit; native history owns the complete result."""
        if self._native_document is None:
            project.validate();self.snapshot();self.project=project
        else:
            self._sync_native_document()
            from workspace_worktops import refresh_shared_worktops
            candidate=refresh_shared_worktops(project,validate=False)
            self._native_document.commit(candidate,record_history=True)
            self.project=candidate;self._native_pending=False;self.changed()
        self.refresh()
        if fit:self.fit()
        return self.project

    def changed(self):
        self.viewport_revision=getattr(self,'viewport_revision',0)+1
        self.dirty=True;self.title(f'Mebel Studio 3D · {self.project.name} *')

    def refresh(self):
        from workspace_worktops import refresh_shared_worktops
        self.project=refresh_shared_worktops(self.project,validate=False)
        self._sync_native_document()
        stale=tuple((p.id,p.assembly['shared_worktop'].get('reason','')) for p in self.project.parts if p.assembly.get('shared_worktop',{}).get('status')=='needs_fit')
        if stale and stale!=getattr(self,'_reported_stale_tops',None) and hasattr(self,'log'):
            for identity,reason in stale:self.log.warning('Wspólny blat %s wymaga dopasowania: %s',identity,reason)
        self._reported_stale_tops=stale
        ids={p.id for p in self.project.parts};self.open_amounts={key:value for key,value in self.open_amounts.items() if key in ids}
        self.loading=True
        self.tree.delete(*self.tree.get_children());groups={}
        for p in self.project.parts:
            if p.group not in groups:
                gid='group_'+str(len(groups));groups[p.group]=gid;self.tree.insert('','end',iid=gid,text=p.group,open=True)
            self.tree.insert(groups[p.group],'end',iid=p.id,text=('◉ ' if p.visible else '○ ')+p.name)
        self.matcombo['values']=[m.name for m in self.project.materials]
        if self.selected and self.tree.exists(self.selected):self.tree.selection_set(self.selected)
        else:self.selected=None
        self.loading=False;self.load_form();self.refresh_production();self.draw()
        if hasattr(self,'room_scene_panel'):self.room_scene_panel.refresh()
        if hasattr(self,'corner_panel'):self.corner_panel.refresh()

    def load_form(self):
        p=self.selected_part()
        if p and hasattr(self,'selected_scene'):self.selected_scene=None
        for key,var in self.vars.items():var.set(getattr(p,key) if p else '')
        self.matvar.set(self.project.mat(p.material).name if p else '')
        self.grain.set(p.grain if p else False);self.visible.set(p.visible if p else True)
        for i,v in enumerate(self.edgevars):v.set(p.edges[i] if p else False)
        if hasattr(self,'quick_inspector') and not self.drag_edit:self.quick_inspector.refresh()
        if hasattr(self,'corner_panel') and not self.drag_edit:self.corner_panel.refresh()

    def flush_parameter_drafts(self):
        for name in ('quick_inspector','corner_panel'):
            panel=getattr(self,name,None)
            if panel and hasattr(panel,'flush_draft') and not panel.flush_draft():return False
        return True

    def tree_select(self,event=None):
        if self.loading:return
        if not self.flush_parameter_drafts():return
        sel=self.tree.selection();self.selected=sel[0] if sel and any(p.id==sel[0] for p in self.project.parts) else None
        if sel and self.selected is None:
            group=self.tree.item(sel[0],'text');part=next((p for p in self.project.parts if p.group==group),None)
            if part:self.selected=part.id
        self.motion_value.set(self.open_amounts.get(self.selected,0)*100)
        self.load_form();self.draw()

    def display_project(self):
        if not any(self.open_amounts.values()):return self.project
        from mechanisms import posed_project
        key=(id(self.project),getattr(self,'viewport_revision',0),tuple(sorted(self.open_amounts.items())))
        if getattr(self,'_posed_cache_key',None)!=key:
            self._posed_cache=posed_project(self.project,self.open_amounts);self._posed_cache_key=key
        return self._posed_cache

    def open_motion(self,value=None):
        from mechanisms import effective_motion
        amount=self.motion_value.get()/100
        targets=[p for p in self.project.parts if effective_motion(p)['mode']!='none'] if self.motion_all.get() else [self.selected_part()] if self.selected_part() else []
        selected=self.selected_part()
        if self.motion_all.get() and selected:targets=[p for p in targets if p.group==selected.group]
        for p in targets:self.open_amounts[p.id]=amount
        self.draw()
        if targets:self.status.set(f'Otwarcie {amount*100:.0f}% · podgląd ruchu; wymiary produkcyjne w pozycji zamkniętej')

    def close_motion(self):
        if self.animation_job:self.after_cancel(self.animation_job);self.animation_job=None
        self.open_amounts={};self.motion_value.set(0);self.draw()

    def animate_motion(self):
        if self.animation_job:self.after_cancel(self.animation_job);self.animation_job=None;return
        target=0 if self.motion_value.get()>50 else 100
        def tick():
            current=self.motion_value.get();value=current+5 if target>current else current-5
            self.motion_value.set(max(0,min(100,value)));self.open_motion()
            if abs(self.motion_value.get()-target)>1:self.animation_job=self.after(45,tick)
            else:self.animation_job=None
        tick()

    def apply(self):
        p=self.selected_part()
        if not p:return
        try:
            if p.assembly:
                from dimension_model import resize_dimensions
                if p.assembly.get('generated_by'):raise ValueError('Edytuj front szuflady zamiast części skrzynki.')
                if any(float(self.vars[k].get().replace(',','.'))!=getattr(p,k) for k in ('x','y','z','rx','ry','rz')) or self.vars['plane'].get()!=p.plane:raise ValueError('Położenie elementu powiązanego zmień przez Edytuj szkic i parametry montażu.')
                temp=resize_dimensions(self.project,p.id,*[float(self.vars[k].get().replace(',','.')) for k in ('u','v','t')])
                q=next(q for q in temp.parts if q.id==p.id);q.name=self.vars['name'].get();q.material=temp.materials[self.matcombo.current()].id;q.visible=self.visible.get();q.grain=self.grain.get();q.edges=[v.get() for v in self.edgevars];temp.validate()
                self.snapshot();self.project=temp;self.refresh();return
            candidate=copy.deepcopy(p)
            for key,var in self.vars.items():setattr(candidate,key,var.get() if key in ('name','group','plane') else float(var.get().replace(',','.')))
            if candidate.group!=p.group:
                peer=next((q for q in self.project.parts if q.group==candidate.group),None)
                candidate.layout=copy.deepcopy(peer.layout) if peer else copy.deepcopy(p.layout)
            candidate.material=self.project.materials[self.matcombo.current()].id
            candidate.edges=[v.get() for v in self.edgevars];candidate.grain=self.grain.get();candidate.visible=self.visible.get()
            if candidate.contour and (candidate.u!=p.u or candidate.v!=p.v):
                candidate.contour=[(x*candidate.u/p.u,y*candidate.v/p.v) for x,y in p.contour]
            candidate.operations=[op for op in candidate.operations if op.get('recipe_id')!='handle']
            candidate.validate()
            if min(candidate.blank(self.project.edge_t))<=0:raise ValueError('Wymiar do cięcia musi być dodatni.')
            temp=copy.deepcopy(self.project);temp.parts[self.project.parts.index(p)]=candidate
            from fittings import refresh_handles
            refresh_handles(temp,p.id)
            from opening_options import refresh_openings
            refresh_openings(temp,p.id)
            temp=rebuild_dependents(temp,p.id)
            self.snapshot();self.project=temp;self.refresh();self.status.set('Zmieniono element · model i formatki przeliczone')
        except (ValueError,IndexError) as e:messagebox.showerror('Sprawdź dane',str(e),parent=self)

    def refresh_production(self):
        table=self.tables['parts'];table.delete(*table.get_children())
        for p in self.project.parts:
            a,b=p.blank(self.project.edge_t)
            table.insert('','end',values=[p.group,p.name,self.project.mat(p.material).name,f'{p.u:g}',f'{p.v:g}',f'{p.t:g}',f'{a:g}',f'{b:g}',f'{p.edge_m():.2f}','Tak' if p.contour else ''])
        c=self.project.costs();table=self.tables['costs'];table.delete(*table.get_children())
        for label,key in [('Materiał · powierzchnia netto','panels'),('Obrzeża','edges'),('Akcesoria','hardware'),('Robocizna','labor'),('Koszt bazowy','base'),(f'Cena z narzutem {self.project.markup:g}%','total')]:table.insert('','end',values=[label,f'{c[key]:.2f}'])
        self.sheets,bad=cut_plan(self.project)
        stock=sum(self.project.mat(s['material']).cost(Part(u=s['u'],v=s['v'],t=s['thickness'])) for s in self.sheets)
        self.costnote.configure(text=f'Pełne płyty według rozkroju: {stock:.2f} zł · ceny własne, bez podatku i transportu; materiał netto nie zawiera odpadów.')
        used=sum(p.blank(self.project.edge_t)[0]*p.blank(self.project.edge_t)[1] for p in self.project.parts if p.id not in bad)
        total=sum(s['u']*s['v'] for s in self.sheets)
        self.cutlabel.configure(text=f'Płyty: {len(self.sheets)} · wykorzystanie półfabrykatów: {(used/total*100 if total else 0):.1f}% · nie mieszczą się: {len(bad)} · rzaz {self.project.kerf:g} mm / margines {self.project.margin:g} mm')
        vals=[f'{i+1}. {self.project.mat(s["material"]).name} · {s["thickness"]:g} mm · {s["u"]:g} × {s["v"]:g}' for i,s in enumerate(self.sheets)]
        self.sheetcombo['values']=vals
        if vals:self.sheetcombo.current(0)
        else:self.sheetvar.set('Brak płyt')
        self.draw_cut()

    def draw_cut(self):
        c=self.cutcanvas;c.delete('all');index=self.sheetcombo.current()
        if not hasattr(self,'sheets') or not 0<=index<len(self.sheets):return
        s=self.sheets[index];w=max(c.winfo_width(),200);h=max(c.winfo_height(),200)
        scale=min((w-50)/s['u'],(h-50)/s['v']);ox=(w-s['u']*scale)/2;oy=(h-s['v']*scale)/2
        c.create_rectangle(ox,oy,ox+s['u']*scale,oy+s['v']*scale,fill='#eef1f5',outline='#536277')
        for n,r in enumerate(s['pieces']):
            p=next(p for p in self.project.parts if p.id==r['id']);x,y=ox+r['x']*scale,oy+r['y']*scale
            c.create_rectangle(x,y,x+r['u']*scale,y+r['v']*scale,fill=self.project.mat(p.material).color,outline='#45546b')
            if r['u']*scale>70 and r['v']*scale>32:
                c.create_text(x+r['u']*scale/2,y+r['v']*scale/2,text=f'{n+1}. {p.name}\n{r["u"]:g} × {r["v"]:g}',font=('Segoe UI',9),width=max(30,r['u']*scale-8),fill='#172332')

    def draw(self):
        if getattr(self,'_native_document',None) is not None:self._sync_native_document()
        c=self.canvas;c.delete('all');w,h=c.winfo_width(),c.winfo_height()
        if w<10 or h<10:return
        display=self.display_project()
        from viewport_scene import paint_background,paint_faces,paint_overlay
        from viewport_modes import xray_part_ids
        xray_ids=xray_part_ids(display,self.xray.get(),self.xray_scope.get(),self.selected)
        interactive=getattr(self,'_view_interactive',False)
        background=paint_background(c,self.camera,display,(w,h),interactive=interactive)
        identity='scene:'+self.selected_scene if getattr(self,'selected_scene',None) else self.selected
        selected_part=self.selected_part()
        layout_ids={q.id for q in self.project.parts if selected_part and q.group==selected_part.group} if self.edit_mode.get() in ('layout','furniture') or self.inspector_tabs.index(self.inspector_tabs.select())==0 else set()
        gpu_result=None
        from native_viewport import NATIVE_ENGINES,paint_native,close_native
        if self.viewport_engine in NATIVE_ENGINES:
            gpu_result=paint_native(c,project=display,backend=self.viewport_engine,camera=self.camera,size=(w,h),
                interactive=interactive,selected_ids=layout_ids,selected=identity,
                explode=.22 if self.explode.get() else 0,xray=self.xray.get(),xray_ids=xray_ids,quality=self.viewport_quality)
            if gpu_result.get('ok') or self.viewport_engine!='auto':
                from gpu_viewport import close_gpu
                close_gpu(c)
            warning=gpu_result.get('warning') or gpu_result.get('error')
            if warning and getattr(self,'_gpu_warning',None)!=warning and hasattr(self,'log'):
                self._gpu_warning=warning;self.log.warning('Silnik C++: %s',warning)
        else:
            close_native(c)
        if self.viewport_engine=='gpu' or self.viewport_engine=='auto' and not (gpu_result and gpu_result.get('ok')):
            try:
                from gpu_viewport import paint_gpu
                gpu_result=paint_gpu(c,project=display,camera=self.camera,size=(w,h),interactive=interactive,selected_ids=layout_ids,
                    selected=identity,explode=.22 if self.explode.get() else 0,xray=self.xray.get(),xray_ids=xray_ids,quality=self.viewport_quality)
                if not gpu_result.get('ok') and hasattr(self,'log'):
                    error=gpu_result.get('error','Silnik GPU jest niedostępny.')
                    if getattr(self,'_gpu_warning',None)!=error:
                        self._gpu_warning=error;self.log.warning('Silnik 3D: użyto renderera CPU: %s',error)
            except (ImportError,RuntimeError,OSError,ValueError) as error:
                if getattr(self,'_gpu_warning',None)!=str(error) and hasattr(self,'log'):
                    self._gpu_warning=str(error);self.log.warning('Silnik 3D: użyto renderera CPU: %s',error)
        if gpu_result and gpu_result.get('ok'):
            self.drawn=gpu_result['drawn'];c._active_renderer='native' if gpu_result.get('engine')=='native' else 'gpu'
            if c._active_renderer=='native' and hasattr(self,'log'):
                diagnostics=gpu_result.get('diagnostics',{})
                identity_engine=(diagnostics.get('engine'),diagnostics.get('adapter'),diagnostics.get('software'))
                if identity_engine!=getattr(self,'_native_engine_logged',None):
                    self._native_engine_logged=identity_engine
                    self.log.info('Widok 3D C++: %s · %s · %s',identity_engine[0],identity_engine[1],
                                  'renderer programowy' if identity_engine[2] else 'renderowanie sprzętowe')
        else:
            c._active_renderer='cpu'
            self.drawn=render_faces(display,self.camera,w,h,identity,0.22 if self.explode.get() else 0,interactive=interactive)
            paint_faces(c,self.drawn,display,selected_ids=layout_ids,camera=self.camera,size=(w,h),xray=self.xray.get(),explode=.22 if self.explode.get() else 0,interactive=interactive,xray_ids=xray_ids)
        p=next((q for q in display.parts if q.id==self.selected),None)
        factor=.22 if self.explode.get() else 0
        def project_local(part,point):
            shift=explosion_shift(part,self.camera,factor);position=world(part,*point)
            return self.camera.project(tuple(position[i]+shift[i] for i in range(3)),w,h)
        from viewport_annotations import paint
        paint(self,display)
        c.create_text(18,20,text=f'{len(self.project.parts)} elementów   /   {self.project.name}',anchor='w',font=('Segoe UI',11),fill='#637285')
        paint_overlay(c,self.camera,(w,h),spacing=background['spacing'])
        self.handles=[];self.edge_handles=[]
        if p and not any(self.open_amounts.values()) and self.edit_mode.get() in ('resize','furniture') and not p.assembly.get('generated_by'):
            if self.edit_mode.get()=='furniture':
                from assembly import frame
                from shaped_furniture import styled_bounds
                try:
                    f=styled_bounds(self.project,p.group) or frame(self.project,p.group);lo,hi=f['lower'],f['upper'];center=[(lo[i]+hi[i])/2 for i in range(3)]
                    for index,axis in enumerate('xyz'):
                        for end in ('0','1'):
                            from core import layout_point
                            point=center[:];point[index]=lo[index] if end=='0' else hi[index];x,y,_=self.camera.project(layout_point(p,point),w,h)
                            unit=point[:];unit[index]+=1;a=self.camera.project(layout_point(p,unit),w,h);vector=(a[0]-x,a[1]-y)
                            if math.hypot(*vector)<.01:continue
                            self.handles.append(dict(face=axis+end,x=x,y=y,vector=vector,group=p.group))
                            c.create_rectangle(x-6,y-6,x+6,y+6,fill='#176a78',outline='white',width=2);c.create_text(x+10,y-12,text=axis.upper()+end,fill='#176a78',anchor='w')
                except ValueError as e:self.status.set(str(e))
            else:
                center=(p.u/2,p.v/2,p.t/2)
                for axis in ('u','v','t'):
                    index={'u':0,'v':1,'t':2}[axis]
                    for end in ('0','1'):
                        point=list(center);point[index]=0 if end=='0' else getattr(p,axis)
                        x,y,_=project_local(p,point)
                        unit=list(point);unit[index]+=1
                        a=project_local(p,unit);vector=(a[0]-x,a[1]-y)
                        if math.hypot(*vector)<.01:continue
                        color={'u':'#c95048','v':'#288563','t':'#3e77bd'}[axis]
                        c.create_rectangle(x-6,y-6,x+6,y+6,fill=color,outline='white',width=2,tags='resize_handle')
                        c.create_text(x+10,y-10,text=axis.upper()+('−' if end=='0' else '+'),fill=color,font=('Segoe UI',10,'bold'))
                        self.handles.append(dict(x=x,y=y,face=axis+end,vector=vector))
        if p and self.edit_mode.get()=='fillet':
            from cad_geometry import edge_specs,edge_path
            for number,spec in enumerate(edge_specs(p.points(),p.t,p.profile,(p.u,p.v)),1):
                edge=spec[0];path=[project_local(p,point)[:2] for point in edge_path(spec)]
                c.create_line(*(value for point in path for value in point),fill='#6c40a3',width=2)
                for qa,qb in zip(path,path[1:]):self.edge_handles.append(dict(edge=edge,a=qa,b=qb))
                label_point=path[len(path)//2]
                c.create_text(*label_point,text=str(number),fill='#5a2485',font=('Segoe UI',9,'bold'))

    def fit(self):
        from viewport_scene import fit_scene
        fit_scene(self.camera,self.display_project(),(max(self.canvas.winfo_width(),100),max(self.canvas.winfo_height(),100)))
        if self.explode.get():self.camera.scale/=1.22
        self.draw()
    def view(self,yaw,pitch):self.camera.yaw=yaw;self.camera.pitch=pitch;self.fit()
    def zoom(self,factor):self.camera.scale=max(.003,min(15,self.camera.scale*factor));self.draw()
    def wheel(self,e):self.zoom(1.12 if e.delta>0 else 1/1.12)
    def mouse_down(self,e):
        if not self.flush_parameter_drafts():
            self.start=self.last=(e.x,e.y);self.dragged=True;return
        self.canvas.focus_set();self.start=(e.x,e.y);self.last=(e.x,e.y);self.dragged=False
        if self.edit_mode.get()=='layout':
            from furniture_layout_ui import begin_drag
            begin_drag(self,e);return
        if self.edit_mode.get() in ('resize','furniture'):
            nearest=min(self.handles,key=lambda h:math.hypot(e.x-h['x'],e.y-h['y']),default=None)
            if nearest and math.hypot(e.x-nearest['x'],e.y-nearest['y'])<12:
                self.drag_edit=dict(handle=nearest,base=copy.deepcopy(self.project),candidate=None,id=self.selected,last=0)
    def direct_drag(self,e):
        drag=self.drag_edit;handle=drag['handle'];vx,vy=handle['vector']
        distance=round(((e.x-self.start[0])*vx+(e.y-self.start[1])*vy)/(vx*vx+vy*vy))
        if self.resize_step.get():distance=round(distance/32)*32
        if time.monotonic()-drag['last']<.04:return
        drag['last']=time.monotonic()
        try:
            if handle.get('group'):
                from direct_edit import resize_furniture_face
                result=resize_furniture_face(drag['base'],handle['group'],handle['face'],distance)
            else:result=resize_part(drag['base'],drag['id'],handle['face'],distance)
            drag['candidate']=result;self.project=result;self.load_form();self.draw()
            self.status.set(f'Rozciąganie {handle["face"]}: {distance:+g} mm · puść przycisk, aby zatwierdzić')
        except ValueError as error:
            drag['candidate']=None;self.project=drag['base'];self.draw();self.status.set(str(error))
    def orbit(self,e):
        if self.edit_mode.get()=='layout':
            from furniture_layout_ui import drag
            drag(self,e);return
        if self.drag_edit:self.direct_drag(e);return
        if self.edit_mode.get()!='camera':return
        dx,dy=e.x-self.last[0],e.y-self.last[1]
        if math.hypot(e.x-self.start[0],e.y-self.start[1])>3:self.dragged=True
        if self.dragged:self.camera.yaw+=dx*.45;self.camera.pitch=max(-89,min(89,self.camera.pitch+dy*.4));self.draw()
        self.last=(e.x,e.y)
    def mouse_up(self,e):
        if self.layout_drag:
            from furniture_layout_ui import end_drag
            if end_drag(self,e):return
        if self.drag_edit:
            self.drag_edit['last']=0;self.direct_drag(e)
            drag=self.drag_edit;self.project=drag['base'];self.drag_edit=None
            if drag['candidate'] is not None:self.snapshot();self.project=drag['candidate']
            self.refresh();self.status.set('Zapisano rozciągnięcie mebla / elementu · Ctrl+Z cofa zmianę');return
        if self.edit_mode.get()=='fillet' and self.edge_handles:
            def distance(item):
                ax,ay=item['a'];bx,by=item['b'];dx,dy=bx-ax,by-ay
                t=max(0,min(1,((e.x-ax)*dx+(e.y-ay)*dy)/(dx*dx+dy*dy or 1)))
                return math.hypot(e.x-ax-t*dx,e.y-ay-t*dy)
            nearest=min(self.edge_handles,key=distance)
            if distance(nearest)<10:self.round_edges(nearest['edge']);return
        if not getattr(self,'dragged',False):
            self.selected=None
            from studio_interaction import pick
            self.selected=pick(self,e.x,e.y)
            if self.selected and self.selected.startswith('scene:'):self.selected=None
            if self.selected:self.tree.selection_set(self.selected);self.tree.see(self.selected)
            else:self.tree.selection_remove(*self.tree.selection())
            self.load_form();self.draw()
    def pan_start(self,e):self.last=(e.x,e.y)
    def pan(self,e):self.camera.pan=(self.camera.pan[0]+e.x-self.last[0],self.camera.pan[1]+e.y-self.last[1]);self.last=(e.x,e.y);self.draw()

    def exact_resize(self):
        p=self.selected_part()
        if not p:return
        dialog=self.dialog('Rozciągnij wybraną ścianę','500x320')
        ttk.Label(dialog,text='Przesunięcie ściany w osi lokalnej.\nWartość dodatnia zwiększa współrzędną ściany.',padding=12).pack(fill='x')
        labels={'U max':'u1','U min':'u0','V max':'v1','V min':'v0','Grubość max':'t1','Grubość min':'t0'}
        face=tk.StringVar(value='U max');ttk.Combobox(dialog,textvariable=face,values=list(labels),state='readonly').pack(fill='x',padx=15)
        distance=self.field(dialog,'Przesunięcie [mm]',100)
        def submit():
            try:
                dialog.ensure_current_project(raise_error=True);self.require_part(p.id)
                result=resize_part(self.project,p.id,labels[face.get()],self.number(distance))
                self.snapshot();self.project=result;self.refresh();self.fit();dialog.destroy()
            except ValueError as e:messagebox.showerror('Rozciąganie',str(e),parent=dialog)
        ttk.Button(dialog,text='Zastosuj',command=submit).pack(fill='x',padx=15,pady=12)

    def furniture_dimensions(self):
        from assembly import frame
        from shaped_furniture import styled_bounds
        p=self.selected_part()
        selection=self.tree.selection()
        group=p.group if p else self.tree.item(selection[0],'text') if selection else None
        if not group:
            messagebox.showinfo('Mebel','Wybierz mebel lub jego element w drzewie projektu.',parent=self);return
        try:f=styled_bounds(self.project,group) or frame(self.project,group)
        except ValueError as e:messagebox.showerror('Mebel',str(e),parent=self);return
        lo,hi=f['lower'],f['upper']
        dialog=self.dialog('Wymiary całego mebla — '+group,'530x390')
        fields=[self.field(dialog,label,value) for label,value in [('Szerokość X [mm]',hi[0]-lo[0]),('Wysokość Z [mm]',hi[2]-lo[2]),('Głębokość Y [mm]',hi[1]-lo[1])]]
        ttk.Label(dialog,text='Grubości części i promienie pozostają stałe.\nSystem przelicza położenie boków, górę, dół i elementy zależne.\nParametry prowadnic i skrzynki szuflady pozostają zadane.',padding=15,wraplength=490).pack(fill='x')
        def submit():
            try:
                result=resize_furniture(self.project,group,*[self.number(var) for var in fields])
                self.snapshot();self.project=result;self.refresh();self.fit();dialog.destroy()
            except ValueError as e:messagebox.showerror('Wymiary mebla',str(e),parent=dialog)
        ttk.Button(dialog,text='Przelicz cały mebel',command=submit,style='Accent.TButton').pack(fill='x',padx=15,pady=12)

    def round_edges(self,edge=None):
        p=self.selected_part()
        if not p:return
        try:
            from cad_geometry import kernel
            kernel()
        except ValueError as e:messagebox.showinfo('Moduł CAD',str(e),parent=self);return
        from edge_editor import EdgeEditor
        EdgeEditor(self,p,edge)

    def add_part(self):
        self.snapshot();p=Part(material=self.project.materials[0].id);self.project.parts.append(p);self.selected=p.id;self.refresh();self.fit()
    def delete(self):
        p=self.selected_part()
        if p:
            if p.assembly.get('generated_by'):
                messagebox.showinfo('Szuflada','Część należy do wygenerowanej szuflady. Usuń front szuflady, aby usunąć cały zespół.',parent=self);return
            self.snapshot();delete_with_connections(self.project,p.id);self.selected=None;self.refresh()
    def duplicate(self):
        p=self.selected_part()
        if p:
            self.snapshot();q=copy.deepcopy(p);q.id=uid();q.name+=' kopia';q.x+=50
            q.assembly={};q.operations=[];q.role='other';q.configurator_source={}
            for cut in q.machining:cut.pop('attachment_id',None)
            self.project.parts.append(q);self.selected=q.id;self.refresh()
    def cancel_active_edits(self):
        if hasattr(self,'quick_inspector'):self.quick_inspector.cancel_pending()
        if hasattr(self,'corner_panel'):self.corner_panel.cancel_pending()
        if getattr(self,'hardware_drag',None) and self.hardware_drag.active:self.hardware_drag.cancel();return True
        if self.scene_drag:
            from room_scene_drag import cancel
            return cancel(self)
        if self.layout_drag:
            from furniture_layout_ui import cancel_drag
            cancel_drag(self);return True
        if self.drag_edit:
            self.project=self.drag_edit['base'];self.drag_edit=None;self.refresh();return True
        return False
    def undo(self):
        if self.cancel_active_edits():return
        if self._native_document is not None:
            self._sync_native_document();project=self._native_document.undo()
            if project is not None:self.project=project;self.changed();self.refresh()
            return
        if self.undo_stack:self.redo_stack.append(self.project.data());self.project=Project.from_data(self.undo_stack.pop());self.changed();self.refresh()
    def redo(self):
        if self.cancel_active_edits():return
        if self._native_document is not None:
            self._sync_native_document();project=self._native_document.redo()
            if project is not None:self.project=project;self.changed();self.refresh()
            return
        if self.redo_stack:self.undo_stack.append(self.project.data());self.project=Project.from_data(self.redo_stack.pop());self.changed();self.refresh()

    def dialog(self,title,size='520x600'):
        from form_dialog import FormDocument
        return FormDocument(self,title,size).form
    def field(self,parent,label,default):
        frame=ttk.Frame(parent);frame.pack(fill='x',padx=15,pady=5)
        ttk.Label(frame,text=label,width=26).pack(side='left')
        var=tk.StringVar(value=str(default));ttk.Entry(frame,textvariable=var).pack(side='right',fill='x',expand=True);return var
    def number(self,v):return float(v.get().replace(',','.'))

    def template(self):
        d=self.dialog('Dodaj mebel parametryczny','520x610')
        ttk.Label(d,text='Konstrukcja',style='Title.TLabel',padding=15).pack(anchor='w')
        kind=tk.StringVar(value='Szafka');ttk.Combobox(d,textvariable=kind,values=['Szafka','Szafa','Regał','Stół'],state='readonly').pack(fill='x',padx=15,pady=5)
        fields={k:self.field(d,label,v) for k,label,v in [('group','Nazwa mebla','Mebel 1'),('w','Szerokość X [mm]',800),('h','Wysokość Z [mm]',1200),('d','Głębokość Y [mm]',480),('t','Grubość płyty [mm]',18),('s','Liczba półek',2),('x','Pozycja X [mm]',0),('y','Pozycja Y [mm]',0),('z','Pozycja Z [mm]',0)]}
        front=tk.BooleanVar(value=True);ttk.Checkbutton(d,text='Dodaj dwa fronty (szafka / szafa)',variable=front).pack(anchor='w',padx=15,pady=10)
        ttk.Label(d,text='Plecy nakładane 3 mm. Fronty: szczeliny po 2 mm.\nKonstrukcja robocza; po dodaniu edytuj każdą część.',padding=15).pack(anchor='w')
        def submit():
            try:
                temp=copy.deepcopy(self.project)
                s=self.number(fields['s'])
                if s!=int(s):raise ValueError('Liczba półek musi być całkowita.')
                parts=add_template(temp,{'Szafka':'cabinet','Szafa':'wardrobe','Regał':'shelf','Stół':'table'}[kind.get()],*[self.number(fields[k]) for k in ('w','h','d','t')],shelves=int(s),front=front.get(),origin=tuple(self.number(fields[k]) for k in ('x','y','z')),group=fields['group'].get())
                temp.validate();self.snapshot();self.project=temp;self.selected=parts[0].id;self.refresh();self.fit();d.destroy()
            except ValueError as e:messagebox.showerror('Sprawdź dane',str(e),parent=d)
        ttk.Button(d,text='Dodaj do projektu',command=submit,style='Accent.TButton').pack(fill='x',padx=15,pady=10)

    def contour(self):
        p=self.selected_part()
        if not p:return
        if p.assembly:
            self.edit_sketch();return
        d=self.dialog('Kontur i otwory · '+p.name,'650x650')
        ttk.Label(d,text='Kontur lokalny U;V · jeden punkt na wiersz',padding=12).pack(anchor='w')
        points=tk.Text(d,height=9,font=('Consolas',11));points.pack(fill='both',expand=True,padx=12)
        if p.contour:points.insert('1.0','\n'.join(f'{x:g};{y:g}' for x,y in p.contour))
        ttk.Label(d,text='Puste pole = prostokąt. Minimum U i V musi wynosić 0.\nKontur zamykany automatycznie. Nieregularne obrzeża nie są obsługiwane.',padding=12).pack(anchor='w')
        ttk.Label(d,text='Otwory od strony +T: U;V;średnica;głębokość [mm]',padding=12).pack(anchor='w')
        holes=tk.Text(d,height=7,font=('Consolas',11));holes.pack(fill='both',expand=True,padx=12)
        holes.insert('1.0','\n'.join(';'.join(f'{x:g}' for x in hole) for hole in p.holes))
        ttk.Label(d,text='Otwory zapisują się w CSV i raporcie; DXF zawiera tylko koła.\nW widoku 3D włącz „Otwory montażowe”, aby zobaczyć pozycje wierceń.',padding=12).pack(anchor='w')
        def parse(widget,n):
            result=[]
            for line in widget.get('1.0','end').strip().splitlines():
                values=[float(i.strip().replace(',','.')) for i in line.split(';')]
                if len(values)!=n:raise ValueError(f'Każdy wiersz wymaga {n} wartości rozdzielonych średnikiem.')
                result.append(values)
            return result
        def submit():
            try:
                d.ensure_current_project(raise_error=True)
                current=self.require_part(p.id);q=copy.deepcopy(current);q.contour=parse(points,2);q.holes=parse(holes,4)
                if q.contour!=current.contour and (current.profile or current.fillets or current.chamfers):
                    raise ValueError('Kontur z profilem lub obróbką kantów zmień w edytorze szkicu / CAD.')
                if q.contour:q.edges=[False]*4
                q.validate();self.snapshot();self.project.parts[self.project.parts.index(current)]=q;self.refresh();d.destroy()
            except ValueError as e:messagebox.showerror('Sprawdź kontur',str(e),parent=d)
        ttk.Button(d,text='Zapisz kontur i otwory',command=submit,style='Accent.TButton').pack(fill='x',padx=12,pady=12)

    def move(self):
        p=self.selected_part()
        if not p:return
        if p.assembly or p.operations:
            messagebox.showinfo('Montaż','Użyj „Edytuj szkic” dla elementów powiązanych i podpór okuć.',parent=self);return
        d=self.dialog('Przesunięcie elementu','450x280');fields=[self.field(d,'Przesunięcie '+axis+' [mm]',0) for axis in ['X','Y','Z']]
        duplicate=tk.BooleanVar();ttk.Checkbutton(d,text='Utwórz kopię',variable=duplicate).pack(anchor='w',padx=15,pady=10)
        def submit():
            try:
                d.ensure_current_project(raise_error=True)
                current=self.require_part(p.id);q=copy.deepcopy(current)
                for key,var in zip(('x','y','z'),fields):setattr(q,key,getattr(q,key)+self.number(var))
                q.validate();self.snapshot()
                if duplicate.get():q.id=uid();q.name+=' kopia';self.project.parts.append(q)
                else:self.project.parts[self.project.parts.index(current)]=q
                self.selected=q.id;self.refresh();d.destroy()
            except ValueError as e:messagebox.showerror('Sprawdź dane',str(e),parent=d)
        ttk.Button(d,text='Zastosuj',command=submit,style='Accent.TButton').pack(fill='x',padx=15,pady=10)

    def settings(self):
        d=self.dialog('Materiały i ustawienia produkcji','680x850')
        ttk.Label(d,text='Materiały · ceny własne',style='Title.TLabel',padding=15).pack(anchor='w')
        mv=tk.StringVar(value=self.project.materials[0].name);combo=ttk.Combobox(d,textvariable=mv,values=[m.name for m in self.project.materials],state='readonly');combo.pack(fill='x',padx=15)
        vals={k:self.field(d,label,getattr(self.project.materials[0],k)) for k,label in [('name','Nazwa materiału'),('kind','Rodzaj: plyta/mdf/sklejka/drewno/hdf/inne'),('color','Kolor #RRGGBB'),('price_m2','Cena płyt [zł/m²]'),('price_m3','Cena drewna litego [zł/m³]'),('sheet_u','Długość surowca U [mm]'),('sheet_v','Szerokość surowca V [mm]')]}
        def load(e=None):
            m=self.project.materials[combo.current()]
            for k,v in vals.items():v.set(getattr(m,k))
        combo.bind('<<ComboboxSelected>>',load)
        def choose():
            col=colorchooser.askcolor(vals['color'].get(),parent=d)[1]
            if col:vals['color'].set(col)
        ttk.Button(d,text='Wybierz kolor',command=choose).pack(anchor='w',padx=15,pady=8)
        ttk.Separator(d).pack(fill='x',padx=15,pady=8)
        setting={k:self.field(d,label,getattr(self.project,k)) for k,label in [('kerf','Rzaz piły [mm]'),('margin','Margines płyty [mm]'),('edge_t','Grubość obrzeża [mm]'),('edge_price','Obrzeże z usługą [zł/m]'),('labor','Robocizna projektu [zł]'),('markup','Narzut na koszt [%]')]}
        ttk.Label(d,text='Włókna biegną wzdłuż U płyty. Narzut nie jest marżą.\nCeny są przykładowe; wpisz ceny swojego dostawcy.\nZapisz wybrany materiał przed przejściem do kolejnego.',padding=15).pack(anchor='w')
        def submit():
            try:
                temp=copy.deepcopy(self.project);m=temp.materials[combo.current()]
                for k,v in vals.items():setattr(m,k,v.get() if k in ('name','color','kind') else self.number(v))
                for k,v in setting.items():setattr(temp,k,self.number(v))
                temp.validate();self.snapshot();self.project=temp;self.refresh();combo['values']=[m.name for m in temp.materials];mv.set(m.name);self.status.set('Zapisano materiał i ceny')
            except ValueError as e:messagebox.showerror('Sprawdź dane',str(e),parent=d)
        ttk.Button(d,text='Zapisz materiał i ustawienia',command=submit,style='Accent.TButton').pack(fill='x',padx=15,pady=8)
        def add():
            self.snapshot();m=Material(name='Nowy materiał');self.project.materials.append(m);combo['values']=[m.name for m in self.project.materials];combo.current(len(self.project.materials)-1);load();self.refresh()
        ttk.Button(d,text='+ Nowy materiał',command=add).pack(fill='x',padx=15,pady=4)
        ttk.Button(d,text='Zamknij',command=d.destroy).pack(fill='x',padx=15,pady=4)

    def hardware(self):
        d=self.dialog('Akcesoria · ręczne zestawienie','650x480')
        ttk.Label(d,text='Nazwa; ilość; cena jednostkowa [zł] · jeden wpis na wiersz',padding=15).pack(anchor='w')
        txt=tk.Text(d,font=('Consolas',11));txt.pack(fill='both',expand=True,padx=15)
        txt.insert('1.0','\n'.join(f'{h["name"]};{h["qty"]};{h["price"]}' for h in self.project.hardware if not h.get('owner_id')))
        automatic=[h for h in self.project.hardware if h.get('owner_id')]
        ttk.Label(d,text='Ręczne akcesoria: nazwa;ilość;cena.\nAutomatyczne okucia zmienisz przez „Edytuj szkic” i parametry okuć.\n'+', '.join(f'{h["qty"]:g} × {h["name"]}' for h in automatic),padding=15,wraplength=600).pack(anchor='w')
        def submit():
            try:
                result=[]
                for line in txt.get('1.0','end').strip().splitlines():
                    name,qty,price=line.split(';');result.append(dict(name=name.strip(),qty=float(qty.replace(',','.')),price=float(price.replace(',','.'))))
                d.ensure_current_project(raise_error=True)
                automatic_current=[h for h in self.project.hardware if h.get('owner_id')]
                temp=copy.deepcopy(self.project);temp.hardware=result+copy.deepcopy(automatic_current);temp.validate();self.snapshot();self.project=temp;self.refresh();d.destroy()
            except ValueError as e:messagebox.showerror('Sprawdź dane',str(e),parent=d)
        ttk.Button(d,text='Zapisz zestawienie',command=submit,style='Accent.TButton').pack(fill='x',padx=15,pady=10)

    def confirm_save(self):
        if not self.dirty:return True
        ans=messagebox.askyesnocancel('Niezapisany projekt','Zapisać zmiany przed kontynuowaniem?',parent=self)
        if ans is None:return False
        return self.save() if ans else True
    def confirm_project_transition(self):
        if not self.flush_parameter_drafts():return False
        return self.workspace.confirm_project_change() and self.confirm_save()
    def end_project_documents(self):
        self.selected_scene=None
        self.cancel_active_edits()
        self.workspace.close_project_documents()
        self.project_generation+=1
        if self.animation_job:
            self.after_cancel(self.animation_job)
            self.animation_job=None
        self.open_amounts={};self.motion_value.set(0)
    def new(self):
        if not self.confirm_project_transition():return False
        self.end_project_documents()
        self.project=Project();
        from palette import install_palette
        install_palette(self.project)
        if self._native_document is not None:self._native_document.reset(self.project)
        self.open_amounts={};self.animation_job=None;self.motion_value.set(0);self.motion_all.set(False);self.resize_step.set(False)
        self.path=None;self.selected=None;self.undo_stack.clear();self.redo_stack.clear();self.dirty=False;self.title('Mebel Studio 3D · Mój projekt');self.refresh();self.fit();self.workspace.select(self.design_page);return True
    def save(self,save_as=False):
        if not self.flush_parameter_drafts():return False
        self.cancel_active_edits()
        self._sync_native_document()
        path=self.path
        if save_as or not path:path=filedialog.asksaveasfilename(parent=self,title='Zapisz projekt',defaultextension='.mebel',filetypes=[('Projekt Mebel Studio','*.mebel')],initialfile=self.project.name+'.mebel')
        if not path:return False
        previous_name=self.project.name
        try:
            self.project.name=Path(path).stem
            if self._native_document is not None:self._native_document.save(self.project,path)
            else:self.project.save(path)
            self.path=path;self.dirty=False;self.title(f'Mebel Studio 3D · {self.project.name}');self.status.set('Zapisano: '+str(path));self.log.info('Zapisano projekt: %s',path);return True
        except (OSError,ValueError) as e:
            self.project.name=previous_name
            if self._native_document is not None:
                from native_document import serialize
                authoritative=self._native_document.read()
                if serialize(self.project)!=authoritative:self.project=Project.from_data(json.loads(authoritative))
                self._native_dirty_state()
            messagebox.showerror('Nie udało się zapisać',str(e),parent=self);return False
    def open(self):
        path=filedialog.askopenfilename(parent=self,filetypes=[('Projekt Mebel Studio','*.mebel')])
        return self.load_path(path) if path else False
    def load_path(self,path):
        try:
            project=Project.load(path)
        except (OSError,ValueError,KeyError,TypeError,StopIteration) as e:
            messagebox.showerror('Nie udało się otworzyć projektu',str(e),parent=self);return False
        if not self.confirm_project_transition():return False
        self.end_project_documents()
        if self._native_document is not None:self._native_document.reset(project)
        self.project=project;self.path=str(path);self.dirty=False;self.undo_stack.clear();self.redo_stack.clear();self.selected=None;self.refresh();self.fit();self.workspace.select(self.design_page);self.title(f'Mebel Studio 3D · {project.name}');self.status.set('Otwarto: '+str(path));self.log.info('Otwarto projekt: %s',path);return True
    def close(self):
        if not self.confirm_project_transition():return False
        if hasattr(self,'ui_layout'):
            try:
                self.ui_layout.persist()
            except (OSError,ValueError) as error:self.log.warning('Nie zapisano układu interfejsu: %s',error)
        self.end_project_documents();self.destroy();return True

    def destroy(self):
        if getattr(self,'_native_document',None) is not None:self._native_document.close()
        if hasattr(self,'canvas'):
            try:
                from native_viewport import close_native
                close_native(self.canvas)
                from gpu_viewport import close_gpu
                close_gpu(self.canvas)
            except (ImportError,RuntimeError,OSError):pass
        from developer_logs import dispose
        if hasattr(self,'log'):
            self.log.info('Zamknięto program');dispose(self)
        super().destroy()

    def export(self):
        if not self.flush_parameter_drafts():return False
        self.cancel_active_edits();self._sync_native_document()
        d=self.dialog('Eksport dokumentacji','610x760')
        ttk.Label(d,text='Dokumentacja projektu',style='Title.TLabel',padding=15).pack(anchor='w')
        def run(kind):
            ext={'csv':'.csv','dxf':'.dxf','html':'.html','svg':'.svg','json':'.json','step':'.step','stl':'.stl','drawings':'.html','labels':'.html','project_print':'.html'}[kind]
            path=filedialog.asksaveasfilename(parent=d,defaultextension=ext,initialfile=self.project.name+ext,filetypes=[('Eksport', '*'+ext)])
            if not path:return
            try:
                self.project.validate()
                if kind=='csv':csv_export(self.project,path)
                elif kind=='dxf':dxf_export(self.project,path)
                elif kind=='html':report_export(self.project,path)
                elif kind=='json':manufacturing_export(self.project,path)
                elif kind in ('drawings','labels'):
                    from production_docs import drawings_export,labels_export
                    (drawings_export if kind=='drawings' else labels_export)(self.project,path)
                elif kind=='project_print':
                    from project_print import export_project_print
                    export_project_print(self.project,path)
                elif kind=='stl':
                    from cad_geometry import stl_export
                    stl_export(self.project,path)
                elif kind=='step':
                    from cad_geometry import step_export
                    step_export(self.project,path)
                else:
                    cam=copy.deepcopy(self.camera);cam.fit(self.project,1400,900);Path(path).write_text(svg_view(self.project,cam,1400,900),encoding='utf-8')
                self.status.set('Wyeksportowano: '+path)
                if kind in ('html','svg','drawings','labels','project_print'):webbrowser.open(Path(path).resolve().as_uri())
                else:messagebox.showinfo('Eksport gotowy','Zapisano plik:\n'+path,parent=d)
            except (OSError,ValueError) as e:messagebox.showerror('Błąd eksportu',str(e),parent=d)
        for label,kind in [('Komplet + rozstrzelone szafki + rozkrój · druk / PDF','project_print'),('Formatki CSV · Excel / rozkrój','csv'),('Kontury i otwory DXF · dokumentacja 2D','dxf'),('Bryły STEP · dokładna geometria obróbek','step'),('Model STL · siatka 3D','stl'),('Rysunki części · wymiary i obróbki','drawings'),('Etykiety części · druk A4','labels'),('Dane produkcyjne JSON · geometria i wiercenia','json'),('Raport HTML · wydruk / zapis PDF','html'),('Widok SVG · prezentacja klientowi','svg')]:ttk.Button(d,text=label,command=lambda k=kind:run(k)).pack(fill='x',padx=15,pady=6)
        ttk.Label(d,text='DXF nie jest programem maszyny CNC.\nRaport zawiera model, rzuty, formatki, rozkrój i kosztorys.\nPDF zapiszesz z raportu przez „Drukuj”.',padding=15).pack(anchor='w')

    def help(self):
        path=ROOT/'INSTRUKCJA.html'
        if path.exists():webbrowser.open(path.as_uri())
        else:messagebox.showinfo('Sterowanie','Dodaj mebel lub element. Zaznacz część i zmień właściwości.\nLPM obraca widok; PPM przesuwa; rolka powiększa.\nCtrl+S zapisuje; Ctrl+Z cofa; Ctrl+Y ponawia.',parent=self)

if __name__=='__main__':
    try:App().mainloop()
    except Exception:
        import traceback
        error=traceback.format_exc()
        try:Path.home().joinpath('MebelStudio3D-blad.txt').write_text(error,encoding='utf-8')
        except OSError:pass
        try:messagebox.showerror('Błąd uruchomienia','Nie udało się uruchomić aplikacji.\n'+error[-1500:])
        except Exception:print(error)
        sys.exit(1)

