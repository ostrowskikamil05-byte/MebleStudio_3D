"""Tear-off Tk documents. The original frame and its state survive every move.

Tk's ``wm manage``/``wm forget`` support classic Frame widgets. Documents are
children of the application, so the same frame can also be managed by Notebook.
"""
import tkinter as tk
from tkinter import ttk, messagebox
import copy
import dataclasses
import sys


class WindowsDockBridge:
    """Out-of-context observation of native title moves; no Tcl in callbacks.

    WinEvents avoid subclassing Tk's window procedure, whose callbacks require
    _tkinter's own thread state. Geometry changes therefore go through Tcl wm,
    including the absolute "+-120" syntax on left-hand monitors.
    """
    def __init__(self):
        import ctypes
        import os
        from ctypes import wintypes
        self.c, self.w = ctypes, wintypes
        self.api = ctypes.WinDLL('user32', use_last_error=True)
        self.hooks, self.pending = {}, []
        self.proc_type = ctypes.WINFUNCTYPE(None, wintypes.HANDLE, wintypes.DWORD,
            wintypes.HWND, wintypes.LONG, wintypes.LONG, wintypes.DWORD, wintypes.DWORD)
        self.api.GetAncestor.argtypes = (wintypes.HWND, wintypes.UINT)
        self.api.GetAncestor.restype = wintypes.HWND
        self.api.GetCursorPos.argtypes = (ctypes.POINTER(wintypes.POINT),)
        self.api.GetAsyncKeyState.argtypes = (ctypes.c_int,)
        self.api.GetAsyncKeyState.restype = wintypes.SHORT
        self.api.GetWindowRect.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.RECT))
        self.api.GetClientRect.argtypes = self.api.GetWindowRect.argtypes
        self.api.ScreenToClient.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.POINT))
        self.api.MonitorFromPoint.argtypes = (wintypes.POINT, wintypes.DWORD)
        self.api.MonitorFromPoint.restype = wintypes.HANDLE
        self.api.SetWinEventHook.argtypes = (wintypes.DWORD, wintypes.DWORD, wintypes.HMODULE,
                                            self.proc_type, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD)
        self.api.SetWinEventHook.restype = wintypes.HANDLE
        self.api.UnhookWinEvent.argtypes = (wintypes.HANDLE,)
        class MonitorInfo(ctypes.Structure):
            _fields_ = [('cbSize', wintypes.DWORD), ('rcMonitor', wintypes.RECT),
                        ('rcWork', wintypes.RECT), ('dwFlags', wintypes.DWORD)]
        self.monitor_info = MonitorInfo
        self.api.GetMonitorInfoW.argtypes = (wintypes.HANDLE, ctypes.POINTER(MonitorInfo))
        self.dpi_api = getattr(self.api, 'GetDpiForWindow', None)
        if self.dpi_api:
            self.dpi_api.argtypes = (wintypes.HWND,)
            self.dpi_api.restype = wintypes.UINT
        def on_event(hook, event, hwnd, object_id, child_id, thread, timestamp):
            # Only Win32 reads and plain Python queue writes. No Tcl here.
            try:
                self.record_event(event, hwnd)
            except Exception:
                pass
        self.callback = self.proc_type(on_event)  # Strong reference until shutdown.
        self.event_hook = self.api.SetWinEventHook(0x000A, 0x000B, None, self.callback,
                                                   os.getpid(), 0, 0)  # OUTOFCONTEXT

    def hwnd(self, page):
        return self.api.GetAncestor(page.winfo_id(), 2)  # GA_ROOT: decorated wrapper

    def rect(self, hwnd):
        result = self.w.RECT()
        if not self.api.GetWindowRect(hwnd, self.c.byref(result)):
            return None
        return result.left, result.top, result.right, result.bottom

    def dpi(self, hwnd):
        return (self.dpi_api(hwnd) or 96) if self.dpi_api else 96

    def cursor(self):
        point = self.w.POINT()
        if self.api.GetCursorPos(self.c.byref(point)):
            return point.x, point.y
        return None

    def left_button_down(self):
        # A native window transition can lose Tk's ButtonRelease event. Read
        # only the high bit: the low bit reports a historical press instead.
        return bool(self.api.GetAsyncKeyState(0x01) & 0x8000)

    def workarea(self, x, y):
        monitor = self.api.MonitorFromPoint(self.w.POINT(int(x), int(y)), 2)
        info = self.monitor_info()
        info.cbSize = self.c.sizeof(info)
        if self.api.GetMonitorInfoW(monitor, self.c.byref(info)):
            r = info.rcWork
            return r.left, r.top, r.right, r.bottom
        return None

    def move(self, page, x, y, *, fit=False):
        rect = self.rect(self.hwnd(page))
        if rect is None:
            return
        width, height = rect[2]-rect[0], rect[3]-rect[1]
        work = self.workarea(x, y)
        if work and fit:
            left, top, right, bottom = work
            frame_x, frame_y = width-page.winfo_width(), height-page.winfo_height()
            width, height = min(width, right-left), min(height, bottom-top)
            x, y = max(left, min(x, right-width)), max(top, min(y, bottom-height))
            geometry = '%dx%d+%d+%d' % (max(1,width-frame_x), max(1,height-frame_y), int(x), int(y))
        else:
            geometry = '+%d+%d' % (int(x), int(y))
        # Do not resize Tk windows directly through ctypes: Configure callbacks
        # must run inside _tkinter's Tcl entry rather than a foreign Win32 call.
        page.tk.call('wm', 'geometry', str(page), geometry)

    def install(self, page):
        if not self.event_hook:
            return False
        self.hooks[str(page)] = dict(hwnd=self.hwnd(page), start=None, start_dpi=None)
        return True

    def record_event(self, event, hwnd):
        matches = [(key,state) for key,state in self.hooks.items() if state['hwnd']==hwnd]
        for key,state in matches:
            if event == 0x000A:  # EVENT_SYSTEM_MOVESIZESTART
                state['start'], state['start_dpi'] = self.rect(hwnd), self.dpi(hwnd)
            elif event == 0x000B:  # EVENT_SYSTEM_MOVESIZEEND
                start, end = state.pop('start', None), self.rect(hwnd)
                dpi_changed = state.pop('start_dpi', None) != self.dpi(hwnd)
                same_size = start and end and (start[2]-start[0],start[3]-start[1]) == (end[2]-end[0],end[3]-end[1])
                if start and end and start[:2] != end[:2] and (same_size or dpi_changed):
                    point = self.cursor()
                    if point:
                        self.pending.append(('drop', key, point))

    def remove(self, key):
        self.hooks.pop(key, None)

    def close(self):
        if self.event_hook:
            self.api.UnhookWinEvent(self.event_hook)
            self.event_hook = None
        self.hooks.clear()
        self.pending.clear()

    def notebook_point(self, notebook, x, y):
        point, rect = self.w.POINT(int(x), int(y)), self.w.RECT()
        hwnd = notebook.winfo_id()
        if not self.api.ScreenToClient(hwnd, self.c.byref(point)):
            return None
        if not self.api.GetClientRect(hwnd, self.c.byref(rect)):
            return None
        if rect.right <= 0 or rect.bottom <= 0:
            return None
        return (notebook.winfo_rootx()+point.x*notebook.winfo_width()/rect.right,
                notebook.winfo_rooty()+point.y*notebook.winfo_height()/rect.bottom)


