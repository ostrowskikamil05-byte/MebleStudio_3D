"""Scrollable forms with clipped content and fixed action footers."""
import tkinter as tk
from tkinter import ttk
from dock_workspace import DockDocument

class FormContent(ttk.Frame):
    def __init__(self,parent,document):
        super().__init__(parent);self.document=document
    def ensure_current_project(self,**kwargs):return self.document.ensure_current_project(**kwargs)
    def mark_clean(self):return self.document.mark_clean()
    def has_changes(self):return self.document.has_changes()
    def guard(self,callback):return self.document.guard(callback)
    def protocol(self,*args):return self.document.protocol(*args)
    def transient(self,*args):return self.document.transient(*args)
    def grab_set(self):return None
    def grab_release(self):return None
    def destroy(self):
        if self.document._destroying:super().destroy()
        else:self.document.destroy()

class FormDocument(DockDocument):
    def __init__(self,app,title,size):
        self._destroying=False
        super().__init__(app);self.title(title);self.geometry(size+'+30+30')
        self.footer=ttk.Frame(self);self.footer.pack(side='bottom',fill='x')
        self.body=ttk.Frame(self);self.body.pack(fill='both',expand=True)
        self.viewport=tk.Canvas(self.body,highlightthickness=0,bg='#f6f8fa')
        self.viewport.pack(side='left',fill='both',expand=True)
        scrollbar=ttk.Scrollbar(self.body,command=self.viewport.yview);scrollbar.pack(side='right',fill='y')
        self.viewport.configure(yscrollcommand=scrollbar.set)
        self.form=FormContent(self.viewport,self)
        self.window=self.viewport.create_window((0,0),window=self.form,anchor='nw')
        self.form.bind('<Configure>',lambda e:self.viewport.configure(scrollregion=self.viewport.bbox('all')))
        self.viewport.bind('<Configure>',lambda e:self.viewport.itemconfigure(self.window,width=e.width))
        self.after_idle(self.arrange)
    def arrange(self):
        actions=[w for w in self.form.pack_slaves() if w.winfo_class()=='TButton']
        if actions:
            primary=actions[-1];primary.pack_forget()
            # Retain its registered Tcl callback until the document closes.
            ttk.Button(self.footer,text=primary.cget('text'),command=primary.invoke,
                       style=primary.cget('style'),state=primary.cget('state')).pack(fill='x',padx=12,pady=8)
        def scroll(event):self.viewport.yview_scroll(-1 if event.delta>0 else 1,'units')
        def visit(widget):
            for child in widget.winfo_children():
                if child.winfo_class() not in ('TCombobox','Treeview','Text','Listbox'):
                    child.bind('<MouseWheel>',scroll,add='+')
                visit(child)
        visit(self.form);self.mark_clean()
    def destroy(self):
        if self._destroying:return
        self._destroying=True
        super().destroy()
