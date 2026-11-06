"""Small persistent tray; root places it beside the actual 3D canvas."""
import tkinter as tk
from tkinter import ttk
from drag_hardware import HardwareDragController
from fittings import RECIPES
from runners_catalog import PROFILES


class HardwarePlacementPanel(ttk.Frame):
    def __init__(self,parent,app):
        super().__init__(parent,padding=(8,4));self.app=app
        self.note=tk.StringVar(value='Przeciągnij kafelek na mebel · lub kliknij kafelek, potem element.')
        self.face=tk.StringVar(value='Powierzchnia kursora');self.width=tk.StringVar(value='12')
        self.axis=tk.StringVar(value='Automatycznie')
        self.length=tk.StringVar(value='');self.margin=tk.StringVar(value='30');self.price=tk.StringVar(value='0')
        self.price_unit=tk.StringVar(value='Cena [zł/szt.]')
        self.groove=tk.BooleanVar(value=False);self.count=tk.StringVar(value='2')
        self.profile=tk.StringVar(value=PROFILES['hettich_ka5632']['label'])
        self.hinge=tk.StringVar(value=next(r['name'] for r in RECIPES if r['id']=='hinge45'))
        self.controller=HardwareDragController(app,self);app.hardware_drag=self.controller
        row=ttk.Frame(self);row.pack(fill='x')
        self.details=ttk.Frame(self);self._details_visible=False
        self.details_button=ttk.Button(row,text='Parametry ▾',command=self.toggle_details)
        self.details_button.pack(side='right',padx=(5,0))
        self.tiles={}
        for kind,label in [('hinge','◉ Zawiasy'),('runner','⇆ Prowadnice'),('led','▰ LED'),
                           ('handle','▱ Uchwyt'),('lamp','☼ Lampa'),('bulb','✧ Żarówka')]:
            tile=ttk.Label(row,text=label,padding=(8,7),anchor='center',relief='ridge',cursor='hand2')
            tile.pack(side='left',fill='x',expand=True,padx=2);self.tiles[kind]=tile
            tile.bind('<ButtonPress-1>',lambda event,k=kind:self.begin(k,event))
            tile.bind('<B1-Motion>',self.controller.move);tile.bind('<ButtonRelease-1>',self.controller.end)
        # Long catalogue names live in a single shared row, never a new window.
        row=ttk.Frame(self.details);row.pack(fill='x',pady=(4,2))
        ttk.Label(row,text='Zawias:').pack(side='left')
        ttk.Combobox(row,textvariable=self.hinge,values=[r['name'] for r in RECIPES if r['id'] in ('hinge45','hinge52','hinge_sensys52')],state='readonly',width=20).pack(side='left',fill='x',expand=True,padx=3)
        ttk.Label(row,text='Prowadnica:').pack(side='left')
        ttk.Combobox(row,textvariable=self.profile,values=[r['label'] for r in PROFILES.values()],state='readonly',width=20).pack(side='left',fill='x',expand=True,padx=3)
        row=ttk.Frame(self.details);row.pack(fill='x',pady=2)
        values=['Powierzchnia kursora','Góra +T','Dół −T','Krawędź U=0','Krawędź U=max','Krawędź V=0','Krawędź V=max']
        ttk.Combobox(row,textvariable=self.face,values=values,state='readonly',width=20).pack(side='left',padx=2)
        for label,var,width in [('LED szer.',self.width,4),('Dł. auto',self.length,5),('Marg.',self.margin,4)]:
            ttk.Label(row,text=label).pack(side='left',padx=(3,1));ttk.Entry(row,textvariable=var,width=width).pack(side='left')
        row=ttk.Frame(self.details);row.pack(fill='x',pady=2)
        ttk.Label(row,text='Zawiasów').pack(side='left');ttk.Entry(row,textvariable=self.count,width=3).pack(side='left',padx=3)
        ttk.Label(row,textvariable=self.price_unit).pack(side='left',padx=3);ttk.Entry(row,textvariable=self.price,width=7).pack(side='left')
        ttk.Checkbutton(row,text='Rowek LED',variable=self.groove).pack(side='left',padx=4)
        ttk.Label(row,text='Kierunek LED:').pack(side='left',padx=(4,1))
        ttk.Combobox(row,textvariable=self.axis,values=['Automatycznie','Oś U powierzchni','Oś V powierzchni'],state='readonly',width=17).pack(side='left')
        ttk.Button(row,text='Anuluj / Esc',command=self.controller.cancel).pack(side='right')
        self.note_label=ttk.Label(self,textvariable=self.note,foreground='#176a78',wraplength=850)
        self.note_label.pack(anchor='w',pady=2)

    def toggle_details(self):
        self._details_visible=not self._details_visible
        if self._details_visible:self.details.pack(fill='x',before=self.note_label)
        else:self.details.pack_forget()
        self.details_button.configure(text='Parametry ▴' if self._details_visible else 'Parametry ▾')

    def options_for(self,kind):
        number=lambda var:float(var.get().strip().replace(',','.'))
        price=number(self.price)
        if kind=='hinge':return dict(recipe_id=next(r['id'] for r in RECIPES if r['name']==self.hinge.get()),price=price,count=int(self.count.get()))
        if kind=='runner':return dict(profile_id=next(key for key,value in PROFILES.items() if value['label']==self.profile.get()),length=number(self.length) if self.length.get().strip() else 400,price=price)
        if kind=='led':
            faces={'Góra +T':'top','Dół −T':'bottom','Krawędź U=0':'u0','Krawędź U=max':'u1','Krawędź V=0':'v0','Krawędź V=max':'v1'}
            return dict(face=faces.get(self.face.get()),width=number(self.width),length=number(self.length) if self.length.get().strip() else None,
                        margin=number(self.margin),groove=self.groove.get(),price_per_m=price,
                        axis={'Oś U powierzchni':'u','Oś V powierzchni':'v'}.get(self.axis.get(),'auto'))
        return dict(price=price)

    def begin(self,kind,event=None):
        self.price_unit.set('Cena [zł/m]' if kind=='led' else 'Cena [zł/parę]' if kind=='runner' else 'Cena [zł/zawias]' if kind=='hinge' else 'Cena [zł/szt.]')
        try:return self.controller.begin(kind,self.options_for(kind),event)
        except (ValueError,TypeError,StopIteration) as error:
            self.note.set(str(error) or 'Sprawdź parametry wyposażenia.');return 'break'

    def refresh(self):
        # There is no document-bound pending form; previews always use app.project.
        if self.controller.active:self.controller.draw_ghost()