class DockWorkspace:
    def __init__(self, app, notebook):
        self.app, self.notebook = app, notebook
        self.entries = {}
        self.drag = None
        self._drag_grab = None
        self._closed = False
        self.native = WindowsDockBridge() if sys.platform == 'win32' else None
        self._poll_job = app.after(45, self._poll_native)
        app.bind('<Destroy>', self._app_destroyed, add='+')
        notebook.bind('<ButtonPress-1>', self._tab_press, add='+')
        notebook.bind('<B1-Motion>', self._drag_motion, add='+')
        notebook.bind('<ButtonRelease-1>', self._drag_release, add='+')
        notebook.bind('<Double-Button-1>', self._tab_double, add='+')
        app.bind('<ButtonRelease-1>', self._drag_release, add='+')
        app.bind('<Escape>', self._cancel_drag, add='+')
        app.workspace = self

    def _app_destroyed(self, event):
        if event.widget == self.app:
            self.cleanup()

    def cleanup(self):
        self._closed = True
        self._release_grab()
        if self._poll_job:
            try:
                self.app.after_cancel(self._poll_job)
            except tk.TclError:
                pass
            self._poll_job = None
        if self.native:
            for key in tuple(self.native.hooks):
                self.native.remove(key)
            self.native.close()

    def _poll_native(self):
        self._poll_job = None
        if self._closed:
            return
        try:
            if self.native:
                if self.drag and not self.native.left_button_down():
                    self._release_grab()
                pending, self.native.pending = self.native.pending, []
                for kind, key, point in pending:
                    if kind == 'destroy':
                        self.native.remove(key)
                        continue
                    entry = self.entries.get(key)
                    if not entry or not entry['detached']:
                        continue
                    try:
                        mapped = self.native.notebook_point(self.notebook, *point)
                        if mapped and self.over_tab_bar(*mapped):
                            # Deferred until Tcl is back in its normal event loop.
                            self.dock(entry['page'], self._drop_index(*mapped))
                    except tk.TclError:
                        # A queued native event can outlive its document.
                        self.native.remove(key)
        finally:
            if not self._closed and self.app.winfo_exists():
                self._poll_job = self.app.after(45, self._poll_native)

    def confirm_project_change(self):
        documents = [e['page'] for e in self.entries.values() if e['allow_close']]
        changed = [d for d in documents if hasattr(d, 'has_changes') and d.has_changes()]
        if not changed:
            return True
        labels = '\n'.join('• '+self.entries[str(d)]['title'] for d in changed)
        return messagebox.askyesno('Niezapisane dokumenty',
            'Otwarte dokumenty mają niezapisane zmiany:\n'+labels+
            '\n\nOdrzucić te zmiany i kontynuować? Aby je zachować, anuluj i zapisz w dokumentach.',
            parent=self.app)

    def close_project_documents(self):
        # Confirmation happens once, before any destructive transition.
        for entry in tuple(self.entries.values()):
            if entry['allow_close']:
                entry['page'].destroy()
        self._release_grab()

    def add_existing(self, page, title, *, allow_close=False):
        """Register a tk.Frame, including one already displayed in Notebook."""
        if page.winfo_class() not in ('Frame', 'Labelframe'):
            raise ValueError('Odłączana zakładka wymaga tk.Frame jako ramki głównej.')
        if page.master != self.app:
            raise ValueError('Ramka dokumentu musi mieć aplikację jako bezpośredniego rodzica.')
        key = str(page)
        if key in self.entries:
            return page
        entry = dict(page=page, title=str(title), detached=False,
                     geometry='1100x700+60+60', minsize=(640, 420),
                     close=page.destroy if allow_close else lambda: self.dock(page),
                     allow_close=allow_close)
        self.entries[key] = entry
        header = ttk.Frame(page, padding=(8, 3))
        packed = page.pack_slaves()
        header.pack(side='top', fill='x', **({'before': packed[0]} if packed else {}))
        name = tk.StringVar(master=page, value='⠿  ' + title)
        grip = ttk.Label(header, textvariable=name, cursor='fleur')
        grip.pack(side='left', fill='x', expand=True)
        toggle = ttk.Button(header, text='Odłącz', width=10,
                            command=lambda: self.toggle(page))
        toggle.pack(side='right', padx=3)
        if allow_close:
            ttk.Button(header, text='×', width=3, command=lambda: self.close(page)).pack(side='right')
        entry.update(header=header, label=name, toggle=toggle, grip=grip)
        for widget in (header, grip):
            widget.bind('<ButtonPress-1>', lambda event, p=page: self._header_press(p, event))
            widget.bind('<B1-Motion>', self._drag_motion)
            widget.bind('<ButtonRelease-1>', self._drag_release)
        page.bind('<Destroy>', lambda event, p=page: self._removed(p, event), add='+')
        if key not in notebook_tabs(self.notebook):
            self.notebook.add(page, text=title)
        self.notebook.select(page)
        self.app.after_idle(lambda: self._install_document_tag(page))
        return page

    def _install_document_tag(self, page):
        if not page.winfo_exists():
            return
        def visit(widget):
            for child in widget.winfo_children():
                tags = list(child.bindtags())
                if str(page) not in tags:
                    root = str(self.app)
                    index = tags.index(root) if root in tags else len(tags)
                    tags.insert(index, str(page))
                if str(self.app) not in tags:
                    tags.append(str(self.app))
                child.bindtags(tuple(tags))
                visit(child)
        visit(page)

    def bind_app_shortcut(self, sequence, command):
        def run(event):
            command()
            return 'break'
        self.app.bind(sequence, run)
        for entry in self.entries.values():
            if not entry['allow_close']:
                entry['page'].bind(sequence, run)

    def _removed(self, page, event):
        if str(event.widget) == str(page):
            if self.drag and self.drag['page'] == page:
                self._release_grab()
            if self.native:
                self.native.remove(str(page))
            self.entries.pop(str(page), None)

    def title(self, page, value):
        entry = self.entries[str(page)]
        entry['title'] = str(value)
        entry['label'].set('⠿  ' + str(value))
        if entry['detached']:
            self.app.tk.call('wm', 'title', str(page), value)
        elif str(page) in notebook_tabs(self.notebook):
            self.notebook.tab(page, text=value)

    def select(self, page):
        entry = self.entries[str(page)]
        if entry['detached']:
            self.app.tk.call('wm', 'deiconify', str(page))
            page.lift()
        else:
            self.notebook.select(page)

    def detached(self, page):
        return bool(self.entries[str(page)]['detached'])

    def detach(self, page, *, x=None, y=None):
        self._release_grab()
        self._detach_page(page, x=x, y=y)

    def _detach_page(self, page, *, x=None, y=None):
        entry = self.entries[str(page)]
        if entry['detached']:
            self.select(page)
            return
        entry['index'] = list(notebook_tabs(self.notebook)).index(str(page))
        self.notebook.forget(page)
        self.app.tk.call('wm', 'manage', str(page))
        entry['detached'] = True
        self.app.tk.call('wm', 'title', str(page), entry['title'])
        self.app.tk.call('wm', 'geometry', str(page), entry['geometry'])
        self.app.tk.call('wm', 'minsize', str(page), *entry['minsize'])
        callback = entry.get('wm_callback')
        if not callback:
            callback = page.register(lambda: self.close(page))
            entry['wm_callback'] = callback
        self.app.tk.call('wm', 'protocol', str(page), 'WM_DELETE_WINDOW', callback)
        entry['toggle'].configure(text='Przypnij')
        self.app.tk.call('wm', 'deiconify', str(page))
        page.update_idletasks()
        if self.native:
            point = (x, y) if x is not None and y is not None else self.native.cursor()
            if point:
                work = self.native.workarea(*point)
                if work:
                    self.app.tk.call('wm', 'minsize', str(page),
                                     min(entry['minsize'][0], work[2]-work[0]-20),
                                     min(entry['minsize'][1], work[3]-work[1]-60))
                current = self.native.rect(self.native.hwnd(page))
                if current:
                    wx, wy = (int(x)-85, int(y)-12) if x is not None and y is not None else current[:2]
                    self.native.move(page, wx, wy, fit=True)
            entry['native_title_drag'] = self.native.install(page)
        else:
            self._move_window(page, x, y)
        page.event_generate('<<DocumentDetached>>')

    def dock(self, page, index=None):
        self._release_grab()
        entry = self.entries[str(page)]
        if entry['detached']:
            entry['geometry'] = self.app.tk.call('wm', 'geometry', str(page))
            if self.native:
                self.native.remove(str(page))
            self.app.tk.call('wm', 'forget', str(page))
            entry['detached'] = False
            wanted = entry.get('index', 'end') if index is None else index
            count = len(notebook_tabs(self.notebook))
            if isinstance(wanted, int) and wanted >= count:
                wanted = 'end'
            self.notebook.insert(wanted, page, text=entry['title'])
            entry['toggle'].configure(text='Odłącz')
            page.event_generate('<<DocumentDocked>>')
        self.notebook.select(page)

    def toggle(self, page):
        if self.detached(page):
            self.dock(page)
        else:
            self.detach(page)

    def close(self, page):
        self._release_grab()
        entry = self.entries.get(str(page))
        if entry:
            entry['close']()

    def _bar_bounds(self):
        n = self.notebook
        height = 42
        selected = n.select()
        if selected:
            try:
                active = self.app.nametowidget(selected)
                if active.winfo_ismapped():
                    height = max(28, active.winfo_rooty() - n.winfo_rooty())
            except tk.TclError:
                pass
        return n.winfo_rootx(), n.winfo_rooty(), n.winfo_width(), height

    def over_tab_bar(self, x, y):
        if not self.notebook.winfo_ismapped():
            return False
        bx, by, bw, bh = self._bar_bounds()
        return bx <= x <= bx+bw and by <= y <= by+bh

    def _tab_at(self, event):
        if not self.over_tab_bar(event.x_root, event.y_root):
            return None
        try:
            key = notebook_tabs(self.notebook)[self.notebook.index('@%d,%d' % (event.x, event.y))]
            return self.entries.get(key, {}).get('page')
        except (tk.TclError, IndexError):
            return None

    def _start(self, page, event):
        self._release_grab()
        # Never steal a modal dialog's grab. A tab click itself needs no grab:
        # reserve it for an actual movement beyond the drag threshold.
        if self.app.grab_current() is not None:
            return False
        self.drag = dict(page=page, x=event.x_root, y=event.y_root,
                         widget=event.widget, moved=False)
        return True

    def _tab_press(self, event):
        page = self._tab_at(event)
        if page:
            self.notebook.select(page)
            self._start(page, event)

    def _tab_double(self, event):
        page = self._tab_at(event)
        if page:
            self._release_grab()
            self.detach(page)
            return 'break'

    def _header_press(self, page, event):
        if self._start(page, event):
            return 'break'

    def _move_window(self, page, x, y):
        if x is not None and y is not None:
            if self.native:
                self.native.move(page, int(x)-85, int(y)-12)
            else:
                # Tk minus offsets mean distance from the right/bottom edge.
                self.app.tk.call('wm', 'geometry', str(page), '+%d+%d' % (int(x)-85, int(y)-12))

    def _drag_motion(self, event):
        if not self.drag:
            return
        d = self.drag
        if abs(event.x_root-d['x']) + abs(event.y_root-d['y']) < 14 and not d['moved']:
            return
        d['moved'] = True
        page = d['page']
        if str(page) not in self.entries:
            self._release_grab()
            return 'break'
        if not self.detached(page) and not self.over_tab_bar(event.x_root, event.y_root):
            # A Notebook grab belongs to the old Toplevel. Release it before
            # wm manage, then capture on the new window's visible drag handle.
            self._release_owned_grab()
            self._detach_page(page, x=event.x_root, y=event.y_root)
            entry = self.entries.get(str(page))
            if entry is None:
                self._release_grab()
                return 'break'
            d['widget'] = entry['grip']
        if self.drag is not d:
            return 'break'
        if not self._acquire_drag_grab(d['widget']):
            self._release_grab()
            return 'break'
        if self.detached(page):
            self._move_window(page, event.x_root, event.y_root)
        return 'break'

    def _acquire_drag_grab(self, widget):
        if self._drag_grab is widget and self.app.grab_current() is widget:
            return True
        self._release_owned_grab()
        if self.app.grab_current() is not None:
            return False
        try:
            # Use Tcl directly: DockDocument.grab_set is deliberately a no-op
            # for non-modal editing documents, while drag capture is temporary.
            self.app.tk.call('grab', 'set', str(widget))
            self._drag_grab = widget
            return True
        except tk.TclError:
            return False

    def _release_owned_grab(self):
        widget, self._drag_grab = self._drag_grab, None
        if widget is not None:
            try:
                if str(self.app.tk.call('grab', 'current', str(self.app))) == str(widget):
                    self.app.tk.call('grab', 'release', str(widget))
            except tk.TclError:
                pass

    def _release_grab(self):
        self._release_owned_grab()
        self.drag = None

    def _cancel_drag(self, event=None):
        if self.drag:
            self._release_grab()
            return 'break'

    def _drag_release(self, event):
        if not self.drag:
            return
        d = self.drag
        self._release_grab()
        if str(d['page']) not in self.entries:
            return 'break'
        if d['moved'] and self.over_tab_bar(event.x_root, event.y_root):
            # A drop onto a particular tab also changes the document order.
            index = self._drop_index(event.x_root, event.y_root)
            if self.detached(d['page']):
                self.dock(d['page'], index)
            else:
                self.notebook.insert(index, d['page'])
        return 'break'

    def _drop_index(self, x, y):
        bx, by, *_ = self._bar_bounds()
        try:
            return self.notebook.index('@%d,%d' % (x-bx, y-by))
        except tk.TclError:
            return 'end'


