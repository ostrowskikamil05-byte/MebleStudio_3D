"""Responsive toolbar layout and user controlled panes, independent of projects."""
import copy
import sys
import tkinter as tk
from tkinter import ttk, font as tkfont
from ui_preferences import DEFAULTS, load_preferences, save_preferences, validate_preferences


def flow_layout(width, sizes, gap=4):
    """Return (x,y,width,height) for every item; keep every action reachable."""
    width=max(1,int(width)); x=y=row_height=0; result=[]
    for requested_w, requested_h in sizes:
        w=min(width,max(1,int(requested_w))); h=max(1,int(requested_h))
        if x and x+w>width: x=0; y+=row_height+gap; row_height=0
        result.append((x,y,w,h)); x+=w+gap; row_height=max(row_height,h)
    return result, y+row_height if result else 1


class WrapToolbar(ttk.Frame):
    """Adopts packed children, including later plugin controls, without grid conflicts."""
    def __init__(self,parent,**kwargs):
        super().__init__(parent,**kwargs); self._job=None; self._layout_key=None
        self.pack_propagate(False); self.configure(height=1)
        self.bind('<Configure>',self.schedule,add='+'); self.bind('<Map>',self.schedule,add='+')
        self.bind('<Destroy>',self.destroyed,add='+')
    def destroyed(self,event):
        if event.widget is self and self._job is not None:
            try:self.after_cancel(self._job)
            except tk.TclError:pass
            self._job=None
    def schedule(self,event=None):
        if self._job is None:self._job=self.after_idle(self.reflow)
    def reflow(self):
        self._job=None
        if not self.winfo_exists():return
        children=self.winfo_children(); width=max(1,self.winfo_width())
        sizes=[(w.winfo_reqwidth()+4,w.winfo_reqheight()+4) for w in children]
        key=(width,tuple((str(w),s) for w,s in zip(children,sizes)))
        if key==self._layout_key:return
        self._layout_key=key; placements,height=flow_layout(width,sizes)
        for widget,(x,y,w,h) in zip(children,placements):
            if widget.winfo_manager()=='pack':widget.pack_forget()
            widget.place(x=x+2,y=y+2,width=max(1,w-4),height=max(1,h-4))
        self.configure(height=height)


class RibbonPage(ttk.Frame):
    def __init__(self,parent):
        super().__init__(parent)
        self.canvas=tk.Canvas(self,highlightthickness=0,bg='#f6f8fa',height=76)
        scrollbar=ttk.Scrollbar(self,orient='vertical',command=self.canvas.yview)
        scrollbar.pack(side='right',fill='y');self.canvas.pack(side='left',fill='both',expand=True)
        self.body=WrapToolbar(self.canvas)
        self.window=self.canvas.create_window(0,0,window=self.body,anchor='nw')
        self.body.bind('<Configure>',lambda e:self.canvas.configure(scrollregion=self.canvas.bbox('all')),add='+')
        self.canvas.bind('<Configure>',lambda e:self.canvas.itemconfigure(self.window,width=e.width))
        self.canvas.bind('<MouseWheel>',lambda e:self.canvas.yview_scroll(-1 if e.delta>0 else 1,'units'))


class FieldRow(ttk.Frame):
    """A label and editor stack when the inspector is narrower than their content."""
    def __init__(self,parent,label,**entry_options):
        super().__init__(parent);self.label=ttk.Label(self,text=label,font=('Segoe UI',9))
        self.entry=ttk.Entry(self,**entry_options);self.columnconfigure(0,weight=1)
        self.bind('<Configure>',self.arrange);self._idle=self.after_idle(self.initial_arrange)
        self.bind('<Destroy>',self.destroyed,add='+')
        self._stacked=None
    def initial_arrange(self):self._idle=None;self.arrange()
    def destroyed(self,event):
        if event.widget is self and self._idle is not None:
            try:self.after_cancel(self._idle)
            except tk.TclError:pass
            self._idle=None
    def arrange(self,event=None):
        width=max(1,self.winfo_width())
        label_width=tkfont.Font(root=self,font=self.label.cget('font')).measure(self.label.cget('text'))+4
        stack=width<label_width+self.entry.winfo_reqwidth()+8
        self.label.configure(wraplength=max(80,width-4) if stack else 0)
        if stack==self._stacked:return
        self._stacked=stack;self.label.grid_forget();self.entry.grid_forget()
        if stack:
            self.label.grid(row=0,column=0,sticky='w',pady=(0,2))
            self.entry.grid(row=1,column=0,sticky='ew')
        else:
            self.label.configure(wraplength=0);self.label.grid(row=0,column=0,sticky='w')
            self.entry.grid(row=0,column=1,sticky='e',padx=(5,0))


