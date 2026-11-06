"""Inspection controls grouped under the View ribbon."""
import tkinter as tk
from tkinter import ttk
from viewport_modes import XRAY_MODES,DRILL_MODES


def install(app):
    page=app.ribbon_pages.get('Widok')
    if page is None:return
    ttk.Label(page,text='X-ray obejmuje:').pack(side='left',padx=(3,0))
    combo=ttk.Combobox(page,textvariable=app.xray_scope,values=XRAY_MODES,state='readonly',width=17)
    combo.pack(side='left',padx=3);combo.bind('<<ComboboxSelected>>',lambda event:app.draw())
    ttk.Label(page,text='Otwory:').pack(side='left',padx=(3,0))
    mode=ttk.Combobox(page,textvariable=app.drill_mode,values=DRILL_MODES,state='readonly',width=21)
    mode.pack(side='left',padx=3);mode.bind('<<ComboboxSelected>>',lambda event:app.draw())
    ttk.Label(page,text='● widoczne  ◌ zasłonięte',foreground='#c14160').pack(side='left',padx=4)
    app.xray_scope_combo=combo;app.drill_mode_combo=mode