def notebook_tabs(notebook):
    return tuple(str(path) for path in notebook.tabs())


class DockDocument(tk.Frame):
    """Toplevel-compatible editing document, initially displayed as a tab."""
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        # A stale reference must not commit data into a replacement project.
        # Actual document destruction and timer cancellation remain primary.
        for name in ('save', 'apply'):
            method = cls.__dict__.get(name)
            if method is None:
                continue
            def protected(self, *args, _method=method, **kwargs):
                if not self.ensure_current_project():
                    return False
                return _method(self, *args, **kwargs)
            protected.__name__ = name
            setattr(cls, name, protected)

    def __init__(self, app, **kwargs):
        super().__init__(app, **kwargs)
        self.app = app
        self.project_generation = getattr(app, 'project_generation', 0)
        self._doc_after_jobs = set()
        self._doc_title = 'Dokument'
        self._doc_baseline = None
        workspace = getattr(app, 'workspace', None)
        if workspace is None:
            if not hasattr(app, 'tabs'):
                app.tabs = ttk.Notebook(app)
                app.tabs.pack(fill='both', expand=True)
            workspace = DockWorkspace(app, app.tabs)
        self.workspace = workspace
        workspace.add_existing(self, self._doc_title, allow_close=True)
        workspace.entries[str(self)]['close'] = self.request_close
        self.after_idle(self._document_bindtags)
        self.after_idle(lambda: self.mark_clean() if self._doc_baseline is None else None)

    def ensure_current_project(self, *, raise_error=False):
        current = self.project_generation == getattr(self.app, 'project_generation', 0)
        if current:
            return True
        text = 'Ten dokument pochodzi z poprzedniego projektu. Otwórz edytor ponownie w bieżącym projekcie.'
        if raise_error:
            raise ValueError(text)
        messagebox.showerror('Nieaktualny dokument', text, parent=self.app)
        return False

    def guard(self, callback):
        """Protect a dynamically-created button/menu commit closure."""
        def run(*args, **kwargs):
            self.ensure_current_project(raise_error=True)
            return callback(*args, **kwargs)
        return run

    def _document_state(self):
        """Draft models and typed control values, including uncommitted fields."""
        controls = {}
        def visit(widget):
            for child in widget.winfo_children():
                if child.winfo_class() in ('Entry', 'TEntry', 'TCombobox', 'Spinbox', 'TSpinbox'):
                    controls[str(child)] = child.get()
                elif child.winfo_class() in ('Checkbutton', 'TCheckbutton', 'Radiobutton', 'TRadiobutton'):
                    variable = child.cget('variable')
                    if variable:
                        controls[str(variable)] = child.getvar(variable)
                visit(child)
        visit(self)
        model = {}
        for name in ('part', 'candidate', 'values', 'config'):
            value = self.__dict__.get(name)
            if dataclasses.is_dataclass(value):
                model[name] = dataclasses.asdict(value)
            elif isinstance(value, (dict, list, tuple)):
                model[name] = copy.deepcopy(value)
        return controls, model

    def mark_clean(self):
        if self.winfo_exists():
            self._doc_baseline = self._document_state()

    def has_changes(self):
        return self._doc_baseline is not None and self._document_state() != self._doc_baseline

    def request_close(self):
        if self.has_changes() and not messagebox.askyesno('Niezapisany dokument',
                'Odrzucić niezapisane zmiany tego dokumentu?', parent=self.winfo_toplevel()):
            return False
        self.destroy()
        return True

    def _document_bindtags(self):
        # Docked child widgets ordinarily dispatch shortcuts to root. Include
        # the document frame first, so its own editing shortcut can consume it.
        def visit(widget):
            for child in widget.winfo_children():
                tags = list(child.bindtags())
                if str(self) not in tags:
                    root_tag = str(self.app)
                    index = tags.index(root_tag) if root_tag in tags else len(tags)
                    tags.insert(index, str(self))
                    child.bindtags(tuple(tags))
                visit(child)
        if self.winfo_exists():
            visit(self)

    def title(self, value=None):
        if value is None:
            return self._doc_title
        self._doc_title = str(value)
        self.workspace.title(self, value)

    def geometry(self, value=None):
        entry = self.workspace.entries[str(self)]
        if value is None:
            return entry['geometry']
        entry['geometry'] = str(value)
        if entry['detached']:
            self.tk.call('wm', 'geometry', str(self), value)

    def minsize(self, width=None, height=None):
        entry = self.workspace.entries[str(self)]
        if width is None:
            return entry['minsize']
        entry['minsize'] = (width, height)
        if entry['detached']:
            self.tk.call('wm', 'minsize', str(self), width, height)

    def protocol(self, name=None, func=None):
        if name == 'WM_DELETE_WINDOW' and func is not None:
            self.workspace.entries[str(self)]['close'] = func

    wm_protocol = protocol

    def transient(self, master=None):
        # Editing tabs and detached documents stay non-modal.
        return ''

    def grab_set(self):
        return None

    def grab_release(self):
        return None

    def after(self, ms, func=None, *args):
        if func is None:
            return super().after(ms)
        job = None
        def run():
            self._doc_after_jobs.discard(job)
            return func(*args)
        job = super().after(ms, run)
        self._doc_after_jobs.add(job)
        return job

    def after_cancel(self, job):
        self._doc_after_jobs.discard(job)
        return super().after_cancel(job)

    def destroy(self):
        # Tk deletes registered commands on destruction but leaves pending
        # timer scripts in its interpreter unless explicitly cancelled.
        for job in tuple(self._doc_after_jobs):
            try:
                self.after_cancel(job)
            except tk.TclError:
                pass
        super().destroy()