def monitor_info(window=None,point=None):
    """Read current monitor work area and DPI; never change Windows settings."""
    try:
        if sys.platform!='win32':raise OSError()
        import ctypes
        from ctypes import wintypes
        user=ctypes.WinDLL('user32',use_last_error=True)
        class Info(ctypes.Structure):
            _fields_=[('cbSize',wintypes.DWORD),('rcMonitor',wintypes.RECT),('rcWork',wintypes.RECT),('dwFlags',wintypes.DWORD)]
        user.MonitorFromWindow.argtypes=[wintypes.HWND,wintypes.DWORD];user.MonitorFromWindow.restype=wintypes.HANDLE
        user.MonitorFromPoint.argtypes=[wintypes.POINT,wintypes.DWORD];user.MonitorFromPoint.restype=wintypes.HANDLE
        user.GetMonitorInfoW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Info)]
        user.GetAncestor.argtypes=[wintypes.HWND,wintypes.UINT];user.GetAncestor.restype=wintypes.HWND
        user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
        user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
        hwnd=user.GetAncestor(window.winfo_id(),2) if window is not None else None
        monitor=user.MonitorFromPoint(wintypes.POINT(*map(int,point)),2) if point else user.MonitorFromWindow(hwnd,2)
        info=Info();info.cbSize=ctypes.sizeof(info)
        if not user.GetMonitorInfoW(monitor,ctypes.byref(info)):raise OSError()
        r=info.rcWork;dpi=96
        if hwnd and hasattr(user,'GetDpiForWindow'):
            user.GetDpiForWindow.argtypes=[wintypes.HWND];user.GetDpiForWindow.restype=wintypes.UINT
            dpi=user.GetDpiForWindow(hwnd) or 96
        frame=(16,64)
        if hwnd:
            outer=wintypes.RECT();client=wintypes.RECT()
            if user.GetWindowRect(hwnd,ctypes.byref(outer)) and user.GetClientRect(hwnd,ctypes.byref(client)):
                fw=(outer.right-outer.left)-(client.right-client.left)
                fh=(outer.bottom-outer.top)-(client.bottom-client.top)
                # An unrealized Tk root reports zero-sized decorations. Keep
                # enough room for the caption and native menu until mapped.
                if 0<=fw<=200 and 24<=fh<=200:frame=(fw,fh)
        return dict(workarea=(r.left,r.top,r.right,r.bottom),dpi=dpi,frame=frame)
    except (OSError,AttributeError,tk.TclError):
        return dict(workarea=(0,0,window.winfo_screenwidth() if window else 1920,
                              window.winfo_screenheight() if window else 1080),dpi=96)


def fitted_geometry(preferences,workarea,frame=(16,40)):
    p=validate_preferences(preferences);left,top,right,bottom=workarea
    available_w=max(1,right-left-frame[0]);available_h=max(1,bottom-top-frame[1])
    if p['window_mode']=='auto':w=min(1440,max(900,int(available_w*.94)));h=min(900,max(600,int(available_h*.94)))
    else:w,h=p['window_width'],p['window_height']
    w=min(w,available_w);h=min(h,available_h)
    x=p['window_x'] if p['window_x'] is not None else left+(available_w-w)//2
    y=p['window_y'] if p['window_y'] is not None else top+(available_h-h)//2
    return w,h,max(left,min(x,right-w-frame[0])),max(top,min(y,bottom-h-frame[1]))


def clamp_inspector(width,total,scale=1):
    minimum=min(240,max(1,total-320));maximum=max(minimum,total-max(320,int(320*min(scale,1.25)))-10)
    return max(minimum,min(int(width),maximum))


