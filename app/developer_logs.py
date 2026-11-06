"""Bounded diagnostics, persisted outside the installation, with a dockable viewer."""
from collections import deque
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import sys
import threading
import weakref
import tkinter as tk
from tkinter import ttk, filedialog
from dock_workspace import DockDocument

_apps=weakref.WeakSet()
_message_wrapped=False


def _observe_messages():
    global _message_wrapped
    if _message_wrapped:return
    from tkinter import messagebox
    for name,level in [('showerror',logging.ERROR),('showwarning',logging.WARNING)]:
        original=getattr(messagebox,name)
        def observed(title=None,message=None,_original=original,_level=level,**kwargs):
            for app in list(_apps):app.log.log(_level,'%s: %s',title,message)
            return _original(title,message,**kwargs)
        setattr(messagebox,name,observed)
    _message_wrapped=True


class MemoryHandler(logging.Handler):
    def __init__(self):
        super().__init__();self.records=deque(maxlen=1500);self.sequence=0
        self.setFormatter(logging.Formatter('%(asctime)s  %(levelname)s  %(message)s',datefmt='%H:%M:%S'))

    def emit(self,record):
        self.sequence+=1;self.records.append((self.sequence,record.levelno,self.format(record)))


class LogDocument(DockDocument):
    def __init__(self,app):
        super().__init__(app);self.title('Logi developerskie');self.cursor=0
        bar=ttk.Frame(self,padding=6);bar.pack(fill='x')
        ttk.Label(bar,text='Zielony: OK / informacje   Żółty: ostrzeżenie   Czerwony: błąd').pack(side='left')
        ttk.Button(bar,text='Zapisz log…',command=self.export).pack(side='right',padx=4)
        ttk.Button(bar,text='Historia pliku',command=self.history).pack(side='right',padx=4)
        ttk.Button(bar,text='Zamknij panel',command=self.destroy).pack(side='right')
        ttk.Label(self,text='Plik: '+str(app.developer_log_path),padding=6).pack(fill='x')
        box=ttk.Frame(self);box.pack(fill='both',expand=True)
        self.text=tk.Text(box,bg='#172332',fg='#89d7ad',font=('Consolas',10),wrap='word',state='disabled')
        sb=ttk.Scrollbar(box,command=self.text.yview);sb.pack(side='right',fill='y');self.text.pack(fill='both',expand=True)
        self.text.configure(yscrollcommand=sb.set)
        for name,color in [('info','#89d7ad'),('warning','#ffda78'),('error','#ff8c8c')]:self.text.tag_configure(name,foreground=color)
        self.poll()

    def poll(self):
        pending=[r for r in self.app.developer_memory.records if r[0]>self.cursor]
        if pending:
            at_end=self.text.yview()[1]>=.98;self.text.configure(state='normal')
            for seq,level,line in pending:
                self.text.insert('end',line+'\n','error' if level>=logging.ERROR else 'warning' if level>=logging.WARNING else 'info')
                self.cursor=seq
            if int(self.text.index('end-1c').split('.')[0])>4000:self.text.delete('1.0','1000.0')
            self.text.configure(state='disabled')
            if at_end:self.text.see('end')
        self.after(350,self.poll)

    def export(self):
        path=filedialog.asksaveasfilename(parent=self.winfo_toplevel(),defaultextension='.log',initialfile='MebelStudio3D-diagnostyka.log')
        if path:
            try:Path(path).write_text('\n'.join(r[2] for r in self.app.developer_memory.records),encoding='utf-8')
            except OSError:self.app.log.exception('Nie udało się wyeksportować logu')

    def history(self):
        try:
            with self.app.developer_log_path.open('rb') as source:
                source.seek(0,2);size=source.tell();source.seek(max(0,size-200000));content=source.read().decode('utf-8',errors='replace')
            self.text.configure(state='normal');self.text.delete('1.0','end');tag='info'
            for line in content.splitlines():
                if ' ERROR ' in line or ' CRITICAL ' in line:tag='error'
                elif ' WARNING ' in line:tag='warning'
                elif ' INFO ' in line:tag='info'
                self.text.insert('end',line+'\n',tag)
            self.text.configure(state='disabled');self.text.see('end')
        except OSError:self.app.log.exception('Nie udało się odczytać historii logu')


def install(app):
    logger=logging.getLogger('MebelStudio3D.'+str(id(app)));logger.setLevel(logging.INFO);logger.propagate=False
    memory=MemoryHandler();logger.addHandler(memory)
    base=Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'MebelStudio3D'/'logi'
    app.developer_log_path=base/'MebelStudio3D.log';app.developer_memory=memory;app.log=logger
    _apps.add(app);_observe_messages()
    try:
        base.mkdir(parents=True,exist_ok=True)
        disk=RotatingFileHandler(app.developer_log_path,maxBytes=2_000_000,backupCount=3,encoding='utf-8')
        disk.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'));logger.addHandler(disk)
    except OSError:logger.warning('Plik logu niedostępny; diagnostyka pozostaje w pamięci.',exc_info=True)
    app._previous_exception_hook=sys.excepthook;app._previous_thread_hook=threading.excepthook
    def exception(kind,value,tb):
        logger.error('Nieobsłużony błąd',exc_info=(kind,value,tb))
    app._diagnostic_exception_hook=exception;sys.excepthook=exception
    app._diagnostic_thread_hook=lambda args:exception(args.exc_type,args.exc_value,args.exc_traceback)
    threading.excepthook=app._diagnostic_thread_hook
    def callback_exception(kind,value,tb):
        logger.error('Błąd funkcji interfejsu',exc_info=(kind,value,tb))
        app.status.set('Błąd: '+str(value)+' · szczegóły w panelu Logi')
    app.report_callback_exception=callback_exception
    def background_error(message):
        try:details=app.tk.getvar('errorInfo')
        except tk.TclError:details=message
        logger.error('Błąd Tcl/Tk: %s\n%s',message,details)
        app.status.set('Błąd interfejsu · szczegóły w panelu Logi')
    app.tk.createcommand('bgerror',background_error)
    def open_logs():
        doc=getattr(app,'developer_log_document',None)
        if doc is None or not doc.winfo_exists():doc=LogDocument(app);app.developer_log_document=doc
        app.workspace.select(doc);return doc
    app.open_developer_logs=open_logs
    logger.info('Uruchomiono MebelStudio3D 0.15 · Python %s · EXE %s',sys.version.split()[0],bool(getattr(sys,'frozen',False)))
    from native_model import available,diagnostics
    if available():
        native_info=diagnostics()
        logger.info('Silnik obliczeniowy C++20 aktywny · API %s · %s',native_info['api_version'],native_info['library'])
    original_changed=app.changed
    def changed():
        original_changed();logger.info('Zmieniono projekt %s · %s części',app.project.name,len(app.project.parts))
    app.changed=changed


def dispose(app):
    _apps.discard(app)
    if sys.excepthook is getattr(app,'_diagnostic_exception_hook',None):sys.excepthook=app._previous_exception_hook
    if threading.excepthook is getattr(app,'_diagnostic_thread_hook',None):threading.excepthook=app._previous_thread_hook
    for handler in list(getattr(app,'log',logging.getLogger('unused')).handlers):
        handler.close();app.log.removeHandler(handler)