class VisualizationDocument(DockDocument):
    """Independent camera; live project or a fixed sketch assembly snapshot."""
    def __init__(self, app, project=None, selected=None, title='Wizualizacja 3D', note=''):
        from core import Camera
        super().__init__(app)
        self.title(title)
        self.geometry('1100x760+60+60')
        self.snapshot_project, self.selected = project, selected
        self.camera = Camera()
        self.explode = tk.BooleanVar(master=self, value=False)
        self.dimensions = tk.BooleanVar(master=self, value=True)
        self.xray = tk.BooleanVar(master=self, value=False)
        self.need_fit = True
        toolbar = ttk.Frame(self, padding=6)
        toolbar.pack(fill='x')
        for label, angles in [('3D', (-38, 24)), ('Przód', (0, 0)), ('Bok', (-90, 0)), ('Góra', (0, 90))]:
            ttk.Button(toolbar, text=label, command=lambda a=angles: self.view(*a)).pack(side='left', padx=3)
        ttk.Button(toolbar, text='Dopasuj', command=self.fit).pack(side='left', padx=3)
        ttk.Checkbutton(toolbar, text='Rozstrzelony', variable=self.explode, command=self.draw).pack(side='left', padx=8)
        ttk.Checkbutton(toolbar, text='Wymiary', variable=self.dimensions, command=self.draw).pack(side='left')
        ttk.Checkbutton(toolbar, text='X-ray', variable=self.xray, command=self.draw).pack(side='left')
        if note:
            ttk.Label(self, text=note, padding=8, wraplength=1100).pack(fill='x')
        self.canvas = tk.Canvas(self, background='#eaf0f4', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)
        self.canvas.bind('<Configure>', lambda e: self.draw())
        self.canvas.bind('<ButtonPress-1>', self._press)
        self.canvas.bind('<B1-Motion>', self._orbit)
        self.canvas.bind('<ButtonPress-3>', self._press)
        self.canvas.bind('<B3-Motion>', self._pan)
        self.canvas.bind('<MouseWheel>', self._zoom)
        self.canvas.bind('<ButtonPress-2>', self._press)
        self.canvas.bind('<B2-Motion>', self._orbit)
        self.canvas.bind('<ButtonRelease-1>', self._finish_view)
        self.canvas.bind('<ButtonRelease-2>', self._finish_view)
        self.canvas.bind('<ButtonRelease-3>', self._finish_view)
        self._interactive = False
        ttk.Label(self, text='LPM: obrót · PPM: przesuwanie · Rolka: przybliżenie. Kamera tej zakładki jest niezależna.', padding=7).pack(fill='x')
        self._refresh_job = self.after(400, self._live_refresh)

    def viewport_preferences_changed(self):
        """All views share the engine preference, retaining independent cameras."""
        if self.winfo_exists():
            from native_viewport import reset_native_failure
            reset_native_failure(self.canvas);self.draw()

    def project(self):
        if self.snapshot_project is not None:
            return self.snapshot_project
        return self.app.display_project() if hasattr(self.app, 'display_project') else self.app.project

    def has_changes(self):
        # Camera/visibility settings are presentation state, not a model draft.
        return False

    def view(self, yaw, pitch):
        self.camera.yaw, self.camera.pitch = yaw, pitch
        self.fit()

    def fit(self):
        self.need_fit = True
        self.draw()

    def _live_refresh(self):
        if not self.winfo_exists():
            return
        if self.winfo_ismapped():
            key=(id(self.project()),getattr(self.app,'viewport_revision',0),tuple(sorted(getattr(self.app,'open_amounts',{}).items())),
                 getattr(self.app,'viewport_engine','cpu'),getattr(self.app,'viewport_quality','balanced'))
            if key!=getattr(self,'_live_key',None):self._live_key=key;self.draw()
        self._refresh_job = self.after(400, self._live_refresh)

    def _press(self, event):
        self.anchor = (event.x, event.y)

    def _orbit(self, event):
        self._interactive = True
        self.camera.yaw += (event.x-self.anchor[0])*.5
        self.camera.pitch = max(-89, min(89, self.camera.pitch+(event.y-self.anchor[1])*.4))
        self.anchor = (event.x, event.y)
        self.draw()

    def _pan(self, event):
        self._interactive = True
        self.camera.pan = (self.camera.pan[0]+event.x-self.anchor[0], self.camera.pan[1]+event.y-self.anchor[1])
        self.anchor = (event.x, event.y)
        self.draw()

    def _zoom(self, event):
        self.camera.scale = min(40, max(.01, self.camera.scale*(1.12 if event.delta>0 else 1/1.12)))
        self.draw()

    def _finish_view(self,event=None):
        self._interactive=False;self.draw()

    def draw(self,*,force_cpu=False):
        from core import render_faces, world, explosion_shift
        if not hasattr(self, 'canvas'):
            return
        c = self.canvas
        c.delete('all')
        w, h = c.winfo_width(), c.winfo_height()
        if w < 10 or h < 10:
            return
        project = self.project()
        if self.need_fit:
            self.camera.fit(project, w, h)
            self.need_fit = False
        selected = self.selected if self.snapshot_project is not None else getattr(self.app, 'selected', None)
        factor = .22 if self.explode.get() else 0
        from viewport_scene import paint_background,paint_faces
        interactive=getattr(self,'_interactive',False)
        paint_background(c,self.camera,project,(w,h),interactive=interactive)
        from viewport_modes import xray_part_ids
        from native_viewport import paint_native,close_native
        engine='cpu' if force_cpu else getattr(self.app,'viewport_engine','cpu')
        quality=getattr(self.app,'viewport_quality','balanced')
        scope=getattr(self.app,'xray_scope',None)
        xray_ids=xray_part_ids(project,self.xray.get(),scope.get() if scope is not None else 'Cały zestaw',selected)
        result=None
        if engine in ('auto','d3d11','d3d12','vulkan'):
            result=paint_native(c,project,camera=self.camera,size=(w,h),interactive=interactive,
                selected=selected,selected_ids=(),explode=factor,xray=self.xray.get(),
                xray_ids=xray_ids,quality=quality,backend=engine)
            if result.get('ok'):
                from gpu_viewport import close_gpu
                close_gpu(c)
        else:close_native(c)
        if engine in ('auto','gpu') and not (result and result.get('ok')):
            try:
                from gpu_viewport import paint_gpu
                result=paint_gpu(c,project=project,camera=self.camera,size=(w,h),interactive=interactive,
                    selected=selected,selected_ids=(),explode=factor,xray=self.xray.get(),xray_ids=xray_ids,quality=quality)
            except (ImportError,RuntimeError,OSError,ValueError) as error:
                result=dict(ok=False,error=str(error))
        if result and result.get('ok'):
            rendered=result['drawn']
            if result.get('engine')!='native':c._active_renderer='gpu'
        else:
            from gpu_viewport import close_gpu
            close_gpu(c);close_native(c);c._active_renderer='cpu'
            rendered=render_faces(project,self.camera,w,h,selected,factor,interactive=interactive)
            paint_faces(c,rendered,project,camera=self.camera,size=(w,h),xray=self.xray.get(),explode=factor,interactive=interactive,xray_ids=xray_ids)
            error=(result or {}).get('error')
            if error and error!=getattr(self,'_viewport_warning',None):
                self._viewport_warning=error
                if hasattr(self.app,'log'):self.app.log.warning('Wizualizacja 3D: %s; podgląd CPU.',error)
        self.drawn=rendered
        if self.dimensions.get():
            targets = [p for p in project.parts if p.visible and (self.explode.get() or p.id == selected)]
            for part in targets:
                shift = explosion_shift(part, self.camera, factor)
                def position(point):
                    xyz = world(part, *point)
                    return self.camera.project(tuple(xyz[i]+shift[i] for i in range(3)), w, h)
                if self.explode.get():
                    x, y, _ = position((part.u/2, part.v/2, part.t))
                    c.create_text(x, y-12, text=f'{part.name}\n{part.u:g} × {part.v:g} × {part.t:g} mm', fill='#123c49', font=('Segoe UI', 9, 'bold'), tags='dimensions')
                else:
                    for a, b, text in [((0,0,0),(part.u,0,0),f'U {part.u:g}'),((part.u,0,0),(part.u,part.v,0),f'V {part.v:g}'),((part.u,part.v,0),(part.u,part.v,part.t),f'T {part.t:g}')]:
                        qa, qb = position(a), position(b)
                        c.create_line(*qa[:2], *qb[:2], fill='#176a78', width=2, tags='dimensions')
                        c.create_text((qa[0]+qb[0])/2,(qa[1]+qb[1])/2-12,text=text+' mm',fill='#123c49',font=('Segoe UI',9,'bold'),tags='dimensions')
        if c._active_renderer=='native':
            from native_viewport import flush_native
            try:flush_native(c)
            except (RuntimeError,OSError,ValueError,tk.TclError) as error:
                if hasattr(self.app,'log'):self.app.log.warning('Nakładka wizualizacji C++: %s; podgląd CPU.',error)
                self.draw(force_cpu=True)

    def destroy(self):
        if hasattr(self,'canvas'):
            from native_viewport import close_native
            from gpu_viewport import close_gpu
            close_native(self.canvas);close_gpu(self.canvas)
        if hasattr(self, '_refresh_job'):
            try:
                self.after_cancel(self._refresh_job)
            except tk.TclError:
                pass
        super().destroy()
