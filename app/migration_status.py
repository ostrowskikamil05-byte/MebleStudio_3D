"""Read-only diagnostics for the ongoing, explicitly staged C++ migration."""
import json
from pathlib import Path
import sys
import tkinter as tk
from tkinter import ttk

def open_status(app):
    existing=getattr(app,'_cpp_status_document',None)
    if existing is not None:
        try:
            if existing.winfo_exists():app.workspace.select(existing.document);return existing
        except tk.TclError:pass
    d=app.dialog('Silnik C++ i stan migracji','950x650');app._cpp_status_document=d
    ttk.Label(d,text='Przenoszenie programu do C++',style='Title.TLabel',padding=12).pack(fill='x')
    ttk.Label(d,text='Obliczenia wymienione poniżej są już dostępne w natywnej bibliotece. Interfejs oraz dokładna walidacja CAD zachowują dotychczasowe moduły. Cały program pozostaje na etapie migracji.',wraplength=850,padding=(12,0,12,8)).pack(fill='x')
    note=tk.StringVar();ttk.Label(d,textvariable=note,padding=12,wraplength=850).pack(fill='x')
    frame=ttk.Frame(d,padding=12);frame.pack(fill='both',expand=True)
    columns=('module','native','remaining')
    table=ttk.Treeview(frame,columns=columns,show='headings')
    for key,title,width in [('module','Moduł',235),('native','Operacje dostępne w C++',245),('remaining','Pozostała część modułu',350)]:
        table.heading(key,text=title);table.column(key,width=width,stretch=True)
    scroll=ttk.Scrollbar(frame,orient='vertical',command=table.yview);table.configure(yscrollcommand=scroll.set)
    scroll.pack(side='right',fill='y');table.pack(fill='both',expand=True)
    details=tk.Text(d,height=7,wrap='word',font=('Consolas',9),state='disabled');details.pack(fill='x',padx=12,pady=6)
    root=Path(sys.executable).resolve().parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent
    manifest_path=root/'MIGRACJA_CPP.json'
    def refresh():
        from native_model import diagnostics
        info=diagnostics();manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        table.delete(*table.get_children())
        for module in manifest['modules']:
            operations=module.get('cpp_operations',[])
            table.insert('','end',values=(module['name'],f'{len(operations)}: '+', '.join(operations),module['remaining']))
        note.set(f"C++20 · biblioteka aktywna · {info['calls']} wywołań natywnych · {info['batch_transforms']} transformacji części · {info['transformed_vertices']} wierzchołków")
        details.configure(state='normal');details.delete('1.0','end')
        text='Biblioteka: '+info['library']+'\nFunkcje wykonane w tej sesji: '+', '.join(sorted(info['routed_functions']))
        document=getattr(app,'_native_document',None)
        if document is not None:
            state=document.info()
            text+='\nDokument edytora C++: wersja '+str(state['revision'])+' · Cofnij: '+str(state['undo_count'])+' · Ponów: '+str(state['redo_count'])
            text+='\nPełny dokument: '+str(state['byte_length'])+' bajtów · historia: '+str(state['history_bytes'])+' bajtów'
            text+='\nWalidacja dokładnej geometrii CAD i generatory mebli pozostają w zachowanych modułach.'
        if info.get('layout_api_version') == 1:
            layout_calls=sum(value for key,value in info['routed_functions'].items() if key.startswith('layout.'))
            text+='\nUkład mebli C++: '+str(len(info['layout_operations']))+' operacji · '+str(layout_calls)+' wywołań zbiorczych'
        if info.get('worktop_api_version') == 1:
            worktop_calls=sum(value for key,value in info['routed_functions'].items() if key.startswith('workspace_worktop.'))
            text+='\nGeometria wspólnych blatów C++: '+str(len(info['worktop_operations']))+' operacje · '+str(worktop_calls)+' wywołań'
        details.insert('1.0',text)
        details.configure(state='disabled');d.mark_clean()
    bar=ttk.Frame(d,padding=12);bar.pack(fill='x')
    ttk.Button(bar,text='Odśwież diagnostykę',command=refresh).pack(side='left')
    ttk.Button(bar,text='Logi programu',command=app.open_developer_logs).pack(side='left',padx=8)
    ttk.Button(bar,text='Zamknij',command=d.destroy).pack(side='right')
    refresh();d.refresh_cpp_status=refresh;d.cpp_status_table=table
    return d