class UILayout:
    def __init__(self,app,preferences=None,path=None):
        self.app=app;self.path=path;self.prefs=validate_preferences(preferences or load_preferences(path))
        self.job=None;self.save_job=None;self.suspended=False;self.base_fonts={};self.font_refs={}
        self.panel_selector=None
        if hasattr(app,'inspector_tabs'):
            self.panel_var=tk.StringVar(app)
            self.panel_selector=ttk.Combobox(app.inspector_frame,textvariable=self.panel_var,state='readonly')
            self.panel_selector.bind('<<ComboboxSelected>>',self.select_panel)
            app.inspector_tabs.bind('<<NotebookTabChanged>>',self.sync_panel,add='+')
            ttk.Style(app).layout('HiddenInspector.TNotebook.Tab',[])
        self.app.main_pane.bind('<Configure>',self.schedule,add='+')
        self.app.main_pane.bind('<ButtonRelease-1>',self.sash_released,add='+')
        if hasattr(app,'workspace_pane'):
            app.workspace_pane.bind('<ButtonRelease-1>',self.sash_released,add='+')
            app.workspace_pane.bind('<Configure>',self.schedule,add='+')
        app.bind('<Configure>',self.window_changed,add='+')
        app.bind('<Destroy>',lambda e:self.close(False) if e.widget is app else None,add='+')
        self.apply(self.prefs,geometry=True)
    def schedule(self,event=None):
        if self.suspended:return
        if self.job is not None:self.app.after_cancel(self.job)
        self.job=self.app.after(35,self.reflow)
    def reflow(self):
        self.job=None;app=self.app
        if self.prefs['show_inspector'] and str(app.inspector_frame) in app.main_pane.panes():
            w=clamp_inspector(min(1200,self.prefs['inspector_width']),app.main_pane.winfo_width(),self.prefs['ui_scale'])
            if app.main_pane.sashpos(0)!=w:app.main_pane.sashpos(0,w)
        if hasattr(app,'workspace_pane') and self.prefs['show_ribbon'] and len(app.workspace_pane.panes())>1:
            h=max(64,min(500,self.prefs['ribbon_height'],max(64,int(app.workspace_pane.winfo_height()*.35))))
            if app.canvas.winfo_ismapped():
                # Extra fitting tools use space above the viewport. Reduce
                # the effective ribbon first; keep the user's saved height
                # so it returns when that tray closes.
                available=app.canvas.winfo_height()+app.workspace_pane.sashpos(0)
                h=max(64,min(h,available-240))
            if app.workspace_pane.sashpos(0)!=h:app.workspace_pane.sashpos(0,h)
        for toolbar in getattr(app,'responsive_toolbars',[]):toolbar.schedule()
        if self.panel_selector is not None:
            tabs=app.inspector_tabs.tabs();labels=[app.inspector_tabs.tab(tab,'text') for tab in tabs]
            self.panel_selector.configure(values=labels)
            font=tkfont.Font(root=app,font=ttk.Style(app).lookup('Inspector.TNotebook.Tab','font') or ('Segoe UI',9))
            needed=sum(font.measure(label)+24 for label in labels)
            if needed>app.inspector_frame.winfo_width()-12:
                app.inspector_tabs.configure(style='HiddenInspector.TNotebook')
                if not self.panel_selector.winfo_manager():self.panel_selector.pack(fill='x',padx=5,pady=4,before=app.inspector_tabs)
            else:
                self.panel_selector.pack_forget();app.inspector_tabs.configure(style='Inspector.TNotebook')
            self.sync_panel()
    def select_panel(self,event=None):
        index=self.panel_selector.current();tabs=self.app.inspector_tabs.tabs()
        if 0<=index<len(tabs):self.app.inspector_tabs.select(tabs[index])
    def sync_panel(self,event=None):
        if self.panel_selector is not None:
            selected=self.app.inspector_tabs.select()
            if selected:self.panel_var.set(self.app.inspector_tabs.tab(selected,'text'))
    def sash_released(self,event=None):
        if self.suspended:return
        if self.prefs['show_inspector'] and len(self.app.main_pane.panes())>1:
            self.prefs['inspector_width']=max(240,min(1200,self.app.main_pane.sashpos(0)))
        if self.prefs['show_ribbon'] and hasattr(self.app,'workspace_pane') and len(self.app.workspace_pane.panes())>1:
            self.prefs['ribbon_height']=max(64,min(500,self.app.workspace_pane.sashpos(0)))
        self.schedule();self.schedule_save()
    def window_changed(self,event):
        if event.widget is self.app and not self.suspended:self.schedule_save()
    def schedule_save(self):
        if self.save_job is not None:self.app.after_cancel(self.save_job)
        self.save_job=self.app.after(650,self.persist)
    def preferences(self):return copy.deepcopy(self.prefs)
    def persist(self):
        if self.save_job is not None:
            try:self.app.after_cancel(self.save_job)
            except tk.TclError:pass
        self.save_job=None
        try:
            if self.app.state()=='normal' and self.app.winfo_width()>=900 and self.app.winfo_height()>=600:
                self.prefs.update(window_width=self.app.winfo_width(),window_height=self.app.winfo_height(),
                                  window_x=self.app.winfo_x(),window_y=self.app.winfo_y())
            save_preferences(self.prefs,self.path);return True
        except (OSError,ValueError) as error:
            if hasattr(self.app,'status'):self.app.status.set('Nie zapisano ustawień interfejsu: '+str(error))
            return False
    def close(self,persist=True):
        if persist:self.persist()
        for name in ('job','save_job'):
            job=getattr(self,name)
            if job is not None:
                try:self.app.after_cancel(job)
                except tk.TclError:pass
                setattr(self,name,None)
    def apply(self,preferences,geometry=True):
        p=validate_preferences(preferences);self.suspended=True;self.prefs=p
        try:
            self.set_scale(p['ui_scale'])
            if geometry:
                info=monitor_info(self.app,point=(p['window_x'],p['window_y']) if p['window_x'] is not None and p['window_y'] is not None else None)
                w,h,x,y=fitted_geometry(p,info['workarea'],frame=info.get('frame',(16,64)));self.app.minsize(min(900,w),min(600,h))
                self.app.geometry(f'{w}x{h}+{x}+{y}')
            self.show_inspector(p['show_inspector'],save=False);self.show_ribbon(p['show_ribbon'],save=False)
            self.show_toolbars(p['show_toolbars'],save=False)
            hook=getattr(self.app,'apply_viewport_preferences',None)
            if hook:hook(copy.deepcopy(p))
            # Existing detached and snapshot documents use the same setting;
            # they retain their independent cameras and source projects.
            workspace=getattr(self.app,'workspace',None)
            for entry in tuple(getattr(workspace,'entries',{}).values()):
                document=entry['page']
                redraw=getattr(document,'viewport_preferences_changed',None)
                if redraw:redraw()
        finally:self.suspended=False
        self.schedule();self.schedule_save()
    def set_scale(self,scale):
        style=ttk.Style(self.app)
        style.configure('.',font=('Segoe UI',round(10*scale)))
        for name,size,bold in [('TNotebook.Tab',9,True),('Inspector.TNotebook.Tab',9,True),('Section.TButton',9,True),('Treeview.Heading',10,True)]:
            style.configure(name,font=('Segoe UI',round(size*scale),'bold' if bold else 'normal'))
        style.configure('Treeview',rowheight=round(29*scale));style.configure('TPanedwindow',sashwidth=9)
        style.configure('Sash',sashthickness=9,gripcount=1)
        def visit(widget):
            try:
                specification=widget.cget('font')
                if specification:
                    key=str(widget)
                    if key not in self.base_fonts:self.base_fonts[key]=tkfont.Font(root=self.app,font=specification).actual()
                    actual=dict(self.base_fonts[key]);actual['size']=round(actual['size']*scale)
                    font=tkfont.Font(root=self.app,**actual);self.font_refs[key]=font;widget.configure(font=font)
            except tk.TclError:pass
            for child in widget.winfo_children():visit(child)
        visit(self.app)
    def show_inspector(self,visible,save=True):
        pane=self.app.main_pane;child=self.app.inspector_frame
        present=str(child) in pane.panes()
        if visible and not present:pane.insert(0,child,weight=0)
        elif not visible and present:pane.forget(child)
        self.prefs['show_inspector']=bool(visible)
        if save:self.schedule();self.schedule_save()
    def show_ribbon(self,visible,save=True):
        if hasattr(self.app,'workspace_pane'):
            pane=self.app.workspace_pane;child=self.app.ribbon;present=str(child) in pane.panes()
            if visible and not present:pane.insert(0,child,weight=0)
            elif not visible and present:pane.forget(child)
        self.prefs['show_ribbon']=bool(visible)
        if save:self.schedule();self.schedule_save()
    def show_toolbars(self,visible,save=True):
        for toolbar in (self.app.view_toolbar,self.app.detail_toolbar):
            if visible and not toolbar.winfo_manager():toolbar.pack(fill='x',before=self.app.canvas)
            elif not visible and toolbar.winfo_manager():toolbar.pack_forget()
        self.prefs['show_toolbars']=bool(visible)
        if save:self.schedule();self.schedule_save()
    def fit_view(self):
        self.app.workspace.select(self.app.design_page);self.app.fit()
    def restore_layout(self):
        p=self.preferences();p.update(inspector_width=330,ribbon_height=110,show_inspector=True,show_ribbon=True,show_toolbars=True)
        self.apply(p,geometry=False)


def install(app,preferences=None,path=None):
    app.ui_layout=UILayout(app,preferences,path)
    from ui_settings import open_settings
    app.interface_settings=lambda:open_settings(app)
    return app.ui_layout


# Verified C++ calculation ports; UI and exact CAD validators are retained.
from native_model import install_services as _install_cpp
_install_cpp(globals(), 'responsive_ui')
