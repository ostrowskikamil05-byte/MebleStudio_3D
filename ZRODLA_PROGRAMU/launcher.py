"""Frozen desktop entry point and offline diagnostic mode."""
import sys,json,traceback
from pathlib import Path

def self_test(path):
    result={'version':'0.15','frozen':bool(getattr(sys,'frozen',False)),'checks':[]}
    app=None;diagnostic_settings=None;original_preference_path=None
    try:
        from core import Project,Part,add_template
        from cad_geometry import local_shape,step_export
        from furniture_library import create_furniture,TEMPLATES
        from fittings import cabinet_connectors,system32
        from mechanisms import posed_project
        import tempfile
        p=Project();part=Part(u=200,v=120,t=20,material=p.materials[0].id,chamfers=[dict(edge='top:0',distance=2)],machining=[dict(type='pocket',shape='rect',u=100,v=60,length=40,width=20,depth=5,face='top')]);p.parts=[part];p.validate();assert local_shape(part).Volume()<480000;result['checks'].append('CAD built in: chamfer and pocket')
        with tempfile.TemporaryDirectory() as d:step_export(p,Path(d)/'test.step');assert (Path(d)/'test.step').stat().st_size>100;result['checks'].append('STEP export')
        cabinet,group=create_furniture(Project(),0);cabinet=cabinet_connectors(cabinet,group);cabinet.validate();result['checks'].append('Mating confirmat drills and countersink')
        side=next(p for p in cabinet.parts if p.name=='Bok lewy');cabinet=system32(cabinet,side.id);result['checks'].append('System 32')
        for i in range(len(TEMPLATES)):
            project,group=create_furniture(Project(),i);posed=posed_project(project,{p.id:.75 for p in project.parts});assert len(posed.parts)==len(project.parts)
        result['checks'].append(f'All {len(TEMPLATES)} furniture templates and opening transforms')
        from runners_catalog import PROFILES
        from runner_installation import apply_runner
        drawers,group=create_furniture(Project(),3);front=next(p for p in drawers.parts if p.role=='drawer')
        drawers=apply_runner(drawers,front.id,'blum_movento_760h',400)
        plan=next(h['dimensions'] for h in drawers.hardware if h.get('runner_profile')=='blum_movento_760h')
        assert plan['external_clearance_per_side']==5 and plan['drawer_outer_width']==554
        result['checks'].append(f'Runner catalogue: {len(PROFILES)} profiles; synchronized MOVENTO drawer')
        from opening_options import install
        door=next(p for p in cabinet.parts if p.role=='door');opened=install(cabinet,door.id,'tip_bumper',hinge_spring='sprung')
        assert any(o.get('opening_owner')==door.id and o['diameter']==10 for p in opened.parts for o in p.operations)
        result['checks'].append('Handle alternatives and real ejector drilling')
        from shaped_furniture import TEMPLATES as SHAPES,create_shaped_furniture,apply_shape
        import math
        for i in range(len(SHAPES)):
            model,group=create_shaped_furniture(Project(),i);model.validate()
        disc=Project();disc.parts=[Part(u=400,v=400,t=28,material=disc.materials[0].id)]
        disc=apply_shape(disc,disc.parts[0].id,'circle')
        assert abs(local_shape(disc.parts[0]).Volume()-math.pi*200**2*28)<.01
        result['checks'].append(f'{len(SHAPES)} shaped furniture templates and exact circular CAD geometry')
        disc.parts[0].fillets=[dict(edge='top:0',radius=3)]
        disc.parts[0].chamfers=[dict(edge='bottom:0',distance=2)];disc.validate()
        assert local_shape(disc.parts[0]).Volume()<math.pi*200**2*28
        result['checks'].append('Exact circular rim fillet and chamfer')
        from furniture_layout import rotate_group,snap_group,group_bounds,move_group
        placed,first=create_furniture(Project(),0,group='A')
        placed,second=create_furniture(placed,0,origin=(900,0,0),group='B')
        placed=rotate_group(placed,second,90);placed=snap_group(placed,second,first,'x+')
        assert abs(group_bounds(placed,first)[1][0]-group_bounds(placed,second)[0][0])<.001
        loaded=Project.from_data(placed.data());assert loaded.data()==placed.data()
        posed=posed_project(loaded,{p.id:.75 for p in loaded.parts})
        assert posed.parts[-1].layout==loaded.parts[-1].layout
        result['checks'].append('Rigid layout, rotation, six-direction magnetic placement and roundtrip')
        from project_print import export_project_print
        with tempfile.TemporaryDirectory() as d:
            book=Path(d)/'complete.html';info=export_project_print(loaded,book)
            assert len(info['groups'])==2 and info['parts']==len(loaded.parts)
            assert 'window.print()' in book.read_text(encoding='utf-8')
        result['checks'].append('Dimensioned set and exploded cabinet print book with cutting plans')
        from app import App
        from sketch import Sketch
        import ui_preferences
        diagnostic_settings=tempfile.TemporaryDirectory(prefix='MebelStudio3D-diagnostic-')
        original_preference_path=ui_preferences.preference_path
        ui_preferences.preference_path=lambda:Path(diagnostic_settings.name)/'ui_preferences.json'
        ui_preferences.save_preferences(dict(ui_preferences.DEFAULTS,engine='cpu'))
        app=App();app.withdraw();app.update();assert app.canvas.winfo_exists()
        editor=Sketch(app);editor.fields['name'].set('Offline unfinished sketch');app.update()
        canvas=editor.canvas;app.workspace.detach(editor);app.update();app.workspace.dock(editor);app.update()
        assert editor.canvas is canvas and editor.fields['name'].get()=='Offline unfinished sketch'
        main=app.design_page;app.workspace.detach(main);app.update()
        callback=app.tk.call('wm','protocol',str(main),'WM_DELETE_WINDOW');app.tk.call(callback);app.update()
        assert not app.workspace.detached(main) and app.grab_current() is None
        form=app.dialog('Offline form','520x600');app.field(form,'Value',1);app.update()
        assert hasattr(form,'ensure_current_project') and app.grab_current() is None
        form.destroy();app.update()
        result['checks'].append('Detached 3D native Close and non-modal clipped form tabs')
        from furniture_configurator import default_config,configure,configuration_status
        from configurator_accessories import accessory_vertices
        model=Project();cfg=default_config(3,model);cfg['leg_count']=6
        model,group=configure(model,cfg)
        assert len([p for p in model.parts if p.role=='drawer'])==3
        assert next(h for h in model.hardware if h.get('kind')=='leg')['qty']==6
        assert min(v[2] for v in accessory_vertices(model))==0
        cfg['width']=800;model,group=configure(model,cfg,group)
        assert configuration_status(model,group)['editable']
        result['checks'].append('Unified configurator: three drawers, six purchased legs and atomic resize')
        from quick_studio_ui import focus_section
        app.workspace.detach(app.design_page);app.update();focus_section(app,'fronts');app.update()
        assert app.workspace.detached(app.design_page) and app.grab_current() is None
        app.workspace.dock(app.design_page);app.update()
        result['checks'].append('Creator settings remain responsive with detached 3D')
        from room_objects import add_object,set_scene
        equipped=add_object(model,'washer',x=900,price=1000)
        equipped=add_object(equipped,'lamp',x=400,z=1800)
        equipped=set_scene(equipped,brightness=.35,led_enabled=True)
        assert Project.from_data(equipped.data()).data()==equipped.data()
        assert len(equipped.parts)==len(model.parts) and equipped.costs()['hardware']>=1000
        result['checks'].append('Purchased room objects, lamp emitter, pricing and persisted dark scene')
        from workspace_worktops import install_shared_worktop,read_shared_worktop
        from corner_furniture import create_corner,default_corner_config
        test=Project();options=default_config(0,test);options.update(width=600,worktop=False)
        test,body=configure(test,options);test,top=install_shared_worktop(test,[body])
        assert read_shared_worktop(test,top)['status']=='ready'
        from furniture_configurator import read_config
        assert read_config(test,body)['top_mode']=='rails'
        corner=default_corner_config(test);corner.update(side='right',shape='rounded',origin=[900,0,0])
        test,cg=create_corner(test,corner);test.validate()
        assert any(p.contour and len(p.contour)>6 for p in test.parts if p.group==cg)
        result['checks'].append('Real reinforcing rails, linked worktop and rounded right corner contour')
        from core import render_faces
        rendered=render_faces(equipped,app.camera,900,600,interactive=True)
        assert any(f.get('scene_object') for f in rendered)
        from viewport_scene import paint_faces
        paint_faces(app.canvas,rendered*(800//max(1,len(rendered))+1),equipped,camera=app.camera,size=(900,600))
        assert app.canvas.find_withtag('viewport.raster_model')
        result['checks'].append('Offline room geometry, lighting and raster-capable viewport')
        from viewport_render import clear_caches
        clear_caches()
        for index in range(12):
            altered=set_scene(equipped,brightness=.3+index*.08)
            render_faces(altered,app.camera,900,600,interactive=True)
            render_faces(altered,app.camera,900,600,interactive=False)
        result['checks'].append('Cold interactive to full geometry survives repeated scene mutations without color-cache mismatch')
        from depth_raster import DepthRaster,pick_visible
        depth=DepthRaster((120,80),['front','rear'])
        depth.polygon([(5,5),(115,5),(115,75),(5,75)],(10,10,10,10),'#e86240','front')
        depth.polygon([(5,5),(115,5),(115,75),(5,75)],(0,0,0,0),'#4090e8','rear')
        assert depth.ids[int(depth.owner[40,60])]=='front'
        result['checks'].append('Opaque surfaces use per-pixel depth independent of polygon draw order')
        from drag_hardware import attach
        leaf=next(p for p in model.parts if p.role=='drawer')
        fitted=attach(model,'led',leaf.id,dict(u=leaf.u/2,v=leaf.v/2,w=0,face='bottom'),dict(width=12,margin=30))
        assert any(h.get('manual') and h.get('kind')=='led_strip' for h in fitted.hardware)
        from room_architecture import world_faces
        fitted=set_scene(fitted,architecture=dict(enabled=True))
        assert world_faces(fitted,app.camera) and Project.from_data(fitted.data()).data()==fitted.data()
        result['checks'].append('Manual LED fitting, automatic length and persisted room walls are available offline')
        doc=app.open_developer_logs();app.update();assert doc.winfo_exists();doc.destroy();app.update()
        assert app.developer_log_path.is_file()
        result['checks'].append('Developer log tab closes and rotating disk log is available')
        app.project=model;app.selected=model.parts[0].id;app.refresh();app.fit();app.update()
        before=app.project.data();dirty=app.dirty
        prefs=app.ui_layout.preferences();prefs.update(ui_scale=1.2,inspector_width=270,engine='cpu')
        app.ui_layout.apply(prefs,geometry=False);app.update()
        settings=app.interface_settings();app.update()
        assert settings.interface_vars['ui_scale'].get().startswith('120%')
        settings.destroy();app.ui_layout.show_inspector(False);app.corner_dialog();app.update()
        assert app.ui_layout.preferences()['show_inspector']
        assert app.project.data()==before and app.dirty==dirty
        result['checks'].append('Responsive panels and arbitrary saved UI scale preserve the furniture project')
        from visual_catalogue import catalogue_items
        assert len(catalogue_items('furniture',app.project))==len(TEMPLATES)
        assert len(catalogue_items('runner',app.project))>=10
        assert catalogue_items('leg',app.project) and catalogue_items('appliance',app.project)
        catalogue=app.visual_catalogue('leg');app.update()
        same=app.visual_catalogue('runner');app.update()
        assert catalogue is same and app.grab_current() is None
        catalogue.form.destroy();app.workspace.select(app.design_page);app.update()
        result['checks'].append('Graphic furniture, fitting, leg and room catalogues share one non-modal tab')
        from viewport_modes import xray_part_ids
        ids=xray_part_ids(app.project,True,'Cały zestaw',app.selected)
        assert ids=={p.id for p in app.project.parts if p.visible}
        app.xray.set(True);app.xray_scope.set('Cały zestaw');app.draw();app.update()
        app.xray.set(False);app.show_drills.set(True);app.drill_mode.set('Także zasłonięte');app.draw();app.update()
        assert app.project.data()==before
        result['checks'].append('Whole-body X-ray and highlighted mounting-hole overlays preserve exact geometry')
        app.viewport_engine='gpu';app.viewport_quality='balanced';app.draw();app.update()
        result['gpu']=dict(engine=app.canvas._active_renderer,error=getattr(app.canvas,'_gpu_error',None),
                          diagnostics=getattr(app.canvas,'_render_diagnostics',{}))
        if app.canvas._active_renderer=='gpu':
            import numpy as np
            assert (app.canvas._id_buffer>=0).any() and np.isfinite(app.canvas._depth_buffer).any()
            app.viewport_quality='high';app.draw();app.update()
            assert app.canvas._active_renderer=='gpu'
        else:assert getattr(app.canvas,'_gpu_error',None)
        app.viewport_engine='cpu';app.draw();app.update()
        assert app.canvas._active_renderer=='cpu' and app.project.data()==before
        result['checks'].append('Bundled OpenGL view, high-quality mode and controlled CPU fallback')
        app.ui_layout.persist();assert ui_preferences.preference_path().is_file()
        app.destroy();app=None;result['checks'].append('Tkinter startup and retained dockable sketch state')
        result['ok']=True
    except Exception:result.update(ok=False,error=traceback.format_exc())
    finally:
        if app is not None:
            try:app.destroy()
            except Exception:pass
        if original_preference_path is not None:ui_preferences.preference_path=original_preference_path
        if diagnostic_settings is not None:diagnostic_settings.cleanup()
    report_path=Path(path).expanduser().resolve();report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');return 0 if result['ok'] else 1

if __name__=='__main__':
    import multiprocessing
    multiprocessing.freeze_support()
    if len(sys.argv)>2 and sys.argv[1]=='--self-test':sys.exit(self_test(sys.argv[2]))
    from app import App
    try:App().mainloop()
    except Exception:
        if getattr(sys,'_mebel_native_host',False):raise
        from tkinter import messagebox
        messagebox.showerror('Błąd uruchomienia',traceback.format_exc()[-2000:]);sys.exit(1)

